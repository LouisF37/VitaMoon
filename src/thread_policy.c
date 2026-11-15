/*
 * Thread placement and priorities for streaming. See thread_policy.h.
 */
#include "thread_policy.h"
#include "config.h"
#include "debug.h"

#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>
#include <string.h>

static const int k_core_masks[3] = {
  SCE_KERNEL_CPU_MASK_USER_0, SCE_KERNEL_CPU_MASK_USER_1, SCE_KERNEL_CPU_MASK_USER_2,
};

void thread_place(SceUID thid, int core) {
  if (thid == 0)
    thid = sceKernelGetThreadId();
  int mask = config.thread_pinning && core >= 0 && core <= 2 ? k_core_masks[core]
                                                             : SCE_KERNEL_CPU_MASK_USER_ALL;
  int ret = sceKernelChangeThreadCpuAffinityMask(thid, mask);
  if (ret < 0)
    vita_debug_log("thread 0x%x: affinity 0x%x refused (0x%x)\n", thid, mask, ret);
}

/*
 * Called by moonlight-common-c on each thread it starts (third_party patch
 * 0003), from that thread. Receiving and decoding get the highest priority, as
 * the receive thread already had before; with "Spread work over the cores" on,
 * reception goes to core 1 and decoding to core 2.
 */
void LiVitaThreadStarted(const char *name) {
  if (name == NULL)
    return;
  int core = -1;
  bool critical = false;
  if (strcmp(name, "VideoRecv") == 0 || strcmp(name, "AudioRecv") == 0) {
    core = CORE_NET;
    critical = true;
  } else if (strcmp(name, "ControlRecv") == 0 || strcmp(name, "VideoPing") == 0 ||
             strcmp(name, "AudioPing") == 0) {
    core = CORE_NET;
  } else if (strcmp(name, "VideoDec") == 0 || strcmp(name, "AudioDec") == 0) {
    core = CORE_MEDIA;
    critical = true;
  } else if (strcmp(name, "InputSend") == 0) {
    core = CORE_UI;
  }

  if (critical)
    sceKernelChangeThreadPriority(sceKernelGetThreadId(), STREAM_THREAD_PRIORITY);
  if (core >= 0)
    thread_place(0, core);
  vita_debug_log("thread %s: core %d%s, priority %s\n", name, config.thread_pinning ? core : -1,
                 config.thread_pinning ? "" : " (any)", critical ? "64" : "default");
}
