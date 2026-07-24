#include "vn_input.h"
#include "helpers.h"
#include "buffer.h" // NORMAL / INSERT / VISUAL / CMDLINE mode bits, via libvim.h
#include "env_vars.h"

struct vn_input g_vn_input;
char g_vn_debug_app_name[256] = "";

bool vn_input_blacklisted(struct vn_input* vn, char* app, char* bundle_id) {
  return blacklist_contains(vn->blacklist, vn->blacklist_count, app, bundle_id);
}

enum vn_flow vn_input_route(struct vn_input* vn, bool is_vn_blacklisted,
                            bool front_app_ignored, uint32_t cursor_mode) {
  if (!vn->enabled || is_vn_blacklisted) return VN_FLOW_NONE;
  if (front_app_ignored) return VN_FLOW_SYNTHETIC;
  if (cursor_mode & INSERT) return VN_FLOW_VIM_BUFFER;
  return VN_FLOW_NONE;
}

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>

void vn_debug_log(const char* fmt, ...) {
  if (!g_vn_input.debug) return;

  char* home = getenv("HOME");
  char path[512];
  snprintf(path, sizeof(path), "%s/.config/svim/vn_debug.log", home);
  FILE* file = fopen(path, "a");
  if (!file) return;

  // ms precision: the race we're chasing plays out within single-digit
  // milliseconds, invisible at whole-second resolution.
  struct timeval tv;
  gettimeofday(&tv, NULL);
  char stamp[32];
  strftime(stamp, sizeof(stamp), "%H:%M:%S", localtime(&tv.tv_sec));
  fprintf(file, "[%s.%03d][%s] ", stamp, (int)(tv.tv_usec / 1000), g_vn_debug_app_name);

  va_list args;
  va_start(args, fmt);
  vfprintf(file, fmt, args);
  va_end(args);

  fprintf(file, "\n");
  fclose(file);
}

static CGEventFlags parse_hotkey(const char* str) {
  CGEventFlags mask = 0;
  char buf[128];
  snprintf(buf, sizeof(buf), "%s", str);

  char* token = strtok(buf, "+");
  while (token) {
    if (strcmp(token, "control") == 0) mask |= kCGEventFlagMaskControl;
    else if (strcmp(token, "shift") == 0) mask |= kCGEventFlagMaskShift;
    else if (strcmp(token, "command") == 0) mask |= kCGEventFlagMaskCommand;
    else if (strcmp(token, "option") == 0) mask |= kCGEventFlagMaskAlternate;
    token = strtok(NULL, "+");
  }
  return mask;
}

static void vn_config_load(struct vn_input* vn) {
  vn->method = VN_METHOD_TELEX;
  vn->hotkey_mask = kCGEventFlagMaskControl | kCGEventFlagMaskShift;
  vn->debug = false;

  char* home = getenv("HOME");
  char path[512];
  snprintf(path, sizeof(path), "%s/.config/svim/vn_config", home);
  FILE* file = fopen(path, "r");
  if (!file) return;

  char line[255];
  while (fgets(line, sizeof(line), file)) {
    uint32_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = '\0';
    char* key = line;
    char* value = eq + 1;
    if (strcmp(key, "method") == 0) {
      vn->method = (strcmp(value, "vni") == 0) ? VN_METHOD_VNI : VN_METHOD_TELEX;
    } else if (strcmp(key, "hotkey") == 0) {
      vn->hotkey_mask = parse_hotkey(value);
    } else if (strcmp(key, "debug") == 0) {
      vn->debug = (strcmp(value, "1") == 0 || strcmp(value, "on") == 0);
    }
  }
  fclose(file);
}

void vn_input_begin(struct vn_input* vn) {
  vn->enabled = false;
  vn_config_load(vn);

  char* home = getenv("HOME");
  char path[512];
  snprintf(path, sizeof(path), "%s/.config/svim/vn_blacklist", home);
  struct string_list list = load_string_list(path);
  vn->blacklist = list.items;
  vn->blacklist_count = list.count;

  vn_engine_init(vn->method);
}

void vn_input_toggle(struct vn_input* vn) {
  vn->enabled = !vn->enabled;
  vn_engine_reset();

  struct env_vars env_vars;
  env_vars_init(&env_vars);
  env_vars_set(&env_vars, string_copy("VNMODE"), string_copy(vn->enabled ? "on" : "off"));

  char* home = getenv("HOME");
  char buf[512];
  snprintf(buf, sizeof(buf), "%s/.config/svim/svim.sh", home);
  vfork_exec(buf, &env_vars);
  env_vars_destroy(&env_vars);
}
