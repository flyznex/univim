VERSION = "1.0.11"
CC = clang
DEFINES = -DHAVE_CONFIG_H -DMACOS_X -DMACOS_X_DARWIN # -DMANUAL_AX -DGUI_MOVES
LIBS = lib/libvim.a lib/libunikey.a -lm -lncurses -liconv -lc++ -framework Carbon -framework Cocoa
WARN_FLAGS = -Wall -Wno-array-bounds \
	     -Wno-unknown-warning-option \
	     -Wno-cpp -Wno-pointer-sign \
	     -Wno-unused-parameter \
	     -Wno-strict-overflow \
	     -Wno-return-type -Werror
CFLAGS = $(WARN_FLAGS) $(DEFINES) -g -Ilib -Ilib/libvim/proto -std=c99 -O2 #-fsanitize=address -fsanitize=undefined
ODIR = bin
SRC = src

_OBJ = helpers.om helpers.o workspace.om event_tap.o ax.o buffer.o line.o env_vars.o vn_engine.o vn_input.o toast.om config_watcher.o codesign_selfheal.o
OBJ = $(patsubst %, $(ODIR)/%, $(_OBJ))

.PHONY: all x86 arm64 universal sign lib clean app sign-app test-selfheal

all: $(ODIR)/univim

# Minimal .app bundle so univim has an icon in Activity Monitor/Force Quit.
# LSUIElement=true keeps it out of the Dock/app switcher -- same background
# daemon behavior as running the bare binary, just no longer icon-less.
# Doesn't touch/replace the `bundle` target above (that one is an unrelated
# distribution tarball of the raw binary).
app: $(ODIR)/univim
	rm -rf $(ODIR)/UniVim.app
	mkdir -p $(ODIR)/UniVim.app/Contents/MacOS
	mkdir -p $(ODIR)/UniVim.app/Contents/Resources
	cp $(ODIR)/univim $(ODIR)/UniVim.app/Contents/MacOS/univim
	cp icons/uv.icns $(ODIR)/UniVim.app/Contents/Resources/uv.icns
	cp scripts/ensure_codesign_cert.sh $(ODIR)/UniVim.app/Contents/Resources/ensure_codesign_cert.sh
	chmod +x $(ODIR)/UniVim.app/Contents/Resources/ensure_codesign_cert.sh
	@printf '%s\n' \
		'<?xml version="1.0" encoding="UTF-8"?>' \
		'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
		'<plist version="1.0">' \
		'<dict>' \
		'	<key>CFBundleExecutable</key>' \
		'	<string>univim</string>' \
		'	<key>CFBundleIconFile</key>' \
		'	<string>uv</string>' \
		'	<key>CFBundleIdentifier</key>' \
		'	<string>org.univim.univim</string>' \
		'	<key>CFBundleName</key>' \
		'	<string>UniVim</string>' \
		'	<key>CFBundlePackageType</key>' \
		'	<string>APPL</string>' \
		'	<key>CFBundleShortVersionString</key>' \
		'	<string>$(VERSION)</string>' \
		'	<key>LSUIElement</key>' \
		'	<true/>' \
		'</dict>' \
		'</plist>' \
		> $(ODIR)/UniVim.app/Contents/Info.plist

# Separate from `app` deliberately: Homebrew's sandboxed build environment
# blocks writing to the real login keychain entirely ("UNIX[Operation not
# permitted]"), so this can't run as part of `install` in the Formula --
# it has to happen after, outside the sandbox (see the Formula's
# post_install). Signs with a local, per-machine self-signed identity
# (auto-created if missing) instead of the linker's default ad-hoc
# signature -- an ad-hoc signature hashes the binary's own bytes, so it
# changes on every rebuild and macOS treats each one as a "different app",
# requiring Accessibility permission to be re-granted every time. A stable
# identity here means it only needs to be granted once per machine,
# surviving rebuilds/reinstalls.
sign-app:
	scripts/ensure_codesign_cert.sh univim-cert
	codesign --force --sign univim-cert $(ODIR)/UniVim.app

test-selfheal:
	$(CC) -std=c99 -Wall -Werror -Isrc src/codesign_selfheal.c src/codesign_selfheal_test.c -o $(ODIR)/test-selfheal
	$(ODIR)/test-selfheal

x86: CFLAGS = $(WARN_FLAGS) $(DEFINES) -g -Ilib -Ilib/libvim/proto -std=c99 -O2 -target x86_64-apple-macos12.0
x86: $(ODIR)/univim
	mv $(ODIR)/univim $(ODIR)/univim_x86
	rm -rf $(ODIR)/*.o
	rm -rf $(ODIR)/*.om

arm64: CFLAGS = $(WARN_FLAGS) $(DEFINES) -g -Ilib -Ilib/libvim/proto -std=c99 -O2 -target arm64-apple-macos12.0
arm64: $(ODIR)/univim
	mv $(ODIR)/univim $(ODIR)/univim_arm64
	rm -rf $(ODIR)/*.o
	rm -rf $(ODIR)/*.om

universal:
	$(MAKE) x86
	$(MAKE) arm64
	lipo -create -output $(ODIR)/univim $(ODIR)/univim_x86 $(ODIR)/univim_arm64

sign:
	$(MAKE) universal
	codesign -fs 'univim-cert' $(ODIR)/univim

bundle: clean
	$(MAKE) sign
	@mkdir bundle
	cp $(ODIR)/univim bundle/
	cp -r examples/ bundle/
	tar -czf bundle_$(VERSION).tgz bundle/
	rm -rf bundle/

lib:
	cd libvim/src/ && make
	cp libvim/src/libvim.a lib/libvim.a

bin/univim: $(SRC)/main.m $(OBJ) | $(ODIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)

$(ODIR)/%.o: $(SRC)/%.c $(SRC)/%.h | $(ODIR)
	$(CC) -c -o $@ $< $(CFLAGS)

$(ODIR)/%.om: $(SRC)/%.m $(SRC)/%.h | $(ODIR)
	$(CC) -c -o $@ $< $(CFLAGS)

$(ODIR):
	mkdir $(ODIR)

.PHONY: clean

clean:
	rm -rf $(ODIR)
