#pragma once

#include <psp2/types.h>
#include <stdbool.h>

/*
 * Where the streaming threads run (setting "Spread work over the cores").
 *
 * The Vita gives applications three cores. With the setting on, the work is
 * split the way dubleyu/vita-moonlight-relay does it:
 *   core 0  drawing and display, input, the menus
 *   core 1  network reception (video, audio, control)
 *   core 2  video and audio decoding, audio output
 * With it off, threads may run on any core (the system's choice), as before.
 */
enum { CORE_UI = 0, CORE_NET = 1, CORE_MEDIA = 2 };

/* Places a thread on one core when the setting is on, on any core otherwise.
 * thid 0 means the calling thread. */
void thread_place(SceUID thid, int core);

/* Highest priority given to the threads of the stream's critical path
 * (lower number = higher priority; 64 is the highest for applications). */
#define STREAM_THREAD_PRIORITY 64
