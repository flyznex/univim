# Suppress Vietnamese processing under a composing system IME — Design

## Goal

Stop UniVim from mangling input when a composing macOS system input method
(Korean, Japanese, Chinese, etc.) is the active input source. Today, with VI
mode on and a Korean IME selected, UniVim runs Telex/VNI on the raw pre-IME
keystrokes and posts backspace/insert corrections, which then collide with the
Hangul the Korean IME composes downstream — corrupting both. UniVim must stand
down while a composing IME is active and resume automatically when the user
returns to a plain keyboard layout.

## Root cause

UniVim's event tap is created at `kCGAnnotatedSessionEventTap` /
`kCGHeadInsertEventTap` (`src/event_tap.c:352`), which sits **before** the
system input method composes. UniVim sees the US-layout unicode first,
processes it, and only then does the (mangled) stream reach the system IME. Two
composers own the same keystroke. There is currently **no** keyboard-layout or
input-source awareness anywhere in the codebase — the sole routing gate
`vn_input_route` (`src/vn_input.c:30-36`) checks only the VI/EN toggle,
blacklist, front-app-ignored, and vim INSERT mode.

## The distinction we key on

macOS Text Input Sources expose `kTISPropertyInputSourceType`:

- `kTISTypeKeyboardLayout` — non-composing layouts: U.S., ABC, French AZERTY,
  German QWERTZ, Vietnamese Telex/VNI layouts, etc. These only map keys to
  characters. **UniVim stays active.**
- Any other type (notably `kTISTypeKeyboardInputMode`, and IME parent sources)
  — composing input methods: Korean 2-Set/3-Set, Japanese Hiragana/Katakana,
  Pinyin, Zhuyin, etc. These own the keystroke and compose. **UniVim stands
  down.**

The classifier keys on **type, not language**. This is exactly "suppress for
system IMEs, not for keyboard layouts": it never suppresses on
AZERTY/QWERTZ/Vietnamese layouts (all `kTISTypeKeyboardLayout`), and it
suppresses on every composing IME regardless of its language.

**Conservative bias:** the test is `type == kTISTypeKeyboardLayout` → not an
IME; **anything else → treat as IME (suppress).** If an odd third-party source
reports an unexpected type, the safe failure is UniVim being dormant (user
switches source to fix) rather than corrupting IME input.

## Non-goals

- Not moving the event tap or re-architecting the capture point. The tap stays
  pre-IME; we gate processing instead.
- Not automating mixed-language typing. To type a Vietnamese word inside a
  Korean sentence, the user switches the system source (KR → a Latin layout →
  back). This is architecturally unavoidable — the OS routes each keystroke to
  exactly one active input source; two composers cannot share a keystroke
  without the corruption this bug describes. This is how every macOS IME
  behaves. It will be documented, not coded around.
- Not changing the menubar. The active system IME already shows its own menubar
  indicator, so the user can see UniVim is dormant. UniVim's menubar keeps
  showing its own EN/VI (and EN-/VI-) state unchanged.
- Not language/script filtering. Type-based only.
- Not a new dependency. Carbon (which hosts the Text Input Sources API) is
  already linked (`makefile:4`, `CMakeLists.txt:66`) and `<Carbon/Carbon.h>` is
  already included in the relevant sources.

## Components

### Input-source classifier — `src/input_source.{h,c}` (new, small)

A tiny module with one job: classify the current input source.

```c
// input_source.h
#pragma once
#include <stdbool.h>

// True when the current keyboard input source is a composing IME
// (anything whose kTISPropertyInputSourceType is not kTISTypeKeyboardLayout).
// Reads TISCopyCurrentKeyboardInputSource(); intended to be called on
// source-change notifications, not per-keystroke.
bool input_source_is_composing_ime(void);
```

