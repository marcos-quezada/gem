/*
 * Manages filesystem and virtual Trash browser windows for the GEM desktop.
 * Directory, metadata and process operations are delegated to the portable
 * GEM OS and AES shell interfaces.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2026 tomaz stih
 */

#include "desktop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *desktop_browser_entry_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot != NULL && dot != name ? dot : "";
}

static int desktop_browser_entry_cmp(const void *left, const void *right)
{
    const desktop_browser_entry_t *a = (const desktop_browser_entry_t *)left;
    const desktop_browser_entry_t *b = (const desktop_browser_entry_t *)right;

    if (a->is_parent != b->is_parent) {
        return a->is_parent != 0 ? -1 : 1;
    }
    if (a->is_dir != b->is_dir) {
        return a->is_dir != 0 ? -1 : 1;
    }
    switch (g_desktop.sort_mode) {
        case DESKTOP_SORT_DATE:
            if (a->mtime != b->mtime) {
                return a->mtime > b->mtime ? -1 : 1;
            }
            break;
        case DESKTOP_SORT_SIZE:
            if (a->size_bytes != b->size_bytes) {
                return a->size_bytes > b->size_bytes ? -1 : 1;
            }
            break;
        case DESKTOP_SORT_TYPE: {
            int order = strcasecmp(desktop_browser_entry_extension(a->name),
                                   desktop_browser_entry_extension(b->name));
            if (order != 0) {
                return order;
            }
            break;
        }
        case DESKTOP_SORT_NAME:
        default:
            break;
    }
    return strcmp(a->name, b->name);
}

static int desktop_browser_name_is_app(const char *name, int executable)
{
    const char *ext;

    if (name == NULL || executable == 0) {
        return 0;
    }
    ext = strrchr(name, '.');
    if (ext != NULL &&
        (strcasecmp(ext, ".app") == 0 || strcasecmp(ext, ".prg") == 0)) {
        return 1;
    }
    if (ext != NULL &&
        (strcasecmp(ext, ".so") == 0 || strcasecmp(ext, ".a") == 0 ||
         strcasecmp(ext, ".o") == 0)) {
        return 0;
    }
    return strstr(name, "_hosted") == NULL && strcmp(name, "gemd") != 0 &&
           !(strncmp(name, "lib", 3u) == 0 && ext == NULL);
}

int desktop_browser_entry_is_launchable(const desktop_browser_window_t *browser,
                                        const desktop_browser_entry_t *entry)
{
    return browser != NULL && browser->backend == DESKTOP_BROWSER_FILES &&
           entry != NULL && entry->is_dir == 0 && entry->is_parent == 0 &&
           entry->is_app != 0;
}

static void desktop_browser_reset_entries(desktop_browser_window_t *browser,
                                          const char *path)
{
    browser->entry_count = 0;
    browser->selected_index = NIL;
    browser->top_index = 0;
    browser->last_click_ms = 0u;
    browser->last_click_index = NIL;
    (void)snprintf(browser->path, sizeof(browser->path), "%s", path);
}

static void desktop_browser_set_title(desktop_browser_window_t *browser)
{
    const char *base;

    if (browser->backend == DESKTOP_BROWSER_TRASH) {
        if (strcmp(browser->path, "trash://") == 0) {
            (void)snprintf(browser->title, sizeof(browser->title),
                           "File Manager - Trash");
        } else {
            base = strrchr(browser->path, '/');
            if (strcmp(browser->path, browser->trash_payload_root) == 0 &&
                browser->trash_root_item.name[0] != '\0') {
                base = browser->trash_root_item.name;
            } else {
                base = base != NULL && base[1] != '\0' ? base + 1 : "Trash";
            }
            (void)snprintf(browser->title, sizeof(browser->title),
                           "Trash - %.*s", 84, base);
        }
        return;
    }
    if (strcmp(browser->path, "/") == 0) {
        (void)snprintf(browser->title, sizeof(browser->title),
                       "File Manager - /");
        return;
    }
    base = strrchr(browser->path, '/');
    base = base != NULL && base[1] != '\0' ? base + 1 : browser->path;
    (void)snprintf(browser->title, sizeof(browser->title),
                   "File Manager - %.*s", 76, base);
}

