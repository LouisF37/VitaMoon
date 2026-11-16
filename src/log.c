/*
 * The app's log: see log.h. The lines are formatted by the caller under a
 * lightweight lock, copied into a 64 KiB ring (log_core.c) and written to the
 * memory card by a low-priority thread, once a second or sooner when the ring
 * fills or an error is logged.
 */
#include "log.h"
#include "log_core.h"
#include "app_info.h"
#include "config.h"
#include "power/vita.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/netctl.h>
#include <psp2/power.h>
#include <psp2/rtc.h>

#include <stdio.h>
#include <string.h>

#ifndef VERSION_TEXT
#define VERSION_TEXT "dev"
#endif
#ifndef BUILD_ID
#define BUILD_ID "local"
#endif

#define LOG_KEEP 4                      /* vitamoon.1.log .. vitamoon.4.log */
#define LOG_FILE_MAX (4 * 1024 * 1024)  /* a file stops growing past this */
#define LOG_LINE_MAX 1024
#define LOG_WAKE_BYTES (16 * 1024)      /* the writer is woken past this */
#define LOG_REPEAT_REPORT_US 2000000ULL

static log_ring s_ring;
static SceKernelLwMutexWork s_lock;
static SceUID s_sema = -1;
static SceUID s_thread = -1;
static SceUID s_fd = -1;
static char s_path[256];
static unsigned s_file_bytes;
static bool s_file_full;
static volatile bool s_ready;
static volatile bool s_detailed;

/* Budgets (burst, then lines a second): errors and warnings, information and
 * detail, and moonlight-common-c's messages (a few dozen when a stream starts,
 * then loss reports that can come with every frame). */
static log_budget s_budget_alert = { .burst = 40, .per_second = 10 };
static log_budget s_budget_info = { .burst = 150, .per_second = 30 };
static log_budget s_budget_mcc = { .burst = 60, .per_second = 3 };
static log_repeat s_repeat;

/* Used under s_lock only. */
static char s_msg[LOG_LINE_MAX];
static char s_line[LOG_LINE_MAX + 64];

static const char k_level_letter[] = { 'E', 'W', 'I', 'D' };

static void clock_stamp(char *out, size_t n) {
  SceDateTime t;
  if (sceRtcGetCurrentClockLocalTime(&t) < 0) {
    snprintf(out, n, "--:--:--.---");
    return;
  }
  snprintf(out, n, "%02u:%02u:%02u.%03u", (unsigned)t.hour % 24u, (unsigned)t.minute % 60u,
           (unsigned)t.second % 60u, (unsigned)(t.microsecond / 1000u) % 1000u);
}

/* Formats one line and puts it in the ring. Under s_lock. */
static void emit_locked(const char *stamp, log_level level, const char *tag, const char *msg) {
  int len = snprintf(s_line, sizeof(s_line), "%s %c %-6s %s\n", stamp, k_level_letter[level], tag, msg);
  if (len <= 0)
    return;
  if ((size_t)len >= sizeof(s_line)) {
    len = sizeof(s_line) - 1;
    s_line[len - 1] = '\n';
  }
  log_ring_push(&s_ring, s_line, (size_t)len);
}

static void report_locked(const char *stamp, const char *fmt, uint32_t count) {
  char note[96];
  snprintf(note, sizeof(note), fmt, (unsigned)count);
  emit_locked(stamp, LOG_WARN, "log", note);
}