Implementation: `TISCopyCurrentKeyboardInputSource()`, read
`kTISPropertyInputSourceType`, compare to `kTISTypeKeyboardLayout`, `CFRelease`
the source. Returns `false` (not an IME → UniVim active) only when the type is
exactly `kTISTypeKeyboardLayout`; returns `true` otherwise, including if the
copy or property read fails in a way that leaves the type indeterminate — but
if `TISCopyCurrentKeyboardInputSource` returns NULL entirely, return `false`
(no source info → don't suppress; the app was working before this feature).

A new file (not folded into `vn_input.c`) because it is a self-contained,
single-responsibility unit with a clean interface and no dependency on
`vn_input`'s state — and it keeps the one piece that needs live TIS validation
isolated.

### Cached flag + observer — `src/workspace.m`

A single cached bool, updated on input-source changes:

- Add `extern bool g_input_source_is_ime;` (define in `workspace.m` or a small
  shared spot; it is read by `vn_input_route`). Default `false` (UniVim active)
  until first computed.
- In `workspace_begin` (or the existing init path in `workspace.m`): register a
  Distributed/CF notification observer for
  `kTISNotifySelectedKeyboardInputSourceChanged`, and compute the flag once at
  startup via `input_source_is_composing_ime()`.
- The observer callback recomputes `g_input_source_is_ime =
  input_source_is_composing_ime();` and calls `vn_engine_reset()` so a
  half-composed Vietnamese word does not leak across a source switch (mirrors
  the existing `vn_engine_reset()` on app switch at `workspace.m:73`).
- Remove the observer in `dealloc` alongside the existing NSWorkspace observer
  teardown.

`workspace.m` is the right home: it already owns notification-observer
scaffolding and already resets the engine on context changes. Critically, the
observer catches layout switches that happen **without** an app switch (the user
hits the input-source hotkey while staying in the same app) — an
app-switch-only refresh would go stale.

Note: `kTISNotifySelectedKeyboardInputSourceChanged` is a Core Foundation
distributed notification (`CFNotificationCenterGetDistributedCenter`), not an
`NSWorkspace` notification, so it uses the CF observer API rather than the
existing `NSNotificationCenter` path — but it lives in the same init/teardown
lifecycle.

### Routing guard — `src/vn_input.c`

`vn_input_route` gains one parameter and one guard, matching the existing
`is_vn_blacklisted` / `front_app_ignored` parameter style so the function stays
pure and unit-testable:

```c
enum vn_flow vn_input_route(struct vn_input* vn, bool is_vn_blacklisted,
                            bool front_app_ignored, bool input_source_is_ime,
                            uint32_t cursor_mode) {
  if (!vn->enabled || is_vn_blacklisted) return VN_FLOW_NONE;
  if (input_source_is_ime) return VN_FLOW_NONE;   // <-- new: stand down under a system IME
  if (front_app_ignored) return VN_FLOW_SYNTHETIC;
  if (cursor_mode & INSERT) return VN_FLOW_VIM_BUFFER;
  return VN_FLOW_NONE;
}
```

Placed before the `front_app_ignored` check: when a composing IME is active,
UniVim does nothing at all — not even synthetic VN — so the raw keystroke flows
untouched to the system IME.

Both call sites pass the cached flag:
- Flow A (synthetic): `event_tap.c:149` — pass `g_input_source_is_ime`.
- Flow B (vim buffer): `ax.c:301` — pass `g_input_source_is_ime`.

This single chokepoint fixes both flows with the smallest correct diff.

## Data flow

Input-source change → CF notification → observer in `workspace.m` recomputes
`g_input_source_is_ime` via `input_source_is_composing_ime()` and resets the
engine. Every keystroke → `vn_input_route` reads the cached bool → returns
`VN_FLOW_NONE` while an IME is active, so no correction is ever synthesized and
the key passes to the system IME. No per-keystroke Carbon calls.

## Interaction with existing state

The VI/EN toggle becomes effectively a third layer under "is a real IME
active":
- Composing IME active → UniVim silent, regardless of VI/EN.
- Latin layout + VI → Vietnamese.
- Latin layout + EN → plain Latin.

This is coherent; the only UX wrinkle (VI/EN toggle silently doing nothing while
an IME is active) is handled by documentation, since the system IME's own
menubar indicator already signals the state. Menubar unchanged per decision.

## Testing

1. **Unit test** (`test/test_vn_input.c`, extend existing): `vn_input_route`
   with the new `input_source_is_ime` parameter. Assert:
   - `input_source_is_ime = true` → `VN_FLOW_NONE` for every combination of the
     other args (enabled/disabled, blacklisted or not, front_app_ignored or
     not, INSERT or not) — the IME guard dominates once `enabled` passes.
   - `input_source_is_ime = false` → all existing behaviors preserved (the
     current assertions, updated for the new arg): synthetic when
     front_app_ignored, vim-buffer when INSERT, none otherwise.
   The classifier (`input_source_is_composing_ime`) is NOT unit-tested — it
   needs live TIS state; it is covered by live verification below.
2. **Live verification** (manual, requires a real machine with multiple input
   sources installed — this is the user's to run):
   - Install/enable a Korean (and ideally Japanese and a Chinese Pinyin) input
     source plus at least one non-US Latin layout (French AZERTY or Vietnamese).
   - With UniVim VI on: select Korean → typing composes Hangul cleanly, no
     Vietnamese interference, no stray backspaces.
   - Switch to US layout (no app switch) → Vietnamese Telex works again
     immediately (confirms the observer fires without an app switch).
   - Switch to French AZERTY / Vietnamese layout → Vietnamese processing still
     works (confirms layouts are NOT suppressed — the type check, not language).
   - Switch back to Korean → suppressed again.
   - Confirm no half-composed Vietnamese word leaks across a source switch
     (engine reset on change).

## Open risk (flagged for live validation)

The exact set of types third-party IMEs report is not verifiable without a real
machine. The design uses the documented `type == kTISTypeKeyboardLayout` test
with a suppress-on-doubt default. If live testing surfaces a specific layout
misclassified as an IME (or vice versa), adjust the classifier in
`input_source.c` only — no other component changes.
