/*
 * Renders text from the loaded GEM fonts: glyph lookup through the
 * validated offset table, per-character and string metrics, clipped
 * glyph painting with opaque or transparent background, and the scancode
 * fallback used by the console input path.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#include "fonts_private.h"

#include <string.h>

static WORD glyph_index(const vdi_font_t *font, unsigned int ch)
{
    if (font == NULL || !font->resident) {
        return -1;
    }
    if (ch < (unsigned int)font->first_ade ||
        ch > (unsigned int)font->last_ade) {
        return -1;
    }
    return (WORD)(ch - (unsigned int)font->first_ade);
}

static WORD glyph_start_bit(const vdi_font_t *font, WORD index)
{
    const uint8_t *table;

    if (font == NULL || !font->resident || index < 0) {
        return 0;
    }

    table = font->data + font->off_offset + (size_t)index * 2u;
    return (WORD)vdi_font_read_le16(table);
}

static WORD glyph_end_bit(const vdi_font_t *font, WORD index)
{
    const uint8_t *table;

    if (font == NULL || !font->resident || index < 0) {
        return 0;
    }

    table = font->data + font->off_offset + (size_t)(index + 1) * 2u;
    return (WORD)vdi_font_read_le16(table);
}

static WORD glyph_width(const vdi_font_t *font, unsigned int ch)
{
    WORD index;

    if (font == NULL || !font->resident) {
        return 0;
    }
    if (font->uniform_width > 0) {
        return font->uniform_width;
    }
    index = glyph_index(font, ch);
    if (index < 0) {
        return font->max_char_width;
    }
    return (WORD)(glyph_end_bit(font, index) - glyph_start_bit(font, index));
}

WORD vdi_font_cell_width(void)
{
    vdi_font_t *font = vdi_current_font();

    return (font != NULL) ? font->max_char_width : 0;
}

WORD vdi_font_text_height(void)
{
    vdi_font_t *font = vdi_current_font();

    return (font != NULL) ? font->form_height : 0;
}

WORD vdi_font_ascent(void)
{
    vdi_font_t *font = vdi_current_font();

    return (font != NULL) ? font->ascent : 0;
}

WORD vdi_font_first_ade(void)
{
    vdi_font_t *font = vdi_current_font();

    return (font != NULL) ? font->first_ade : 0;
}

WORD vdi_font_last_ade(void)
{
    vdi_font_t *font = vdi_current_font();

    return (font != NULL) ? font->last_ade : 0;
}

WORD vdi_char_cell_width(char ch)
{
    vdi_font_t *font = vdi_current_font();

    return glyph_width(font, (unsigned char)ch);
}

WORD vdi_string_width(const char *string)
{
    vdi_font_t *font;
    WORD width;

    if (string == NULL) {
        return 0;
    }

    font = vdi_current_font();
    if (font != NULL && font->uniform_width > 0) {
        return (WORD)(strlen(string) * (size_t)font->uniform_width);
    }

    width = 0;
    while (*string != '\0') {
        width = (WORD)(width + vdi_char_cell_width(*string));
        ++string;
    }
    return width;
}

void vdi_draw_glyph(WORD x, WORD y, char ch, WORD color)
{
    vdi_font_t *font = vdi_current_font();
    vdi_rect_t clip;
    WORD opaque_background = vdi_text_background_mode();
    WORD index;
    WORD start_bit;
    WORD width;
    WORD clip_col0;
    WORD clip_col1;
    WORD row_index;

    if (font == NULL) {
        return;
    }

    index = glyph_index(font, (unsigned char)ch);
    if (index < 0) {
        index = glyph_index(font, (unsigned char)'?');
        if (index < 0) {
            return;
        }
    }

    if (font->uniform_width > 0) {
        start_bit = (WORD)((LONG)index * font->uniform_width);
        width = font->uniform_width;
    } else {
        start_bit = glyph_start_bit(font, index);
        width = (WORD)(glyph_end_bit(font, index) - start_bit);
    }
    if (width <= 0) {
        return;
    }

    vdi_get_active_clip_rect(&clip);

    /* Compare edges in LONG: x near the WORD limits makes x + width wrap,
     * which would pass the clip test and then write outside the row. Once
     * the glyph overlaps the clip, both column offsets fit a WORD. */
    if ((LONG)x > clip.x1 || (LONG)x + width - 1 < clip.x0) {
        return;
    }
    clip_col0 = (clip.x0 > x) ? (WORD)(clip.x0 - x) : 0;
    clip_col1 = ((LONG)x + width - 1 > clip.x1) ? (WORD)(clip.x1 - x)
                                                : (WORD)(width - 1);
    if (clip_col0 > clip_col1) {
        return;
    }

    vdi_prepare_screen_write();
    vdi_mark_dirty((WORD)(x + clip_col0), y, (WORD)(x + clip_col1),
                   (WORD)(y + font->form_height - 1));

    for (row_index = 0; row_index < font->form_height; ++row_index) {
        WORD draw_y = (WORD)(y + row_index);
        const uint8_t *glyph_row;
        WORD col;

        if (draw_y < clip.y0 || draw_y > clip.y1) {
            continue;
        }

        if (opaque_background != 0) {
            vdi_draw_screen_hline_direct(draw_y, (WORD)(x + clip_col0),
                                         (WORD)(x + clip_col1), 0);
        }

        glyph_row = font->data + font->data_offset +
                    (size_t)row_index * (size_t)font->form_width;

        col = clip_col0;
        while (col <= clip_col1) {
            WORD abs_bit = (WORD)(start_bit + col);
            size_t byte_off = (size_t)abs_bit / 8u;
            unsigned int bit_in_byte = 7u - ((unsigned int)abs_bit & 7u);

            if (byte_off < font->data_size &&
                !(glyph_row[byte_off] & (uint8_t)(1u << bit_in_byte))) {
                ++col;
                continue;
            }

            {
                WORD run_start = col;

                ++col;
                while (col <= clip_col1) {
                    abs_bit = (WORD)(start_bit + col);
                    byte_off = (size_t)abs_bit / 8u;
                    bit_in_byte = 7u - ((unsigned int)abs_bit & 7u);
                    if (byte_off >= font->data_size ||
                        !(glyph_row[byte_off] & (uint8_t)(1u << bit_in_byte))) {
                        break;
                    }
                    ++col;
                }
                vdi_draw_screen_hline_direct(draw_y, (WORD)(x + run_start),
                                             (WORD)(x + col - 1), color);
            }
        }
    }
}

char vdi_scancode_to_ascii(uint16_t key)
{
    if (key >= 4u && key <= 29u) {
        return (char)('a' + (char)(key - 4u));
    }
    if (key >= 30u && key <= 38u) {
        return (char)('1' + (char)(key - 30u));
    }

    switch (key) {
        case 39u:
            return '0';
        case 40u:
            return '\n';
        case 42u:
            return '\b';
        case 44u:
            return ' ';
        case 45u:
            return '-';
        case 47u:
            return '[';
        case 48u:
            return ']';
        case 51u:
            return ';';
        case 52u:
            return '\'';
        case 54u:
            return ',';
        case 55u:
            return '.';
        case 56u:
            return '/';
        default:
            return '\0';
    }
}
