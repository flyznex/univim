#pragma once

#include <stdbool.h>
#include <stddef.h>

bool codesign_selfheal_bundle_path(const char* resolved_exe_path, char* bundle_path, size_t bundle_path_size);
void codesign_selfheal_relaunch_if_needed(int argc, char* argv[]);
