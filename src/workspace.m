#include "workspace.h"
#include "buffer.h"
#include "event_tap.h"
#include "vn_input.h"
#include "input_source.h"
#import <Carbon/Carbon.h> // kTISNotifySelectedKeyboardInputSourceChanged

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

        // Input-source changes are event-driven (no polling): the system posts
        // this distributed notification whenever the selected keyboard input
        // source changes -- including layout switches that happen without an
        // app switch. The callback recomputes the cached IME flag.
        [[NSDistributedNotificationCenter defaultCenter] addObserver:self
                selector:@selector(inputSourceChanged:)
                name:(NSString*)kTISNotifySelectedKeyboardInputSourceChanged
                object:nil];

        // Compute once for whatever source is already active at startup, before
        // any switch notification arrives.
        g_input_source_is_ime = input_source_is_composing_ime();

        // The notification above only fires on a *future* app switch -- the
        // app already frontmost when svim starts (e.g. right after a deploy
        // restart) never gets one, leaving g_event_tap.front_pid/delay_us/
        // strategy at their zero-initialized defaults (pid=0 falls back to
        // the session-wide CGEventPost broadcast instead of the targeted
        // CGEventPostToPid, reintroducing the exact delivery race that fix
        // was for) until the user happens to switch away and back. Resolve
        // the current frontmost app once, immediately, through the same
        // handler used for every later switch.
        NSRunningApplication* frontmost = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (frontmost) {
          NSNotification* initial = [NSNotification notificationWithName:NSWorkspaceDidActivateApplicationNotification
                                                                    object:nil
                                                                  userInfo:@{NSWorkspaceApplicationKey: frontmost}];
          [self appSwitched:initial];
        }
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
    vn_input_lookup_override(&g_vn_input, name, bundle_id,
                             &g_event_tap.delay_us, &g_event_tap.strategy);
    vn_debug_log("appSwitched: vn_engine_reset (app=%s)", name ? name : "?");
    vn_engine_reset();
    ax_front_app_changed(&g_ax, pid);
}

- (void)inputSourceChanged:(NSNotification *)notification {
    g_input_source_is_ime = input_source_is_composing_ime();
    // A source switch breaks word context; drop any half-composed Vietnamese
    // word so it doesn't leak across the switch (mirrors appSwitched's reset).
    vn_engine_reset();
    vn_debug_log("inputSourceChanged: g_input_source_is_ime=%d", g_input_source_is_ime);
}

@end
