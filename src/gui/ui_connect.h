#include <stdbool.h>
#include "../device.h"

void ui_connect_address(char *addr);
const char *ui_connect_host_label(void);
device_info_t* ui_connect_and_pairing(device_info_t *info);
bool ui_connect_connected();

void ui_connect_resume();
/* Resumes the paused stream (offering a restart if settings that apply to a
 * new stream changed) and waits until it is paused again or ends. */
int ui_connect_resume_paused(void);
void ui_connect_manual();
void ui_connect_paired_device(device_info_t *info);
