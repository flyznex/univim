#include "ax.h"
#include "buffer.h"
#include "event_tap.h"
#include "vn_input.h"
#include <Carbon/Carbon.h> // kVK_Delete

void ax_begin(struct ax* ax) {
  buffer_begin(&ax->buffer);
  ax->system_element = NULL;
  ax->selected_element = NULL;
  ax->role = 0;

  const void *keys[] = { kAXTrustedCheckOptionPrompt };
  const void *values[] = { kCFBooleanTrue };

  CFDictionaryRef options;
  options = CFDictionaryCreate(kCFAllocatorDefault,
                               keys, values, sizeof(keys) / sizeof(*keys),
                               &kCFCopyStringDictionaryKeyCallBacks,
                               &kCFTypeDictionaryValueCallBacks           );

  ax->is_privileged = AXIsProcessTrustedWithOptions(options);
  CFRelease(options);

  if (ax->is_privileged) ax->system_element = AXUIElementCreateSystemWide();
  else {
    printf("Accessibility not granted. Exit.");
    exit(1);
  }

  assert(ax->system_element != NULL);
}

static inline bool ax_get_text(struct ax* ax) {
  CFTypeRef text_ref = NULL;
  AXError error = AXUIElementCopyAttributeValue(ax->selected_element,
                                                kAXValueAttribute,
                                                &text_ref            );
  if(error == kAXErrorSuccess) {
    char* raw = cfstring_get_cstring(text_ref);
    if (!raw) {
      CFRelease(text_ref);
      return false;
    } 

    if (!ax->buffer.raw || !(strcmp(ax->buffer.raw, raw) == 0)) {
      if (ax->buffer.raw) free(ax->buffer.raw);
      ax->buffer.raw = raw;
      buffer_revsync_text(&ax->buffer);
    }
    else free(raw);
  }

  if (text_ref) CFRelease(text_ref);
  return error == kAXErrorSuccess;
}

static inline bool ax_get_cursor(struct ax* ax) {
  CFTypeRef text_range_ref = NULL;
  CFRange text_range = CFRangeMake(0, 0);
  AXError error = AXUIElementCopyAttributeValue(ax->selected_element,
                                                kAXSelectedTextRangeAttribute,
                                                &text_range_ref              );

  if (error == kAXErrorSuccess) {
    AXValueGetValue(text_range_ref, kAXValueCFRangeType, &text_range);

    if (ax->buffer.cursor.position  != text_range.location || 
        ax->buffer.cursor.selection != text_range.length     ) {
      ax->buffer.cursor.position = text_range.location;
      ax->buffer.cursor.selection = text_range.length;
      buffer_revsync_cursor(&ax->buffer);
    }
  }

  if (text_range_ref) CFRelease(text_range_ref);
  return error == kAXErrorSuccess;
}

static inline bool ax_set_text(struct ax* ax) {
  if (!ax->is_supported || !ax->buffer.raw) return false;
  if (!ax->buffer.did_change) return true;

  CFStringRef text_ref = CFStringCreateWithCString(NULL,
                                                   ax->buffer.raw,
                                                   kCFStringEncodingUTF8);

  AXError error = AXUIElementSetAttributeValue(ax->selected_element,
                                               kAXValueAttribute,
                                               text_ref             );

  CFRelease(text_ref);
  return error == kAXErrorSuccess;
}

static inline bool ax_set_cursor(struct ax* ax) {
  if (!ax->is_supported) return false;

  CFRange text_range = CFRangeMake(ax->buffer.cursor.position,
                                   ax->buffer.cursor.selection);
  AXValueRef value = AXValueCreate(kAXValueCFRangeType, &text_range);
  // HACK: This is needed when the text has been set to give the
  // HACK: AX API some time to breathe...
  if (ax->buffer.did_change) usleep(15000);

  AXError error = AXUIElementSetAttributeValue(ax->selected_element,
                                               kAXSelectedTextRangeAttribute,
                                               value                         );

  CFRelease(value);
  return error == kAXErrorSuccess;
}

static inline bool ax_set_buffer(struct ax* ax) {
  return ax_set_text(ax)
      && ax_set_cursor(ax);
}

