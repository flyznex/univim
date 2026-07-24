#include "event_tap.h"
#include "helpers.h"
#include "vn_input.h"
#include <Carbon/Carbon.h> // kVK_Delete

#define VN_SYNTH_TAG 0x564E5359 // 'VNSY'

bool event_tap_check_blacklist(struct event_tap* event_tap,
                               char* app, char* bundle_id  ) {
  return blacklist_contains(event_tap->blacklist, event_tap->blacklist_count, app, bundle_id);
}

static void vn_post_correction(int backspace_count, const unsigned char* insert_text, int insert_len) {
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

  CFRelease(source);
}

static CGEventRef vn_flow_a_process(struct event_tap* event_tap, CGEventRef event) {
  // cursor_mode is irrelevant here: vn_input_route short-circuits on
  // front_app_ignored (always true on this call path) before ever looking
  // at it, so 0 is a safe placeholder value, not a guess.
  enum vn_flow flow = vn_input_route(&g_vn_input, event_tap->vn_ignored, true, 0);
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
      if ((flags & g_vn_input.hotkey_mask) == g_vn_input.hotkey_mask) {
        vn_input_toggle(&g_vn_input);
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
        return vn_flow_a_process(event_tap, event);
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
