#pragma once
#include <stdbool.h>

// True when the current keyboard input source is a composing IME (its
// kTISPropertyInputSourceType is anything other than kTISTypeKeyboardLayout,
// e.g. Korean/Japanese/Chinese input modes). False for plain keyboard layouts
// (US, AZERTY, QWERTZ, Vietnamese layouts) and when the current source cannot
// be determined -- in doubt about "layout", suppress; but with no source info
// at all, stay active (preserve pre-feature behavior).
//
// Reads TISCopyCurrentKeyboardInputSource(); intended to be called on
// input-source-change notifications and once at startup, NOT per keystroke.
bool input_source_is_composing_ime(void);
