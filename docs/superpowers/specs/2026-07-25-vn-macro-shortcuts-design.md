# Vietnamese text-shortcut expansion ("gõ tắt") — Design

## Goal

Let users define typing shortcuts (e.g. `sdt` → `số điện thoại`) that expand
automatically while typing Vietnamese, by exposing libunikey's existing
macro-table feature (`UnikeyLoadMacroTable` / `UnikeyOptions.macroEnabled`)
rather than writing expansion logic from scratch.

## Non-goals

- Not building a macro editor UI — plain text config file, matching every
  other univim config file.
- Not exposing every `UnikeyOptions` field (`freeMarking`, `modernStyle`,
  `alwaysMacro`, etc.) — only `macroEnabled` is touched; everything else
  keeps whatever libunikey already has configured.
- Not supporting per-app macro overrides in this round — global on/off only,
  matching the "auto-enable if the file exists" model already used for
  `vn_blacklist`/`vn_overrides`.

## Components

### User-facing config: `~/.config/univim/vn_macros`
Plain lines of `key:text` (UTF-8), e.g.:
```
sdt:số điện thoại
vn:Việt Nam
```
No header, no libunikey-specific syntax — users only ever see this format.
Feature is active automatically whenever this file exists; absent file
means macros stay disabled (matches `CreateDefaultUnikeyOptions`'s default).

### `vn_engine_load_macros(const char* path)` (new, in `src/vn_engine.c` /
`src/vn_engine.h`)
1. Open `path` (the user's `vn_macros`). If missing, return immediately —
   macros stay off.
2. Write `~/.config/univim/.vn_macros_compiled`: the required libunikey
   version header line (`DO NOT DELETE THIS LINE*** version=1 ***`,
   matching libunikey's own `writeHeader` format for the UTF-8 macro-table
   version) followed by a verbatim copy of the user's file content.
   Regenerated on every call (i.e. every univim startup) — not a
   temp file, not deleted afterward, so a failed load leaves the actual
   file libunikey tried to read available for inspection.
   - Without this header, libunikey's loader falls back to interpreting
     macro text as VIQR-encoded ASCII instead of UTF-8, silently mangling
     every Vietnamese macro expansion — this is the one detail users must
     not have to know about.
3. Call `UnikeyLoadMacroTable()` on the compiled path. Check its return
   value; on failure, `vn_debug_log` a message naming the compiled file's
   path (already left on disk from step 2 for inspection).
4. `UnikeyGetOptions(&opt)` (read current options, don't assume defaults),
   set `opt.macroEnabled = 1`, `UnikeySetOptions(&opt)` — the only option
   this feature touches.

### Call site: `vn_input_begin` (`src/vn_input.c`)
Right after the existing `vn_engine_init(vn->method)` call — same spot
every other config file (`vn_blacklist`, `vn_overrides`) is loaded from —
build the `~/.config/univim/vn_macros` path the same way those are built,
and call `vn_engine_load_macros(path)`.

## Data flow

Macro expansion is independent of Telex/VNI method and of Flow A/B routing
— it's a layer inside `UnikeyFilter` itself, so no changes are needed to
`vn_synthetic_process`/`ax_process_event`/`vn_post_correction`: whatever
`UnikeyBuf`/`UnikeyBackspaces` produce (now potentially a full expansion
instead of a single accented character) flows through the exact same
existing backspace/insert delivery path.

## Testing

1. **Exploratory first**: before wiring into `vn_input_begin`, use a small
   standalone harness (extending the existing `test_vn_engine.c` pattern)
   to empirically confirm libunikey's actual trigger mechanism — does
   typing `sdt` then a space expand immediately, replacing the shortcut
   text before the space, or does it need some other break key? This isn't
   documented in the headers available in this repo (no vendored `.cpp`
   source), so behavior needs to be observed directly, the same way the
   Telex tone-correction cases were verified earlier this session.
2. Once confirmed, add a regression case to `test/test_vn_engine.c` in the
   same style as the existing checks (`check("telex macro expansion", ...)`),
   using `vn_engine_load_macros` against a small fixture macro file.
