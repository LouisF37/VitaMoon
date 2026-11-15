#include "psp2common/types.h"
#include "stdbool.h"

bool vita_motion_init();

void *vitainput_motion_gyro_thread(void *argp);
void *vitainput_motion_accel_thread(void *argp);

void motion_process_gyro();
void motion_process_accel();