#pragma once
#ifdef __OBJC__
#include "Cocoa/Cocoa.h"
#else
// ponytail: plain-C translation units (helpers.c, tests) can't parse
// Foundation's @class/@protocol syntax; give them the C-compatible
// equivalents instead of pulling in all of Cocoa.
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
#endif
#include <sys/stat.h>
#include <stdint.h>
#include "env_vars.h"

#define FORK_TIMEOUT 60

char* string_copy(char* s);
char* cfstring_get_cstring(CFStringRef text_ref);
const char* get_name_for_pid(uint64_t pid);
const char* read_file(char* path);
bool vfork_exec(char *command, struct env_vars* env_vars);

struct string_list {
  char** items;
  uint32_t count;
};

struct string_list load_string_list(const char* path);
bool blacklist_contains(char** list, uint32_t count, char* app, char* bundle_id);
