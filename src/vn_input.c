#include "vn_input.h"
#include "helpers.h"
#include "buffer.h" // NORMAL / INSERT / VISUAL / CMDLINE mode bits, via libvim.h
#include "env_vars.h"
#include "toast.h"

struct vn_input g_vn_input;
char g_vn_debug_app_name[256] = "";

bool vn_input_blacklisted(struct vn_input* vn, char* app, char* bundle_id) {
  return blacklist_contains(vn->blacklist, vn->blacklist_count, app, bundle_id);
}

void vn_input_lookup_override(struct vn_input* vn, char* app, char* bundle_id,
                              int* out_delay_us, enum vn_correction_strategy* out_strategy) {
  *out_delay_us = 5000;
  *out_strategy = VN_STRATEGY_BACKSPACE;
  if (!app || !bundle_id) return;

  for (uint32_t i = 0; i < vn->overrides_count; i++) {
    if (strcmp(vn->overrides[i].app, app) == 0 || strcmp(vn->overrides[i].app, bundle_id) == 0) {
      *out_delay_us = vn->overrides[i].delay_us;
      *out_strategy = vn->overrides[i].strategy;
      return;
    }
  }
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
// Row format: "AppName delay_ms strategy" -- AppName may contain spaces
// (e.g. "Visual Studio Code"), so this parses from the *end* of the line:
// the last token is the strategy, the second-to-last is the delay, and
// everything before that (trimmed) is the app name.
static bool parse_override_line(char* line, struct vn_override* out) {
  char* last_space = strrchr(line, ' ');
  if (!last_space) return false;
  char* strategy_str = last_space + 1;
  *last_space = '\0';

  char* second_last_space = strrchr(line, ' ');
  if (!second_last_space) return false;
  char* delay_str = second_last_space + 1;
  *second_last_space = '\0';

  if (line[0] == '\0') return false;

  enum vn_correction_strategy strategy;
  if (strcmp(strategy_str, "backspace") == 0) strategy = VN_STRATEGY_BACKSPACE;
  else if (strcmp(strategy_str, "select") == 0) strategy = VN_STRATEGY_SELECT;
  else return false;

  if (delay_str[0] == '\0') return false;
  for (char* c = delay_str; *c; c++)
    if (*c < '0' || *c > '9') return false;

  out->app = string_copy(line);
  out->delay_us = atoi(delay_str) * 1000;
  out->strategy = strategy;
  return true;
}

struct vn_override_list load_vn_overrides(const char* path) {
  struct vn_override_list list = { NULL, 0 };

  FILE* file = fopen(path, "r");
  if (!file) return list;

  char line[255];
  while (fgets(line, sizeof(line), file)) {
    uint32_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
    if (len == 0) continue;
    if (line[0] == '#') continue;

    struct vn_override parsed;
    if (!parse_override_line(line, &parsed)) continue;

    list.items = realloc(list.items, sizeof(struct vn_override) * ++list.count);
    list.items[list.count - 1] = parsed;
  }
  fclose(file);
  return list;
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

  char overrides_path[512];
  snprintf(overrides_path, sizeof(overrides_path), "%s/.config/svim/vn_overrides", home);
  struct vn_override_list overrides = load_vn_overrides(overrides_path);
  vn->overrides = overrides.items;
  vn->overrides_count = overrides.count;

  vn_engine_init(vn->method);
}

void vn_input_toggle(struct vn_input* vn) {
  vn->enabled = !vn->enabled;
  vn_engine_reset();
  toast_show(vn->enabled ? "VI" : "EN");

  struct env_vars env_vars;
  env_vars_init(&env_vars);
  env_vars_set(&env_vars, string_copy("VNMODE"), string_copy(vn->enabled ? "on" : "off"));

  char* home = getenv("HOME");
  char buf[512];
  snprintf(buf, sizeof(buf), "%s/.config/svim/svim.sh", home);
  vfork_exec(buf, &env_vars);
  env_vars_destroy(&env_vars);
}
