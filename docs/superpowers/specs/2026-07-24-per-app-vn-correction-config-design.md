# Per-app VN correction tuning (delay + strategy) — Design

## Goal

Let users tune, per frontmost app, how `vn_post_correction` delivers a Telex/VNI
correction (backspace-then-reinsert), instead of one hardcoded global delay and
one hardcoded posting strategy for every app. Motivated by comparing svim's
approach against Gõ Nhanh (`khaphanspace/gonhanh.org`), a similar CGEventTap-based
VN IME: it ships a small default delay (0.8ms) but exposes a per-app "Delay"
override up to "Very high" for apps (VS Code, Cursor, Windsurf running Claude
Code) that still lose characters at the default — and, for particularly
troublesome fields (address bars, search fields, IDEs), switches from
backspace-based deletion to select-then-replace (`Shift+Left` × N, then type
over the selection) instead of N discrete `Delete` keystrokes.

## Non-goals

- No automatic detection of "problem" apps via AX role/subrole (that's how Gõ
  Nhanh decides when to use select-replace). Out of scope for this round —
  purely user-declared per-app entries, matching how `vn_blacklist` already
  works. Revisit only if manual declaration proves too tedious in practice.
- Not changing the global defaults (`5ms`, backspace strategy) — both are
  empirically validated this session (zero drops across extensive live
  testing after landing the `CGEventPostToPid` fix). This feature is an
  escape hatch for apps that need something different, not a re-tuning of
  the baseline.
- No hot-reload of the config file — matches every other svim config file
  today (`vn_config`, `vn_blacklist`, `blacklist`); a restart is already the
  expected way to pick up changes.

## Components

- `src/vn_input.h` / `src/vn_input.c`:
  - `enum vn_correction_strategy { VN_STRATEGY_BACKSPACE, VN_STRATEGY_SELECT }`.
  - `struct vn_override { char* app; int delay_us; enum vn_correction_strategy strategy; }`.
  - New fields on `struct vn_input`: `struct vn_override* overrides;` /
    `uint32_t overrides_count;`.
  - `static void vn_overrides_load(struct vn_input* vn)` — parses
    `~/.config/svim/vn_overrides`, called from `vn_input_begin` alongside the
    existing `vn_blacklist` load. Follows `vn_config_load`'s existing
    fopen/fgets loop style, not `load_string_list` (rows have 3 fields, not 1).
  - `void vn_input_lookup_override(struct vn_input* vn, char* app, char*
    bundle_id, int* out_delay_us, enum vn_correction_strategy* out_strategy)`
    — always writes a result (defaults `5000`/`VN_STRATEGY_BACKSPACE` when no
    row matches), so call sites never need a found/not-found branch.
- `src/event_tap.h` / `src/event_tap.c`:
  - `struct vn_post_target { pid_t pid; int delay_us; enum
    vn_correction_strategy strategy; }` — replaces the current bare `pid_t
    target_pid` parameter on `vn_post_correction`, grouping the three values
    that travel together at every call site.
  - New fields on `struct event_tap`: `int delay_us;` / `enum
    vn_correction_strategy strategy;` — resolved once per app switch, same
    lifecycle as the existing `front_pid` (added earlier this session for the
    same reason: cheap to cache, expensive to look up on every keystroke).
- `src/workspace.m` `appSwitched:`: alongside the existing
  `vn_input_blacklisted` call, call `vn_input_lookup_override` and store the
  results into `g_event_tap.delay_us` / `g_event_tap.strategy`.

## Data flow

1. Startup: `vn_input_begin` loads `~/.config/svim/vn_overrides` into
   `g_vn_input.overrides` (empty list if the file doesn't exist — identical
   behavior to today for every app).
2. App switch: `workspace.m`'s `appSwitched:` resolves `delay_us`/`strategy`
   for the new frontmost app via `vn_input_lookup_override` and caches them on
   `g_event_tap`, next to the existing `front_pid` resolution.
3. Correction time: `vn_synthetic_process` (Flow A) and `ax.c`'s Flow B both
   already read `event_tap->front_pid` / `g_event_tap.front_pid` to build the
   pid for `vn_post_correction`; they now build a `struct vn_post_target {
   .pid = ..., .delay_us = g_event_tap.delay_us, .strategy =
   g_event_tap.strategy }` instead, and pass that single struct.
