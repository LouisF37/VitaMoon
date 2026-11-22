// check_host.c
// Checks the status of the hosts saved in Moonlight Vita
// Shows: green (online), red (offline), yellow (same host, new IP)
// Home connection: each PC is looked for on the network (mDNS), then at its
// home address. Away connection: each PC is listened for on the network all
// the same (heard there, the Vita is at home), then tried at its Internet
// address (route.h). From home, the box's public address is also asked for
// once in a while (STUN), for the PCs' Internet address.

#include "check_host.h"
#include "device.h"
#include "config.h"
#include "util.h"
#include "route.h"
#include "net_address.h"
#include "log.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include "debug.h"

#ifdef __vita__
#include "udp_sniffer_vita.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#define MAX_HOSTS 16

/* 1 while the scan runs, 2 when it is asked to stop, 0 when it is over. */
volatile int g_host_scan_thread_status = 0;
struct host_status g_host_status[MAX_HOSTS];

/* Scan threads not deleted yet: the one running, and any that was asked to stop
 * but had not ended by then (deleted once it has). One runs at a time: they
 * share the mDNS sniffer and the variables below. */
#define MAX_SCAN_THREADS 8
static SceUID host_scan_thread_ids[MAX_SCAN_THREADS];
static int host_scan_thread_count = 0;

// Forward declaration
static bool ping_host(const char *ip, uint16_t port);

static volatile int scan_mdns_found = 0;
static char scan_mdns_found_ip[64] = "";
static char scan_mdns_hostname_ref[256] = "";

static void scan_mdns_found_cb(int idx, const char* host, const char* pcname, const char* ip, int port) {
    if (strcmp(pcname, scan_mdns_hostname_ref) == 0) {
        strncpy((char*)scan_mdns_found_ip, ip, sizeof(scan_mdns_found_ip)-1);
        scan_mdns_found = 1;
    }
}

volatile int g_host_status_changed = 0;

/* The box's public IPv4 (STUN), under devices_lock(); asked again at most
 * every PUBLIC_IP_EVERY_US, and only while a PC answers at a home address.
 * Asked in a thread of its own: a slow name server or STUN server must not
 * hold the scan (the menus wait for the scan to stop before they go on). */
#define PUBLIC_IP_EVERY_US (10 * 60 * 1000000ULL)
static char public_ip[16] = "";
static SceUInt64 public_ip_asked_us = 0;
static bool public_ip_asked = false;
static volatile int public_ip_busy = 0;

bool host_scan_public_ip(char *out, size_t n) {
    devices_lock();
    snprintf(out, n, "%s", public_ip);
    devices_unlock();
    return out[0] != '\0';
}

static int public_ip_thread(SceSize args, void *argp) {
    char ip[16];
    if (net_find_public_ipv4(ip, sizeof(ip))) {
        devices_lock();
        snprintf(public_ip, sizeof(public_ip), "%s", ip);
        devices_unlock();
        g_host_status_changed = 1;   /* the menus apply it to the PCs seen at home */
    }
    public_ip_busy = 0;
    return sceKernelExitDeleteThread(0);
}

static void ask_public_ip(void) {
    SceUInt64 now = sceKernelGetProcessTimeWide();
    if (public_ip_busy || (public_ip_asked && now - public_ip_asked_us < PUBLIC_IP_EVERY_US))
        return;
    public_ip_asked = true;
    public_ip_asked_us = now;
    public_ip_busy = 1;
    SceUID tid = sceKernelCreateThread("publicip", public_ip_thread, 0x10000100, 0x10000, 0, 0, NULL);
    if (tid < 0 || sceKernelStartThread(tid, 0, NULL) < 0) {
        if (tid >= 0)
            sceKernelDeleteThread(tid);
        public_ip_busy = 0;
    }
}

