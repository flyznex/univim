# Per-app VN correction tuning (delay + strategy) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let users override, per frontmost app, how `vn_post_correction` posts a
Telex/VNI correction — the delay after posting, and whether it deletes via
`Delete` keystrokes (backspace strategy) or via `Shift+Left` selection
followed by a replacing insert (select strategy) — through a new
`~/.config/svim/vn_overrides` config file.

**Architecture:** A new parser (`load_vn_overrides`, in `vn_input.c`) reads
`~/.config/svim/vn_overrides` into a `struct vn_override_list` at startup,
stored on `g_vn_input`. On every app switch, `workspace.m` resolves the
current app's delay/strategy via a new `vn_input_lookup_override` and caches
them on `g_event_tap` (same lifecycle as the existing `front_pid` cache).
`vn_post_correction` takes a new `struct vn_post_target {pid, delay_us,
strategy}` instead of a bare pid, and its backspace loop sends `Shift+Left`
instead of `Delete` when `strategy == VN_STRATEGY_SELECT`; the delay at the
end only runs for the backspace strategy.

**Tech Stack:** C (macOS, `-std=c99`), Core Graphics event APIs
(`CGEventTap`/`CGEventPost`/`CGEventPostToPid`), Carbon virtual keycodes.

## Global Constraints

- Default delay stays `5ms` (`5000` microseconds) and default strategy stays
  `backspace` for any app with no matching row — this feature is additive,
  it must not change behavior for apps not listed in the config.
- No AX role/subrole auto-detection — overrides are only ever looked up by
  app name or bundle id, declared by the user, matching how
  `~/.config/svim/vn_blacklist` already works.
- No hot-reload — `~/.config/svim/vn_overrides` is read once at startup in
  `vn_input_begin`, same as every other svim config file today.
- `select` strategy always skips the end-of-correction delay, regardless of
  its configured `delay_ms` value (which is still present in the row but
  unused) — explicit product decision, not yet empirically validated as
  universally safe.

---

## File Structure

- `src/vn_input.h` / `src/vn_input.c` — add `enum vn_correction_strategy`,
  `struct vn_override`, `struct vn_override_list`, `load_vn_overrides()`
  (Task 1), then `vn_input_lookup_override()` and the `struct vn_input`
  fields + `vn_input_begin` wiring (Task 2).
- `src/event_tap.h` / `src/event_tap.c` — add `struct vn_post_target`, change
  `vn_post_correction`'s signature and posting logic (Task 3), then add the
  `delay_us`/`strategy` fields to `struct event_tap` (Task 4).
- `src/ax.c` — update Flow B's `vn_post_correction` call site (Tasks 3 & 4).
- `src/workspace.m` — resolve the override into `g_event_tap` on app switch
  (Task 4).
- `test/test_vn_overrides.c` — new, standalone assert-based test (Tasks 1 & 2).
- `examples/vn_overrides` — new example config file (Task 5).

---

### Task 1: Parse `vn_overrides` rows (`load_vn_overrides`)

**Files:**
- Modify: `src/vn_input.h`
- Modify: `src/vn_input.c`
- Test: `test/test_vn_overrides.c` (new)

**Interfaces:**
- Produces: `enum vn_correction_strategy { VN_STRATEGY_BACKSPACE,
  VN_STRATEGY_SELECT }`; `struct vn_override { char* app; int delay_us; enum
  vn_correction_strategy strategy; }`; `struct vn_override_list { struct
  vn_override* items; uint32_t count; }`; `struct vn_override_list
  load_vn_overrides(const char* path)`.

- [ ] **Step 1: Write the failing test**

Create `test/test_vn_overrides.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/vn_input.h"

int main(void) {
  const char* path = "/tmp/test_vn_overrides.txt";
  FILE* f = fopen(path, "w");
  fprintf(f, "# a comment\n");
  fprintf(f, "\n");
  fprintf(f, "Ghostty 15 backspace\n");
  fprintf(f, "Visual Studio Code 0 select\n");
  fprintf(f, "BadRow onlytwo\n");
  fprintf(f, "AnotherBad 10 bogus\n");
  fprintf(f, "NegativeDelay -5 backspace\n");
  fclose(f);

  struct vn_override_list list = load_vn_overrides(path);
  assert(list.count == 2);

  assert(strcmp(list.items[0].app, "Ghostty") == 0);
  assert(list.items[0].delay_us == 15000);
  assert(list.items[0].strategy == VN_STRATEGY_BACKSPACE);

  assert(strcmp(list.items[1].app, "Visual Studio Code") == 0);
  assert(list.items[1].delay_us == 0);
  assert(list.items[1].strategy == VN_STRATEGY_SELECT);

  struct vn_override_list missing = load_vn_overrides("/tmp/does_not_exist_xyz.txt");
  assert(missing.count == 0);
  assert(missing.items == NULL);

  printf("ALL TESTS PASSED\n");
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails to compile**

Run (from repo root):
```bash
clang -std=c99 -DHAVE_CONFIG_H -DMACOS_X -DMACOS_X_DARWIN -Ilib -Ilib/libvim/proto -Isrc \
  test/test_vn_overrides.c src/vn_input.c src/helpers.c src/helpers.m src/env_vars.c src/vn_engine.c \
  lib/libunikey.a lib/libvim.a -lm -lncurses -liconv -lc++ -framework Carbon -framework Cocoa \
  -o /tmp/test_vn_overrides
