#include "route.h"

#include <stdio.h>
#include <string.h>

#include "check_host.h"

/* The four numbers of a dotted IPv4 address, or false. */
static bool parse_ipv4(const char *s, unsigned int out[4]) {
  if (s == NULL)
    return false;
  for (int part = 0; part < 4; part++) {
    unsigned int value = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
      value = value * 10 + (unsigned int)(*s - '0');
      s++;
      if (++digits > 3)
        return false;
    }
    if (digits == 0 || value > 255)
      return false;
    out[part] = value;
    if (part < 3) {
      if (*s != '.')
        return false;
      s++;
    }
  }
  return *s == '\0';
}

bool route_is_ipv4(const char *s) {
  unsigned int n[4];
  return parse_ipv4(s, n);
}

bool route_is_private_ipv4(const char *s) {
  unsigned int n[4];
  if (!parse_ipv4(s, n))
    return false;
  return n[0] == 0 || n[0] == 10 || n[0] == 127 ||
         (n[0] == 100 && n[1] >= 64 && n[1] <= 127) ||
         (n[0] == 169 && n[1] == 254) ||
         (n[0] == 172 && n[1] >= 16 && n[1] <= 31) ||
         (n[0] == 192 && n[1] == 168);
}

bool route_internet_address_usable(const char *external) {
  return external != NULL && external[0] != '\0' && !route_is_private_ipv4(external);
}

bool route_internet_address_refreshable(const char *external) {
  return external == NULL || external[0] == '\0' || route_is_ipv4(external);
}

const char *route_address(const device_info_t *info, bool away) {
  return away ? info->external : info->internal;
}

void route_set_home_address(device_info_t *info, const char *address) {
  snprintf(info->internal, sizeof(info->internal), "%s", address ? address : "");
}

void route_set_internet_address(device_info_t *info, const char *address) {
  snprintf(info->external, sizeof(info->external), "%s", address ? address : "");
}

bool route_apply_public_ip(device_info_t *info, const char *public_ip) {
  if (!route_is_ipv4(public_ip) || route_is_private_ipv4(public_ip))
    return false;
  if (!route_internet_address_refreshable(info->external) || strcmp(info->external, public_ip) == 0)
    return false;
  route_set_internet_address(info, public_ip);
  return true;
}

int route_host_state(bool away, const device_info_t *info, int scan_status, const char *current_ip, bool on_lan,
                     bool pending_update) {
  if (away) {
    if (!route_internet_address_usable(info->external))
      return HOST_STATE_NO_ADDRESS;
    /* Heard on this network: the Vita is at home, whatever the Internet
     * address answers (the box may send it back inside). */
    if (on_lan)
      return HOST_STATE_AT_HOME;
    return scan_status == HOST_ONLINE ? HOST_STATE_ONLINE : HOST_STATE_OFFLINE;
  }
  if (pending_update)
    return HOST_STATE_MOVED;
  if (current_ip && current_ip[0] && strcmp(info->internal, current_ip) != 0)
    return HOST_STATE_MOVED;
  return scan_status == HOST_ONLINE ? HOST_STATE_ONLINE : HOST_STATE_OFFLINE;
}
