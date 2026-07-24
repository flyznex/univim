# Vietnamese IME Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a system-wide Telex/VNI Vietnamese input method to svim, built on the vendored `libunikey` engine, independent of vim-mode's existing per-app blacklist.

**Architecture:** A pure-C wrapper (`vn_engine.c`) calls libunikey's already-`extern "C"` API directly (no C++ shim needed — verified against the real upstream header). Two integration points reuse this same engine: `event_tap.c` applies corrections via synthetic `CGEventPost` for apps svim doesn't manage (Flow A), `ax.c`/`buffer.c` apply corrections directly into the existing vim buffer for apps in vim INSERT mode (Flow B). A shared `vn_input.c` owns enable state, its own blacklist, config, and the pure routing decision between the two flows.

**Tech Stack:** C99, Objective-C (existing files), libunikey (C++ library, C-linkage public API), CoreGraphics event tap/synthesis, CoreFoundation string conversion.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-24-vietnamese-ime-design.md` — every task below implements a section of it.
- No new UI (no NSStatusBar/menu bar item) — state broadcast only via the existing `svim.sh` hook + `VNMODE` env var.
- No per-keystroke heap allocation in the engine wrapper's hot path.
- VN/EN state always starts `false` (EN) on launch — no persistence across restarts.
- Config/blacklist changes require a service restart to take effect (matches existing `blacklist` file behavior) — no hot-reload.
- Vendoring convention: mirror `libvim/` exactly — submodule at repo root, headers + compiled `.a` committed directly under `lib/`, so a fresh clone + `make` never requires the submodule to be checked out or CMake to be installed.

---

### Task 1: Vendor libunikey

**Files:**
- Create: `.gitmodules` (append entry)
- Create (submodule): `libunikey/`
- Create (committed vendored copies): `lib/libunikey/unikey.h`, `lib/libunikey/keycons.h`, `lib/libunikey/vnconv.h`
- Create (committed build product): `lib/libunikey.a`

**Interfaces:**
- Produces: `lib/libunikey.a` (static lib, C++-compiled, C-linkage public symbols `UnikeySetup`, `UnikeySetInputMethod`, `UnikeySetCapsState`, `UnikeyFilter`, `UnikeyBackspacePress`, `UnikeyResetBuf`, `UnikeySetOutputCharset`, and globals `UnikeyBuf`, `UnikeyBackspaces`, `UnikeyBufChars`) plus `lib/libunikey/{unikey.h,keycons.h,vnconv.h}` includable via the existing `-Ilib` flag as `#include "libunikey/unikey.h"`.

- [ ] **Step 1: Add the submodule**

```bash
git submodule add https://github.com/inteplus/libunikey.git libunikey
```

- [ ] **Step 2: Build it with CMake**

```bash
cmake -S libunikey -B libunikey/build -DCMAKE_BUILD_TYPE=Release
cmake --build libunikey/build --parallel
ls libunikey/build/libunikey.a
```
Expected: `libunikey/build/libunikey.a` exists (the `CMakeLists.txt` sets `OUTPUT_NAME "unikey"` on a target named `libunikey`, which CMake resolves to `libunikey.a`).

- [ ] **Step 3: Vendor the compiled lib and public headers**

```bash
mkdir -p lib/libunikey
cp libunikey/build/libunikey.a lib/libunikey.a
cp libunikey/src/unikey.h lib/libunikey/unikey.h
cp libunikey/src/keycons.h lib/libunikey/keycons.h
cp libunikey/src/vnconv.h lib/libunikey/vnconv.h
```

- [ ] **Step 4: Verify the headers compile standalone from a C99 file**

