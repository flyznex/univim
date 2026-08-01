# Suppress VN processing under a system IME — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make UniVim stand down (no Vietnamese processing) whenever a composing macOS system input method (Korean/Japanese/Chinese/etc.) is the active input source, and resume automatically on return to a plain keyboard layout.

**Architecture:** A small `input_source.{h,c}` module classifies the current source via `kTISPropertyInputSourceType` (layout vs. anything else). `workspace.m` observes `kTISNotifySelectedKeyboardInputSourceChanged` (event-driven, no polling), caches one `bool g_input_source_is_ime`, and resets the vn engine on change. `vn_input_route` gains one parameter + one guard; both call sites pass the cached flag.

**Tech Stack:** C99 + Objective-C, Carbon (Text Input Sources API, already linked), Cocoa, `make`/CMake, assert-based standalone tests.

## Global Constraints

- Language/standard: C99 (`-std=c99`), compiler `clang`. Copy verbatim from makefile.
- Build defines required to compile any file including `buffer.h`→`libvim.h`: `-DHAVE_CONFIG_H -DMACOS_X -DMACOS_X_DARWIN`; includes `-Ilib -Ilib/libvim/proto -Isrc`.
- No new dependency. Carbon is already linked (`makefile:4`, `CMakeLists.txt:66`); the TIS API lives in Carbon/HIToolbox.
- Classifier keys on **type, not language**: `kTISPropertyInputSourceType == kTISTypeKeyboardLayout` ⇒ NOT an IME (UniVim active). Anything else ⇒ IME (suppress). If `TISCopyCurrentKeyboardInputSource()` returns NULL ⇒ return `false` (don't suppress — preserve pre-feature behavior).
- Suppress-on-doubt bias: only a confirmed `kTISTypeKeyboardLayout` returns "not IME".
- Detection is event-driven (observe the notification); the TIS query runs only in the observer callback and once at startup — never per keystroke.
- Menubar is NOT changed by this feature.
- Full build check: `make` from repo root must succeed under `-Werror` (makefile line 10).
- Cached flag default is `false` (UniVim active) until first computed.

---

## File Structure

- `src/input_source.h` / `src/input_source.c` — NEW. One public function `bool input_source_is_composing_ime(void)`. Self-contained; depends only on Carbon. Isolates the one piece needing live TIS validation.
- `src/workspace.m` — MODIFY. Add a distributed-notification observer for input-source changes, startup computation, engine reset on change; write the cached flag. (Observer teardown already present at line 45.)
- `src/vn_input.h` / `src/vn_input.c` — MODIFY. Add `input_source_is_ime` parameter + guard to `vn_input_route`; declare `extern bool g_input_source_is_ime;` (header) and DEFINE it (`vn_input.c`, default `false`).
- `src/event_tap.c` — MODIFY. Pass `g_input_source_is_ime` at the `vn_input_route` call (line 149).
- `src/ax.c` — MODIFY. Pass `g_input_source_is_ime` at the `vn_input_route` call (line 301).
- `test/test_vn_input.c` — MODIFY. Update all `vn_input_route` calls for the new param; add IME-guard assertions.
- `makefile` — MODIFY. Add `input_source.o` to `_OBJ`.
- `CMakeLists.txt` — MODIFY. Add `src/input_source.c` to `SOURCES`.

---

## Task 1: Input-source classifier module

**Files:**
- Create: `src/input_source.h`, `src/input_source.c`
- Modify: `makefile` (line 15 `_OBJ`), `CMakeLists.txt` (SOURCES list, ends line 52)

**Interfaces:**
- Consumes: Carbon TIS API.
- Produces: `bool input_source_is_composing_ime(void);`

- [ ] **Step 1: Create the header**

Create `src/input_source.h`:

```c
#pragma once
#include <stdbool.h>

// True when the current keyboard input source is a composing IME (its
// kTISPropertyInputSourceType is anything other than kTISTypeKeyboardLayout,
// e.g. Korean/Japanese/Chinese input modes). False for plain keyboard layouts
// (US, AZERTY, QWERTZ, Vietnamese layouts) and when the current source cannot
// be determined -- in doubt about "layout", suppress; but with no source info
// at all, stay active (preserve pre-feature behavior).
//
// Reads TISCopyCurrentKeyboardInputSource(); intended to be called on
// input-source-change notifications and once at startup, NOT per keystroke.
bool input_source_is_composing_ime(void);
```

- [ ] **Step 2: Create the implementation**

Create `src/input_source.c`:

```c
#include "input_source.h"
#include <Carbon/Carbon.h>

bool input_source_is_composing_ime(void) {
  TISInputSourceRef src = TISCopyCurrentKeyboardInputSource();
  if (!src) return false; // no source info -> don't suppress

  bool is_ime = true; // default: treat unknown as IME (suppress-on-doubt)
  CFStringRef type = (CFStringRef)TISGetInputSourceProperty(src, kTISPropertyInputSourceType);
  if (type && CFStringCompare(type, kTISTypeKeyboardLayout, 0) == kCFCompareEqualTo) {
    is_ime = false; // confirmed plain keyboard layout -> UniVim active
  }
  CFRelease(src);
  return is_ime;
}
```

Note: `TISGetInputSourceProperty` returns a borrowed reference (do NOT release `type`); `TISCopyCurrentKeyboardInputSource` returns an owned reference (DO `CFRelease` `src`).

- [ ] **Step 3: Add the source to the makefile**

In `makefile` line 15, add `input_source.o` to `_OBJ`. Change:

```make
_OBJ = helpers.om helpers.o workspace.om event_tap.o ax.o buffer.o line.o env_vars.o vn_engine.o vn_input.o toast.om config_watcher.o codesign_selfheal.o
```

to (append `input_source.o`):

```make
_OBJ = helpers.om helpers.o workspace.om event_tap.o ax.o buffer.o line.o env_vars.o vn_engine.o vn_input.o toast.om config_watcher.o codesign_selfheal.o input_source.o
```

- [ ] **Step 4: Add the source to CMakeLists.txt**

In `CMakeLists.txt`, in the `SOURCES` list (currently ending with `src/config_watcher.c` and `src/codesign_selfheal.c` before the closing `)`), add `src/input_source.c`:

```cmake
    src/config_watcher.c
    src/codesign_selfheal.c
    src/input_source.c
)
```

- [ ] **Step 5: Compile-check the new module in isolation**

Run:
```bash
clang -std=c99 -Isrc -c src/input_source.c -o /tmp/input_source.o
```
Expected: compiles with no errors (Carbon resolves via the SDK; no explicit `-framework` needed for a `-c` object compile).

- [ ] **Step 6: Build the whole app**

Run: `make`
Expected: builds `bin/univim` clean under `-Werror`, now linking `input_source.o`.

- [ ] **Step 7: Commit**

```bash
git add src/input_source.h src/input_source.c makefile CMakeLists.txt
git commit -m "feat: add input_source classifier (composing IME vs keyboard layout)"
```

---

## Task 2: Add IME guard to vn_input_route (signature change + call sites + tests)

**Files:**
- Modify: `src/vn_input.h` (declaration line 48; add extern), `src/vn_input.c` (`vn_input_route` 30-36)
- Modify: `src/event_tap.c` (call at line 149), `src/ax.c` (call at line 301)
- Test: `test/test_vn_input.c` (all `vn_input_route` calls + new assertions)

**Interfaces:**
- Consumes: nothing new.
- Produces: `enum vn_flow vn_input_route(struct vn_input* vn, bool is_vn_blacklisted, bool front_app_ignored, bool input_source_is_ime, uint32_t cursor_mode);` and the global `bool g_input_source_is_ime` (DEFINED here in `vn_input.c` with default `false`; Task 3's `workspace.m` only writes to it). Defining it here — next to the function that reads it — keeps every commit building cleanly (no broken-link intermediate state).

- [ ] **Step 1: Update the unit test (RED)**

In `test/test_vn_input.c`, the routing-logic section currently calls `vn_input_route` with 4 args. Update ALL of them to 5 args (insert `input_source_is_ime` as the 4th arg, before `cursor_mode`) and add IME-guard assertions. Replace the block from the first `// VN disabled entirely` comment through the last CMDLINE assertion (currently lines 17-35) with:

```c
  // VN disabled entirely -> never routes, regardless of everything else.
  struct vn_input disabled = { .enabled = false };
  assert(vn_input_route(&disabled, false, true, false, INSERT) == VN_FLOW_NONE);
  assert(vn_input_route(&disabled, false, false, false, INSERT) == VN_FLOW_NONE);

  // App is VN-blacklisted -> never routes.
  assert(vn_input_route(&vn, true, true, false, INSERT) == VN_FLOW_NONE);
  assert(vn_input_route(&vn, true, false, false, INSERT) == VN_FLOW_NONE);

  // A composing system IME is active -> never routes, regardless of everything
  // else once enabled+not-blacklisted passes (the IME guard dominates).
  assert(vn_input_route(&vn, false, true, true, INSERT) == VN_FLOW_NONE);
  assert(vn_input_route(&vn, false, false, true, INSERT) == VN_FLOW_NONE);
  assert(vn_input_route(&vn, false, false, true, NORMAL) == VN_FLOW_NONE);

  // vim-mode blacklisted for this app (front_app_ignored) -> synthetic flow,
  // regardless of cursor mode (vim isn't tracking mode for this app at all).
  assert(vn_input_route(&vn, false, true, false, NORMAL) == VN_FLOW_SYNTHETIC);
  assert(vn_input_route(&vn, false, true, false, INSERT) == VN_FLOW_SYNTHETIC);

  // vim-mode active for this app -> only INSERT routes, to the vim buffer.
  assert(vn_input_route(&vn, false, false, false, INSERT) == VN_FLOW_VIM_BUFFER);
  assert(vn_input_route(&vn, false, false, false, NORMAL) == VN_FLOW_NONE);
  assert(vn_input_route(&vn, false, false, false, VISUAL) == VN_FLOW_NONE);
  assert(vn_input_route(&vn, false, false, false, CMDLINE) == VN_FLOW_NONE);
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
clang -std=c99 -DHAVE_CONFIG_H -DMACOS_X -DMACOS_X_DARWIN -Wno-return-type -Ilib -Ilib/libvim/proto -Isrc test/test_vn_input.c src/vn_input.c src/helpers.c src/helpers.m src/vn_engine.c src/env_vars.c src/toast.m lib/libvim.a lib/libunikey.a -lm -lncurses -liconv -lc++ -framework Carbon -framework Cocoa -o /tmp/test_vn_input && /tmp/test_vn_input
```
Expected: FAIL — compile error, too many arguments to `vn_input_route` (signature still 4-param).

- [ ] **Step 3: Update the header declaration**

In `src/vn_input.h`, change the `vn_input_route` declaration (line 48) to add the parameter, and add the extern for the cached flag. Replace:

```c
enum vn_flow vn_input_route(struct vn_input* vn, bool is_vn_blacklisted,
                            bool front_app_ignored, uint32_t cursor_mode);
```

with:

```c
enum vn_flow vn_input_route(struct vn_input* vn, bool is_vn_blacklisted,
                            bool front_app_ignored, bool input_source_is_ime,
                            uint32_t cursor_mode);
// Cached "is the current system input source a composing IME?" flag.
// Defined in workspace.m, updated on input-source-change notifications.
extern bool g_input_source_is_ime;
```

- [ ] **Step 4: Update the implementation**

In `src/vn_input.c`, replace `vn_input_route` (lines 30-36) with the updated function AND the global definition just above it. The global lives next to the reader `g_vn_input` (line 8). First add, right after `char g_vn_debug_app_name[256] = "";` (line 9):

```c
bool g_input_source_is_ime = false; // updated by workspace.m on input-source changes
```

Then replace `vn_input_route` (lines 30-36) with:

```c
enum vn_flow vn_input_route(struct vn_input* vn, bool is_vn_blacklisted,
                            bool front_app_ignored, bool input_source_is_ime,
                            uint32_t cursor_mode) {
  if (!vn->enabled || is_vn_blacklisted) return VN_FLOW_NONE;
  if (input_source_is_ime) return VN_FLOW_NONE; // a composing system IME owns the keystroke
  if (front_app_ignored) return VN_FLOW_SYNTHETIC;
  if (cursor_mode & INSERT) return VN_FLOW_VIM_BUFFER;
  return VN_FLOW_NONE;
}
```

- [ ] **Step 5: Run the test to verify it passes (GREEN)**

Run the same command as Step 2.
Expected: PASS — `[parse_hotkey regular-key combos] OK`, `[vim_status_label] OK`, `ALL TESTS PASSED`.

- [ ] **Step 6: Update the event_tap.c call site**

In `src/event_tap.c`, the call at line 149 is:

```c
  enum vn_flow flow = vn_input_route(&g_vn_input, event_tap->vn_ignored, true, 0);
```

Change it to pass `g_input_source_is_ime` as the new 4th arg:

```c
  enum vn_flow flow = vn_input_route(&g_vn_input, event_tap->vn_ignored, true,
                                     g_input_source_is_ime, 0);
```

- [ ] **Step 7: Update the ax.c call site**

In `src/ax.c`, the call at lines 301-303 is:

```c
    enum vn_flow flow = vn_input_route(&g_vn_input, g_event_tap.vn_ignored,
                                       g_event_tap.front_app_ignored,
                                       ax->buffer.cursor.mode           );
```

Change it to insert `g_input_source_is_ime` as the 4th arg:

```c
    enum vn_flow flow = vn_input_route(&g_vn_input, g_event_tap.vn_ignored,
                                       g_event_tap.front_app_ignored,
                                       g_input_source_is_ime,
                                       ax->buffer.cursor.mode           );
```

- [ ] **Step 8: Build the whole app (must be clean)**

Run: `make`
Expected: builds `bin/univim` clean under `-Werror`. The global `g_input_source_is_ime` is defined in `vn_input.c` (Step 4), so both call sites link. It is only ever `false` until Task 3 wires the observer — the feature is inert but the build is correct.

- [ ] **Step 9: Commit**

```bash
git add src/vn_input.h src/vn_input.c src/event_tap.c src/ax.c test/test_vn_input.c
git commit -m "feat: gate vn_input_route on composing-IME flag"
```

---

## Task 3: Observer + cached flag in workspace.m

**Files:**
- Modify: `src/workspace.m` (define flag; observer registration in `init`; callback method; teardown already present at line 45)

**Interfaces:**
- Consumes: `input_source_is_composing_ime()` (Task 1); `vn_engine_reset()` (existing, used at workspace.m:72); `extern bool g_input_source_is_ime;` (declared in vn_input.h, DEFINED in vn_input.c — Task 2).
- Produces: runtime behavior only — the observer that actually updates `g_input_source_is_ime`.

- [ ] **Step 1: Add the include**

In `src/workspace.m`, add the include after the existing includes (after line 4 `#include "vn_input.h"`):

```c
#include "input_source.h"
```

The global `g_input_source_is_ime` is already DEFINED in `vn_input.c` (Task 2) and declared `extern` in `vn_input.h` (already included here at line 4), so this file only reads/writes it — do NOT define it again here (that would be a duplicate-symbol link error).

- [ ] **Step 2: Register the observer and compute the startup value**

In `src/workspace.m`, in `- (id)init`, inside the `if ((self = [super init]))` block, after the existing NSWorkspace observer registration (after line 19, before the frontmost-resolution comment at line 21), add:

```c
        // Input-source changes are event-driven (no polling): the system posts
        // this distributed notification whenever the selected keyboard input
        // source changes -- including layout switches that happen without an
        // app switch. The callback recomputes the cached IME flag.
        [[NSDistributedNotificationCenter defaultCenter] addObserver:self
                selector:@selector(inputSourceChanged:)
                name:(NSString*)kTISNotifySelectedKeyboardInputSourceChanged
                object:nil];

        // Compute once for whatever source is already active at startup, before
        // any switch notification arrives.
        g_input_source_is_ime = input_source_is_composing_ime();
```

Note: `kTISNotifySelectedKeyboardInputSourceChanged` is a `CFStringRef` from Carbon; cast to `NSString*` for the Cocoa notification API. `workspace.m` already includes Cocoa via `workspace.h`; add `#import <Carbon/Carbon.h>` at the top if `kTISNotifySelectedKeyboardInputSourceChanged` is not resolved (check: the notification constant lives in Carbon/HIToolbox). Add the Carbon import alongside the other includes if needed.

- [ ] **Step 3: Add the callback method**

In `src/workspace.m`, add a new method inside the `@implementation` block, after `appSwitched:` (after line 74, before `@end`):

```c
- (void)inputSourceChanged:(NSNotification *)notification {
    g_input_source_is_ime = input_source_is_composing_ime();
    // A source switch breaks word context; drop any half-composed Vietnamese
    // word so it doesn't leak across the switch (mirrors appSwitched's reset).
    vn_engine_reset();
    vn_debug_log("inputSourceChanged: g_input_source_is_ime=%d", g_input_source_is_ime);
}
```

`vn_engine_reset` and `vn_debug_log` are already reachable in this file (used in `appSwitched:` at lines 71-72). If `vn_engine_reset`'s declaration is not visible, it is declared in the vn_engine header already pulled in transitively via existing includes — verify it compiles; if not, add `#include "vn_engine.h"`.

- [ ] **Step 4: Confirm teardown**

Verify `- (dealloc)` already removes the distributed observer. It does (line 45: `[[NSDistributedNotificationCenter defaultCenter] removeObserver:self];`). No change needed. Confirm this in your report.

- [ ] **Step 5: Build the whole app**

Run: `make`
Expected: builds `bin/univim` clean under `-Werror`. The observer now updates `g_input_source_is_ime` at runtime.

- [ ] **Step 6: Regression — run the unit test**

Run the Task 2 Step 2 command.
Expected: PASS — `ALL TESTS PASSED`.

- [ ] **Step 7: CMake build check (was fixed earlier; confirm still clean)**

Run:
```bash
cmake -S . -B build >/dev/null && cmake --build build 2>&1 | tail -3
```
Expected: `Built target univim`, no errors.

- [ ] **Step 8: Commit**

```bash
git add src/workspace.m
git commit -m "feat: observe input-source changes, cache IME flag, reset engine on switch"
```

---

## Task 4: Live verification (manual — user runs; needs multiple input sources + permissions)

**Files:** none (runtime verification of the assembled feature).

Requires a real machine with several input sources installed and Accessibility granted. Cannot be automated.

- [ ] **Step 1: Install input sources**

In System Settings → Keyboard → Input Sources, add: a Korean IME (e.g. "2-Set Korean"), ideally a Japanese IME and a Chinese Pinyin IME, and at least one non-US Latin layout (French AZERTY or a Vietnamese layout).

- [ ] **Step 2: Build and run**

Run `make`, then run `bin/univim` (grant Accessibility if prompted). Enable UniVim VI mode.

- [ ] **Step 3: Verify suppression under Korean**

Select the Korean input source. In a text field, type: Hangul composes cleanly, with NO Vietnamese interference and NO stray backspaces from UniVim.

- [ ] **Step 4: Verify resume without an app switch**

Without switching apps, switch the input source back to U.S. (or ABC). Type Vietnamese Telex (e.g. `tieengs` → `tiếng`): Vietnamese processing works again immediately. (Confirms the observer fires on a layout switch that involves no app switch.)

- [ ] **Step 5: Verify layouts are NOT suppressed**

Switch to French AZERTY (or the Vietnamese layout). Confirm Vietnamese processing STILL works (these are `kTISTypeKeyboardLayout`, not IMEs). This distinguishes type-based from language-based filtering.

- [ ] **Step 6: Verify Japanese/Chinese too (if installed)**

Repeat Step 3 with Japanese and Chinese Pinyin: UniVim suppressed, their composition clean.

- [ ] **Step 7: Verify no leak across switch**

Start typing a partial Vietnamese word on a Latin layout, then switch source mid-word: the half-composed word must not leak or corrupt the new source's input (engine reset on change).

---

## Self-Review Notes

- **Spec coverage:** classifier module + type-not-language + suppress-on-doubt + NULL→false (Task 1); routing guard as a parameter, both call sites, unit tests (Task 2); event-driven observer in workspace.m, cached bool, startup compute, engine reset on change, teardown (Task 3); live verification incl. layout-not-suppressed and no-app-switch cases (Task 4); menubar untouched (no task changes it); no new dependency (Carbon reused); makefile + CMake source lists updated (Task 1).
- **Every commit builds:** the global `g_input_source_is_ime` is defined in `vn_input.c` (Task 2) so both call sites link immediately; Task 3's `workspace.m` only writes to it. No broken-link intermediate state; each task ends with a clean `make`.
- **Type consistency:** `vn_input_route(struct vn_input*, bool, bool, bool, uint32_t)` and `g_input_source_is_ime` (bool) and `input_source_is_composing_ime(void)->bool` used identically across Tasks 1-3 and the tests.
- **Memory correctness:** `TISCopyCurrentKeyboardInputSource` owned (CFRelease); `TISGetInputSourceProperty` borrowed (no release) — stated in Task 1 Step 2.
- **Compile command:** the unit-test command is the same verified one used by the prior feature (needs helpers.m/toast.m/env_vars.c/vn_engine.c + libs because vn_input.c pulls buffer.h→libvim.h).
