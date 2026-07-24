#include "event_tap.h"
#include "helpers.h"
#include "vn_input.h"
#include <Carbon/Carbon.h> // kVK_Delete

#define VN_SYNTH_TAG 0x564E5359 // 'VNSY'
#define VN_HOTKEY_RELEVANT_FLAGS (kCGEventFlagMaskControl | kCGEventFlagMaskShift | kCGEventFlagMaskCommand | kCGEventFlagMaskAlternate)

bool event_tap_check_blacklist(struct event_tap* event_tap,
                               char* app, char* bundle_id  ) {
  return blacklist_contains(event_tap->blacklist, event_tap->blacklist_count, app, bundle_id);
}

void vn_post_correction(int backspace_count, const unsigned char* insert_text, int insert_len) {
  CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);

  for (int i = 0; i < backspace_count; i++) {
    CGEventRef down = CGEventCreateKeyboardEvent(source, kVK_Delete, true);
    CGEventRef up   = CGEventCreateKeyboardEvent(source, kVK_Delete, false);
    CGEventSetIntegerValueField(down, kCGEventSourceUserData, VN_SYNTH_TAG);
    CGEventSetIntegerValueField(up, kCGEventSourceUserData, VN_SYNTH_TAG);
    CGEventPost(kCGAnnotatedSessionEventTap, down);
    CGEventPost(kCGAnnotatedSessionEventTap, up);
    CFRelease(down);
    CFRelease(up);
  }

  if (insert_len > 0) {
    CFStringRef str = CFStringCreateWithBytes(NULL, insert_text, insert_len,
                                              kCFStringEncodingUTF8, false);
    if (str) {
      CFIndex length = CFStringGetLength(str);
      UniChar chars[length];
      CFStringGetCharacters(str, CFRangeMake(0, length), chars);

      CGEventRef down = CGEventCreateKeyboardEvent(source, 0, true);
      CGEventRef up   = CGEventCreateKeyboardEvent(source, 0, false);
      CGEventKeyboardSetUnicodeString(down, length, chars);
      CGEventSetIntegerValueField(down, kCGEventSourceUserData, VN_SYNTH_TAG);
      CGEventSetIntegerValueField(up, kCGEventSourceUserData, VN_SYNTH_TAG);
      CGEventPost(kCGAnnotatedSessionEventTap, down);
      CGEventPost(kCGAnnotatedSessionEventTap, up);
      CFRelease(down);
      CFRelease(up);
      CFRelease(str);
    }
  }

  CFRelease(source);
}

// AX-independent VN delivery, matching how real Vietnamese IMEs (Unikey,
// OpenKey) work: no AX read/write at all, just keystrokes in, keystrokes
// out. Used both for apps svim's vim-mode ignores entirely (Flow A) and, by
// ax.c, as a fallback when AX simply can't see the focused element (e.g.
// Chrome's own web-content text areas without -DMANUAL_AX -- common enough,
// per the project's own README "Known Issues", that VN shouldn't just give
// up whenever vim-mode's AX detection does).
CGEventRef vn_synthetic_process(struct event_tap* event_tap, CGEventRef event) {
  // cursor_mode is irrelevant here: vn_input_route short-circuits on
  // front_app_ignored (always true on this call path) before ever looking
  // at it, so 0 is a safe placeholder value, not a guess.
  enum vn_flow flow = vn_input_route(&g_vn_input, event_tap->vn_ignored, true, 0);
  vn_debug_log("vn_synthetic_process: flow=%d vn_enabled=%d vn_ignored=%d",
              flow, g_vn_input.enabled, event_tap->vn_ignored);
  if (flow != VN_FLOW_SYNTHETIC) return event;

  UniCharCount count;
  UniChar character;
  CGEventKeyboardGetUnicodeString(event, 1, &count, &character);
  CGEventFlags flags = CGEventGetFlags(event);
  int64_t keycode = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);

  struct vn_engine_result result = (keycode == kVK_Delete)
    ? vn_engine_process_backspace()
    : vn_engine_process_key(character,
                             flags & kCGEventFlagMaskShift,
                             flags & kCGEventFlagMaskAlphaShift);

  vn_debug_log("vn_synthetic_process: char=0x%x keycode=%lld backspaces=%d insert_len=%d",
              character, keycode, result.backspace_count, result.insert_len);

  if (result.backspace_count == 0 && result.insert_len == 0) return event;

  vn_post_correction(result.backspace_count, result.insert_text, result.insert_len);
  return NULL;
}

