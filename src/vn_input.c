#include "vn_input.h"
#include "helpers.h"
#include "buffer.h" // NORMAL / INSERT / VISUAL / CMDLINE mode bits, via libvim.h
#include "env_vars.h"
#include "toast.h"
#include <Carbon/Carbon.h> // kVK_Space, kVK_ANSI_* -- the hotkey= regular-key lookup table

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
  snprintf(path, sizeof(path), "%s/.config/univim/vn_debug.log", home);
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

// Regular-key component of a hotkey= combo (e.g. the "space" in
// "control+space") -- deliberately just space/a-z/0-9. Not arrows,
// function keys, Tab, Escape, Enter, Delete: those already carry their
// own vim-mode/editing meaning and this table has no way to disambiguate
// "the user wants this as a global toggle everywhere" from "this key means
// what it normally means right now."
static const struct { const char* name; CGKeyCode code; } HOTKEY_KEY_TABLE[] = {
  {"space", kVK_Space},
  {"a", kVK_ANSI_A}, {"b", kVK_ANSI_B}, {"c", kVK_ANSI_C}, {"d", kVK_ANSI_D},
  {"e", kVK_ANSI_E}, {"f", kVK_ANSI_F}, {"g", kVK_ANSI_G}, {"h", kVK_ANSI_H},
  {"i", kVK_ANSI_I}, {"j", kVK_ANSI_J}, {"k", kVK_ANSI_K}, {"l", kVK_ANSI_L},
  {"m", kVK_ANSI_M}, {"n", kVK_ANSI_N}, {"o", kVK_ANSI_O}, {"p", kVK_ANSI_P},
  {"q", kVK_ANSI_Q}, {"r", kVK_ANSI_R}, {"s", kVK_ANSI_S}, {"t", kVK_ANSI_T},
  {"u", kVK_ANSI_U}, {"v", kVK_ANSI_V}, {"w", kVK_ANSI_W}, {"x", kVK_ANSI_X},
  {"y", kVK_ANSI_Y}, {"z", kVK_ANSI_Z},
  {"0", kVK_ANSI_0}, {"1", kVK_ANSI_1}, {"2", kVK_ANSI_2}, {"3", kVK_ANSI_3},
  {"4", kVK_ANSI_4}, {"5", kVK_ANSI_5}, {"6", kVK_ANSI_6}, {"7", kVK_ANSI_7},
  {"8", kVK_ANSI_8}, {"9", kVK_ANSI_9},
};
#define HOTKEY_KEY_TABLE_COUNT (sizeof(HOTKEY_KEY_TABLE) / sizeof(HOTKEY_KEY_TABLE[0]))

static bool lookup_hotkey_key(const char* token, CGKeyCode* out_code) {
  for (size_t i = 0; i < HOTKEY_KEY_TABLE_COUNT; i++) {
    if (strcmp(token, HOTKEY_KEY_TABLE[i].name) == 0) {
      *out_code = HOTKEY_KEY_TABLE[i].code;
      return true;
    }
  }
  return false;
}

CGEventFlags parse_hotkey(const char* str, int64_t* out_keycode, bool* out_has_keycode, bool* valid) {
  CGEventFlags mask = 0;
  *out_has_keycode = false;
  *out_keycode = 0;
  *valid = true;
  char buf[128];
  snprintf(buf, sizeof(buf), "%s", str);

  char* token = strtok(buf, "+");
  while (token) {
    CGKeyCode code;
    if (strcmp(token, "control") == 0) mask |= kCGEventFlagMaskControl;
    else if (strcmp(token, "shift") == 0) mask |= kCGEventFlagMaskShift;
    else if (strcmp(token, "command") == 0) mask |= kCGEventFlagMaskCommand;
    else if (strcmp(token, "option") == 0) mask |= kCGEventFlagMaskAlternate;
    else if (lookup_hotkey_key(token, &code)) {
      if (*out_has_keycode) *valid = false; // more than one regular key in the combo
      *out_has_keycode = true;
      *out_keycode = code;
    }
    else *valid = false; // unrecognized token -- previously silently
                          // dropped with no error, quietly degrading to
                          // whatever modifiers did match instead of
                          // telling the user anything was wrong.
    token = strtok(NULL, "+");
  }
  if (*out_has_keycode && mask == 0) *valid = false; // regular key needs >=1 modifier
  return mask;
}

