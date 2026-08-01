#include "input_source.h"
#include <Carbon/Carbon.h>

bool input_source_is_composing_ime(void) {
  TISInputSourceRef src = TISCopyCurrentKeyboardInputSource();
  if (!src) return false; // no source info -> don't suppress

  bool is_ime = true; // default: treat unknown as IME (suppress-on-doubt)
  CFStringRef type = (CFStringRef)TISGetInputSourceProperty(src, kTISPropertyInputSourceType);
  if (type && CFStringCompare(type, kTISTypeKeyboardLayout, 0) == kCFCompareEqualTo) {
    is_ime = false; // confirmed plain keyboard layout -> UniVim active
  }
  CFRelease(src);
  return is_ime;
}
