#pragma once
#include <dispatch/dispatch.h>

#define CONFIG_FILE_COUNT 6

struct config_watcher {
    int fds[CONFIG_FILE_COUNT];
    dispatch_source_t sources[CONFIG_FILE_COUNT];
};

extern struct config_watcher g_config_watcher;

void config_watcher_begin(struct config_watcher* watcher);
void config_watcher_end(struct config_watcher* watcher);