static inline bool ax_get_selected_element(struct ax* ax) {
  CFTypeRef selected_element = NULL;
  AXError error = AXUIElementCopyAttributeValue(ax->system_element,
                                                kAXFocusedUIElementAttribute,
                                                &selected_element            );

  // Compared against last_focused_element (tracks the raw focus target
  // regardless of role). For some apps (e.g. Chrome's own web-content areas
  // without -DMANUAL_AX), AXUIElementCopyAttributeValue doesn't just return
  // an element with an unrecognized role -- it returns NULL outright, every
  // single call, with no identity to compare at all. Treating "NULL both
  // times" as a change (the naive "both non-NULL and CFEqual" check below
  // would) means every keystroke looks like a brand new focus, triggering
  // ax_clear()/vn_engine_reset() constantly and wiping Telex/VNI
  // composition state before it can ever combine two keystrokes. Only a
  // transition between "AX gave us something" and "AX gave us nothing" (or
  // two different non-NULL elements) counts as a real change.
  bool same_element;
  if (!ax->last_focused_element && !selected_element) {
    same_element = true;
  } else if (ax->last_focused_element && selected_element) {
    same_element = CFEqual(ax->last_focused_element, selected_element);
  } else {
    same_element = false;
  }
  vn_debug_log("ax_get_selected_element: had_last=%d had_new=%d same=%d",
              ax->last_focused_element != NULL, selected_element != NULL, same_element);
  if (same_element) {
    if (selected_element) CFRelease(selected_element);
    return (error == kAXErrorSuccess) && ax->role;
  }

  ax_clear(ax);
  vn_engine_reset();

  if (ax->last_focused_element) CFRelease(ax->last_focused_element);
  ax->last_focused_element = selected_element;

  uint32_t role = 0;
  CFTypeRef role_ref = NULL;
  CFTypeRef text_element = NULL;

  if (selected_element) {
    AXUIElementCopyAttributeValue(selected_element,
                                  kAXRoleAttribute,
                                  &role_ref        );

    if (role_ref && (CFEqual(role_ref, kAXTextFieldRole) ||
                     CFEqual(role_ref,  kAXTextAreaRole) ||
                     CFEqual(role_ref,  kAXComboBoxRole)   )) {
      role = ROLE_TEXT;
    }
    else if (role_ref && (CFEqual(role_ref,   kAXTableRole) ||
                          CFEqual(role_ref,  kAXButtonRole) ||
                          CFEqual(role_ref, kAXOutlineRole)   )) {
      role = ROLE_TABLE;
    }
    else if (role_ref && CFEqual(role_ref, kAXGroupRole)) {
      role = ROLE_SCROLL;
    }
    else if (role_ref) {
      // char* role = cfstring_get_cstring(role_ref);
      // printf("Role: %s\n", role);
      // free(role);
    }

    if (role_ref) CFRelease(role_ref);

    if (role) {
      text_element = selected_element;
      CFRetain(text_element); // separate reference from last_focused_element
    }
  }

  ax->role = role;
  ax->selected_element = text_element;

  return (error == kAXErrorSuccess) && role;
}

bool ax_process_selected_element(struct ax* ax) {
  ax->is_supported = ax_get_selected_element(ax); 

  bool success = true;
  if (ax->role == ROLE_TEXT && ax->buffer.cursor.mode != INSERT) {
    success = ax_get_text(ax) && ax_get_cursor(ax);
  }

  return ax->is_supported && success;
}

void ax_front_app_changed(struct ax* ax, pid_t pid) {
#ifdef MANUAL_AX
  AXUIElementRef app = AXUIElementCreateApplication(pid);
  if (app) {
    AXUIElementSetAttributeValue(app,
                                 CFSTR("AXManualAccessibility"),
                                 kCFBooleanTrue                 );
    CFRelease(app);
  }
#endif
}

