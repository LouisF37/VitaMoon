/*
 * Home or Away (src/route.c): which address of a saved PC each connection
 * uses, how the two addresses are kept apart, and what the home screen says.
 */
#include "test.h"
#include "route.h"
#include "check_host.h"

#include <string.h>

static device_info_t pc(const char *internal, const char *external) {
  device_info_t d;
  memset(&d, 0, sizeof(d));
  snprintf(d.name, sizeof(d.name), "%s", "DESKTOP-CK238PP.local");
  snprintf(d.internal, sizeof(d.internal), "%s", internal);
  snprintf(d.external, sizeof(d.external), "%s", external);
  d.port = 47989;
  d.paired = true;
  return d;
}

int main(void) {
  TEST_BEGIN("route");

  /* ---- addresses ---- */
  CHECK(route_is_ipv4("192.168.1.24") && route_is_ipv4("0.0.0.0") && route_is_ipv4("255.255.255.255"),
        "dotted IPv4 addresses");
  CHECK(!route_is_ipv4("256.1.1.1") && !route_is_ipv4("1.2.3") && !route_is_ipv4("1.2.3.4.5") &&
            !route_is_ipv4("1..2.3") && !route_is_ipv4("1.2.3.4 ") && !route_is_ipv4("") && !route_is_ipv4(NULL) &&
            !route_is_ipv4("0001.2.3.4") && !route_is_ipv4("a.b.c.d"),
        "anything else is not an IPv4 address");
  CHECK(!route_is_ipv4("louis.duckdns.org") && !route_is_ipv4("DESKTOP-CK238PP.local"), "names are not IPs");
  CHECK(route_is_private_ipv4("192.168.1.24") && route_is_private_ipv4("10.0.0.5") &&
            route_is_private_ipv4("172.16.0.1") && route_is_private_ipv4("172.31.255.255") &&
            route_is_private_ipv4("169.254.3.4") && route_is_private_ipv4("127.0.0.1") &&
            route_is_private_ipv4("100.64.0.1") && route_is_private_ipv4("100.127.255.255") &&
            route_is_private_ipv4("0.1.2.3"),
        "private, shared, link-local and loopback ranges");
  CHECK(!route_is_private_ipv4("172.15.0.1") && !route_is_private_ipv4("172.32.0.1") &&
            !route_is_private_ipv4("100.63.0.1") && !route_is_private_ipv4("100.128.0.1") &&
            !route_is_private_ipv4("176.133.10.20") && !route_is_private_ipv4("8.8.8.8") &&
            !route_is_private_ipv4("louis.duckdns.org"),
        "public addresses and names are not private");
  CHECK(route_internet_address_usable("176.133.10.20") && route_internet_address_usable("louis.duckdns.org"),
        "a public IP or a name is an Internet address");
  CHECK(!route_internet_address_usable("") && !route_internet_address_usable("192.168.1.24") &&
            !route_internet_address_usable(NULL),
        "empty or a home address is no Internet address");
  CHECK(route_internet_address_refreshable("") && route_internet_address_refreshable("176.133.10.20") &&
            route_internet_address_refreshable("192.168.1.24"),
        "an empty address or an IP may be refreshed");
  CHECK(!route_internet_address_refreshable("louis.duckdns.org"), "a name typed by the user is never replaced");

  /* ---- the two addresses are kept apart ---- */
  device_info_t d = pc("192.168.1.24", "176.133.10.20");
  CHECK(!strcmp(route_address(&d, false), "192.168.1.24") && !strcmp(route_address(&d, true), "176.133.10.20"),
        "Home uses the home address, Away the Internet address");
  route_set_home_address(&d, "192.168.1.30");
  CHECK(!strcmp(d.internal, "192.168.1.30") && !strcmp(d.external, "176.133.10.20"),
        "a new home address leaves the Internet address alone");
  route_set_internet_address(&d, "louis.duckdns.org");
  CHECK(!strcmp(d.internal, "192.168.1.30") && !strcmp(d.external, "louis.duckdns.org"),
        "a new Internet address leaves the home address alone");
  route_set_internet_address(&d, "");
  CHECK(d.external[0] == '\0' && !strcmp(d.internal, "192.168.1.30"), "the Internet address can be forgotten");

  /* ---- the box's public address, found from home ---- */
  d = pc("192.168.1.24", "");
  CHECK(route_apply_public_ip(&d, "176.133.10.20") && !strcmp(d.external, "176.133.10.20"),
        "no Internet address yet: the public address becomes it");
  CHECK(!route_apply_public_ip(&d, "176.133.10.20"), "the same public address: no change, nothing to save");
  CHECK(route_apply_public_ip(&d, "176.133.99.1") && !strcmp(d.external, "176.133.99.1"),
        "the box's address changed: the Internet address follows");
  CHECK(!route_apply_public_ip(&d, "192.168.1.1") && !strcmp(d.external, "176.133.99.1"),
        "a private address is never taken as the public one");
  CHECK(!route_apply_public_ip(&d, "not an ip") && !route_apply_public_ip(&d, ""), "nonsense is refused");
  d = pc("192.168.1.24", "louis.duckdns.org");
  CHECK(!route_apply_public_ip(&d, "176.133.10.20") && !strcmp(d.external, "louis.duckdns.org"),
        "a name typed by the user is kept");
  d = pc("192.168.1.24", "192.168.1.24");
  CHECK(route_apply_public_ip(&d, "176.133.10.20") && !strcmp(d.external, "176.133.10.20") &&
            !strcmp(d.internal, "192.168.1.24"),
        "an old copy of the home address (versions up to 1.5) is replaced");

  /* ---- what the home screen says: Home ---- */
  d = pc("192.168.1.24", "176.133.10.20");
  CHECK(route_host_state(false, &d, HOST_ONLINE, "192.168.1.24", false, false) == HOST_STATE_ONLINE,
        "Home: answers at its home address");
  CHECK(route_host_state(false, &d, HOST_OFFLINE, "192.168.1.24", false, false) == HOST_STATE_OFFLINE,
        "Home: no answer");
  CHECK(route_host_state(false, &d, HOST_IP_CHANGED, "192.168.1.90", false, false) == HOST_STATE_MOVED,
        "Home: seen at another address");
  CHECK(route_host_state(false, &d, HOST_OFFLINE, "", false, true) == HOST_STATE_MOVED,
        "Home: a new address waits to be confirmed");
  CHECK(route_host_state(false, &d, HOST_ONLINE, "192.168.1.24", true, false) == HOST_STATE_ONLINE,
        "Home: the Away flag means nothing");

  /* ---- Away ---- */
  CHECK(route_host_state(true, &d, HOST_ONLINE, "", false, false) == HOST_STATE_ONLINE,
        "Away: answers over the Internet");
  CHECK(route_host_state(true, &d, HOST_OFFLINE, "", false, false) == HOST_STATE_OFFLINE,
        "Away: no answer over the Internet");
  CHECK(route_host_state(true, &d, HOST_ONLINE, "192.168.1.24", true, false) == HOST_STATE_AT_HOME,
        "Away but heard on this network: at home, even when the box answers from outside");
  CHECK(route_host_state(true, &d, HOST_OFFLINE, "192.168.1.24", true, false) == HOST_STATE_AT_HOME,
        "Away, heard at home, no answer from outside: at home");
  CHECK(route_host_state(true, &d, HOST_OFFLINE, "", false, true) == HOST_STATE_OFFLINE,
        "Away: a pending home address is not the Away connection's business");
  device_info_t none = pc("192.168.1.24", "");
  CHECK(route_host_state(true, &none, HOST_ONLINE, "", false, false) == HOST_STATE_NO_ADDRESS,
        "Away with no Internet address");
  device_info_t copied = pc("192.168.1.24", "192.168.1.24");
  CHECK(route_host_state(true, &copied, HOST_ONLINE, "", false, false) == HOST_STATE_NO_ADDRESS,
        "Away with only a copy of the home address: no Internet address");
  device_info_t named = pc("192.168.1.24", "louis.duckdns.org");
  CHECK(route_host_state(true, &named, HOST_ONLINE, "", false, false) == HOST_STATE_ONLINE, "Away with a name");

  /* ---- the PC saved by 1.5's "Change the address" (both = the public IP),
   *      back home: the scan sees it at its home address ---- */
  device_info_t louis = pc("176.133.10.20", "176.133.10.20");
  CHECK(route_host_state(false, &louis, HOST_IP_CHANGED, "192.168.1.24", false, true) == HOST_STATE_MOVED,
        "1.5's both-public PC at home: its address changed");
  route_set_home_address(&louis, "192.168.1.24");   /* what ui_check_ip_update() does on Yes */
  CHECK(!strcmp(louis.internal, "192.168.1.24") && !strcmp(louis.external, "176.133.10.20"),
        "after Yes: home address 192.168.1.24, the Internet address kept");
  CHECK(route_host_state(false, &louis, HOST_ONLINE, "192.168.1.24", false, false) == HOST_STATE_ONLINE &&
            route_host_state(true, &louis, HOST_ONLINE, "", false, false) == HOST_STATE_ONLINE,
        "then Home and Away both reach it");

  return TEST_END();
}
