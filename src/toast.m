#import "toast.h"
#import <Cocoa/Cocoa.h>
#import "ax.h"

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
