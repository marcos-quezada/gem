/*
 * Declares Stout's guest machine state, the parameter block layouts and
 * the trap handlers shared between the emulator core and the trap module.
 * Private to the sample.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#ifndef GEM_SAMPLE_STOUT_H
#define GEM_SAMPLE_STOUT_H

#include <gem.h>
#include <gem/os.h>
#include <gem/aes.h>
#include <gem/vdi.h>

#include <m68k.h>

#include "prg/prg.h"

#include <stdint.h>

#define STOUT_RAM_SIZE 0x01000000u
#define STOUT_LOAD_ADDRESS 0x00010000u
#define STOUT_STACK_TOP 0x000ff000u
#define STOUT_EXIT_TRAP 15
#define STOUT_MAX_CYCLES 5000000

#define AES_TRAP_MAGIC 200u
#define VDI_TRAP_MAGIC 115u

typedef struct stout_aespb {
    uint32_t control;
    uint32_t global;
    uint32_t intin;
    uint32_t intout;
    uint32_t addrin;
    uint32_t addrout;
} stout_aespb_t;

typedef struct stout_vdipb {
    uint32_t control;
    uint32_t intin;
    uint32_t ptsin;
    uint32_t intout;
    uint32_t ptsout;
} stout_vdipb_t;

typedef struct stout_state {
    uint8_t *ram;
    prg_image_t program;
    uint32_t load_address;
    uint32_t stack_top;
    uint32_t stop_address;
    int running;
    int exit_code;
    int trace_traps;
} stout_state_t;

extern stout_state_t g_stout;

/* Service an illegal instruction as a guest fault. */
int stout_handle_illegal(int opcode);
/* Service the trap the guest just raised; nonzero when handled. */
int stout_handle_trap(int trap);
/* Push the return address that ends the program. */
uint32_t stout_push_return_trap(uint32_t sp);
/* Load a big-endian 16-bit value from guest RAM. */
uint16_t stout_read_u16(uint32_t address);
/* Load a big-endian 32-bit value from guest RAM. */
uint32_t stout_read_u32(uint32_t address);
void stout_set_reg(m68k_register_t reg, uint32_t value);
/* Nonzero when a guest span lies inside RAM. */
int stout_valid_range(uint32_t address, size_t size);
/* Store a big-endian 16-bit value into guest RAM. */
void stout_write_u16(uint32_t address, uint16_t value);
/* Store a big-endian 32-bit value into guest RAM. */
void stout_write_u32(uint32_t address, uint32_t value);

#endif
