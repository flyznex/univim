#include "vn_engine.h"
#include "libunikey/unikey.h"
#include "libunikey/vnconv.h"

void vn_engine_init(vn_method method) {
  UnikeySetup();
  UnikeySetOutputCharset(CONV_CHARSET_UNIUTF8);
  vn_engine_set_method(method);
}

void vn_engine_set_method(vn_method method) {
  UnikeySetInputMethod(method == VN_METHOD_VNI ? UkVni : UkTelex);
}

void vn_engine_reset(void) {
  UnikeyResetBuf();
}

static struct vn_engine_result current_result(void) {
  struct vn_engine_result result = {
    .backspace_count = UnikeyBackspaces,
    .insert_text = UnikeyBuf,
    .insert_len = UnikeyBufChars
  };
  return result;
}

struct vn_engine_result vn_engine_process_key(unsigned int ch, bool shift, bool capslock) {
  UnikeySetCapsState(shift ? 1 : 0, capslock ? 1 : 0);
  UnikeyFilter(ch);
  return current_result();
}

struct vn_engine_result vn_engine_process_backspace(void) {
  UnikeyBackspacePress();
  return current_result();
}
