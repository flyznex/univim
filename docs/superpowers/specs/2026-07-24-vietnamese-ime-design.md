# Vietnamese IME add-on for svim — Design

## Goal

Add a system-wide Vietnamese input method (Telex and VNI) to svim, independent
of the existing vim-mode feature, with its own per-app toggle list, so that
Vietnamese typing works in every app (including apps where vim-mode is
disabled) while vim-mode's existing per-app blacklist behavior is unchanged.

## Non-goals

- No native UI (no menu bar icon/status item). State changes are broadcast
  through the existing hook-script mechanism (`svim.sh` + env vars) instead.
- No cross-platform support (macOS only, matching the rest of svim).
- Not reimplementing the Telex/VNI rule engine — vendoring
  [libunikey](https://github.com/inteplus/libunikey) (GPL-3.0, matches
  svim's own license) instead of writing a new one.
- No persistence of VN/EN state across restarts — always starts in EN.

## Components

- `vendor/libunikey/` — git submodule, same vendoring pattern as `libvim`.
- `src/vn_engine.cpp` / `src/vn_engine.h` — thin `extern "C"` wrapper around
  libunikey. Pure transform logic, no I/O:
  - `vn_engine_process_key(UniChar key) -> { int backspace_count; const char* insert_text; }`
    `insert_text` points into a static internal buffer, valid until the next
    call — no per-keystroke allocation (perf priority).
  - `vn_engine_reset()` — clears in-progress word state.
  - `vn_engine_set_method(VN_TELEX | VN_VNI)`.
- `src/vn_input.c` / `src/vn_input.h` — global state and config:
  - `vn_enabled` (bool, toggled by hotkey, starts `false`).
  - VN-specific blacklist, loaded via a new shared helper (see below).
  - `vn_config` loader (method + hotkey combo).
- `helpers.c`: generalize `event_tap_load_blacklist`'s file-reading logic
  into a shared `load_string_list(path) -> {list, count}` helper, reused by
  both the existing vim blacklist and the new VN blacklist, instead of
  duplicating the fopen/fgets/realloc loop.

## Integration points (existing files)

- `event_tap.c` `key_handler`:
  - First: if the incoming event is tagged as self-synthesized (see
    Recursion guard below), pass it through unmodified immediately.
  - If `front_app_ignored == true` (app blacklisted for vim-mode) and the app
    is not VN-blacklisted and `vn_enabled`: call `vn_engine_process_key`,
    apply the result via synthetic `CGEventPost` (backspace x N + new chars).
    This path never touches `ax.c` or the vim buffer.
  - Also taps `kCGEventLeftMouseDown` and toggles hotkey combo
    (`kCGEventFlagsChanged`) — see Data flow.
- `ax.c` `ax_process_event`: right before the existing
  `buffer_input(&ax->buffer, character, count)` call, when
  `cursor.mode & INSERT`, app not VN-blacklisted, and `vn_enabled`: call
  `vn_engine_process_key`, and if it requests a change, apply it as
  `vimKey(BACKSPACE) x N` + `vimInput(insert_text)` instead of the plain
  character insert — writeback still goes through the existing
  `buffer_sync()` / `ax_set_buffer()` path unchanged.
- `ax.c` `ax_get_selected_element`: on focus-element change (existing
  `ax_clear()` call site), also call `vn_engine_reset()`.
- `workspace.m` `appSwitched:`: alongside the existing vim blacklist check,
  check the VN blacklist and call `vn_engine_reset()` on every app switch.
- `main.m`: extend the existing `CGEventTapCreate` mask to include
  `kCGEventFlagsChanged` and `kCGEventLeftMouseDown` for the hotkey and
  reset triggers.

## Data flow

**Flow A — vim-mode-blacklisted app, VN enabled, app not VN-blacklisted:**
1. `key_handler` receives `kCGEventKeyDown`; `front_app_ignored == true`.
2. VN-blacklist check + `vn_enabled` both pass.
3. `vn_engine_process_key(key)` → e.g. `{backspace_count: 1, insert_text: "ệ"}`.
4. If no change requested (`backspace_count == 0 && insert_text == NULL`):
   return the original event unmodified.
5. Otherwise: swallow the original event (return `NULL`), then
   `CGEventPost` a synthetic backspace x `backspace_count`, followed by the
   characters in `insert_text`. Synthetic events are tagged (via
   `CGEventSetIntegerValueField` with a dedicated field, e.g.
   `kCGEventSourceUserData`) so `key_handler` recognizes and ignores them
   when they re-enter the tap, preventing infinite recursion.

**Flow B — vim-mode-active app, cursor in INSERT mode, VN enabled, not VN-blacklisted:**
1. `ax_process_event` reaches the point where it would call
   `buffer_input(&ax->buffer, character, count)`.
2. VN-blacklist check + `vn_enabled` + `cursor.mode & INSERT` all pass.
3. `vn_engine_process_key(character)` → `{backspace_count, insert_text}`.
4. If no change requested: call `buffer_input` exactly as today (unchanged
   vim behavior).
5. Otherwise: call `vimKey(BACKSPACE)` `backspace_count` times, then
   `vimInput(insert_text)`, instead of the plain character insert. The rest
   of `buffer_input`'s existing `buffer_sync()` call (and therefore
   `ax_set_buffer()` writeback) is untouched.

