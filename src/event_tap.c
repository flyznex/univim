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

// CGEventTapPostEvent re-injects an event at the exact point in the stream
// where this tap intercepted the original keystroke -- unlike
// CGEventPostToPid/CGEventPost, which inject through a *separate* path with
// no ordering guarantee relative to a real keystroke typed right after a
// correction (that race was reproduced repeatedly under fast typing even
// with per-pid posting, per the investigation in the VN corruption project
// memory). Posting through the tap's own proxy guarantees this correction is
// ordered ahead of whatever the OS delivers next, matching Gõ Nhanh's
// `.syncProxy` strategy. Re-entrant synthetic events coming back through
// key_handler are already short-circuited by the VN_SYNTH_TAG check there.
static void vn_post_event(CGEventTapProxy proxy, CGEventRef event) {
  CGEventTapPostEvent(proxy, event);
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
    vn_post_event(target.proxy, down);
    vn_post_event(target.proxy, up);
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
      vn_post_event(target.proxy, down);
      vn_post_event(target.proxy, up);
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
  } else if (select_strategy && backspace_count > 0) {
    // Select strategy just highlighted backspace_count characters above,
    // expecting the insert below to type over them -- a plain backspace/
    // tone-undo correction (insert_len == 0, nothing to replace them with)
    // leaves that selection sitting there instead of actually removing
    // anything. One Delete removes a whole selection in one press
    // regardless of how many characters it spans.
    CGEventRef down = CGEventCreateKeyboardEvent(source, kVK_Delete, true);
    CGEventRef up   = CGEventCreateKeyboardEvent(source, kVK_Delete, false);
    CGEventSetIntegerValueField(down, kCGEventSourceUserData, VN_SYNTH_TAG);
    CGEventSetIntegerValueField(up, kCGEventSourceUserData, VN_SYNTH_TAG);
    vn_post_event(target.proxy, down);
    vn_post_event(target.proxy, up);
    CFRelease(down);
    CFRelease(up);
    vn_debug_log("vn_post_correction: select strategy with no replacement, deleted the selection");
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
// Switching tmux windows/panes happens entirely inside the terminal's PTY --
// invisible to both CGEventTap and NSWorkspace, since the terminal itself
// stays the frontmost app throughout. vn_engine has no signal to reset on,
// so a word left mid-composition in one tmux window can still be "continued"
// by the first keystroke typed in a completely different window after the
// switch: the resulting backspace count looks entirely plausible (1-2 chars,
// same shape as a real Telex tone correction) but lands in the new window,
// deleting into its prompt instead (reproduced consistently; not a race --
// raising vn_overrides' delay_ms had no effect). Switching windows always
// takes at least a couple hundred ms, while keystrokes within one real word
// never pause anywhere near that long, so treat a long idle gap as an
// implicit context change and reset before treating this keystroke as a
// continuation of whatever came before it.
#define VN_STALE_THRESHOLD_NS (2ULL * NSEC_PER_SEC)
static uint64_t last_activity_ns = 0;

CGEventRef vn_synthetic_process(struct event_tap* event_tap, CGEventTapProxy proxy, CGEventRef event) {
  uint64_t now_ns = CGEventGetTimestamp(event);
  if (last_activity_ns != 0 && now_ns > last_activity_ns
      && now_ns - last_activity_ns > VN_STALE_THRESHOLD_NS) {
    vn_debug_log("vn_synthetic_process: idle > 2s, resetting stale composition state");
    vn_engine_reset();
  }
  last_activity_ns = now_ns;

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

  // Command+key combos (Cmd+A, Cmd+V, Cmd+Delete, etc.) break word context --
  // reset the engine so the next word starts fresh. Without this, Cmd+A +
  // Delete leaves stale word_history and corrupts the first word typed after.
  if (flags & kCGEventFlagMaskCommand) {
    vn_engine_reset();
    return event;
  }

  // Pure cursor-navigation keys carry no text for vn_engine to compose (same
  // reasoning as ax.c's identical check) -- without this, the autorepeat
  // guard below treats a held arrow key as a held letter and drops every
  // repeat after the first, so holding Right/Down/etc. here moved the
  // cursor once and then stopped.
  //
  // vn_engine_reset() here too: word_history/libunikey's buffer otherwise
  // still hold the last-typed char as "pending" after the cursor moves away
  // from it, so a later key composes a tone mark meant for that abandoned
  // char and the correction lands at the *new* cursor position instead
  // (typing "a", pressing Left, then "s" produced "áa" instead of "sa").
  if (keycode == kVK_LeftArrow || keycode == kVK_RightArrow
      || keycode == kVK_UpArrow || keycode == kVK_DownArrow
      || keycode == kVK_Home || keycode == kVK_End
      || keycode == kVK_PageUp || keycode == kVK_PageDown) {
    vn_engine_reset();
    return event;
  }

  // Enter never legitimately continues a composition into whatever comes
  // next (a submitted terminal line, a cleared chat field) -- but
  // UnikeyFilter(CR) just reports "no correction" rather than clearing its
  // own internal buffer, so without an explicit reset here the *next*
  // keystroke on the fresh line can still be evaluated as a continuation of
  // the word typed before Enter. Confirmed empirically: the resulting
  // single, entirely plausible-looking backspace has nothing left of that
  // old word to remove on the new line, so it deletes into the freshly
  // drawn shell prompt instead (reproduced consistently, same prompt glyph
  // torn every time -- not a race, since raising the correction delay via
  // vn_overrides had no effect).
  if (keycode == kVK_Return) {
    vn_engine_reset();
    return event;
  }

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
    .proxy = proxy,
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
        return vn_synthetic_process(event_tap, proxy, event);
      }

      return ax_process_event(&g_ax, proxy, event);
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
  snprintf(buf, sizeof(buf), "%s/%s", home, ".config/univim/blacklist");

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
