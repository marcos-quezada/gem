/*
 * Services the Atari system traps a guest program raises under Stout:
 * XBIOS stubs, the AES parameter block dispatched onto the hosted AES with
 * field-number translation, and VDI parameter blocks with bounded copies of
 * guest arrays. Guest memory is validated before every access.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#include "stout.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

uint16_t stout_read_u16(uint32_t address)
{
    return (uint16_t)(((uint16_t)g_stout.ram[address] << 8) |
                      (uint16_t)g_stout.ram[address + 1u]);
}

uint32_t stout_read_u32(uint32_t address)
{
    return ((uint32_t)g_stout.ram[address] << 24) |
           ((uint32_t)g_stout.ram[address + 1u] << 16) |
           ((uint32_t)g_stout.ram[address + 2u] << 8) |
           (uint32_t)g_stout.ram[address + 3u];
}

/*
 * Atari numbers the window rectangle selectors WF_WORKXYWH=4 and
 * WF_CURRXYWH=5; the hosted AES header uses 4 for the outer frame and 5
 * for the work area. Guest programs speak the Atari ABI, so their field
 * numbers are translated here for wind_get and wind_set.
 */
static WORD stout_wind_field(WORD field)
{
    if (field == 4) {
        return WF_CXYWH;
    }
    if (field == 5) {
        return WF_WXYWH;
    }
    return field;
}

static int16_t stout_read_s16(uint32_t address)
{
    return (int16_t)stout_read_u16(address);
}

void stout_write_u16(uint32_t address, uint16_t value)
{
    g_stout.ram[address] = (uint8_t)((value >> 8) & 0xffu);
    g_stout.ram[address + 1u] = (uint8_t)(value & 0xffu);
}

void stout_write_u32(uint32_t address, uint32_t value)
{
    g_stout.ram[address] = (uint8_t)((value >> 24) & 0xffu);
    g_stout.ram[address + 1u] = (uint8_t)((value >> 16) & 0xffu);
    g_stout.ram[address + 2u] = (uint8_t)((value >> 8) & 0xffu);
    g_stout.ram[address + 3u] = (uint8_t)(value & 0xffu);
}

int stout_valid_range(uint32_t address, size_t size)
{
    if (address >= STOUT_RAM_SIZE) {
        return 0;
    }
    if (size > (size_t)STOUT_RAM_SIZE - (size_t)address) {
        return 0;
    }
    return 1;
}

static void stout_read_string(uint32_t address, char *buffer, size_t size)
{
    size_t index;

    if (buffer == NULL || size == 0u) {
        return;
    }

    for (index = 0u; index + 1u < size; ++index) {
        if (!stout_valid_range(address + (uint32_t)index, 1u)) {
            break;
        }
        buffer[index] = (char)g_stout.ram[address + (uint32_t)index];
        if (buffer[index] == '\0') {
            return;
        }
    }

    buffer[index] = '\0';
}

static uint32_t stout_get_reg(m68k_register_t reg)
{
    return m68k_get_reg(NULL, reg);
}

void stout_set_reg(m68k_register_t reg, uint32_t value)
{
    m68k_set_reg(reg, value);
}

static void stout_stop_with_code(int code)
{
    g_stout.running = 0;
    g_stout.exit_code = code;
}

uint32_t stout_push_return_trap(uint32_t sp)
{
    sp -= 4u;
    stout_write_u32(sp, g_stout.stop_address);
    return sp;
}

static int stout_handle_xbios(void)
{
    uint32_t sp = stout_get_reg(M68K_REG_SP);
    uint16_t opcode = stout_read_u16(sp);

    switch (opcode) {
        case 4:
            stout_set_reg(M68K_REG_D0, 2u);
            stout_set_reg(M68K_REG_SP, sp + 2u);
            return 1;
        default:
            fprintf(stderr, "stout: unsupported XBIOS opcode %u\n", opcode);
            return 0;
    }
}

