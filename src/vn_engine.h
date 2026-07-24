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
struct vn_engine_result vn_engine_process_key(unsigned int ch, bool shift, bool capslock);
struct vn_engine_result vn_engine_process_backspace(void);
