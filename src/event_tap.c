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

// Posting to the frontmost app's pid directly (rather than broadcasting via
// kCGAnnotatedSessionEventTap) delivers straight into that process's own
// event queue alongside its real keystrokes, instead of re-entering the
// whole session-wide HID pipeline from scratch -- which is what let a real
// keystroke typed right after a correction win the race and land out of
// order (reproduced in Ghostty and Chrome alike, since both go through this
// same fallback). Falls back to the session-wide post if we don't have a
// pid yet (e.g. before the first app-switch notification arrives).
static void vn_post_event(pid_t target_pid, CGEventRef event) {
  if (target_pid > 0) CGEventPostToPid(target_pid, event);
  else CGEventPost(kCGAnnotatedSessionEventTap, event);
}

void vn_post_correction(struct vn_post_target target, int backspace_count, const unsigned char* insert_text, int insert_len) {
  vn_debug_log("vn_post_correction: pid=%d backspaces=%d insert_len=%d strategy=%d",
              target.pid, backspace_count, insert_len, target.strategy);

  CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
  bool select_strategy = target.strategy == VN_STRATEGY_SELECT;
  CGKeyCode backspace_key = select_strategy ? kVK_LeftArrow : kVK_Delete;

  for (int i = 0; i < backspace_count; i++) {
    CGEventRef down = CGEventCreateKeyboardEvent(source, backspace_key, true);
    CGEventRef up   = CGEventCreateKeyboardEvent(source, backspace_key, false);
    if (select_strategy) {
      // Select-then-replace instead of N discrete deletes: Shift+Left
      // extends a selection backward one character at a time; the Unicode
      // insert below then types over that selection, which every native
      // macOS text field treats as a single replace -- fewer discrete
      // events than N deletes + an insert, per Gõ Nhanh's approach for
      // apps that still drop characters at the default backspace strategy.
      CGEventSetFlags(down, kCGEventFlagMaskShift);
      CGEventSetFlags(up, kCGEventFlagMaskShift);
    }
    CGEventSetIntegerValueField(down, kCGEventSourceUserData, VN_SYNTH_TAG);
    CGEventSetIntegerValueField(up, kCGEventSourceUserData, VN_SYNTH_TAG);
    vn_post_event(target.pid, down);
    vn_post_event(target.pid, up);
    CFRelease(down);
    CFRelease(up);
    vn_debug_log("vn_post_correction: posted %s %d/%d",
                select_strategy ? "select" : "backspace", i + 1, backspace_count);
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
      vn_post_event(target.pid, down);
      vn_post_event(target.pid, up);
      CFRelease(down);
      CFRelease(up);
      CFRelease(str);
      vn_debug_log("vn_post_correction: posted insert unichar_len=%ld", (long) length);
    } else {
      // If this ever fires, the backspaces above already ran with nothing to
      // replace them -- a silent drop with a completely different cause
      // than event-ordering, so it needs to stand out from the timing noise.
      vn_debug_log("vn_post_correction: CFStringCreateWithBytes FAILED, insert silently dropped");
    }
  }

  // HACK: small safety margin on top of the pid-targeted delivery above --
  // still gives a slower app (Chrome re-rendering a web text area, a
  // terminal round-tripping through its PTY) a moment to actually apply the
  // correction before the next physical keystroke is dequeued from the tap.
  // Select-replace always skips this -- fewer discrete events already means
  // less to race against, and it's the escape hatch for apps where even a
  // tuned backspace delay isn't enough.
  if (!select_strategy) usleep(target.delay_us);

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

  // Pure cursor-navigation keys carry no text for vn_engine to compose (same
  // reasoning as ax.c's identical check) -- without this, the autorepeat
  // guard below treats a held arrow key as a held letter and drops every
  // repeat after the first, so holding Right/Down/etc. here moved the
  // cursor once and then stopped.
  if (keycode == kVK_LeftArrow || keycode == kVK_RightArrow
      || keycode == kVK_UpArrow || keycode == kVK_DownArrow
      || keycode == kVK_Home || keycode == kVK_End
      || keycode == kVK_PageUp || keycode == kVK_PageDown)
    return event;

  // OS-level key autorepeat firing while a letter is held (a slightly long
  // press, not a deliberate second/third tap) feeds vn_engine a keystroke it
  // can't tell apart from an intentional one -- and Telex treats a 3rd
  // consecutive same letter as "cancel the diacritic", so one stray repeat
  // silently corrupts the word being composed (e.g. "thấy" -> "thâấ").
  // Repeated Delete still needs to reach vn_engine_process_backspace to
  // keep its internal buffer in step with on-screen deletes from held
  // Backspace, so only letters are dropped here.
  bool is_repeat = CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat);
  if (is_repeat && keycode != kVK_Delete) return NULL;

  struct vn_engine_result result = (keycode == kVK_Delete)
    ? vn_engine_process_backspace()
    : vn_engine_process_key(character,
                             flags & kCGEventFlagMaskShift,
                             flags & kCGEventFlagMaskAlphaShift);

  vn_debug_log("vn_synthetic_process: char=0x%x keycode=%lld backspaces=%d insert_len=%d",
              character, keycode, result.backspace_count, result.insert_len);

  if (result.backspace_count == 0 && result.insert_len == 0) return event;

  struct vn_post_target target = {
    .pid = event_tap->front_pid,
    .delay_us = event_tap->delay_us,
    .strategy = event_tap->strategy
  };
  vn_post_correction(target, result.backspace_count, result.insert_text, result.insert_len);
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
      vn_debug_log("kCGEventLeftMouseDown: vn_engine_reset");
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
  // Safe defaults before workspace.m's startup resolution (or the first
  // real app switch) ever runs -- g_event_tap is zero-initialized, so
  // delay_us would otherwise be 0 (no safety margin) rather than the
  // spec'd 5ms default in that narrow window. strategy's zero value
  // already equals VN_STRATEGY_BACKSPACE; set explicitly for clarity.
  event_tap->delay_us = 5000;
  event_tap->strategy = VN_STRATEGY_BACKSPACE;

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
