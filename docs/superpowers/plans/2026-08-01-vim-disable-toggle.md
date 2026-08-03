# Temporary vim-mode disable toggle — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a configurable hotkey that globally turns UniVim's vim buffer processing off (Vietnamese IME stays on), with an `EN-`/`VI-` menubar indicator while disabled.

**Architecture:** One new session-only bool `vim_disabled` plus a hotkey (mask + optional keycode) live on the existing `struct vn_input`. The hotkey is parsed from a new `disable_vim_hotkey` config key via the existing `parse_hotkey`, detected in the existing `CGEventTap` `key_handler` (two parallel checks mirroring the VN toggle), and gates a single existing routing branch in `event_tap.c`. The menubar label is centralized in a new pure `vim_status_label()` helper wrapped by `statusbar_refresh()`.

**Tech Stack:** C99 + Objective-C, Carbon/Cocoa, libvim.a/libunikey.a, `make`, assert-based standalone tests.

## Global Constraints

- Language/standard: C99 (`-std=c99`), compiler `clang`. Copy verbatim from makefile.
- Build defines (required to compile any file including `buffer.h`→`libvim.h`): `-DHAVE_CONFIG_H -DMACOS_X -DMACOS_X_DARWIN`, includes `-Ilib -Ilib/libvim/proto -Isrc`.
- No new dependencies, no new subsystems. Reuse existing `parse_hotkey`, `statusbar_update`, `toast_show`, `vn_synthetic_process`, config watcher.
- `disable_vim_hotkey` has NO default binding: absent key ⇒ `disable_hotkey_mask == 0` ⇒ feature inert. Session-only: `vim_disabled` resets to `false` (vim on) every launch; never persisted.
- Hotkey syntax is identical to the existing `hotkey=` (whatever `parse_hotkey` accepts: control/shift/command/option + optional space/a-z/0-9). `control+option+v` is only a commented example, never hardcoded active.
- Full build check for the whole feature: `make` from repo root must succeed with `-Werror` (makefile line 10).

---

## File Structure

- `src/vn_input.h` — add 4 struct fields (`vim_disabled`, `disable_hotkey_mask`, `disable_hotkey_keycode`, `has_disable_hotkey_keycode`); declare `vim_disable_toggle`, `statusbar_refresh`, `vim_status_label`.
- `src/vn_input.c` — parse `disable_vim_hotkey` in `vn_config_load`; add `vim_status_label` (pure), `statusbar_refresh`, `vim_disable_toggle`; route `vn_input_toggle` + `vn_input_begin` through `statusbar_refresh`.
- `src/event_tap.c` — add two hotkey-detection branches in `key_handler` and one term to the routing `if`.
- `examples/vn_config` — new file documenting the config keys, including the commented `disable_vim_hotkey` example.
- `test/test_vn_input.c` — extend with assertions for `vim_status_label` and the new parse path.

---

## Task 1: State fields, pure label helper, and its test

**Files:**
- Modify: `src/vn_input.h` (struct at lines 21-33; declarations near line 45-47)
- Modify: `src/vn_input.c` (add helper functions)
- Test: `test/test_vn_input.c` (extend existing `main`)

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `struct vn_input` fields: `bool vim_disabled;`, `CGEventFlags disable_hotkey_mask;`, `int64_t disable_hotkey_keycode;`, `bool has_disable_hotkey_keycode;`
  - `void vim_status_label(bool enabled, bool vim_disabled, char* out, size_t n);` — writes `"EN"`/`"VI"`/`"EN-"`/`"VI-"` into `out`.

- [ ] **Step 1: Add the struct fields**

In `src/vn_input.h`, inside `struct vn_input` (after the `has_hotkey_keycode` line, currently line 30), add:

```c
  bool vim_disabled;                 // session-only; true = vim buffer processing off (IME unaffected)
  CGEventFlags disable_hotkey_mask;  // 0 = no binding (feature inert)
  int64_t disable_hotkey_keycode;    // meaningful only when has_disable_hotkey_keycode is true
  bool has_disable_hotkey_keycode;   // false = disable_hotkey_mask is a modifier-only chord
```

- [ ] **Step 2: Declare the new functions in the header**

In `src/vn_input.h`, after the `parse_hotkey` declaration (currently line 47), add:

