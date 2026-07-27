#include "codesign_selfheal.h"

#include <limits.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STABLE_IDENTITY "univim-cert"
#define REEXEC_GUARD_ENV "UNIVIM_RESIGN_ATTEMPTED"

bool codesign_selfheal_bundle_path(const char* resolved_exe_path, char* bundle_path, size_t bundle_path_size) {
  const char* macos_dir = strstr(resolved_exe_path, ".app/Contents/MacOS/");
  if (!macos_dir) return false;

  size_t bundle_len = (size_t)(macos_dir - resolved_exe_path) + 4; // + ".app"
  snprintf(bundle_path, bundle_path_size, "%.*s", (int)bundle_len, resolved_exe_path);
  return true;
}

// Ad-hoc signing (the linker's default, and what every `brew
// reinstall`/`upgrade` produces) hashes the binary's own bytes, so each
// rebuild looks like a "different app" to TCC and Accessibility has to be
// re-granted by hand every time (remove the app in System Settings,
// re-drag it in). Re-signing with a stable per-machine identity before the
// app ever asks for Accessibility trust means the grant survives rebuilds.
// This runs from main() -- the one entry point every launch path shares
// (raw binary, `bin/univim` symlink, `brew services`) -- so it's a single
// fix instead of one per caller. Homebrew's build sandbox blocks doing
// this at install time (can't write to the real login keychain), but
// there's no sandbox once the app is actually running.
void codesign_selfheal_relaunch_if_needed(int argc, char* argv[]) {
  (void)argc;
  if (getenv(REEXEC_GUARD_ENV)) return; // already tried once in this process tree -- don't loop

  char exe_path[PATH_MAX];
  uint32_t size = sizeof(exe_path);
  if (_NSGetExecutablePath(exe_path, &size) != 0) return; // path didn't fit -- bail quietly, not worth a crash

  char resolved[PATH_MAX];
  if (!realpath(exe_path, resolved)) return;

  // resolved looks like ".../UniVim.app/Contents/MacOS/univim" -- find the
  // bundle root. Not running from inside a bundle (e.g. `make sign`'s
  // plain bin/univim) -- nothing to self-heal, just run.
  char bundle_path[PATH_MAX];
  if (!codesign_selfheal_bundle_path(resolved, bundle_path, sizeof(bundle_path))) return;

  char check_cmd[PATH_MAX + 128];
  snprintf(check_cmd, sizeof(check_cmd),
           "codesign -d --verbose=4 \"%s\" 2>&1 | grep -q '^Authority=" STABLE_IDENTITY "$'",
           bundle_path);
  if (system(check_cmd) == 0) return; // already signed with the stable identity -- nothing to do

  char sign_cmd[PATH_MAX * 2 + 128];
  snprintf(sign_cmd, sizeof(sign_cmd),
           "\"%s/Contents/Resources/ensure_codesign_cert.sh\" " STABLE_IDENTITY
           " && codesign --force --sign " STABLE_IDENTITY " \"%s\"",
           bundle_path, bundle_path);
  if (system(sign_cmd) != 0) return; // couldn't sign (e.g. keychain locked) -- keep running ad-hoc rather than get stuck

  setenv(REEXEC_GUARD_ENV, "1", 1); // inherited across execv -- guards the relaunch below
  execv(resolved, argv); // relaunch as the freshly-signed binary before Accessibility is ever requested
  _exit(1); // only reached if execv itself failed
}
