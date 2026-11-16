/*
 * mDNS search (third_party/mdnsniff/udp_sniffer_vita.c): the packets it reads
 * come from anything on the network. A Sunshine answer is understood; damaged,
 * cut or forged packets (a name that points to itself, a record longer than
 * the packet, random bytes) neither hang nor crash it.
 */
#include "test.h"

#include <arpa/inet.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <psp2/net/net.h>
#include "udp_sniffer_vita.h"

/* ---------------- the network, as the sniffer sees it ---------------- */

static const unsigned char *next_packet;
static int next_len;

int sceNetSocket(const char *name, int domain, int type, int protocol) { return 3; }
int sceNetBind(int s, const SceNetSockaddr *addr, unsigned int addrlen) { return 0; }
int sceNetInetPton(int af, const char *src, void *dst) { return inet_pton(AF_INET, src, dst); }
int sceNetSetsockopt(int s, int level, int optname, const void *optval, unsigned int optlen) { return 0; }
int sceNetSendto(int s, const void *msg, unsigned int len, int flags, const SceNetSockaddr *to, unsigned int tolen) {
  return (int)len;
}
int sceNetRecvfrom(int s, void *buf, unsigned int len, int flags, SceNetSockaddr *from, unsigned int *fromlen) {
  if (!next_packet)
    return -1;
  int n = next_len < (int)len ? next_len : (int)len;
  memcpy(buf, next_packet, n);
  next_packet = NULL;
  return n;
}
int sceNetSocketClose(int s) { return 0; }
unsigned int sceNetHtonl(unsigned int host32) { return htonl(host32); }
unsigned short int sceNetHtons(unsigned short int host16) { return htons(host16); }
unsigned int sceKernelGetProcessTimeLow(void) { return 0; }
int sceClibPrintf(const char *fmt, ...) { return 0; }

static int found = 0;
static char found_name[256], found_ip[64];
static int found_port = 0;

static void on_found(int idx, const char *host, const char *pcname, const char *ip, int port) {
  found++;
  snprintf(found_name, sizeof(found_name), "%s", pcname);
  snprintf(found_ip, sizeof(found_ip), "%s", ip);
  found_port = port;
}

/* One packet through the sniffer, as one poll of the search. */
static void deliver(const unsigned char *pkt, int len) {
  next_packet = pkt;
  next_len = len;
  udp_sniffer_vita_poll();
}

/* ---------------- packets ---------------- */

static int put_name(unsigned char *p, const char *dotted) {
  int n = 0;
  while (*dotted) {
    const char *dot = strchr(dotted, '.');
    int l = dot ? (int)(dot - dotted) : (int)strlen(dotted);
    p[n++] = (unsigned char)l;
    memcpy(p + n, dotted, l);
    n += l;
    dotted += l + (dot ? 1 : 0);
  }
  p[n++] = 0;
  return n;
}

static int put_rr(unsigned char *p, const char *name, int type, const unsigned char *rdata, int rdlen) {
  int n = put_name(p, name);
  p[n++] = 0; p[n++] = (unsigned char)type;   /* type */
  p[n++] = 0x80; p[n++] = 1;                  /* class IN, cache flush */
  p[n++] = 0; p[n++] = 0; p[n++] = 0x11; p[n++] = 0x94;   /* TTL */
  p[n++] = (unsigned char)(rdlen >> 8); p[n++] = (unsigned char)rdlen;
  memcpy(p + n, rdata, rdlen);
  return n + rdlen;
}