static void notify_config_error(const char* error_msg) {
  struct env_vars env_vars;
  env_vars_init(&env_vars);
  env_vars_set(&env_vars, string_copy("UNIVIM_CONFIG_ERROR"), string_copy((char*)error_msg));

  char* home = getenv("HOME");
  char buf[512];
  snprintf(buf, sizeof(buf), "%s/.config/univim/svim.sh", home);
  vfork_exec(buf, &env_vars);
  env_vars_destroy(&env_vars);
}

// Returns error message if any, NULL if no errors. Caller must free.
// Preserves previous values for fields that fail to parse.
static char* vn_config_load(struct vn_input* vn, bool is_reload) {
  char* home = getenv("HOME");
  char path[512];
  snprintf(path, sizeof(path), "%s/.config/univim/vn_config", home);
  FILE* file = fopen(path, "r");

  // Defaults for fresh load, preserved values for reload
  vn_method new_method = is_reload ? vn->method : VN_METHOD_SIMPLETELEX;
  CGEventFlags new_hotkey = is_reload ? vn->hotkey_mask : (kCGEventFlagMaskControl | kCGEventFlagMaskShift);
  int64_t new_hotkey_keycode = is_reload ? vn->hotkey_keycode : 0;
  bool new_has_hotkey_keycode = is_reload ? vn->has_hotkey_keycode : false;
  CGEventFlags new_disable_hotkey = is_reload ? vn->disable_hotkey_mask : 0;
  int64_t new_disable_hotkey_keycode = is_reload ? vn->disable_hotkey_keycode : 0;
  bool new_has_disable_hotkey_keycode = is_reload ? vn->has_disable_hotkey_keycode : false;
  int64_t new_restore_trigger_keycode = is_reload ? vn->restore_trigger_keycode : 0;
  bool new_has_restore_trigger_keycode = is_reload ? vn->has_restore_trigger_keycode : false;
  bool new_debug = is_reload ? vn->debug : false;
  bool new_modern_style = is_reload ? vn->modern_style : true;

  if (!file) {
    // No config file is not an error -- but a fresh load (app startup, no
    // vn_config ever existing) still needs these defaults actually applied
    // to `vn`, not just computed and discarded here: returning early before
    // ever assigning them left hotkey_mask at its zero-initialized value,
    // silently disabling the toggle hotkey entirely (0 fails the `&&
    // hotkey_mask` check in event_tap.c) instead of falling back to the
    // documented default (control+shift).
    vn->method = new_method;
    vn->hotkey_mask = new_hotkey;
    vn->hotkey_keycode = new_hotkey_keycode;
    vn->has_hotkey_keycode = new_has_hotkey_keycode;
    vn->disable_hotkey_mask = new_disable_hotkey;
    vn->disable_hotkey_keycode = new_disable_hotkey_keycode;
    vn->has_disable_hotkey_keycode = new_has_disable_hotkey_keycode;
    vn->restore_trigger_keycode = new_restore_trigger_keycode;
    vn->has_restore_trigger_keycode = new_has_restore_trigger_keycode;
    vn->debug = new_debug;
    vn->modern_style = new_modern_style;
    return NULL;
  }

  char errors[1024] = "";
  int error_count = 0;
  int line_num = 0;

  char line[255];
  while (fgets(line, sizeof(line), file)) {
    line_num++;
    uint32_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
    if (len == 0 || line[0] == '#') continue; // Skip empty lines and comments

    char* eq = strchr(line, '=');
    if (!eq) {
      if (error_count < 3) {
        char err[128];
        snprintf(err, sizeof(err), "line %d: missing '=' in '%s'\n", line_num, line);
        strncat(errors, err, sizeof(errors) - strlen(errors) - 1);
      }
      error_count++;
      continue;
    }
    *eq = '\0';
    char* key = line;
    char* value = eq + 1;

    if (strcmp(key, "method") == 0) {
      if (strcmp(value, "vni") == 0) new_method = VN_METHOD_VNI;
      else if (strcmp(value, "telex") == 0) new_method = VN_METHOD_TELEX;
      else if (strcmp(value, "simpletelex") == 0) new_method = VN_METHOD_SIMPLETELEX;
      else {
        if (error_count < 3) {
          char err[128];
          snprintf(err, sizeof(err), "line %d: invalid method '%s'\n", line_num, value);
          strncat(errors, err, sizeof(errors) - strlen(errors) - 1);
        }
        error_count++;
      }
    } else if (strcmp(key, "hotkey") == 0) {
      int64_t keycode;
      bool has_keycode;
      bool hotkey_valid;
      CGEventFlags mask = parse_hotkey(value, &keycode, &has_keycode, &hotkey_valid);
      if (!hotkey_valid || (mask == 0 && !has_keycode && strlen(value) > 0)) {
        if (error_count < 3) {
          char err[128];
          snprintf(err, sizeof(err), "line %d: invalid hotkey '%s'\n", line_num, value);
          strncat(errors, err, sizeof(errors) - strlen(errors) - 1);
        }
        error_count++;
      } else {
        new_hotkey = mask;
        new_hotkey_keycode = keycode;
        new_has_hotkey_keycode = has_keycode;
      }
    } else if (strcmp(key, "disable_vim_hotkey") == 0) {
      int64_t keycode;
      bool has_keycode;
      bool hotkey_valid;
      CGEventFlags mask = parse_hotkey(value, &keycode, &has_keycode, &hotkey_valid);
      if (!hotkey_valid || (mask == 0 && !has_keycode && strlen(value) > 0)) {
        if (error_count < 3) {
          char err[128];
          snprintf(err, sizeof(err), "line %d: invalid disable_vim_hotkey '%s'\n", line_num, value);
          strncat(errors, err, sizeof(errors) - strlen(errors) - 1);
        }
        error_count++;
      } else {
        new_disable_hotkey = mask;
        new_disable_hotkey_keycode = keycode;
        new_has_disable_hotkey_keycode = has_keycode;
      }
    } else if (strcmp(key, "debug") == 0) {
      if (strcmp(value, "1") == 0 || strcmp(value, "on") == 0) new_debug = true;
      else if (strcmp(value, "0") == 0 || strcmp(value, "off") == 0) new_debug = false;
      else {
        if (error_count < 3) {
          char err[128];
          snprintf(err, sizeof(err), "line %d: invalid debug value '%s'\n", line_num, value);
          strncat(errors, err, sizeof(errors) - strlen(errors) - 1);
        }
        error_count++;
      }
    } else if (strcmp(key, "modern_style") == 0) {
      if (strcmp(value, "1") == 0 || strcmp(value, "on") == 0) new_modern_style = true;
      else if (strcmp(value, "0") == 0 || strcmp(value, "off") == 0) new_modern_style = false;
      else {
        if (error_count < 3) {
          char err[128];
          snprintf(err, sizeof(err), "line %d: invalid modern_style value '%s'\n", line_num, value);
          strncat(errors, err, sizeof(errors) - strlen(errors) - 1);
        }
        error_count++;
      }
    } else if (strcmp(key, "restore_trigger_key") == 0) {
      CGKeyCode code;
      if (!lookup_hotkey_key(value, &code)) {
        if (error_count < 3) {
          char err[128];
          snprintf(err, sizeof(err), "line %d: invalid restore_trigger_key '%s'\n", line_num, value);
          strncat(errors, err, sizeof(errors) - strlen(errors) - 1);
        }
        error_count++;
      } else {
        new_restore_trigger_keycode = code;
        new_has_restore_trigger_keycode = true;
      }
    } else {
      if (error_count < 3) {
        char err[128];
        snprintf(err, sizeof(err), "line %d: unknown key '%s'\n", line_num, key);
        strncat(errors, err, sizeof(errors) - strlen(errors) - 1);
      }
      error_count++;
    }
  }
  fclose(file);

  // Apply successfully parsed values
  vn->method = new_method;
  vn->hotkey_mask = new_hotkey;
  vn->hotkey_keycode = new_hotkey_keycode;
  vn->has_hotkey_keycode = new_has_hotkey_keycode;
  vn->disable_hotkey_mask = new_disable_hotkey;
  vn->disable_hotkey_keycode = new_disable_hotkey_keycode;
  vn->has_disable_hotkey_keycode = new_has_disable_hotkey_keycode;
  vn->restore_trigger_keycode = new_restore_trigger_keycode;
  vn->has_restore_trigger_keycode = new_has_restore_trigger_keycode;
  vn->debug = new_debug;
  vn->modern_style = new_modern_style;

  if (error_count > 0) {
    char* result = malloc(1200);
    if (error_count > 3) {
      snprintf(result, 1200, "vn_config: %d errors\n%s... and %d more", error_count, errors, error_count - 3);
    } else {
      snprintf(result, 1200, "vn_config: %d error(s)\n%s", error_count, errors);
    }
    return result;
  }
  return NULL;
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
  char* config_error = vn_config_load(vn, false);
  if (config_error) {
    notify_config_error(config_error);
    vn_debug_log("vn_input_begin: %s", config_error);
    free(config_error);
  }

  char* home = getenv("HOME");
  char path[512];
  snprintf(path, sizeof(path), "%s/.config/univim/vn_blacklist", home);
  struct string_list list = load_string_list(path);
  vn->blacklist = list.items;
  vn->blacklist_count = list.count;

  char overrides_path[512];
  snprintf(overrides_path, sizeof(overrides_path), "%s/.config/univim/vn_overrides", home);
  struct vn_override_list overrides = load_vn_overrides(overrides_path);
  vn->overrides = overrides.items;
  vn->overrides_count = overrides.count;

  vn_engine_init(vn->method);
  vn_engine_set_tone_style(vn->modern_style);

  char macros_path[512];
  snprintf(macros_path, sizeof(macros_path), "%s/.config/univim/vn_macros", home);
  if (!vn_engine_load_macros(macros_path))
    vn_debug_log("vn_input_begin: vn_engine_load_macros failed, path=%s", macros_path);

  statusbar_init();
  statusbar_refresh(vn);
}

