/*
 * Declares the hosted file selector's private state and the helpers shared
 * by its directory model, painting and interaction modules. These are
 * libaes implementation details, not public GEM interfaces.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#ifndef GEM_AES_FSEL_PRIVATE_H
#define GEM_AES_FSEL_PRIVATE_H

#include "aes_internal.h"

#include <stddef.h>
#include <stdint.h>

enum {
    AES_FSEL_MAX_ENTRIES = 256,
    AES_FSEL_LINE_LEN = AES_PATH_LEN,
    AES_FSEL_MARGIN = 6,
    AES_FSEL_HEADER_LINES = 3,
    AES_FSEL_BUTTON_HEIGHT = 30,
    AES_FSEL_BUTTON_WIDTH = 80
};

typedef struct aes_fsel_entry {
    char name[AES_PATH_LEN];
    WORD is_directory;
} aes_fsel_entry_t;

typedef struct aes_fsel_state {
    WORD handle;
    WORD char_w;
    WORD char_h;
    WORD text_ascent;
    WORD row_h;
    WORD visible_rows;
    WORD top_index;
    WORD selected;
    WORD entry_count;
    WORD done;
    WORD confirmed;
    GRECT work;
    MFDB background;
    WORD editing_name;
    WORD pressed_button;
    char directory[AES_PATH_LEN];
    char pattern[AES_PATH_LEN];
    char selection[AES_PATH_LEN];
    aes_fsel_entry_t entries[AES_FSEL_MAX_ENTRIES];
} aes_fsel_state_t;

/* Strip trailing separators, keeping a lone root. */
void aes_fsel_normalize_dir(char *path);
/* Replace a directory path with its parent. */
void aes_fsel_parent_dir(char *path);
/* Split the caller's path and selection into directory, pattern and name. */
void aes_fsel_split_input(const char *pipath, const char *pisel,
                          char *directory, size_t directory_size, char *pattern,
                          size_t pattern_size, char *selection,
                          size_t selection_size);
/* Join a directory and name; nonzero when the result fits. */
int aes_fsel_join_path(const char *directory, const char *name, char *path,
                       size_t path_size);
/* Format "prefix value" into a bounded line. */
void aes_fsel_prefix_line(char *dst, size_t dst_size, const char *prefix,
                          const char *value);
/* Copy the highlighted entry into the selection text. */
void aes_fsel_set_selection_from_index(aes_fsel_state_t *state);
/* Rescan the directory through the pattern, yielding periodically. */
void aes_fsel_reload_entries(aes_fsel_state_t *state);
/* Refresh cached metrics from the selector window's work area. */
void aes_fsel_sync_work(aes_fsel_state_t *state);
/* Compute the OK and Cancel button rectangles. */
void aes_fsel_button_rects(const aes_fsel_state_t *state, GRECT *ok_rect,
                           GRECT *cancel_rect);
/* Y coordinate of the first list row. */
WORD aes_fsel_list_top(const aes_fsel_state_t *state);
/* Adjust the top index so the selection is visible. */
void aes_fsel_scroll_into_view(aes_fsel_state_t *state);
/* Repaint the selector, limited to dirty when given. */
void aes_fsel_draw(const aes_fsel_state_t *state, const GRECT *dirty);

#endif
