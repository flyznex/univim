# Restore original keystrokes hotkey — Design

## Goal

Let a user who forgot to turn off Vietnamese input recover an
accidentally-transformed English word (e.g. typed "of", got "ò") back to
the literal characters they typed, via a dedicated, user-configured
hotkey — wiring up libunikey's existing `UnikeyRestoreKeyStrokes()` API,
confirmed working by live empirical test during this design.

## Non-goals

- Not automatic/heuristic detection (no dictionary, no "auto-revert on
  space") — a separate, much larger and riskier feature explored and
  explicitly deferred in this same brainstorming session (see
  `docs/superpowers/plans/2026-07-28-hotkey-regular-key.md`'s prior
  session and the `project_restore_keystrokes_feature` memory note for
  the full evaluation: no known algorithm solves this cleanly for short
  words, and a heuristic risks silently corrupting valid Vietnamese text
  with no user action to blame). This feature is manual-trigger only.
- Not reusing/building on libunikey's vendored `wordtrie.h`/`.cpp` — dead
  code with no callers, no data, and no C bridge; contributes nothing
  toward this manual-hotkey feature.
- Not changing `hotkey=`'s (VN on/off toggle) behavior, defaults, or
  detection path at all.

## Key empirical finding driving this design

Hand-tested `UnikeyRestoreKeyStrokes()` directly (small throwaway harness
against `lib/libunikey.a`, since deleted):
- `"of"` → `"ò"` → restore → `"of"` ✅
- `"as"` → `"á"` → restore → `"as"` ✅
- `"thays"` → `"tháy"` (has a real tone mark) → restore → `"thays"` ✅
  (reverts the *whole current word*, all-or-nothing — not selective
  character-by-character)
- `"hello"` (never transformed) → restore → no-op (`backspaces=0`) ✅

Output shape is identical to `UnikeyFilter`/`UnikeyBackspacePress`
(`UnikeyBackspaces`/`UnikeyBuf`/`UnikeyBufChars`), so it slots directly
into the existing `vn_engine_result` pattern with no new plumbing needed
at the correction-delivery layer.

## Components

### `src/vn_input.h` / `src/vn_input.c` — new config key

`~/.config/univim/vn_config` gains `restore_hotkey=`, parsed with the
*already-built* `parse_hotkey()` (from the modifier+regular-key hotkey
work earlier this session) — no changes needed to that function.

Unlike `hotkey=` (VN on/off toggle, allows modifier-only chords via
`kCGEventFlagsChanged`), `restore_hotkey=` **requires a regular key
component** — rejected as invalid config error otherwise. Reasoning: this
is an action, not a state toggle, and only `kCGEventKeyDown` (which
requires a concrete key) can trigger an action; a bare modifier chord has
no keydown to attach to.

No default combo — opt-in only. Unlike `hotkey=`'s `control+shift`
default, there's no obviously-universal choice here, and an unrequested
surprise global hotkey is worse than requiring one line of setup.

`struct vn_input` gains 3 fields, parallel to (not merged with) the
existing `hotkey_mask`/`hotkey_keycode`/`has_hotkey_keycode` trio — kept
separate deliberately, to avoid touching already-reviewed/tested code for
an unrelated feature:
```c
CGEventFlags restore_hotkey_mask;
int64_t restore_hotkey_keycode;
bool has_restore_hotkey_keycode;
```

### `src/vn_engine.c` — `vn_engine_restore_key_strokes()`

Structurally identical to the existing `vn_engine_process_backspace()`:
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
No extra `vn_engine_reset()` needed afterward — confirmed by the empirical
test that `UnikeyRestoreKeyStrokes()` already replays the restored keys
through the engine's own append logic internally (marking them
`converted=false`), so subsequent typing continues correctly without help.

### `src/event_tap.h` — shared macro

`VN_HOTKEY_RELEVANT_FLAGS` moves from a private `#define` in
`event_tap.c` to `event_tap.h`, unchanged in value/meaning — `ax.c` (Flow
B) needs the identical modifier-masking logic that `event_tap.c` (Flow A)
already uses, and duplicating the constant would risk the two flows
silently drifting apart.

### `src/event_tap.c` — Flow A detection

In `vn_synthetic_process`, immediately before the existing
`vn_engine_process_backspace()`/`vn_engine_process_key()` selection:
```c
bool is_restore_hotkey = g_vn_input.has_restore_hotkey_keycode
  && !is_repeat
  && keycode == g_vn_input.restore_hotkey_keycode
  && (flags & VN_HOTKEY_RELEVANT_FLAGS) == g_vn_input.restore_hotkey_mask;

struct vn_engine_result result = is_restore_hotkey
  ? vn_engine_restore_key_strokes()
  : (keycode == kVK_Delete)
    ? vn_engine_process_backspace()
    : vn_engine_process_key(character, flags & kCGEventFlagMaskShift,
                             flags & kCGEventFlagMaskAlphaShift);
```
Everything downstream (posting the correction, `return NULL`) is
unchanged — `vn_engine_restore_key_strokes()`'s result has the same shape
every other correction already has.

### `src/ax.c` — Flow B detection

Same pattern inside `ax_process_event`'s `VN_FLOW_VIM_BUFFER` branch,
using that file's existing `FLAG_SHIFT` naming. Reusing the exact
existing `buffer_input_string`/`vn_post_correction` delivery beneath it is
what makes this safe for Flow B specifically — a global, flow-unaware
check (checked once in `key_handler` like the VN-toggle hotkey) was
considered and rejected during brainstorming: it would bypass
`buffer_input_string`, desyncing Flow B's internal vim buffer model from
the real on-screen text, a real correctness bug (not just simpler code).

## Data flow

Config-load-time (parsing, same as every other `vn_config` key) plus one
new branch inside each flow's existing per-keystroke dispatch — no new
interaction with `ax_get_cursor`/`ax_get_text`/mode-sync. A matched combo
routes through `vn_engine_restore_key_strokes()` instead of the normal
key/backspace functions, then flows through the identical, already-proven
correction-delivery path either flow already has.

## Testing

1. **Unit tests** (`test/test_vn_engine.c`, extending the existing
   pattern — `vn_engine_restore_key_strokes()` is pure logic, no
   CGEvent/GUI dependency): type `"of"` → restore → expect `"of"`; type
   `"hello"` (never transformed) → restore → expect no-op
   (`backspace_count=0, insert_len=0`); type `"thaays"` (real tone mark)
   → restore → expect `"thaays"`.
2. **Live verification** (no automated coverage possible — needs real
   `CGEventTap` delivery, same as every event-tap-level change this
   session): build, set `restore_hotkey=` to some combo in `vn_config`,
   restart, type an English word that gets mangled, press the combo,
   confirm it reverts correctly in a real app; confirm holding the combo
   doesn't repeatedly fire (autorepeat guard); confirm both Flow A (an
   ignored/non-vim app) and Flow B (a vim-mode app) independently.
