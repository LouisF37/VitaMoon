cmake_minimum_required(VERSION 3.2)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
	set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
	message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()
add_compile_options(-g -Wall -Wextra -Wno-unused-parameter -Wno-pointer-sign -Wno-sign-compare -Wno-switch)


project(vitamoon)

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
# Package identity and version: the place to edit them (docs/RELEASING.md).
# The name and data folder used by the code are in src/app_info.h.
# VMOON0001 is the app's own title ID: it installs next to Moonlight
# (XYZZ00002) and keeps its data in ux0:data/vitamoon. Up to 1.0.0-beta.1 the
# app was Lune (LUNE00001, ux0:data/lune): its folder is moved on first start.
set(TITLE_ID "VMOON0001")
set(TITLE "VitaMoon")
set(LIVEAREA_TITLE "VITAMOON")
set(LIVEAREA_SUBTITLE "GAME STREAMING\nFROM YOUR PC")
set(VERSION_MAJOR "1")
set(VERSION_MINOR "0")
set(VERSION_PATCH "0")
set(VERSION_LABEL "beta.3")        # empty for a final release
set(VERSION_NAME "Nouvelle Lune")  # milestone name, shown in the docs
if(VERSION_LABEL)
	set(VERSION_TEXT "v${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}-${VERSION_LABEL}")
