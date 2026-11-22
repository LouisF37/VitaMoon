// check_host.h
#ifndef CHECK_HOST_H
#define CHECK_HOST_H

#include "device.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Possible host states
#define HOST_OFFLINE 0
#define HOST_ONLINE  1
#define HOST_IP_CHANGED 2

struct host_status {
    int status; // 0: offline, 1: online, 2: ip changed
    char current_ip[64];
    bool on_lan; // Away connection: heard over mDNS on the network the Vita is on
};

#include <psp2/kernel/threadmgr.h>
#define MAX_HOSTS 16

// Global array of host status results
extern struct host_status g_host_status[MAX_HOSTS];
extern volatile int g_host_scan_thread_status;
extern volatile int g_host_status_changed;

void start_host_scan_thread();
void stop_host_scan_thread();

/* The home box's public IPv4, found by the scan (STUN) while a PC answers at a
 * home address, for the Internet address of the PCs (route_apply_public_ip).
 * Copied into out (empty when unknown); false when unknown. */
bool host_scan_public_ip(char *out, size_t n);

#ifdef __cplusplus
}
#endif

#endif // CHECK_HOST_H
