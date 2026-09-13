/*
 * Runs simple Atari `.PRG` programs on a Musashi 68000 core: loads and
 * relocates the image, provides the guest memory callbacks, boots the
 * program with a return trap and drives the emulation loop. System traps
 * are serviced in traps.c.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#include "stout.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

stout_state_t g_stout;

unsigned int m68k_read_memory_8(unsigned int address)
{
    if (!stout_valid_range(address, 1u)) {
        return 0u;
    }
    return g_stout.ram[address];
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    if (!stout_valid_range(address, 2u)) {
        return 0u;
    }
    return stout_read_u16(address);
}

unsigned int m68k_read_memory_32(unsigned int address)
{
    if (!stout_valid_range(address, 4u)) {
        return 0u;
    }
    return stout_read_u32(address);
}

unsigned int m68k_read_immediate_16(unsigned int address)
{
    return m68k_read_memory_16(address);
}

unsigned int m68k_read_immediate_32(unsigned int address)
{
    return m68k_read_memory_32(address);
}

unsigned int m68k_read_pcrelative_8(unsigned int address)
{
    return m68k_read_memory_8(address);
}

unsigned int m68k_read_pcrelative_16(unsigned int address)
{
    return m68k_read_memory_16(address);
}

unsigned int m68k_read_pcrelative_32(unsigned int address)
{
    return m68k_read_memory_32(address);
}

unsigned int m68k_read_disassembler_8(unsigned int address)
{
    return m68k_read_memory_8(address);
}

unsigned int m68k_read_disassembler_16(unsigned int address)
{
    return m68k_read_memory_16(address);
}

unsigned int m68k_read_disassembler_32(unsigned int address)
{
    return m68k_read_memory_32(address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    if (!stout_valid_range(address, 1u)) {
        return;
    }
    g_stout.ram[address] = (uint8_t)(value & 0xffu);
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    if (!stout_valid_range(address, 2u)) {
        return;
    }
    stout_write_u16(address, (uint16_t)value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    if (!stout_valid_range(address, 4u)) {
        return;
    }
    stout_write_u32(address, value);
}

void m68k_write_memory_32_pd(unsigned int address, unsigned int value)
{
    m68k_write_memory_16(address + 2u, (value >> 16) & 0xffffu);
    m68k_write_memory_16(address, value & 0xffffu);
}

static int stout_find_entry(uint32_t *entry_out)
{
    static const char *const names[] = {"main", "_main", "__main"};
    size_t index;

    for (index = 0u; index < sizeof(names) / sizeof(names[0]); ++index) {
        const prg_symbol_t *symbol =
            prg_find_symbol(&g_stout.program, names[index]);

        if (symbol != NULL) {
            *entry_out = g_stout.load_address + symbol->value;
            return 1;
        }
    }

    *entry_out = g_stout.load_address;
    return 1;
}

static int stout_load_program(const char *path)
{
    char error_text[PRG_ERROR_LENGTH];

    prg_image_init(&g_stout.program);
    if (!prg_read_file(path, g_stout.load_address, &g_stout.program, error_text,
                       sizeof(error_text))) {
        fprintf(stderr, "stout: %s\n", error_text);
        return 0;
    }

    if (!stout_valid_range(g_stout.load_address, g_stout.program.image_size)) {
        fprintf(stderr, "stout: program does not fit guest RAM\n");
        return 0;
    }

    memcpy(g_stout.ram + g_stout.load_address, g_stout.program.image,
           g_stout.program.image_size);
    return 1;
}

static int stout_boot(const char *path)
{
    uint32_t entry;
    uint32_t sp;

    memset(&g_stout, 0, sizeof(g_stout));
    g_stout.ram = (uint8_t *)calloc(1u, STOUT_RAM_SIZE);
    if (g_stout.ram == NULL) {
        fprintf(stderr, "stout: out of memory\n");
        return 0;
    }

    g_stout.load_address = STOUT_LOAD_ADDRESS;
    g_stout.stack_top = STOUT_STACK_TOP;
    g_stout.stop_address = 0x00008000u;
    g_stout.trace_traps = (getenv("STOUT_TRACE") != NULL);

    stout_write_u16(g_stout.stop_address,
                    (uint16_t)(0x4e40u | STOUT_EXIT_TRAP));

    if (!stout_load_program(path)) {
        return 0;
    }
    if (!stout_find_entry(&entry)) {
        fprintf(stderr, "stout: failed to choose an entry point\n");
        return 0;
    }

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_trap_instr_callback(stout_handle_trap);
    m68k_set_illg_instr_callback(stout_handle_illegal);
    m68k_pulse_reset();

    sp = stout_push_return_trap(g_stout.stack_top);
    /* Select user mode before initializing its stack bank. */
    stout_set_reg(M68K_REG_SR, 0u);
    stout_set_reg(M68K_REG_SP, sp);
    stout_set_reg(M68K_REG_A7, sp);
    stout_set_reg(M68K_REG_PC, entry);

    g_stout.running = 1;
    return 1;
}

static int stout_run(void)
{
    uint64_t total_cycles = 0;
    uint64_t limit = STOUT_MAX_CYCLES;
    const char *setting = getenv("STOUT_MAX_CYCLES");
    if (setting) {
        char *end;
        errno = 0;
        unsigned long long value = strtoull(setting, &end, 10);
        if (!*setting || *setting == '-' || *end || errno) {
            fprintf(stderr, "stout: invalid STOUT_MAX_CYCLES\n");
            return 1;
        }
        limit = value;
    }

    while (g_stout.running != 0 && (!limit || total_cycles < limit)) {
        int cycles = m68k_execute(limit ? 1000 : 10000);
        if (cycles > 0)
            total_cycles += (unsigned)cycles;
        /* Interactive guest loops must share the host with other samples. */
        if (!limit)
            gem_os_sleep_ms(1);
    }

    if (g_stout.running != 0) {
        fprintf(stderr, "stout: execution limit reached\n");
        return 1;
    }

    return g_stout.exit_code;
}

static void stout_shutdown(void)
{
    prg_image_free(&g_stout.program);
    free(g_stout.ram);
    memset(&g_stout, 0, sizeof(g_stout));
}

int main(int argc, char **argv)
{
    int exit_code;

    if (argc != 2) {
        fprintf(stderr, "usage: stout <filename>\n");
        return 1;
    }

    if (!stout_boot(argv[1])) {
        stout_shutdown();
        return 1;
    }

    exit_code = stout_run();
    stout_shutdown();
    return exit_code;
}
