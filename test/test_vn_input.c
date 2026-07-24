#include <assert.h>
#include <stdio.h>
#include "../src/vn_input.h"

// libvim mode bit values, copied from lib/libvim/libvim.h so this test has
// no dependency on the real vim engine (this test targets pure routing
// logic in vn_input.c, not libvim).
#define NORMAL  0x01
#define VISUAL  0x02
#define CMDLINE 0x08
#define INSERT  0x10

int main(void) {
  struct vn_input vn = { .enabled = true };

  // VN disabled entirely -> never routes, regardless of everything else.
  struct vn_input disabled = { .enabled = false };
  assert(vn_input_route(&disabled, false, true, INSERT) == VN_FLOW_NONE);
  assert(vn_input_route(&disabled, false, false, INSERT) == VN_FLOW_NONE);

  // App is VN-blacklisted -> never routes.
  assert(vn_input_route(&vn, true, true, INSERT) == VN_FLOW_NONE);
  assert(vn_input_route(&vn, true, false, INSERT) == VN_FLOW_NONE);

  // vim-mode blacklisted for this app (front_app_ignored) -> synthetic flow,
  // regardless of cursor mode (vim isn't tracking mode for this app at all).
  assert(vn_input_route(&vn, false, true, NORMAL) == VN_FLOW_SYNTHETIC);
  assert(vn_input_route(&vn, false, true, INSERT) == VN_FLOW_SYNTHETIC);

  // vim-mode active for this app -> only INSERT routes, to the vim buffer.
  assert(vn_input_route(&vn, false, false, INSERT) == VN_FLOW_VIM_BUFFER);
  assert(vn_input_route(&vn, false, false, NORMAL) == VN_FLOW_NONE);
  assert(vn_input_route(&vn, false, false, VISUAL) == VN_FLOW_NONE);
  assert(vn_input_route(&vn, false, false, CMDLINE) == VN_FLOW_NONE);

  printf("ALL TESTS PASSED\n");
  return 0;
}
