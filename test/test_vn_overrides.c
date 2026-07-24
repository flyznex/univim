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
  fprintf(f, "com.apple.Terminal 20 backspace\n");
  fprintf(f, "BadRow onlytwo\n");
  fprintf(f, "AnotherBad 10 bogus\n");
  fprintf(f, "NegativeDelay -5 backspace\n");
  fprintf(f, "Ghostty 999 select\n"); // duplicate app row -- first one should win
  fclose(f);

  struct vn_override_list list = load_vn_overrides(path);
  assert(list.count == 4);

  assert(strcmp(list.items[0].app, "Ghostty") == 0);
  assert(list.items[0].delay_us == 15000);
  assert(list.items[0].strategy == VN_STRATEGY_BACKSPACE);

  assert(strcmp(list.items[1].app, "Visual Studio Code") == 0);
  assert(list.items[1].delay_us == 0);
  assert(list.items[1].strategy == VN_STRATEGY_SELECT);

  struct vn_override_list missing = load_vn_overrides("/tmp/does_not_exist_xyz.txt");
  assert(missing.count == 0);
  assert(missing.items == NULL);

  // vn_input_lookup_override: matched by app name.
  struct vn_input vn = { .overrides = list.items, .overrides_count = list.count };
  int delay_us;
  enum vn_correction_strategy strategy;

  vn_input_lookup_override(&vn, "Ghostty", "com.mitchellh.ghostty", &delay_us, &strategy);
  assert(delay_us == 15000);
  assert(strategy == VN_STRATEGY_BACKSPACE);

  // matched by bundle id, not app name.
  vn_input_lookup_override(&vn, "Terminal", "com.apple.Terminal", &delay_us, &strategy);
  assert(delay_us == 20000);
  assert(strategy == VN_STRATEGY_BACKSPACE);

  // select strategy resolves too, even though its delay_ms was 0 in the file.
  vn_input_lookup_override(&vn, "Visual Studio Code", "com.microsoft.VSCode", &delay_us, &strategy);
  assert(delay_us == 0);
  assert(strategy == VN_STRATEGY_SELECT);

  // no matching row anywhere -> defaults.
  vn_input_lookup_override(&vn, "Some Other App", "com.example.other", &delay_us, &strategy);
  assert(delay_us == 5000);
  assert(strategy == VN_STRATEGY_BACKSPACE);

  // duplicate app rows -> the first matching row in the file wins, not the last.
  vn_input_lookup_override(&vn, "Ghostty", "com.mitchellh.ghostty", &delay_us, &strategy);
  assert(delay_us == 15000);
  assert(strategy == VN_STRATEGY_BACKSPACE);

  // empty overrides list (as if the file didn't exist) -> defaults.
  struct vn_input empty_vn = { .overrides = NULL, .overrides_count = 0 };
  vn_input_lookup_override(&empty_vn, "Anything", "com.example.anything", &delay_us, &strategy);
  assert(delay_us == 5000);
  assert(strategy == VN_STRATEGY_BACKSPACE);

  printf("ALL TESTS PASSED\n");
  return 0;
}
