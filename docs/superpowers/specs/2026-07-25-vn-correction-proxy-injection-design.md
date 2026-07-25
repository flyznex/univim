# VN correction delivery via CGEventTapPostEvent — Design

## Goal

Fix an event-ordering race in how `vn_post_correction` (event_tap.c) delivers
synthetic Telex/VNI corrections (backspace-then-reinsert), confirmed this
session via a fully-automated, human-free repro (`repro_thay_race.c`) to
still corrupt words under fast typing even with all prior mitigations
(per-pid posting, idle-gap reset, autorepeat guard, increased per-app
delay) in place. See
[[../../../../.claude/projects/-Users-felix-Projects-SketchyVim/memory/project_vn_corruption_investigation.md|project memory]]
for the full investigation trail.

## Background

`vn_post_correction` currently posts synthetic backspace/insert keystrokes
via `CGEventPostToPid(target.pid, event)` (falling back to
`CGEventPost(kCGAnnotatedSessionEventTap, event)`). This is a *separate*
injection path from the one carrying real, unmodified keystrokes (which are
simply returned from the `key_handler` CGEventTap callback and continue
through the OS's normal event-tap pipeline). Nothing guarantees relative
ordering between these two paths — a real keystroke typed immediately after
a correction can be delivered to the target app before the correction's own
synthetic events finish landing, scrambling the text. This was already
identified once before (the per-pid posting change, documented in
`vn_post_event`'s existing comment) but per-pid posting only reduces, not
eliminates, the race.

Comparable project [gonhanh.org](https://github.com/khaphanspace/gonhanh.org)
(a similar CGEventTap-based Vietnamese IME, already used as prior art for
this project's per-app delay/strategy config) solves this with a
`.syncProxy` injection strategy: `CGEventTapPostEvent(proxy, event)` posts a
synthetic event directly into the *same* event-tap pipeline that intercepted
the original keystroke, at the correct position in the stream — guaranteeing
order relative to whatever comes after, since there is no second injection
path to race against. Their code comment: "Events are injected directly
into the event tap pipeline, guaranteeing correct ordering."

## Non-goals

- Not adopting gonhanh.org's other injection strategies (direct AX
  value read/write, char-by-char mode, autocomplete-breaking empty-char
  prefix). Those solve different problems (AX-write racing with Spotlight,
  browser autocomplete UI) not currently observed in svim. Revisit only if
  proxy injection alone proves insufficient.
- Not removing the existing per-app `delay_us` / `vn_overrides` mechanism.
  Proxy injection fixes *ordering*, not how fast a slow-rendering app (Chrome,
  Electron) can visually catch up — the delay is still a real, separate
  concern.
- Not changing `vn_engine.c` / libunikey usage at all — already confirmed
  uninvolved in this bug.

## Components

- `src/event_tap.h`: `struct vn_post_target` gains a `CGEventTapProxy proxy`
  field.
- `src/event_tap.c`:
  - `vn_post_event` (renamed usage, same helper): posts via
    `CGEventTapPostEvent(proxy, event)` instead of
    `CGEventPostToPid`/`CGEventPost`.
  - `vn_post_correction`: passes `target.proxy` through to every keyboard
    event it posts (backspace/select-arrow keys and the text-insert event).
  - `vn_synthetic_process`: gains a `CGEventTapProxy proxy` parameter,
    forwards it into the `vn_post_target` it builds.
  - `key_handler`: already receives `proxy` per the standard CGEventTap
    callback signature — passes it to `vn_synthetic_process` and
    `ax_process_event`.
- `src/ax.h` / `src/ax.c`: `ax_process_event` gains a `CGEventTapProxy proxy`
  parameter, forwards it into the `vn_post_target` it builds for Flow B's
  correction.

## Data flow

Unchanged except for the delivery leg: `vn_engine_process_key`/
`_process_backspace` still compute `{backspace_count, insert_text}`
identically. Where `vn_post_correction` used to call
`CGEventPostToPid`/`CGEventPost`, it now calls `CGEventTapPostEvent(proxy,
event)` for every synthetic key (backspace/select-arrow, unicode-string
insert). Since this re-injects the event into the same tap, it re-enters
`key_handler` — already handled correctly by the existing `VN_SYNTH_TAG`
short-circuit at the top of `key_handler`, which returns synthetic events
immediately without reprocessing.

## Error handling / edge cases

1. Both call sites (`vn_synthetic_process`, the Flow B branch of
   `ax_process_event`) are only ever reached from inside `key_handler`
   itself, so a valid `proxy` is always available — no null-proxy fallback
   path is needed.
2. The existing `usleep(target.delay_us)` safety margin at the end of
   `vn_post_correction` is unchanged — it addresses a slow-rendering app's
   processing time, not event ordering, and remains relevant regardless of
   injection API.

## Testing

- Not unit-testable in isolation — `CGEventTapPostEvent` requires a real
  `CGEventTapProxy` from an active tap callback, which `test_vn_engine.c`'s
  harness doesn't have (and doesn't need to, since `vn_engine.c` itself is
  unchanged).
- Verification: rebuild, deploy to the running svim instance, and:
  1. Re-run the existing regression suite (`test_vn_engine.c`) to confirm
     no change to engine behavior.
  2. Re-run `repro_thay_race.c`'s automated, human-free repro (300
     iterations, realistic captured timing) and compare the corruption
     count against this session's baseline runs (299 clean-run baseline had
     1/299 failures, an apparent cold-start artifact unrelated to this fix;
     the messier earlier run had multiple mid-run corruptions).
  3. Manual fast-typing spot check in Ghostty and Telegram.
