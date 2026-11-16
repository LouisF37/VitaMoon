// check_host.c
// Checks the status of the hosts saved in Moonlight Vita
// Shows: green (online), red (offline), yellow (same host, new IP)

#include "check_host.h"
#include "device.h"
#include "config.h"
#include "util.h"
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
                g_host_status[i].current_ip[0] = '\0';
                continue;
            }
            // mDNS: the PC answers under its name, maybe at a new address
            scan_mdns_found = 0;
            scan_mdns_found_ip[0] = '\0';
            strncpy(scan_mdns_hostname_ref, info->name, sizeof(scan_mdns_hostname_ref)-1);
            scan_mdns_hostname_ref[sizeof(scan_mdns_hostname_ref)-1] = '\0';
            udp_sniffer_vita_set_callback(scan_mdns_found_cb);
            int mdns_ticks = 0;
            while (!scan_mdns_found && mdns_ticks < 10 && scan_running()) { // 1 s max
                udp_sniffer_vita_poll();
                sceKernelDelayThread(1000 * 100); // 100ms
                mdns_ticks++;
            }
            udp_sniffer_vita_set_callback(NULL);
            if (!scan_running())
                break;
            if (scan_mdns_found) {
                vita_debug_log("[SCAN] Host %s found over mDNS: IP=%s\n", info->name, scan_mdns_found_ip);
                if (strcmp(scan_mdns_found_ip, info->internal) == 0) {
                    if (ping_host(scan_mdns_found_ip, info->port)) {
                        if (g_host_status[i].status != HOST_ONLINE) {
                            vita_debug_log("[SCAN] Host %s ONLINE (mDNS IP same as internal)\n", info->name);
                            g_host_status_changed = 1;
                        }
                        g_host_status[i].status = HOST_ONLINE;
                        strncpy(g_host_status[i].current_ip, scan_mdns_found_ip, sizeof(g_host_status[i].current_ip)-1);
                        g_host_status[i].current_ip[sizeof(g_host_status[i].current_ip)-1] = '\0';
                        continue;
                    }
                } else {
                    if (ping_host(scan_mdns_found_ip, info->port)) {
                        if (g_host_status[i].status != HOST_IP_CHANGED) {
                            vita_debug_log("[SCAN] Host %s IP CHANGED: old=%s, new=%s\n", info->name, info->internal, scan_mdns_found_ip);
                            g_host_status_changed = 1;
                        }
                        g_host_status[i].status = HOST_IP_CHANGED;
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
                strncpy(g_host_status[i].current_ip, info->internal, sizeof(g_host_status[i].current_ip)-1);
                g_host_status[i].current_ip[sizeof(g_host_status[i].current_ip)-1] = '\0';
                continue;
            }
            if (g_host_status[i].status != HOST_OFFLINE) {
                vita_debug_log("[SCAN] Host %s OFFLINE (no answer)\n", info->name);
                g_host_status_changed = 1;
            }
            g_host_status[i].status = HOST_OFFLINE;
            strncpy(g_host_status[i].current_ip, info->internal, sizeof(g_host_status[i].current_ip)-1);
            g_host_status[i].current_ip[sizeof(g_host_status[i].current_ip)-1] = '\0';
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
    // A scan asked to stop may still be finishing (a ping in flight): two would
    // share the sniffer, so none starts until it has ended.
    if (reap_scan_threads(0) > 0) {
        vita_debug_log("[SCAN] The previous scan has not ended yet: not starting another one\n");
        return;
    }
    for (int i = 0; i < MAX_HOSTS; i++) {
        g_host_status[i].status = HOST_OFFLINE;
        g_host_status[i].current_ip[0] = '\0';
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
