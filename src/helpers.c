#include "helpers.h"
#include <stdio.h>
#include <string.h>

struct string_list load_string_list(const char* path) {
  struct string_list list = { NULL, 0 };

  FILE* file = fopen(path, "r");
  if (!file) return list;

  char line[255];
  while (fgets(line, sizeof(line), file)) {
    uint32_t len = strlen(line);
    if (len == 0) continue;
    if (line[len - 1] == '\n') line[--len] = '\0';
    if (len == 0) continue;

    list.items = realloc(list.items, sizeof(char*) * ++list.count);
    list.items[list.count - 1] = string_copy(line);
  }
  fclose(file);
  return list;
}

bool blacklist_contains(char** list, uint32_t count, char* app, char* bundle_id) {
  if (!app || !bundle_id) return true;
  for (uint32_t i = 0; i < count; i++)
    if (strcmp(list[i], app) == 0 || strcmp(list[i], bundle_id) == 0) return true;
  return false;
}
