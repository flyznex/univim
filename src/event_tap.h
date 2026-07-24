#pragma once
#include <stdbool.h>
#include <Carbon/Carbon.h>
#include <stdint.h>
#include "ax.h"
#include "buffer.h"
#include "vn_input.h"

extern const char* get_name_for_pid(uint64_t pid);
extern char* string_copy(char* s);

struct vn_post_target {
  pid_t pid;
  int delay_us;
  enum vn_correction_strategy strategy;
};

struct event_tap {
  bool front_app_ignored;
  bool vn_ignored;
  pid_t front_pid;
  int delay_us;
  enum vn_correction_strategy strategy;
  uint32_t blacklist_count;
  char** blacklist;
  CFMachPortRef handle;
  CFRunLoopSourceRef runloop_source;
  CGEventMask mask;
};

struct event_tap g_event_tap;
bool event_tap_enabled(struct event_tap *event_tap);
bool event_tap_begin(struct event_tap *event_tap);
void event_tap_end(struct event_tap *event_tap);
bool event_tap_check_blacklist(struct event_tap* event_tap, char* app, char* bundle_id);
void vn_post_correction(struct vn_post_target target, int backspace_count, const unsigned char* insert_text, int insert_len);
CGEventRef vn_synthetic_process(struct event_tap* event_tap, CGEventRef event);
