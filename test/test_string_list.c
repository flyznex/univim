#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/helpers.h"

int main(void) {
  const char* path = "/tmp/test_string_list.txt";
  FILE* f = fopen(path, "w");
  fprintf(f, "Terminal\ncom.apple.Terminal\n\n");
  fclose(f);

  struct string_list list = load_string_list(path);
  assert(list.count == 2);
  assert(strcmp(list.items[0], "Terminal") == 0);
  assert(strcmp(list.items[1], "com.apple.Terminal") == 0);

  assert(blacklist_contains(list.items, list.count, "Terminal", "com.example.other") == true);
  assert(blacklist_contains(list.items, list.count, "Other", "com.apple.Terminal") == true);
  assert(blacklist_contains(list.items, list.count, "Other", "com.example.other") == false);

  struct string_list missing = load_string_list("/tmp/does_not_exist_xyz.txt");
  assert(missing.count == 0);
  assert(missing.items == NULL);

  printf("ALL TESTS PASSED\n");
  return 0;
}
