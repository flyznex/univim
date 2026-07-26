#include "config_watcher.h"
#include "event_tap.h"
#include "vn_input.h"
#include "vn_engine.h"
#include "buffer.h"
#include "helpers.h"
#include "ax.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct config_watcher g_config_watcher;

typedef void (*reload_fn)(void);

static const char* config_names[CONFIG_FILE_COUNT] = {
    "blacklist",
    "vn_blacklist",
    "vn_config",
    "vn_overrides",
    "svimrc",
    "vn_macros"
};

// Forward declarations of reload functions
static void reload_blacklist(void);
static void reload_vn_blacklist(void);
static void reload_vn_config(void);
static void reload_vn_overrides(void);
static void reload_svimrc(void);
static void reload_vn_macros(void);

static reload_fn reload_functions[CONFIG_FILE_COUNT] = {
    reload_blacklist,
    reload_vn_blacklist,
    reload_vn_config,
    reload_vn_overrides,
    reload_svimrc,
    reload_vn_macros
};

static void reload_blacklist(void) {
    // Free old blacklist
    for (uint32_t i = 0; i < g_event_tap.blacklist_count; i++) {
        if (g_event_tap.blacklist[i]) free(g_event_tap.blacklist[i]);
    }
    if (g_event_tap.blacklist) free(g_event_tap.blacklist);

    // Load new blacklist
    char* home = getenv("HOME");
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/univim/blacklist", home);
    struct string_list list = load_string_list(path);
    g_event_tap.blacklist = list.items;
    g_event_tap.blacklist_count = list.count;

    // Re-evaluate current frontmost app (requires workspace to re-check)
    vn_debug_log("config_watcher: reloaded blacklist (%u entries)", list.count);
}

static void reload_vn_blacklist(void) {
    // Free old blacklist
    for (uint32_t i = 0; i < g_vn_input.blacklist_count; i++) {
        if (g_vn_input.blacklist[i]) free(g_vn_input.blacklist[i]);
    }
    if (g_vn_input.blacklist) free(g_vn_input.blacklist);

    // Load new blacklist
    char* home = getenv("HOME");
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/univim/vn_blacklist", home);
    struct string_list list = load_string_list(path);
    g_vn_input.blacklist = list.items;
    g_vn_input.blacklist_count = list.count;

    vn_debug_log("config_watcher: reloaded vn_blacklist (%u entries)", list.count);
}

static void reload_vn_config(void) {
    vn_input_reload_config(&g_vn_input);
    vn_debug_log("config_watcher: reloaded vn_config");
}

static void reload_vn_overrides(void) {
    // Free old overrides
    for (uint32_t i = 0; i < g_vn_input.overrides_count; i++) {
        if (g_vn_input.overrides[i].app) free(g_vn_input.overrides[i].app);
    }
    if (g_vn_input.overrides) free(g_vn_input.overrides);

    // Load new overrides
    char* home = getenv("HOME");
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/univim/vn_overrides", home);
    struct vn_override_list overrides = load_vn_overrides(path);
    g_vn_input.overrides = overrides.items;
    g_vn_input.overrides_count = overrides.count;

    vn_debug_log("config_watcher: reloaded vn_overrides (%u entries)", overrides.count);
}

static void reload_svimrc(void) {
    buffer_reload_svimrc(&g_ax.buffer);
    vn_debug_log("config_watcher: reloaded svimrc");
}

static void reload_vn_macros(void) {
    char* home = getenv("HOME");
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/univim/vn_macros", home);
    vn_engine_load_macros(path);
    vn_debug_log("config_watcher: reloaded vn_macros");
}

static void watch_file(struct config_watcher* watcher, int index) {
    char* home = getenv("HOME");
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/univim/%s", home, config_names[index]);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        watcher->fds[index] = -1;
        watcher->sources[index] = NULL;
        return;
    }

    watcher->fds[index] = fd;

    dispatch_source_t source = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_VNODE,
        fd,
        DISPATCH_VNODE_WRITE | DISPATCH_VNODE_DELETE | DISPATCH_VNODE_RENAME,
        dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0)
    );

    if (!source) {
        close(fd);
        watcher->fds[index] = -1;
        watcher->sources[index] = NULL;
        return;
    }

    watcher->sources[index] = source;

    // Capture index for the handler
    int captured_index = index;

    dispatch_source_set_event_handler(source, ^{
        unsigned long flags = dispatch_source_get_data(source);

        // Dispatch reload to main queue for thread safety
        dispatch_async(dispatch_get_main_queue(), ^{
            reload_functions[captured_index]();
        });

        // If file was deleted or renamed, try to re-watch
        if (flags & (DISPATCH_VNODE_DELETE | DISPATCH_VNODE_RENAME)) {
            dispatch_source_cancel(source);
            dispatch_async(dispatch_get_main_queue(), ^{
                // Re-open and re-watch after a brief delay
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC),
                               dispatch_get_main_queue(), ^{
                    watch_file(watcher, captured_index);
                });
            });
        }
    });

    dispatch_source_set_cancel_handler(source, ^{
        close(fd);
    });

    dispatch_resume(source);
}

void config_watcher_begin(struct config_watcher* watcher) {
    for (int i = 0; i < CONFIG_FILE_COUNT; i++) {
        watcher->fds[i] = -1;
        watcher->sources[i] = NULL;
        watch_file(watcher, i);
    }
}

void config_watcher_end(struct config_watcher* watcher) {
    for (int i = 0; i < CONFIG_FILE_COUNT; i++) {
        if (watcher->sources[i]) {
            dispatch_source_cancel(watcher->sources[i]);
            watcher->sources[i] = NULL;
        }
        // fd is closed by cancel handler
        watcher->fds[i] = -1;
    }
}
