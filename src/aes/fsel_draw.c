/*
 * Lays out and paints the hosted file selector window: work-area metrics,
 * button and entry rectangles, scrolling the selection into view and the
 * dirty-rectangle repaint of the header, list and buttons.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#include "fsel_private.h"

#include <stdio.h>
#include <string.h>

void aes_fsel_sync_work(aes_fsel_state_t *state)
{
    WORD list_top;
    WORD list_bottom;
    WORD available;

    if (state == NULL) {
        return;
    }

    wind_get(state->handle, WF_CXYWH, &state->work.g_x, &state->work.g_y,
             &state->work.g_w, &state->work.g_h);
    list_top = aes_fsel_list_top(state);
    list_bottom = (WORD)(state->work.g_y + state->work.g_h -
                         AES_FSEL_BUTTON_HEIGHT - AES_FSEL_MARGIN * 2);
    available = (WORD)(list_bottom - list_top);
    state->visible_rows =
        (available > 0) ? (WORD)(available / state->row_h) : 0;
    if (state->visible_rows < 1) {
        state->visible_rows = 1;
    }
}

static void aes_fsel_draw_hline(WORD x0, WORD x1, WORD y)
{
    WORD pts[4];

    pts[0] = x0;
    pts[1] = y;
    pts[2] = x1;
    pts[3] = y;
    vsl_color(aes_state.vdi_handle, aes_dark_color());
    v_pline(aes_state.vdi_handle, 2, pts);
}

static void aes_fsel_draw_vline(WORD x, WORD y0, WORD y1)
{
    WORD pts[4];

    pts[0] = x;
    pts[1] = y0;
    pts[2] = x;
    pts[3] = y1;
    vsl_color(aes_state.vdi_handle, aes_dark_color());
    v_pline(aes_state.vdi_handle, 2, pts);
}

void aes_fsel_button_rects(const aes_fsel_state_t *state, GRECT *ok_rect,
                           GRECT *cancel_rect)
{
    WORD y;
    WORD cancel_x;
    WORD ok_x;

    if (state == NULL) {
        return;
    }

    y = (WORD)(state->work.g_y + state->work.g_h - AES_FSEL_BUTTON_HEIGHT -
               AES_FSEL_MARGIN);
    cancel_x = (WORD)(state->work.g_x + state->work.g_w - AES_FSEL_MARGIN -
                      AES_FSEL_BUTTON_WIDTH);
    ok_x = (WORD)(cancel_x - AES_FSEL_MARGIN - AES_FSEL_BUTTON_WIDTH);

    if (ok_rect != NULL) {
        ok_rect->g_x = ok_x;
        ok_rect->g_y = y;
        ok_rect->g_w = AES_FSEL_BUTTON_WIDTH;
        ok_rect->g_h = AES_FSEL_BUTTON_HEIGHT;
    }
    if (cancel_rect != NULL) {
        cancel_rect->g_x = cancel_x;
        cancel_rect->g_y = y;
        cancel_rect->g_w = AES_FSEL_BUTTON_WIDTH;
        cancel_rect->g_h = AES_FSEL_BUTTON_HEIGHT;
    }
}

WORD aes_fsel_list_top(const aes_fsel_state_t *state)
{
    return (WORD)(state->work.g_y + AES_FSEL_MARGIN +
                  AES_FSEL_HEADER_LINES * state->row_h);
}

static int aes_fsel_entry_rect(const aes_fsel_state_t *state, WORD row,
                               GRECT *rect)
{
    WORD y;

    if (state == NULL || rect == NULL || row < 0 ||
        row >= state->visible_rows) {
        return 0;
    }

    y = (WORD)(aes_fsel_list_top(state) + row * state->row_h);
    rect->g_x = (WORD)(state->work.g_x + AES_FSEL_MARGIN);
    rect->g_y = y;
    rect->g_w = (WORD)(state->work.g_w - AES_FSEL_MARGIN * 2);
    rect->g_h = state->row_h;
    return 1;
}

void aes_fsel_scroll_into_view(aes_fsel_state_t *state)
{
    WORD max_top;

    if (state == NULL || state->selected == NIL) {
        return;
    }

    if (state->selected < state->top_index) {
        state->top_index = state->selected;
    } else if (state->selected >= state->top_index + state->visible_rows) {
        state->top_index = (WORD)(state->selected - state->visible_rows + 1);
    }

    max_top = 0;
    if (state->entry_count > state->visible_rows) {
        max_top = (WORD)(state->entry_count - state->visible_rows);
    }
    if (state->top_index < 0) {
        state->top_index = 0;
    }
    if (state->top_index > max_top) {
        state->top_index = max_top;
    }
}

void aes_fsel_draw(const aes_fsel_state_t *state, const GRECT *dirty)
{
    GRECT box;
    GRECT ok_rect;
    GRECT cancel_rect;

    if (state == NULL || aes_ensure_vdi() == 0) {
        return;
    }

    aes_fsel_button_rects(state, &ok_rect, &cancel_rect);

    wind_update(BEG_UPDATE);
    wind_get(state->handle, WF_FIRSTXYWH, &box.g_x, &box.g_y, &box.g_w,
             &box.g_h);
    while (box.g_w > 0 && box.g_h > 0) {
        WORD x0 = box.g_x;
        WORD y0 = box.g_y;
        WORD x1 = (WORD)(box.g_x + box.g_w - 1);
        WORD y1 = (WORD)(box.g_y + box.g_h - 1);
        WORD clip[4];
        WORD fill[4];
        WORD line_y;
        WORD row;

        if (dirty != NULL) {
            WORD dx1 = (WORD)(dirty->g_x + dirty->g_w - 1);
            WORD dy1 = (WORD)(dirty->g_y + dirty->g_h - 1);

            if (x0 < dirty->g_x) {
                x0 = dirty->g_x;
            }
            if (y0 < dirty->g_y) {
                y0 = dirty->g_y;
            }
            if (x1 > dx1) {
                x1 = dx1;
            }
            if (y1 > dy1) {
                y1 = dy1;
            }
        }

        if (x0 <= x1 && y0 <= y1) {
            char line[AES_FSEL_LINE_LEN];

            clip[0] = x0;
            clip[1] = y0;
            clip[2] = x1;
            clip[3] = y1;
            fill[0] = state->work.g_x;
            fill[1] = state->work.g_y;
            fill[2] = (WORD)(state->work.g_x + state->work.g_w - 1);
            fill[3] = (WORD)(state->work.g_y + state->work.g_h - 1);

            vs_clip(aes_state.vdi_handle, 1, clip);
            vswr_mode(aes_state.vdi_handle, MD_REPLACE);
            vsf_interior(aes_state.vdi_handle, FIS_SOLID);
            vsl_width(aes_state.vdi_handle, 1);
            vsf_color(aes_state.vdi_handle, aes_light_color());
            vr_recfl(aes_state.vdi_handle, fill);
            vst_color(aes_state.vdi_handle, aes_dark_color());

            line_y =
                (WORD)(state->work.g_y + AES_FSEL_MARGIN + state->text_ascent);
            aes_fsel_prefix_line(line, sizeof(line),
                                 "Directory: ", state->directory);
            v_gtext(aes_state.vdi_handle,
                    (WORD)(state->work.g_x + AES_FSEL_MARGIN), line_y,
                    (CONST BYTE *)line);

            line_y = (WORD)(line_y + state->row_h);
            aes_fsel_prefix_line(line, sizeof(line),
                                 "Pattern: ", state->pattern);
            v_gtext(aes_state.vdi_handle,
                    (WORD)(state->work.g_x + AES_FSEL_MARGIN), line_y,
                    (CONST BYTE *)line);

            line_y = (WORD)(line_y + state->row_h);
            aes_fsel_prefix_line(
                line, sizeof(line), "Selected: ",
                (state->selection[0] != '\0') ? state->selection : "(none)");
            v_gtext(aes_state.vdi_handle,
                    (WORD)(state->work.g_x + AES_FSEL_MARGIN), line_y,
                    (CONST BYTE *)line);

            for (row = 0; row < state->visible_rows; ++row) {
                WORD index = (WORD)(state->top_index + row);
                GRECT rect;
                WORD rect_xy[4];

                if (!aes_fsel_entry_rect(state, row, &rect)) {
                    continue;
                }

                rect_xy[0] = rect.g_x;
                rect_xy[1] = rect.g_y;
                rect_xy[2] = (WORD)(rect.g_x + rect.g_w - 1);
                rect_xy[3] = (WORD)(rect.g_y + rect.g_h - 1);

                if (index < state->entry_count && index == state->selected) {
                    vsf_color(aes_state.vdi_handle, aes_dark_color());
                    vr_recfl(aes_state.vdi_handle, rect_xy);
                    vst_color(aes_state.vdi_handle, aes_light_color());
                } else {
                    vst_color(aes_state.vdi_handle, aes_dark_color());
                }

                if (index < state->entry_count) {
                    snprintf(line, sizeof(line), "%s%s",
                             state->entries[index].name,
                             state->entries[index].is_directory ? "/" : "");
                    v_gtext(aes_state.vdi_handle, (WORD)(rect.g_x + 2),
                            (WORD)(rect.g_y + state->text_ascent),
                            (CONST BYTE *)line);
                }
            }

            fill[0] = ok_rect.g_x;
            fill[1] = ok_rect.g_y;
            fill[2] = (WORD)(ok_rect.g_x + ok_rect.g_w - 1);
            fill[3] = (WORD)(ok_rect.g_y + ok_rect.g_h - 1);
            vsf_color(aes_state.vdi_handle, state->pressed_button == 1
                                                ? aes_dark_color()
                                                : aes_light_color());
            vst_color(aes_state.vdi_handle, state->pressed_button == 1
                                                ? aes_light_color()
                                                : aes_dark_color());
            vr_recfl(aes_state.vdi_handle, fill);
            aes_fsel_draw_hline(fill[0], fill[2], fill[1]);
            aes_fsel_draw_hline(fill[0], fill[2], fill[3]);
            aes_fsel_draw_vline(fill[0], fill[1], fill[3]);
            aes_fsel_draw_vline(fill[2], fill[1], fill[3]);
            v_gtext(aes_state.vdi_handle,
                    (WORD)(ok_rect.g_x + (ok_rect.g_w - state->char_w * 2) / 2),
                    (WORD)(ok_rect.g_y + (ok_rect.g_h - state->char_h) / 2 +
                           state->text_ascent),
                    (CONST BYTE *)"OK");

            fill[0] = cancel_rect.g_x;
            fill[1] = cancel_rect.g_y;
            fill[2] = (WORD)(cancel_rect.g_x + cancel_rect.g_w - 1);
            fill[3] = (WORD)(cancel_rect.g_y + cancel_rect.g_h - 1);
            vsf_color(aes_state.vdi_handle, state->pressed_button == 2
                                                ? aes_dark_color()
                                                : aes_light_color());
            vst_color(aes_state.vdi_handle, state->pressed_button == 2
                                                ? aes_light_color()
                                                : aes_dark_color());
            vr_recfl(aes_state.vdi_handle, fill);
            aes_fsel_draw_hline(fill[0], fill[2], fill[1]);
            aes_fsel_draw_hline(fill[0], fill[2], fill[3]);
            aes_fsel_draw_vline(fill[0], fill[1], fill[3]);
            aes_fsel_draw_vline(fill[2], fill[1], fill[3]);
            v_gtext(aes_state.vdi_handle,
                    (WORD)(cancel_rect.g_x +
                           (cancel_rect.g_w - state->char_w * 6) / 2),
                    (WORD)(cancel_rect.g_y +
                           (cancel_rect.g_h - state->char_h) / 2 +
                           state->text_ascent),
                    (CONST BYTE *)"Cancel");

            vs_clip(aes_state.vdi_handle, 0, clip);
        }

        wind_get(state->handle, WF_NEXTXYWH, &box.g_x, &box.g_y, &box.g_w,
                 &box.g_h);
    }
    wind_update(END_UPDATE);
}
