#ifndef APP_INFO_H
#define APP_INFO_H

/* Name and data folder of the app, as the code uses them. The package identity
 * (title ID, name under the LiveArea bubble, version) is set at the top of
 * CMakeLists.txt; docs/RELEASING.md lists both places. */

/* Shown in the top bar, the logs and the statistics files. */
#define APP_NAME "Lune"

/* Folder under ux0:data (or uma0:data) holding the settings, the pairing key,
 * the known PCs, the statistics and the logs. Its own folder, so Lune can be
 * installed next to Moonlight without sharing anything with it. */
#define APP_DATA_DIR "lune"

#endif // APP_INFO_H
