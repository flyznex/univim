#import "toast.h"
#import "vn_input.h"
#import <Cocoa/Cocoa.h>
#import "ax.h"

// Click/menu controller for the status item. Gives the item a real menu so
// Golden Gate (single-window menubar, native overflow) and third-party
// managers treat it as a first-class item instead of a hidable text label.
@interface UnivimStatusController : NSObject
@end
@implementation UnivimStatusController
- (void)toggle:(id)sender {
  vn_input_toggle(&g_vn_input);
}
- (void)quit:(id)sender {
  [NSApp terminate:nil];
}
@end
static UnivimStatusController* g_status_controller = nil;

static NSWindow* g_toast_window = nil;
static dispatch_block_t g_pending_dismiss = nil;

static NSPoint get_cursor_position(void) {
    // Try to get caret position from focused element via Accessibility
    if (g_ax.selected_element) {
        CFTypeRef range_ref = NULL;
        AXError err = AXUIElementCopyAttributeValue(g_ax.selected_element,
                                                    kAXSelectedTextRangeAttribute,
                                                    &range_ref);
        if (err == kAXErrorSuccess && range_ref) {
            CFTypeRef bounds_ref = NULL;
            AXError bounds_err = AXUIElementCopyParameterizedAttributeValue(
                g_ax.selected_element,
                kAXBoundsForRangeParameterizedAttribute,
                range_ref,
                &bounds_ref);
            CFRelease(range_ref);
            
            if (bounds_err == kAXErrorSuccess && bounds_ref) {
                CGRect caret_rect;
                if (AXValueGetValue(bounds_ref, kAXValueCGRectType, &caret_rect)) {
                    CFRelease(bounds_ref);
                    // Convert from top-left origin to bottom-left origin (Cocoa)
                    NSRect screen = [[NSScreen mainScreen] frame];
                    return NSMakePoint(caret_rect.origin.x + caret_rect.size.width,
                                       screen.size.height - caret_rect.origin.y - caret_rect.size.height);
                }
                CFRelease(bounds_ref);
            }
        }
    }
    // Fallback: mouse position
    return [NSEvent mouseLocation];
}

static void create_window_if_needed(void) {
    if (g_toast_window) return;
    
    NSRect frame = NSMakeRect(0, 0, 40, 28);
    g_toast_window = [[NSWindow alloc]
        initWithContentRect:frame
        styleMask:NSWindowStyleMaskBorderless
        backing:NSBackingStoreBuffered
        defer:NO];
    
    [g_toast_window setLevel:NSPopUpMenuWindowLevel];
    [g_toast_window setBackgroundColor:[NSColor colorWithWhite:0.15 alpha:0.9]];
    [g_toast_window setOpaque:NO];
    [g_toast_window setHasShadow:YES];
    [g_toast_window setIgnoresMouseEvents:YES];
    
    NSTextField* label = [[NSTextField alloc] initWithFrame:frame];
    [label setBezeled:NO];
    [label setDrawsBackground:NO];
    [label setEditable:NO];
    [label setSelectable:NO];
    [label setAlignment:NSTextAlignmentCenter];
    [label setFont:[NSFont boldSystemFontOfSize:16]];
    [label setTextColor:[NSColor whiteColor]];
    [[g_toast_window contentView] addSubview:label];
}

void toast_show(const char* text) {
    create_window_if_needed();
    
    // Cancel pending dismiss
    if (g_pending_dismiss) {
        dispatch_block_cancel(g_pending_dismiss);
        g_pending_dismiss = nil;
    }
    
    NSString* str = [NSString stringWithUTF8String:text];
    NSTextField* label = [[[g_toast_window contentView] subviews] firstObject];
    [label setStringValue:str];
    
    NSPoint pos = get_cursor_position();
    [g_toast_window setFrameOrigin:NSMakePoint(pos.x + 8, pos.y - 28)];
    [g_toast_window orderFrontRegardless];
    
    // Auto-dismiss after 1 second
    g_pending_dismiss = dispatch_block_create(0, ^{
        [g_toast_window orderOut:nil];
        g_pending_dismiss = nil;
    });
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.0 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(),
                   g_pending_dismiss);
}

// MARK: - Menu bar status item

static NSStatusItem* g_status_item = nil;

// Native menu-font metrics via attributedTitle (Hammerspoon-style): no
// explicit color so the button keeps its vibrancy/highlight behavior.
static void set_status_title(NSString* title) {
    NSFont* font = [NSFont monospacedSystemFontOfSize:[NSFont labelFontSize]
                                               weight:NSFontWeightMedium];
    NSAttributedString* attr = [[NSAttributedString alloc]
        initWithString:title
            attributes:@{NSFontAttributeName: font}];
    [g_status_item.button setAttributedTitle:attr];
}

void statusbar_init(void) {
    if (g_status_item) return;
    g_status_item = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    // Persist visibility across relaunches; required for Control Center /
    // manager allowlists on macOS 27 Golden Gate (single-window menubar).
    // Note: isVisible is readonly -- creating the item shows it; autosaveName
    // lets the system remember the user's choice instead of re-hiding it.
    g_status_item.autosaveName = @"org.univim.status";
    // Text-only label: compact, the classic EN/VI look. (A template image
    // was tried here and reverted -- it ate menubar space next to the text.)
    set_status_title(@"EN");
    g_status_controller = [[UnivimStatusController alloc] init];
    NSMenu* menu = [[NSMenu alloc] init];
    NSMenuItem* toggle = [[NSMenuItem alloc] initWithTitle:@"Toggle VI/EN"
                                                    action:@selector(toggle:)
                                             keyEquivalent:@""];
    toggle.target = g_status_controller;
    [menu addItem:toggle];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:@"Quit UniVim"
                                                  action:@selector(quit:)
                                           keyEquivalent:@"q"];
    quit.target = g_status_controller;
    [menu addItem:quit];
    g_status_item.menu = menu;
}

void statusbar_update(const char* text) {
    if (!g_status_item) return;
    NSString* title = [NSString stringWithUTF8String:text];
    if ([NSThread isMainThread]) {
        set_status_title(title);
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
            set_status_title(title);
        });
    }
}
