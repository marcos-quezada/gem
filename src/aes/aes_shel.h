/*
 * Declares the private AES shell service: global scrap-buffer ownership,
 * per-child launch records and gemd caller identity integration.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#ifndef GEM_AES_SHEL_H
#define GEM_AES_SHEL_H

#include "gem/aes.h"

typedef struct shel_launch {
    int appl_id;
    int pid;
    char cmd[256];
    char tail[128];
} shel_launch_t;

/* Reset global shell storage before AES begins accepting applications. */
void aes_shel_reset(void);

/* Set the OS process identity for the AES call currently being dispatched. */
void aes_shel_set_caller_pid(int pid);

/* Claim a launch-time application ID for the current caller, or return zero. */
WORD aes_shel_claim_app_id(void);

/* Reap exited children and release their launch records. */
void aes_shel_reap_children(void);

#endif
