file(GLOB platform_freebsd_sources CONFIGURE_DEPENDS *.c)

add_library(platform_freebsd SHARED ${platform_freebsd_sources})

target_include_directories(platform_freebsd
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(platform_freebsd PUBLIC c_std_11)
target_link_libraries(platform_freebsd PUBLIC Threads::Threads)

# raster.c is DRM/KMS-based (no fbdev equivalent exists on FreeBSD, per
# this project's own reconnaissance -- confirmed via vt(4)/syscons(4)'s
# own FILES sections listing no raw framebuffer device node). Uses
# libdrm's dumb-buffer API, the same one validated with standalone test
# programs (staging/gem-freebsd-port/drm-stage-{a,b,c}.c) before any of
# this file existed.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GEM_LIBDRM REQUIRED IMPORTED_TARGET libdrm)
target_link_libraries(platform_freebsd PUBLIC PkgConfig::GEM_LIBDRM)

# devctl(3) -- confirmed this session as the actual, real fix needed for
# this hardware: neither drmModeSetCrtc succeeding nor a DPMS property
# cycle is enough to make the eDP link display content on a cold boot;
# only a genuine PCI power-state cycle of the GPU device (devctl_suspend
# + devctl_resume) does, confirmed directly against dmesg showing real
# pci_set_powerstate/pci_enable_io transitions.
target_link_libraries(platform_freebsd PUBLIC devctl)

target_compile_options(platform_freebsd
    PRIVATE
        -Wall
        -Wextra
        -pedantic
)

target_compile_options(platform_freebsd
    PRIVATE
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
