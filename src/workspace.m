#include "workspace.h"
#include "buffer.h"
#include "event_tap.h"
#include "vn_input.h"

void workspace_begin(void **context) {
    workspace_context *ws_context = [workspace_context alloc];
    *context = ws_context;

    [ws_context init];
}

@implementation workspace_context
- (id)init {
    if ((self = [super init])) {
        [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self
                selector:@selector(appSwitched:)
                name:NSWorkspaceDidActivateApplicationNotification
                object:nil];
    }

    return self;
}

- (void)dealloc {
    [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:self];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [[NSDistributedNotificationCenter defaultCenter] removeObserver:self];
    [super dealloc];
}

- (void)appSwitched:(NSNotification *)notification {
    char* name = NULL;
    char* bundle_id = NULL;
    pid_t pid = 0;
    if (notification && notification.userInfo) {
      NSRunningApplication* app = [notification.userInfo objectForKey:NSWorkspaceApplicationKey];
      if (app) {
        name = (char*)[[app localizedName] UTF8String];
        bundle_id = (char*)[[app bundleIdentifier] UTF8String];
        pid = app.processIdentifier;
      }
    }

    snprintf(g_vn_debug_app_name, sizeof(g_vn_debug_app_name), "%s", name ? name : "?");

    g_event_tap.front_pid = pid;
    g_event_tap.front_app_ignored = event_tap_check_blacklist(&g_event_tap,
                                                              name,
                                                              bundle_id    );
    g_event_tap.vn_ignored = vn_input_blacklisted(&g_vn_input, name, bundle_id);
    vn_debug_log("appSwitched: vn_engine_reset (app=%s)", name ? name : "?");
    vn_engine_reset();
    ax_front_app_changed(&g_ax, pid);
}

@end