static void desktop_browser_add_parent(desktop_browser_window_t *browser)
{
    desktop_browser_entry_t *parent;

    if (browser->entry_count >= DESKTOP_BROWSER_MAX_ENTRIES) {
        return;
    }
    parent = &browser->entries[browser->entry_count++];
    memset(parent, 0, sizeof(*parent));
    (void)snprintf(parent->name, sizeof(parent->name), "..");
    (void)snprintf(parent->path, sizeof(parent->path), "%s", browser->path);
    parent->is_dir = 1;
    parent->is_parent = 1;
}

static int desktop_browser_load_directory(desktop_browser_window_t *browser,
                                          const char *path, int add_parent)
{
    gem_os_dir_t directory = {0};
    gem_os_dirent_t source;

    if (browser == NULL || path == NULL ||
        gem_os_dir_open(path, &directory) == 0) {
        return 0;
    }
    desktop_browser_reset_entries(browser, path);
    if (add_parent != 0) {
        desktop_browser_add_parent(browser);
    }
    while (browser->entry_count < DESKTOP_BROWSER_MAX_ENTRIES &&
           gem_os_dir_read(&directory, &source) != 0) {
        desktop_browser_entry_t *item;

        if (strcmp(source.name, ".") == 0 || strcmp(source.name, "..") == 0) {
            continue;
        }
        item = &browser->entries[browser->entry_count];
        memset(item, 0, sizeof(*item));
        if (snprintf(item->name, sizeof(item->name), "%s", source.name) <= 0 ||
            desktop_path_join(path, source.name, item->path,
                              sizeof(item->path)) == 0) {
            continue;
        }
        item->is_dir = source.info.is_directory;
        item->is_executable = source.info.is_executable;
        item->is_app = desktop_browser_name_is_app(
            item->name, source.info.is_executable);
        item->size_bytes = source.info.size_bytes;
        item->mtime = (int64_t)(source.info.mtime_ms / 1000u);
        ++browser->entry_count;
    }
    gem_os_dir_close(&directory);
    qsort(browser->entries, (size_t)browser->entry_count,
          sizeof(browser->entries[0]), desktop_browser_entry_cmp);
    desktop_browser_set_title(browser);
    return 1;
}

typedef struct desktop_trash_load_context {
    desktop_browser_window_t *browser;
} desktop_trash_load_context_t;

static int desktop_browser_add_trash_item(const gem_trash_item_t *source,
                                          void *opaque)
{
    desktop_trash_load_context_t *context =
        (desktop_trash_load_context_t *)opaque;
    desktop_browser_window_t *browser = context->browser;
    desktop_browser_entry_t *item;

    if (browser->entry_count >= DESKTOP_BROWSER_MAX_ENTRIES) {
        return 0;
    }
    item = &browser->entries[browser->entry_count++];
    memset(item, 0, sizeof(*item));
    (void)snprintf(item->name, sizeof(item->name), "%s", source->name);
    (void)snprintf(item->path, sizeof(item->path), "%s", source->payload_path);
    item->size_bytes = source->size_bytes;
    item->mtime = source->mtime;
    item->is_dir = source->is_directory;
    item->is_executable = source->is_executable;
    item->is_trash_root_item = 1;
    item->trash_item = *source;
    return 1;
}

static int desktop_browser_load_trash(desktop_browser_window_t *browser)
{
    desktop_trash_load_context_t context;
    int result;

    desktop_browser_reset_entries(browser, "trash://");
    browser->backend = DESKTOP_BROWSER_TRASH;
    browser->trash_payload_root[0] = '\0';
    memset(&browser->trash_root_item, 0, sizeof(browser->trash_root_item));
    context.browser = browser;
    result = gem_trash_list(desktop_browser_add_trash_item, &context, NULL);
    if (result != GEM_TRASH_OK && result != GEM_TRASH_FULL) {
        return 0;
    }
    g_desktop.trash_nonempty = browser->entry_count > 0;
    qsort(browser->entries, (size_t)browser->entry_count,
          sizeof(browser->entries[0]), desktop_browser_entry_cmp);
    desktop_browser_set_title(browser);
    return 1;
}

int desktop_browser_reload(desktop_browser_window_t *browser)
{
    char path[GEM_OS_PATH_MAX];

    if (browser == NULL || browser->used == 0) {
        return 0;
    }
    if (browser->backend == DESKTOP_BROWSER_TRASH &&
        strcmp(browser->path, "trash://") == 0) {
        return desktop_browser_load_trash(browser);
    }
    /*
     * desktop_browser_load_directory() resets browser->path before loading
     * its entries.  Preserve the source string here: passing browser->path
     * as both snprintf's source and destination is undefined and can turn a
     * reload into an empty path (and entries such as "/name").
     */
    (void)snprintf(path, sizeof(path), "%s", browser->path);
    return desktop_browser_load_directory(
        browser, path,
        browser->backend == DESKTOP_BROWSER_TRASH ||
            strcmp(path, "/") != 0);
}

