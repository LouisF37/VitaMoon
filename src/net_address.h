#pragma once
/* Addresses on the Internet, for the Away connection (route.h). */
#include <stdbool.h>
#include <stddef.h>

/* A name (a DynDNS one, for example) turned into a dotted IPv4 address with
 * the Vita's own resolver; an IP is copied as it is. The rest of the app
 * (libcurl, moonlight-common-c) then only ever sees IPs. The system's own
 * timeout applies. False when the name is not found. */
bool net_resolve_ipv4(const char *host, char *out, size_t n);

/* The public IPv4 the home box shows to the Internet, asked to a STUN server
 * (Moonlight's own, as Moonlight on PC does). Only meaningful at home, behind
 * the box. Waits a few seconds when a server does not answer (the scan asks
 * it from a thread of its own). False when no server answered. */
bool net_find_public_ipv4(char *out, size_t n);
