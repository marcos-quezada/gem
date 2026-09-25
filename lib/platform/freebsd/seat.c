/*
 * gem-freebsd-libseat: the shared libseat handle owned by gemd, and the
 * open/close/enable/disable plumbing raster.c and hid.c use instead of
 * raw open()/close() on /dev/drm/N and /dev/input/eventN.
 *
 * Only gemd calls gem_freebsd_seat_init/shutdown/fd/dispatch (matching
 * design.md's "gemd is the sole seat client" decision). raster.c/hid.c
 * only ever call the open_device/close_device wrappers and register
 * their own enable/disable hooks -- this file has zero knowledge of
 * either subsystem's internals.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#include "platform/freebsd_seat.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <libseat.h>

static struct libseat *g_seat;
static void (*g_raster_enable_hook)(void);
static void (*g_raster_disable_hook)(void);
static void (*g_hid_enable_hook)(void);
static void (*g_hid_disable_hook)(void);

static void on_enable_seat(struct libseat *seat, void *userdata)
{
    (void)seat;
    (void)userdata;
    if (g_raster_enable_hook != NULL) {
        g_raster_enable_hook();
    }
    if (g_hid_enable_hook != NULL) {
        g_hid_enable_hook();
    }
}

static void on_disable_seat(struct libseat *seat, void *userdata)
{
    (void)userdata;
    if (g_raster_disable_hook != NULL) {
        g_raster_disable_hook();
    }
    if (g_hid_disable_hook != NULL) {
        g_hid_disable_hook();
    }
    /*
     * Confirmed via libseat.h's own doc comment: this MUST be
     * acknowledged shortly after receiving the event, or the seat
     * provider may forcibly revoke devices instead of a clean,
     * cooperative release. Call this AFTER the hooks above have had a
     * chance to release their own devices/state.
     */
    if (libseat_disable_seat(seat) != 0) {
        fprintf(stderr,
                "gem_freebsd_seat: libseat_disable_seat failed: %s\n",
                strerror(errno));
    }
}

static const struct libseat_seat_listener g_listener = {
    .enable_seat = on_enable_seat,
    .disable_seat = on_disable_seat,
};

int gem_freebsd_seat_init(void)
{
    g_seat = libseat_open_seat(&g_listener, NULL);
    return g_seat != NULL;
}

void gem_freebsd_seat_shutdown(void)
{
    if (g_seat != NULL) {
        (void)libseat_close_seat(g_seat);
        g_seat = NULL;
    }
}

int gem_freebsd_seat_fd(void)
{
    return (g_seat != NULL) ? libseat_get_fd(g_seat) : -1;
}

void gem_freebsd_seat_dispatch(void)
{
    if (g_seat != NULL) {
        (void)libseat_dispatch(g_seat, 0);
    }
}

int gem_freebsd_seat_open_device(const char *path, int *fd)
{
    if (g_seat == NULL) {
        errno = ENODEV;
        return -1;
    }
    return libseat_open_device(g_seat, path, fd);
}

void gem_freebsd_seat_close_device(int device_id)
{
    if (g_seat != NULL) {
        (void)libseat_close_device(g_seat, device_id);
    }
}

int gem_freebsd_seat_switch_session(int session)
{
    if (g_seat == NULL) {
        errno = ENODEV;
        return -1;
    }
    return libseat_switch_session(g_seat, session);
}

void gem_freebsd_seat_set_raster_hooks(void (*enable)(void),
                                       void (*disable)(void))
{
    g_raster_enable_hook = enable;
    g_raster_disable_hook = disable;
}

void gem_freebsd_seat_set_hid_hooks(void (*enable)(void), void (*disable)(void))
{
    g_hid_enable_hook = enable;
    g_hid_disable_hook = disable;
}