void desktop_browser_resort(desktop_browser_window_t *browser)
{
    if (browser == NULL || browser->entry_count <= 0) {
        return;
    }
    qsort(browser->entries, (size_t)browser->entry_count,
          sizeof(browser->entries[0]), desktop_browser_entry_cmp);
    browser->selected_index = NIL;
    browser->last_click_index = NIL;
    browser->last_click_ms = 0u;
}

desktop_browser_window_t *desktop_find_browser_by_handle(WORD handle)
{
    WORD index;

    for (index = 0; index < DESKTOP_BROWSER_MAX_WINDOWS; ++index) {
        if (g_desktop.browsers[index].used != 0 &&
            g_desktop.browsers[index].handle == handle) {
            return &g_desktop.browsers[index];
        }
    }
    return NULL;
}

static desktop_browser_window_t *desktop_alloc_browser(void)
{
    WORD index;

    for (index = 0; index < DESKTOP_BROWSER_MAX_WINDOWS; ++index) {
        if (g_desktop.browsers[index].used == 0) {
            memset(&g_desktop.browsers[index], 0,
                   sizeof(g_desktop.browsers[index]));
            g_desktop.browsers[index].selected_index = NIL;
            g_desktop.browsers[index].last_click_index = NIL;
            return &g_desktop.browsers[index];
        }
    }
    return NULL;
}

static int desktop_browser_parent_path(const char *path, char *parent,
                                       size_t parent_size)
{
    char *slash;

    if (snprintf(parent, parent_size, "%s", path) <= 0) {
        return 0;
    }
    slash = strrchr(parent, '/');
    if (slash == NULL || slash == parent) {
        (void)snprintf(parent, parent_size, "/");
    } else {
        *slash = '\0';
    }
    return 1;
}

static void desktop_browser_changed(desktop_browser_window_t *browser)
{
    wind_set_str(browser->handle, WF_NAME, browser->title);
    desktop_browser_sync_work(browser);
    desktop_update_desk_menu_labels();
    desktop_browser_redraw(browser, NULL);
}

void desktop_browser_open_entry(desktop_browser_window_t *browser, WORD index)
{
    const desktop_browser_entry_t *entry;
    char path[GEM_OS_PATH_MAX];

    if (browser == NULL || index < 0 || index >= browser->entry_count) {
        return;
    }
    entry = &browser->entries[index];
    if (browser->backend == DESKTOP_BROWSER_TRASH) {
        if (entry->is_parent != 0) {
            if (strcmp(browser->path, browser->trash_payload_root) == 0) {
                (void)desktop_browser_load_trash(browser);
            } else if (desktop_browser_parent_path(browser->path, path,
                                                   sizeof(path)) != 0 &&
                       gem_trash_resolve_directory(&browser->trash_root_item,
                                                   path, path,
                                                   sizeof(path)) ==
                           GEM_TRASH_OK) {
                (void)desktop_browser_load_directory(browser, path, 1);
            }
            desktop_browser_changed(browser);
            return;
        }
        if (entry->is_dir != 0) {
            if (entry->is_trash_root_item != 0) {
                browser->trash_root_item = entry->trash_item;
                (void)snprintf(browser->trash_payload_root,
                               sizeof(browser->trash_payload_root), "%s",
                               entry->path);
            }
            if (gem_trash_resolve_directory(&browser->trash_root_item,
                                            entry->path, path,
                                            sizeof(path)) == GEM_TRASH_OK &&
                desktop_browser_load_directory(browser, path, 1) != 0) {
                desktop_browser_changed(browser);
            }
        }
        return;
    }
    if (entry->is_parent != 0) {
        if (desktop_browser_parent_path(browser->path, path, sizeof(path)) != 0 &&
            desktop_browser_load_directory(browser, path,
                                           strcmp(path, "/") != 0) != 0) {
            desktop_browser_changed(browser);
        }
        return;
    }
    if (entry->is_dir != 0) {
        if (desktop_browser_load_directory(browser, entry->path, 1) != 0) {
            desktop_browser_changed(browser);
        }
        return;
    }
    if (desktop_browser_entry_is_launchable(browser, entry) != 0) {
        char tail[2] = {0, 0};

        if (shel_write(1, 1, 0, (char *)entry->path, tail) != 0) {
            (void)snprintf(path, sizeof(path), "Launched %.*s", 480,
                           entry->name);
        } else {
            (void)snprintf(path, sizeof(path), "Launch failed: %.*s", 470,
                           entry->name);
        }
        desktop_set_status(path);
        desktop_redraw(NULL);
    }
}

