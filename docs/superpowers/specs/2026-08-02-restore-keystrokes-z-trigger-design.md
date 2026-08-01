# Restore original keystrokes via z-trigger — Design

## Goal

Let a user who forgot to turn off Vietnamese input recover an
accidentally-transformed English word (e.g. typed "of", got "ò") back to
the literal characters they typed — by typing a trailing `z` right after
the word, instead of a dedicated hotkey combo. Wires up libunikey's
existing `UnikeyRestoreKeyStrokes()` API (confirmed working by live
empirical test in an earlier session — see
`project_restore_keystrokes_feature` memory).

## Supersedes

Replaces `docs/superpowers/specs/2026-07-28-restore-keystrokes-hotkey-design.md`
in full — that design (a dedicated `restore_hotkey=` modifier+key combo)
is not being built. This doc is the current, decided direction.

## Why `z`, and why it's safe without a dictionary

Vietnamese has no letter `z` in its alphabet, and confirmed by reading
libunikey's key tables (`libunikey/src/data.cpp`), `z`/`Z` map straight
through to `z`/`Z` in every input method (Telex/VNI/...) — it carries no
existing tone/diacritic meaning to collide with.

The trigger is self-checking, with no dictionary and no false-positive
risk: on an unmodified `z`/`Z` keypress, always attempt
`UnikeyRestoreKeyStrokes()` on the current word first.

- If it's a no-op (`backspaces == 0` — the word was never transformed),
  the word had nothing to restore, so `z` falls through and is typed
  literally. This is what makes "quiz", "jazz", "friz" type correctly
  unchanged: none of their prefixes ever trigger a Telex tone, so the
  restore attempt is always a no-op and `z` is inserted as normal.
- If it actually reverts something (`backspaces > 0`), the `z` keydown is
  consumed (never inserted) and the word reverts to its original
  keystrokes.

This is a stronger guarantee than the dictionary-based approach evaluated
and rejected in the earlier brainstorming session: it's driven by
libunikey's own ground truth (did this word actually change?) rather than
a heuristic guess about whether a string looks like English or Vietnamese.

## Non-goals

- Not automatic/heuristic detection on its own (no dictionary, no
  "auto-revert on space") — still manual-trigger, just triggered by a
  plain keystroke instead of a modifier chord.
- Not reusing/building on libunikey's vendored `wordtrie.h`/`.cpp` — dead
  code with no callers, no data, no C bridge; irrelevant here.
- Not changing `hotkey=`'s (VN on/off toggle) behavior, defaults, or
  detection path at all.
- Not enforcing the trigger key to literally always be `z` at the config
  level — reusing the existing single-key lookup (below) means any
  letter/digit in that table technically parses. Only `z` is documented
  and recommended: it's the only letter with the "never appears in real
  Vietnamese text" property that makes the no-op check collision-free.
  Any other letter (e.g. `x`, itself a Telex tone key) would misfire on
  real Vietnamese words that legitimately contain it and have already
  been transformed — the no-op check only protects words that were
  never transformed at all.

## Components

### `src/vn_input.h` / `src/vn_input.c` — new config key

`~/.config/univim/vn_config` gains `restore_trigger_key=` (e.g.
`restore_trigger_key=z`), a single token — no modifier syntax, parsed
with the *already-existing* `lookup_hotkey_key()` static helper in
`vn_input.c` (built for `hotkey=`/`restore_hotkey=`'s regular-key
component). No new parsing logic needed.

No default — absent from config = feature disabled (opt-in only), same
reasoning as the superseded design: no unrequested surprise behavior.

`struct vn_input` gains 2 fields:
```c
int64_t restore_trigger_keycode;
bool has_restore_trigger_keycode;  // false = feature disabled
```
(No mask field — unlike `hotkey_mask`/`restore_hotkey_mask`, this isn't a
modifier chord; the "no modifier held" requirement below is a fixed
guard, not something configured per line.)

