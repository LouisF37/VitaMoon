#pragma once
/*
 * Home or Away: which address of a saved PC is used, and what the home screen
 * says about it. No Vita calls here (tools/tests/test_route.c runs it on a PC).
 *
 * A saved PC has two addresses (device.h): `internal`, its address on the home
 * network, and `external`, the address of the home box on the Internet (an IP
 * or a name such as a DynDNS one). The Connection setting (config.away) picks
 * one: Home uses only the home address, Away only the Internet address. Each
 * is changed on its own; neither is ever copied over the other.
 */
#include <stdbool.h>
#include <stddef.h>
#include "device.h"

/* What the home screen shows for a saved PC (ui_host_state()). */
enum {
  HOST_STATE_OFFLINE,     /* Home: no answer. Away: no answer over the Internet */
  HOST_STATE_ONLINE,      /* answers at the address of the current connection */
  HOST_STATE_MOVED,       /* Home: seen on the network at another address */
  HOST_STATE_AT_HOME,     /* Away: heard on the network the Vita is on (it is at home) */
  HOST_STATE_NO_ADDRESS,  /* Away: no Internet address known for it */
  HOST_STATE_AWAY_ONLY,   /* Home: no answer at home, but at its Internet address (the Vita is away) */
};

/* "a.b.c.d", each number 0..255 written with 1 to 3 digits; nothing else. */
bool route_is_ipv4(const char *s);
/* An IPv4 address that cannot be reached from the Internet: private networks
 * (10/8, 172.16/12, 192.168/16), shared carrier space (100.64/10), link-local
 * (169.254/16), loopback (127/8) and 0.0.0.0/8. */
bool route_is_private_ipv4(const char *s);
/* An Internet address to try: not empty and not a private IPv4 (older
 * versions could copy the home address there). */
bool route_internet_address_usable(const char *external);
/* The Internet address may be replaced by the one the box shows to the
 * Internet: when it is empty or an IP. A name was typed by the user: never. */
bool route_internet_address_refreshable(const char *external);

/* The saved address for a connection: external when away, internal at home. */
const char *route_address(const device_info_t *info, bool away);
/* An address as typed, cleaned: spaces around it removed, and a ":port" after
 * it too (the menus show "address : port"; the PC's port is used anyway). */
void route_clean_address(const char *in, char *out, size_t n);
/* Change one address of a PC (cleaned); the other is left as it is. */
void route_set_home_address(device_info_t *info, const char *address);
void route_set_internet_address(device_info_t *info, const char *address);
/* The box's public IPv4 (found from home): becomes the Internet address when
 * that one may be replaced. True when it changed (the caller saves the PC). */
bool route_apply_public_ip(device_info_t *info, const char *public_ip);

/* HOST_STATE_* of a saved PC.
 *   scan_status   HOST_OFFLINE / HOST_ONLINE / HOST_IP_CHANGED of the scan (check_host.h)
 *   current_ip    Home: the address the scan last saw it at ("" if none)
 *   on_lan        Away: the scan heard it on the network the Vita is on, or it
 *                 answered at its home address
 *   via_internet  Home: no answer at home, an answer at its Internet address
 *   pending_update Home: the scan offers a new home address for it */
int route_host_state(bool away, const device_info_t *info, int scan_status, const char *current_ip, bool on_lan,
                     bool via_internet, bool pending_update);
