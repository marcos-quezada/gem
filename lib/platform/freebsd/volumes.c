/*
 * Reuses the POSIX volume enumeration backend for the native FreeBSD
 * target. rasta/volumes.c already carries a FreeBSD-specific branch using
 * getfsstat(2) in place of Linux's mntent.h/proc-mounts approach, with
 * its own FreeBSD pseudo-filesystem ignore list.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#include "../rasta/volumes.c"
