# Restore Keystrokes via z-trigger Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a user recover an accidentally Vietnamese-transformed English word (e.g. typed "of", got "ò") by typing a trailing `z` right after the word — no dictionary, no false-positive risk.

**Architecture:** Wire libunikey's `UnikeyRestoreKeyStrokes()` into a new `vn_engine_restore_key_strokes()` in `src/vn_engine.c` (pure logic, unit-testable). A new opt-in `restore_trigger_key=` config value (`src/vn_input.c`) names the trigger keycode. Both keystroke-processing flows (`src/event_tap.c` Flow A, `src/ax.c` Flow B) detect an unmodified press of that key, attempt the restore first, and only consume the keystroke when the restore actually changed something — a no-op result (word was never transformed) falls through to ordinary key processing, so the trigger character still types literally in words like "quiz"/"jazz".

**Tech Stack:** C99, libunikey (vendored C++ engine via a C bridge), Carbon `CGEventTap`/Accessibility APIs.

**Spec:** `docs/superpowers/specs/2026-08-02-restore-keystrokes-z-trigger-design.md`

## Global Constraints

- `restore_trigger_key=` is opt-in only — no line in `vn_config` means the feature is fully disabled (`has_restore_trigger_keycode == false`).
- `z` is the only documented/recommended value (the only letter that never appears in real Vietnamese text); other single letters/digits technically parse via the existing key-lookup table but are not a supported use case.
- Must never intercept Cmd+`z` or Ctrl+`z` (undo in many apps/editors). Command is already excluded upstream in both flows before this code runs; Control and Option must be excluded explicitly by this feature's own check.
- Applies identically to both Flow A (`event_tap.c`, apps svim's vim-mode ignores) and Flow B (`ax.c`, vim-mode-active apps).
- Always attempt the restore first; only a non-no-op result (`backspace_count > 0` or `insert_len > 0`) may consume the keystroke. A no-op result (`backspace_count == 0 && insert_len == 0`) must fall through to ordinary `vn_engine_process_key` handling of that same keystroke.

---

### Task 1: `vn_engine_restore_key_strokes()` + unit tests

**Files:**
- Modify: `src/vn_engine.h`
- Modify: `src/vn_engine.c`
- Test: `test/test_vn_engine.c`

**Interfaces:**
- Produces: `struct vn_engine_result vn_engine_restore_key_strokes(void)` — same `vn_engine_result` shape (`backspace_count`, `insert_text`, `insert_len`) every other `vn_engine_process_*` function already returns. Consumed by Task 3 (`event_tap.c`) and Task 4 (`ax.c`).

- [ ] **Step 1: Write the failing tests**

In `test/test_vn_engine.c`, replace the existing `type_sequence` function (currently lines 11-40) with a shared `apply_correction` helper plus a `type_sequence` that uses it — this is a pure refactor (same logic, now reusable), no behavior change:

```c
// Applies a vn_engine_result's backspace/insert onto a plain-C-string
// oracle buffer -- shared by type_sequence (below) and the restore tests,
// since both need the exact same "N character-backspaces, then insert N
// bytes" logic that the real integration points (event_tap.c/ax.c) apply
// incrementally instead.
static void apply_correction(struct vn_engine_result r, char* out, size_t* len) {
  if (r.backspace_count > 0) {
    // backspace_count is a character count (vn_engine.c converts
    // libunikey's raw byte count into this via its own tracked word
    // history) -- remove that many trailing *characters*, which may span
    // more than that many bytes, by skipping UTF-8 continuation bytes.
    int remaining = r.backspace_count;
    while (remaining > 0 && *len > 0) {
      (*len)--;
      while (*len > 0 && (out[*len] & 0xC0) == 0x80) (*len)--;
      remaining--;
    }
    out[*len] = '\0';
  }
  if (r.insert_len > 0) {
    memcpy(out + *len, r.insert_text, r.insert_len);
    *len += r.insert_len;
    out[*len] = '\0';
  }
}

// Feeds a sequence of ASCII keystrokes through the engine and returns the
// fully composed UTF-8 result as a plain C string (test-only helper — the
// real integration points apply backspace_count/insert_text incrementally
// instead of collecting a final string, but doing so here gives a simple,
// readable oracle to assert against).
static void type_sequence(const char* keys, char* out, size_t out_size) {
  size_t len = 0;
  out[0] = '\0';
  for (const char* k = keys; *k; k++) {
    struct vn_engine_result r = vn_engine_process_key((unsigned char) *k, false, false);
    apply_correction(r, out, &len);
    if (r.insert_len == 0 && r.backspace_count == 0) {
      // no correction needed — the OS just echoes the original keystroke
      out[len++] = *k;
      out[len] = '\0';
    }
  }
  (void) out_size;
}
```

Right after the existing `check` function (ends at line 50), add:

```c
// Types `keys`, then triggers vn_engine_restore_key_strokes() and applies
// its result the same way a real caller (event_tap.c/ax.c) would — checks
// the word reverts to its original literal keystrokes, or, for a word that
// was never transformed, that restore is a true no-op. That no-op case is
// exactly what lets the real caller fall back to typing the trigger key
// literally instead of misfiring on words like "quiz".
static void check_restore(const char* label, vn_method method, const char* keys, const char* expected) {
  vn_engine_set_method(method);
  vn_engine_reset();
  char out[256];
  type_sequence(keys, out, sizeof(out));
  size_t len = strlen(out);
  struct vn_engine_result r = vn_engine_restore_key_strokes();
  apply_correction(r, out, &len);
  printf("[%s] keys=\"%s\" after_restore=\"%s\" want=\"%s\" %s\n",
         label, keys, out, expected, strcmp(out, expected) == 0 ? "OK" : "MISMATCH");
  assert(strcmp(out, expected) == 0);
}
```

In `main()`, right after the existing "cursor-move breaks composition" block (the `}` that currently closes it, just before the macro-expansion test block), add:

```c
  // z-trigger design (docs/superpowers/specs/2026-08-02-restore-keystrokes-z-trigger-design.md):
  // restoring a transformed word must revert it to the literal keystrokes,
  // and restoring an untransformed word must be a true no-op -- verified
  // against the real engine (values below are not guessed).
  check_restore("restore reverts a transformed word", VN_METHOD_TELEX, "of", "of");
  check_restore("restore is a no-op on an untransformed word", VN_METHOD_TELEX, "hello", "hello");
  check_restore("restore reverts a word with a real tone mark", VN_METHOD_TELEX, "thaays", "thaays");
  check_restore("restore is a no-op on a word that never triggers a tone", VN_METHOD_TELEX, "quiz", "quiz");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `clang -std=c99 -Ilib -o /tmp/test_vn_engine test/test_vn_engine.c src/vn_engine.c lib/libunikey.a -lc++`
Expected: FAIL to compile — `error: use of undeclared identifier 'vn_engine_restore_key_strokes'` (or `implicit declaration of function` depending on clang version).

- [ ] **Step 3: Add the declaration**

In `src/vn_engine.h`, right after `struct vn_engine_result vn_engine_process_backspace(void);` (currently line 23):

```c
struct vn_engine_result vn_engine_restore_key_strokes(void);
```

- [ ] **Step 4: Write the implementation**

In `src/vn_engine.c`, right after `vn_engine_process_backspace` (currently ends at line 227):

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

No extra `vn_engine_reset()` needed afterward — `UnikeyRestoreKeyStrokes()` already replays the restored keys through the engine's own append logic internally, so subsequent typing continues correctly without help.

- [ ] **Step 5: Run test to verify it passes**

Run: `clang -std=c99 -Ilib -o /tmp/test_vn_engine test/test_vn_engine.c src/vn_engine.c lib/libunikey.a -lc++ && /tmp/test_vn_engine`
Expected: every line ends `OK`, final line `ALL TESTS PASSED`.

- [ ] **Step 6: Commit**

```bash
git add src/vn_engine.h src/vn_engine.c test/test_vn_engine.c
git commit -m "feat: add vn_engine_restore_key_strokes for z-trigger restore feature"
```

---

### Task 2: `restore_trigger_key=` config wiring + docs

**Files:**
- Modify: `src/vn_input.h`
- Modify: `src/vn_input.c`
- Modify: `README.md`

**Interfaces:**
- Consumes: the existing `static bool lookup_hotkey_key(const char* token, CGKeyCode* out_code)` helper already in `src/vn_input.c` (built for `hotkey=`'s regular-key component) — no changes to it.
- Produces: `struct vn_input.restore_trigger_keycode` (`int64_t`) and `.has_restore_trigger_keycode` (`bool`) — read directly by Task 3 (`event_tap.c`) and Task 4 (`ax.c`), the same way `hotkey_keycode`/`has_hotkey_keycode` are already read directly with no wrapper function.

- [ ] **Step 1: Add the fields to `struct vn_input`**

In `src/vn_input.h`, add two fields right after `bool has_hotkey_keycode;`:

```c
struct vn_input {
  bool enabled;
  bool debug;
  char** blacklist;
  uint32_t blacklist_count;
  vn_method method;
  bool modern_style;
  CGEventFlags hotkey_mask;
  int64_t hotkey_keycode;   // meaningful only when has_hotkey_keycode is true
  bool has_hotkey_keycode;  // false = hotkey_mask is a modifier-only chord (unchanged behavior)
  int64_t restore_trigger_keycode;  // meaningful only when has_restore_trigger_keycode is true
  bool has_restore_trigger_keycode; // false = feature disabled (no restore_trigger_key= line)
  struct vn_override* overrides;
  uint32_t overrides_count;
};
```

- [ ] **Step 2: Parse `restore_trigger_key` in `vn_config_load`**

In `src/vn_input.c`, add new default variables next to the existing ones (currently the block starting at line 152, right after `bool new_has_hotkey_keycode = is_reload ? vn->has_hotkey_keycode : false;`):

```c
  int64_t new_restore_trigger_keycode = is_reload ? vn->restore_trigger_keycode : 0;
  bool new_has_restore_trigger_keycode = is_reload ? vn->has_restore_trigger_keycode : false;
```

In the same function's "no config file" early-return branch (currently lines 167-172), add the same two fields alongside the existing assignments:

```c
    vn->method = new_method;
    vn->hotkey_mask = new_hotkey;
    vn->hotkey_keycode = new_hotkey_keycode;
    vn->has_hotkey_keycode = new_has_hotkey_keycode;
    vn->restore_trigger_keycode = new_restore_trigger_keycode;
    vn->has_restore_trigger_keycode = new_has_restore_trigger_keycode;
    vn->debug = new_debug;
    vn->modern_style = new_modern_style;
    return NULL;
```

Add a new parse branch in the `if`/`else if` chain, right before the final `} else {` that handles unknown keys (currently line 252):

```c
    } else if (strcmp(key, "restore_trigger_key") == 0) {
      CGKeyCode code;
      if (!lookup_hotkey_key(value, &code)) {
        if (error_count < 3) {
          char err[128];
          snprintf(err, sizeof(err), "line %d: invalid restore_trigger_key '%s'\n", line_num, value);
          strncat(errors, err, sizeof(errors) - strlen(errors) - 1);
        }
        error_count++;
      } else {
        new_restore_trigger_keycode = code;
        new_has_restore_trigger_keycode = true;
      }
    } else {
```

(This replaces the existing bare `} else {` — the new branch slots in immediately before it, same technique already used for `modern_style`.)

Add the corresponding apply lines next to the others (currently lines 264-269):

```c
  vn->method = new_method;
  vn->hotkey_mask = new_hotkey;
  vn->hotkey_keycode = new_hotkey_keycode;
  vn->has_hotkey_keycode = new_has_hotkey_keycode;
  vn->restore_trigger_keycode = new_restore_trigger_keycode;
  vn->has_restore_trigger_keycode = new_has_restore_trigger_keycode;
  vn->debug = new_debug;
  vn->modern_style = new_modern_style;
```

- [ ] **Step 3: Document the config key**

In `README.md`, update the `vn_config` example block (currently lines 41-46):

```
method=simpletelex  # telex, simpletelex (default), or vni
hotkey=control+shift
restore_trigger_key=z  # opt-in; typing z right after a word reverts it to
                        # the literal keys typed, if libunikey actually
                        # transformed it -- otherwise z types normally
debug=1             # logs routing/correction decisions to ~/.config/univim/vn_debug.log
modern_style=1      # 1/on (default, e.g. "hoà") or 0/off (old style, e.g. "hòa")
```

Right after the existing `hotkey=` explanation paragraph (currently ends at line 52, "...so it's rejected as invalid instead)."), add:

```
`restore_trigger_key=` is disabled unless set (no default) — a single
regular key (same table as `hotkey=`'s regular-key component: `space`,
`a`-`z`, `0`-`9`), no modifier. `z` is the only recommended value: it's the
one letter that never appears in real Vietnamese text, so an unmodified
press of it only ever reverts a word libunikey actually transformed (e.g.
"of" → "ò", then `z` → "of") and otherwise types out literally (e.g.
"quiz", "jazz" type unchanged, since neither ever triggers a transformation
to revert). Cmd/Ctrl/Option held alongside the key (e.g. Cmd+Z / Ctrl+Z
undo) is never treated as this trigger.
```

- [ ] **Step 4: Build the full project and confirm no regressions**

`vn_input.c` transitively includes `lib/libvim/libvim.h` (via `buffer.h`) and calls into `env_vars.c`/`toast.m`/`vn_engine.c`, so there's no small standalone link line for it; use the project's actual build:

Run: `make clean && make`
Expected: builds `bin/univim` with no errors or warnings (the Makefile's `WARN_FLAGS` includes `-Werror`).

Run: `clang -std=c99 -Ilib -o /tmp/test_vn_engine test/test_vn_engine.c src/vn_engine.c lib/libunikey.a -lc++ && /tmp/test_vn_engine`
Expected: `ALL TESTS PASSED` (re-confirms Task 1's engine wiring still works once `vn_input.c` also compiles).

- [ ] **Step 5: Commit**

```bash
git add src/vn_input.h src/vn_input.c README.md
git commit -m "feat: add restore_trigger_key= config for z-trigger restore"
```

---

### Task 3: Flow A detection (`event_tap.c`)

**Files:**
- Modify: `src/event_tap.c`

**Interfaces:**
- Consumes: `struct vn_engine_result vn_engine_restore_key_strokes(void)` (Task 1), `g_vn_input.has_restore_trigger_keycode` / `g_vn_input.restore_trigger_keycode` (Task 2).

- [ ] **Step 1: Replace the key/backspace dispatch in `vn_synthetic_process`**

In `src/event_tap.c`, replace the existing dispatch (currently lines 214-218):

```c
  struct vn_engine_result result = (keycode == kVK_Delete)
    ? vn_engine_process_backspace()
    : vn_engine_process_key(character,
                             flags & kCGEventFlagMaskShift,
                             flags & kCGEventFlagMaskAlphaShift);
```

with:

```c
  // A candidate z-trigger keystroke (docs/superpowers/specs/2026-08-02-
  // restore-keystrokes-z-trigger-design.md): only fires with no Control/
  // Option held (Command already returned early above at the
  // kCGEventFlagMaskCommand check; Shift/CapsLock stay allowed so
  // Shift+trigger-key also works). is_repeat is already excluded by the
  // autorepeat guard just above this block. Always attempts restore first
  // and only commits to it when that's not a no-op -- a no-op means the
  // current word was never transformed, so the keystroke falls through to
  // ordinary key processing instead (e.g. "quiz" types out literally).
  bool is_restore_candidate = g_vn_input.has_restore_trigger_keycode
    && keycode == g_vn_input.restore_trigger_keycode
    && (flags & (kCGEventFlagMaskControl | kCGEventFlagMaskAlternate)) == 0;

  struct vn_engine_result result;
  if (is_restore_candidate) {
    result = vn_engine_restore_key_strokes();
    if (result.backspace_count == 0 && result.insert_len == 0) {
      result = vn_engine_process_key(character, flags & kCGEventFlagMaskShift,
                                      flags & kCGEventFlagMaskAlphaShift);
    }
  } else {
    result = (keycode == kVK_Delete)
      ? vn_engine_process_backspace()
      : vn_engine_process_key(character,
                               flags & kCGEventFlagMaskShift,
                               flags & kCGEventFlagMaskAlphaShift);
  }
```

Everything downstream (the debug log, posting the correction, `return NULL`) is unchanged.

- [ ] **Step 2: Build the full project**

Run: `make clean && make`
Expected: builds `bin/univim` with no errors or warnings.

- [ ] **Step 3: Live verification (Flow A)**

No automated coverage is possible here — it needs real `CGEventTap` delivery, same as every event-tap-level change in this project.

1. Add `restore_trigger_key=z` to `~/.config/univim/vn_config`. Restart `univim`.
2. In an app Flow A applies to (one not using vim-mode / in svim's ignore list), enable VN input (`hotkey=`, default `control+shift`), type `textz` — confirm it reverts to `text` with no literal `z` left behind.
3. Type `quiz` and `jazz` — confirm both type out unchanged, including the `z`.
4. Confirm holding `z` down (autorepeat) after a transformed word doesn't misfire repeatedly — the existing autorepeat guard drops the repeated events entirely.

- [ ] **Step 4: Commit**

```bash
git add src/event_tap.c
git commit -m "feat: detect z-trigger restore in Flow A (event_tap.c)"
```

---

### Task 4: Flow B detection (`ax.c`) + full live verification

**Files:**
- Modify: `src/ax.h`
- Modify: `src/ax.c`

**Interfaces:**
- Consumes: same as Task 3 — `vn_engine_restore_key_strokes()`, `g_vn_input.has_restore_trigger_keycode` / `restore_trigger_keycode`.
- Produces: `FLAG_CONTROL`, `FLAG_ALTERNATE` macros in `src/ax.h` — new, parallel to the existing `FLAG_SHIFT`/`FLAG_COMMAND`, for any future flag check in this file to reuse.

- [ ] **Step 1: Add `FLAG_CONTROL`/`FLAG_ALTERNATE` macros**

In `src/ax.h`, replace:

```c
#define FLAG_SHIFT   1 << 17
#define FLAG_COMMAND 1 << 20
```

with:

```c
#define FLAG_SHIFT     1 << 17
#define FLAG_CONTROL   1 << 18
#define FLAG_ALTERNATE 1 << 19
#define FLAG_COMMAND   1 << 20
```

(Values match `kCGEventFlagMaskControl`/`kCGEventFlagMaskAlternate`'s real bit positions, same convention `FLAG_SHIFT`/`FLAG_COMMAND` already follow.)

- [ ] **Step 2: Replace the key/backspace dispatch in `ax_process_event`**

In `src/ax.c`, inside the `if (flow == VN_FLOW_VIM_BUFFER) {` block, replace (currently lines 328-332):

```c
    if (flow == VN_FLOW_VIM_BUFFER) {
      struct vn_engine_result result = (keycode == kVK_Delete)
        ? vn_engine_process_backspace()
        : vn_engine_process_key(character, flags & FLAG_SHIFT,
                                flags & kCGEventFlagMaskAlphaShift);
```

with:

```c
    if (flow == VN_FLOW_VIM_BUFFER) {
      // Same z-trigger check as event_tap.c's Flow A (docs/superpowers/specs/
      // 2026-08-02-restore-keystrokes-z-trigger-design.md). FLAG_COMMAND is
      // already excluded earlier in this function (resets and returns), and
      // the autorepeat guard just above this block already excludes
      // repeats, so only Control/Option need excluding here.
      bool is_restore_candidate = g_vn_input.has_restore_trigger_keycode
        && keycode == g_vn_input.restore_trigger_keycode
        && (flags & (FLAG_CONTROL | FLAG_ALTERNATE)) == 0;

      struct vn_engine_result result;
      if (is_restore_candidate) {
        result = vn_engine_restore_key_strokes();
        if (result.backspace_count == 0 && result.insert_len == 0) {
          result = vn_engine_process_key(character, flags & FLAG_SHIFT,
                                          flags & kCGEventFlagMaskAlphaShift);
        }
      } else {
        result = (keycode == kVK_Delete)
          ? vn_engine_process_backspace()
          : vn_engine_process_key(character, flags & FLAG_SHIFT,
                                  flags & kCGEventFlagMaskAlphaShift);
      }
```

Everything below this (the `if (result.backspace_count > 0 || result.insert_len > 0) { ... }` block through `buffer_input_string`/`vn_post_correction`) is unchanged.

- [ ] **Step 3: Build the full project**

Run: `make clean && make`
Expected: builds `bin/univim` with no errors or warnings.

- [ ] **Step 4: Live verification (Flow B + combined edge cases)**

1. With `restore_trigger_key=z` still set, in a vim-mode-active app (Flow B — e.g. a real text editor with svim's vim-mode enabled and AX support), enable VN input, type `textz` in INSERT mode — confirm it reverts to `text` with no literal `z` left behind.
2. Type `quiz` and `jazz` in the same app — confirm both type out unchanged.
3. Press Cmd+Z, then Ctrl+Z, in a real text editor with VN input enabled — confirm Undo still works normally in both flows (the event must be completely untouched by this feature).
4. Confirm both Flow A (Task 3) and Flow B behave independently and consistently — same word, same trigger, same result in each.

- [ ] **Step 5: Commit**

```bash
git add src/ax.h src/ax.c
git commit -m "feat: detect z-trigger restore in Flow B (ax.c)"
```

---

## Post-implementation note

Task 1's review found that `check_restore`'s original 4-arg signature above
(label, method, keys, expected) only asserted the reconstructed string, so a
future regression that performs a real (non-no-op) restore whose net text
happens to match would still pass. The shipped version (commit 2568cb9)
added a 5th `expect_noop` bool parameter that asserts
`backspace_count == 0 && insert_len == 0` directly on the raw
`vn_engine_result` for the "hello"/"quiz" no-op cases, in addition to the
string check. This doc's code blocks above still show the original 4-arg
version — the shipped `test/test_vn_engine.c` is the authoritative version.