CGEventRef ax_process_event(struct ax* ax, CGEventRef event) {
  if (!ax_process_selected_element(ax)) {
    vn_debug_log("ax_process_selected_element failed: role=%u is_supported=%d",
                ax->role, ax->is_supported);
    // AX can't see this element at all (e.g. Chrome's own web-content text
    // areas without -DMANUAL_AX) -- vim-mode has nothing to work with here,
    // but VN doesn't need AX in the first place, so fall back to the same
    // AX-independent synthetic-keystroke delivery Flow A already uses.
    return vn_synthetic_process(&g_event_tap, event);
  }

  // A previous Enter was passed through while staying in INSERT mode (see
  // below) -- many apps (chat inputs especially) react to Enter by
  // submitting and clearing the field themselves, entirely outside our
  // control. INSERT mode normally skips re-reading the real field (see the
  // "Insert mode is passed and only synced later" comment further down) for
  // performance, so without this we'd keep editing a stale internal copy
  // and overwrite the app's now-empty field with old text on the next sync.
  if (ax->resync_pending) {
    ax->resync_pending = false;
    if (ax->role == ROLE_TEXT) {
      ax_get_text(ax);
      ax_get_cursor(ax);
    }
  }

  UniCharCount count;
  UniChar character;
  CGEventKeyboardGetUnicodeString(event, 1, &count, &character);
  CGEventFlags flags = CGEventGetFlags(event);
  int64_t keycode = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);

  // Pure cursor-navigation keys carry no text to insert -- CGEventKeyboardGetUnicodeString
  // maps them to legacy control-range codepoints (e.g. Right Arrow -> 0x1D),
  // which would otherwise be fed straight into vim's buffer/the VN engine as
  // if the user had typed a literal character, corrupting the buffer every
  // time the cursor is repositioned mid-word (very common while placing a
  // Vietnamese tone mark).
  if (keycode == kVK_LeftArrow || keycode == kVK_RightArrow
      || keycode == kVK_UpArrow || keycode == kVK_DownArrow
      || keycode == kVK_Home || keycode == kVK_End
      || keycode == kVK_PageUp || keycode == kVK_PageDown)
    return event;

  // Command+key combos break word context -- reset the VN engine so the next
  // word starts fresh (e.g. Cmd+A + Delete would otherwise leave stale state).
  if (flags & FLAG_COMMAND) {
    vn_engine_reset();
    return event;
  }
  
  // Shift Enter
  if (character == ENTER && (flags & FLAG_SHIFT))
    return event;

  // Shift Escape
  if (character == ESCAPE && (flags & FLAG_SHIFT))
    return event;

  if (ax->role == ROLE_TEXT) {
    // Escape in normal mode
    if (character == ESCAPE && ax->buffer.cursor.mode & NORMAL)
      return event;

    // Enter in normal mode
    if (character == ENTER && ax->buffer.cursor.mode & NORMAL)
      return event;
    
    bool was_insert = ax->buffer.cursor.mode & INSERT
                      || !ax->buffer.cursor.mode;

    enum vn_flow flow = vn_input_route(&g_vn_input, g_event_tap.vn_ignored,
                                       g_event_tap.front_app_ignored,
                                       ax->buffer.cursor.mode           );
    vn_debug_log("role=%u mode=%u flow=%d vn_enabled=%d vn_ignored=%d front_ignored=%d char=0x%x",
                 ax->role, ax->buffer.cursor.mode, flow, g_vn_input.enabled,
                 g_event_tap.vn_ignored, g_event_tap.front_app_ignored, character);

    if (flow == VN_FLOW_VIM_BUFFER && keycode == kVK_Delete) {
      // Deleting an active native text selection (e.g. select-all then
      // Backspace) is not a Telex/VNI tone-undo -- refresh the real
      // selection state first so a selected block gets deleted whole
      // instead of the engine's single-character tone-undo count silently
      // truncating it to one character.
      ax_get_cursor(ax);
      if (ax->buffer.cursor.selection > 0) flow = VN_FLOW_NONE;
    }

    // Same reasoning as event_tap.c's vn_synthetic_process: a stray OS
    // autorepeat on a held letter looks identical to a deliberate extra tap
    // to vn_engine, and Telex reads a 3rd consecutive same letter as
    // "cancel the diacritic" -- so it corrupts the word being composed.
    // Repeated Delete is left alone; buffer_input_string still needs every
    // one to keep vim's model in sync with held-Backspace deletes.
    if (flow == VN_FLOW_VIM_BUFFER && keycode != kVK_Delete
        && CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat))
      return NULL;

    if (flow == VN_FLOW_VIM_BUFFER) {
      struct vn_engine_result result = (keycode == kVK_Delete)
        ? vn_engine_process_backspace()
        : vn_engine_process_key(character, flags & FLAG_SHIFT,
                                flags & kCGEventFlagMaskAlphaShift);

      if (result.backspace_count > 0 || result.insert_len > 0) {
        char* text = NULL;
        if (result.insert_len > 0) {
          CFStringRef str = CFStringCreateWithBytes(NULL, result.insert_text,
                                                    result.insert_len,
                                                    kCFStringEncodingUTF8,
                                                    false                );
          if (str) {
            text = cfstring_get_cstring(str);
            CFRelease(str);
          }
        }
        buffer_input_string(&ax->buffer, result.backspace_count, text);
        if (text) free(text);
        // Deliver via synthetic keystrokes (same mechanism as Flow A)
        // instead of AXUIElementSetAttributeValue: web content (e.g. a
        // Chrome page's own text areas, as opposed to Chrome's native
        // address bar) is often JS-controlled and silently ignores or
        // reverts a direct AX value write, while synthetic keystrokes go
        // through the app's normal input pipeline and work reliably
        // regardless of how good the app's AX support is. buffer_input_string
        // above still keeps vim's own internal model in sync for
        // NORMAL-mode motions/visual mode.
        vn_debug_log("flow_b correction: backspaces=%d insert_len=%d",
                    result.backspace_count, result.insert_len);
        struct vn_post_target target = {
          .pid = g_event_tap.front_pid,
          .delay_us = g_event_tap.delay_us,
          .strategy = g_event_tap.strategy
        };
        vn_post_correction(target, result.backspace_count, result.insert_text, result.insert_len);
        return NULL;
      }
      // ponytail: vn_engine returns an empty result (no backspaces, nothing
      // to insert) for most keys -- e.g. libunikey never reports output for
      // the first consonant of a new word, or for control keys like escape
      // -- since it mirrors Flow A's "OS delivers raw, engine corrects
      // asynchronously" model. Flow B has no such OS-delivers-raw fallback:
      // the vim buffer is the only source of truth, so an untouched key
      // still has to go through buffer_input or it silently falls out of
      // sync with the real app text.
      buffer_input(&ax->buffer, character, count);
    } else {
      buffer_input(&ax->buffer, character, count);
    }

    // Insert mode is passed and only synced later
    if (was_insert && ax->buffer.cursor.mode & INSERT) {
      // Enter is the common "submit" key (chat/forms) -- flag a resync for
      // the next event in case the app cleared the field in response.
      if (character == ENTER) ax->resync_pending = true;
      return event;
    }
    else if (was_insert) {
      if (!ax_get_text(ax) || !ax_get_cursor(ax)) return event;
    }

    ax_set_buffer(ax);

    return NULL;
  }

