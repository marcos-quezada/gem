#ifndef GEM_PLATFORM_FREEBSD_SEAT_H
#define GEM_PLATFORM_FREEBSD_SEAT_H

/*
 * FreeBSD-only libseat integration point (gem-freebsd-libseat). NOT part
 * of the cross-platform platform/hid.h / platform/raster.h contract --
 * linux and rasta never include this file.
 *
 * gemd is the sole owner/caller of init/shutdown/fd/dispatch (matching
 * design.md's "gemd is the sole seat client" decision -- gem_raster_init
 * and gem_hid_init are only ever called from gemd's own process).
 * raster.c and hid.c call the open/close wrappers below to actually
 * acquire devices through the shared seat handle, and register their
 * own enable/disable hooks so this module doesn't need to know either
 * subsystem's internals.
 */

/* Called once by gemd at startup, BEFORE gem_raster_init()/gem_hid_init().
 * Returns 1 on success, 0 on failure (errno set by libseat). */
int gem_freebsd_seat_init(void);

/* Called once by gemd at shutdown. */
void gem_freebsd_seat_shutdown(void);

/* Pollable fd for gemd's own poll() loop; -1 if the seat isn't open. */
int gem_freebsd_seat_fd(void);

/* Call when gem_freebsd_seat_fd() is readable, to process pending seat
 * events (this is what actually invokes the enable/disable hooks). */
void gem_freebsd_seat_dispatch(void);

/* Used by raster.c/hid.c in place of raw open()/close() on
 * /dev/drm/N and /dev/input/eventN. Same success/failure contract as
 * libseat_open_device(): returns the device id (>=0) on success, -1 on
 * failure (errno set), placing the fd in *fd on success. */
int gem_freebsd_seat_open_device(const char *path, int *fd);
void gem_freebsd_seat_close_device(int device_id);

/*
 * Requests a VT switch to the given session/VT number. Compositors using
 * libseat are expected to recognize their own VT-switch hotkey (e.g.
 * Ctrl+Alt+Fn) and call this explicitly -- the kernel/seatd do NOT
 * intercept that combo on GEM's behalf the way old-style vt(4) console
 * switching did. A no-op if the seat isn't open.
 */
int gem_freebsd_seat_switch_session(int session);

/*
 * Registration hooks: raster.c/hid.c each register a pair of callbacks
 * called when the seat is enabled/disabled (typically: VT switch away
 * calls disable, back calls enable). Either pointer may be NULL if that
 * subsystem hasn't initialized yet -- seat.c tolerates calling into an
 * unregistered subsystem as a no-op.
 */
void gem_freebsd_seat_set_raster_hooks(void (*enable)(void),
                                       void (*disable)(void));
void gem_freebsd_seat_set_hid_hooks(void (*enable)(void), void (*disable)(void));

#endif
