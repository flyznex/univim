#pragma once
#include <stdbool.h>

typedef enum { VN_METHOD_TELEX, VN_METHOD_VNI } vn_method;

struct vn_engine_result {
  int backspace_count;
  const unsigned char* insert_text; // valid until the next vn_engine_process_* call
  int insert_len;
};

void vn_engine_init(vn_method method);
void vn_engine_set_method(vn_method method);
void vn_engine_reset(void);
// Loads user-defined "key:text" shortcut lines from `path` (e.g.
// ~/.config/univim/vn_macros) into libunikey's macro table and enables
// expansion. A no-op (returns true, macros stay disabled) if `path` doesn't
// exist. Returns false only on an actual failure (can't write the compiled
// file, or libunikey rejects it) -- caller decides how to log that.
bool vn_engine_load_macros(const char* path);
struct vn_engine_result vn_engine_process_key(unsigned int ch, bool shift, bool capslock);
struct vn_engine_result vn_engine_process_backspace(void);
