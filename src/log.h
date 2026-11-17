/*
 * The app's log: <data folder>/logs/vitamoon-<YYYYMMDD-HHMMSS>.log, one file per
 * run of the app (log_files.h), always written.
 *
 * The normal log keeps what happens (start-up, PCs, streams and how they end,
 * reconnections, errors) at a few lines a minute; the detailed log (setting
 * "Detailed log") adds every debug line. Callers only format a line and copy
 * it into a memory ring: a low-priority thread writes it to the memory card,
 * so a stream thread never waits for the card. The logs of the last 30 runs
 * are kept.
 *
 * Line format: "14:03:27.512 I stream  message" (local time; E error,
 * W warning, I information, D detail).
 */
#pragma once

#include <stdarg.h>
#include <stdbool.h>

typedef enum { LOG_ERROR = 0, LOG_WARN, LOG_INFO, LOG_DEBUG } log_level;

/* Opens this run's file in <dir>logs/ (after deleting the oldest) and starts
 * the writer. Lines logged before are lost. dir ends with '/'. */
void log_init(const char *dir);
/* Detail lines (LOG_DEBUG) written or not; also echoed to the system console. */
void log_set_detailed(bool on);
bool log_detailed(void);
/* Wakes the writer so that everything logged so far reaches the card soon. */
void log_flush(void);

void log_write(log_level level, const char *tag, const char *fmt, ...)
    __attribute__((format(__printf__, 3, 4)));
void log_vwrite(log_level level, const char *tag, const char *fmt, va_list va);

#define LOGE(tag, ...) log_write(LOG_ERROR, tag, __VA_ARGS__)
#define LOGW(tag, ...) log_write(LOG_WARN, tag, __VA_ARGS__)
#define LOGI(tag, ...) log_write(LOG_INFO, tag, __VA_ARGS__)
#define LOGD(tag, ...) log_write(LOG_DEBUG, tag, __VA_ARGS__)

/* moonlight-common-c's log callback (connection.c): information lines, with a
 * budget of their own so that a burst of loss messages cannot crowd out the rest. */
void log_mcc(const char *fmt, ...) __attribute__((format(__printf__, 1, 2)));

/* Device state worth a line at start-up and around each stream: firmware,
 * model, free memory, battery, Wi-Fi. `when` names the moment. */
void log_system_state(const char *when);
