#include "vn_engine.h"
#include "libunikey/unikey.h"
#include "libunikey/vnconv.h"
#include <stdio.h>
#include <string.h>

void vn_engine_init(vn_method method) {
  UnikeySetup();
  UnikeySetOutputCharset(CONV_CHARSET_UNIUTF8);
  vn_engine_set_method(method);
}

bool vn_engine_load_macros(const char* path) {
  FILE* src = fopen(path, "r");
  if (!src) return true; // no vn_macros file -- macros stay disabled (default), not an error

  // Build the sibling ".vn_macros_compiled" path from vn_macros's directory.
  char compiled_path[512];
  char* last_slash = strrchr(path, '/');
  if (last_slash) {
    int dir_len = (int) (last_slash - path);
    snprintf(compiled_path, sizeof(compiled_path), "%.*s/.vn_macros_compiled", dir_len, path);
  } else {
    snprintf(compiled_path, sizeof(compiled_path), ".vn_macros_compiled");
  }

  FILE* compiled = fopen(compiled_path, "w");
  if (!compiled) { fclose(src); return false; }

  // libunikey's macro loader reads the first line as a version header; the
  // user-facing vn_macros file intentionally omits it (users just write
  // "key:text" lines) so it's regenerated here on every load. Without this
  // exact header, the loader falls back to interpreting macro text as
  // VIQR-encoded ASCII instead of UTF-8, silently mangling every accented
  // macro expansion (confirmed empirically: "số điện thoại" -> garbage
  // mojibake without it, correct with it).
  fprintf(compiled, "DO NOT DELETE THIS LINE*** version=1 ***\n");

  char line[1040]; // MAX_MACRO_LINE from libunikey/keycons.h
  while (fgets(line, sizeof(line), src)) fputs(line, compiled);
  fclose(src);
  fclose(compiled);

  // Left on disk (not a temp file, not deleted) so a failed load can be
  // inspected afterward instead of vanishing along with the evidence.
  if (!UnikeyLoadMacroTable(compiled_path)) return false;

  UnikeyOptions opt;
  UnikeyGetOptions(&opt);
  opt.macroEnabled = 1;
  UnikeySetOptions(&opt);
  return true;
}

void vn_engine_set_method(vn_method method) {
  // UkSimpleTelex vs UkTelex is a typing-feel preference, not a bugfix
  // (confirmed identical output to UkTelex on every case tested, including
  // the intermittent fast-typing corruption bug this was originally
  // explored for) -- exposed via vn_config's `method=` so it's a user
  // choice instead of hardcoded.
  switch (method) {
    case VN_METHOD_VNI: UnikeySetInputMethod(UkVni); break;
    case VN_METHOD_TELEX: UnikeySetInputMethod(UkTelex); break;
    case VN_METHOD_SIMPLETELEX: default: UnikeySetInputMethod(UkSimpleTelex); break;
  }
}

// libunikey's own CreateDefaultUnikeyOptions() defaults modernStyle to 0
// (old style, e.g. "hòa"); svim defaults to modern style (e.g. "hoà")
// instead via vn_config's `modern_style=`, so callers apply this after
// vn_engine_init to override libunikey's built-in default.
void vn_engine_set_tone_style(bool modern) {
  UnikeyOptions opt;
  UnikeyGetOptions(&opt);
  opt.modernStyle = modern ? 1 : 0;
  UnikeySetOptions(&opt);
}

// libunikey reports UnikeyBackspaces as a count of UTF-8 *bytes* to remove
// from whatever was previously output (confirmed empirically: a plain byte
// subtraction on a byte buffer reproduces every Telex/VNI test case
// correctly). Callers (Flow A's synthetic CGEventPost, Flow B's
// vimKey(BACKSPACE)) each issue one discrete backspace *keystroke* per unit
// of backspace_count, and a keystroke always removes exactly one character,
// not one byte. Left unconverted, correcting a multi-byte character (any
// accented vowel) followed by further plain-ASCII input over-deletes:
// e.g. typing "thaays" ended up as "tấy" because removing "ây"'s 3 bytes
// via 3 character-backspaces also ate the preceding "h".
//
// word_history mirrors the raw UTF-8 bytes of everything output so far for
// the current word (own bookkeeping, not libunikey's internal state), so a
// byte count can be converted into an accurate character count by walking
// backward through bytes *we know we inserted* and counting how many
// complete codepoints (leading, non-continuation bytes) those bytes span.
static unsigned char word_history[256];
static int word_history_len = 0;

static int bytes_to_char_count(int byte_count) {
  int i = word_history_len;
  int consumed = 0;
  int chars = 0;
  while (i > 0 && consumed < byte_count) {
    i--;
    consumed++;
    if ((word_history[i] & 0xC0) != 0x80) chars++;
  }
  return chars;
}

