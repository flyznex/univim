# Modifier+regular-key VN toggle hotkeys — Design

## Goal

Let `~/.config/univim/vn_config`'s `hotkey=` value include one regular key
(`space`, or a single letter `a`-`z`, or a single digit `0`-`9`) alongside
modifiers, e.g. `hotkey=control+space`, so the VN toggle isn't limited to
pure modifier chords (`control+shift` today).

## Non-goals

- Not adding function keys, arrows, Tab/Escape/Enter/Delete, or any other
  named key beyond space/a-z/0-9 — explicit scope decision.
- Not allowing a bare regular key with no modifier (`hotkey=space`) — would
  swallow every normal keystroke of that key app-wide. Requires ≥1
  modifier, same spirit as every real OS-level global hotkey.
- Not changing modifier-only hotkeys (`control+shift`, etc.) at all — that
  path (`kCGEventFlagsChanged`) is untouched; this is a purely additive
  second path for combos that include a regular key.
- Not building a second, independent hotkey-registration mechanism (e.g.
  Carbon's `RegisterEventHotKey`) — the app already intercepts every
  keystroke via its own `CGEventTap` for vim-mode/VN; one more check in
  that same tap is simpler and more consistent than a parallel system.

## Components

### `src/vn_input.h` / `src/vn_input.c` — config parsing

`struct vn_input` gains two fields next to the existing `hotkey_mask`:

```c
CGKeyCode hotkey_keycode;   // the combo's regular key, only meaningful when...
bool has_hotkey_keycode;    // ...this is true. false = modifier-only (today's behavior, unchanged)
```

A bare `CGKeyCode` sentinel (e.g. `0` for "unset") doesn't work here:
`kVK_ANSI_A` (the letter `a`) is itself `0`, so a separate bool is required
to distinguish "no regular key configured" from "configured key is `a`".

A static 37-entry lookup table (`"space"` → `kVK_Space`, `"a"` → `kVK_ANSI_A`,
..., `"z"` → `kVK_ANSI_Z`, `"0"` → `kVK_ANSI_0`, ..., `"9"` → `kVK_ANSI_9`)
maps token strings to `CGKeyCode`s, using the `kVK_ANSI_*` constants already
available via the existing `<Carbon/Carbon.h>` include.

`parse_hotkey` (currently `static`, only reachable from within
`vn_input.c`) is extended and exposed via `vn_input.h` so it's directly
unit-testable:

- Each `+`-separated token is checked against the modifier names first
  (`control`/`shift`/`command`/`option`, existing behavior), then against
  the new lookup table.
- At most one token may resolve to a regular key — a second one is an
  error.
- If a regular key token is present, at least one modifier token must also
  be present — otherwise it's an error (a bare `space`/`j`/etc. would
  intercept ordinary typing app-wide).
- Any token matching neither category is an error (existing behavior,
  unchanged — this is what already made `control+space` report an error
  after the earlier fix in this session).

`vn_config_load`'s existing "hotkey" key-handling calls the extended
`parse_hotkey` and applies `new_hotkey_keycode`/`new_has_hotkey_keycode`
the same way it already applies `new_hotkey` — same defaults-for-fresh-load
/ preserved-for-reload pattern already in place for every other field.

### `src/event_tap.c` — detection

One new check at the top of `key_handler`'s `case kCGEventKeyDown:`, before
the existing `front_app_ignored` branching, so it applies globally exactly
like the modifier-only hotkey does today:

```c
case kCGEventKeyDown: {
  if (g_vn_input.has_hotkey_keycode) {
    int64_t keycode = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
    bool is_repeat = CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat);
    CGEventFlags flags = CGEventGetFlags(event);
    if (!is_repeat && keycode == g_vn_input.hotkey_keycode
        && (flags & VN_HOTKEY_RELEVANT_FLAGS) == g_vn_input.hotkey_mask) {
      vn_input_toggle(&g_vn_input);
      return NULL; // consume -- never reaches vim-mode/VN engine/the app
    }
  }
  ... existing front_app_ignored branching, unchanged ...
```

Autorepeat is explicitly excluded: holding the combo down must toggle once,
not repeatedly flip on/off for as long as the key is held (same reasoning
as the existing autorepeat guards elsewhere in this file).

`vn_input_toggle` itself (`src/vn_input.c:344`) is unchanged — it already
takes no event-specific parameters, so it's reusable as-is from this new
call site.

## Data flow

Entirely config-load-time (parsing) plus one new branch in the existing
key-event dispatch — no interaction with `vn_engine`, `ax.c`, or
`vn_post_correction`. A matched combo returns `NULL` before any of that
code ever sees the event, identical in effect to how the modifier-only
hotkey path already short-circuits everything else.

## Testing

1. **Unit tests** (`test/test_vn_input.c`, extending the existing pattern):
   `control+space` → mask=Control, keycode=`kVK_Space`,
   has_keycode=true; `command+option+j` → mask=Command|Option,
   keycode=`kVK_ANSI_J`, has_keycode=true; `control+shift` (today's format)
   → has_keycode=false, behavior-preserving; `space` alone → invalid;
   `control+space+j` (two regular keys) → invalid; `control+xyz` (garbage
   token) → invalid.
2. **Live verification** (no automated coverage possible — needs real
   `CGEventTap` delivery): build, set `hotkey=control+space` in
   `vn_config`, restart, confirm the combo actually toggles VN in a real
   app and that holding it down doesn't rapid-fire toggle. Same manual
   process already used throughout this session for every event-tap-level
   change.
