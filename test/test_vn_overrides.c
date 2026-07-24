#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/vn_input.h"

int main(void) {
  const char* path = "/tmp/test_vn_overrides.txt";
  FILE* f = fopen(path, "w");
  fprintf(f, "# a comment\n");
  fprintf(f, "\n");
  fprintf(f, "Ghostty 15 backspace\n");
  fprintf(f, "Visual Studio Code 0 select\n");
  fprintf(f, "BadRow onlytwo\n");
  fprintf(f, "AnotherBad 10 bogus\n");
  fprintf(f, "NegativeDelay -5 backspace\n");
  fclose(f);

  struct vn_override_list list = load_vn_overrides(path);
  assert(list.count == 2);

  assert(strcmp(list.items[0].app, "Ghostty") == 0);
  assert(list.items[0].delay_us == 15000);
  assert(list.items[0].strategy == VN_STRATEGY_BACKSPACE);

  assert(strcmp(list.items[1].app, "Visual Studio Code") == 0);
  assert(list.items[1].delay_us == 0);
  assert(list.items[1].strategy == VN_STRATEGY_SELECT);

  struct vn_override_list missing = load_vn_overrides("/tmp/does_not_exist_xyz.txt");
  assert(missing.count == 0);
  assert(missing.items == NULL);

  printf("ALL TESTS PASSED\n");
  return 0;
}
