# Temporary vim-mode disable toggle — Design

## Goal

A configurable hotkey that globally turns vim buffer processing off while
leaving the Vietnamese IME fully working. Use case: running a real vim/terminal
inside another app (e.g. a browser-hosted terminal) where UniVim's vim mode
fights the app's own vim. Toggling it off yields plain keystrokes to that app
(still Vietnamese-processed) without disabling the IME.

While disabled the menubar indicator gains a `-` suffix: `EN-` / `VI-`.

## Non-goals

- Not persisting the disabled state across restarts — it is session-only and
  resets to enabled on next launch. "Temporary" means temporary. Add a state
  file later if this needs to become sticky.
- Not per-app — this is one global toggle affecting all apps at once. Per-app
  disabling is what the existing `blacklist` file already does.
- Not shipping a default binding — the config key has no default, so nothing
  changes for existing users until they opt in by setting it. Avoids colliding
  with any app shortcut out of the box.
- Not a new hotkey-registration mechanism — reuses the existing `CGEventTap`
  detection and the existing `parse_hotkey` parser, exactly like the VN toggle
  and the restore-keystrokes hotkey do.
- Not touching the IME enable/disable flag (`g_vn_input.enabled`) — vim-disable
  is an orthogonal, independent flag.

## Components

### `src/vn_input.h` / `src/vn_input.c` — state + config

`struct vn_input` gains one session state field and three hotkey fields
mirroring the existing `hotkey_*` group:

```c
bool vim_disabled;                    // false = vim on (default). Session-only.
CGEventFlags disable_hotkey_mask;     // 0 = no binding (feature off)
int64_t disable_hotkey_keycode;       // only meaningful when has_... is true
bool has_disable_hotkey_keycode;      // false = modifier-only chord
```

All zero-initialized in `vn_input_begin` — `vim_disabled=false`,
`disable_hotkey_mask=0` (a zero mask means "no binding", same convention the
existing `hotkey_mask` uses at its default-fallback guard).

`vn_config_load` gains one `else if (strcmp(key, "disable_vim_hotkey") == 0)`
branch next to the existing `hotkey` branch. It calls the already-exposed
`parse_hotkey` and applies the results with the same defaults-for-fresh-load /
preserved-for-reload pattern as every other field. No default value — if the
key is absent, `disable_hotkey_mask` stays 0 and the feature is inert.

New function `vim_disable_toggle(struct vn_input* vn)`:

```c
void vim_disable_toggle(struct vn_input* vn) {
    vn->vim_disabled = !vn->vim_disabled;
    statusbar_refresh(vn);                       // menubar EN-/VI-
    toast_show(vn->vim_disabled ? "Vim off" : "Vim on");
}
```

It intentionally does NOT reset the vim/VN engine or re-run `svim.sh` — those
belong to IME toggling, not to gating key routing.

### Menubar label — single render function

Today the `EN`/`VI` label is built inline in `vn_input_toggle`
(`src/vn_input.c:393-395`) and the initial `"EN"` is hardcoded in
`statusbar_init` (`src/toast.m:102`). Extract one helper so both toggles and
init produce a consistent string including the suffix:

```c
// src/vn_input.c
void statusbar_refresh(struct vn_input* vn) {
    char label[8];
    snprintf(label, sizeof label, "%s%s",
             vn->enabled ? "VI" : "EN",
             vn->vim_disabled ? "-" : "");
    statusbar_update(label);
}
```

- `vn_input_toggle` replaces its inline label build + `statusbar_update` call
  with `statusbar_refresh(vn)` (keeps its existing `toast_show` for EN/VI).
- `vn_input_begin` calls `statusbar_refresh(vn)` after `statusbar_init()`
  instead of relying on the hardcoded `"EN"`, so the suffix is correct from the
  first frame even in the (impossible-at-boot but harmless) disabled case.

This is the only non-trivial new logic and the sole unit-tested piece.

### `src/event_tap.c` — detection + routing guard

**Detection.** Two parallel checks mirroring the VN-toggle hotkey, gated on
`disable_hotkey_mask != 0` so an unbound feature costs nothing:

- In `case kCGEventFlagsChanged:` (modifier-only chords, ~line 259), alongside
  the existing VN check:
  ```c
  if (!g_vn_input.has_disable_hotkey_keycode && g_vn_input.disable_hotkey_mask
      && (flags & VN_HOTKEY_RELEVANT_FLAGS) == g_vn_input.disable_hotkey_mask) {
      vim_disable_toggle(&g_vn_input);
  }
  ```
- In `case kCGEventKeyDown:` (modifier+key, ~line 277), alongside the existing
  VN check, before `front_app_ignored` branching:
  ```c
  if (g_vn_input.has_disable_hotkey_keycode) {
      // keycode + is_repeat + flags already read for the VN check; reuse them
      if (!is_repeat && keycode == g_vn_input.disable_hotkey_keycode
          && (flags & VN_HOTKEY_RELEVANT_FLAGS) == g_vn_input.disable_hotkey_mask) {
          vim_disable_toggle(&g_vn_input);
          return NULL; // consume
      }
  }
  ```
  Autorepeat excluded (same reason as the VN hotkey — hold = one toggle).

**Routing guard.** One-line change at the vim-vs-passthrough branch
(`src/event_tap.c:294`):

```c
if (event_tap->front_app_ignored || g_vn_input.vim_disabled) {
```

When `vim_disabled` is true every event routes through the existing
`vn_synthetic_process` path — the exact code blacklisted apps already use: vim
off, Vietnamese typing intact. No new routing code.

### `examples/vn_config` — documentation

Add a commented example line:

```
# disable_vim_hotkey=control+option+v   # toggle vim mode off (IME stays on); shows EN-/VI-
```

## Data flow

Config-load-time parsing plus two additive branches in the existing key-event
dispatch, plus one extra term in one existing `if`. The disable flag is read
only at that single routing branch — no interaction with `vn_engine`, `ax.c`,
or corrections. A matched hotkey returns before any of that runs, identical to
the existing hotkey short-circuits.

## Testing

1. **Unit test** (`test/test_vn_input.c` or a small addition, assert-based):
   `statusbar_refresh` label logic across the 4 state combos —
   `enabled=false,vim_disabled=false → "EN"`;
   `enabled=true,vim_disabled=false → "VI"`;
   `enabled=false,vim_disabled=true → "EN-"`;
   `enabled=true,vim_disabled=true → "VI-"`.
   Since `statusbar_refresh` calls into Cocoa (`statusbar_update`), the testable
   unit is the label-building step — factor the `snprintf` into a pure
   `vim_status_label(bool enabled, bool vim_disabled, char* out, size_t n)` that
   `statusbar_refresh` wraps, and assert on that. Hotkey parsing needs no new
   test — `parse_hotkey` is reused unchanged and already covered.
2. **Live verification** (no automated coverage for `CGEventTap` delivery):
   build, set `disable_vim_hotkey=control+option+v`, restart, confirm: the
   combo flips the menubar between `EN`/`EN-` (and `VI`/`VI-`); while disabled,
   vim keybindings do nothing in a normal text field but Vietnamese Telex still
   composes; the IME toggle still works and the `-` suffix survives an EN↔VI
   switch; holding the combo toggles once.
