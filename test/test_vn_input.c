#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/vn_input.h"
#include <Carbon/Carbon.h>

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

  // parse_hotkey: modifier+regular-key combos (space/a-z/0-9), additive to
  // the existing modifier-only chord support above.
  {
    int64_t keycode;
    bool has_keycode;
    bool valid;

    CGEventFlags mask = parse_hotkey("control+space", &keycode, &has_keycode, &valid);
    assert(valid);
    assert(mask == kCGEventFlagMaskControl);
    assert(has_keycode);
    assert(keycode == kVK_Space);

    mask = parse_hotkey("command+option+j", &keycode, &has_keycode, &valid);
    assert(valid);
    assert(mask == (kCGEventFlagMaskCommand | kCGEventFlagMaskAlternate));
    assert(has_keycode);
    assert(keycode == kVK_ANSI_J);

    // existing modifier-only format keeps working, with has_keycode=false
    mask = parse_hotkey("control+shift", &keycode, &has_keycode, &valid);
    assert(valid);
    assert(mask == (kCGEventFlagMaskControl | kCGEventFlagMaskShift));
    assert(!has_keycode);

    // a regular key with no modifier at all is invalid
    parse_hotkey("space", &keycode, &has_keycode, &valid);
    assert(!valid);

    // two regular keys in one combo is invalid
    parse_hotkey("control+space+j", &keycode, &has_keycode, &valid);
    assert(!valid);

    // unrecognized token is invalid (unchanged behavior)
    parse_hotkey("control+xyz", &keycode, &has_keycode, &valid);
    assert(!valid);

    printf("[parse_hotkey regular-key combos] OK\n");
  }

  // vim_status_label: 4 state combos (enabled x vim_disabled)
  {
    char label[8];
    vim_status_label(false, false, label, sizeof label);
    assert(strcmp(label, "EN") == 0);
    vim_status_label(true, false, label, sizeof label);
    assert(strcmp(label, "VI") == 0);
    vim_status_label(false, true, label, sizeof label);
    assert(strcmp(label, "EN-") == 0);
    vim_status_label(true, true, label, sizeof label);
    assert(strcmp(label, "VI-") == 0);
    printf("[vim_status_label] OK\n");
  }

  printf("ALL TESTS PASSED\n");
  return 0;
}
