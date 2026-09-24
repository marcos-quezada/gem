/*
 * Reuses the POSIX pseudo-terminal backend for the native FreeBSD target.
 * Confirmed fully portable POSIX (posix_openpt/grantpt/unlockpt/ptsname,
 * TIOCSWINSZ/TIOCSCTTY) -- no FreeBSD-specific changes needed at all.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#include "../rasta/pty.c"
