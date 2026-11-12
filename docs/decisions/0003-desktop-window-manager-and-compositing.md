# Architectural Decision Record 0003: Desktop GUI Environment, Window Styling & Compositor Integration

## Status
**Accepted and Implemented**

## Context
In previous phases, the system suffered from three core issues:
1. System freeze due to blocking reads in `pipe.c` and buffered character delivery in `uart_keyboard.c`.
2. Rendering bottleneck from 120,000 per-pixel `copyin` operations per redraw.
3. `/dev/fb` system calls returning `-1` because `framebuffer_init()` was never called in `main.c`, leaving `devsw[FRAMEBUFFER].write` uninitialized (`NULL`).

Furthermore, the prototype window manager rendered raw unstyled blue boxes floating on a pitch-black background with no visual distinction between active and inactive windows, no title bars, and no OS desktop affordances (wallpaper, taskbar, system status).

## Decisions

### 1. Driver Registration
- In [main.c](file:///Users/luigisc/Desktop/projects/xv6-rpi-1/main.c), registered `/dev/fb` (major device 3) in `devsw` by invoking `framebuffer_init()`.
- Removed `memset(devsw, 0, ...)` from [framebuffer.c](file:///Users/luigisc/Desktop/projects/xv6-rpi-1/framebuffer.c) to prevent clobbering other device switch entries (`CONSOLE` and `UART_KEYBOARD`).

### 2. Desktop & Taskbar Architecture
- **Desktop Canvas ($1024 \times 736$)**:
  - Filled with classic system teal (`0x0410` in RGB565) in a single kernel blit via `fb_fillrect`.
- **System Taskbar ($1024 \times 32$ at $y = 736 \dots 768$)**:
  - Light gray background (`0xC618`) with 1-pixel white 3D bevel top line (`0xFFFF`).
  - System brand indicator: `[ Antigravity OS ]`.
  - Window tabs with active/inactive highlighting (`> [1] Term 1 <` vs `  [1] Term 1  `).
  - Help navigation hint: `Press Tab or [1]/[2] to focus`.

### 3. Window Decoration & Typography
- **Window Geometry**: $460 \times 320$ pixels, supporting terminal matrices of 56 columns $\times$ 18 rows.
- **Title Bar**:
  - Height: 20 pixels ($y = 1 \dots 19$).
  - Active Window: Deep Royal Blue (`COLOR_TITLE_ACTIVE = 0x19B9`).
  - Inactive Window: Slate Gray (`COLOR_TITLE_INACTIVE = 0x4A69`).
  - Outer 1px border: Bright White (`0xFFFF`) for active, Muted Gray (`0x7BEF`) for inactive.
  - Window Title: Rendered using font glyphs (8x16 bitmap) at offset $(x = 8, y = 2)$.
  - Window Controls: Red close box `[X]` at top-right.
- **Content Area Protection**:
  - Terminal text cursor starts below the title bar at $y = 22$.
  - Full scrolling support: when the terminal buffer fills, the content rows scroll up without corrupting the title bar or borders.
  - Full backspace (`\b` / ASCII 127) cell erasure.

### 4. Window Stacking & Z-Order Management
- Window list is ordered by stacking order: `windows[num_windows - 1]` is always the topmost (focused) window.
- Window tracking uses stable IDs (`win_id = 0`, `win_id = 1`) rather than transient array indices.
- Hotkey navigation:
  - `Tab`: Cycles focus to the next window.
  - `'1'`: Focuses Terminal 1.
  - `'2'`: Focuses Terminal 2.
- Occlusion engine: [compute_visible_rects()](file:///Users/luigisc/Desktop/projects/xv6-rpi-1/usr/wm.c) clips covered window rectangles against all windows above them in the Z-order stack. Only uncovered strips are blitted to the framebuffer.

### 5. Terminal Local Echo & Serial Raw Mode
- **Local Echo**: Typed printable characters, Backspace, and Newline are echoed directly to the active window backing buffer in real-time by the Window Manager.
- **QEMU Terminal Raw Mode**: Switched QEMU invocation in `Makefile` to `-serial null -serial mon:stdio`, which automatically places the host terminal into unbuffered raw mode, delivering single keystrokes (`1`, `2`, `Tab`) instantly without requiring Enter.
- **On-Screen Visual Indicators**: Added top banner instructions on the desktop wallpaper and a real-time key/status display in the bottom taskbar showing every key received.

## Consequences & Verification
- When running `make qemu`, the user sees a complete graphical desktop with teal wallpaper, bottom taskbar, and two styled terminal windows running interactive shells.
- Redraw performance is instantaneous with scanline bulk transfers.
- Keystrokes are delivered to the currently focused window only, and pressing `Tab`, `'1'`, or `'2'` updates the active title bar highlight, taskbar indicator, and window layer cleanly.