```c
void vim_status_label(bool enabled, bool vim_disabled, char* out, size_t n);
void statusbar_refresh(struct vn_input* vn);
void vim_disable_toggle(struct vn_input* vn);
```

Also ensure `#include <stddef.h>` is present for `size_t` (add it near the other includes at the top, after line 3 `#include <stdint.h>` — check first; `stdint.h`/`stdbool.h` are already there, `stddef.h` may not be).

- [ ] **Step 3: Add the pure `vim_status_label` helper**

In `src/vn_input.c`, add this function (place it just above `vn_input_toggle`, near line 389). It must not touch Cocoa — pure string logic only:

```c
void vim_status_label(bool enabled, bool vim_disabled, char* out, size_t n) {
  snprintf(out, n, "%s%s", enabled ? "VI" : "EN", vim_disabled ? "-" : "");
}
```

- [ ] **Step 4: Write the failing test**

In `test/test_vn_input.c`, add this block just before the final `printf("ALL TESTS PASSED\n");` (currently line 77):

```c
  // vim_status_label: 4 state combos (enabled x vim_disabled)
  {
    char label[8];
    vim_status_label(false, false, label, sizeof label);
    assert(strcmp(label, "EN") == 0);
    vim_status_label(true, false, label, sizeof label);
    assert(strcmp(label, "VI") == 0);
    vim_status_label(false, true, label, sizeof label);
    assert(strcmp(label, "EN-") == 0);
    vim_status_label(true, true, label, sizeof label);
    assert(strcmp(label, "VI-") == 0);
    printf("[vim_status_label] OK\n");
  }
```

Add `#include <string.h>` at the top of `test/test_vn_input.c` if not already present (needed for `strcmp` — check the current includes at lines 1-4; only `assert.h`/`stdio.h` are there, so add it).

- [ ] **Step 5: Run test to verify it fails**

Run:
```bash
clang -std=c99 -DHAVE_CONFIG_H -DMACOS_X -DMACOS_X_DARWIN -Wno-return-type \
  -Ilib -Ilib/libvim/proto -Isrc \
  test/test_vn_input.c src/vn_input.c src/helpers.c src/helpers.m \
  src/vn_engine.c src/env_vars.c src/toast.m \
  lib/libvim.a lib/libunikey.a -lm -lncurses -liconv -lc++ \
  -framework Carbon -framework Cocoa -o /tmp/test_vn_input && /tmp/test_vn_input
```
Expected: FAIL — before Step 3 the compile errors on undefined `vim_status_label`; with Step 3 done first it should PASS. (If you did Steps 1-3 before writing the test, this step confirms the compile succeeds and the new assertions pass; the "failing" state is the missing function.)

- [ ] **Step 6: Run test to verify it passes**

Run the same command as Step 5.
Expected: PASS — output includes `[vim_status_label] OK` and `ALL TESTS PASSED`.

- [ ] **Step 7: Commit**

```bash
git add src/vn_input.h src/vn_input.c test/test_vn_input.c
git commit -m "feat: add vim_disabled state + vim_status_label helper"
```

---

## Task 2: Menubar refresh + vim_disable_toggle, wired through existing toggles

**Files:**
- Modify: `src/vn_input.c` (`statusbar_refresh`, `vim_disable_toggle`; `vn_input_toggle` at 389-406; `vn_input_begin` at 337-368)

**Interfaces:**
- Consumes: `vim_status_label` (Task 1); `statusbar_update`, `toast_show` (from `toast.h`, already included at `vn_input.c:5`).
- Produces: `void statusbar_refresh(struct vn_input* vn);`, `void vim_disable_toggle(struct vn_input* vn);`

- [ ] **Step 1: Add `statusbar_refresh` and `vim_disable_toggle`**

In `src/vn_input.c`, just below `vim_status_label` (from Task 1), add:

```c
void statusbar_refresh(struct vn_input* vn) {
  char label[8];
  vim_status_label(vn->enabled, vn->vim_disabled, label, sizeof label);
  statusbar_update(label);
}

void vim_disable_toggle(struct vn_input* vn) {
  vn->vim_disabled = !vn->vim_disabled;
  statusbar_refresh(vn);
  toast_show(vn->vim_disabled ? "Vim off" : "Vim on");
  vn_debug_log("vim_disable_toggle: vim_disabled now=%d", vn->vim_disabled);
}
```

