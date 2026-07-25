# IME Toast Notification Design

## Summary

Show a small text toast near the cursor when Vietnamese IME is toggled on/off, so the user knows which input mode is active.

## Requirements

- Display "VI" when Vietnamese enabled, "EN" when disabled
- Position near the text cursor (caret)
- Auto-dismiss after 1 second
- No external dependencies — use native macOS Cocoa APIs

## Architecture

### New Module: `toast.m` / `toast.h`

Single-file Objective-C module (~60-80 lines).

**Window:**
- Borderless `NSWindow` (no title bar, no shadow optional)
- `NSWindowLevelPopUpMenu` — floats above all windows
- Semi-transparent dark background, white text
- Reused across toggles (created once, shown/hidden)

**Text:**
- `NSTextField` with large, readable font
- Displays "VI" or "EN"

**Positioning:**
- Primary: Get caret screen rect via `kAXBoundsForRangeParameterizedAttribute` on the focused text element's selected range
- Fallback: Mouse cursor position via `NSEvent.mouseLocation` if caret bounds unavailable

**Auto-dismiss:**
- `dispatch_after` with 1.0 second delay
- Cancels previous pending dismiss if toggled again rapidly (avoids flicker)

### API

```c
// toast.h
#pragma once
void toast_show(const char* text);
```

### Integration

In `vn_input_toggle()` (`src/vn_input.c`):

```c
#include "toast.h"

void vn_input_toggle(struct vn_input* vn) {
  vn->enabled = !vn->enabled;
  vn_engine_reset();
  
  toast_show(vn->enabled ? "VI" : "EN");  // <-- add this
  
  // ... existing svim.sh call
}
```

## Files Changed

| File | Change |
|------|--------|
| `src/toast.h` | New — API declaration |
| `src/toast.m` | New — implementation |
| `src/vn_input.c` | Add `#include "toast.h"` and `toast_show()` call |
| `makefile` | Add `toast.om` to object list |

## Constraints

- Must run on main thread (already satisfied — CFRunLoop runs on main)
- No external libraries
- Keep code minimal (<100 lines total)
