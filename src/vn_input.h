#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <ApplicationServices/ApplicationServices.h>
#include "vn_engine.h"

enum vn_flow { VN_FLOW_NONE, VN_FLOW_SYNTHETIC, VN_FLOW_VIM_BUFFER };
enum vn_correction_strategy { VN_STRATEGY_BACKSPACE, VN_STRATEGY_SELECT };

struct vn_override {
  char* app;
  int delay_us;
  enum vn_correction_strategy strategy;
};

struct vn_override_list {
  struct vn_override* items;
  uint32_t count;
};

struct vn_input {
  bool enabled;
  bool debug;
  char** blacklist;
  uint32_t blacklist_count;
  vn_method method;
  bool modern_style;
  CGEventFlags hotkey_mask;
  int64_t hotkey_keycode;   // meaningful only when has_hotkey_keycode is true
  bool has_hotkey_keycode;  // false = hotkey_mask is a modifier-only chord (unchanged behavior)
  struct vn_override* overrides;
  uint32_t overrides_count;
};

extern struct vn_input g_vn_input;
extern char g_vn_debug_app_name[256]; // current frontmost app, for vn_debug_log context

void vn_input_begin(struct vn_input* vn);
bool vn_input_blacklisted(struct vn_input* vn, char* app, char* bundle_id);
struct vn_override_list load_vn_overrides(const char* path);
void vn_input_lookup_override(struct vn_input* vn, char* app, char* bundle_id,
                              int* out_delay_us, enum vn_correction_strategy* out_strategy);
enum vn_flow vn_input_route(struct vn_input* vn, bool is_vn_blacklisted,
                            bool front_app_ignored, uint32_t cursor_mode);
void vn_input_toggle(struct vn_input* vn);
void vn_input_reload_config(struct vn_input* vn);
CGEventFlags parse_hotkey(const char* str, int64_t* out_keycode, bool* out_has_keycode, bool* valid);

// Appends a formatted line to ~/.config/univim/vn_debug.log, only when
// `debug=1` is set in vn_config -- a no-op otherwise. Enable it to trace
// routing decisions and engine results without recompiling.
void vn_debug_log(const char* fmt, ...);