**Flow C — hotkey toggle:**
1. The configured modifier combo (default `control+shift`) is
   detected via `kCGEventFlagsChanged` in `key_handler`, *before* the
   front_app_ignored/ax branches — swallowed so no app ever sees it.
2. `vn_enabled = !vn_enabled`.
3. `vn_engine_reset()`.
4. Fire the existing hook-script mechanism (`vfork_exec` + `env_vars`,
   reusing the same plumbing `buffer_call_script` already uses) with a new
   env var `VNMODE=on|off`. Display, if any, is entirely up to the user's
   own `svim.sh` (e.g. piping into SketchyBar or JankyBorders) — svim itself
   adds no UI.

**Flow D — no-op (existing behavior unchanged):** VN-blacklisted app, or
`vn_enabled == false`, or (vim-mode-active app) cursor not in INSERT mode —
none of the new code paths are touched.

## Config format

**`~/.config/svim/vn_blacklist`** — same one-app-or-bundle-id-per-line format
as the existing `~/.config/svim/blacklist`, loaded through the new shared
`load_string_list` helper (see Components).

**`~/.config/svim/vn_config`** — plain `key=value` lines, no parser beyond a
split on `=`:
```
method=telex
hotkey=control+shift
```
- `method`: `telex` (default) or `vni`.
- `hotkey`: modifier combo toggling VN/EN, matched via exact `CGEventFlags`
  mask on `kCGEventFlagsChanged`. Default avoids collision with macOS's
  built-in input-source-switch shortcuts (`Cmd+Space` / `Ctrl+Space`).
- Missing file or missing individual keys fall back to defaults — no error.
- Like the existing blacklist file, config changes require a service
  restart to take effect (no hot-reload) — consistent with svim's current
  behavior, not a new limitation.

## Error handling / edge cases

1. **Synthetic-event recursion (Flow A):** self-synthesized events are
   tagged and short-circuited at the very top of `key_handler`.
2. **Mid-word context switches:** `vn_engine_reset()` is called on app
   switch (`workspace.m`), on AX focus-element change (`ax.c`), and on
   `kCGEventLeftMouseDown` (new tap mask bit) — covers app-switch, in-app
   focus change, and same-app mouse clicks to a different field.
3. **Accepted limitation (not fixed):** Flow A has no visibility into actual
   field content (pure keystroke-stream based, by design, for
   performance/compatibility). If something outside svim's observation
   changes the field's content mid-word, the synthetic backspace count can
   desync and delete the wrong characters. This is an inherent limitation
   shared by Unikey/OpenKey themselves, not a bug to chase; documented here
   so it isn't rediscovered as a surprise later.
4. **Engine output ownership:** `insert_text` points into a static buffer
   internal to `vn_engine.cpp`, valid only until the next
   `vn_engine_process_key` call — zero heap allocation on the hot path.
5. **Engine failure:** any unexpected/invalid engine result is treated as
   "no change requested" (fail-safe passthrough) — never blocks a keystroke
   or crashes the daemon.

## Testing

1. **`test_vn_engine.c`** (standalone, assert-based, no framework — same
   style as the `buffer_update_raw_text` regression test written earlier
   this session): feed known Telex/VNI keystroke sequences, assert the
   final composed string. Required cases: compound vowels (`vieejt` →
   `việt`), `d d` → `đ`, self-correction mid-word, word-boundary reset
   (typing two words in a row doesn't leak state), and VNI parity for the
   same word set. Verifies `vn_engine.cpp`'s wrapping, not libunikey's
   internals (trust upstream's own tests for the engine core).
2. **`test_vn_enable_matrix.c`**: table-driven test over all combinations of
   `(vn_enabled, is_vn_blacklisted, front_app_ignored, cursor_mode)`,
   asserting the Flow A/B/D routing decision from the Data flow section.
   Cheap, but getting this wrong misroutes the entire feature.
3. **Not unit-tested:** actual `CGEventPost`/AX interaction with real apps —
   impractical to mock the OS injection layer in a small check. Verified by
   manually typing in three representative apps after building: one with
   good AX support (e.g. TextEdit/Safari), one without (Terminal), and one
   with vim-mode active — matching how the Accessibility-permission fix
   earlier in this project was verified by hand.