static int stout_handle_aes(void)
{
    stout_aespb_t pb;
    uint32_t d0 = stout_get_reg(M68K_REG_D0);
    uint32_t d1 = stout_get_reg(M68K_REG_D1);
    uint16_t opcode;
    WORD intin0;
    WORD intin1;
    WORD intin2;
    WORD intin3;
    WORD intin4;

    if (d0 != AES_TRAP_MAGIC || !stout_valid_range(d1, sizeof(pb))) {
        return 0;
    }

    pb.control = stout_read_u32(d1);
    pb.global = stout_read_u32(d1 + 4u);
    pb.intin = stout_read_u32(d1 + 8u);
    pb.intout = stout_read_u32(d1 + 12u);
    pb.addrin = stout_read_u32(d1 + 16u);
    pb.addrout = stout_read_u32(d1 + 20u);

    opcode = stout_read_u16(pb.control);
    intin0 = stout_read_s16(pb.intin + 0u);
    intin1 = stout_read_s16(pb.intin + 2u);
    intin2 = stout_read_s16(pb.intin + 4u);
    intin3 = stout_read_s16(pb.intin + 6u);
    intin4 = stout_read_s16(pb.intin + 8u);

    switch (opcode) {
        case 10:
            stout_write_u16(pb.intout, (uint16_t)appl_init());
            return 1;
        case 19:
            stout_write_u16(pb.intout, (uint16_t)appl_exit());
            return 1;
        case 25: {
            WORD message[8];
            WORD mx;
            WORD my;
            WORD mb;
            WORD ks;
            WORD kr;
            WORD br;
            WORD event;
            uint32_t message_ptr = stout_read_u32(pb.addrin);
            size_t index;

            event = evnt_multi(
                (UWORD)intin0, (UWORD)intin1, (UWORD)intin2, (UWORD)intin3,
                (UWORD)intin4, stout_read_s16(pb.intin + 10u),
                stout_read_s16(pb.intin + 12u), stout_read_s16(pb.intin + 14u),
                stout_read_s16(pb.intin + 16u),
                (UWORD)stout_read_s16(pb.intin + 18u),
                stout_read_s16(pb.intin + 20u), stout_read_s16(pb.intin + 22u),
                stout_read_s16(pb.intin + 24u), stout_read_s16(pb.intin + 26u),
                message, (UWORD)stout_read_s16(pb.intin + 28u),
                (UWORD)stout_read_s16(pb.intin + 30u), &mx, &my, &mb, &ks, &kr,
                &br);
            stout_write_u16(pb.intout + 0u, (uint16_t)event);
            stout_write_u16(pb.intout + 2u, (uint16_t)mx);
            stout_write_u16(pb.intout + 4u, (uint16_t)my);
            stout_write_u16(pb.intout + 6u, (uint16_t)mb);
            stout_write_u16(pb.intout + 8u, (uint16_t)ks);
            stout_write_u16(pb.intout + 10u, (uint16_t)kr);
            stout_write_u16(pb.intout + 12u, (uint16_t)br);
            for (index = 0u; index < 8u; ++index) {
                stout_write_u16(message_ptr + (uint32_t)(index * 2u),
                                (uint16_t)message[index]);
            }
            return 1;
        }
        case 52: {
            char alert[256];
            uint32_t string_ptr = stout_read_u32(pb.addrin);

            stout_read_string(string_ptr, alert, sizeof(alert));
            stout_write_u16(pb.intout, (uint16_t)form_alert(intin0, alert));
            return 1;
        }
        case 77: {
            WORD wchar;
            WORD hchar;
            WORD wbox;
            WORD hbox;
            WORD handle = graf_handle(&wchar, &hchar, &wbox, &hbox);

            stout_write_u16(pb.intout + 0u, (uint16_t)handle);
            stout_write_u16(pb.intout + 2u, (uint16_t)wchar);
            stout_write_u16(pb.intout + 4u, (uint16_t)hchar);
            stout_write_u16(pb.intout + 6u, (uint16_t)wbox);
            stout_write_u16(pb.intout + 8u, (uint16_t)hbox);
            return 1;
        }
        case 78: {
            uint32_t form_ptr = stout_read_u32(pb.addrin);

            stout_write_u16(
                pb.intout,
                (uint16_t)graf_mouse(intin0, (MFORM *)(uintptr_t)form_ptr));
            return 1;
        }
        case 100:
            stout_write_u16(pb.intout,
                            (uint16_t)wind_create((UWORD)intin0, intin1, intin2,
                                                  intin3, intin4));
            return 1;
        case 101:
            stout_write_u16(
                pb.intout,
                (uint16_t)wind_open(intin0, intin1, intin2, intin3, intin4));
            return 1;
        case 102:
            stout_write_u16(pb.intout, (uint16_t)wind_close(intin0));
            return 1;
        case 103:
            stout_write_u16(pb.intout, (uint16_t)wind_delete(intin0));
            return 1;
        case 104: {
            WORD w1;
            WORD w2;
            WORD w3;
            WORD w4;
            WORD status =
                wind_get(intin0, stout_wind_field(intin1), &w1, &w2, &w3, &w4);

            stout_write_u16(pb.intout + 0u, (uint16_t)status);
            stout_write_u16(pb.intout + 2u, (uint16_t)w1);
            stout_write_u16(pb.intout + 4u, (uint16_t)w2);
            stout_write_u16(pb.intout + 6u, (uint16_t)w3);
            stout_write_u16(pb.intout + 8u, (uint16_t)w4);
            return 1;
        }
        case 105:
            if (intin1 == WF_NAME || intin1 == WF_INFO) {
                char text[256];
                uint32_t text_ptr = ((uint32_t)(uint16_t)intin2 << 16) |
                                    (uint32_t)(uint16_t)intin3;

                stout_read_string(text_ptr, text, sizeof(text));
                stout_write_u16(pb.intout,
                                (uint16_t)wind_set_str(intin0, intin1, text));
            } else {
                stout_write_u16(pb.intout, (uint16_t)wind_set(
                                               intin0, stout_wind_field(intin1),
                                               intin2, intin3, intin4,
                                               stout_read_s16(pb.intin + 10u)));
            }
            return 1;
        case 107:
            stout_write_u16(pb.intout, (uint16_t)wind_update(intin0));
            return 1;
        default:
            fprintf(stderr, "stout: unsupported AES opcode %u\n", opcode);
            return 0;
    }
}

