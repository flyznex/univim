# SketchyVim
This small project turns accessible(!) input fields on macOS into full vim
buffers. It should behave and feel like native vim, because, under the hood
I synchronize the text field with a real vim buffer.

![demo](https://user-images.githubusercontent.com/22680421/153753171-e818d40b-4d72-4b88-9719-d1e36d16dec0.gif)

You can use all modes (even commandline etc.) and all commands included in vim.

It is also possible to load a custom `svimrc` file, which can contain
custom vim configurations, e.g. remappings (see the examples folder).

Additionally, you can edit the `blacklist` file in the `~/.config/svim/` folder
to manually exclude applications from being handled by svim.
You will likely want to blacklist your terminal emulator and gvim, such that there
is no conflict.

Every time the vim mode changes, or a commandline update is issued, the script
`svim.sh` in the folder `~/.config/svim/` is executed where you can handle 
how you want to process this information. I have a small popup in my [SketchyBar](https://github.com/FelixKratz/SketchyBar)
which shows me the command line output on demand for example.

(!): Accessible means, that the input field needs to conform to the accessibility
     standards for text input fields, else there is nothing we can do.

## Vietnamese Input (Telex/VNI)
svim also includes a system-wide Vietnamese input method (Telex and VNI),
independent of the vim-mode feature above.

Toggle it on/off with a hotkey (default `control+shift`). Configure it via
`~/.config/svim/vn_config` (plain `key=value` lines):
```
method=telex        # telex (default) or vni
hotkey=control+shift
debug=1             # logs routing/correction decisions to ~/.config/svim/vn_debug.log
```

`~/.config/svim/vn_blacklist` excludes apps from Vietnamese input entirely
(same one-app-or-bundle-id-per-line format as `blacklist` above).

### Per-app correction tuning
Some apps need a different delay or correction strategy than the defaults
(5ms, backspace-based deletion). Configure this per app in
`~/.config/svim/vn_overrides` (see `examples/vn_overrides`):
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
  changes require a restart (`brew services restart svim`) to take effect.

## Installation
You can install this using brew from my tap:
```bash
brew tap FelixKratz/formulae
brew install svim
```
and then you can start the brew service using:
```
brew services start svim
```
where you will be asked to grant accessibility permissions.

You can change the macOS selection color to anything you like with this command (which is my green):
```bash
defaults write NSGlobalDomain AppleHighlightColor -string "0.615686 0.823529 0.454902"
```

## Issues
Please tell me if you encounter issues.

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
a pull request, such that all people (including me) can enjoy those improvements.

## Credits
* I use the libvim library which is a compact and minimal c library for the vim core.
* Many prior projects tried to accomplish a similar vision by rebuilding the vim
  movements by hand, those have inspired me to create this project.
