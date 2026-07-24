#include "vn_engine.h"
#include "libunikey/unikey.h"
#include "libunikey/vnconv.h"
#include <string.h>

void vn_engine_init(vn_method method) {
  UnikeySetup();
  UnikeySetOutputCharset(CONV_CHARSET_UNIUTF8);
  vn_engine_set_method(method);
}

void vn_engine_set_method(vn_method method) {
  UnikeySetInputMethod(method == VN_METHOD_VNI ? UkVni : UkTelex);
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

  struct vn_engine_result result = {
    .backspace_count = char_backspaces,
    .insert_text = UnikeyBuf,
    .insert_len = UnikeyBufChars
  };
  return result;
}

struct vn_engine_result vn_engine_process_backspace(void) {
  UnikeyBackspacePress();

  int char_backspaces = bytes_to_char_count(UnikeyBackspaces);
  word_history_apply(UnikeyBackspaces, UnikeyBuf, UnikeyBufChars);

  struct vn_engine_result result = {
    .backspace_count = char_backspaces,
    .insert_text = UnikeyBuf,
    .insert_len = UnikeyBufChars
  };
  return result;
}
