/*
 * Reuses the POSIX operating system abstraction backend for the native
 * FreeBSD target. rasta/os.c already carries a FreeBSD-specific branch
 * for the two genuinely non-portable pieces (mount enumeration via
 * getfsstat(2), and pipe2() visibility under FreeBSD's strict-POSIX
 * feature-test macro rules) -- everything else in it is plain POSIX,
 * confirmed to build and link cleanly on FreeBSD this session.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#include "../rasta/os.c"