static void stout_copy_words_from_guest(uint32_t address, WORD *words,
                                        size_t count)
{
    size_t index;

    for (index = 0u; index < count; ++index) {
        words[index] = stout_read_s16(address + (uint32_t)(index * 2u));
    }
}

static void stout_copy_words_to_guest(uint32_t address, const WORD *words,
                                      size_t count)
{
    size_t index;

    for (index = 0u; index < count; ++index) {
        stout_write_u16(address + (uint32_t)(index * 2u),
                        (uint16_t)words[index]);
    }
}

static int stout_handle_vdi(void)
{
    stout_vdipb_t pb;
    uint32_t d0 = stout_get_reg(M68K_REG_D0);
    uint32_t d1 = stout_get_reg(M68K_REG_D1);
    WORD control[12] = {0};
    WORD intin[64] = {0};
    WORD ptsin[256] = {0};
    WORD intout[64] = {0};
    WORD handle;
    WORD status;

    if (d0 != VDI_TRAP_MAGIC || !stout_valid_range(d1, sizeof(pb))) {
        return 0;
    }

    pb.control = stout_read_u32(d1);
    pb.intin = stout_read_u32(d1 + 4u);
    pb.ptsin = stout_read_u32(d1 + 8u);
    pb.intout = stout_read_u32(d1 + 12u);
    pb.ptsout = stout_read_u32(d1 + 16u);

    if (!stout_valid_range(pb.control, sizeof(control)))
        return 0;
    stout_copy_words_from_guest(pb.control, control, 12u);
    if (control[3] < 0 || control[3] > 64 || control[1] < 0 ||
        control[1] > 128 ||
        !stout_valid_range(pb.intin, (size_t)control[3] * 2u) ||
        !stout_valid_range(pb.ptsin, (size_t)control[1] * 4u)) {
        fprintf(stderr, "stout: invalid VDI parameter counts or addresses\n");
        return 0;
    }
    handle = control[6];

    if (control[3] > 0) {
        stout_copy_words_from_guest(pb.intin, intin, (size_t)control[3]);
    }
    if (control[1] > 0) {
        stout_copy_words_from_guest(pb.ptsin, ptsin, (size_t)control[1] * 2u);
    }

    switch (control[0]) {
        case 6:
            v_pline(handle, control[1], ptsin);
            return 1;
        case 9:
            v_fillarea(handle, control[1], ptsin);
            return 1;
        case 11:
            if (control[5] == 1) {
                v_bar(handle, ptsin);
                return 1;
            }
            fprintf(stderr, "stout: unsupported VDI GDP subopcode %d\n",
                    control[5]);
            return 0;
        case 15:
            status = vsl_type(handle, intin[0]);
            stout_write_u16(pb.intout, (uint16_t)status);
            return 1;
        case 16:
            status = vsl_width(handle, ptsin[0]);
            stout_write_u16(pb.intout, (uint16_t)status);
            return 1;
        case 17:
            vsl_color(handle, intin[0]);
            stout_write_u16(pb.intout, (uint16_t)intin[0]);
            return 1;
        case 23:
            status = vsf_interior(handle, intin[0]);
            stout_write_u16(pb.intout, (uint16_t)status);
            return 1;
        case 24:
            status = vsf_style(handle, intin[0]);
            stout_write_u16(pb.intout, (uint16_t)status);
            return 1;
        case 25:
            vsf_color(handle, intin[0]);
            stout_write_u16(pb.intout, (uint16_t)intin[0]);
            return 1;
        case 32:
            status = vswr_mode(handle, intin[0]);
            stout_write_u16(pb.intout, (uint16_t)status);
            return 1;
        case 100:
            v_opnvwk(intin, &handle, intout);
            control[6] = handle;
            stout_copy_words_to_guest(pb.control, control, 12u);
            stout_copy_words_to_guest(pb.intout, intout, 57u);
            stout_copy_words_to_guest(pb.ptsout, intout + 45u, 12u);
            return 1;
        case 101:
            v_clsvwk(handle);
            return 1;
        case 104:
            status = vsf_perimeter(handle, intin[0]);
            stout_write_u16(pb.intout, (uint16_t)status);
            return 1;
        case 114:
            v_bar(handle, ptsin);
            return 1;
        case 129:
            vs_clip(handle, intin[0], ptsin);
            return 1;
        default:
            fprintf(stderr, "stout: unsupported VDI opcode %d\n", control[0]);
            return 0;
    }
}

int stout_handle_trap(int trap)
{
    if (g_stout.trace_traps != 0) {
        fprintf(stderr, "stout: trap #%d d0=%u d1=0x%08x pc=0x%08x\n", trap,
                stout_get_reg(M68K_REG_D0), stout_get_reg(M68K_REG_D1),
                stout_get_reg(M68K_REG_PC));
    }

    switch (trap) {
        case 2:
            if (stout_handle_aes()) {
                return 1;
            }
            if (stout_handle_vdi()) {
                return 1;
            }
            return 0;
        case 14:
            return stout_handle_xbios();
        case STOUT_EXIT_TRAP:
            stout_stop_with_code((int)stout_get_reg(M68K_REG_D0));
            return 1;
        default:
            fprintf(stderr, "stout: unsupported trap #%d\n", trap);
            return 0;
    }
}

int stout_handle_illegal(int opcode)
{
    fprintf(stderr, "stout: illegal opcode 0x%04x at pc=0x%08x\n", opcode,
            stout_get_reg(M68K_REG_PC));
    stout_stop_with_code(1);
    return 1;
}