static void write_file(const char *data, size_t len) {
  if (s_fd < 0 || s_file_full || len == 0)
    return;
  if (s_file_bytes + len > LOG_FILE_MAX) {
    static const char note[] = "... log full: later lines are not written ...\n";
    sceIoWrite(s_fd, note, sizeof(note) - 1);
    s_file_full = true;
    return;
  }
  int ret = sceIoWrite(s_fd, data, len);
  if (ret < 0) {
    /* After a sleep the file can be closed under us: open it again once. */
    sceIoClose(s_fd);
    s_fd = sceIoOpen(s_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (s_fd >= 0)
      ret = sceIoWrite(s_fd, data, len);
  }
  if (ret > 0)
    s_file_bytes += (unsigned)ret;
}

static int writer_main(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  static char chunk[8 * 1024];
  for (;;) {
    SceUInt timeout = 1000 * 1000;
    sceKernelWaitSema(s_sema, 1, &timeout);

    /* Notes the writer owes: repeats of a flood that stopped, lines that did
     * not fit in the ring. */
    char stamp[24];
    clock_stamp(stamp, sizeof(stamp));
    sceKernelLockLwMutex(&s_lock, 1, NULL);
    uint32_t repeats = log_repeat_expire(&s_repeat, sceKernelGetProcessTimeWide(), LOG_REPEAT_REPORT_US);
    if (repeats)
      report_locked(stamp, "(previous line repeated %u more times)", repeats);
    if (s_ring.dropped) {
      uint32_t dropped = s_ring.dropped;
      s_ring.dropped = 0;
      report_locked(stamp, "%u lines lost: the log could not keep up", dropped);
    }
    sceKernelUnlockLwMutex(&s_lock, 1);

    for (;;) {
      sceKernelLockLwMutex(&s_lock, 1, NULL);
      size_t n = log_ring_pop(&s_ring, chunk, sizeof(chunk));
      sceKernelUnlockLwMutex(&s_lock, 1);
      if (n == 0)
        break;
      write_file(chunk, n);
    }
  }
  return 0;
}

static void rotate(const char *dir) {
  char from[256], to[256];
  snprintf(to, sizeof(to), "%s" APP_DATA_DIR ".%d.log", dir, LOG_KEEP);
  sceIoRemove(to);
  for (int i = LOG_KEEP - 1; i >= 1; i--) {
    snprintf(from, sizeof(from), "%s" APP_DATA_DIR ".%d.log", dir, i);
    snprintf(to, sizeof(to), "%s" APP_DATA_DIR ".%d.log", dir, i + 1);
    sceIoRename(from, to);
  }
  snprintf(from, sizeof(from), "%s" APP_DATA_DIR ".log", dir);
  snprintf(to, sizeof(to), "%s" APP_DATA_DIR ".1.log", dir);
  sceIoRename(from, to);
}

void log_init(const char *dir) {
  if (s_ready)
    return;
  log_ring_init(&s_ring);
  if (sceKernelCreateLwMutex(&s_lock, "log", 0, 0, NULL) < 0)
    return;
  rotate(dir);
  snprintf(s_path, sizeof(s_path), "%s" APP_DATA_DIR ".log", dir);
  s_fd = sceIoOpen(s_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  s_sema = sceKernelCreateSema("log_wake", 0, 0, 1, NULL);
  /* Low priority (191; 64 is the most urgent): it never delays the stream. */
  s_thread = sceKernelCreateThread("log_writer", writer_main, 191, 0x4000, 0, 0, NULL);
  if (s_sema < 0 || s_thread < 0)
    return;
  sceKernelStartThread(s_thread, 0, NULL);
  s_ready = true;

  SceDateTime t;
  sceRtcGetCurrentClockLocalTime(&t);
  LOGI("app", APP_NAME " %s, build %s, started %04d-%02d-%02d", VERSION_TEXT, BUILD_ID, t.year, t.month, t.day);
  if (s_fd < 0)
    sceClibPrintf("[" APP_NAME "] cannot open %s (0x%08x)\n", s_path, s_fd);
}

void log_set_detailed(bool on) {
  s_detailed = on;
}

bool log_detailed(void) {
  return s_detailed;
}

void log_flush(void) {
  if (s_ready)
    sceKernelSignalSema(s_sema, 1);
}

static void vwrite_budget(log_budget *budget, log_level level, const char *tag, const char *fmt, va_list va) {
  if (level == LOG_DEBUG && !s_detailed)
    return;
  if (!s_ready) {
    /* Before the log opens (very early start-up): the system console only. */
    char tmp[256];
    vsnprintf(tmp, sizeof(tmp), fmt, va);
    sceClibPrintf("[%s] %s\n", tag, tmp);
    return;
  }
  char stamp[24];
  clock_stamp(stamp, sizeof(stamp));
  uint64_t now = sceKernelGetProcessTimeWide();

  sceKernelLockLwMutex(&s_lock, 1, NULL);
  vsnprintf(s_msg, sizeof(s_msg), fmt, va);
  /* One line per call: trailing new lines go, inner ones become " | ". */
  size_t len = strlen(s_msg);
  while (len > 0 && (s_msg[len - 1] == '\n' || s_msg[len - 1] == '\r'))
    s_msg[--len] = '\0';
  for (size_t i = 0; i < len; i++)
    if (s_msg[i] == '\n' || s_msg[i] == '\r')
      s_msg[i] = '|';

  char key[LOG_REPEAT_KEY];
  snprintf(key, sizeof(key), "%s %s", tag, s_msg);
  uint32_t repeats = 0;
  if (log_repeat_check(&s_repeat, key, now, &repeats)) {
    sceKernelUnlockLwMutex(&s_lock, 1);
    return;
  }
  if (repeats)
    report_locked(stamp, "(previous line repeated %u more times)", repeats);

  uint32_t suppressed = 0;
  bool ok = log_budget_take(level <= LOG_WARN ? &s_budget_alert : budget, now, &suppressed);
  if (suppressed)
    report_locked(stamp, "%u lines left out before this one (too many at once)", suppressed);
  if (ok)
    emit_locked(stamp, level, tag, s_msg);
  bool wake = s_ring.used >= LOG_WAKE_BYTES || level == LOG_ERROR;
  sceKernelUnlockLwMutex(&s_lock, 1);

  if (wake)
    sceKernelSignalSema(s_sema, 1);
  if (s_detailed && ok)
    sceClibPrintf("%c %s %s\n", k_level_letter[level], tag, s_msg);
}

void log_vwrite(log_level level, const char *tag, const char *fmt, va_list va) {
  vwrite_budget(&s_budget_info, level, tag, fmt, va);
}

void log_write(log_level level, const char *tag, const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  vwrite_budget(&s_budget_info, level, tag, fmt, va);
  va_end(va);
}

void log_mcc(const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  vwrite_budget(&s_budget_mcc, LOG_INFO, "mcc", fmt, va);
  va_end(va);
}

void log_system_state(const char *when) {
  SceKernelFreeMemorySizeInfo mem;
  memset(&mem, 0, sizeof(mem));
  mem.size = sizeof(mem);
  int mem_ok = sceKernelGetFreeMemorySize(&mem) >= 0;

  SceNetCtlInfo info;
  char ip[16] = "-";
  int signal = -1, channel = -1, state = 0;
  sceNetCtlInetGetState(&state);
  if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) >= 0)
    snprintf(ip, sizeof(ip), "%s", info.ip_address);
  if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &info) >= 0)
    signal = (int)info.rssi_percentage;
  if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_CHANNEL, &info) >= 0)
    channel = (int)info.channel;

  LOGI("system", "%s: free memory %d KiB main, %d KiB video, %d KiB contiguous%s; battery %d %%%s",
       when, mem.size_user / 1024, mem.size_cdram / 1024, mem.size_phycont / 1024,
       mem_ok ? "" : " (not readable)", scePowerGetBatteryLifePercent(),
       scePowerIsPowerOnline() ? ", charging" : "");
  LOGI("system", "%s: Wi-Fi %s, address %s, signal %d %%, channel %d, power saving %d, Bluetooth %d",
       when, state == SCE_NETCTL_STATE_CONNECTED ? "connected" : "not connected", ip, signal, channel,
       vitapower_wifi_power_save(), vitapower_bluetooth());
}