```
Expected: FAIL — `error: use of undeclared identifier 'load_vn_overrides'` (or
`'struct vn_override_list'` undeclared) since neither exists yet.

- [ ] **Step 3: Add the types and declaration to `vn_input.h`**

In `src/vn_input.h`, change:
```c
enum vn_flow { VN_FLOW_NONE, VN_FLOW_SYNTHETIC, VN_FLOW_VIM_BUFFER };

struct vn_input {
```
to:
```c
enum vn_flow { VN_FLOW_NONE, VN_FLOW_SYNTHETIC, VN_FLOW_VIM_BUFFER };
enum vn_correction_strategy { VN_STRATEGY_BACKSPACE, VN_STRATEGY_SELECT };

struct vn_override {
  char* app;
  int delay_us;
  enum vn_correction_strategy strategy;
};

struct vn_override_list {
  struct vn_override* items;
  uint32_t count;
};

struct vn_input {
```

And change:
```c
void vn_input_begin(struct vn_input* vn);
bool vn_input_blacklisted(struct vn_input* vn, char* app, char* bundle_id);
```
to:
```c
void vn_input_begin(struct vn_input* vn);
bool vn_input_blacklisted(struct vn_input* vn, char* app, char* bundle_id);
struct vn_override_list load_vn_overrides(const char* path);
```

- [ ] **Step 4: Implement `load_vn_overrides` in `vn_input.c`**

In `src/vn_input.c`, insert this right after `vn_config_load` (i.e. right
before `void vn_input_begin(struct vn_input* vn) {`):

```c
// Row format: "AppName delay_ms strategy" -- AppName may contain spaces
// (e.g. "Visual Studio Code"), so this parses from the *end* of the line:
// the last token is the strategy, the second-to-last is the delay, and
// everything before that (trimmed) is the app name.
static bool parse_override_line(char* line, struct vn_override* out) {
  char* last_space = strrchr(line, ' ');
  if (!last_space) return false;
  char* strategy_str = last_space + 1;
  *last_space = '\0';

  char* second_last_space = strrchr(line, ' ');
  if (!second_last_space) return false;
  char* delay_str = second_last_space + 1;
  *second_last_space = '\0';

  if (line[0] == '\0') return false;

  enum vn_correction_strategy strategy;
  if (strcmp(strategy_str, "backspace") == 0) strategy = VN_STRATEGY_BACKSPACE;
  else if (strcmp(strategy_str, "select") == 0) strategy = VN_STRATEGY_SELECT;
  else return false;

  if (delay_str[0] == '\0') return false;
  for (char* c = delay_str; *c; c++)
    if (*c < '0' || *c > '9') return false;

  out->app = string_copy(line);
  out->delay_us = atoi(delay_str) * 1000;
  out->strategy = strategy;
  return true;
}

struct vn_override_list load_vn_overrides(const char* path) {
  struct vn_override_list list = { NULL, 0 };

  FILE* file = fopen(path, "r");
  if (!file) return list;

  char line[255];
  while (fgets(line, sizeof(line), file)) {
    uint32_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
    if (len == 0) continue;
    if (line[0] == '#') continue;

    struct vn_override parsed;
    if (!parse_override_line(line, &parsed)) continue;

    list.items = realloc(list.items, sizeof(struct vn_override) * ++list.count);
    list.items[list.count - 1] = parsed;
  }
  fclose(file);
  return list;
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
clang -std=c99 -DHAVE_CONFIG_H -DMACOS_X -DMACOS_X_DARWIN -Ilib -Ilib/libvim/proto -Isrc \
  test/test_vn_overrides.c src/vn_input.c src/helpers.c src/helpers.m src/env_vars.c src/vn_engine.c \
  lib/libunikey.a lib/libvim.a -lm -lncurses -liconv -lc++ -framework Carbon -framework Cocoa \
  -o /tmp/test_vn_overrides && /tmp/test_vn_overrides
```
Expected: `ALL TESTS PASSED`

- [ ] **Step 6: Commit**

```bash
git add src/vn_input.h src/vn_input.c test/test_vn_overrides.c
git commit --no-gpg-sign -m "add load_vn_overrides: parse ~/.config/svim/vn_overrides rows"
```

---

### Task 2: `vn_input_lookup_override` + wire into `struct vn_input`

**Files:**
- Modify: `src/vn_input.h`
- Modify: `src/vn_input.c`
- Test: `test/test_vn_overrides.c`

**Interfaces:**
- Consumes: `struct vn_override_list`, `load_vn_overrides` (Task 1).
- Produces: `void vn_input_lookup_override(struct vn_input* vn, char* app,
  char* bundle_id, int* out_delay_us, enum vn_correction_strategy*
  out_strategy)` — always writes a result, defaulting to `5000` /
  `VN_STRATEGY_BACKSPACE` when no row matches. New `struct vn_input` fields:
  `struct vn_override* overrides; uint32_t overrides_count;`.

- [ ] **Step 1: Extend the test with lookup assertions**

Replace the whole contents of `test/test_vn_overrides.c` with:

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/vn_input.h"

int main(void) {
  const char* path = "/tmp/test_vn_overrides.txt";
  FILE* f = fopen(path, "w");
  fprintf(f, "# a comment\n");
  fprintf(f, "\n");
  fprintf(f, "Ghostty 15 backspace\n");
  fprintf(f, "Visual Studio Code 0 select\n");
  fprintf(f, "com.apple.Terminal 20 backspace\n");
  fprintf(f, "BadRow onlytwo\n");
  fprintf(f, "AnotherBad 10 bogus\n");
  fprintf(f, "NegativeDelay -5 backspace\n");
  fprintf(f, "Ghostty 999 select\n"); // duplicate app row -- first one should win
  fclose(f);

  struct vn_override_list list = load_vn_overrides(path);
  assert(list.count == 4);

  assert(strcmp(list.items[0].app, "Ghostty") == 0);
  assert(list.items[0].delay_us == 15000);
  assert(list.items[0].strategy == VN_STRATEGY_BACKSPACE);

  assert(strcmp(list.items[1].app, "Visual Studio Code") == 0);
  assert(list.items[1].delay_us == 0);
  assert(list.items[1].strategy == VN_STRATEGY_SELECT);

  struct vn_override_list missing = load_vn_overrides("/tmp/does_not_exist_xyz.txt");
  assert(missing.count == 0);
  assert(missing.items == NULL);

  // vn_input_lookup_override: matched by app name.
  struct vn_input vn = { .overrides = list.items, .overrides_count = list.count };
  int delay_us;
  enum vn_correction_strategy strategy;

  vn_input_lookup_override(&vn, "Ghostty", "com.mitchellh.ghostty", &delay_us, &strategy);
  assert(delay_us == 15000);
  assert(strategy == VN_STRATEGY_BACKSPACE);

  // matched by bundle id, not app name.
  vn_input_lookup_override(&vn, "Terminal", "com.apple.Terminal", &delay_us, &strategy);
  assert(delay_us == 20000);
  assert(strategy == VN_STRATEGY_BACKSPACE);

  // select strategy resolves too, even though its delay_ms was 0 in the file.
  vn_input_lookup_override(&vn, "Visual Studio Code", "com.microsoft.VSCode", &delay_us, &strategy);
  assert(delay_us == 0);
  assert(strategy == VN_STRATEGY_SELECT);

  // no matching row anywhere -> defaults.
  vn_input_lookup_override(&vn, "Some Other App", "com.example.other", &delay_us, &strategy);
  assert(delay_us == 5000);
  assert(strategy == VN_STRATEGY_BACKSPACE);

  // duplicate app rows -> the first matching row in the file wins, not the last.
  vn_input_lookup_override(&vn, "Ghostty", "com.mitchellh.ghostty", &delay_us, &strategy);
  assert(delay_us == 15000);
  assert(strategy == VN_STRATEGY_BACKSPACE);

  // empty overrides list (as if the file didn't exist) -> defaults.
  struct vn_input empty_vn = { .overrides = NULL, .overrides_count = 0 };
  vn_input_lookup_override(&empty_vn, "Anything", "com.example.anything", &delay_us, &strategy);
  assert(delay_us == 5000);
  assert(strategy == VN_STRATEGY_BACKSPACE);

  printf("ALL TESTS PASSED\n");
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails to compile**

```bash
clang -std=c99 -DHAVE_CONFIG_H -DMACOS_X -DMACOS_X_DARWIN -Ilib -Ilib/libvim/proto -Isrc \
  test/test_vn_overrides.c src/vn_input.c src/helpers.c src/helpers.m src/env_vars.c src/vn_engine.c \
  lib/libunikey.a lib/libvim.a -lm -lncurses -liconv -lc++ -framework Carbon -framework Cocoa \
  -o /tmp/test_vn_overrides
```
Expected: FAIL — `error: no member named 'overrides' in 'struct vn_input'`
(and `vn_input_lookup_override` undeclared).

- [ ] **Step 3: Add fields and declaration to `vn_input.h`**

In `src/vn_input.h`, change:
```c
struct vn_input {
  bool enabled;
  bool debug;
  char** blacklist;
  uint32_t blacklist_count;
  vn_method method;
  CGEventFlags hotkey_mask;
};
```
to:
```c
struct vn_input {
  bool enabled;
  bool debug;
  char** blacklist;
  uint32_t blacklist_count;
  vn_method method;
  CGEventFlags hotkey_mask;
  struct vn_override* overrides;
  uint32_t overrides_count;
};
```

And change:
```c
struct vn_override_list load_vn_overrides(const char* path);
```
to:
```c
struct vn_override_list load_vn_overrides(const char* path);
void vn_input_lookup_override(struct vn_input* vn, char* app, char* bundle_id,
                              int* out_delay_us, enum vn_correction_strategy* out_strategy);
```

- [ ] **Step 4: Implement `vn_input_lookup_override` and wire loading into `vn_input_begin`**

In `src/vn_input.c`, add this function right after `vn_input_blacklisted`:

```c
void vn_input_lookup_override(struct vn_input* vn, char* app, char* bundle_id,
                              int* out_delay_us, enum vn_correction_strategy* out_strategy) {
  *out_delay_us = 5000;
  *out_strategy = VN_STRATEGY_BACKSPACE;
  if (!app || !bundle_id) return;

  for (uint32_t i = 0; i < vn->overrides_count; i++) {
    if (strcmp(vn->overrides[i].app, app) == 0 || strcmp(vn->overrides[i].app, bundle_id) == 0) {
      *out_delay_us = vn->overrides[i].delay_us;
      *out_strategy = vn->overrides[i].strategy;
      return;
    }
  }
}
```

Then change `vn_input_begin`:
```c
void vn_input_begin(struct vn_input* vn) {
  vn->enabled = false;
  vn_config_load(vn);

  char* home = getenv("HOME");
  char path[512];
  snprintf(path, sizeof(path), "%s/.config/svim/vn_blacklist", home);
  struct string_list list = load_string_list(path);
  vn->blacklist = list.items;
  vn->blacklist_count = list.count;

  vn_engine_init(vn->method);
}
```
to:
```c
void vn_input_begin(struct vn_input* vn) {
  vn->enabled = false;
  vn_config_load(vn);

  char* home = getenv("HOME");
  char path[512];
  snprintf(path, sizeof(path), "%s/.config/svim/vn_blacklist", home);
  struct string_list list = load_string_list(path);
  vn->blacklist = list.items;
  vn->blacklist_count = list.count;

  char overrides_path[512];
  snprintf(overrides_path, sizeof(overrides_path), "%s/.config/svim/vn_overrides", home);
  struct vn_override_list overrides = load_vn_overrides(overrides_path);
  vn->overrides = overrides.items;
  vn->overrides_count = overrides.count;

  vn_engine_init(vn->method);
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
clang -std=c99 -DHAVE_CONFIG_H -DMACOS_X -DMACOS_X_DARWIN -Ilib -Ilib/libvim/proto -Isrc \
  test/test_vn_overrides.c src/vn_input.c src/helpers.c src/helpers.m src/env_vars.c src/vn_engine.c \
  lib/libunikey.a lib/libvim.a -lm -lncurses -liconv -lc++ -framework Carbon -framework Cocoa \
  -o /tmp/test_vn_overrides && /tmp/test_vn_overrides
```
Expected: `ALL TESTS PASSED`

- [ ] **Step 6: Full project build check**

```bash
make clean && make 2>&1 | tail -20
```
Expected: no errors (this confirms adding fields to `struct vn_input` didn't
break any other file that constructs/reads it — `event_tap.c`, `ax.c`,
`workspace.m`, `main.m`).

- [ ] **Step 7: Commit**

```bash
git add src/vn_input.h src/vn_input.c test/test_vn_overrides.c
git commit --no-gpg-sign -m "add vn_input_lookup_override, load vn_overrides at startup"
```

---

### Task 3: `struct vn_post_target` + strategy-aware `vn_post_correction`

**Files:**
- Modify: `src/event_tap.h`
- Modify: `src/event_tap.c`
- Modify: `src/ax.c`

**Interfaces:**
- Consumes: `enum vn_correction_strategy` (Task 1, via `vn_input.h`).
- Produces: `struct vn_post_target { pid_t pid; int delay_us; enum
  vn_correction_strategy strategy; }`; `vn_post_correction(struct
  vn_post_target target, int backspace_count, const unsigned char*
  insert_text, int insert_len)` (replaces the old `pid_t target_pid` first
  parameter).

No automated test for this task — it's OS event-posting behavior, matching
the existing untested boundary around `CGEventPost`/AX interaction
elsewhere in this codebase. Verified by build success plus a manual retype
check (Step 5), confirming the refactor is behavior-preserving (both call
sites are updated in this same task using hardcoded `5000`/`VN_STRATEGY_BACKSPACE`
literals — i.e. exactly today's behavior — so there is nothing new to
functionally verify yet; Task 4 is where real per-app values get wired in).

- [ ] **Step 1: Add `struct vn_post_target` to `event_tap.h` and change the `vn_post_correction` declaration**

In `src/event_tap.h`, change:
```c
#pragma once
#include <stdbool.h>
#include <Carbon/Carbon.h>
#include <stdint.h>
#include "ax.h"
#include "buffer.h"

extern const char* get_name_for_pid(uint64_t pid);
extern char* string_copy(char* s);

struct event_tap {
```
to:
```c
#pragma once
#include <stdbool.h>
#include <Carbon/Carbon.h>
#include <stdint.h>
#include "ax.h"
#include "buffer.h"
#include "vn_input.h"

extern const char* get_name_for_pid(uint64_t pid);
extern char* string_copy(char* s);

struct vn_post_target {
  pid_t pid;
  int delay_us;
  enum vn_correction_strategy strategy;
};

struct event_tap {
```

And change:
```c
void vn_post_correction(pid_t target_pid, int backspace_count, const unsigned char* insert_text, int insert_len);
```
to:
```c
void vn_post_correction(struct vn_post_target target, int backspace_count, const unsigned char* insert_text, int insert_len);
```

- [ ] **Step 2: Update `vn_post_correction`'s implementation in `event_tap.c`**

Change:
```c
void vn_post_correction(pid_t target_pid, int backspace_count, const unsigned char* insert_text, int insert_len) {
  vn_debug_log("vn_post_correction: pid=%d backspaces=%d insert_len=%d", target_pid, backspace_count, insert_len);

  CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);

  for (int i = 0; i < backspace_count; i++) {
    CGEventRef down = CGEventCreateKeyboardEvent(source, kVK_Delete, true);
    CGEventRef up   = CGEventCreateKeyboardEvent(source, kVK_Delete, false);
    CGEventSetIntegerValueField(down, kCGEventSourceUserData, VN_SYNTH_TAG);
    CGEventSetIntegerValueField(up, kCGEventSourceUserData, VN_SYNTH_TAG);
    vn_post_event(target_pid, down);
    vn_post_event(target_pid, up);
    CFRelease(down);
    CFRelease(up);
    vn_debug_log("vn_post_correction: posted backspace %d/%d", i + 1, backspace_count);
  }

  if (insert_len > 0) {
    CFStringRef str = CFStringCreateWithBytes(NULL, insert_text, insert_len,
                                              kCFStringEncodingUTF8, false);
    if (str) {
      CFIndex length = CFStringGetLength(str);
      UniChar chars[length];
      CFStringGetCharacters(str, CFRangeMake(0, length), chars);

      CGEventRef down = CGEventCreateKeyboardEvent(source, 0, true);
      CGEventRef up   = CGEventCreateKeyboardEvent(source, 0, false);
      CGEventKeyboardSetUnicodeString(down, length, chars);
      CGEventSetIntegerValueField(down, kCGEventSourceUserData, VN_SYNTH_TAG);
      CGEventSetIntegerValueField(up, kCGEventSourceUserData, VN_SYNTH_TAG);
      vn_post_event(target_pid, down);
      vn_post_event(target_pid, up);
      CFRelease(down);
      CFRelease(up);
      CFRelease(str);
      vn_debug_log("vn_post_correction: posted insert unichar_len=%ld", (long) length);
    } else {
      // If this ever fires, the backspaces above already ran with nothing to
      // replace them -- a silent drop with a completely different cause
      // than event-ordering, so it needs to stand out from the timing noise.
      vn_debug_log("vn_post_correction: CFStringCreateWithBytes FAILED, insert silently dropped");
    }
  }

  // HACK: small safety margin on top of the pid-targeted delivery above --
  // still gives a slower app (Chrome re-rendering a web text area, a
  // terminal round-tripping through its PTY) a moment to actually apply the
  // correction before the next physical keystroke is dequeued from the tap.
  // Shorter than before (was 15ms) since targeted delivery does most of the
  // ordering work now; raise it if a specific app still shows drops.
  usleep(5000);

  CFRelease(source);
}
```
to:
```c
void vn_post_correction(struct vn_post_target target, int backspace_count, const unsigned char* insert_text, int insert_len) {
  vn_debug_log("vn_post_correction: pid=%d backspaces=%d insert_len=%d strategy=%d",
              target.pid, backspace_count, insert_len, target.strategy);

  CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
  bool select_strategy = target.strategy == VN_STRATEGY_SELECT;
  CGKeyCode backspace_key = select_strategy ? kVK_LeftArrow : kVK_Delete;

  for (int i = 0; i < backspace_count; i++) {
    CGEventRef down = CGEventCreateKeyboardEvent(source, backspace_key, true);
    CGEventRef up   = CGEventCreateKeyboardEvent(source, backspace_key, false);
    if (select_strategy) {
      // Select-then-replace instead of N discrete deletes: Shift+Left
      // extends a selection backward one character at a time; the Unicode
      // insert below then types over that selection, which every native
      // macOS text field treats as a single replace -- fewer discrete
      // events than N deletes + an insert, per Gõ Nhanh's approach for
      // apps that still drop characters at the default backspace strategy.
      CGEventSetFlags(down, kCGEventFlagMaskShift);
      CGEventSetFlags(up, kCGEventFlagMaskShift);
    }
    CGEventSetIntegerValueField(down, kCGEventSourceUserData, VN_SYNTH_TAG);
    CGEventSetIntegerValueField(up, kCGEventSourceUserData, VN_SYNTH_TAG);
    vn_post_event(target.pid, down);
    vn_post_event(target.pid, up);
    CFRelease(down);
    CFRelease(up);
    vn_debug_log("vn_post_correction: posted %s %d/%d",
                select_strategy ? "select" : "backspace", i + 1, backspace_count);
  }

  if (insert_len > 0) {
    CFStringRef str = CFStringCreateWithBytes(NULL, insert_text, insert_len,
                                              kCFStringEncodingUTF8, false);
    if (str) {
      CFIndex length = CFStringGetLength(str);
      UniChar chars[length];
      CFStringGetCharacters(str, CFRangeMake(0, length), chars);

      CGEventRef down = CGEventCreateKeyboardEvent(source, 0, true);
      CGEventRef up   = CGEventCreateKeyboardEvent(source, 0, false);
      CGEventKeyboardSetUnicodeString(down, length, chars);
      CGEventSetIntegerValueField(down, kCGEventSourceUserData, VN_SYNTH_TAG);
      CGEventSetIntegerValueField(up, kCGEventSourceUserData, VN_SYNTH_TAG);
      vn_post_event(target.pid, down);
      vn_post_event(target.pid, up);
      CFRelease(down);
      CFRelease(up);
      CFRelease(str);
      vn_debug_log("vn_post_correction: posted insert unichar_len=%ld", (long) length);
    } else {
      // If this ever fires, the backspaces above already ran with nothing to
      // replace them -- a silent drop with a completely different cause
      // than event-ordering, so it needs to stand out from the timing noise.
      vn_debug_log("vn_post_correction: CFStringCreateWithBytes FAILED, insert silently dropped");
    }
  }

  // HACK: small safety margin on top of the pid-targeted delivery above --
  // still gives a slower app (Chrome re-rendering a web text area, a
  // terminal round-tripping through its PTY) a moment to actually apply the
  // correction before the next physical keystroke is dequeued from the tap.
  // Select-replace always skips this -- fewer discrete events already means
  // less to race against, and it's the escape hatch for apps where even a
  // tuned backspace delay isn't enough.
  if (!select_strategy) usleep(target.delay_us);

  CFRelease(source);
}
```

- [ ] **Step 3: Update the `vn_synthetic_process` call site in `event_tap.c`**

Change:
```c
  vn_post_correction(event_tap->front_pid, result.backspace_count, result.insert_text, result.insert_len);
```
to:
```c
  struct vn_post_target target = { .pid = event_tap->front_pid, .delay_us = 5000, .strategy = VN_STRATEGY_BACKSPACE };
  vn_post_correction(target, result.backspace_count, result.insert_text, result.insert_len);
```

(The literal `5000`/`VN_STRATEGY_BACKSPACE` here are today's exact hardcoded
values, expressed through the new struct — Task 4 replaces them with the
real per-app lookup.)

- [ ] **Step 4: Update the Flow B call site in `ax.c`**

Change (around line 348):
```c
        vn_post_correction(g_event_tap.front_pid, result.backspace_count, result.insert_text, result.insert_len);
```
to:
```c
        struct vn_post_target target = { .pid = g_event_tap.front_pid, .delay_us = 5000, .strategy = VN_STRATEGY_BACKSPACE };
        vn_post_correction(target, result.backspace_count, result.insert_text, result.insert_len);
```

- [ ] **Step 5: Build and manually verify no regression**

```bash
make clean && make 2>&1 | tail -20
```
Expected: no errors.

Deploy and smoke-test exactly as earlier in this session:
```bash
rm -f /opt/homebrew/opt/svim/bin/svim && cp bin/svim /opt/homebrew/opt/svim/bin/svim && \
  chmod 555 /opt/homebrew/opt/svim/bin/svim && \
  codesign -s - --identifier com.felixkratz.svim -f /opt/homebrew/opt/svim/bin/svim && \
  brew services restart felixkratz/formulae/svim && sleep 1 && launchctl list | grep -i svim
```
Expected: `<pid> 0 homebrew.mxcl.svim` (running, exit status 0 — if it shows
`- 1` instead, re-grant Accessibility for svim in System Settings and rerun
the `brew services restart` line).

Retype a couple of the diacritic words from this session's debugging
("thấy", "lỗi") in Ghostty — should behave identically to before this task
(no config file exists yet, so every app still gets the hardcoded
`5000`/`VN_STRATEGY_BACKSPACE` from Steps 3–4).

- [ ] **Step 6: Commit**

```bash
git add src/event_tap.h src/event_tap.c src/ax.c
git commit --no-gpg-sign -m "add vn_post_target, switch vn_post_correction to strategy-aware posting"
```

---

### Task 4: Wire per-app resolution end-to-end

**Files:**
- Modify: `src/event_tap.h`
- Modify: `src/workspace.m`
- Modify: `src/event_tap.c`
- Modify: `src/ax.c`

**Interfaces:**
- Consumes: `vn_input_lookup_override` (Task 2), `struct vn_post_target`
  (Task 3).
- Produces: `struct event_tap` gains `int delay_us; enum
  vn_correction_strategy strategy;`, resolved on every app switch.

- [ ] **Step 1: Add fields to `struct event_tap`**

In `src/event_tap.h`, change:
```c
struct event_tap {
  bool front_app_ignored;
  bool vn_ignored;
  pid_t front_pid;
  uint32_t blacklist_count;
  char** blacklist;
  CFMachPortRef handle;
  CFRunLoopSourceRef runloop_source;
  CGEventMask mask;
};
```
to:
```c
struct event_tap {
  bool front_app_ignored;
  bool vn_ignored;
  pid_t front_pid;
  int delay_us;
  enum vn_correction_strategy strategy;
  uint32_t blacklist_count;
  char** blacklist;
  CFMachPortRef handle;
  CFRunLoopSourceRef runloop_source;
  CGEventMask mask;
};
```

- [ ] **Step 2: Resolve the override on every app switch in `workspace.m`**

Change:
```c
    g_event_tap.front_pid = pid;
    g_event_tap.front_app_ignored = event_tap_check_blacklist(&g_event_tap,
                                                              name,
                                                              bundle_id    );
    g_event_tap.vn_ignored = vn_input_blacklisted(&g_vn_input, name, bundle_id);
    vn_debug_log("appSwitched: vn_engine_reset (app=%s)", name ? name : "?");
```
to:
```c
    g_event_tap.front_pid = pid;
    g_event_tap.front_app_ignored = event_tap_check_blacklist(&g_event_tap,
                                                              name,
                                                              bundle_id    );
    g_event_tap.vn_ignored = vn_input_blacklisted(&g_vn_input, name, bundle_id);
    vn_input_lookup_override(&g_vn_input, name, bundle_id,
                             &g_event_tap.delay_us, &g_event_tap.strategy);
    vn_debug_log("appSwitched: vn_engine_reset (app=%s)", name ? name : "?");
```

- [ ] **Step 3: Read the resolved values at the `vn_synthetic_process` call site**

In `src/event_tap.c`, change:
```c
  struct vn_post_target target = { .pid = event_tap->front_pid, .delay_us = 5000, .strategy = VN_STRATEGY_BACKSPACE };
  vn_post_correction(target, result.backspace_count, result.insert_text, result.insert_len);
```
to:
```c
  struct vn_post_target target = {
    .pid = event_tap->front_pid,
    .delay_us = event_tap->delay_us,
    .strategy = event_tap->strategy
  };
  vn_post_correction(target, result.backspace_count, result.insert_text, result.insert_len);
```

- [ ] **Step 4: Read the resolved values at the Flow B call site**

In `src/ax.c`, change:
```c
        struct vn_post_target target = { .pid = g_event_tap.front_pid, .delay_us = 5000, .strategy = VN_STRATEGY_BACKSPACE };
        vn_post_correction(target, result.backspace_count, result.insert_text, result.insert_len);
```
to:
```c
        struct vn_post_target target = {
          .pid = g_event_tap.front_pid,
          .delay_us = g_event_tap.delay_us,
          .strategy = g_event_tap.strategy
        };
        vn_post_correction(target, result.backspace_count, result.insert_text, result.insert_len);
```

- [ ] **Step 5: Build**

```bash
make clean && make 2>&1 | tail -20
```
Expected: no errors.

- [ ] **Step 6: Manually verify the override actually takes effect**

```bash
mkdir -p ~/.config/svim
cat > ~/.config/svim/vn_overrides <<'EOF'
Ghostty 60 backspace
EOF
```

Deploy (same command as Task 3):
```bash
rm -f /opt/homebrew/opt/svim/bin/svim && cp bin/svim /opt/homebrew/opt/svim/bin/svim && \
  chmod 555 /opt/homebrew/opt/svim/bin/svim && \
  codesign -s - --identifier com.felixkratz.svim -f /opt/homebrew/opt/svim/bin/svim && \
  brew services restart felixkratz/formulae/svim && sleep 1 && launchctl list | grep -i svim
```

Switch focus away from Ghostty and back to it (to force `appSwitched:` to
fire and resolve the new override), then type a word needing a correction
(e.g. "thấy") and check `~/.config/svim/vn_debug.log` (requires `debug=1` in
`~/.config/svim/vn_config`, already used throughout this session) — the
`vn_post_correction: pid=... backspaces=... insert_len=... strategy=0` line
should appear, and the visible typing delay should feel slightly more
sluggish than before (60ms vs 5ms). Then delete the test override:
```bash
rm ~/.config/svim/vn_overrides
```

- [ ] **Step 7: Commit**

```bash
git add src/event_tap.h src/event_tap.c src/ax.c src/workspace.m
git commit --no-gpg-sign -m "resolve per-app vn_overrides on app switch, wire into vn_post_correction"
```

---

### Task 5: Example config file and final acceptance check

**Files:**
- Create: `examples/vn_overrides`

- [ ] **Step 1: Add the example file**

Create `examples/vn_overrides`:
```
# ~/.config/svim/vn_overrides
# format: AppName delay_ms strategy
# strategy: backspace | select  (delay_ms ignored when strategy=select)
#
# Uncomment and edit for an app that still drops/reorders characters at the
# default 5ms + backspace strategy:
#
# Ghostty 15 backspace
# Visual Studio Code 0 select
```

- [ ] **Step 2: Final clean rebuild**

```bash
make clean && make 2>&1 | tail -20
```
Expected: no errors.

- [ ] **Step 3: Deploy and run the full regression check**

```bash
rm -f /opt/homebrew/opt/svim/bin/svim && cp bin/svim /opt/homebrew/opt/svim/bin/svim && \
  chmod 555 /opt/homebrew/opt/svim/bin/svim && \
  codesign -s - --identifier com.felixkratz.svim -f /opt/homebrew/opt/svim/bin/svim && \
  brew services restart felixkratz/formulae/svim && sleep 1 && launchctl list | grep -i svim
```
Expected: `<pid> 0 homebrew.mxcl.svim`.

With no `~/.config/svim/vn_overrides` present, retype the stress-test words
from this session's debugging log in Ghostty — "thấy", "lỗi", "nhiều" —
several times each, confirming they still compose correctly (default
behavior unchanged by this feature).

Then add one `select`-strategy row for Ghostty specifically to validate the
new strategy end-to-end:
```bash
cat > ~/.config/svim/vn_overrides <<'EOF'
Ghostty 0 select
EOF
```
Switch focus away and back to Ghostty (to re-trigger `appSwitched:`), retype
the same words again, and confirm they still compose correctly with the
`select` strategy (watch `~/.config/svim/vn_debug.log` for `posted select
N/M` lines instead of `posted backspace N/M`). Remove the test file
afterward:
```bash
rm ~/.config/svim/vn_overrides
```

- [ ] **Step 4: Commit**

```bash
git add examples/vn_overrides
git commit --no-gpg-sign -m "add examples/vn_overrides"
```