Note: intentionally does NOT reset the vim/VN engine or run `svim.sh` — those are IME-toggle concerns.

- [ ] **Step 2: Route `vn_input_toggle` through `statusbar_refresh`**

In `src/vn_input.c`, in `vn_input_toggle` (lines 389-406), replace:

```c
  const char* label = vn->enabled ? "VI" : "EN";
  toast_show(label);
  statusbar_update(label);
```

with:

```c
  toast_show(vn->enabled ? "VI" : "EN");
  statusbar_refresh(vn);
```

(The toast keeps showing the plain EN/VI; the menubar goes through `statusbar_refresh` so the `-` suffix survives an EN↔VI switch while vim is disabled.)

- [ ] **Step 3: Make `vn_input_begin` render the correct initial label**

In `src/vn_input.c`, in `vn_input_begin`, replace the final line `statusbar_init();` (line 367) with:

```c
  statusbar_init();
  statusbar_refresh(vn);
```

- [ ] **Step 4: Build the whole app to verify it compiles**

Run:
```bash
make
```
Expected: builds `bin/univim` with no errors (`-Werror` clean).

- [ ] **Step 5: Re-run the unit test (regression)**

Run the Task 1 Step 5 command.
Expected: PASS — `ALL TESTS PASSED`.

- [ ] **Step 6: Commit**

```bash
git add src/vn_input.c
git commit -m "feat: centralize menubar label render + add vim_disable_toggle"
```

---

## Task 3: Config parsing for `disable_vim_hotkey`

**Files:**
- Modify: `src/vn_input.c` (`vn_config_load` at 145-281: defaults block 151-157, no-file apply 167-172, key branch near 213, final apply 263-269)

**Interfaces:**
- Consumes: `parse_hotkey` (existing); struct fields from Task 1.
- Produces: parsed values written into `vn->disable_hotkey_mask` / `disable_hotkey_keycode` / `has_disable_hotkey_keycode`.

- [ ] **Step 1: Add defaults in the defaults block**

In `src/vn_input.c`, after the `new_has_hotkey_keycode` default (line 155), add:

```c
  CGEventFlags new_disable_hotkey = is_reload ? vn->disable_hotkey_mask : 0;
  int64_t new_disable_hotkey_keycode = is_reload ? vn->disable_hotkey_keycode : 0;
  bool new_has_disable_hotkey_keycode = is_reload ? vn->has_disable_hotkey_keycode : false;
```

- [ ] **Step 2: Apply defaults in the no-file early-return block**

In `src/vn_input.c`, inside the `if (!file)` block, after `vn->has_hotkey_keycode = new_has_hotkey_keycode;` (line 170), add:

```c
    vn->disable_hotkey_mask = new_disable_hotkey;
    vn->disable_hotkey_keycode = new_disable_hotkey_keycode;
    vn->has_disable_hotkey_keycode = new_has_disable_hotkey_keycode;
```

- [ ] **Step 3: Add the `disable_vim_hotkey` key branch**

In `src/vn_input.c`, immediately after the closing `}` of the `else if (strcmp(key, "hotkey") == 0) { ... }` branch (which ends at line 229, before `else if (strcmp(key, "debug")`), insert:

```c
    } else if (strcmp(key, "disable_vim_hotkey") == 0) {
      int64_t keycode;
      bool has_keycode;
      bool hotkey_valid;
      CGEventFlags mask = parse_hotkey(value, &keycode, &has_keycode, &hotkey_valid);
      if (!hotkey_valid || (mask == 0 && !has_keycode && strlen(value) > 0)) {
        if (error_count < 3) {
          char err[128];
          snprintf(err, sizeof(err), "line %d: invalid disable_vim_hotkey '%s'\n", line_num, value);
          strncat(errors, err, sizeof(errors) - strlen(errors) - 1);
        }
        error_count++;
      } else {
        new_disable_hotkey = mask;
        new_disable_hotkey_keycode = keycode;
        new_has_disable_hotkey_keycode = has_keycode;
      }
```

(This inserts a new `} else if ...` arm; it must sit between the `hotkey` arm's close and the `debug` arm's `} else if`.)

- [ ] **Step 4: Apply parsed values in the final apply block**