else()
	set(VERSION_TEXT "v${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
endif()

execute_process(COMMAND
	python -c "print('%02d.%02d' % (${VERSION_MAJOR}, ${VERSION_MINOR}))"
	OUTPUT_VARIABLE APP_VER
)
string(REPLACE "\n" "" APP_VER "${APP_VER}")

add_definitions(-DVERSION_MAJOR=${VERSION_MAJOR})
add_definitions(-DVERSION_MINOR=${VERSION_MINOR})
add_definitions(-DVERSION_PATCH=${VERSION_PATCH})
add_definitions(-DVERSION_TEXT=\"${VERSION_TEXT}\")

# Build identifier (branch + short commit) shown in the main menu, so a tester
# can always tell which build is installed on the Vita.
execute_process(COMMAND git rev-parse --short=7 HEAD
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	OUTPUT_VARIABLE BUILD_COMMIT
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET
)
if(DEFINED ENV{BUILD_BRANCH})
	set(BUILD_BRANCH "$ENV{BUILD_BRANCH}")
else()
	execute_process(COMMAND git rev-parse --abbrev-ref HEAD
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_VARIABLE BUILD_BRANCH
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)
endif()
if(BUILD_COMMIT)
	add_definitions(-DBUILD_ID=\"${BUILD_BRANCH}-${BUILD_COMMIT}\")
endif()

# gnu99 rather than c99: the code relies on POSIX/GNU declarations such as strdup.
# -fpermissive keeps GCC >= 14 from turning legacy warnings (implicit declarations,
# int/pointer conversions) into hard errors in this code base and its submodules.
#
# This line replaces CMAKE_C_FLAGS outright and no build type is selected, so the
# optimisation level has to be given here: the original build compiled the app,
# moonlight-common-c (FEC included), ENet and the rest at -O0. VITA_OPT_LEVEL can
# be set to -O0 for a debugging build. Debug info (-g) stays, for crash dumps.
set(VITA_OPT_LEVEL "-O2" CACHE STRING "Optimisation flag for all C sources (-O0 to debug)")
set(CMAKE_C_FLAGS "-Wl,-q -g ${VITA_OPT_LEVEL} -std=gnu99 -fpermissive -D__vita__")



include_directories(
	include/
	src/
	third_party/moonlight-common-c/src/
	libgamestream/
	third_party/libuuid/src/
	third_party/h264bitstream/
	third_party/enet/include/
	third_party/inih/
	third_party/mdnsniff/
)


add_executable(${PROJECT_NAME}.elf
	src/config.c
	src/input/mapping.c
	src/connection.c
	src/debug.c
	src/log.c
	src/log_core.c
	src/main.c
	src/platform.c
	src/util.c
	src/device.c
	src/check_dir.c
	src/wake_on_lan.c
	src/audio/vita.c
	src/video/vita.c
	src/video/pacing.c
	src/video/stream_stats.c
	src/gui/boxart.c
	src/gui/ui_library.c
	src/gui/quickmenu.c
	src/input/vita.c
	src/input/shortcuts.c
	src/power/vita.c
	src/thread_policy.c
	src/input/motion.c
	src/graphics.c
	src/font.c
	src/gui/guilib.c
	src/gui/theme.c
	src/i18n.c
	src/gui/ime.c
	src/gui/ui.c
	src/gui/ui_settings.c
	src/gui/ui_connect.c
	src/gui/ui_device.c
	src/gui/ui_check.c

	libgamestream/client.c
	libgamestream/http.c
	libgamestream/mkcert.c
	libgamestream/sps.c
	libgamestream/url.c
	libgamestream/xml.c

	# moonlight-common-c, ENet, h264bitstream, inih and libuuid are built once,
	# as the static libraries of third_party/CMakeLists.txt linked below (they
	# used to be compiled a second time here, and these copies won).

	src/keyboardsystem.c
	src/input/keyboardkeys.c
	src/input/touchabsolute.c
	src/check_host.c
)


# Por defecto, solo enlazamos curl y el resto de libs estándar:
target_link_libraries(${PROJECT_NAME}.elf
	-lexpat
	-lcurl
	-lzstd
	moonlight-common-c
	enet
	h264bitstream
	inih
	libuuid
	mdnsniff
	-lz
	-lssl
	-lcrypto
	-lopus
	-lbz2
	-lpthread
	-lSceDisplay_stub
	-lSceCtrl_stub
	-lSceTouch_stub
	-lSceNet_stub
	-lSceNetCtl_stub
	-lScePower_stub
	-lSceRegistryMgr_stub
	-lSceVideodec_stub
	-lSceCommonDialog_stub
	-lSceShellSvc_stub
	-lSceAppMgr_stub
	-lSceAudio_stub
	-lSceIme_stub
	-lSceAppUtil_stub
	-lSceMotion_stub
	-lvita2d
	-lSceGxm_stub
	-lScePgf_stub
	-lSceCommonDialog_stub
	-lfreetype
	-lpng
	-ljpeg
	-lz
	-lm
	-lc
)

add_custom_target(${PROJECT_NAME}.velf ALL
	COMMAND vita-elf-create ${PROJECT_NAME}.elf ${PROJECT_NAME}.velf
)

add_custom_target(param.sfo ALL   
	COMMAND vita-mksfoex -s TITLE_ID=${TITLE_ID} -s APP_VER=${APP_VER} "${TITLE}" param.sfo
)

# LiveArea text with the title and version, written into the build folder
# (the source tree is not modified by a build).
configure_file(${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/template.xml.in
               ${CMAKE_BINARY_DIR}/livearea/template.xml @ONLY)

add_custom_target(${PROJECT_NAME}.vpk ALL
	COMMAND vita-make-fself -a 0x2800000000000001 -at 0x0E -c ${PROJECT_NAME}.velf eboot.bin
	COMMAND vita-pack-vpk -s param.sfo -b eboot.bin
			--add ../sce_sys/icon0.png=sce_sys/icon0.png
			--add ../sce_sys/livearea/contents/bg.png=sce_sys/livearea/contents/bg.png
			--add ../sce_sys/livearea/contents/startup.png=sce_sys/livearea/contents/startup.png
			--add ${CMAKE_BINARY_DIR}/livearea/template.xml=sce_sys/livearea/contents/template.xml
			--add ../assets/nerdfont.ttf=assets/nerdfont.ttf
			--add ../assets/Inter-Regular.otf=assets/Inter-Regular.otf
			--add ../assets/Inter-Light.otf=assets/Inter-Light.otf
			--add ../assets/Poppins-Regular.ttf=assets/Poppins-Regular.ttf
			--add ../assets/LICENSE.Inter=assets/LICENSE.Inter
			--add ../assets/LICENSE.Poppins=assets/LICENSE.Poppins
			--add ../assets/bg_night.png=assets/bg_night.png
			--add ../assets/bg_dawn.png=assets/bg_dawn.png
			--add ../assets/bg_aurora.png=assets/bg_aurora.png
			--add ../assets/bg_twilight.png=assets/bg_twilight.png
			--add ../assets/bg_eclipse.png=assets/bg_eclipse.png
			--add ../assets/logo_mark.png=assets/logo_mark.png
			--add ../assets/moon.png=assets/moon.png
			${PROJECT_NAME}.vpk
)

add_dependencies(${PROJECT_NAME}.velf ${PROJECT_NAME}.elf)
add_dependencies(${PROJECT_NAME}.vpk ${PROJECT_NAME}.velf param.sfo)

add_subdirectory(third_party)

option(USE_DIR_UMA0 "Keep the data folder in uma0:data/vitamoon instead of ux0:data/vitamoon" OFF)
if(USE_DIR_UMA0)
  add_definitions(-DUSE_DIR_UMA0)
endif()


