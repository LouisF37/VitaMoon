#include "net_address.h"

#include <stdio.h>
#include <string.h>

#include <psp2/net/net.h>

#include "Limelight.h"
#include "log.h"
#include "route.h"

/* The resolver's wait for one answer, and how many times it asks. */
#define RESOLVE_TIMEOUT_US (2 * 1000 * 1000)
#define RESOLVE_RETRIES 2

bool net_resolve_ipv4(const char *host, char *out, size_t n) {
  if (n > 0)
    out[0] = '\0';
  if (host == NULL || host[0] == '\0' || n < 16)
    return false;
  if (route_is_ipv4(host)) {
    snprintf(out, n, "%s", host);
    return true;
  }
  int rid = sceNetResolverCreate("vitamoon_dns", NULL, 0);
  if (rid < 0) {
    LOGW("net", "no name resolver (0x%08X): %s not looked up", (unsigned int)rid, host);
    return false;
  }
  SceNetInAddr addr;
  memset(&addr, 0, sizeof(addr));
  int ret = sceNetResolverStartNtoa(rid, host, &addr, RESOLVE_TIMEOUT_US, RESOLVE_RETRIES, 0);
  sceNetResolverDestroy(rid);
  if (ret < 0) {
    LOGW("net", "%s: name not found (0x%08X)", host, (unsigned int)ret);
    return false;
  }
  /* s_addr is in network order: its first byte is the first number */
  const unsigned char *b = (const unsigned char *)&addr.s_addr;
  snprintf(out, n, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
  LOGI("net", "%s is %s", host, out);
  return true;
}

bool net_find_public_ipv4(char *out, size_t n) {
  /* Moonlight's own server first (moonlight-qt asks it too), then a public one. */
  static const char *const servers[] = {"stun.moonlight-stream.org", "stun.stunprotocol.org"};
  if (n > 0)
    out[0] = '\0';
  if (n < 16)
    return false;
  for (size_t i = 0; i < sizeof(servers) / sizeof(servers[0]); i++) {
    /* resolved here: moonlight-common-c's own lookup goes through getaddrinfo */
    char server_ip[16];
    if (!net_resolve_ipv4(servers[i], server_ip, sizeof(server_ip)))
      continue;
    unsigned int wan = 0;
    int err = LiFindExternalAddressIP4(server_ip, 3478, &wan);
    if (err != 0) {
      LOGW("net", "STUN %s (%s): no answer (%d)", servers[i], server_ip, err);
      continue;
    }
    const unsigned char *b = (const unsigned char *)&wan;   /* network order */
    snprintf(out, n, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    LOGI("net", "public address of the box: %s (STUN %s)", out, servers[i]);
    return true;
  }
  return false;
}