static int desktop_browser_open_window(desktop_browser_window_t *browser,
                                       WORD used_windows)
{
    GRECT outer;
    WORD work_x = (WORD)(g_desktop.work.g_x + 220 + used_windows * 24);
    WORD work_y = (WORD)(g_desktop.work.g_y + 40 + used_windows * 20);
    WORD work_w = 360;
    WORD work_h = 220;
    WORD max_x = (WORD)(g_desktop.work.g_x + g_desktop.work.g_w - work_w - 24);
    WORD max_y = (WORD)(g_desktop.work.g_y + g_desktop.work.g_h - work_h - 24);
    UWORD kind = NAME | CLOSER | MOVER | SIZER | UPARROW | DNARROW | VSLIDE;

    if (work_x > max_x) {
        work_x = max_x;
    }
    if (work_y > max_y) {
        work_y = max_y;
    }
    (void)wind_calc(WC_BORDER, kind, work_x, work_y, work_w, work_h,
                    &outer.g_x, &outer.g_y, &outer.g_w, &outer.g_h);
    browser->handle = wind_create(kind, 0, 0, g_desktop.work.g_w,
                                  g_desktop.work.g_h);
    if (browser->handle <= 0) {
        return 0;
    }
    browser->used = 1;
    browser->view_mode = g_desktop.icon_view ? DESKTOP_BROWSER_VIEW_ICONS
                                             : DESKTOP_BROWSER_VIEW_LIST;
    wind_set_str(browser->handle, WF_NAME, browser->title);
    if (wind_open(browser->handle, outer.g_x, outer.g_y, outer.g_w,
                  outer.g_h) == 0) {
        wind_delete(browser->handle);
        return 0;
    }
    g_desktop.active_browser_handle = browser->handle;
    desktop_browser_sync_work(browser);
    desktop_update_desk_menu_labels();
    desktop_browser_redraw(browser, NULL);
    return 1;
}

static WORD desktop_browser_used_count(void)
{
    WORD index;
    WORD count = 0;

    for (index = 0; index < DESKTOP_BROWSER_MAX_WINDOWS; ++index) {
        count += g_desktop.browsers[index].used != 0 ? 1 : 0;
    }
    return count;
}

void desktop_open_disk_path(const char *path, const char *label)
{
    desktop_browser_window_t *browser = desktop_alloc_browser();

    (void)label;
    if (browser == NULL) {
        desktop_set_status("No free file windows.");
    } else {
        browser->backend = DESKTOP_BROWSER_FILES;
        if (desktop_browser_load_directory(browser, path,
                                           strcmp(path, "/") != 0) == 0 ||
            desktop_browser_open_window(browser,
                                        desktop_browser_used_count()) == 0) {
            memset(browser, 0, sizeof(*browser));
            desktop_set_status("Unable to open directory.");
        }
    }
    desktop_redraw(NULL);
}

void desktop_open_trash(void)
{
    desktop_browser_window_t *browser = desktop_alloc_browser();

    if (browser == NULL) {
        desktop_set_status("No free file windows.");
    }
    if (browser != NULL &&
        (desktop_browser_load_trash(browser) == 0 ||
         desktop_browser_open_window(browser, desktop_browser_used_count()) ==
             0)) {
        memset(browser, 0, sizeof(*browser));
        desktop_set_status("Unable to open Trash.");
    }
    desktop_redraw(NULL);
}

void desktop_browser_close(desktop_browser_window_t *browser)
{
    GRECT outer;

    if (browser == NULL || browser->used == 0) {
        return;
    }
    wind_get(browser->handle, WF_WXYWH, &outer.g_x, &outer.g_y, &outer.g_w,
             &outer.g_h);
    (void)wind_close(browser->handle);
    (void)wind_delete(browser->handle);
    if (g_desktop.active_browser_handle == browser->handle) {
        g_desktop.active_browser_handle = NIL;
    }
    memset(browser, 0, sizeof(*browser));
    desktop_update_desk_menu_labels();
    desktop_redraw(&outer);
}
