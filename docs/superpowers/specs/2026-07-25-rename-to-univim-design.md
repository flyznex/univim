# Rename fork to UniVim — Design

## Goal

Rebrand this fork (which adds a full Vietnamese Telex/VNI input method on
top of upstream SketchyVim) as **UniVim**, with the binary, config
directory, and README all reflecting the new name and giving proper credit
to the projects this fork builds on.

## Non-goals

- Not setting up a new Homebrew tap/formula for UniVim — no tap exists yet;
  installation instructions move to build-from-source instead.
- Not touching vim-mode/AX/libvim behavior — this is a naming and
  documentation change, not a feature change.

## Scope

### Binary & build (`makefile`)
- `bin/svim` → `bin/univim` throughout (build target, `svim_x86`/`svim_arm64`
  universal-binary intermediates, `cp $(ODIR)/svim bundle/`).
- Codesign identity `svim-cert` → `univim-cert` (user will create the
  matching `univim-cert` identity in Keychain Access separately; not part
  of this repo change).

### Config path (`src/buffer.c`, `src/event_tap.c`, `src/vn_input.c`,
`src/vn_input.h`)
- Every hardcoded `~/.config/svim/...` path (blacklist, svimrc, svim.sh,
  vn_config, vn_blacklist, vn_overrides, vn_debug.log) → `~/.config/univim/...`.
- `examples/vn_overrides`: update any svim-specific path references in
  comments.

### Local migration (this machine only, not part of the repo)
- Copy `~/.config/svim/` → `~/.config/univim/` so the current vn_config,
  vn_overrides, and blacklist keep working under the new path.
- Stop the existing `homebrew.mxcl.svim` brew service (it points at the old
  Cellar binary/label) and run the newly built `univim` binary via its own
  mechanism (a manual launchd plist, since no brew formula exists for it
  yet) instead of continuing to overwrite the `svim`-labeled Cellar binary.

### README
- Title: `UniVim`.
- Rewritten to neutral third-person voice throughout (was first-person as
  Felix Kratz: "my tap", "my green", "my SketchyBar").
- New opening note: this is a fork of
  [FelixKratz/SketchyVim](https://github.com/FelixKratz/SketchyVim), extended
  with a system-wide Vietnamese Telex/VNI input method.
- Installation section rewritten as build-from-source (`git clone` + `make`),
  replacing the `brew tap FelixKratz/formulae` instructions (no UniVim tap
  exists).
- Credits section gains:
  - Felix Kratz, original author of SketchyVim.
  - Phạm Kim Long, original author of Unikey (the Vietnamese input engine
    this fork's Telex/VNI support is built on).
  - [inteplus/libunikey](https://github.com/inteplus/libunikey), the specific
    vendored repo this fork's `lib/libunikey` submodule is sourced from.
- All other content (vim-mode usage, per-app VN correction tuning, known
  issues) carries over with `svim` → `univim` path updates only, no
  behavioral rewording.

## Testing

- Not applicable (documentation + naming change, no logic touched). Verify
  by grepping for any remaining `svim` string in source/config paths after
  the rename and confirming the rebuilt `univim` binary runs against the
  migrated `~/.config/univim/` directory.
