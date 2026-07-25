# Homebrew tap Formula for UniVim — Design

## Goal

Write a Homebrew Formula for UniVim so it can be installed/managed via
`brew install`/`brew services`, matching how upstream SketchyVim is
distributed, while actually using the `.app` bundle (from the earlier icon
work) so the icon shows in Activity Monitor/Force Quit when running as a
service — not just when opened manually.

## Non-goals

- Not creating the tap repository itself (`homebrew-univim` on GitHub) —
  the user creates and owns that; this only produces the `Formula/univim.rb`
  file to drop into it.
- Not adding a stable versioned install path yet (no git tag exists) —
  `head`-only for now. Adding the `url`/`sha256` stable block is a small,
  separate follow-up once a tag exists.
- Not changing the currently-running live service or touching
  `~/.config/svim/` — installing via this Formula is an independent,
  new install path the user opts into whenever ready.

## Components

### `Formula/univim.rb` (new, in this repo)

```ruby
class Univim < Formula
  desc "Vim-mode for macOS text fields, plus Vietnamese Telex/VNI input"
  homepage "https://github.com/flyznex/univim"
  head "https://github.com/flyznex/univim.git", branch: "master"
  depends_on :macos

  def install
    system "git", "submodule", "update", "--init", "--recursive"
    system "make", "lib"
    system "make", "app"
    libexec.install "bin/UniVim.app"
    bin.install_symlink libexec/"UniVim.app/Contents/MacOS/univim"
  end

  service do
    run [opt_libexec/"UniVim.app/Contents/MacOS/univim"]
    keep_alive true
    log_path var/"log/univim.log"
    error_log_path var/"log/univim.log"
  end

  test do
    assert_path_exists libexec/"UniVim.app/Contents/MacOS/univim"
  end
end
```
(`brew style` clean -- `desc` shortened to fit the 80-char limit,
`assert_path_exists` used per `FormulaAudit/AssertStatements`.)

- `git submodule update --init --recursive`: `libvim` is a submodule built
  from source (`make lib` compiles it into `lib/libvim.a`); `libunikey.a`
  is already vendored as a prebuilt static library committed directly to
  the repo, so no separate build step is needed for it.
- `libexec.install` (not `bin.install`): the `.app` bundle isn't a bare CLI
  tool, so it goes in `libexec` per Homebrew convention for
  non-user-invoked support files; `bin.install_symlink` still gives a
  `univim` command on PATH for anyone who wants to run it directly
  (debugging, manual foreground run) pointing at the same binary inside
  the bundle.
- `service do` block: `run` points at the binary path *inside* the
  installed `.app` bundle (`opt_libexec/"UniVim.app/Contents/MacOS/univim"`),
  not a bare copied binary — this is what makes the icon actually show up
  in Activity Monitor/Force Quit when launched via `brew services start
  univim`, fulfilling the point of the earlier icon work.
- `head`-only: no stable `url`/`sha256` block yet since no tag exists.
  Install with `brew install --HEAD <tap>/univim` for now; `brew install
  univim` (no `--HEAD`) won't work until a stable block is added.

## Error handling / edge cases

- Accessibility permission is tied to the installed binary's path/identity.
  Every `brew upgrade` creates a new Cellar version directory, so the
  `.app`'s effective path changes on each upgrade — Accessibility will need
  re-granting after upgrades. This is inherent to how Homebrew versions
  installs, not specific to bundling the `.app` (a bare-binary Formula
  would have the identical issue), and isn't something the Formula can
  paper over.
- `brew services` expects the `service` block's `run` command to stay in
  the foreground (not daemonize itself) — matches how `univim`/`svim`
  already runs today (confirmed by the existing upstream SketchyVim Formula
  using the same pattern).

## Testing

- Not logic-testable. Verify once the user has created the tap repo and
  copied `Formula/univim.rb` into it:
  1. `brew install --HEAD <tap>/univim` completes (submodule fetch + `make
     lib` + `make app` all succeed from a clean clone).
  2. `brew services start univim` launches it, Activity Monitor shows the
     UniVim icon next to the running process.
  3. Existing functionality (vim-mode, VN input) works identically to the
     manually-built binary.

## Follow-up (not part of this spec)

- Once the user creates a git tag for a release, add a stable `url`
  (pointing at the tag's tarball) + `sha256` block to the Formula so plain
  `brew install` (without `--HEAD`) works too.