/* Writes a host's status; flags a change for the menus when it differs. */
static void set_status(int i, int status, const char *ip, bool on_lan, bool via_internet, const char *why,
                       const char *name) {
    if (g_host_status[i].status != status || g_host_status[i].on_lan != on_lan ||
        g_host_status[i].via_internet != via_internet) {
        vita_debug_log("[SCAN] Host %s: %d%s%s (%s)\n", name, status, on_lan ? ", on this network" : "",
                       via_internet ? ", over the Internet only" : "", why);
        g_host_status_changed = 1;
    }
    g_host_status[i].status = status;
    g_host_status[i].on_lan = on_lan;
    g_host_status[i].via_internet = via_internet;
    snprintf(g_host_status[i].current_ip, sizeof(g_host_status[i].current_ip), "%s", ip ? ip : "");
}

/* Each PC's Internet address as an IP: looked up at the first try of a scan,
 * then again when it stops answering, 30 s apart at most (a DynDNS name's IP
 * may change; a name server that does not answer is not asked every round). */
#define INTERNET_LOOKUP_EVERY_US (30 * 1000000ULL)
static char internet_ip[MAX_HOSTS][16];
static SceUInt64 internet_lookup_us[MAX_HOSTS];

static bool scan_running(void);

/* True when the PC answers at its Internet address. */
static bool internet_answers(int i, const device_info_t *info) {
    if (!route_internet_address_usable(info->external))
        return false;
    SceUInt64 now = sceKernelGetProcessTimeWide();
    if (internet_ip[i][0] == '\0') {
        if (internet_lookup_us[i] != 0 && now - internet_lookup_us[i] < INTERNET_LOOKUP_EVERY_US)
            return false;
        internet_lookup_us[i] = now;
        if (!net_resolve_ipv4(info->external, internet_ip[i], sizeof(internet_ip[i]))) {
            internet_ip[i][0] = '\0';
            return false;
        }
    }
    if (!scan_running())
        return false;
    bool answers = ping_host(internet_ip[i], info->port);
    if (!answers && now - internet_lookup_us[i] >= INTERNET_LOOKUP_EVERY_US)
        internet_ip[i][0] = '\0';   /* looked up again next round */
    return answers;
}

static bool scan_running(void) {
    return g_host_scan_thread_status == 1;
}

/* Sleeps up to ms, in steps short enough to stop soon when asked. */
static void scan_sleep(int ms) {
    for (int t = 0; t < ms && scan_running(); t += 100)
        sceKernelDelayThread(100 * 1000);
}

