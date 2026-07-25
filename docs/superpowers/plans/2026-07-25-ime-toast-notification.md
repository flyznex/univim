# IME Toast Notification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a text toast ("VI" or "EN") near the cursor when Vietnamese IME is toggled.

**Architecture:** Single Objective-C module creates a reusable borderless NSWindow. Position from Accessibility caret bounds with mouse fallback. Auto-dismiss via dispatch_after (1s).

**Tech Stack:** Cocoa (NSWindow, NSTextField), Accessibility API, GCD dispatch_after

## Global Constraints

- macOS 12.0+ target
- No external dependencies — Cocoa only (already linked)
- C99 + Objective-C
- Keep total new code <100 lines

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/toast.h` | Public API: `void toast_show(const char* text)` |
| `src/toast.m` | NSWindow creation, positioning, auto-dismiss |
| `src/vn_input.c` | Call `toast_show()` on toggle |
| `makefile` | Add `toast.om` to build |

---

### Task 1: Create toast module

**Files:**
- Create: `src/toast.h`
- Create: `src/toast.m`
- Modify: `makefile:15` (add `toast.om` to `_OBJ`)

**Interfaces:**
- Produces: `void toast_show(const char* text)` — called by vn_input.c

- [ ] **Step 1: Create toast.h**

```c
#pragma once

void toast_show(const char* text);
```

- [ ] **Step 2: Create toast.m**

```objc
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
```

- [ ] **Step 3: Update makefile**

In `makefile` line 15, add `toast.om` to `_OBJ`:

```makefile
_OBJ = helpers.om helpers.o workspace.om event_tap.o ax.o buffer.o line.o env_vars.o vn_engine.o vn_input.o toast.om
```

- [ ] **Step 4: Verify build compiles**

Run: `make clean && make`
Expected: Build succeeds, produces `bin/svim`

- [ ] **Step 5: Commit**

```bash
git add src/toast.h src/toast.m makefile
git commit -m "feat: add toast notification module"
```

---

### Task 2: Integrate toast with IME toggle

**Files:**
- Modify: `src/vn_input.c:1-5` (add include)
- Modify: `src/vn_input.c:189-202` (add toast_show call)

**Interfaces:**
- Consumes: `void toast_show(const char* text)` from toast.h

- [ ] **Step 1: Add include to vn_input.c**

At line 5, after existing includes, add:

```c
#include "toast.h"
```

- [ ] **Step 2: Call toast_show in vn_input_toggle**

In `vn_input_toggle()`, after line 191 (`vn_engine_reset();`), add:

```c
  toast_show(vn->enabled ? "VI" : "EN");
```

- [ ] **Step 3: Verify build**

Run: `make`
Expected: Build succeeds

- [ ] **Step 4: Manual test**

Run: `./bin/svim`
- Focus a text field (e.g., Notes, TextEdit)
- Press the Vietnamese toggle hotkey (default: Ctrl+Shift)
- Expected: Toast appears near cursor showing "VI" or "EN", disappears after 1 second

- [ ] **Step 5: Commit**

```bash
git add src/vn_input.c
git commit -m "feat: show toast on IME toggle"
```

---

## Summary

2 tasks, ~85 lines of new code. After completion:
- Toggle Vietnamese IME → toast appears near cursor
- Shows "VI" when enabled, "EN" when disabled
- Auto-dismisses after 1 second
