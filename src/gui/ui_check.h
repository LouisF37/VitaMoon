#pragma once
#include "../device.h"

// Shows the dialog and updates the IP if the user accepts.
// Returns 1 if updated, 0 otherwise.
int ui_check_ip_update(device_info_t *info, const char *new_ip);
