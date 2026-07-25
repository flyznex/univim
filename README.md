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

## Vietnamese Input (Telex/VNI)
UniVim also includes a system-wide Vietnamese input method (Telex and VNI),
independent of the vim-mode feature above.

Toggle it on/off with a hotkey (default `control+shift`). Configure it via
`~/.config/univim/vn_config` (plain `key=value` lines):
```
method=telex        # telex (default) or vni
hotkey=control+shift
debug=1             # logs routing/correction decisions to ~/.config/univim/vn_debug.log
```

`~/.config/univim/vn_blacklist` excludes apps from Vietnamese input entirely
(same one-app-or-bundle-id-per-line format as `blacklist` above).

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
