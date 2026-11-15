add_library(enet STATIC
    enet/callbacks.c
    enet/compress.c
    enet/host.c
    enet/list.c
    enet/packet.c
    enet/peer.c
    enet/protocol.c
    enet/unix.c
)

target_include_directories(enet PUBLIC enet/include)

add_library(h264bitstream STATIC
    h264bitstream/h264_nal.c
    h264bitstream/h264_sei.c
    h264bitstream/h264_stream.c
)
target_include_directories(h264bitstream PUBLIC h264bitstream)

add_library(inih STATIC
    inih/ini.c
)
target_include_directories(inih PUBLIC inih)

add_library(libuuid STATIC
    libuuid/src/clear.c
    libuuid/src/compare.c
    libuuid/src/copy.c
    libuuid/src/gen_uuid.c
    libuuid/src/isnull.c
    libuuid/src/pack.c
    libuuid/src/parse.c
    libuuid/src/unpack.c
    libuuid/src/unparse.c
    libuuid/src/uuid_time.c
)
target_include_directories(libuuid PUBLIC libuuid/src)

# moonlight-common-c is kept in this repository (upstream commit e2722dc plus the
# patches listed in third_party/patches/moonlight-common-c). Its forward error
# correction is nanors, which uses NEON on the Vita (__ARM_NEON is on by default).
file(GLOB MOONLIGHT_COMMON_SOURCES CONFIGURE_DEPENDS moonlight-common-c/src/*.c)

add_library(moonlight-common-c STATIC
    ${MOONLIGHT_COMMON_SOURCES}
    moonlight-common-c/nanors/rs.c
    moonlight-common-c/nanors/deps/obl/oblas_common.c
    moonlight-common-c/nanors/deps/obl/oblas_lite.c
)
target_include_directories(moonlight-common-c
    PUBLIC moonlight-common-c/src
    PRIVATE
        moonlight-common-c/nanors
        moonlight-common-c/nanors/deps
        moonlight-common-c/nanors/deps/obl
)
target_compile_definitions(moonlight-common-c PRIVATE HAS_SOCKLEN_T NDEBUG)
# C11: nanors allocates with aligned_alloc(), which gnu99 headers do not declare
target_compile_options(moonlight-common-c PRIVATE -std=gnu11)
target_link_libraries(moonlight-common-c PRIVATE enet)

add_library(mdnsniff STATIC
    mdnsniff/udp_sniffer_vita.c
)
target_include_directories(mdnsniff PUBLIC mdnsniff)