static int host_scan_thread(SceSize args, void *argp) {
    int ticks = 0;
    vita_debug_log("[SCAN] Scan thread started\n");
    memset(internet_ip, 0, sizeof(internet_ip));
    memset(internet_lookup_us, 0, sizeof(internet_lookup_us));
    udp_sniffer_vita_deinit();
    udp_sniffer_vita_init();
    while (scan_running() && ticks < 1000) { // 1000 rounds: several minutes
        for (int i = 0; scan_running(); i++) {
            /* A copy of the host: the menus may change the list meanwhile. */
            device_info_t host;
            devices_lock();
            bool more = i < known_devices.count && i < MAX_HOSTS;
            if (more)
                host = known_devices.devices[i];
            devices_unlock();
            if (!more)
                break;
            device_info_t *info = &host;
            if (!info->paired) {
                if (g_host_status[i].status != HOST_OFFLINE) {
                    vita_debug_log("[SCAN] Host %s marked OFFLINE (not paired)\n", info->name);
                    g_host_status_changed = 1;
                }
                g_host_status[i].status = HOST_OFFLINE;
                g_host_status[i].on_lan = false;
                g_host_status[i].via_internet = false;
                g_host_status[i].current_ip[0] = '\0';
                continue;
            }
            // mDNS: the PC answers under its name, maybe at a new address
            scan_mdns_found = 0;
            scan_mdns_found_ip[0] = '\0';
            strncpy(scan_mdns_hostname_ref, info->name, sizeof(scan_mdns_hostname_ref)-1);
            scan_mdns_hostname_ref[sizeof(scan_mdns_hostname_ref)-1] = '\0';
            udp_sniffer_vita_set_callback(scan_mdns_found_cb);
            // The PCs answer each question once: ask again for each PC, or only
            // the first round of a scan would hear them.
            udp_sniffer_vita_query();
            int mdns_ticks = 0;
            while (!scan_mdns_found && mdns_ticks < 10 && scan_running()) { // 1 s max
                udp_sniffer_vita_poll();
                sceKernelDelayThread(1000 * 100); // 100ms
                mdns_ticks++;
            }
            udp_sniffer_vita_set_callback(NULL);
            if (!scan_running())
                break;
            if (config.away) {
                // Away. Heard on this network, or answering at its home address
                // (a private one: a public one answers from anywhere): the Vita
                // is at home, whatever the Internet address answers (the box may
                // send it back inside). The address kept is the one it was found
                // at here (a switch back to Home offers it as the home address).
                bool heard = scan_mdns_found != 0;
                bool home_answers = !heard && route_is_private_ipv4(info->internal) &&
                                    ping_host(info->internal, info->port);
                if (!scan_running())
                    break;
                if (heard || home_answers) {
                    set_status(i, HOST_ONLINE, heard ? (const char *)scan_mdns_found_ip : info->internal, true, false,
                               heard ? "heard on this network" : "answers at its home address", info->name);
                    continue;
                }
                if (!route_internet_address_usable(info->external)) {
                    set_status(i, HOST_OFFLINE, "", false, false, "no Internet address", info->name);
                    continue;
                }
                bool answers = internet_answers(i, info);
                if (!scan_running())
                    break;
                set_status(i, answers ? HOST_ONLINE : HOST_OFFLINE, "", false, false,
                           answers ? "answers over the Internet" : "no answer over the Internet", info->name);
                continue;
            }
            if (scan_mdns_found) {
                vita_debug_log("[SCAN] Host %s found over mDNS: IP=%s\n", info->name, scan_mdns_found_ip);
                if (strcmp(scan_mdns_found_ip, info->internal) == 0) {
                    if (ping_host(scan_mdns_found_ip, info->port)) {
                        if (g_host_status[i].status != HOST_ONLINE) {
                            vita_debug_log("[SCAN] Host %s ONLINE (mDNS IP same as internal)\n", info->name);
                            g_host_status_changed = 1;
                        }
                        g_host_status[i].status = HOST_ONLINE;
                        g_host_status[i].on_lan = false;
                        g_host_status[i].via_internet = false;
                        strncpy(g_host_status[i].current_ip, scan_mdns_found_ip, sizeof(g_host_status[i].current_ip)-1);
                        g_host_status[i].current_ip[sizeof(g_host_status[i].current_ip)-1] = '\0';
                        if (route_is_private_ipv4(scan_mdns_found_ip))
                            ask_public_ip();
                        continue;
                    }
                } else {
                    if (ping_host(scan_mdns_found_ip, info->port)) {
                        if (g_host_status[i].status != HOST_IP_CHANGED) {
                            vita_debug_log("[SCAN] Host %s IP CHANGED: old=%s, new=%s\n", info->name, info->internal, scan_mdns_found_ip);
                            g_host_status_changed = 1;
                        }
                        g_host_status[i].status = HOST_IP_CHANGED;
                        g_host_status[i].on_lan = false;
                        g_host_status[i].via_internet = false;
                        strncpy(g_host_status[i].current_ip, scan_mdns_found_ip, sizeof(g_host_status[i].current_ip)-1);
                        g_host_status[i].current_ip[sizeof(g_host_status[i].current_ip)-1] = '\0';
                        // Set the global flags for the menu
                        extern int pending_ip_update_idx;
                        extern char pending_ip_update[64];
                        pending_ip_update_idx = i;
                        strncpy(pending_ip_update, scan_mdns_found_ip, sizeof(pending_ip_update)-1);
                        pending_ip_update[sizeof(pending_ip_update)-1] = '\0';
                        // The menu offers the new address: the scan stops here
                        vita_debug_log("[SCAN] IP change detected, stopping the scan thread and the sniffer\n");
                        udp_sniffer_vita_deinit();
                        g_host_scan_thread_status = 0;
                        return 0;
                    }
                }
            }
            if (!scan_running())
                break;
            if (ping_host(info->internal, info->port)) {
                if (g_host_status[i].status != HOST_ONLINE) {
                    vita_debug_log("[SCAN] Host %s ONLINE (internal IP)\n", info->name);
                    g_host_status_changed = 1;
                }
                g_host_status[i].status = HOST_ONLINE;
                g_host_status[i].on_lan = false;
                g_host_status[i].via_internet = false;
                strncpy(g_host_status[i].current_ip, info->internal, sizeof(g_host_status[i].current_ip)-1);
                g_host_status[i].current_ip[sizeof(g_host_status[i].current_ip)-1] = '\0';
                if (route_is_private_ipv4(info->internal))
                    ask_public_ip();
                continue;
            }
            // Silent at home: if it answers at its Internet address, the Vita is
            // away (the menus offer the Away connection). Not when it was heard
            // here, nor when its home address is a public one (tried just above).
            bool via_internet = !scan_mdns_found && route_is_private_ipv4(info->internal) &&
                                internet_answers(i, info);
            if (!scan_running())
                break;
            set_status(i, HOST_OFFLINE, info->internal, false, via_internet,
                       via_internet ? "no answer at home, answers over the Internet" : "no answer", info->name);
        }
        scan_sleep(500);
        ticks++;
    }
    udp_sniffer_vita_deinit();
    vita_debug_log("[SCAN] Scan thread stopped\n");
    g_host_scan_thread_status = 0;
    return 0;
}

