#include <assert.h>
#include <stdio.h>
#include "../src/buffer.h"

int main(void) {
  struct buffer buf = {0};
  buffer_begin(&buf);

  // Simulate ax_clear()'s effect on a genuine focus change to a new element:
  // buffer_clear() zeroes cursor.mode and forces libvim into insert mode.
  buffer_clear(&buf);

  // Simulate ax_get_text()'s revsync call, which fires whenever buffer.raw
  // is NULL (exactly the state right after buffer_clear()) -- this is what
  // actually runs, in the real event, before the VN routing decision reads
  // buf.cursor.mode.
  buffer_revsync_text(&buf);

  printf("cursor.mode after clear+revsync = %u (INSERT=%u)\n", buf.cursor.mode, (unsigned) INSERT);
  assert(buf.cursor.mode & INSERT);

  printf("ALL TESTS PASSED\n");
  return 0;
}
