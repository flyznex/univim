#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/vn_engine.h"

// Applies a vn_engine_result's backspace/insert onto a plain-C-string
// oracle buffer -- shared by type_sequence (below) and the restore tests,
// since both need the exact same "N character-backspaces, then insert N
// bytes" logic that the real integration points (event_tap.c/ax.c) apply
// incrementally instead.
static void apply_correction(struct vn_engine_result r, char* out, size_t* len) {
  if (r.backspace_count > 0) {
    // backspace_count is a character count (vn_engine.c converts
    // libunikey's raw byte count into this via its own tracked word
    // history) -- remove that many trailing *characters*, which may span
    // more than that many bytes, by skipping UTF-8 continuation bytes.
    int remaining = r.backspace_count;
    while (remaining > 0 && *len > 0) {
      (*len)--;
      while (*len > 0 && (out[*len] & 0xC0) == 0x80) (*len)--;
      remaining--;
    }
    out[*len] = '\0';
  }
  if (r.insert_len > 0) {
    memcpy(out + *len, r.insert_text, r.insert_len);
    *len += r.insert_len;
    out[*len] = '\0';
  }
}

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
    apply_correction(r, out, &len);
    if (r.insert_len == 0 && r.backspace_count == 0) {
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

// Types `keys`, then triggers vn_engine_restore_key_strokes() and applies
// its result the same way a real caller (event_tap.c/ax.c) would — checks
// the word reverts to its original literal keystrokes, or, for a word that
// was never transformed, that restore is a true no-op. That no-op case is
// exactly what lets the real caller fall back to typing the trigger key
// literally instead of misfiring on words like "quiz". expect_noop asserts
// directly on the raw result (backspace_count == 0 && insert_len == 0)
// rather than relying solely on the net string match, which a hypothetical
// "backspace N, reinsert the same N chars" regression could still pass even
// though it isn't the true no-op Task 3/4's dispatch logic depends on.
static void check_restore(const char* label, vn_method method, const char* keys, const char* expected, bool expect_noop) {
  vn_engine_set_method(method);
  vn_engine_reset();
  char out[256];
  type_sequence(keys, out, sizeof(out));
  size_t len = strlen(out);
  struct vn_engine_result r = vn_engine_restore_key_strokes();
  bool is_noop = (r.backspace_count == 0 && r.insert_len == 0);
  if (expect_noop) {
    printf("[%s] keys=\"%s\" backspace_count=%d insert_len=%d %s\n",
           label, keys, r.backspace_count, r.insert_len, is_noop ? "OK" : "MISMATCH");
    assert(is_noop);
  }
  apply_correction(r, out, &len);
  printf("[%s] keys=\"%s\" after_restore=\"%s\" want=\"%s\" %s\n",
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
  // config-exposed default method (vn_config's `method=simpletelex`, or no
  // method= line at all) -- must actually route to UkSimpleTelex, not
  // silently fall back to UkTelex/VNI.
  check("simpletelex default method", VN_METHOD_SIMPLETELEX, "thaays", "th\xe1\xba\xa5y"); // "thấy"

  // modern vs. old tone-mark placement (vn_config's `modern_style=`) --
  // libunikey defaults to old style (modernStyle=0); vn_engine_set_tone_style
  // must actually flip UnikeyOptions.modernStyle, not silently no-op.
  {
    vn_engine_set_method(VN_METHOD_TELEX);

    vn_engine_set_tone_style(false);
    vn_engine_reset();
    char out_old[64];
    type_sequence("hoaf", out_old, sizeof(out_old));
    printf("[tone style old] keys=\"hoaf\" got=\"%s\" want=\"h\xc3\xb2" "a\" %s\n",
           out_old, strcmp(out_old, "h\xc3\xb2" "a") == 0 ? "OK" : "MISMATCH");
    assert(strcmp(out_old, "h\xc3\xb2" "a") == 0); // "hòa"

    vn_engine_set_tone_style(true);
    vn_engine_reset();
    char out_modern[64];
    type_sequence("hoaf", out_modern, sizeof(out_modern));
    printf("[tone style modern] keys=\"hoaf\" got=\"%s\" want=\"ho\xc3\xa0\" %s\n",
           out_modern, strcmp(out_modern, "ho\xc3\xa0") == 0 ? "OK" : "MISMATCH");
    assert(strcmp(out_modern, "ho\xc3\xa0") == 0); // "hoà"

    // toggling back to old must also work, not just the first switch
    vn_engine_set_tone_style(false);
    vn_engine_reset();
    char out_old2[64];
    type_sequence("hoaf", out_old2, sizeof(out_old2));
    assert(strcmp(out_old2, "h\xc3\xb2" "a") == 0); // "hòa"
  }

  // regression: cursor moved away mid-composition (e.g. Left Arrow after "a")
  // must not let a later keystroke still apply a tone mark meant for the
  // abandoned "a" -- event_tap.c/ax.c call vn_engine_reset() on cursor
  // navigation keys for exactly this reason (previously they didn't, so
  // "a" + Left + "s" produced "áa" instead of "sa").
  {
    vn_engine_set_method(VN_METHOD_TELEX);
    vn_engine_reset();
    struct vn_engine_result r1 = vn_engine_process_key('a', false, false);
    assert(r1.backspace_count == 0 && r1.insert_len == 0); // "a" passes through raw

    vn_engine_reset(); // what the arrow-key handlers now do

    struct vn_engine_result r2 = vn_engine_process_key('s', false, false);
    printf("[cursor-move breaks composition] backspaces=%d insert_len=%d %s\n",
           r2.backspace_count, r2.insert_len,
           (r2.backspace_count == 0 && r2.insert_len == 0) ? "OK" : "MISMATCH");
    assert(r2.backspace_count == 0 && r2.insert_len == 0); // "s" passes through raw too, not "á"
  }

  // z-trigger design (docs/superpowers/specs/2026-08-02-restore-keystrokes-z-trigger-design.md):
  // restoring a transformed word must revert it to the literal keystrokes,
  // and restoring an untransformed word must be a true no-op -- verified
  // against the real engine (values below are not guessed).
  check_restore("restore reverts a transformed word", VN_METHOD_TELEX, "of", "of", false);
  check_restore("restore is a no-op on an untransformed word", VN_METHOD_TELEX, "hello", "hello", true);
  check_restore("restore reverts a word with a real tone mark", VN_METHOD_TELEX, "thaays", "thaays", false);
  check_restore("restore is a no-op on a word that never triggers a tone", VN_METHOD_TELEX, "quiz", "quiz", true);

  // Macro shortcut expansion (vn_engine_load_macros): user-facing vn_macros
  // files are plain "key:text" lines with no header -- vn_engine_load_macros
  // must add libunikey's required UTF-8 version header itself, or accented
  // macro text silently mangles (see vn_engine.c's comment).
  {
    const char* fixture_path = "/tmp/test_vn_macros_fixture";
    FILE* f = fopen(fixture_path, "w");
    assert(f);
    fputs("sdt:s\xe1\xbb\x91 \xc4\x91i\xe1\xbb\x87n tho\xe1\xba\xa1i\n", f); // "số điện thoại"
    fclose(f);

    vn_engine_load_macros(fixture_path);
    check("macro shortcut expansion", VN_METHOD_TELEX, "sdt ",
          "s\xe1\xbb\x91 \xc4\x91i\xe1\xbb\x87n tho\xe1\xba\xa1i "); // "số điện thoại "
  }

  printf("ALL TESTS PASSED\n");
  return 0;
}
