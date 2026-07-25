# Package univim as a minimal .app bundle with an icon — Design

## Goal

Give UniVim a proper icon, visible in Activity Monitor/Force Quit, using
the icon set already added to `icons/`. No Dock icon or app-switcher
presence — stays a background daemon in behavior, just no longer icon-less.

## Non-goals

- Not changing the currently-running live service (still the raw `svim`
  binary, per the earlier deferred decision on brew-tap vs. manual
  launchd) — this only produces a new `.app` build artifact.
- Not adding a Dock icon, menu bar item, or any other UI surface. The
  `menubar_uv@2x.png` asset in `icons/` is not used by this change — no
  menu bar UI exists yet to use it.
- Not changing runtime behavior/performance — bundling only changes where
  the same compiled binary lives on disk and what static metadata sits
  next to it (Info.plist, icon file); nothing the process itself loads at
  runtime.

## Components

### `icons/uv.iconset/` (new, derived from existing `icons/*.png`)
Standard 10-file Apple iconset, built from the 7 files already present
plus 3 duplicates per `icons/README.txt`'s own instructions:
- `icon_16x16.png`, `icon_16x16@2x.png` (= copy of `icon_32x32.png`)
- `icon_32x32.png`, `icon_32x32@2x.png`
- `icon_128x128.png`, `icon_128x128@2x.png` (= copy of `icon_256x256.png`)
- `icon_256x256.png`, `icon_256x256@2x.png` (= copy of `icon_512x512.png`)
- `icon_512x512.png`, `icon_512x512@2x.png`

Built into `icons/uv.icns` via `iconutil -c icns`.

### `makefile`: new `app` target
Assembles `bin/UniVim.app/Contents/{MacOS,Resources}`:
- Depends on `bin/univim` (existing build).
- Copies `bin/univim` → `Contents/MacOS/univim`.
- Copies `icons/uv.icns` → `Contents/Resources/uv.icns`.
- Generates `Contents/Info.plist` (`CFBundleExecutable=univim`,
  `CFBundleIconFile=uv`, `CFBundleIdentifier=org.univim.univim`,
  `CFBundlePackageType=APPL`, `LSUIElement=true`).
- Does not touch the existing `bundle` target (that one produces a
  distribution tarball of the raw binary + examples — unrelated, kept
  as-is).

## Error handling / edge cases

- Accessibility permission is tied to the running binary's identity —
  since `UniVim.app/Contents/MacOS/univim` is a different path/bundle
  identity than the currently-running raw binary, it will need its own
  Accessibility grant the first time it's actually run. Not handled by
  this change (it only builds the artifact; running/granting is a
  separate, later step, same as the earlier deferred service migration).

## Testing

- Not logic-testable — verify by building (`make app`), confirming
  `bin/UniVim.app` has the expected structure (`Contents/MacOS/univim`,
  `Contents/Resources/uv.icns`, valid `Contents/Info.plist`), and that
  Finder shows the custom icon on the bundle.