/* What Sunshine announces: PTR, SRV (port 47989) and A (192.168.1.24). */
static int sunshine_answer(unsigned char *p) {
  static const unsigned char header[12] = {0, 0, 0x84, 0, 0, 0, 0, 3, 0, 0, 0, 0};
  int n = 0;
  memcpy(p, header, 12);
  n = 12;
  unsigned char rd[300];
  int l = put_name(rd, "MINI-PC._nvstream._tcp.local");
  n += put_rr(p + n, "_nvstream._tcp.local", 12, rd, l);
  rd[0] = 0; rd[1] = 0; rd[2] = 0; rd[3] = 0; rd[4] = 47989 >> 8; rd[5] = 47989 & 0xFF;
  l = 6 + put_name(rd + 6, "MINI-PC.local");
  n += put_rr(p + n, "MINI-PC._nvstream._tcp.local", 33, rd, l);
  const unsigned char a[4] = {192, 168, 1, 24};
  n += put_rr(p + n, "MINI-PC.local", 1, a, 4);
  return n;
}

/* ---------------- a hang is a failure ---------------- */

static sigjmp_buf hang_jump;
static void on_alarm(int sig) { siglongjmp(hang_jump, 1); }

/* Delivers a packet; false if the sniffer did not come back within a second. */
static bool delivered_in_time(const unsigned char *pkt, int len) {
  if (sigsetjmp(hang_jump, 1)) {
    alarm(0);
    return false;
  }
  alarm(1);
  deliver(pkt, len);
  alarm(0);
  return true;
}

int main(void) {
  signal(SIGALRM, on_alarm);
  TEST_BEGIN("mDNS search");

  unsigned char good[1024];
  int good_len = sunshine_answer(good);

  udp_sniffer_vita_deinit();
  udp_sniffer_vita_init();
  udp_sniffer_vita_set_callback(on_found);
  deliver(good, good_len);
  CHECK(found == 1, "a Sunshine answer is understood");
  CHECK(!strcmp(found_ip, "192.168.1.24") && found_port == 47989, "its address and port");
  CHECK(!strcmp(found_name, "MINI-PC.local"), "its name");

  /* a name made of a pointer to itself, in the question and in an answer */
  unsigned char loop[64] = {0, 0, 0x84, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0xC0, 12, 0, 12, 0, 1, 0xC0, 18, 0, 12, 0, 1,
                            0, 0, 0, 10, 0, 2, 0xC0, 30};
  CHECK(delivered_in_time(loop, 32), "a name that points to itself does not hang the search");

  /* two pointers to each other */
  unsigned char pair[40] = {0, 0, 0x84, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0xC0, 14, 0xC0, 12, 0, 12, 0, 1, 0, 0, 0, 10, 0, 2,
                            0xC0, 12};
  CHECK(delivered_in_time(pair, 28), "two names pointing to each other do not hang it");

  /* every cut of a good answer */
  bool cuts_ok = true;
  for (int len = 1; len < good_len && cuts_ok; len++)
    cuts_ok = delivered_in_time(good, len);
  CHECK(cuts_ok, "an answer cut anywhere is read without hanging");

  /* a record claiming more data than the packet holds */
  unsigned char longer[1024];
  memcpy(longer, good, good_len);
  longer[good_len - 6] = 0x7F;   /* rdlength of the A record */
  CHECK(delivered_in_time(longer, good_len), "a record longer than its packet is not read past it");

  /* random bytes, and good answers with random bytes changed */
  srand(1);
  bool fuzz_ok = true;
  unsigned char pkt[1024];
  for (int i = 0; i < 20000 && fuzz_ok; i++) {
    int len;
    if (i % 2) {
      len = 12 + rand() % 500;
      for (int k = 0; k < len; k++)
        pkt[k] = (unsigned char)rand();
      pkt[5] = (unsigned char)(rand() % 4);   /* a few questions and records */
      pkt[7] = (unsigned char)(rand() % 8);
    } else {
      len = good_len;
      memcpy(pkt, good, len);
      for (int k = rand() % 6; k >= 0; k--)
        pkt[rand() % len] = (unsigned char)rand();
    }
    fuzz_ok = delivered_in_time(pkt, len);
  }
  CHECK(fuzz_ok, "20000 random and damaged packets: no hang, no crash");

  udp_sniffer_vita_deinit();
  return TEST_END();
}
