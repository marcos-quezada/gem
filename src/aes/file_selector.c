/*
 * Implements the classic blocking fsel_input() on top of the hosted window
 * manager: opens the selector window, tracks mouse and keyboard input,
 * navigates directories, restores exposed applications while waiting and
 * returns the confirmed path and file name.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#include "fsel_private.h"

#include "platform/os.h"

#include <stdlib.h>
#include <string.h>

static void aes_fsel_enter_directory(aes_fsel_state_t *state, const char *name)
{
    char path[AES_PATH_LEN];

    if (state == NULL || name == NULL) {
        return;
    }
    if (strcmp(name, "..") == 0) {
        aes_fsel_parent_dir(state->directory);
    } else if (aes_fsel_join_path(state->directory, name, path, sizeof(path)) !=
               0) {
        strncpy(state->directory, path, sizeof(state->directory) - 1u);
        state->directory[sizeof(state->directory) - 1u] = '\0';
    }

    aes_fsel_normalize_dir(state->directory);
    state->selection[0] = '\0';
    state->top_index = 0;
    aes_fsel_reload_entries(state);
    aes_fsel_scroll_into_view(state);
}

static void aes_fsel_activate(aes_fsel_state_t *state)
{
    if (state != NULL && state->selected == NIL &&
        state->selection[0] != '\0') {
        state->confirmed = 1;
        state->done = 1;
        return;
    }
    if (state == NULL || state->selected == NIL ||
        state->selected >= state->entry_count) {
        return;
    }
    state->editing_name = 0;
    if (state->entries[state->selected].is_directory != 0) {
        aes_fsel_enter_directory(state, state->entries[state->selected].name);
        return;
    }

    aes_fsel_set_selection_from_index(state);
    state->confirmed = 1;
    state->done = 1;
}

static void aes_fsel_move_selection(aes_fsel_state_t *state, WORD delta)
{
    WORD next;

    if (state == NULL || state->entry_count <= 0) {
        return;
    }

    if (state->selected == NIL) {
        state->selected = 0;
    }

    next = (WORD)(state->selected + delta);
    if (next < 0) {
        next = 0;
    }
    if (next >= state->entry_count) {
        next = (WORD)(state->entry_count - 1);
    }
    state->selected = next;
    aes_fsel_set_selection_from_index(state);
    aes_fsel_scroll_into_view(state);
}

static void aes_fsel_page_selection(aes_fsel_state_t *state, WORD delta)
{
    if (state == NULL) {
        return;
    }
    aes_fsel_move_selection(state, (WORD)(delta * state->visible_rows));
}

static void aes_fsel_accept(aes_fsel_state_t *state, char *pipath, char *pisel,
                            WORD *pbutton)
{
    if (state == NULL) {
        return;
    }

    if (pbutton != NULL) {
        *pbutton = (state->confirmed != 0) ? 1 : 0;
    }
    if (pipath != NULL) {
        char path[AES_PATH_LEN];

        if (aes_fsel_join_path(state->directory, state->pattern, path,
                               sizeof(path)) == 0) {
            path[0] = '\0';
        }
        strcpy(pipath, path);
    }
    if (pisel != NULL) {
        strcpy(pisel, state->selection);
    }
}

/* Restore an exposed owner from the pre-modal screen snapshot. The classic
 * selector is synchronous, so its caller cannot process redraws until return.
 * Visible rectangle enumeration keeps the selector and other windows intact.
 */
static void aes_fsel_restore_owner(const aes_fsel_state_t *state, WORD msg[8])
{
    GRECT box;
    MFDB screen = {0};
    if (state->background.fd_addr == NULL)
        return;
    wind_update(BEG_UPDATE);
    wind_get(msg[3], WF_FIRSTXYWH, &box.g_x, &box.g_y, &box.g_w, &box.g_h);
    while (box.g_w > 0 && box.g_h > 0) {
        WORD pxy[8];
        GRECT dirty = {msg[4], msg[5], msg[6], msg[7]};
        if (aes_intersect_rects(&box, &dirty, &box)) {
            pxy[0] = pxy[4] = box.g_x;
            pxy[1] = pxy[5] = box.g_y;
            pxy[2] = pxy[6] = (WORD)(box.g_x + box.g_w - 1);
            pxy[3] = pxy[7] = (WORD)(box.g_y + box.g_h - 1);
            vs_clip(aes_state.vdi_handle, 0, NULL);
            vro_cpyfm(aes_state.vdi_handle, S_ONLY, pxy,
                      (MFDB *)&state->background, &screen);
        }
        wind_get(msg[3], WF_NEXTXYWH, &box.g_x, &box.g_y, &box.g_w, &box.g_h);
    }
    wind_update(END_UPDATE);
}

