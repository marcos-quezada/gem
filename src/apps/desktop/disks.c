/*
 * Discovers the disks shown on the desktop: filters mount types and
 * paths, derives labels and free space, and builds the disk, folder and
 * sorted icon list.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#include "desktop.h"

#include <stdio.h>
#include <string.h>

static int desktop_mount_path_visible(const char *path)
{
    if (path == NULL || path[0] != '/') {
        return 0;
    }

    if (strcmp(path, "/") == 0) {
        return 1;
    }

    if (strncmp(path, "/boot", 5) == 0 || strncmp(path, "/dev", 4) == 0 ||
        strncmp(path, "/proc", 5) == 0 || strncmp(path, "/sys", 4) == 0) {
        return 0;
    }

    if (strncmp(path, "/media/", 7) == 0 || strncmp(path, "/mnt/", 5) == 0 ||
        strncmp(path, "/run/media/", 11) == 0) {
        return 1;
    }

    return 0;
}

static int desktop_path_space(const char *path, uint64_t *total_bytes,
                              uint64_t *avail_bytes)
{
    return gem_os_space(path, total_bytes, avail_bytes);
}

static void desktop_label_from_path(const char *path, char *label,
                                    size_t label_size)
{
    const char *base;
    size_t i;
    int new_word = 1;

    if (label_size == 0u) {
        return;
    }

    if (strcmp(path, "/") == 0) {
        strncpy(label, "ROOT", label_size - 1u);
        label[label_size - 1u] = '\0';
        return;
    }

    base = strrchr(path, '/');
    if (base != NULL && base[1] != '\0') {
        ++base;
    } else {
        base = path;
    }

    strncpy(label, base, label_size - 1u);
    label[label_size - 1u] = '\0';
    for (i = 0; label[i] != '\0'; ++i) {
        if (label[i] == ' ' || label[i] == '_' || label[i] == '-') {
            new_word = 1;
            continue;
        }
        if (new_word != 0 && label[i] >= 'a' && label[i] <= 'z') {
            label[i] = (char)(label[i] - ('a' - 'A'));
        } else if (new_word == 0 && label[i] >= 'A' && label[i] <= 'Z') {
            label[i] = (char)(label[i] + ('a' - 'A'));
        }
        new_word = 0;
    }
}

static int desktop_has_path(const char *path, int is_folder)
{
    WORD i;

    for (i = 0; i < g_desktop.icon_count; ++i) {
        if (g_desktop.icons[i].is_folder == is_folder &&
            strcmp(g_desktop.icons[i].path, path) == 0) {
            return 1;
        }
    }
    return 0;
}

static void desktop_add_disk_icon(const char *path)
{
    desktop_icon_entry_t *entry;

    if (g_desktop.icon_count >= DESKTOP_MAX_DISKS || path == NULL ||
        desktop_has_path(path, 0) != 0) {
        return;
    }

    entry = &g_desktop.icons[g_desktop.icon_count];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->path, path, sizeof(entry->path) - 1u);
    entry->path[sizeof(entry->path) - 1u] = '\0';
    desktop_label_from_path(path, entry->label, sizeof(entry->label));
    entry->asset = &desktop_disk_icon_asset;
    entry->draw_info.asset = entry->asset;
    entry->draw_info.label = entry->label;
    entry->userblk.ab_code = (LONG)(intptr_t)desktop_user_draw;
    entry->userblk.ab_parm = (LONG)(intptr_t)&entry->draw_info;
    entry->object_id = (WORD)(DESKTOP_ICON_BASE + g_desktop.icon_count);
    if (desktop_path_space(path, &entry->total_bytes, &entry->avail_bytes) ==
        0) {
        entry->total_bytes = 0;
        entry->avail_bytes = 0;
    }
    ++g_desktop.icon_count;
}

static void desktop_add_folder_icon(const char *path, const char *label)
{
    desktop_icon_entry_t *entry;

    if (g_desktop.icon_count >= DESKTOP_MAX_ICONS || path == NULL ||
        desktop_has_path(path, 1) != 0) {
        return;
    }

    entry = &g_desktop.icons[g_desktop.icon_count];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->path, path, sizeof(entry->path) - 1u);
    entry->path[sizeof(entry->path) - 1u] = '\0';
    if (label != NULL && label[0] != '\0') {
        strncpy(entry->label, label, sizeof(entry->label) - 1u);
        entry->label[sizeof(entry->label) - 1u] = '\0';
    } else {
        desktop_label_from_path(path, entry->label, sizeof(entry->label));
    }
    entry->asset = &desktop_folder_icon_asset;
    entry->draw_info.asset = entry->asset;
    entry->draw_info.label = entry->label;
    entry->userblk.ab_code = (LONG)(intptr_t)desktop_user_draw;
    entry->userblk.ab_parm = (LONG)(intptr_t)&entry->draw_info;
    entry->object_id = (WORD)(DESKTOP_ICON_BASE + g_desktop.icon_count);
    entry->is_folder = 1;
    if (desktop_path_space(path, &entry->total_bytes, &entry->avail_bytes) ==
        0) {
        entry->total_bytes = 0;
        entry->avail_bytes = 0;
    }
    ++g_desktop.icon_count;
}

void desktop_probe_disks(void)
{
    gem_os_volume_iter_t volumes = {0};
    gem_os_volume_t volume;
    char cwd[GEM_OS_PATH_MAX];
    WORD folder_count;

    g_desktop.icon_count = 0;

    if (gem_os_getcwd(cwd, sizeof(cwd)) != 0) {
        desktop_add_folder_icon(cwd, "Workspace");
    }

    folder_count = g_desktop.icon_count;
    if (gem_os_volume_iter_open(&volumes) != 0) {
        while (g_desktop.icon_count < DESKTOP_MAX_DISKS &&
               gem_os_volume_iter_read(&volumes, &volume) != 0) {
            if (desktop_mount_path_visible(volume.mount_path) == 0) {
                continue;
            }
            desktop_add_disk_icon(volume.mount_path);
        }
        gem_os_volume_iter_close(&volumes);
    }

    if (g_desktop.icon_count == folder_count) {
        desktop_add_disk_icon("/");
    }
}

void desktop_sort_icons_by_label(void)
{
    WORD i;
    WORD j;

    if (g_desktop.icon_count < 2) {
        desktop_refresh_icon_bindings();
        return;
    }

    for (i = 0; i < g_desktop.icon_count - 1; ++i) {
        for (j = (WORD)(i + 1); j < g_desktop.icon_count; ++j) {
            int swap_needed = 0;

            if (g_desktop.icons[i].is_trash != 0) {
                swap_needed = 1;
            } else if (g_desktop.icons[j].is_trash == 0 &&
                       strcmp(g_desktop.icons[i].label,
                              g_desktop.icons[j].label) > 0) {
                swap_needed = 1;
            }
            if (swap_needed != 0) {
                desktop_icon_entry_t swap = g_desktop.icons[i];

                g_desktop.icons[i] = g_desktop.icons[j];
                g_desktop.icons[j] = swap;
            }
        }
    }

    desktop_refresh_icon_bindings();
}