static void word_history_apply(int raw_backspace_bytes, const unsigned char* insert_text, int insert_len) {
  int remove = raw_backspace_bytes;
  if (remove > word_history_len) remove = word_history_len;
  word_history_len -= remove;

  int copy_len = insert_len;
  if (copy_len > 0) {
    if (word_history_len + copy_len > (int) sizeof(word_history))
      copy_len = (int) sizeof(word_history) - word_history_len;
    if (copy_len > 0) {
      memcpy(word_history + word_history_len, insert_text, copy_len);
      word_history_len += copy_len;
    }
  }
}

static int encode_utf8(unsigned int cp, unsigned char* out) {
  if (cp < 0x80) {
    out[0] = (unsigned char) cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (unsigned char) (0xC0 | (cp >> 6));
    out[1] = (unsigned char) (0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (unsigned char) (0xE0 | (cp >> 12));
    out[1] = (unsigned char) (0x80 | ((cp >> 6) & 0x3F));
    out[2] = (unsigned char) (0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (unsigned char) (0xF0 | (cp >> 18));
  out[1] = (unsigned char) (0x80 | ((cp >> 12) & 0x3F));
  out[2] = (unsigned char) (0x80 | ((cp >> 6) & 0x3F));
  out[3] = (unsigned char) (0x80 | (cp & 0x3F));
  return 4;
}

void vn_engine_reset(void) {
  UnikeyResetBuf();
  word_history_len = 0;
}

struct vn_engine_result vn_engine_process_key(unsigned int ch, bool shift, bool capslock) {
  UnikeySetCapsState(shift ? 1 : 0, capslock ? 1 : 0);
  UnikeyFilter(ch);

  int char_backspaces = bytes_to_char_count(UnikeyBackspaces);

  if (UnikeyBackspaces == 0 && UnikeyBufChars == 0) {
    // No correction: the caller lets the raw keystroke pass through
    // unmodified, so track its bytes ourselves to keep word_history accurate
    // for future conversions.
    unsigned char raw[4];
    int raw_len = encode_utf8(ch, raw);
    word_history_apply(0, raw, raw_len);
  } else {
    word_history_apply(UnikeyBackspaces, UnikeyBuf, UnikeyBufChars);
  }

  // Telex/VNI corrections never legitimately reach back past the start of
  // the current word (confirmed: processing a long passage continuously
  // produces byte-for-byte identical output to resetting between every
  // word) -- so once libunikey reports we're at a fresh word boundary,
  // word_history has nothing left worth keeping. Without this, a long
  // enough continuous typing session (no arrow keys/Enter/mouse click/2s+
  // idle to trigger vn_engine_reset()) eventually fills the fixed 256-byte
  // word_history buffer; word_history_apply then silently truncates the
  // next insert mid-UTF8-character, and bytes_to_char_count() misreads the
  // corrupted tail on a later keystroke -- producing a too-large character
  // backspace count that eats part of the previous word (reproduced: "cho"
  // -> "chơ" -> "cờ" instead of "chờ", dropping the leading consonant).
  if (UnikeyAtWordBeginning()) word_history_len = 0;

  struct vn_engine_result result = {
    .backspace_count = char_backspaces,
    .insert_text = UnikeyBuf,
    .insert_len = UnikeyBufChars
  };
  return result;
}

struct vn_engine_result vn_engine_process_backspace(void) {
  if (word_history_len == 0) {
    // Nothing left of the current word for libunikey to be tracking (e.g.
    // the previous backspace already consumed back through a word
    // boundary). Calling UnikeyBackspacePress() again here hits an
    // already-empty internal buffer in libunikey and can wedge its state so
    // it silently stops recognizing the *next* word's Telex compositions
    // (confirmed: "aa" no longer converting to "a with hat" at all
    // afterwards). Treat it as a plain passthrough delete instead.
    struct vn_engine_result result = { .backspace_count = 0, .insert_text = NULL, .insert_len = 0 };
    return result;
  }

  UnikeyBackspacePress();

  int char_backspaces = bytes_to_char_count(UnikeyBackspaces);
  word_history_apply(UnikeyBackspaces, UnikeyBuf, UnikeyBufChars);

  // Same word-boundary bound as vn_engine_process_key -- keeps the
  // invariant "at word beginning implies empty word_history" true
  // regardless of whether we arrived there by typing or backspacing.
  if (UnikeyAtWordBeginning()) word_history_len = 0;

  struct vn_engine_result result = {
    .backspace_count = char_backspaces,
    .insert_text = UnikeyBuf,
    .insert_len = UnikeyBufChars
  };
  return result;
}