#ifdef GUI_MOVES
  // NOTE: Gui movement is currently hardcoded for my movement keys jklö and
  // NOTE: only available when compiling with the -DGUI_MOVES flag
  if (ax->role == ROLE_TABLE || ax->role == ROLE_SCROLL) {
    switch (character) {
      case K: {
        CGEventSetIntegerValueField(event, kCGKeyboardEventAutorepeat, false);
        CGEventSetIntegerValueField(event, kCGKeyboardEventKeycode, 125);
      } break;
      case L: {
        CGEventSetIntegerValueField(event, kCGKeyboardEventAutorepeat, false);
        CGEventSetIntegerValueField(event, kCGKeyboardEventKeycode, 126);
      } break;
      case J: {
        CGEventSetIntegerValueField(event, kCGKeyboardEventAutorepeat, false);
        CGEventSetIntegerValueField(event, kCGKeyboardEventKeycode, 123);
      } break;
      case OE: {
        CGEventSetIntegerValueField(event, kCGKeyboardEventAutorepeat, false);
        CGEventSetIntegerValueField(event, kCGKeyboardEventKeycode, 124);
      } break;
    }
  }
#endif //GUI_MOVES

  return event;
}

void ax_clear(struct ax* ax) {
  buffer_clear(&ax->buffer);
  if (ax->selected_element && ax->role == ROLE_TEXT) {
    buffer_call_script(&ax->buffer, false);
  }

  if (ax->selected_element) CFRelease(ax->selected_element);
  ax->role = 0;
  ax->selected_element = NULL;
  ax->is_supported = false;
  ax->resync_pending = false;
}
