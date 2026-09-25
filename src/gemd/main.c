/*
 * Runs the hosted GEM display server's main loop: polls every client
 * socket and the listener, pumps physical input between turns, enforces
 * transport and lock deadlines, and services requests, including the
 * cooperative servicing performed while a classic modal panel waits.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#define _GNU_SOURCE
#include "session.h"
#include "../aes/aes_shel.h"
#include "../aes/system_menu.h"

#ifdef GEM_PLATFORM_FREEBSD
#include "platform/freebsd_seat.h"
#endif

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int g_listen_fd = -1;
uint64_t g_next_generation;
gemd_session_t g_sessions[GEMD_MAX_SESSIONS];
WORD g_server_vdi_handle;
WORD g_server_vdi_refs;
WORD g_server_work_out[57];
struct stat g_socket_identity;
int g_socket_bound;
volatile sig_atomic_t g_stopping;
gemd_session_t *g_modal_session;

static void gemd_pump_hid(void)
{
    gem_hid_event_t evt;

    if (aes_state.update_depth > 0) {
        return;
    }

    for (unsigned i = 0; i < 128 && gem_hid_poll(&evt); ++i) {
        uint32_t started = gem_os_ticks_ms();
        aes_dispatch_hid_event(&evt);
        uint32_t elapsed = gem_os_ticks_ms() - started;

        /* Native menu/drag tracking waits for the user without servicing
         * RPC sockets. That server-imposed pause is not client inactivity.
         * Preserve each deadline's remaining time, including partial frames
         * and replies queued immediately before the menu opened. */
        if (elapsed != 0) {
            for (size_t j = 0; j < GEMD_MAX_SESSIONS; ++j) {
                gemd_session_t *session = &g_sessions[j];
                if (session->fd < 0)
                    continue;
                session->io.started += elapsed;
                session->accepted_at += elapsed;
                session->update_started += elapsed;
            }
        }
    }
}

int gemd_handle_request(gemd_session_t *session)
{
    const gem_rpc_header_t header = session->io.header;
    const uint8_t *payload = session->io.payload;
    _Alignas(max_align_t) uint8_t response[GEM_RPC_PAYLOAD_MAX];
    uint32_t response_size = 0u;
    int32_t status;

    if (!gem_rpc_valid_request(header.opcode, payload, header.size)) {
        gemd_reply(&session->io, -1, NULL, 0);
        return 1;
    }
    if (!gemd_authorized(session, header.opcode, payload)) {
        gemd_reply(&session->io, 0, NULL, 0);
        return 1;
    }
    if (gemd_vdi_request(header.opcode)) {
        gemd_drawing_t server;
        gemd_drawing_save(&server);
        if (!session->drawing.initialized)
            gemd_drawing_init(&session->drawing);
        gemd_drawing_restore(&session->drawing);
        if (gemd_raster_request(header.opcode) ||
            (header.opcode == GEM_RPC_BITMAP_COPY &&
             !((const gem_bitmap_call_t *)payload)->destination.memory) ||
            (header.opcode == GEM_RPC_VDI_EXT &&
             gem_vdi_draws(((const gem_vdi_packet_t *)payload)->function))) {
            if (header.opcode == GEM_RPC_VDI_EXT) {
                memcpy(response, payload, sizeof(gem_vdi_packet_t));
                response_size = sizeof(gem_vdi_packet_t);
            }
            status = gemd_draw_owned(session, &header, payload, response,
                                     &response_size);
            if (session->standalone)
                gemd_drawing_save(&session->drawing);
        } else {
            status = gemd_dispatch(session, &header, payload, response,
                                   &response_size);
            gemd_drawing_save(&session->drawing);
        }
        gemd_drawing_restore(&server);
    } else {
        if (header.opcode == GEM_RPC_FORM_ALERT ||
            header.opcode == GEM_RPC_FSEL_INPUT ||
            header.opcode == GEM_RPC_AES_TREE ||
            header.opcode == GEM_RPC_AES_EXT) {
            g_modal_session = session;
            aes_wait_hook = gemd_service_modal;
        }
        status =
            gemd_dispatch(session, &header, payload, response, &response_size);
        if (g_modal_session == session) {
            aes_wait_hook = NULL;
            g_modal_session = NULL;
        }
    }
    gemd_reply(&session->io, status, response, response_size);
    return 1;
}

/* Cooperate from AES panel waits without recursively opening another panel.
 * Attribute and app identity restoration keeps the suspended AES call intact.
 */
