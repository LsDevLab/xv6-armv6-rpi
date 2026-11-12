# Design Document: Framebuffer, Window Manager & GUI Subsystem

## 1. Current State Analysis

### 1.1 Kernel Framebuffer Driver (`framebuffer.c`)
The kernel initializes the display using the VideoCore Mailbox property tag channel (Channel 1):
- Resolution: $1024 \times 768$, 16-bit color depth (RGB565).
- Physical address of the framebuffer pointer is allocated by the GPU (`fbinfo.fbp`) and mapped in the kernel.
- The character device `/dev/fb` (major number 3) handles user-space `write()` calls using the `fb_pixel_t` structure:
  ```c
  typedef struct {
      uint x;
      uint y;
      u16 color;
      char ch;
      uint w;
      uint h;
      u16 *buffer;
  } fb_pixel_t;
  ```

### 1.2 The Bottleneck in Existing Code
In `usr/wm.c`, drawing operations call:
```c
void fb_draw_pixel(int fb_fd, int x, int y, u16 color) {
    fb_pixel_t p;
    p.x = x; p.y = y; p.color = color; ...
    write(fb_fd, &p, sizeof(p));
}
```
And inside `blit_window_rects()`:
```c
for (int y = 0; y < rc->h; y++) {
    for (int x = 0; x < rc->w; x++) {
        ...
        fb_draw_pixel(fb_fd, rc->x + x, rc->y + y, col);
    }
}
```
**Impact:**
- A single $400 \times 300$ window requires **120,000 distinct `write()` system calls** per frame.
- Even with the bulk `pix.buffer` path in `fbwrite()`, each pixel triggers a separate `copyin()` invocation inside a nested loop in the kernel.
- On a 700 MHz ARM1176 CPU with no hardware MMU acceleration for individual scalar copies, this causes severe stuttering, unresponsive input, and high CPU load.

---

## 2. Redesigned Rendering Architecture

### 2.1 Implemented Batch Scanline Blit Interface
To achieve smooth window rendering and eliminate the 120,000 syscall bottleneck, `fb_pixel_t` was extended with stride and sub-rectangle coordinates:
```c
typedef struct {
  uint x;
  uint y;
  u16 color;
  char ch;
  uint w;
  uint h;
  u16 *buffer;     // user-space pointer (nullable)
  uint stride;     // buffer stride in pixels (defaults to w if 0)
  uint sx;         // sub-rect X in buffer
  uint sy;         // sub-rect Y in buffer
} fb_pixel_t;
```

#### Kernel Implementation (`framebuffer.c` - `fbwrite`):
1. **Scanline Batch Copy**: For visible rectangles where `pix.buffer != 0`, the kernel iterates over rows ($0 \dots H-1$), calculating the contiguous source scanline slice:
   ```c
   uint src_offset = ((pix.sy + row) * stride + pix.sx) * sizeof(u16);
   u16 *dst = fb + (pix.y + row) * fbinfo.width + pix.x;
   copyin(curr_proc->pgdir, (char*)dst, (uint)pix.buffer + src_offset, copy_w * sizeof(u16));
   ```
   This executes only 1 bulk `copyin()` per scanline directly into the GPU framebuffer.
2. **Solid Rect Fill**: When `pix.buffer == 0 && pix.w > 0 && pix.h > 0`, `fb_fillrect()` fills horizontal scanlines in memory directly.
3. **User-Space Blit (`usr/wm.c`)**:
   `blit_window_rects()` computes visible clipped rectangles and issues **one single `write()` call per visible rectangle**:
   ```c
   fb_pixel_t p;
   p.x = rc->x; p.y = rc->y; p.w = rc->w; p.h = rc->h;
   p.stride = w->width; p.sx = rc->x - w->x; p.sy = rc->y - w->y;
   p.buffer = w->buffer;
   write(fb_fd, &p, sizeof(p));
   ```
   Redrawing a typical window now drops from **120,000 system calls to 1–4 system calls**, executing in sub-millisecond time.

### 2.2 Window Clipping & Occlusion
The current clipping logic in `usr/wm.c` implements Sutherland-Hodgman rectangle subtraction:
1. Every window maintains an off-screen backing buffer ($W \times H \times 2$ bytes).
2. The Window Manager maintains windows in Z-order (index 0 = background, index $N-1$ = topmost/focused).
3. For window $k$, its visible region is calculated by subtracting all higher windows ($k+1 \dots N-1$) from its bounding box.
4. Only the non-occluded rectangles are blitted to the display framebuffer.

### 2.3 Mouse Cursor Rendering & Compositing
A software cursor requires a "save-under" mechanism:
1. **Before drawing cursor**: Save the $16 \times 16$ pixel region of the display currently under the cursor position $(cx, cy)$ into a small cursor backup buffer.
2. **Draw cursor**: Draw the cursor sprite (arrow icon) with transparency mask at $(cx, cy)$.
3. **When cursor moves**:
   - Restore the saved background at the old position $(ox, oy)$.
   - Save the background at the new position $(nx, ny)$.
   - Draw the cursor sprite at $(nx, ny)$.

---

## 3. Window Manager & Client IPC

To support multiple applications (`sh`, `writer`, `drawer`, `calc`, `browser`):
```
Client Process (e.g. Writer)               Window Server (WM)
      |                                           |
      | --- 1. Request Window (width, height) --> |
      | <-- 2. Allocate ID & Shared Buffer / Pipe  |
      |                                           |
      | --- 3. Draw widgets into Local Buffer --> |
      | --- 4. Notify Dirty Rect (x, y, w, h) ---> |
      |                                           | Composites visible rects
      |                                           | to Framebuffer
      | <-- 5. Deliver Events (Mouse, Key, Focus) -|
```

### 3.1 Event Protocol
Messages sent from WM to clients:
```c
enum event_type {
    EV_MOUSE_MOVE,
    EV_MOUSE_DOWN,
    EV_MOUSE_UP,
    EV_KEY_DOWN,
    EV_KEY_UP,
    EV_WINDOW_FOCUS,
    EV_WINDOW_CLOSE,
    EV_WINDOW_PAINT
};

struct gui_event {
    enum event_type type;
    int window_id;
    int x;           // local window coordinates
    int y;
    int button;      // 1=left, 2=right, 3=middle
    int keycode;     // ASCII or scancode
};
```