In `src/vn_input.c`, in the "Apply successfully parsed values" block, after `vn->has_hotkey_keycode = new_has_hotkey_keycode;` (line 267), add:

```c
  vn->disable_hotkey_mask = new_disable_hotkey;
  vn->disable_hotkey_keycode = new_disable_hotkey_keycode;
  vn->has_disable_hotkey_keycode = new_has_disable_hotkey_keycode;
```

- [ ] **Step 5: Extend the unit test for the parse path**

In `test/test_vn_input.c`, inside the existing `parse_hotkey` block (after line 72's `control+xyz` assertion, before its `printf`), add:

```c
    // disable_vim_hotkey accepts the same syntax (parsed by the same parse_hotkey)
    mask = parse_hotkey("control+option+v", &keycode, &has_keycode, &valid);
    assert(valid);
    assert(mask == (kCGEventFlagMaskControl | kCGEventFlagMaskAlternate));
    assert(has_keycode);
    assert(keycode == kVK_ANSI_V);
```

- [ ] **Step 6: Run the unit test**

Run the Task 1 Step 5 command.
Expected: PASS — `ALL TESTS PASSED`.

- [ ] **Step 7: Build the whole app**

Run: `make`
Expected: builds clean.

- [ ] **Step 8: Commit**

```bash
git add src/vn_input.c test/test_vn_input.c
git commit -m "feat: parse disable_vim_hotkey config key"
```

---

## Task 4: Hotkey detection + routing guard in event_tap

**Files:**
- Modify: `src/event_tap.c` (`key_handler`: flagsChanged 248-264, keyDown 269-291, routing branch 293-301)

**Interfaces:**
- Consumes: `vim_disable_toggle`, `g_vn_input.disable_hotkey_*`, `g_vn_input.vim_disabled` (all from Tasks 1-3); `VN_HOTKEY_RELEVANT_FLAGS` macro (event_tap.c:7).
- Produces: runtime behavior only.

- [ ] **Step 1: Add modifier-only detection in the flagsChanged case**

In `src/event_tap.c`, inside `case kCGEventFlagsChanged:`, immediately after the existing VN `if (!g_vn_input.has_hotkey_keycode ...) { ... }` block closes (after line 263), add:

```c
      if (!g_vn_input.has_disable_hotkey_keycode && g_vn_input.disable_hotkey_mask
          && (flags & VN_HOTKEY_RELEVANT_FLAGS) == g_vn_input.disable_hotkey_mask) {
        vim_disable_toggle(&g_vn_input);
      }
```

(`flags` is already in scope from line 249.)

- [ ] **Step 2: Add modifier+key detection in the keyDown case**

In `src/event_tap.c`, inside `case kCGEventKeyDown:`, after the existing VN `if (g_vn_input.has_hotkey_keycode) { ... }` block closes (after line 291) and BEFORE the `struct event_tap* event_tap = ...` line (293), add:

```c
      if (g_vn_input.has_disable_hotkey_keycode) {
        int64_t d_keycode = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
        bool d_is_repeat = CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat);
        CGEventFlags d_flags = CGEventGetFlags(event);
        if (!d_is_repeat && d_keycode == g_vn_input.disable_hotkey_keycode
            && (d_flags & VN_HOTKEY_RELEVANT_FLAGS) == g_vn_input.disable_hotkey_mask) {
          vim_disable_toggle(&g_vn_input);
          return NULL; // consume so the key isn't typed
        }
      }
```

(Uses locally-named `d_*` vars to avoid colliding with the VN block's `keycode`/`is_repeat`/`flags`, which are scoped inside the `if (g_vn_input.has_hotkey_keycode)` block above and not visible here.)

- [ ] **Step 3: Add the routing guard**

In `src/event_tap.c`, change the routing branch condition (line 294) from:

```c
      if (event_tap->front_app_ignored) {
```

to:

```c
      if (event_tap->front_app_ignored || g_vn_input.vim_disabled) {
```

- [ ] **Step 4: Build the whole app**

Run: `make`
Expected: builds `bin/univim` clean.

- [ ] **Step 5: Run the unit test (regression)**

Run the Task 1 Step 5 command.
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/event_tap.c
git commit -m "feat: detect disable_vim_hotkey and gate vim routing on vim_disabled"
```

---

## Task 5: Document the config key in examples

**Files:**
- Create: `examples/vn_config`

**Interfaces:** none (documentation).

- [ ] **Step 1: Create `examples/vn_config`**

Create `examples/vn_config` with:

```
# UniVim Vietnamese IME config. Copy to ~/.config/univim/vn_config.
# Lines starting with # are comments. Format: key=value. Live-reloaded on save.

# Input method: telex | vni | simpletelex  (default: simpletelex)
# method=simpletelex

# IME on/off toggle hotkey. Modifiers control/shift/command/option, plus one
# optional regular key (space, a-z, or 0-9). Default: control+shift.
# hotkey=control+shift

# Temporarily disable vim buffer mode while keeping the IME on (e.g. when
# running a real vim/terminal inside another app). Menubar shows EN-/VI- while
# disabled. Same syntax as `hotkey`. No default -- uncomment to enable.
# disable_vim_hotkey=control+option+v

# Tone-mark placement: 1/on = modern, 0/off = classic  (default: on)
# modern_style=on

# Debug logging to ~/.config/univim/vn_debug.log: 1/on or 0/off  (default: off)
# debug=off
```

- [ ] **Step 2: Verify the example parses cleanly**

Manual verification (the parser only reads `~/.config/univim/vn_config`, so this confirms syntax by inspection): every non-comment line above is commented out, so a fresh copy is inert and valid. No automated step.

- [ ] **Step 3: Commit**

```bash
git add examples/vn_config
git commit -m "docs: add example vn_config documenting disable_vim_hotkey"
```

---

## Task 6: Live verification (manual, no automated coverage)

**Files:** none (runtime verification of the assembled feature).

`CGEventTap` delivery can't be unit-tested; this task confirms the end-to-end behavior in a real session.

- [ ] **Step 1: Configure the hotkey**

Add to `~/.config/univim/vn_config`:
```
disable_vim_hotkey=control+option+v
```
Save (config auto-reloads; if unsure, restart `bin/univim`).

- [ ] **Step 2: Verify menubar toggling**

Press `control+option+v`. Expected: menubar `EN` → `EN-` (or `VI` → `VI-`), and a "Vim off" toast. Press again: back to `EN`/`VI`, "Vim on" toast.

- [ ] **Step 3: Verify vim is actually disabled but IME works**

With vim disabled (`EN-`/`VI-`), in a normal text field: vim normal-mode keybindings do nothing (plain typing). Switch IME to VI and type Telex (e.g. `tieengs` → `tiếng`): Vietnamese composition still works.

- [ ] **Step 4: Verify suffix survives IME switch**

While disabled, press the IME toggle (`control+shift`). Expected: label flips `EN-`↔`VI-`, keeping the `-`.

- [ ] **Step 5: Verify re-enable restores vim**

Press `control+option+v` again. Expected: `-` gone; vim keybindings work again in a text field.

- [ ] **Step 6: Verify autorepeat safety**

Hold `control+option+v` down. Expected: toggles once, does not rapid-flip.

- [ ] **Step 7: Verify no-binding default**

Remove/comment the `disable_vim_hotkey` line, restart. Expected: no combo disables vim; behavior identical to before the feature.

---

## Self-Review Notes

- **Spec coverage:** state field (T1), menubar `EN-`/`VI-` render (T1/T2), toast on toggle (T2), config key + no-default + live reload via existing watcher (T3), hotkey detection both paths (T4), routing guard reusing `vn_synthetic_process` (T4), example docs (T5), live verification (T6). All spec sections mapped.
- **Type consistency:** `vim_status_label(bool,bool,char*,size_t)`, `statusbar_refresh(struct vn_input*)`, `vim_disable_toggle(struct vn_input*)`, fields `vim_disabled`/`disable_hotkey_mask`/`disable_hotkey_keycode`/`has_disable_hotkey_keycode` used identically across T1-T4.
- **Compile note:** the standalone test command was verified to build and pass against the current tree (needs `helpers.m`, `toast.m`, `env_vars.c`, `vn_engine.c` and the vim/unikey libs because `vn_input.c` pulls in `buffer.h`→`libvim.h`). The older doc command (`clang -Ilib -Isrc test/... src/vn_input.c src/helpers.c`) is stale and will not link.