int gemd_service_modal(void)
{
    gemd_drawing_t drawing;
    WORD app_id = aes_state.current_app_id;
    size_t i;
    struct pollfd owner = {g_modal_session->fd, POLLRDHUP, 0};
    if (g_stopping || poll(&owner, 1, 0) < 0 ||
        (owner.revents & (POLLRDHUP | POLLHUP | POLLERR | POLLNVAL)))
        return 0;
    gemd_drawing_save(&drawing);
    gemd_accept_client();
    for (i = 0; i < GEMD_MAX_SESSIONS; ++i) {
        gemd_session_t *session = &g_sessions[i];
        struct pollfd fd;
        uint32_t now = gem_os_ticks_ms();
        aes_app_t *app = aes_find_app_by_id(session->app_id);
        if (session->fd < 0)
            continue;
        if (app && app->update_depth && now - session->update_started > 5000u) {
            if (session == g_modal_session) {
                gemd_drawing_restore(&drawing);
                aes_state.current_app_id = global[2] = app_id;
                return 0;
            }
            aes_trace("modal update timeout app=%d opcode=%u age=%u",
                      session->app_id, session->io.header.opcode,
                      now - session->update_started);
            gemd_close_session(session);
            continue;
        }
        if (session == g_modal_session)
            continue;
        fd.fd = session->fd;
        fd.events = session->io.output_size ? POLLOUT
                                            : (session->io.ready ? 0 : POLLIN);
        fd.revents = 0;
        (void)poll(&fd, 1, 0);
        if ((fd.revents & (POLLHUP | POLLERR | POLLNVAL)) ||
            ((fd.revents & POLLIN) &&
             gemd_receive(session->fd, &session->io) < 0) ||
            ((fd.revents & POLLOUT) &&
             gemd_send(session->fd, &session->io) < 0)) {
            gemd_close_session(session);
            continue;
        }
        /* Receiving a new frame can advance io.started. Read the clock
         * afterwards: an earlier sample minus a newer timestamp wraps to
         * UINT32_MAX and would disconnect a healthy client immediately. */
        now = gem_os_ticks_ms();
        if ((((session->io.header_read && !session->io.ready) ||
              session->io.output_size) &&
             now - session->io.started > 2000u) ||
            (!session->app_id && now - session->accepted_at > 5000u)) {
            aes_trace("modal transport timeout app=%d opcode=%u age=%u",
                      session->app_id, session->io.header.opcode,
                      now - session->io.started);
            gemd_close_session(session);
            continue;
        }
        if (session->io.ready && gemd_session_may_run(session) &&
            session->io.header.opcode != GEM_RPC_FORM_ALERT &&
            session->io.header.opcode != GEM_RPC_FSEL_INPUT &&
            session->io.header.opcode != GEM_RPC_AES_TREE &&
            session->io.header.opcode != GEM_RPC_AES_EXT)
            (void)gemd_handle_request(session);
    }
    gemd_drawing_restore(&drawing);
    aes_state.current_app_id = global[2] = app_id;
    return 1;
}

static void gemd_stop(int signal_number)
{
    (void)signal_number;
    g_stopping = 1;
}

/* Shutdown: ask every connected application to quit (AP_TERM, the classic
 * GEM signal) and enforce it with SIGTERM on the peer process, then let the
 * main loop end so gemd releases the display, input and socket. */
static void gemd_broadcast_terminate(void)
{
    WORD msg[8] = {AP_TERM, 0, 0, 0, 0, 0, 0, 0};
    int i;

    for (i = 0; i < GEMD_MAX_SESSIONS; ++i) {
        gemd_session_t *session = &g_sessions[i];

        if (session->fd < 0) {
            continue;
        }
        if (session->app_id != 0) {
            (void)appl_write(session->app_id, (WORD)sizeof(msg), msg);
        }
        if (session->pid > 0) {
            (void)kill((pid_t)session->pid, SIGTERM);
        }
    }
}

/* Installed as the system menu's Shutdown hook: broadcast AP_TERM to every
 * connected application and end the main loop, which closes each session and
 * clears the screen (see gemd_shutdown). */
static int gemd_system_shutdown(void)
{
    gemd_broadcast_terminate();
    g_stopping = 1;
    return 1;
}

