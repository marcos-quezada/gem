/*
 * Implements native FreeBSD DRM/KMS output for GEM. VDI draws into a
 * packed monochrome shadow surface (the same format rasta and the Linux
 * backend use) and this backend expands it into a DRM "dumb buffer"
 * scanned out directly by the GPU -- there is no fbdev-equivalent raw
 * framebuffer device on FreeBSD (confirmed against vt(4)/syscons(4)'s own
 * FILES sections: neither lists one), so this goes through the real
 * DRM/KMS dumb-buffer API instead, validated standalone before any of
 * this file existed (staging/gem-freebsd-port/drm-stage-{a,b,c}.c).
 *
 * SAFETY: this backend never enumerates or opens more than the single
 * DRM device it is configured for. On this project's own development
 * machine, a second DRM node exists for a secondary/PRIME GPU that is
 * confirmed (via a real, reproducible kernel panic, root-caused with a
 * full kgdb backtrace) to crash the kernel when opened by any DRM-aware
 * client. Auto-probing multiple /dev/drm/N nodes to "find the right one"
 * would risk touching that device by accident. The device path is always
 * explicit: GEM_FREEBSD_DRM env var, or the conservative default below.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#define _POSIX_C_SOURCE 200809L

#include "platform/raster.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <devctl.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

/*
 * Dumb-buffer create/map/destroy: not wrapped by libdrm's own
 * convenience API (only mode-setting calls are), so these mirror the
 * kernel uAPI directly -- verified against FreeBSD's own
 * sys/dev/drm2/drm_mode.h / drm.h, and cross-checked live: this
 * project's actual installed libdrm/kernel headers already declare
 * these structs, so including them here would conflict. Declared with
 * `struct drm_mode_create_dumb` etc. left to the system headers pulled
 * in by xf86drmMode.h; only the ioctl numbers are defined locally in
 * case a given libdrm version doesn't expose them under these names.
 */
#ifndef DRM_IOCTL_MODE_CREATE_DUMB
#define DRM_IOCTL_MODE_CREATE_DUMB DRM_IOWR(0xB2, struct drm_mode_create_dumb)
#endif
#ifndef DRM_IOCTL_MODE_MAP_DUMB
#define DRM_IOCTL_MODE_MAP_DUMB DRM_IOWR(0xB3, struct drm_mode_map_dumb)
#endif
#ifndef DRM_IOCTL_MODE_DESTROY_DUMB
#define DRM_IOCTL_MODE_DESTROY_DUMB \
    DRM_IOWR(0xB4, struct drm_mode_destroy_dumb)
#endif

static gem_raster_surface_t g_surface;
static uint8_t *g_dumb_pixels;
static uint64_t g_dumb_size;
static uint32_t g_dumb_handle;
static uint32_t g_dumb_pitch;
static uint32_t g_fb_id;
static uint32_t g_crtc_id;
static int g_drm_fd = -1;
static uint32_t g_connector_id;
static drmModeCrtcPtr g_saved_crtc;
static int g_power_cycled_after_first_content;

static const char *drm_device_path(void)
{
    const char *path = getenv("GEM_FREEBSD_DRM");

    return (path != NULL && path[0] != '\0') ? path : "/dev/drm/0";
}

/*
 * set bit -> black ink, clear -> white paper. Same shadow polarity
 * convention as rasta and the Linux backend; do not "fix" it here.
 */
static uint32_t shadow_bit_to_xrgb8888(int bit_set)
{
    return bit_set ? 0x00000000u : 0x00ffffffu;
}