static CGEventRef key_handler(CGEventTapProxy proxy, CGEventType type,
                              CGEventRef event, void* reference) {
  if (CGEventGetIntegerValueField(event, kCGEventSourceUserData) == VN_SYNTH_TAG) {
    return event;
  }

  switch (type) {
    case kCGEventTapDisabledByTimeout:
      printf("Timeout\n");
    case kCGEventTapDisabledByUserInput: {
      printf("restarting event-tap\n");
      CGEventTapEnable(((struct event_tap*) reference)->handle, true);
    } break;
    case kCGEventFlagsChanged: {
      CGEventFlags flags = CGEventGetFlags(event);
      vn_debug_log("flagsChanged: flags=0x%llx masked=0x%llx hotkey_mask=0x%llx",
                  (unsigned long long) flags,
                  (unsigned long long) (flags & VN_HOTKEY_RELEVANT_FLAGS),
                  (unsigned long long) g_vn_input.hotkey_mask);
      if (g_vn_input.hotkey_mask && (flags & VN_HOTKEY_RELEVANT_FLAGS) == g_vn_input.hotkey_mask) {
        vn_input_toggle(&g_vn_input);
        vn_debug_log("vn_input_toggle fired, enabled now=%d", g_vn_input.enabled);
      }
    } break;
    case kCGEventLeftMouseDown: {
      vn_engine_reset();
    } break;
    case kCGEventKeyDown: {
      struct event_tap* event_tap = (struct event_tap*) reference;
      if (event_tap->front_app_ignored) {
        if (g_ax.selected_element && g_ax.role) {
          ax_clear(&g_ax);
        }
        return vn_synthetic_process(event_tap, event);
      }

      return ax_process_event(&g_ax, event);
    } break;
  }
  return event;
}

bool event_tap_enabled(struct event_tap* event_tap) {
  bool result = (event_tap->handle && CGEventTapIsEnabled(event_tap->handle));
  return result;
}

void event_tap_load_blacklist(struct event_tap* event_tap) {
  event_tap->front_app_ignored = true;

  char* home = getenv("HOME");
  char buf[512];
  snprintf(buf, sizeof(buf), "%s/%s", home, ".config/svim/blacklist");

  struct string_list list = load_string_list(buf);
  event_tap->blacklist = list.items;
  event_tap->blacklist_count = list.count;
}

bool event_tap_begin(struct event_tap* event_tap) {
  event_tap_load_blacklist(event_tap);

  event_tap->mask = (1 << kCGEventKeyDown)
                  | (1 << kCGEventFlagsChanged)
                  | (1 << kCGEventLeftMouseDown);
  event_tap->handle = CGEventTapCreate(kCGAnnotatedSessionEventTap,
                                       kCGHeadInsertEventTap,
                                       kCGEventTapOptionDefault,
                                       event_tap->mask,
                                       &key_handler,
                                       event_tap);

  bool result = event_tap_enabled(event_tap);
  if (result) {
    event_tap->runloop_source = CFMachPortCreateRunLoopSource(
                                                           kCFAllocatorDefault,
                                                           event_tap->handle,
                                                           0);
    CFRunLoopAddSource(CFRunLoopGetMain(),
                       event_tap->runloop_source,
                       kCFRunLoopCommonModes);
  }

  return result;
}

void event_tap_end(struct event_tap* event_tap) {
  if (event_tap_enabled(event_tap)) {
    CGEventTapEnable(event_tap->handle, false);
    CFMachPortInvalidate(event_tap->handle);
    CFRunLoopRemoveSource(CFRunLoopGetMain(),
                          event_tap->runloop_source,
                          kCFRunLoopCommonModes);
    CFRelease(event_tap->runloop_source);
    CFRelease(event_tap->handle);
    event_tap->handle = NULL;

    for (int i = 0; i < event_tap->blacklist_count; i++)
      if (event_tap->blacklist[i]) free(event_tap->blacklist[i]);

    if (event_tap->blacklist) free(event_tap->blacklist);
  }
}
