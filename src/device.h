#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct device_info device_info_t;
struct device_info {
  char name[256];
  uint16_t port;
  bool paired;
  char internal[256];   // address on the home network (Home connection)
  char external[256];   // address of the home box on the Internet, an IP or a name (Away connection)
  char mac[18]; // XX:XX:XX:XX:XX:XX\0
};

typedef struct device_infos device_infos_t;
struct device_infos {
  int size;
  int count;
  device_info_t *devices;
};

extern device_infos_t known_devices;

/* The host scan thread reads the list while the menus may change it (add,
 * forget, rename): both take this lock around a read or a change. */
void devices_lock(void);
void devices_unlock(void);

device_info_t* find_device(const char *name);
device_info_t* append_device(device_info_t *info);
bool update_device(device_info_t *info);
void load_all_known_devices();
bool load_device_info(device_info_t *info);
void save_device_info(const device_info_t *info);
bool remove_device(const char *name);
/* Renames a saved host: its folder holds the pairing keys, so the folder is
 * renamed rather than recreated. False if the name is unusable or taken. */
bool rename_device(device_info_t *info, const char *new_name);
void device_file_path(char *out, const char *dir);
