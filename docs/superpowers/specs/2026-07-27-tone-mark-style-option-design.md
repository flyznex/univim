# Tone-mark placement style option (modern vs. old) — Design

## Goal

Expose libunikey's existing modern/old Vietnamese tone-mark placement rule
(see [Wikipedia: Quy tắc đặt dấu thanh của chữ Quốc ngữ](https://vi.wikipedia.org/wiki/Quy_t%E1%BA%AFc_%C4%91%E1%BA%B7t_d%E1%BA%A5u_thanh_c%E1%BB%A7a_ch%E1%BB%AF_Qu%E1%BB%91c_ng%E1%BB%AF))
as a user-configurable `vn_config` option, defaulting to the modern style
(e.g. `hoà`) rather than libunikey's own default of the old style
(e.g. `hòa`).

## Background

libunikey already implements both placement rules internally via
`UnikeyOptions.modernStyle` (`libunikey/src/keycons.h`), consumed by the rule
engine at `libunikey/src/ukengine.cpp:547`. `CreateDefaultUnikeyOptions()`
(`libunikey/src/unikey.cpp:101`) sets `modernStyle = 0` (old style) as
libunikey's built-in default. svim's wrapper (`src/vn_engine.c`) never calls
`UnikeySetOptions`/reads this field, so svim has always silently typed
old-style placement with no way to change it. This is a wiring task only —
no changes inside the `libunikey` submodule.

## Non-goals

- No per-app override (unlike `vn_overrides`) — one global setting, applied
  uniformly.
- No distinct behavior per input method — `modernStyle` applies the same way
  regardless of Telex/VNI/SimpleTelex, matching libunikey's own model.
- Not re-evaluating the vi-rs-vs-libunikey decision
  ([2026-07-25 evaluation](2026-07-25-vi-rs-vs-libunikey-evaluation-design.md))
  — this ships the equivalent capability that doc noted vi-rs exposes and
  libunikey's wrapper didn't, without switching engines.

## Components

- **`src/vn_input.h`** — add `bool modern_style;` to `struct vn_input`.
- **`src/vn_input.c` (`vn_config_load`)** — parse a new `modern_style` key,
  accepting `1`/`0`/`on`/`off` (same convention as the existing `debug` key).
  Default `true` (modern style) on fresh load; preserved across reload if
  parsing fails, same pattern as `method`/`hotkey`/`debug`.
- **`src/vn_engine.h`/`src/vn_engine.c`** — add
  `void vn_engine_set_tone_style(bool modern)`:
  ```c
  UnikeyOptions opt;
  UnikeyGetOptions(&opt);
  opt.modernStyle = modern ? 1 : 0;
  UnikeySetOptions(&opt);
  ```
- **`src/vn_input.c` (`vn_input_begin`)** — after `vn_engine_init(vn->method)`,
  call `vn_engine_set_tone_style(vn->modern_style)` to override libunikey's
  built-in old-style default with svim's modern-style default (or whatever
  `vn_config` specifies).
- **`src/vn_input.c` (`vn_input_reload_config`)** — mirror the existing
  `old_method`/`vn->method` diff check: capture `old_modern_style` before
  `vn_config_load`, call `vn_engine_set_tone_style(vn->modern_style)` if it
  changed after reload.

## Config format

`~/.config/univim/vn_config`:

```
modern_style=1      # 1/on (default, e.g. "hoà") or 0/off (old style, e.g. "hòa")
```

Documented in `README.md` alongside the existing `method=`/`hotkey=`/`debug=`
lines.

## Testing

Extend `test/test_vn_engine.c` with a check that types the same Telex
sequence (a word whose tone mark lands on a different vowel under each rule,
e.g. a Telex sequence composing to "hoà"/"hòa") once with
`vn_engine_set_tone_style(true)` and once with `false`, asserting the two
outputs differ in tone-mark position — this only verifies svim's wiring is
reaching libunikey's option, not libunikey's own rule correctness (already
trusted per the prior engine evaluation).

## Rollout

No migration concerns: existing installs with no `modern_style` line in
`vn_config` get the new modern-style default (a visible behavior change from
today's implicit old-style output), consistent with the user's request to
default to modern. Old-style typists set `modern_style=0`.
