#include <stdio.h>
#include "ui_check.h"
#include "guilib.h"
#include "../check_host.h"
#include "../debug.h"
#include <string.h>
#include "../i18n.h"

// Globals declared here so they can be cleared
extern int pending_ip_update_idx;
extern char pending_ip_update[64];

/* The scan found a saved PC at another address: ask once, then save the new
 * address (the pairing keys are stored under the PC's name and stay valid).
 * The caller restarts the host scan when it is back on the home screen; it must
 * not run here, a connection attempt usually follows.
 * Returns 1 when the address was updated. */
int ui_check_ip_update(device_info_t *info, const char *new_ip) {
    char msg[320];
    snprintf(msg, sizeof(msg),
        tr("This PC now answers from another address.\nSaved: %s\nNow: %s\nUse the new address?"),
        info->internal[0] ? info->internal : tr("(none)"), new_ip);
    if (!display_confirm(msg)) {
        vita_debug_log("[UI_CHECK] %s: address update declined", info->name);
        // The pending update is kept: the question comes back on the next attempt.
        return 0;
    }

    strncpy(info->internal, new_ip, sizeof(info->internal)-1);
    info->internal[sizeof(info->internal)-1] = '\0';
    strncpy(info->external, new_ip, sizeof(info->external)-1);
    info->external[sizeof(info->external)-1] = '\0';
    info->prefer_external = false;
    info->paired = true;
    info->port = info->port ? info->port : 47989;
    save_device_info(info);
    load_all_known_devices();
    vita_debug_log("[UI_CHECK] %s: address updated to %s", info->name, new_ip);
    pending_ip_update_idx = -1;
    pending_ip_update[0] = '\0';
    return 1;
}