int main(void)
{
    size_t i;

    aes_external_input = 1;
    aes_system_shutdown_hook = gemd_system_shutdown;

    /*
     * Writing to a session socket whose peer already closed its end
     * raises SIGPIPE, whose default disposition kills the process --
     * taking down every other connected client with it. gemd_send
     * already handles a plain -1/EPIPE return gracefully; ignoring the
     * signal is what lets that code path run instead of the process
     * dying first.
     */
    (void)signal(SIGPIPE, SIG_IGN);
    (void)signal(SIGTERM, gemd_stop);
    (void)signal(SIGINT, gemd_stop);

    for (i = 0; i < GEMD_MAX_SESSIONS; ++i) {
        g_sessions[i].fd = -1;
    }

#ifdef GEM_PLATFORM_FREEBSD
    /*
     * Must happen before gemd_init_listener() -- no client can be
     * accepted (and therefore no client can trigger v_opnvwk, which is
     * what actually calls gem_raster_init()/gem_hid_init()) until the
     * seat is ready. gemd is the sole libseat client (design.md).
     */
    if (!gem_freebsd_seat_init()) {
        fprintf(stderr, "gemd: gem_freebsd_seat_init failed: %s\n",
                strerror(errno));
        return 1;
    }
#endif

    if (!gemd_init_listener()) {
        gemd_shutdown();
        return 1;
    }

    printf("gemd listening on %s\n", gem_rpc_socket_path());
    fflush(stdout);

    while (!g_stopping) {
        /*
         * pollfds[] and poll_owner[] are built together, in the same
         * pass, so pollfds[k] and poll_owner[k] always describe the
         * same fd by construction. Earlier versions rebuilt this
         * fd-to-session mapping a second time (by re-running the same
         * "skip if not connected" scan) for the close/dispatch pass
         * below; that second derivation could drift from the first
         * whenever a session was accepted, closed, or reused in
         * between, silently pointing a session at another session's
         * poll result. That one fragile pattern was the root cause of
         * several different-looking failures (a session that never
         * got serviced, one serviced with the wrong readiness state,
         * a freshly-accepted connection closed on the spot). Deriving
         * the mapping exactly once removes the whole class of bug
         * rather than one instance of it.
         */
        struct pollfd pollfds[GEMD_MAX_SESSIONS + 2];
        gemd_session_t *poll_owner[GEMD_MAX_SESSIONS + 2];
        uint64_t poll_generation[GEMD_MAX_SESSIONS + 2];
        nfds_t nfds = 0;
        nfds_t k;
        int rc;
#ifdef GEM_PLATFORM_FREEBSD
        nfds_t seat_poll_index = (nfds_t)-1;
#endif

        pollfds[nfds].fd = g_listen_fd;
        pollfds[nfds].events = POLLIN;
        pollfds[nfds].revents = 0;
        poll_owner[nfds] = NULL;
        ++nfds;

#ifdef GEM_PLATFORM_FREEBSD
        {
            int seat_fd = gem_freebsd_seat_fd();

            if (seat_fd >= 0) {
                seat_poll_index = nfds;
                pollfds[nfds].fd = seat_fd;
                pollfds[nfds].events = POLLIN;
                pollfds[nfds].revents = 0;
                poll_owner[nfds] = NULL;
                ++nfds;
            }
        }
#endif

        for (i = 0; i < GEMD_MAX_SESSIONS; ++i) {
            if (g_sessions[i].fd >= 0) {
                pollfds[nfds].fd = g_sessions[i].fd;
                pollfds[nfds].events =
                    g_sessions[i].io.output_size
                        ? POLLOUT
                        : (g_sessions[i].io.ready ? 0 : POLLIN);
                pollfds[nfds].revents = 0;
                poll_owner[nfds] = &g_sessions[i];
                poll_generation[nfds] = g_sessions[i].generation;
                ++nfds;
            }
        }

        rc = poll(pollfds, nfds, 20);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("gemd: poll");
            break;
        }
        gemd_pump_hid();
        aes_shel_reap_children();

        for (k = 1; k < nfds; ++k) {
            gemd_session_t *session = poll_owner[k];

            /* A modal callback may have closed and reused this session slot
             * (and even its fd) since this poll snapshot was built. */
            if (session == NULL || session->fd != pollfds[k].fd ||
                session->generation != poll_generation[k]) {
                continue;
            }
            /* Bound unfinished frames, unread replies, init and update locks.
             * Deadlines exclude synchronous HID tracking pauses; client
             * activity alone never resets an existing lock deadline. */
            {
                uint32_t now = gem_os_ticks_ms();
                aes_app_t *app = aes_find_app_by_id(session->app_id);
                if (((session->io.header_read && !session->io.ready) ||
                     session->io.output_size) &&
                    now - session->io.started > 2000u) {
                    gemd_close_session(session);
                    continue;
                }
                if ((!session->app_id && now - session->accepted_at > 5000u) ||
                    (app && app->update_depth &&
                     now - session->update_started > 5000u)) {
                    gemd_close_session(session);
                    continue;
                }
            }
            if ((pollfds[k].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
                ((pollfds[k].revents & POLLIN) != 0 &&
                 gemd_receive(session->fd, &session->io) < 0) ||
                ((pollfds[k].revents & POLLOUT) != 0 &&
                 gemd_send(session->fd, &session->io) < 0)) {
                gemd_close_session(session);
                continue;
            }
            /* Recheck after every dispatch: an earlier client may have locked.
             */
            if (session->io.ready && gemd_session_may_run(session))
                (void)gemd_handle_request(session);
        }

        if ((pollfds[0].revents & POLLIN) != 0) {
            gemd_accept_client();
        }

#ifdef GEM_PLATFORM_FREEBSD
        if (seat_poll_index != (nfds_t)-1 &&
            (pollfds[seat_poll_index].revents & POLLIN) != 0) {
            gem_freebsd_seat_dispatch();
        }
#endif

        gemd_pump_hid();
    }

    gemd_shutdown();
    return 0;
}
