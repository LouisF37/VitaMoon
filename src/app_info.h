#ifndef APP_INFO_H
#define APP_INFO_H

/* Name and data folder of the app, as the code uses them. The package identity
 * (title ID, name under the LiveArea bubble, version) is set at the top of
 * CMakeLists.txt; docs/RELEASING.md lists both places. */

/* Shown in the top bar, the logs and the statistics files. */
#define APP_NAME "VitaMoon"

/* Folder under ux0:data (or uma0:data) holding the settings, the pairing key,
 * the known PCs, the statistics and the logs. Its own folder, so VitaMoon can
 * be installed next to Moonlight without sharing anything with it. */
#define APP_DATA_DIR "vitamoon"

/* The app was called Lune up to 1.0.0-beta.1: its folder is moved to the new
 * one on the first start (src/check_dir.c), with the paired PCs and settings. */
#define APP_OLD_DATA_DIR "lune"

#endif // APP_INFO_H
