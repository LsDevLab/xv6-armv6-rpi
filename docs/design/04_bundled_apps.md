# Design Document: Bundled Graphical Applications

## 1. Overview
The OS desktop will bundle four core applications to demonstrate the capabilities of the window manager, 2D graphics toolkit, file system, and TCP/IP networking stack:
1. **Writer**: Document & text editor.
2. **Drawer**: Freehand and geometric drawing program.
3. **Calc**: Desktop calculator.
4. **Simple Browser**: HTTP browser with basic HTML rendering.

---

## 2. Common GUI Framework (`libgui`)
Each application links against `libgui`, which provides:
- Window creation, resize, and destruction.
- Event loop dispatching (`on_mouse_down`, `on_mouse_up`, `on_mouse_move`, `on_key_press`, `on_paint`).
- 2D Drawing Library (`libdraw`):
  - `draw_rect(x, y, w, h, color)`
  - `fill_rect(x, y, w, h, color)`
  - `draw_line(x0, y0, x1, y1, color)`
  - `draw_text(x, y, const char *str, color)`
- Standard UI Widgets:
  - `ui_button(rect, label, on_click)`
  - `ui_label(rect, text)`
  - `ui_text_input(rect, buffer, max_len)`
  - `ui_canvas(rect, on_paint)`

---

## 3. Application Specifications

### 3.1 Writer (Text / Document Editor)
* **Window Size**: $480 \times 320$ pixels.
* **Features**:
  - Multi-line text buffer with dynamic cursor navigation (arrow keys, backspace, enter).
  - Top Toolbar: [New] [Open] [Save] buttons.
  - Status bar at bottom showing cursor position (Line, Column) and file name.
  - File I/O using xv6 standard file system calls (`open`, `read`, `write`, `close`).

### 3.2 Drawer (Paint & Canvas Tool)
* **Window Size**: $500 \times 380$ pixels.
* **Features**:
  - Drawing Canvas ($420 \times 340$) maintaining an independent pixel bitmap.
  - Color Palette: 8 standard colors (Black, White, Red, Green, Blue, Yellow, Cyan, Magenta).
  - Tools:
    - **Pencil / Freehand**: Draws continuous pixels while left mouse button is pressed.
    - **Line**: Draws a straight line between mouse down and mouse up points.
    - **Rectangle**: Draws filled or outlined rectangles.
    - **Clear**: Clears the canvas to white.
  - Export/Save canvas to raw image / BMP format on disk.

### 3.3 Calc (Desktop Calculator)
* **Window Size**: $220 \times 280$ pixels.
* **Features**:
  - Numerical LCD-style display at the top showing current input and calculated result.
  - Grid of clickable buttons:
    - Digits: `0` through `9`
    - Operations: `+`, `-`, `*`, `/`
    - Actions: `=`, `C` (Clear)
  - Keyboard shortcut integration: Typing numbers or operators on the physical keyboard triggers matching button actions.

### 3.4 Simple Browser (HTTP Client & HTML Viewer)
* **Window Size**: $560 \times 400$ pixels.
* **Features**:
  - Address Bar at the top with a [Go] button for URL entry (e.g. `http://example.com/` or IP address).
  - Network Communication:
    - Parses host and path from URL.
    - Resolves host via DNS (or takes IP directly).
    - Opens TCP connection on port 80.
    - Sends HTTP request: `GET /path HTTP/1.0\r\nHost: hostname\r\n\r\n`.
    - Reads HTTP response headers and payload body.
  - Lightweight HTML Parser & Formatter:
    - Recognizes tags: `<h1>`, `<h2>`, `<p>`, `<br>`, `<b>`, and `<a href="...">`.
    - Renders text with appropriate margins, sizes, and colors.
    - Hyperlink navigation: Clicking blue underlined links issues a new HTTP GET request.
