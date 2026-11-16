#ifndef CHECK_DIR_H
#define CHECK_DIR_H

#define MOONLIGHT_PATH_MAX 256

void check_and_create_data_dir(char* out_path, char* out_key_dir);
/* The old folder moved by the last call, or NULL (for the log, opened later). */
const char *check_dir_migrated_from(void);

#endif // CHECK_DIR_H