WORD fsel_input(char *pipath, char *pisel, WORD *pbutton)
{
    aes_fsel_state_t state;
    GRECT desk;
    WORD msg[8];

    memset(&state, 0, sizeof(state));
    aes_fsel_split_input(
        pipath, pisel, state.directory, sizeof(state.directory), state.pattern,
        sizeof(state.pattern), state.selection, sizeof(state.selection));

    if (aes_ensure_vdi() == 0) {
        if (pbutton != NULL) {
            *pbutton = 0;
        }
        return 0;
    }

    state.handle = wind_create(NAME | CLOSER | MOVER, 0, 0, 520, 320);
    if (state.handle <= 0) {
        if (pbutton != NULL) {
            *pbutton = 0;
        }
        return 0;
    }

    state.char_w = vdi_string_width("M");
    state.char_h = vdi_font_text_height();
    state.text_ascent = vdi_font_ascent();
    state.row_h = (WORD)(vdi_font_text_height() + 4);
    aes_fsel_reload_entries(&state);

    wind_get(0, WF_CXYWH, &desk.g_x, &desk.g_y, &desk.g_w, &desk.g_h);
    {
        MFDB screen = {0};
        WORD pxy[8] = {0};
        state.background.fd_w = (WORD)(desk.g_x + desk.g_w);
        state.background.fd_h = (WORD)(desk.g_y + desk.g_h);
        state.background.fd_wdwidth = (WORD)((state.background.fd_w + 15) / 16);
        state.background.fd_nplanes = 1;
        state.background.fd_addr =
            calloc((size_t)state.background.fd_wdwidth * 2,
                   (size_t)state.background.fd_h);
        pxy[2] = pxy[6] = (WORD)(state.background.fd_w - 1);
        pxy[3] = pxy[7] = (WORD)(state.background.fd_h - 1);
        if (state.background.fd_addr != NULL)
            vro_cpyfm(aes_state.vdi_handle, S_ONLY, pxy, &screen,
                      &state.background);
    }
    wind_open(state.handle, (WORD)(desk.g_x + 36), (WORD)(desk.g_y + 30), 520,
              320);
    wind_set_str(state.handle, WF_NAME, "File Selector");
    aes_fsel_sync_work(&state);
    aes_fsel_scroll_into_view(&state);
    aes_fsel_draw(&state, NULL);

    while (state.done == 0) {
        WORD event;
        WORD mx = 0;
        WORD my = 0;
        WORD mb = 0;
        WORD ks = 0;
        WORD kr = 0;
        WORD br = 0;

        if (aes_wait_hook && !aes_wait_hook())
            break;

        event = evnt_multi(MU_MESAG | MU_BUTTON | MU_KEYBD, 1, 1,
                           state.pressed_button != 0 ? 0 : 1, 0, 0, 0, 0, 0, 0,
                           0, 0, 0, 0, msg, 0, 0, &mx, &my, &mb, &ks, &kr, &br);
        (void)mb;
        (void)ks;
        (void)br;

        if ((event & MU_MESAG) && msg[0] == WM_REDRAW && msg[3] != state.handle)
            aes_fsel_restore_owner(&state, msg);

        if ((event & MU_MESAG) != 0 && msg[3] == state.handle) {
            switch (msg[0]) {
                case WM_REDRAW: {
                    GRECT dirty;

                    dirty.g_x = msg[4];
                    dirty.g_y = msg[5];
                    dirty.g_w = msg[6];
                    dirty.g_h = msg[7];
                    aes_fsel_sync_work(&state);
                    aes_fsel_draw(&state, &dirty);
                    break;
                }
                case WM_TOPPED:
                    wind_set(state.handle, WF_TOP, 0, 0, 0, 0);
                    break;
                case WM_MOVED:
                    wind_set(state.handle, WF_WXYWH, msg[4], msg[5], msg[6],
                             msg[7]);
                    aes_fsel_sync_work(&state);
                    aes_fsel_scroll_into_view(&state);
                    aes_fsel_draw(&state, NULL);
                    break;
                case WM_CLOSED:
                    state.confirmed = 0;
                    state.done = 1;
                    break;
                default:
                    break;
            }
        }

        if ((event & MU_KEYBD) != 0) {
            WORD ascii = (WORD)(kr & 0xffu);
            WORD scancode = (WORD)((kr >> 8) & 0xffu);

            if (ascii == 27) {
                state.confirmed = 0;
                state.done = 1;
            } else if (ascii == '\r' || ascii == '\n' || scancode == 40 ||
                       scancode == 28) {
                aes_fsel_activate(&state);
                aes_fsel_draw(&state, NULL);
            } else if (ascii == '\b' || scancode == 42 || scancode == 14) {
                if (state.editing_name && state.selection[0] != '\0')
                    state.selection[strlen(state.selection) - 1] = '\0';
                else
                    aes_fsel_enter_directory(&state, "..");
                aes_fsel_draw(&state, NULL);
            } else if (scancode == 81 || scancode == 80) {
                aes_fsel_move_selection(&state, 1);
                aes_fsel_draw(&state, NULL);
            } else if (scancode == 82 || scancode == 72) {
                aes_fsel_move_selection(&state, -1);
                aes_fsel_draw(&state, NULL);
            } else if (scancode == 78 || scancode == 81) {
                aes_fsel_page_selection(&state, 1);
                aes_fsel_draw(&state, NULL);
            } else if (scancode == 75) {
                aes_fsel_page_selection(&state, -1);
                aes_fsel_draw(&state, NULL);
            } else if (ascii >= 32 && ascii < 127 && ascii != '/') {
                size_t length;
                if (!state.editing_name)
                    state.selection[0] = '\0';
                state.editing_name = 1;
                state.selected = NIL;
                length = strlen(state.selection);
                if (length + 1 < sizeof(state.selection)) {
                    state.selection[length] = (char)ascii;
                    state.selection[length + 1] = '\0';
                }
                aes_fsel_draw(&state, NULL);
            }
        }

        if ((event & MU_BUTTON) != 0 && state.pressed_button != 0) {
            GRECT ok_rect;
            GRECT cancel_rect;
            const WORD pressed = state.pressed_button;
            state.pressed_button = 0;
            aes_fsel_button_rects(&state, &ok_rect, &cancel_rect);
            aes_fsel_draw(&state, pressed == 1 ? &ok_rect : &cancel_rect);
            if (wind_find(mx, my) == state.handle) {
                if (pressed == 1 && aes_point_in_rect(mx, my, &ok_rect)) {
                    aes_fsel_activate(&state);
                    if (!state.done)
                        aes_fsel_draw(&state, NULL);
                } else if (pressed == 2 &&
                           aes_point_in_rect(mx, my, &cancel_rect)) {
                    state.confirmed = 0;
                    state.done = 1;
                }
            }
            continue;
        }
        if ((event & MU_BUTTON) != 0 && wind_find(mx, my) == state.handle) {
            GRECT ok_rect;
            GRECT cancel_rect;
            WORD row;

            aes_fsel_button_rects(&state, &ok_rect, &cancel_rect);
            if (aes_point_in_rect(mx, my, &ok_rect) != 0) {
                state.pressed_button = 1;
                aes_fsel_draw(&state, &ok_rect);
                continue;
            }
            if (aes_point_in_rect(mx, my, &cancel_rect) != 0) {
                state.pressed_button = 2;
                aes_fsel_draw(&state, &cancel_rect);
                continue;
            }

            row = (WORD)((my - aes_fsel_list_top(&state)) / state.row_h);
            if (row >= 0 && row < state.visible_rows) {
                WORD index = (WORD)(state.top_index + row);

                if (index < state.entry_count) {
                    if (state.selected == index) {
                        aes_fsel_activate(&state);
                        if (!state.done)
                            aes_fsel_draw(&state, NULL);
                    } else {
                        state.selected = index;
                        aes_fsel_set_selection_from_index(&state);
                        aes_fsel_scroll_into_view(&state);
                        aes_fsel_draw(&state, NULL);
                    }
                }
            }
        }
    }

    aes_fsel_accept(&state, pipath, pisel, pbutton);
    wind_close(state.handle);
    wind_delete(state.handle);
    free(state.background.fd_addr);
    return 1;
}
