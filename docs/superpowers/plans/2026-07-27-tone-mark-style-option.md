# Modern/Old Tone-Mark Placement Option Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `modern_style` `vn_config` key that switches libunikey's existing modern-vs-old Vietnamese tone-mark placement rule (`UnikeyOptions.modernStyle`), defaulting to modern style (`hoà`) instead of libunikey's built-in old-style default (`hòa`).

**Architecture:** Pure wiring, no changes inside the vendored `libunikey` submodule. `src/vn_engine.c` gets one new setter (`vn_engine_set_tone_style`) that reads/writes `UnikeyOptions.modernStyle` via libunikey's existing `UnikeyGetOptions`/`UnikeySetOptions`. `src/vn_input.c` gets a new `modern_style` field parsed from `vn_config`, applied at startup and on config reload — mirroring the existing `method=` plumbing exactly.

**Tech Stack:** C99, existing `libunikey` C API (prebuilt at `lib/libunikey.a` / `lib/libunikey/*.h`, headers confirmed identical to the `libunikey` submodule's `src/keycons.h` and `src/unikey.h`).

## Global Constraints

- No changes inside the `libunikey` submodule or its prebuilt `lib/libunikey.a`/`lib/libunikey/*.h` — this feature uses only the already-exposed `UnikeyOptions.modernStyle` field and `UnikeyGetOptions`/`UnikeySetOptions` functions.
- `modern_style` accepts `1`/`0`/`on`/`off`, matching the existing `debug=` key's convention in `vn_config`.
- Default is modern style (`true`) when the key is absent — a deliberate behavior change from libunikey's own old-style default, per the approved spec (`docs/superpowers/specs/2026-07-27-tone-mark-style-option-design.md`).
- One global setting — no per-app override (unlike `vn_overrides`), no per-input-method variation.
- Follow the exact `vn_config_load` code shape already used for `method`/`hotkey`/`debug` (default var → parse branch → apply-on-success block) — don't introduce a different parsing style for this one key.

---

### Task 1: `vn_engine_set_tone_style` — engine-level wiring

**Files:**
- Modify: `src/vn_engine.h`
- Modify: `src/vn_engine.c`
- Test: `test/test_vn_engine.c`

**Interfaces:**
- Consumes: libunikey's `UnikeyOptions` struct (field `int modernStyle`), `UnikeyGetOptions(UnikeyOptions*)`, `UnikeySetOptions(UnikeyOptions*)` — all declared in `libunikey/unikey.h`, already included by `vn_engine.c`.
- Produces: `void vn_engine_set_tone_style(bool modern)` — later consumed by Task 2's `vn_input.c` changes.

- [ ] **Step 1: Write the failing test**

Add this block to `test/test_vn_engine.c`, right after the existing `check("simpletelex default method", ...)` call (currently line 65) and before the macro-expansion test block (currently starting at line 71):

```c
  // modern vs. old tone-mark placement (vn_config's `modern_style=`) --
  // libunikey defaults to old style (modernStyle=0); vn_engine_set_tone_style
  // must actually flip UnikeyOptions.modernStyle, not silently no-op.
  {
    vn_engine_set_method(VN_METHOD_TELEX);

    vn_engine_set_tone_style(false);
    vn_engine_reset();
    char out_old[64];
    type_sequence("hoaf", out_old, sizeof(out_old));
    printf("[tone style old] keys=\"hoaf\" got=\"%s\" want=\"h\xc3\xb2" "a\" %s\n",
           out_old, strcmp(out_old, "h\xc3\xb2" "a") == 0 ? "OK" : "MISMATCH");
    assert(strcmp(out_old, "h\xc3\xb2" "a") == 0); // "hòa"

    vn_engine_set_tone_style(true);
    vn_engine_reset();
    char out_modern[64];
    type_sequence("hoaf", out_modern, sizeof(out_modern));
    printf("[tone style modern] keys=\"hoaf\" got=\"%s\" want=\"ho\xc3\xa0\" %s\n",
           out_modern, strcmp(out_modern, "ho\xc3\xa0") == 0 ? "OK" : "MISMATCH");
    assert(strcmp(out_modern, "ho\xc3\xa0") == 0); // "hoà"

    // toggling back to old must also work, not just the first switch
    vn_engine_set_tone_style(false);
    vn_engine_reset();
    char out_old2[64];
    type_sequence("hoaf", out_old2, sizeof(out_old2));
    assert(strcmp(out_old2, "h\xc3\xb2" "a") == 0); // "hòa"
  }
```

(The expected byte sequences were confirmed empirically: typing Telex `hoaf` with `modernStyle=0` produces `h` `0xC3 0xB2` `a` — "hòa" — and with `modernStyle=1` produces `h` `o` `0xC3 0xA0` — "hoà".)

- [ ] **Step 2: Run test to verify it fails (compile error — function doesn't exist yet)**

Run: `clang -std=c99 -Ilib -o /tmp/test_vn_engine test/test_vn_engine.c src/vn_engine.c lib/libunikey.a -lc++`
Expected: FAIL — `error: call to undeclared function 'vn_engine_set_tone_style'`

- [ ] **Step 3: Declare the function**

In `src/vn_engine.h`, add this line right after the existing `void vn_engine_set_method(vn_method method);` (currently line 13):

```c
void vn_engine_set_tone_style(bool modern);
```

- [ ] **Step 4: Implement the function**

In `src/vn_engine.c`, add this function right after `vn_engine_set_method` (currently ending at line 66, right before the `bytes_to_char_count` comment block):

```c
// libunikey's own CreateDefaultUnikeyOptions() defaults modernStyle to 0
// (old style, e.g. "hòa"); svim defaults to modern style (e.g. "hoà")
// instead via vn_config's `modern_style=`, so callers apply this after
// vn_engine_init to override libunikey's built-in default.
void vn_engine_set_tone_style(bool modern) {
  UnikeyOptions opt;
  UnikeyGetOptions(&opt);
  opt.modernStyle = modern ? 1 : 0;
  UnikeySetOptions(&opt);
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `clang -std=c99 -Ilib -o /tmp/test_vn_engine test/test_vn_engine.c src/vn_engine.c lib/libunikey.a -lc++ && /tmp/test_vn_engine`
Expected: every line ends `OK`, final line `ALL TESTS PASSED`

- [ ] **Step 6: Commit**

```bash
git add src/vn_engine.h src/vn_engine.c test/test_vn_engine.c
git commit -m "feat: add vn_engine_set_tone_style for modern/old tone-mark placement"
```

---

### Task 2: `modern_style` config key — `vn_input.c` wiring + docs

**Files:**
- Modify: `src/vn_input.h`
- Modify: `src/vn_input.c`
- Modify: `README.md`

**Interfaces:**
- Consumes: `void vn_engine_set_tone_style(bool modern)` from Task 1.
- Produces: `struct vn_input.modern_style` (bool) — read by nothing outside this task; exists so a future caller/test can inspect the parsed value.

- [ ] **Step 1: Add the field to `struct vn_input`**

In `src/vn_input.h`, add `bool modern_style;` right after `vn_method method;` (currently line 26):

```c
struct vn_input {
  bool enabled;
  bool debug;
  char** blacklist;
  uint32_t blacklist_count;
  vn_method method;
  bool modern_style;
  CGEventFlags hotkey_mask;
  struct vn_override* overrides;
  uint32_t overrides_count;
};
```

- [ ] **Step 2: Parse `modern_style` in `vn_config_load`**

In `src/vn_input.c`, in `vn_config_load` (currently starting line 99):

Add a new default variable next to the existing ones (currently lines 111-113):

```c
  vn_method new_method = is_reload ? vn->method : VN_METHOD_SIMPLETELEX;
  CGEventFlags new_hotkey = is_reload ? vn->hotkey_mask : (kCGEventFlagMaskControl | kCGEventFlagMaskShift);
  bool new_debug = is_reload ? vn->debug : false;
  bool new_modern_style = is_reload ? vn->modern_style : true;
```

Add a new parse branch, in the `if/else if` chain (currently the `debug` branch ends at line 170, right before the final `else` that handles unknown keys at line 171):

```c
    } else if (strcmp(key, "modern_style") == 0) {
      if (strcmp(value, "1") == 0 || strcmp(value, "on") == 0) new_modern_style = true;
      else if (strcmp(value, "0") == 0 || strcmp(value, "off") == 0) new_modern_style = false;
      else {
        if (error_count < 3) {
          char err[128];
          snprintf(err, sizeof(err), "line %d: invalid modern_style value '%s'\n", line_num, value);
          strncat(errors, err, sizeof(errors) - strlen(errors) - 1);
        }
        error_count++;
      }
    } else {
```

(This replaces the existing bare `} else {` that currently starts the unknown-key branch at line 171 — the new branch slots in immediately before it.)

Add the corresponding apply line next to the others (currently lines 183-185):

```c
  vn->method = new_method;
  vn->hotkey_mask = new_hotkey;
  vn->debug = new_debug;
  vn->modern_style = new_modern_style;
```

- [ ] **Step 3: Apply the setting at startup**

In `src/vn_input.c`, in `vn_input_begin` (currently starting line 253), right after `vn_engine_init(vn->method);` (currently line 275):

```c
  vn_engine_init(vn->method);
  vn_engine_set_tone_style(vn->modern_style);
```

- [ ] **Step 4: Apply the setting on reload**

In `src/vn_input.c`, in `vn_input_reload_config` (currently starting line 285):

```c
void vn_input_reload_config(struct vn_input* vn) {
  vn_method old_method = vn->method;
  bool old_modern_style = vn->modern_style;
  char* config_error = vn_config_load(vn, true);
  if (config_error) {
    notify_config_error(config_error);
    vn_debug_log("vn_input_reload_config: %s", config_error);
    free(config_error);
  }

  // Update engine method if changed
  if (vn->method != old_method) {
    vn_engine_set_method(vn->method);
  }
  if (vn->modern_style != old_modern_style) {
    vn_engine_set_tone_style(vn->modern_style);
  }
}
```

- [ ] **Step 5: Document the config key**

In `README.md`, update the `vn_config` example block (currently lines 41-45):

```
method=simpletelex  # telex, simpletelex (default), or vni
hotkey=control+shift
debug=1             # logs routing/correction decisions to ~/.config/univim/vn_debug.log
modern_style=1      # 1/on (default, e.g. "hoà") or 0/off (old style, e.g. "hòa")
```

- [ ] **Step 6: Build the full project and confirm no regressions**

`vn_input.c` transitively includes `lib/libvim/libvim.h` (via `buffer.h`) and
calls into `env_vars.c`/`toast.m`/`vn_engine.c`, so — unlike Task 1's
self-contained `vn_engine.c` — there's no small standalone link line for it;
use the project's actual build instead:

Run: `make clean && make`
Expected: builds `bin/univim` with no errors or warnings (the Makefile's `WARN_FLAGS` includes `-Werror`).

Run: `clang -std=c99 -Ilib -o /tmp/test_vn_engine test/test_vn_engine.c src/vn_engine.c lib/libunikey.a -lc++ && /tmp/test_vn_engine`
Expected: `ALL TESTS PASSED` (re-confirms Task 1's engine wiring still works once `vn_input.c` also calls it).

- [ ] **Step 7: Commit**

```bash
git add src/vn_input.h src/vn_input.c README.md
git commit -m "feat: expose modern/old tone-mark placement as vn_config's modern_style"
```