4. Inside `vn_post_correction`'s backspace loop: `target.strategy ==
   VN_STRATEGY_SELECT` sends `Shift+LeftArrow` down/up instead of `Delete`
   down/up for each of the `backspace_count` iterations. The Unicode-insert
   step afterward is unchanged in both strategies — typing over an active
   selection is standard OS text-editing behavior, no extra code needed.
5. End of `vn_post_correction`: `usleep(target.delay_us)` runs only when
   `target.strategy == VN_STRATEGY_BACKSPACE`. Select-replace always skips the
   delay (explicit product decision — not yet validated empirically that it's
   always safe to skip, but treated as the correct default pending evidence
   otherwise; a user who hits drops with `select` can fall back to
   `backspace` with a tuned delay for that app).

## Config format

**`~/.config/svim/vn_overrides`** — new file, one app per line:

```
# ~/.config/svim/vn_overrides
# format: AppName delay_ms strategy
# strategy: backspace | select  (delay_ms ignored when strategy=select)
Ghostty 15 backspace
Visual Studio Code 0 select
```

- Column 1: app name or bundle id, matched the same dual way as
  `blacklist_contains` (name OR bundle id).
- Column 2: `delay_ms`, non-negative integer, converted to microseconds at
  parse time. Ignored when column 3 is `select` (still required
  syntactically; any value is accepted and unused).
- Column 3: `backspace` or `select`.
- Lines starting with `#`, and blank lines, are skipped.
- Apps with no matching row: current defaults unchanged (`5ms`, `backspace`).
- Malformed rows (wrong field count, non-numeric delay, unrecognized
  strategy keyword): the entire row is skipped (not added to `overrides` at
  all), same leniency as `load_string_list` — see Error handling #3 for why
  this isn't instead coerced to a partial/default row.
- Duplicate app rows: first match wins (lookup returns on first hit, so
  earlier rows in the file shadow later ones for the same app).

## Error handling / edge cases

1. **Missing file:** `overrides_count == 0`, every lookup falls through to
   defaults — identical to svim's current (pre-feature) behavior.
2. **Malformed row:** skipped during parse, never added to `overrides` — same
   failure mode as an app simply not being listed.
3. **Unknown strategy keyword:** treated as a malformed row (skip), rather
   than silently defaulting that one row to `backspace` — avoids a
   config typo silently discarding an intended `select` override without
   any signal.
4. **`select` strategy correctness assumption:** relies on the OS-level
   text-editing convention that typing while a selection is active replaces
   it. This is standard behavior in virtually every native text field, but
   unverified so far against Ghostty/terminal-style apps specifically (which
   may not implement the same selection-replace-on-type convention for a
   `Shift+Left`-created selection coming from synthetic events) — flagged
   here so a `select`-strategy test in Ghostty is the first thing to try
   after implementation, before recommending it as the default fix for any
   specific app.

## Testing

1. **`test/test_vn_overrides.c`** (standalone, assert-based, no framework —
   same style as `test_string_list.c` / `test_vn_input.c`): write a temp
   `vn_overrides`-formatted file, load it, and assert:
   - a row matched by app name resolves the right `delay_us`/`strategy`,
   - a row matched by bundle id resolves the same way,
   - an app with no matching row resolves to the defaults
     (`5000`/`VN_STRATEGY_BACKSPACE`),
   - a malformed row (missing column, bad strategy keyword) is skipped and
     doesn't affect lookups for other apps in the same file,
   - a missing file resolves every lookup to defaults (`overrides_count == 0`).
2. **Not unit-tested:** the actual `Shift+Left` vs `Delete` posting behavior
   and the delay-skip-on-select behavior inside `vn_post_correction` — no
   different from the existing gap in coverage for `CGEventPost`/AX
   interaction with real apps. Verified by hand: configure `select` for
   Ghostty, retype the same stress-test words from this session's debugging
   log (`thấy`, `lỗi`, `nhiều`), and confirm no regression versus the current
   `backspace` strategy before treating `select` as a real fix for any app.
