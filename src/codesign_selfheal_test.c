#include "codesign_selfheal.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  char bundle_path[1024];

  assert(codesign_selfheal_bundle_path(
      "/opt/homebrew/Cellar/univim/HEAD/libexec/UniVim.app/Contents/MacOS/univim",
      bundle_path, sizeof(bundle_path)));
  assert(strcmp(bundle_path, "/opt/homebrew/Cellar/univim/HEAD/libexec/UniVim.app") == 0);

  assert(codesign_selfheal_bundle_path(
      "/Users/dev/univim/bin/UniVim.app/Contents/MacOS/univim",
      bundle_path, sizeof(bundle_path)));
  assert(strcmp(bundle_path, "/Users/dev/univim/bin/UniVim.app") == 0);

  // ponytail: no bundle for a raw, non-.app binary (e.g. `make sign`'s bin/univim) -- self-heal has nothing to do
  assert(!codesign_selfheal_bundle_path("/Users/dev/univim/bin/univim", bundle_path, sizeof(bundle_path)));

  printf("codesign_selfheal_bundle_path: OK\n");
  return 0;
}
