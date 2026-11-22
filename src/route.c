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

void route_clean_address(const char *in, char *out, size_t n) {
  if (n == 0)
    return;
  out[0] = '\0';
  if (in == NULL)
    return;
  while (*in == ' ' || *in == '\t')
    in++;
  size_t len = strlen(in);
  /* a port written after it: "a.b.c.d:47989", "name : 47989" */
  size_t end = len;
  while (end > 0 && (in[end - 1] == ' ' || in[end - 1] == '\t'))
    end--;
  size_t digits = end;
  while (digits > 0 && in[digits - 1] >= '0' && in[digits - 1] <= '9')
    digits--;
  size_t colon = digits;
  while (colon > 0 && (in[colon - 1] == ' ' || in[colon - 1] == '\t'))
    colon--;
  if (digits < end && colon > 0 && in[colon - 1] == ':')
    end = colon - 1;
  while (end > 0 && (in[end - 1] == ' ' || in[end - 1] == '\t'))
    end--;
  snprintf(out, n, "%.*s", (int)end, in);
}

void route_set_home_address(device_info_t *info, const char *address) {
  route_clean_address(address, info->internal, sizeof(info->internal));
}

void route_set_internet_address(device_info_t *info, const char *address) {
  route_clean_address(address, info->external, sizeof(info->external));
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
                     bool via_internet, bool pending_update) {
  if (away) {
    /* Heard on this network: the Vita is at home, whatever the Internet
     * address answers (the box may send it back inside). */
    if (on_lan)
      return HOST_STATE_AT_HOME;
    if (!route_internet_address_usable(info->external))
      return HOST_STATE_NO_ADDRESS;
    return scan_status == HOST_ONLINE ? HOST_STATE_ONLINE : HOST_STATE_OFFLINE;
  }
  if (pending_update)
    return HOST_STATE_MOVED;
  if (current_ip && current_ip[0] && strcmp(info->internal, current_ip) != 0)
    return HOST_STATE_MOVED;
  if (scan_status == HOST_ONLINE)
    return HOST_STATE_ONLINE;
  /* Silent at home, answering over the Internet: the Vita is away. */
  return via_internet ? HOST_STATE_AWAY_ONLY : HOST_STATE_OFFLINE;
}
