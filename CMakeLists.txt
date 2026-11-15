cmake_minimum_required(VERSION 3.2)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
	set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
	message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()
add_compile_options(-g -Wall -Wextra -Wno-unused-parameter -Wno-pointer-sign -Wno-sign-compare -Wno-switch)


project(moonlight)

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
# NOTE: To update the version shown in the UI and VPK, edit the following lines:
#   set(VERSION_MAJOR "0")
#   set(VERSION_MINOR "11")
#   set(VERSION_PATCH "2")
#
set(TITLE_ID "XYZZ00002")
set(TITLE "Moonlight")
set(VERSION_MAJOR "0")
set(VERSION_MINOR "13")
set(VERSION_PATCH "2")

execute_process(COMMAND
	python -c "print('%02d.%02d' % (${VERSION_MAJOR}, ${VERSION_MINOR}))"
	OUTPUT_VARIABLE APP_VER
)
string(REPLACE "\n" "" APP_VER "${APP_VER}")

add_definitions(-DVERSION_MAJOR=${VERSION_MAJOR})
add_definitions(-DVERSION_MINOR=${VERSION_MINOR})
add_definitions(-DVERSION_PATCH=${VERSION_PATCH})

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
	src/main.c
	src/platform.c
	src/util.c
	src/device.c
	src/check_dir.c
	src/wake_on_lan.c
	src/audio/vita.c
	src/video/vita.c
	src/video/stream_stats.c
	src/gui/boxart.c
	src/gui/ui_library.c
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
	src/gui/ui_hotkey.c
	src/gui/hotkey.c
	src/gui/ui_check.c

	libgamestream/client.c
	libgamestream/http.c
	libgamestream/mkcert.c
	libgamestream/sps.c
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

# Actualizar versión en template.xml automáticamente antes de empaquetar el VPK (solo la línea visual)
add_custom_target(update_template_version ALL
	COMMAND sed -i '/<str size=\"18.*>v[0-9]*\.[0-9]*\.[0-9]*<\\/str>/s/v[0-9]*\.[0-9]*\.[0-9]*/v${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}/' sce_sys/livearea/contents/template.xml
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_target(${PROJECT_NAME}.vpk ALL
	COMMAND vita-make-fself -a 0x2800000000000001 -at 0x0E -c ${PROJECT_NAME}.velf eboot.bin
	COMMAND vita-pack-vpk -s param.sfo -b eboot.bin
			--add ../sce_sys/icon0.png=sce_sys/icon0.png
			--add ../sce_sys/livearea/contents/bg.png=sce_sys/livearea/contents/bg.png
			--add ../sce_sys/livearea/contents/startup.png=sce_sys/livearea/contents/startup.png
			--add ../sce_sys/livearea/contents/template.xml=sce_sys/livearea/contents/template.xml
			--add ../assets/nerdfont.ttf=assets/nerdfont.ttf
			--add ../assets/Roboto-Regular.ttf=assets/Roboto-Regular.ttf
			--add ../assets/Roboto-Light.ttf=assets/Roboto-Light.ttf
			--add ../assets/bg_ocean.png=assets/bg_ocean.png
			${PROJECT_NAME}.vpk
)

add_dependencies(${PROJECT_NAME}.velf ${PROJECT_NAME}.elf)
add_dependencies(${PROJECT_NAME}.vpk ${PROJECT_NAME}.velf param.sfo update_template_version)

add_subdirectory(third_party)

option(USE_DIR_UMA0 "Forzar uso de uma0:data/moonlight como directorio de claves/config" OFF)
if(USE_DIR_UMA0)
  add_definitions(-DUSE_DIR_UMA0)
endif()


