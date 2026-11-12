# Operating System Development Roadmap

This document outlines the master implementation plan for extending `xv6-armv6-rpi` into a full-featured graphical operating system for the Raspberry Pi 1.

---

## Phase 1: Foundation, Build System & Emulation Workflow
* **Objective**: Ensure reproducible builds, automated QEMU testability alongside real Raspberry Pi 1 SD card deployment, and diagnostic logging.
* **Key Deliverables**:
  1. Fix / optimize QEMU configuration in `Makefile` for headless and graphical runs.
  2. Implement clean kernel debug output (clean UART output, panic handling with stack trace).
  3. Document SD card flashing instructions for Raspberry Pi 1 Model B.
  4. Ensure standard library (`ulib.c`, `printf.c`, `umalloc.c`) has necessary memory helpers (`memcpy`, `memset`, dynamic memory).

---

## Phase 2: High-Performance Graphics & IPC [COMPLETED]
* **Objective**: Fix the critical rendering bottleneck in the current Window Manager prototype and establish fast inter-process communication (IPC) for graphical clients.
* **Key Deliverables**:
  1. [x] **Fast Framebuffer Blit**:
     - Bulk rectangle and scanline copyin blitting in `framebuffer.c` (`write` with `stride`, `sx`, `sy`).
     - Fixed `framebuffer_init()` registration in `devsw` to activate `/dev/fb`.
  2. [x] **Window Manager IPC & Non-blocking I/O**:
     - Implemented `SYS_fcntl` with `O_NONBLOCK` for asynchronous pipe reading in `wm.c`.
     - Raw character mode in `uart_keyboard.c` for real-time keystroke responsiveness.
  3. [x] **Desktop Rendering & Window Titles**:
     - Full teal desktop wallpaper (`0x0410`) and system taskbar.
     - 20-pixel styled title bars (Royal Blue active, Slate Gray inactive), text glyph rendering, borders, and close button.
     - Text scrolling and backspace in window text engine.

---

## Phase 3: Windows GUI Subsystem & `libgui` [IN PROGRESS]
* **Objective**: Transform `wm.c` into a robust Window Server and provide a user-space GUI library for writing desktop applications.
* **Key Deliverables**:
  1. [x] **Window Server (`wm`) Core Features**:
     - Desktop background rendering (`0x0410` teal canvas).
     - Window decoration: Title bar with title text, draggable window frame, active/inactive styling, close button.
     - Window stacking order (Z-order) and occlusion management.
     - Zero-flicker mouse cursor rendering with saved background restore via `fbread`.
     - Window hit-testing, real-time title bar click-and-drag, and window closing.
     - Focus switching via mouse click, taskbar tabs, or keyboard hotkeys (`Tab`, `1`, `2`).
  2. [ ] **GUI Client Toolkit (`libgui`)**:
     - Window creation and event pump (`gui_create_window`, `gui_poll_event`).
     - Drawing primitives: `draw_rect`, `draw_filled_rect`, `draw_line` (Bresenham), `draw_circle`, `draw_text`.
     - Standard controls / widgets:
       - Button (normal, hover, clicked states)
       - Label / Text box
       - Canvas (raw pixel buffer)
       - Menu / Status bar

---

## Phase 4: USB Subsystem & Input Drivers (Mouse & Keyboard) [COMPLETED]
* **Objective**: Enable physical USB keyboard and USB optical/laser mouse on the Raspberry Pi 1 hardware and QEMU.
* **Key Deliverables**:
  1. [x] **Kernel Mouse Input Device (`/dev/mouse`)**:
     - Implemented `/dev/mouse` (major 4) in `mouse.c` with 64-event ring buffer, non-blocking reads, and write support.
     - Created `mouse_event_t` struct (`dx`, `dy`, `buttons`).
     - Implemented `fbread()` in `framebuffer.c` for reading screen regions behind the cursor.
     - Implemented `_mousetest` userland automated verification tool.
  2. [x] **Dual-Environment Mouse Support (QEMU & Hardware)**:
     - Full terminal keyboard mouse simulation: Arrow keys (`\033[A/B/C/D`) move cursor; Home/End click & drag; backtick (`` ` ``) toggles WASD Mouse Mode.
     - Unified event stream: hardware USB mouse packets and simulated events feed into the exact same Window Manager input loop.
  3. [x] **Raspberry Pi 1 USB Hardware Integration (CSUD)**:
     - Synopsys DWC2 USB Host Controller initialization on BCM2835 (`0xFE980000`).
     - Ported and integrated Chadderz's Simple USB Driver (CSUD) with higher-half DMA translation.
     - Root Hub and downstream USB Hub enumeration (with support for self-powered hubs and zero-length status packets).
     - USB HID Class Drivers for USB Mouse (`MousePoll`, delta tracking) and USB Keyboard (`KeyboardUpdate`, scan code to ASCII).
     - Periodic polling hooked to hardware timer interrupts (`timer3intr()`) and device `read()` calls.

---

## Phase 5: Networking & TCP/IP Stack [IN PROGRESS]
* **Objective**: Connect the Raspberry Pi to the local network and internet.
* **Key Deliverables**:
  1. [x] **Network Hardware Driver (USB CDC-ECM & Ethernet Layer)**:
     - Implemented USB CDC-ECM driver (`csud/source/device/net/ecm.c`) with automatic detection of CDC Communications and CDC Data interfaces.
     - Implemented DWC2 USB bulk DMA endpoint transfers (`HcdSubmitBulkMessage`) in `designware20.c` with split transaction handling and non-blocking NAK management.
     - Implemented kernel network subsystem (`net.c`, `net.h`) providing `net_tx()` and `net_rx()` DMA framing.
     - Verified end-to-end layer 2 Ethernet networking in QEMU: Transmitted 60-byte ARP request to virtual gateway `10.0.2.2`, received and decoded live 64-byte ARP reply with gateway MAC `52:55:0a:00:02:02`.
  2. [ ] **TCP/IP Protocol Stack**:
     - Integration of lwIP (lightweight IP) or an xv6-tailored micro-stack:
       - Ethernet framing (MAC / IEEE 802.3).
       - ARP (Address Resolution Protocol) cache.
       - IPv4 packet handling, ICMP echo (ping).
       - UDP support (for DNS query resolution).
       - TCP support (three-way handshake, sequence numbers, windowing, retransmission, teardown).
  3. **BSD Socket Syscalls**:
     - `socket()`, `bind()`, `connect()`, `send()`, `recv()`, `close()`.
     - DNS resolver for domain name lookups (`gethostbyname`).

---

## Phase 6: Bundled Software Suite
* **Objective**: Deliver the core bundled applications that make the OS immediately useful and interactive.
* **Key Deliverables**:
  1. **Writer (`_writer`)**:
     - Text editor application with cursor navigation, text editing, scrollbar, file Open and Save dialogs.
  2. **Drawer (`_drawer`)**:
     - Paint application featuring a color palette, brush size selector, pencil/freehand tool, line tool, rectangle tool, bucket fill, and canvas clear.
  3. **Calc (`_calc`)**:
     - Graphical calculator featuring a 0-9 numerical keypad, arithmetic operators (`+`, `-`, `*`, `/`), decimal point, clear (`C`), and result display.
  4. **Simple Browser (`_browser`)**:
     - URL entry bar and status bar.
     - HTTP GET client over TCP socket port 80.
     - Minimal HTML parser and layout engine (headings `<h1>`-`<h3>`, paragraphs `<p>`, line breaks `<br>`, hyperlinked anchor tags `<a href>`).
     - Clickable hyperlinks triggering page transitions.