void vn_input_reload_config(struct vn_input* vn) {
  vn_method old_method = vn->method;
  bool old_modern_style = vn->modern_style;
  char* config_error = vn_config_load(vn, true);
  if (config_error) {
    notify_config_error(config_error);
    vn_debug_log("vn_input_reload_config: %s", config_error);
    free(config_error);
  }

  // Update engine method if changed
  if (vn->method != old_method) {
    vn_engine_set_method(vn->method);
  }
  if (vn->modern_style != old_modern_style) {
    vn_engine_set_tone_style(vn->modern_style);
  }
}

void vim_status_label(bool enabled, bool vim_disabled, char* out, size_t n) {
  snprintf(out, n, "%s%s", enabled ? "VI" : "EN", vim_disabled ? "-" : "");
}

void statusbar_refresh(struct vn_input* vn) {
  char label[8];
  vim_status_label(vn->enabled, vn->vim_disabled, label, sizeof label);
  statusbar_update(label);
}

void vim_disable_toggle(struct vn_input* vn) {
  vn->vim_disabled = !vn->vim_disabled;
  statusbar_refresh(vn);
  toast_show(vn->vim_disabled ? "Vim-" : "Vim+");
  vn_debug_log("vim_disable_toggle: vim_disabled now=%d", vn->vim_disabled);
}

void vn_input_toggle(struct vn_input* vn) {
  vn->enabled = !vn->enabled;
  vn_engine_reset();
  
  toast_show(vn->enabled ? "VI" : "EN");
  statusbar_refresh(vn);

  struct env_vars env_vars;
  env_vars_init(&env_vars);
  env_vars_set(&env_vars, string_copy("VNMODE"), string_copy(vn->enabled ? "on" : "off"));

  char* home = getenv("HOME");
  char buf[512];
  snprintf(buf, sizeof(buf), "%s/.config/univim/svim.sh", home);
  vfork_exec(buf, &env_vars);
  env_vars_destroy(&env_vars);
}