static void teardown_drm(int restore_crtc)
{
    if (restore_crtc && g_saved_crtc != NULL && g_drm_fd >= 0) {
        (void)drmModeSetCrtc(g_drm_fd, g_saved_crtc->crtc_id,
                             g_saved_crtc->buffer_id, g_saved_crtc->x,
                             g_saved_crtc->y, &g_connector_id, 1,
                             &g_saved_crtc->mode);
    }
    if (g_saved_crtc != NULL) {
        drmModeFreeCrtc(g_saved_crtc);
        g_saved_crtc = NULL;
    }
    if (g_fb_id != 0u && g_drm_fd >= 0) {
        drmModeRmFB(g_drm_fd, g_fb_id);
        g_fb_id = 0u;
    }
    if (g_dumb_pixels != NULL) {
        (void)munmap(g_dumb_pixels, g_dumb_size);
        g_dumb_pixels = NULL;
    }
    if (g_dumb_handle != 0u && g_drm_fd >= 0) {
        struct drm_mode_destroy_dumb dreq;

        memset(&dreq, 0, sizeof(dreq));
        dreq.handle = g_dumb_handle;
        (void)drmIoctl(g_drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        g_dumb_handle = 0u;
    }
    if (g_drm_fd >= 0) {
        close(g_drm_fd);
        g_drm_fd = -1;
    }
}

/*
 * Confirmed via direct evidence (not guessed): neither drmModeSetCrtc
 * succeeding nor a DPMS property OFF/ON cycle is sufficient to make this
 * hardware's eDP link actually display content for a freshly-opened
 * client on a cold boot -- only an ACTUAL PCI power-state cycle of the
 * GPU device does (confirmed via `devctl suspend drmn0` / `devctl resume
 * drmn0`, cross-checked against dmesg showing real
 * `pci_set_powerstate`/`pci_enable_io` transitions, and an HDA audio
 * codec reacting with "unsolicited response" messages consistent with a
 * real display-link retraining event). This performs the same operation
 * devctl(8) does, via the documented devctl(3) C API
 * (devctl_suspend()/devctl_resume()), rather than shelling out.
 *
 * The device name is machine-specific (this project's own convention,
 * matching GEM_FREEBSD_DRM's existing env-var-override pattern) --
 * override via GEM_FREEBSD_DRM_DEVCTL if a different machine's GPU
 * newbus device name differs from the default below.
 *
 * A single cycle is not always enough -- consistent with real eDP link
 * training genuinely being flaky at the hardware level (a documented,
 * known category of behavior, not specific to this driver). No clean
 * way exists to query "did the link actually train" from userspace to
 * decide whether a retry is needed, so this pragmatically cycles twice,
 * unconditionally. Override the cycle count via
 * GEM_FREEBSD_DRM_POWERCYCLES if a different machine needs more (or
 * fewer, though 1 is not recommended given the evidence).
 */
static void power_cycle_gpu_device(void)
{
    const char *device = getenv("GEM_FREEBSD_DRM_DEVCTL");
    const char *count_env = getenv("GEM_FREEBSD_DRM_POWERCYCLES");
    int count = 2;
    int attempt;

    if (device == NULL || device[0] == '\0') {
        device = "drmn0";
    }
    if (count_env != NULL && count_env[0] != '\0') {
        char *end = NULL;
        long parsed = strtol(count_env, &end, 10);

        if (end != count_env && *end == '\0' && parsed >= 1L &&
            parsed <= 10L) {
            count = (int)parsed;
        }
    }

    for (attempt = 1; attempt <= count; ++attempt) {
        if (devctl_suspend(device) != 0) {
            continue;
        }
        {
            struct timespec delay = {0, 300000000L};

            nanosleep(&delay, NULL);
        }
        if (devctl_resume(device) != 0) {
            continue;
        }
        if (attempt < count) {
            sleep(3); /* settle time between cycles */
        }
    }
}

int gem_raster_init(uint16_t width, uint16_t height, gem_raster_format_t format)
{
    drmModeResPtr res = NULL;
    drmModeConnectorPtr conn = NULL;
    drmModeEncoderPtr enc = NULL;
    struct drm_mode_create_dumb creq;
    struct drm_mode_map_dumb mreq;
    size_t pitch;
    size_t shadow_size;
    int i;
    int ok = 0;

    if (format != GEM_RASTER_MONO1) {
        errno = EINVAL;
        return 0;
    }

    g_drm_fd = open(drm_device_path(), O_RDWR | O_CLOEXEC);
    if (g_drm_fd < 0) {
        return 0;
    }

    res = drmModeGetResources(g_drm_fd);
    if (res == NULL) {
        goto fail;
    }
    for (i = 0; i < res->count_connectors; ++i) {
        conn = drmModeGetConnector(g_drm_fd, res->connectors[i]);
        if (conn != NULL && conn->connection == DRM_MODE_CONNECTED &&
            conn->count_modes > 0) {
            break;
        }
        if (conn != NULL) {
            drmModeFreeConnector(conn);
            conn = NULL;
        }
    }
    if (conn == NULL || conn->encoder_id == 0u) {
        goto fail;
    }
    enc = drmModeGetEncoder(g_drm_fd, conn->encoder_id);
    if (enc == NULL || enc->crtc_id == 0u) {
        goto fail;
    }
    g_connector_id = conn->connector_id;
    g_crtc_id = enc->crtc_id;

    /* Save the CRTC's current state so gem_raster_shutdown can restore
     * it exactly, leaving the console/compositor state untouched. */
    g_saved_crtc = drmModeGetCrtc(g_drm_fd, enc->crtc_id);
    if (g_saved_crtc == NULL) {
        goto fail;
    }

    /* width/height 0 (or oversized) -> use the connector's current mode,
     * matching the Linux backend's "0 means fill the device" contract. */
    if (width == 0u || width > conn->modes[0].hdisplay) {
        width = (uint16_t)conn->modes[0].hdisplay;
    }
    if (height == 0u || height > conn->modes[0].vdisplay) {
        height = (uint16_t)conn->modes[0].vdisplay;
    }
    if (width == 0u || height == 0u) {
        errno = ENOTSUP;
        goto fail;
    }

    memset(&creq, 0, sizeof(creq));
    creq.width = width;
    creq.height = height;
    creq.bpp = 32u;
    if (drmIoctl(g_drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
        goto fail;
    }
    g_dumb_handle = creq.handle;
    g_dumb_pitch = creq.pitch;
    g_dumb_size = creq.size;

    memset(&mreq, 0, sizeof(mreq));
    mreq.handle = creq.handle;
    if (drmIoctl(g_drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
        goto fail;
    }
    g_dumb_pixels = mmap(NULL, g_dumb_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, g_drm_fd, (off_t)mreq.offset);
    if (g_dumb_pixels == MAP_FAILED) {
        g_dumb_pixels = NULL;
        goto fail;
    }
    memset(g_dumb_pixels, 0, g_dumb_size);

    if (drmModeAddFB(g_drm_fd, creq.width, creq.height, 24, 32, creq.pitch,
                     creq.handle, &g_fb_id) < 0) {
        goto fail;
    }
    if (drmModeSetCrtc(g_drm_fd, enc->crtc_id, g_fb_id, 0, 0, &g_connector_id,
                       1, &conn->modes[0]) < 0) {
        goto fail;
    }

    pitch = ((size_t)width + 7u) / 8u;
    if (pitch > UINT16_MAX) {
        errno = EOVERFLOW;
        goto fail;
    }
    shadow_size = pitch * (size_t)height;
    g_surface.pixels = calloc(1u, shadow_size);
    if (g_surface.pixels == NULL) {
        goto fail;
    }
    g_surface.width = width;
    g_surface.height = height;
    g_surface.pitch = (uint16_t)pitch;
    g_surface.format = format;
    ok = 1;

fail:
    if (enc != NULL) {
        drmModeFreeEncoder(enc);
    }
    if (conn != NULL) {
        drmModeFreeConnector(conn);
    }
    if (res != NULL) {
        drmModeFreeResources(res);
    }
    if (!ok) {
        /* Only restore the CRTC if we actually changed it (g_fb_id set). */
        teardown_drm(g_fb_id != 0u);
    }
    return ok;
}

int gem_raster_resync(void)
{
    return g_surface.pixels != NULL && g_dumb_pixels != NULL;
}

void gem_raster_shutdown(void)
{
    teardown_drm(1);
    free(g_surface.pixels);
    memset(&g_surface, 0, sizeof(g_surface));
    g_dumb_pitch = 0u;
    g_power_cycled_after_first_content = 0;
}

gem_raster_surface_t *gem_raster_surface(void)
{
    return (g_surface.pixels != NULL) ? &g_surface : NULL;
}

void gem_raster_present_rect(int x, int y, int width, int height)
{
    const uint8_t *source = g_surface.pixels;
    int x0;
    int y0;
    int64_t x1;
    int64_t y1;
    int row_y;
    static unsigned long call_count;

    ++call_count;

    if (source == NULL || g_dumb_pixels == NULL || width <= 0 || height <= 0) {
        return;
    }

    x0 = x;
    y0 = y;
    x1 = (int64_t)x + width - 1;
    y1 = (int64_t)y + height - 1;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 >= (int)g_surface.width) {
        x1 = (int)g_surface.width - 1;
    }
    if (y1 >= (int)g_surface.height) {
        y1 = (int)g_surface.height - 1;
    }
    if (x0 > x1 || y0 > y1) {
        return;
    }

    for (row_y = y0; row_y <= y1; ++row_y) {
        uint32_t *dst_row =
            (uint32_t *)(g_dumb_pixels + (size_t)row_y * g_dumb_pitch);
        const uint8_t *src_row = source + (size_t)row_y * g_surface.pitch;
        int col_x;

        for (col_x = x0; col_x <= x1; ++col_x) {
            uint8_t bits = src_row[col_x / 8];
            int bit_set = (bits & (uint8_t)(0x80u >> (col_x & 7))) != 0u;

            dst_row[col_x] = shadow_bit_to_xrgb8888(bit_set);
        }
    }

    /*
     * Deferred, one-time power-cycle: confirmed only reliable once real
     * content has already been drawn and AES has had a chance to finish
     * its real startup drawing (call #1, or even the very first write,
     * was confirmed NOT sufficient). Deferred to call #30 as an
     * empirically-confirmed threshold.
     */
    if (!g_power_cycled_after_first_content && call_count >= 30u) {
        g_power_cycled_after_first_content = 1;
        power_cycle_gpu_device();
        gem_raster_present();
    }

    /*
     * Confirmed via direct evidence: after the devctl power-cycle, the
     * display shows one correct frame and then never rescans the
     * framebuffer again, even though the underlying memory keeps being
     * correctly updated. An explicit page-flip to the SAME fb_id forces
     * KMS to re-latch/rescan at the next vblank. Rate-limited implicitly
     * by call frequency; EBUSY (a flip already queued for the next
     * vblank) is expected and harmless.
     */
    if (g_power_cycled_after_first_content && g_crtc_id != 0u &&
        g_fb_id != 0u) {
        (void)drmModePageFlip(g_drm_fd, g_crtc_id, g_fb_id, 0, NULL);
    }
}

void gem_raster_present(void)
{
    if (g_surface.pixels == NULL) {
        return;
    }
    gem_raster_present_rect(0, 0, (int)g_surface.width, (int)g_surface.height);
}

void gem_raster_clear(void)
{
    if (g_surface.pixels != NULL) {
        size_t shadow_size = (size_t)g_surface.pitch * g_surface.height;

        memset(g_surface.pixels, 0, shadow_size);
    }
    if (g_dumb_pixels != NULL && g_dumb_size != 0u) {
        memset(g_dumb_pixels, 0, g_dumb_size);
    }
}

void gem_raster_set_palette(uint8_t index, uint8_t red, uint8_t green,
                            uint8_t blue)
{
    /* MONO1 is the only format this backend accepts (see gem_raster_init);
     * matches the Linux backend's own no-op stub for the same reason. */
    (void)index;
    (void)red;
    (void)green;
    (void)blue;
}