Invalid single-token value (not in `lookup_hotkey_key`'s table) is a
config error, reported the same way other invalid `vn_config` values
already are.

### `src/vn_engine.c` — `vn_engine_restore_key_strokes()`

Unchanged from the superseded design — structurally identical to the
existing `vn_engine_process_backspace()`:
```c
struct vn_engine_result vn_engine_restore_key_strokes(void) {
  UnikeyRestoreKeyStrokes();

  int char_backspaces = bytes_to_char_count(UnikeyBackspaces);
  word_history_apply(UnikeyBackspaces, UnikeyBuf, UnikeyBufChars);

  struct vn_engine_result result = {
    .backspace_count = char_backspaces,
    .insert_text = UnikeyBuf,
    .insert_len = UnikeyBufChars
  };
  return result;
}
```
No extra `vn_engine_reset()` needed — `UnikeyRestoreKeyStrokes()` already
replays the restored keys through the engine's own append logic
internally, confirmed by the earlier empirical test.

### `src/event_tap.c` — Flow A detection

In `vn_synthetic_process`, this runs *after* the existing Command-combo
early return (`event_tap.c:163`, unchanged) — so by the time this check
runs, Command is already guaranteed absent. Only Control and Option need
excluding explicitly (Shift/CapsLock stay allowed, so `Z` also triggers):

```c
bool is_restore_candidate = g_vn_input.has_restore_trigger_keycode
  && !is_repeat
  && keycode == g_vn_input.restore_trigger_keycode
  && (flags & (kCGEventFlagMaskControl | kCGEventFlagMaskAlternate)) == 0;

struct vn_engine_result result;
if (is_restore_candidate) {
  result = vn_engine_restore_key_strokes();
  if (result.backspace_count == 0 && result.insert_len == 0) {
    // No-op: current word was never transformed, so this wasn't
    // actually a restore -- treat the keystroke as an ordinary letter.
    result = vn_engine_process_key(character, flags & kCGEventFlagMaskShift,
                                    flags & kCGEventFlagMaskAlphaShift);
  }
} else {
  result = (keycode == kVK_Delete)
    ? vn_engine_process_backspace()
    : vn_engine_process_key(character, flags & kCGEventFlagMaskShift,
                             flags & kCGEventFlagMaskAlphaShift);
}
```
Everything downstream (posting the correction, `return NULL`) is
unchanged.

Note: Cmd+`z` (macOS Undo) is already safe today — `event_tap.c:163`
resets the engine and returns before this code ever runs. Ctrl+`z` is
made safe by the explicit `kCGEventFlagMaskControl` exclusion above.

### `src/ax.c` — Flow B detection

Same pattern inside `ax_process_event`'s `VN_FLOW_VIM_BUFFER` branch,
using that file's existing `FLAG_SHIFT`/`FLAG_COMMAND` naming (Command is
already excluded there too, at `ax.c:276`). Reusing the exact existing
`buffer_input_string`/`vn_post_correction` delivery beneath it keeps Flow
B's internal vim buffer model in sync — a global, flow-unaware check was
considered and rejected for the same reason as the superseded design: it
would desync Flow B's buffer from the real on-screen text.

## Data flow

Config-load-time (parsing, same as every other `vn_config` key) plus one
new branch inside each flow's existing per-keystroke dispatch. A
candidate `z`/`Z` keystroke (no Ctrl/Cmd/Option held) always attempts
restore first; only a non-no-op result changes what gets typed. No new
interaction with `ax_get_cursor`/`ax_get_text`/mode-sync.

## Testing

1. **Unit tests** (`test/test_vn_engine.c`, extending the existing
   pattern — pure logic, no CGEvent/GUI dependency):
   - Type `"of"`, trigger restore → expect `"of"` (backspaces revert the
     transformed `"ò"`).
   - Type `"hello"` (never transformed), trigger restore → expect no-op
     (`backspace_count=0, insert_len=0`) — confirms the fallback-to-
     literal-key path is reachable.
   - Type `"thaays"` (real tone mark mid-word), trigger restore → expect
     `"thaays"`.
2. **Live verification** (needs real `CGEventTap` delivery):
   - Set `restore_trigger_key=z`, restart, type `"textz"` in a real app →
     confirm it reverts to `"text"` with no literal `z` left behind.
   - Type `"quiz"`, `"jazz"` → confirm they type out unchanged, letter by
     letter, including the `z`.
   - Press Cmd+Z and Ctrl+Z in a real text editor → confirm Undo still
     works normally (event untouched by this feature).
   - Confirm both Flow A (an ignored/non-vim app) and Flow B (a vim-mode
     app) independently.
   - Confirm holding `z` (autorepeat) doesn't misfire — already covered
     by the existing autorepeat guard ahead of this code.
