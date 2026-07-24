#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/vn_engine.h"

// Feeds a sequence of ASCII keystrokes through the engine and returns the
// fully composed UTF-8 result as a plain C string (test-only helper — the
// real integration points apply backspace_count/insert_text incrementally
// instead of collecting a final string, but doing so here gives a simple,
// readable oracle to assert against).
static void type_sequence(const char* keys, char* out, size_t out_size) {
  size_t len = 0;
  out[0] = '\0';
  for (const char* k = keys; *k; k++) {
    struct vn_engine_result r = vn_engine_process_key((unsigned char) *k, false, false);
    if (r.backspace_count > 0) {
      // backspace_count is a character count (vn_engine.c converts
      // libunikey's raw byte count into this via its own tracked word
      // history) — remove that many trailing *characters*, which may span
      // more than that many bytes, by skipping UTF-8 continuation bytes.
      int remaining = r.backspace_count;
      while (remaining > 0 && len > 0) {
        len--;
        while (len > 0 && (out[len] & 0xC0) == 0x80) len--;
        remaining--;
      }
      out[len] = '\0';
    }
    if (r.insert_len > 0) {
      memcpy(out + len, r.insert_text, r.insert_len);
      len += r.insert_len;
      out[len] = '\0';
    } else if (r.backspace_count == 0) {
      // no correction needed — the OS just echoes the original keystroke
      out[len++] = *k;
      out[len] = '\0';
    }
  }
  (void) out_size;
}

static void check(const char* label, vn_method method, const char* keys, const char* expected) {
  vn_engine_set_method(method);
  vn_engine_reset();
  char out[256];
  type_sequence(keys, out, sizeof(out));
  printf("[%s] keys=\"%s\" got=\"%s\" want=\"%s\" %s\n",
         label, keys, out, expected, strcmp(out, expected) == 0 ? "OK" : "MISMATCH");
  assert(strcmp(out, expected) == 0);
}

int main(void) {
  vn_engine_init(VN_METHOD_TELEX);

  check("telex compound vowel", VN_METHOD_TELEX, "vieejt", "vi\xe1\xbb\x87t");   // "việt"
  check("telex dd", VN_METHOD_TELEX, "ddi", "\xc4\x91i");                        // "đi"
  check("telex word boundary", VN_METHOD_TELEX, "anh a", "anh a");               // no cross-word leak
  check("vni compound vowel", VN_METHOD_VNI, "vie6t5", "vi\xe1\xbb\x87t");       // "việt" via VNI (6=circumflex, 5=nặng, applied once after the syllable)
  // regression: tone mark reaching back past a leading consonant + trailing
  // consonant must not eat the leading consonant (byte-vs-char backspace bug)
  check("telex tone past trailing consonant", VN_METHOD_TELEX, "thaays", "th\xe1\xba\xa5y"); // "thấy"

  printf("ALL TESTS PASSED\n");
  return 0;
}
