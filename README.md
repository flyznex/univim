# UniVim

<p align="center">
  <img src="icons/icon_128x128.png" width="128" height="128" alt="UniVim icon">
</p>

UniVim is a fork of [FelixKratz/SketchyVim](https://github.com/FelixKratz/SketchyVim),
extended with a system-wide Vietnamese Telex/VNI input method on top of the
original vim-mode feature.

This small project turns accessible(!) input fields on macOS into full vim
buffers. It should behave and feel like native vim, because, under the hood
the text field is synchronized with a real vim buffer.

![demo](https://user-images.githubusercontent.com/22680421/153753171-e818d40b-4d72-4b88-9719-d1e36d16dec0.gif)

You can use all modes (even commandline etc.) and all commands included in vim.

It is also possible to load a custom `svimrc` file, which can contain
custom vim configurations, e.g. remappings (see the examples folder).

Additionally, you can edit the `blacklist` file in the `~/.config/univim/` folder
to manually exclude applications from being handled by UniVim.
You will likely want to blacklist your terminal emulator and gvim, such that there
is no conflict.

Every time the vim mode changes, or a commandline update is issued, the script
`svim.sh` in the folder `~/.config/univim/` is executed where the output can be
handled however you like -- e.g. piping the command line output into a
[SketchyBar](https://github.com/FelixKratz/SketchyBar) popup on demand.

(!): Accessible means, that the input field needs to conform to the accessibility
     standards for text input fields, else there is nothing UniVim can do.

## Temporarily disabling vim mode
Sometimes you want the Vietnamese IME to stay on but vim-mode to get out of
the way -- e.g. when a real vim or terminal is running *inside* another
(accessible) app, where UniVim's buffer sync fights the app's own editing.
Bind `disable_vim_hotkey=` in `~/.config/univim/vn_config` to a hotkey that
toggles vim buffer processing off and back on, without touching the IME:
```
disable_vim_hotkey=control+option+v
```
`disable_vim_hotkey=` has no default -- the feature stays inert until you set
it. It takes the exact same syntax as `hotkey=` above (`+`-joined
`control`/`shift`/`command`/`option`, optionally plus one regular key). Bind
it to a **different** combo than `hotkey=`, or both toggles would fire on the
same keystroke.

While vim mode is disabled the state is shown in two places: a `Vim-` toast
on toggle-off (`Vim+` on toggle-on), and a trailing `-` on the menubar label
(`VI-` / `EN-`). The toggle is session-only -- it resets to enabled when
UniVim restarts, and is never written to disk. The Vietnamese IME is
completely unaffected: it keeps composing while vim mode is off.

## Vietnamese Input (Telex/VNI)
UniVim also includes a system-wide Vietnamese input method (Telex and VNI),
independent of the vim-mode feature above.

Toggle it on/off with a hotkey (default `control+shift`). Configure it via
`~/.config/univim/vn_config` (plain `key=value` lines):
```
method=simpletelex  # telex, simpletelex (default), or vni
hotkey=control+shift
restore_trigger_key=z  # opt-in; typing z right after a word reverts it to
                        # the literal keys typed, if libunikey actually
                        # transformed it -- otherwise z types normally
debug=1             # logs routing/correction decisions to ~/.config/univim/vn_debug.log
modern_style=1      # 1/on (default, e.g. "hoà") or 0/off (old style, e.g. "hòa")
```
`hotkey=` is `+`-joined modifiers (`control`/`shift`/`command`/`option`),
optionally plus exactly one regular key -- `space`, a single letter `a`-`z`,
or a single digit `0`-`9` -- e.g. `hotkey=control+space` or
`hotkey=command+option+j`. A regular key always needs at least one
modifier alongside it (a bare `hotkey=space` would swallow every Space
keystroke app-wide, so it's rejected as invalid instead).

`restore_trigger_key=` is disabled unless set (no default) — a single
regular key (same table as `hotkey=`'s regular-key component: `space`,
`a`-`z`, `0`-`9`), no modifier. `z` is the only recommended value: it's the
one letter that never appears in real Vietnamese text, so an unmodified
press of it only ever reverts a word libunikey actually transformed (e.g.
"of" → "ò", then `z` → "of") and otherwise types out literally (e.g.
"quiz", "jazz" type unchanged, since neither ever triggers a transformation
to revert). Cmd/Ctrl/Option held alongside the key (e.g. Cmd+Z / Ctrl+Z
undo) is never treated as this trigger.

Some combos may already be claimed by macOS or another app before the
keystroke ever reaches UniVim -- e.g. `control+space` is macOS's own
default shortcut for switching input sources (System Settings > Keyboard >
Keyboard Shortcuts > Input Sources). If a hotkey silently does nothing,
check there (and any other app you know binds global shortcuts) before
assuming UniVim is broken.

`simpletelex` is libunikey's `UkSimpleTelex` input method -- a typing-feel
preference, not a different rule set (confirmed identical composition
output to `telex` on every case tested).

`modern_style` defaults to modern tone-mark placement -- a behavior change
from earlier versions, which always typed old style with no way to
switch. Set `modern_style=0` to keep the old-style output you're used to.

`~/.config/univim/vn_blacklist` excludes apps from Vietnamese input entirely
(same one-app-or-bundle-id-per-line format as `blacklist` above).

### Coexisting with other system IMEs
If you also use a composing system input method (Korean, Japanese, Chinese,
...), UniVim now steps aside automatically while that IME is the active
keyboard input source, so its keystrokes reach the IME untouched instead of
being double-processed. No configuration is needed -- it's on by default and
requires no entry in `vn_blacklist`.

The rule is deliberately conservative: UniVim only stays active for a
confirmed plain keyboard *layout* (US, AZERTY, a Vietnamese layout, ...).
Anything that looks like a composing IME -- or any input source it can't
positively identify as a layout -- suppresses Vietnamese input for the
duration, on the safe assumption the IME owns the keystroke. Switching input
source is detected the moment it happens (via the system input-source-changed
notification), and any half-composed Vietnamese word is reset on the switch
so nothing leaks across it. Switch back to a plain layout and Vietnamese
input resumes on the next keystroke.

Note this keys off the *input source*, not the app: a Vietnamese *layout*
keeps UniVim active, but selecting a third-party Vietnamese *IME* as your
input source would also read as "a composing IME owns the keystroke" and
suppress UniVim -- which is the intended behavior, since you'd be using that
IME instead.

### Text-shortcut expansion ("gõ tắt")
Define typing shortcuts that expand automatically, e.g. `sdt` → `số điện thoại`,
in `~/.config/univim/vn_macros` (see `examples/vn_macros`):
```
sdt:số điện thoại
vn:Việt Nam
```
One `key:text` shortcut per line, plain UTF-8. Active automatically whenever
this file exists. A shortcut expands when followed by a word-break key
(space, punctuation, ...); it only matches as a whole word.

### VimL scripting in svimrc
`svimrc` isn't limited to simple `:map`/`:set` one-liners — it's sourced
through libvim's real VimL engine, so `:function`, `:let`, control flow,
and built-ins like `expand('<cword>')` all work.

One rule, confirmed by hand-testing: for a mapping that calls a function,
use an `<expr>` mapping that returns a keystring, e.g.
```vim
nnoremap <expr> <C-t> MyFunc()
```
**not** `nnoremap <C-t> :call MyFunc()<CR>`. The latter enters real
command-line mode and leaves it within a single keystroke, which visibly
corrupts this app's mode/cursor sync (cursor shape, vanishing text, dead
Escape). `<expr>` mappings never enter command-line mode at all, so they
don't hit this.

See `examples/svimrc_toggle` for a full worked example: cycling the word
under the cursor through related values (`true`/`false`, `on`/`off`,
`public`/`protected`/`private`, ...), [toggle.nvim](https://github.com/leblocks/toggle.nvim)-style.
Save your config to `~/.config/univim/svimrc` for it to take effect.

Also note: comments (lines starting with `"`) in `svimrc` break the config (see Known Issues)
— keep scripted `svimrc` content comment-free. Double-quoted string literals like `"\<Esc>"` are fine; the issue is specifically `"` used as a comment-leader, not `"` appearing in regular VimL code.

### Per-app correction tuning
Some apps need a different delay or correction strategy than the defaults
(5ms, backspace-based deletion). Configure this per app in
`~/.config/univim/vn_overrides` (see `examples/vn_overrides`):
```
# AppName delay_ms strategy
Ghostty 15 backspace
Visual Studio Code 0 select
```
* `strategy` is `backspace` (default) or `select`. `select` selects the
  characters being corrected (Shift+Left) and types over the selection
  instead of deleting one character at a time -- this only works in apps
  with a real, keyboard-selectable text field (GUI editors/IDEs).
  Terminal emulators generally don't support keyboard-driven selection at
  all, so `select` will corrupt text there instead of fixing it; use
  `backspace` for terminals.
* `delay_ms` is ignored when `strategy` is `select`.
* Apps not listed use the defaults. Like the other config files above,
  changes require a restart of UniVim to take effect.

## Installation

### Via Homebrew (recommended)
```bash
brew tap flyznex/taproom
brew install --HEAD flyznex/taproom/univim
brew services start flyznex/taproom/univim
```
No stable release tag exists yet, so `--HEAD` is required (builds from the
`master` branch). The first run will ask you to grant Accessibility
permissions to `UniVim.app` (found via `System Settings > Privacy &
Security > Accessibility`, path `$(brew --prefix)/opt/univim/libexec/UniVim.app`).

`UniVim.app` is ad-hoc signed by default (the linker's default, and what
every `brew reinstall`/`brew upgrade` produces), which would normally make
macOS treat every rebuild as a "different app" and force Accessibility
permission to be re-granted each time. UniVim re-signs itself with a
stable per-machine identity on its own first launch after any
rebuild, so Accessibility only needs granting once per machine -- no
manual command needed.

### Build from source
```bash
git clone https://github.com/flyznex/univim.git univim
cd univim
make
```
This produces `bin/univim` (`lib/libvim.a`/`lib/libunikey.a` are already
committed prebuilt, so no submodule checkout or extra build step is
needed for a normal build). Run it directly, or set up your own launchd
job/login item to keep it running in the background; the first run will
ask you to grant accessibility permissions.

You can change the macOS selection color to anything you like with this command:
```bash
defaults write NSGlobalDomain AppleHighlightColor -string "0.615686 0.823529 0.454902"
```

## Issues
Please open an issue if you encounter problems.

Known Issues:
-------------
* Multikey remappings are not recognized (e.g. jk for esc)
* Some text fields break the accessibility api and this leads to bugs,
  be sure to blacklist all apps that are affected by this.
  Sometimes it helps to switch to a "raw" or "markdown" editing mode on websites,
  such that there is no interference.
  Generally, Safari seems to make most text fields available, while Firefox does not.
* Comments in svimrc break the config (#18)

## Contributions
Pull requests are welcome. If you improve the code for your own use, consider creating
a pull request, such that everyone can enjoy those improvements.

## Credits
* [Felix Kratz](https://github.com/FelixKratz), original author of
  [SketchyVim](https://github.com/FelixKratz/SketchyVim), the project this
  fork is based on.
* The [libvim](https://github.com/FelixKratz/libvim) library, a compact and
  minimal c library for the vim core.
* Phạm Kim Long, original author of
  [Unikey](https://www.unikey.org/), the Vietnamese input engine this fork's
  Telex/VNI support is built on.
* [inteplus/libunikey](https://github.com/inteplus/libunikey), the vendored
  library source this fork's Vietnamese input method uses.
* Many prior projects tried to accomplish a similar vision by rebuilding the vim
  movements by hand, those have inspired the creation of this project.