```bash
cat > /tmp/vn_header_check.c <<'EOF'
#include "libunikey/unikey.h"
int main(void) { UnikeySetup(); UnikeyCleanup(); return 0; }
EOF
clang -std=c99 -Ilib /tmp/vn_header_check.c lib/libunikey.a -lc++ -o /tmp/vn_header_check
/tmp/vn_header_check
echo "exit: $?"
```
Expected: compiles with no errors/warnings, exits 0. (`-lc++` is required here and in the main build going forward — `libunikey.a`'s object code is compiled from C++ sources and references the C++ runtime even though its public symbols are `extern "C"`.)

- [ ] **Step 5: Commit**

```bash
git add .gitmodules libunikey lib/libunikey.a lib/libunikey/unikey.h lib/libunikey/keycons.h lib/libunikey/vnconv.h
git commit -m "vendor libunikey (Telex/VNI engine) matching libvim's vendoring pattern"
```

---

### Task 2: `vn_engine` wrapper (TDD)

**Files:**
- Create: `src/vn_engine.h`
- Create: `src/vn_engine.c`
- Test: `test/test_vn_engine.c`

**Interfaces:**
- Consumes: `lib/libunikey/unikey.h`, `lib/libunikey/vnconv.h` (Task 1).
- Produces (used by Tasks 6, 7):
  - `typedef enum { VN_METHOD_TELEX, VN_METHOD_VNI } vn_method;`
  - `struct vn_engine_result { int backspace_count; const unsigned char* insert_text; int insert_len; };`
  - `void vn_engine_init(vn_method method);`
  - `void vn_engine_set_method(vn_method method);`
  - `void vn_engine_reset(void);`
  - `struct vn_engine_result vn_engine_process_key(unsigned int ch, bool shift, bool capslock);`
  - `struct vn_engine_result vn_engine_process_backspace(void);`
  - `insert_text` points into libunikey's own static `UnikeyBuf` — valid until the next `vn_engine_process_*` call, never heap-allocated.

- [ ] **Step 1: Write the failing test**

Create `test/test_vn_engine.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/vn_engine.h"

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
    if (r.backspace_count > 0) {
      // drop the last `backspace_count` UTF-8 codepoints (all test inputs
      // are single-byte-per-codepoint ASCII up to this point, or the
      // just-inserted multi-byte sequence being replaced in the same step)
      for (int i = 0; i < r.backspace_count && len > 0; i++) {
        len--;
        while (len > 0 && (out[len] & 0xC0) == 0x80) len--;
      }
      out[len] = '\0';
    }
    if (r.insert_len > 0) {
      memcpy(out + len, r.insert_text, r.insert_len);
      len += r.insert_len;
      out[len] = '\0';
    }
  }
  (void) out_size;
}

static void check(const char* label, vn_method method, const char* keys, const char* expected) {
  vn_engine_set_method(method);
  vn_engine_reset();
  char out[256];
  type_sequence(keys, out, sizeof(out));
  printf("[%s] keys=\"%s\" got=\"%s\" want=\"%s\" %s\n",
         label, keys, out, expected, strcmp(out, expected) == 0 ? "OK" : "MISMATCH");
  assert(strcmp(out, expected) == 0);
}

int main(void) {
  vn_engine_init(VN_METHOD_TELEX);

  check("telex compound vowel", VN_METHOD_TELEX, "vieejt", "vi\xe1\xbb\x87t");   // "việt"
  check("telex dd", VN_METHOD_TELEX, "ddi", "\xc4\x91i");                        // "đi"
  check("telex word boundary", VN_METHOD_TELEX, "anh a", "anh a");               // no cross-word leak
  check("vni compound vowel", VN_METHOD_VNI, "vie65t5", "vi\xe1\xbb\x87t");      // "việt" via VNI

  printf("ALL TESTS PASSED\n");
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails (compile error — vn_engine.h doesn't exist yet)**

```bash
clang -std=c99 -Ilib -Isrc test/test_vn_engine.c src/vn_engine.c lib/libunikey.a -lc++ -o /tmp/test_vn_engine
```
Expected: FAIL — `src/vn_engine.c` and `src/vn_engine.h` not found.

- [ ] **Step 3: Write `src/vn_engine.h`**

```c
#pragma once
#include <stdbool.h>

typedef enum { VN_METHOD_TELEX, VN_METHOD_VNI } vn_method;

struct vn_engine_result {
  int backspace_count;
  const unsigned char* insert_text; // valid until the next vn_engine_process_* call
  int insert_len;
};

void vn_engine_init(vn_method method);
void vn_engine_set_method(vn_method method);
void vn_engine_reset(void);
struct vn_engine_result vn_engine_process_key(unsigned int ch, bool shift, bool capslock);
struct vn_engine_result vn_engine_process_backspace(void);
```

- [ ] **Step 4: Write `src/vn_engine.c`**

```c
#include "vn_engine.h"
#include "libunikey/unikey.h"
#include "libunikey/vnconv.h"

void vn_engine_init(vn_method method) {
  UnikeySetup();
  UnikeySetOutputCharset(CONV_CHARSET_UNIUTF8);
  vn_engine_set_method(method);
}

void vn_engine_set_method(vn_method method) {
  UnikeySetInputMethod(method == VN_METHOD_VNI ? UkVni : UkTelex);
}

void vn_engine_reset(void) {
  UnikeyResetBuf();
}

static struct vn_engine_result current_result(void) {
  struct vn_engine_result result = {
    .backspace_count = UnikeyBackspaces,
    .insert_text = UnikeyBuf,
    .insert_len = UnikeyBufChars
  };
  return result;
}

struct vn_engine_result vn_engine_process_key(unsigned int ch, bool shift, bool capslock) {
  UnikeySetCapsState(shift ? 1 : 0, capslock ? 1 : 0);
  UnikeyFilter(ch);
  return current_result();
}

struct vn_engine_result vn_engine_process_backspace(void) {
  UnikeyBackspacePress();
  return current_result();
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
clang -std=c99 -Ilib -Isrc test/test_vn_engine.c src/vn_engine.c lib/libunikey.a -lc++ -o /tmp/test_vn_engine
/tmp/test_vn_engine
```
Expected: all 4 `check(...)` lines print `OK`, then `ALL TESTS PASSED`.

If the VNI or word-boundary case doesn't match: this is a real dependency (libunikey), not a bug in our 15 lines — re-check the exact keystroke sequence against Telex/VNI rules before assuming the wrapper is wrong.

- [ ] **Step 6: Commit**

```bash
git add src/vn_engine.h src/vn_engine.c test/test_vn_engine.c
git commit -m "add vn_engine: thin C wrapper around libunikey's Telex/VNI engine"
```

---

### Task 3: Shared blacklist-loading helper (refactor)

**Files:**
- Modify: `src/helpers.h`
- Modify: `src/helpers.c` (note: repo currently has no `helpers.c` — `helpers.m` holds the equivalent Objective-C-compiled helpers; this task adds a plain `.c` file for functions that need no Objective-C runtime, per the project's existing `.c`/`.m` split)
- Modify: `src/event_tap.c:43-66` (`event_tap_load_blacklist`), `src/event_tap.c:3-13` (`event_tap_check_blacklist`)
- Test: `test/test_string_list.c`

**Interfaces:**
- Produces (used by Task 4):
  - `struct string_list { char** items; uint32_t count; };`
  - `struct string_list load_string_list(const char* path);` — returns `{NULL, 0}` if the file doesn't exist.
  - `bool blacklist_contains(char** list, uint32_t count, char* app, char* bundle_id);`

- [ ] **Step 1: Write the failing test**

Create `test/test_string_list.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/helpers.h"

int main(void) {
  const char* path = "/tmp/test_string_list.txt";
  FILE* f = fopen(path, "w");
  fprintf(f, "Terminal\ncom.apple.Terminal\n\n");
  fclose(f);

  struct string_list list = load_string_list(path);
  assert(list.count == 2);
  assert(strcmp(list.items[0], "Terminal") == 0);
  assert(strcmp(list.items[1], "com.apple.Terminal") == 0);

  assert(blacklist_contains(list.items, list.count, "Terminal", "com.example.other") == true);
  assert(blacklist_contains(list.items, list.count, "Other", "com.apple.Terminal") == true);
  assert(blacklist_contains(list.items, list.count, "Other", "com.example.other") == false);

  struct string_list missing = load_string_list("/tmp/does_not_exist_xyz.txt");
  assert(missing.count == 0);
  assert(missing.items == NULL);

  printf("ALL TESTS PASSED\n");
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
clang -std=c99 -Isrc test/test_string_list.c src/helpers.c -o /tmp/test_string_list
```
Expected: FAIL — `src/helpers.c` doesn't exist yet, or `load_string_list`/`blacklist_contains` undefined.

- [ ] **Step 3: Add declarations to `src/helpers.h`**

Add to `src/helpers.h` (alongside the existing declarations):

```c
#include <stdint.h>

struct string_list {
  char** items;
  uint32_t count;
};

struct string_list load_string_list(const char* path);
bool blacklist_contains(char** list, uint32_t count, char* app, char* bundle_id);
```

- [ ] **Step 4: Create `src/helpers.c`**

```c
#include "helpers.h"
#include <stdio.h>
#include <string.h>

struct string_list load_string_list(const char* path) {
  struct string_list list = { NULL, 0 };

  FILE* file = fopen(path, "r");
  if (!file) return list;

  char line[255];
  while (fgets(line, sizeof(line), file)) {
    uint32_t len = strlen(line);
    if (len == 0) continue;
    if (line[len - 1] == '\n') line[--len] = '\0';
    if (len == 0) continue;

    list.items = realloc(list.items, sizeof(char*) * ++list.count);
    list.items[list.count - 1] = string_copy(line);
  }
  fclose(file);
  return list;
}

bool blacklist_contains(char** list, uint32_t count, char* app, char* bundle_id) {
  if (!app || !bundle_id) return true;
  for (uint32_t i = 0; i < count; i++)
    if (strcmp(list[i], app) == 0 || strcmp(list[i], bundle_id) == 0) return true;
  return false;
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
clang -std=c99 -Isrc test/test_string_list.c src/helpers.c -o /tmp/test_string_list
/tmp/test_string_list
```
Expected: `ALL TESTS PASSED`.

- [ ] **Step 6: Update `event_tap.c` to use the shared helper (keep existing behavior identical)**

In `src/event_tap.c`, replace `event_tap_check_blacklist` (lines 3-13) with:

```c
bool event_tap_check_blacklist(struct event_tap* event_tap,
                               char* app, char* bundle_id  ) {
  return blacklist_contains(event_tap->blacklist, event_tap->blacklist_count, app, bundle_id);
}
```

Replace the body of `event_tap_load_blacklist` (lines 43-66), keeping the function signature and the `front_app_ignored`/`mask` setup untouched:

```c
void event_tap_load_blacklist(struct event_tap* event_tap) {
  event_tap->front_app_ignored = true;

  char* home = getenv("HOME");
  char buf[512];
  snprintf(buf, sizeof(buf), "%s/%s", home, ".config/svim/blacklist");

  struct string_list list = load_string_list(buf);
  event_tap->blacklist = list.items;
  event_tap->blacklist_count = list.count;
}
```

- [ ] **Step 7: Rebuild the full binary to confirm nothing broke**

```bash
make clean && make
```
Expected: builds cleanly, no warnings (existing `-Werror` flags catch signature mismatches).

- [ ] **Step 8: Commit**

```bash
git add src/helpers.h src/helpers.c src/event_tap.c test/test_string_list.c
git commit -m "extract shared string-list/blacklist helper for reuse by VN blacklist"
```

---

### Task 4: `vn_input` state, config, and routing decision (TDD)

**Files:**
- Create: `src/vn_input.h`
- Create: `src/vn_input.c`
- Test: `test/test_vn_input.c`

**Interfaces:**
- Consumes: `vn_engine.h` (Task 2), `helpers.h`'s `load_string_list`/`blacklist_contains` (Task 3), `buffer.h`'s mode constants (`NORMAL`, `INSERT`, `VISUAL`, `CMDLINE`, already defined via `libvim.h`), `env_vars.h`, `vfork_exec`/`string_copy` (already declared in `buffer.h`/`helpers.h`).
- Produces (used by Tasks 6, 7, 8):
  - `enum vn_flow { VN_FLOW_NONE, VN_FLOW_SYNTHETIC, VN_FLOW_VIM_BUFFER };`
  - `struct vn_input { bool enabled; char** blacklist; uint32_t blacklist_count; vn_method method; CGEventFlags hotkey_mask; };`
  - `struct vn_input g_vn_input;`
  - `void vn_input_begin(struct vn_input* vn);`
  - `bool vn_input_blacklisted(struct vn_input* vn, char* app, char* bundle_id);`
  - `enum vn_flow vn_input_route(struct vn_input* vn, bool is_vn_blacklisted, bool front_app_ignored, uint32_t cursor_mode);`
  - `void vn_input_toggle(struct vn_input* vn);`

- [ ] **Step 1: Write the failing test**

Create `test/test_vn_input.c`:

```c
#include <assert.h>
#include <stdio.h>
#include "../src/vn_input.h"

// libvim mode bit values, copied from lib/libvim/libvim.h so this test has
// no dependency on the real vim engine (this test targets pure routing
// logic in vn_input.c, not libvim).
#define NORMAL  0x01
#define INSERT  0x02
#define CMDLINE 0x04
#define VISUAL  0x08

int main(void) {
  struct vn_input vn = { .enabled = true };

  // VN disabled entirely -> never routes, regardless of everything else.
  struct vn_input disabled = { .enabled = false };
  assert(vn_input_route(&disabled, false, true, INSERT) == VN_FLOW_NONE);
  assert(vn_input_route(&disabled, false, false, INSERT) == VN_FLOW_NONE);

  // App is VN-blacklisted -> never routes.
  assert(vn_input_route(&vn, true, true, INSERT) == VN_FLOW_NONE);
  assert(vn_input_route(&vn, true, false, INSERT) == VN_FLOW_NONE);

  // vim-mode blacklisted for this app (front_app_ignored) -> synthetic flow,
  // regardless of cursor mode (vim isn't tracking mode for this app at all).
  assert(vn_input_route(&vn, false, true, NORMAL) == VN_FLOW_SYNTHETIC);
  assert(vn_input_route(&vn, false, true, INSERT) == VN_FLOW_SYNTHETIC);

  // vim-mode active for this app -> only INSERT routes, to the vim buffer.
  assert(vn_input_route(&vn, false, false, INSERT) == VN_FLOW_VIM_BUFFER);
  assert(vn_input_route(&vn, false, false, NORMAL) == VN_FLOW_NONE);
  assert(vn_input_route(&vn, false, false, VISUAL) == VN_FLOW_NONE);
  assert(vn_input_route(&vn, false, false, CMDLINE) == VN_FLOW_NONE);

  printf("ALL TESTS PASSED\n");
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
clang -std=c99 -Isrc test/test_vn_input.c -o /tmp/test_vn_input
```
Expected: FAIL — `src/vn_input.h` doesn't exist.

- [ ] **Step 3: Write `src/vn_input.h`**

```c
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <ApplicationServices/ApplicationServices.h>
#include "vn_engine.h"

enum vn_flow { VN_FLOW_NONE, VN_FLOW_SYNTHETIC, VN_FLOW_VIM_BUFFER };

struct vn_input {
  bool enabled;
  char** blacklist;
  uint32_t blacklist_count;
  vn_method method;
  CGEventFlags hotkey_mask;
};

extern struct vn_input g_vn_input;

void vn_input_begin(struct vn_input* vn);
bool vn_input_blacklisted(struct vn_input* vn, char* app, char* bundle_id);
enum vn_flow vn_input_route(struct vn_input* vn, bool is_vn_blacklisted,
                            bool front_app_ignored, uint32_t cursor_mode);
void vn_input_toggle(struct vn_input* vn);
```

- [ ] **Step 4: Write `src/vn_input.c`** (routing logic only for now — config/blacklist loading and toggle come in Steps 6-7, kept in the same file/task since they share the same small module and no later task needs them split out)

```c
#include "vn_input.h"
#include "helpers.h"
#include "buffer.h" // NORMAL / INSERT / VISUAL / CMDLINE mode bits, via libvim.h

struct vn_input g_vn_input;

bool vn_input_blacklisted(struct vn_input* vn, char* app, char* bundle_id) {
  return blacklist_contains(vn->blacklist, vn->blacklist_count, app, bundle_id);
}

enum vn_flow vn_input_route(struct vn_input* vn, bool is_vn_blacklisted,
                            bool front_app_ignored, uint32_t cursor_mode) {
  if (!vn->enabled || is_vn_blacklisted) return VN_FLOW_NONE;
  if (front_app_ignored) return VN_FLOW_SYNTHETIC;
  if (cursor_mode & INSERT) return VN_FLOW_VIM_BUFFER;
  return VN_FLOW_NONE;
}
```

- [ ] **Step 5: Run test to verify it passes**

```bash
clang -std=c99 -Isrc test/test_vn_input.c src/vn_input.c -o /tmp/test_vn_input
/tmp/test_vn_input
```
Expected: `ALL TESTS PASSED`. (Linking `vn_input.c` alone is enough here — `vn_input_begin`/`vn_input_blacklisted`/`vn_input_toggle` aren't called by this test, so their as-yet-unwritten dependencies don't need to be linked.)

- [ ] **Step 6: Add config loading and `vn_input_begin` to `src/vn_input.c`**

Append to `src/vn_input.c`:

```c
#include <stdlib.h>
#include <string.h>

static CGEventFlags parse_hotkey(const char* str) {
  CGEventFlags mask = 0;
  char buf[128];
  snprintf(buf, sizeof(buf), "%s", str);

  char* token = strtok(buf, "+");
  while (token) {
    if (strcmp(token, "control") == 0) mask |= kCGEventFlagMaskControl;
    else if (strcmp(token, "shift") == 0) mask |= kCGEventFlagMaskShift;
    else if (strcmp(token, "command") == 0) mask |= kCGEventFlagMaskCommand;
    else if (strcmp(token, "option") == 0) mask |= kCGEventFlagMaskAlternate;
    token = strtok(NULL, "+");
  }
  return mask;
}

static void vn_config_load(struct vn_input* vn) {
  vn->method = VN_METHOD_TELEX;
  vn->hotkey_mask = kCGEventFlagMaskControl | kCGEventFlagMaskShift;

  char* home = getenv("HOME");
  char path[512];
  snprintf(path, sizeof(path), "%s/.config/svim/vn_config", home);
  FILE* file = fopen(path, "r");
  if (!file) return;

  char line[255];
  while (fgets(line, sizeof(line), file)) {
    uint32_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = '\0';
    char* key = line;
    char* value = eq + 1;
    if (strcmp(key, "method") == 0) {
      vn->method = (strcmp(value, "vni") == 0) ? VN_METHOD_VNI : VN_METHOD_TELEX;
    } else if (strcmp(key, "hotkey") == 0) {
      vn->hotkey_mask = parse_hotkey(value);
    }
  }
  fclose(file);
}

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

- [ ] **Step 7: Add `vn_input_toggle` to `src/vn_input.c`**

```c
void vn_input_toggle(struct vn_input* vn) {
  vn->enabled = !vn->enabled;
  vn_engine_reset();

  struct env_vars env_vars;
  env_vars_init(&env_vars);
  env_vars_set(&env_vars, string_copy("VNMODE"), string_copy(vn->enabled ? "on" : "off"));

  char* home = getenv("HOME");
  char buf[512];
  snprintf(buf, sizeof(buf), "%s/.config/svim/svim.sh", home);
  vfork_exec(buf, &env_vars);
  env_vars_destroy(&env_vars);
}
```

Add `#include "env_vars.h"` to the top of `src/vn_input.c`.

- [ ] **Step 8: Rebuild the standalone test to confirm the file still compiles as a whole**

```bash
clang -std=c99 -Ilib -Isrc test/test_vn_input.c src/vn_input.c src/helpers.c -o /tmp/test_vn_input
/tmp/test_vn_input
```
Expected: `ALL TESTS PASSED` (this only exercises `vn_input_route`, but confirms the rest of the file — used by later tasks — still compiles).

- [ ] **Step 9: Commit**

```bash
git add src/vn_input.h src/vn_input.c test/test_vn_input.c
git commit -m "add vn_input: VN blacklist, config, and Flow A/B/None routing decision"
```

---

### Task 5: Wire `vn_engine`/`vn_input` into the main build

**Files:**
- Modify: `makefile`

**Interfaces:**
- Consumes: `src/vn_engine.c`, `src/vn_input.c`, `src/helpers.c` (Tasks 2-4), `lib/libunikey.a` (Task 1).
- Produces: `bin/svim` links successfully with the new objects — required before Tasks 6-7 can call `vn_engine_*`/`vn_input_*` from `event_tap.c`/`ax.c`.

- [ ] **Step 1: Add new object files to `_OBJ` in `makefile`**

Change:
```make
_OBJ = helpers.om workspace.om event_tap.o ax.o buffer.o line.o env_vars.o
```
to:
```make
_OBJ = helpers.om workspace.om event_tap.o ax.o buffer.o line.o env_vars.o vn_engine.o vn_input.o helpers_c.o
```

(`helpers.om` — the existing Objective-C helpers file, unchanged — and the new plain-C `helpers.c` from Task 3 need distinct object names since the build rules key off matching source extension; name the new one's object `helpers_c.o` to avoid colliding with `helpers.om`'s `helpers.o`... note the existing pattern already produces `helpers.om` for `helpers.m`, so a plain `helpers.o` from `helpers.c` does not collide — no rename needed. Revert to:)

```make
_OBJ = helpers.om workspace.om event_tap.o ax.o buffer.o line.o env_vars.o vn_engine.o vn_input.o helpers.o
```

- [ ] **Step 2: Add `lib/libunikey.a` and `-lc++` to `LIBS`**

Change:
```make
LIBS = lib/libvim.a -lm -lncurses -liconv -framework Carbon -framework Cocoa
```
to:
```make
LIBS = lib/libvim.a lib/libunikey.a -lm -lncurses -liconv -lc++ -framework Carbon -framework Cocoa
```

- [ ] **Step 3: `vn_engine.c`/`vn_input.c` are C, not Objective-C — confirm the existing `%.o` rule already covers them**

`makefile` already has:
```make
$(ODIR)/%.o: $(SRC)/%.c $(SRC)/%.h | $(ODIR)
	$(CC) -c -o $@ $< $(CFLAGS)
```
This matches `.c`+`.h` pairs already (`buffer.c`/`buffer.h`, etc.) — `vn_engine.c`/`vn_engine.h`, `vn_input.c`/`vn_input.h`, and `helpers.c`/`helpers.h` all fit this rule as-is. No new Makefile rule needed.

- [ ] **Step 4: Build and verify**

```bash
make clean && make 2>&1 | tail -40
```
Expected: builds cleanly (no `-Werror` failures), produces `bin/svim`. At this point `vn_engine`/`vn_input` are compiled into the binary but not called from anywhere yet — that's Tasks 6-7.

- [ ] **Step 5: Commit**

```bash
git add makefile
git commit -m "wire vn_engine/vn_input objects and libunikey into the main build"
```

---

### Task 6: Flow A — synthetic backspace+retype in `event_tap.c`

**Files:**
- Modify: `src/event_tap.h` (add `vn_ignored` field)
- Modify: `src/event_tap.c` (`key_handler`, new static helpers)

**Interfaces:**
- Consumes: `vn_input_route`/`g_vn_input` (Task 4), `vn_engine_process_key`/`vn_engine_process_backspace` (Task 2).
- Produces: `struct event_tap` gains a `bool vn_ignored` field, set by Task 8 (`workspace.m`) on app switch — this task only reads it.

- [ ] **Step 1: Add `vn_ignored` to `struct event_tap` in `src/event_tap.h`**

```c
struct event_tap {
  bool front_app_ignored;
  bool vn_ignored;
  uint32_t blacklist_count;
  char** blacklist;
  CFMachPortRef handle;
  CFRunLoopSourceRef runloop_source;
  CGEventMask mask;
};
```

- [ ] **Step 2: Add the synthetic-event tag constant and includes to `src/event_tap.c`**

At the top of `src/event_tap.c`, after `#include "event_tap.h"`:

```c
#include "vn_input.h"
#include <Carbon/Carbon.h> // kVK_Delete

#define VN_SYNTH_TAG 0x564E5359 // 'VNSY'
```

- [ ] **Step 3: Add the correction-application helper**

Add above `key_handler` in `src/event_tap.c`:

```c
static void vn_post_correction(int backspace_count, const unsigned char* insert_text, int insert_len) {
  CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);

  for (int i = 0; i < backspace_count; i++) {
    CGEventRef down = CGEventCreateKeyboardEvent(source, kVK_Delete, true);
    CGEventRef up   = CGEventCreateKeyboardEvent(source, kVK_Delete, false);
    CGEventSetIntegerValueField(down, kCGEventSourceUserData, VN_SYNTH_TAG);
    CGEventSetIntegerValueField(up, kCGEventSourceUserData, VN_SYNTH_TAG);
    CGEventPost(kCGAnnotatedSessionEventTap, down);
    CGEventPost(kCGAnnotatedSessionEventTap, up);
    CFRelease(down);
    CFRelease(up);
  }

  if (insert_len > 0) {
    CFStringRef str = CFStringCreateWithBytes(NULL, insert_text, insert_len,
                                              kCFStringEncodingUTF8, false);
    CFIndex length = CFStringGetLength(str);
    UniChar chars[length];
    CFStringGetCharacters(str, CFRangeMake(0, length), chars);

    CGEventRef down = CGEventCreateKeyboardEvent(source, 0, true);
    CGEventRef up   = CGEventCreateKeyboardEvent(source, 0, false);
    CGEventKeyboardSetUnicodeString(down, length, chars);
    CGEventSetIntegerValueField(down, kCGEventSourceUserData, VN_SYNTH_TAG);
    CGEventSetIntegerValueField(up, kCGEventSourceUserData, VN_SYNTH_TAG);
    CGEventPost(kCGAnnotatedSessionEventTap, down);
    CGEventPost(kCGAnnotatedSessionEventTap, up);
    CFRelease(down);
    CFRelease(up);
    CFRelease(str);
  }

  CFRelease(source);
}

static CGEventRef vn_flow_a_process(struct event_tap* event_tap, CGEventRef event) {
  // cursor_mode is irrelevant here: vn_input_route short-circuits on
  // front_app_ignored (always true on this call path) before ever looking
  // at it, so 0 is a safe placeholder value, not a guess.
  enum vn_flow flow = vn_input_route(&g_vn_input, event_tap->vn_ignored, true, 0);
  if (flow != VN_FLOW_SYNTHETIC) return event;

  UniCharCount count;
  UniChar character;
  CGEventKeyboardGetUnicodeString(event, 1, &count, &character);
  CGEventFlags flags = CGEventGetFlags(event);
  int64_t keycode = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);

  struct vn_engine_result result = (keycode == kVK_Delete)
    ? vn_engine_process_backspace()
    : vn_engine_process_key(character,
                             flags & kCGEventFlagMaskShift,
                             flags & kCGEventFlagMaskAlphaShift);

  if (result.backspace_count == 0 && result.insert_len == 0) return event;

  vn_post_correction(result.backspace_count, result.insert_text, result.insert_len);
  return NULL;
}
```

- [ ] **Step 4: Update `key_handler`**

Replace the whole function:

```c
static CGEventRef key_handler(CGEventTapProxy proxy, CGEventType type,
                              CGEventRef event, void* reference) {
  if (CGEventGetIntegerValueField(event, kCGEventSourceUserData) == VN_SYNTH_TAG) {
    return event;
  }

  switch (type) {
    case kCGEventTapDisabledByTimeout:
      printf("Timeout\n");
    case kCGEventTapDisabledByUserInput: {
      printf("restarting event-tap\n");
      CGEventTapEnable(((struct event_tap*) reference)->handle, true);
    } break;
    case kCGEventFlagsChanged: {
      CGEventFlags flags = CGEventGetFlags(event);
      if ((flags & g_vn_input.hotkey_mask) == g_vn_input.hotkey_mask) {
        vn_input_toggle(&g_vn_input);
      }
    } break;
    case kCGEventLeftMouseDown: {
      vn_engine_reset();
    } break;
    case kCGEventKeyDown: {
      struct event_tap* event_tap = (struct event_tap*) reference;
      if (event_tap->front_app_ignored) {
        if (g_ax.selected_element && g_ax.role) {
          ax_clear(&g_ax);
        }
        return vn_flow_a_process(event_tap, event);
      }

      return ax_process_event(&g_ax, event);
    } break;
  }
  return event;
}
```

- [ ] **Step 5: Build**

```bash
make clean && make 2>&1 | tail -40
```
Expected: builds cleanly. (The tap's event mask doesn't yet include `kCGEventFlagsChanged`/`kCGEventLeftMouseDown` — that's Task 8's `main.m`/`event_tap_begin` change; until then these `case` branches are dead code, which is fine as an intermediate state within this multi-task feature.)

- [ ] **Step 6: Commit**

```bash
git add src/event_tap.h src/event_tap.c
git commit -m "Flow A: apply VN corrections via synthetic CGEventPost for vim-mode-blacklisted apps"
```

---

### Task 7: Flow B — vim buffer integration in `ax.c`/`buffer.c`

**Files:**
- Modify: `src/buffer.h` (new `BACKSPACE_KEY` define, new function declaration)
- Modify: `src/buffer.c` (new `buffer_input_string` function)
- Modify: `src/ax.c` (`ax_process_event`, `ax_get_selected_element`)

**Interfaces:**
- Consumes: `vn_input_route`/`g_vn_input` (Task 4), `vn_engine_process_key`/`vn_engine_process_backspace` (Task 2).
- Produces: `void buffer_input_string(struct buffer* buffer, int backspace_count, const char* text);` — used only within this task, but declared in `buffer.h` to keep `ax.c` ignorant of `vimKey`/`vimInput` internals (matches the existing module boundary where `ax.c` never calls libvim functions directly).

- [ ] **Step 1: Add `BACKSPACE_KEY` and the new declaration to `src/buffer.h`**

Add alongside the existing key defines:

```c
#define BACKSPACE_KEY "<bs>"
```

Add to the function declarations:

```c
void buffer_input_string(struct buffer* buffer, int backspace_count, const char* text);
```

- [ ] **Step 2: Add `buffer_input_string` to `src/buffer.c`**

Add near `buffer_input`:

```c
void buffer_input_string(struct buffer* buffer, int backspace_count, const char* text) {
  for (int i = 0; i < backspace_count; i++) vimKey(BACKSPACE_KEY);
  if (text) vimInput((char_u*) text);
  buffer_sync(buffer);
}
```

- [ ] **Step 3: Add `buffer_sync` to `src/buffer.h`'s declarations**

(It's currently defined in `buffer.c` but not declared in `buffer.h`; `buffer_input_string` above calls it from within the same file so this isn't strictly required for Step 2 to compile, but add it for consistency since `ax.c` already relies on `buffer_input`'s existing wrapping of it — no behavior change, just makes the existing implicit contract explicit):

```c
void buffer_sync(struct buffer* buffer);
```

- [ ] **Step 4: Integrate into `ax.c`'s `ax_process_event`**

In `src/ax.c`, add includes at the top:

```c
#include "vn_input.h"
#include <Carbon/Carbon.h> // kVK_Delete
```

Replace this block (around line 226-228):
```c
    bool was_insert = ax->buffer.cursor.mode & INSERT
                      || !ax->buffer.cursor.mode;
    buffer_input(&ax->buffer, character, count);
```
with:
```c
    bool was_insert = ax->buffer.cursor.mode & INSERT
                      || !ax->buffer.cursor.mode;

    enum vn_flow flow = vn_input_route(&g_vn_input, g_event_tap.vn_ignored,
                                       g_event_tap.front_app_ignored,
                                       ax->buffer.cursor.mode           );
    if (flow == VN_FLOW_VIM_BUFFER) {
      int64_t keycode = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
      struct vn_engine_result result = (keycode == kVK_Delete)
        ? vn_engine_process_backspace()
        : vn_engine_process_key(character, flags & FLAG_SHIFT,
                                flags & kCGEventFlagMaskAlphaShift);

      if (result.backspace_count > 0 || result.insert_len > 0) {
        char* text = NULL;
        if (result.insert_len > 0) {
          CFStringRef str = CFStringCreateWithBytes(NULL, result.insert_text,
                                                    result.insert_len,
                                                    kCFStringEncodingUTF8,
                                                    false                );
          text = cfstring_get_cstring(str);
          CFRelease(str);
        }
        buffer_input_string(&ax->buffer, result.backspace_count, text);
        if (text) free(text);
        return NULL;
      }
    } else {
      buffer_input(&ax->buffer, character, count);
    }
```

Note: `flow` is computed here (inside `ax_process_event`, which is only reached when `front_app_ignored == false`) purely to decide whether to route this specific keystroke through the engine — `front_app_ignored` is always `false` on this path, so `vn_input_route` only ever returns `VN_FLOW_VIM_BUFFER` or `VN_FLOW_NONE` here, never `VN_FLOW_SYNTHETIC`. This reuses the one routing function from Task 4 rather than re-deriving the same `enabled && !blacklisted && mode&INSERT` condition by hand.

- [ ] **Step 5: Add focus-change reset to `ax_get_selected_element`**

In `src/ax.c`, find:
```c
  ax_clear(ax);
```
(inside `ax_get_selected_element`, called when the focused element differs from the previous one) and add immediately after it:
```c
  vn_engine_reset();
```

- [ ] **Step 6: Build**

```bash
make clean && make 2>&1 | tail -40
```
Expected: builds cleanly.

- [ ] **Step 7: Commit**

```bash
git add src/buffer.h src/buffer.c src/ax.c
git commit -m "Flow B: apply VN corrections directly into the vim buffer during INSERT mode"
```

---

### Task 8: App-switch blacklist wiring and hotkey/mouse event mask

**Files:**
- Modify: `src/workspace.m` (`appSwitched:`)
- Modify: `src/main.m` (`main`)
- Modify: `src/event_tap.c` (`event_tap_begin` mask)

**Interfaces:**
- Consumes: `vn_input_blacklisted`, `g_vn_input` (Task 4), `vn_engine_reset` (Task 2), `event_tap->vn_ignored` (Task 6).
- Produces: nothing new — this task is what makes Tasks 6-7's `vn_ignored`/hotkey/mouse-reset code paths actually reachable at runtime.

- [ ] **Step 1: Update `appSwitched:` in `src/workspace.m`**

Replace:
```objc
    g_event_tap.front_app_ignored = event_tap_check_blacklist(&g_event_tap,
                                                              name,
                                                              bundle_id    );
    ax_front_app_changed(&g_ax, pid);
```
with:
```objc
    g_event_tap.front_app_ignored = event_tap_check_blacklist(&g_event_tap,
                                                              name,
                                                              bundle_id    );
    g_event_tap.vn_ignored = vn_input_blacklisted(&g_vn_input, name, bundle_id);
    vn_engine_reset();
    ax_front_app_changed(&g_ax, pid);
```

Add `#include "vn_input.h"` to the top of `src/workspace.m`.

- [ ] **Step 2: Extend the event tap mask in `src/event_tap.c`'s `event_tap_begin`**

Replace:
```c
  event_tap->mask = 1 << kCGEventKeyDown;
```
with:
```c
  event_tap->mask = (1 << kCGEventKeyDown)
                  | (1 << kCGEventFlagsChanged)
                  | (1 << kCGEventLeftMouseDown);
```

- [ ] **Step 3: Initialize `vn_input` at startup in `src/main.m`**

Add `#include "vn_input.h"` to the top of `src/main.m`. In `main()`, after `workspace_begin(&g_workspace);`, add:
```c
  vn_input_begin(&g_vn_input);
```

- [ ] **Step 4: Build**

```bash
make clean && make 2>&1 | tail -40
```
Expected: builds cleanly.

- [ ] **Step 5: Commit**

```bash
git add src/workspace.m src/event_tap.c src/main.m
git commit -m "wire VN blacklist check, engine reset, and hotkey/mouse event mask into startup and app-switch"
```

---

### Task 9: End-to-end build, deploy, and manual verification

**Files:** none (deployment + manual QA only)

**Interfaces:** none — this task consumes the fully-wired binary from Tasks 1-8.

- [ ] **Step 1: Run both standalone unit tests one more time**

```bash
clang -std=c99 -Ilib -Isrc test/test_vn_engine.c src/vn_engine.c lib/libunikey.a -lc++ -o /tmp/test_vn_engine && /tmp/test_vn_engine
clang -std=c99 -Isrc test/test_string_list.c src/helpers.c -o /tmp/test_string_list && /tmp/test_string_list
clang -std=c99 -Ilib -Isrc test/test_vn_input.c src/vn_input.c src/helpers.c -o /tmp/test_vn_input && /tmp/test_vn_input
```
Expected: `ALL TESTS PASSED` from all three.

- [ ] **Step 2: Build and deploy to the existing Homebrew-managed install**

(Reusing the exact redeploy procedure already established earlier for this same Cellar path — adjust the version path if `brew upgrade svim` has since changed it.)

```bash
make clean && make
brew services stop svim
cp -f bin/svim /opt/homebrew/Cellar/svim/1.0.11/bin/svim
chmod 555 /opt/homebrew/Cellar/svim/1.0.11/bin/svim
codesign -fs - -i com.felixkratz.svim /opt/homebrew/Cellar/svim/1.0.11/bin/svim
brew services start svim
brew services list | grep svim
```
Expected: `svim  started  ...` (same identifier as before means no re-grant of Accessibility should be needed this time).

- [ ] **Step 3: Manual verification — Flow A (no vim-mode, e.g. Terminal)**

Add `Terminal` to `~/.config/svim/blacklist` (vim-mode blacklist) if not already present, restart the service, then in Terminal type `vieejt` and confirm the terminal shows `việt` after pressing the hotkey (`Control+Shift` by default) to enable VN mode first, and shows literal `vieejt` when VN mode is off.

- [ ] **Step 4: Manual verification — Flow B (vim-mode active, INSERT mode)**

In an app NOT in the vim-mode blacklist (e.g. a Safari text field), enter svim's INSERT mode, enable VN via the hotkey, type `vieejt` and confirm `việt` appears; switch to NORMAL mode and confirm `j`/`k`/`w` still move the cursor as motions (not transformed).

- [ ] **Step 5: Manual verification — reset triggers**

Start typing a word (e.g. `vi`), click into a different text field mid-word, then type `eejt` — confirm it does not retroactively edit the first field or produce garbled output in the second (mouse-down reset from Task 6/8 working).

- [ ] **Step 6: Manual verification — hook script**

Add a line to `~/.config/svim/svim.sh` that appends `$VNMODE` to a log file, restart the service, toggle the hotkey twice, and confirm the log shows `on` then `off`.

- [ ] **Step 7: Final commit (if any fixes were needed during manual verification)**

```bash
git add -A
git commit -m "fix issues found during manual VN IME verification"
```
(Only if Steps 3-6 required code changes — otherwise nothing to commit here.)