/* Deletes the scan threads that have ended, waiting up to wait_us for each;
 * the others are kept for a later call. Returns how many are left. */
static int reap_scan_threads(SceUInt wait_us) {
    int kept = 0;
    for (int i = 0; i < host_scan_thread_count; i++) {
        SceUInt timeout = wait_us;
        if (sceKernelWaitThreadEnd(host_scan_thread_ids[i], NULL, &timeout) >= 0)
            sceKernelDeleteThread(host_scan_thread_ids[i]);
        else
            host_scan_thread_ids[kept++] = host_scan_thread_ids[i];
    }
    host_scan_thread_count = kept;
    return kept;
}

void start_host_scan_thread() {
    if (g_host_scan_thread_status == 1) return;
    // What the last scan found may be from the other connection (Home, Away):
    // forgotten before anything else, even when no scan can start yet.
    for (int i = 0; i < MAX_HOSTS; i++) {
        g_host_status[i].status = HOST_OFFLINE;
        g_host_status[i].on_lan = false;
        g_host_status[i].via_internet = false;
        g_host_status[i].current_ip[0] = '\0';
    }
    // A scan asked to stop may still be finishing (a ping in flight): two would
    // share the sniffer, so none starts until it has ended.
    if (reap_scan_threads(0) > 0) {
        vita_debug_log("[SCAN] The previous scan has not ended yet: not starting another one\n");
        return;
    }
    // Running before the thread starts, so that a stop right after is not undone.
    g_host_scan_thread_status = 1;
    SceUID tid = sceKernelCreateThread("hostscan", host_scan_thread, 0x10000100, 0x10000, 0, 0, NULL);
    if (tid < 0 || sceKernelStartThread(tid, 0, 0) < 0) {
        if (tid >= 0)
            sceKernelDeleteThread(tid);
        g_host_scan_thread_status = 0;
        return;
    }
    host_scan_thread_ids[host_scan_thread_count++] = tid;
}

void stop_host_scan_thread() {
    if (g_host_scan_thread_status == 1)
        g_host_scan_thread_status = 2;
    // It notices within 100 ms, or once the ping in flight returns: wait for
    // it, so that it no longer uses the network or the sniffer after this.
    reap_scan_threads(2000 * 1000);
    if (host_scan_thread_count > 0)
        vita_debug_log("[SCAN] The scan did not end within 2 s; it will be deleted once it has\n");
}
#endif

// struct host_status and the defines live in check_host.h

// Tries to connect to the host's IP and port; true if it answers
static bool ping_host(const char *ip, uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        close(sock);
        return false;
    }
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
    int res = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
    return res == 0;
}
