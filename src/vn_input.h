#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <ApplicationServices/ApplicationServices.h>
#include "vn_engine.h"

enum vn_flow { VN_FLOW_NONE, VN_FLOW_SYNTHETIC, VN_FLOW_VIM_BUFFER };

struct vn_input {
  bool enabled;
  char** blacklist;
  uint32_t blacklist_count;
  vn_method method;
  CGEventFlags hotkey_mask;
};

extern struct vn_input g_vn_input;

void vn_input_begin(struct vn_input* vn);
bool vn_input_blacklisted(struct vn_input* vn, char* app, char* bundle_id);
enum vn_flow vn_input_route(struct vn_input* vn, bool is_vn_blacklisted,
                            bool front_app_ignored, uint32_t cursor_mode);
void vn_input_toggle(struct vn_input* vn);
