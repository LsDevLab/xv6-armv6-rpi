# System Architecture Overview

## 1. Project Vision
The objective of this project is to build an interactive, graphical Unix-like Operating System for the **Raspberry Pi 1 (Model B)** based on the ARMv6 port of MIT xv6 (`xv6-armv6-rpi`).

The target system features:
1. **Preemptive Multitasking / Multiprocess Kernel**: Clean Unix process model (`fork`, `exec`, `wait`, `exit`), virtual memory with ARMv6 MMU paging, file system, pipes, and inter-process communication (IPC).
2. **Graphical Window Manager (Windows GUI)**: Overlapping / composited windows, focus management, title bars, moving/resizing, buttons, text rendering, and high-performance blitting.
3. **USB Input Subsystem**: Native support for USB HID Keyboard and USB HID Mouse running via the BCM2835 Synopsys DWC2 USB Host Controller.
4. **Networking (TCP/IP Stack)**: Device driver for the on-board SMSC LAN9512 USB Ethernet controller (or SLIP/QEMU interface), integrated with a lightweight TCP/IP stack (e.g., lwIP or embedded TCP/IP) exposing a BSD-style socket interface.
5. **Bundled GUI Applications**:
   - **Writer**: Text editor / document reader.
   - **Drawer**: Pixel canvas drawing and painting utility.
   - **Calc**: Standard graphical desktop calculator.
   - **Simple Browser**: Minimal HTTP client with basic text and HTML rendering.

---

## 2. Target Hardware Specifications

| Component | Specification |
| :--- | :--- |
| **SoC** | Broadcom BCM2835 |
| **CPU Core** | ARM1176JZF-S (ARMv6 instruction set, VFPv2 floating point) |
| **Clock Speed** | 700 MHz default |
| **RAM** | 512 MB (or 256 MB on early revisions); shared with GPU |
| **GPU / VPU** | VideoCore IV @ 250 MHz |
| **Peripherals Base** | `0x20000000` (physical); mapped to `0xFE000000` (kernel virtual) |
| **Interrupt Controller** | BCM2835 ARM interrupt controller (ARM timer, UART, USB, DMA) |
| **System Timers** | BCM2835 System Timer (4 compare channels; Timer 3 used by xv6) |
| **Serial / UART** | Mini UART (BCM AUX MU) & ARM PL011 UART |
| **Video Output** | VideoCore Mailbox property channel framebuffer (`1024x768`, 16-bit RGB565 or 32-bit ARGB8888) |
| **USB Host** | Synopsys DesignWare Hi-Speed USB 2.0 OTG Controller (DWC2) |
| **Ethernet** | SMSC LAN9512 10/100 Mbps Ethernet (connected internally via USB) |

---

## 3. Current Codebase State (xv6-armv6-rpi)

### 3.1 Kernel Boot & Memory Initialization
- **`entry.s`**: Entry point at physical `0x8000` (`KERNLINK 0x80008000`). Switches CPU to SVC mode with IRQs disabled, sets temporary stack at `0x3000`, and invokes `mmuinit0()`.
- **`mmu.c`**:
  - Initializes Mini UART directly at physical MMIO `0x20215000` for early boot printing.
  - Builds the preliminary L1 translation table (mapping RAM and MMIO `0x20000000 -> 0xFE000000`).
  - Enables MMU, data cache, instruction cache, and jumps to `_pagingstart` in higher-half virtual space (`0x80008000+`).
- **`main.c` / `cmain()`**:
  - `mmuinit1()` & `machinit()`: CPU structures.
  - `uartinit()`: Full UART driver initialization.
  - `framebufferinit()`: Queries VideoCore mailbox (tag channel 1) for a 1024x768 16bpp framebuffer.
  - `uartkbdinit()`: Enables UART receive interrupts.
  - `mailboxinit()`: Prepares mailbox tag buffer.
  - `pinit()`, `tvinit()`: Process table and interrupt vectors.
  - `binit()`, `fileinit()`, `iinit()`, `ideinit()`: Buffer cache, file table, inode table, and embedded ramdisk (`memide`).
  - `timer3init()`: BCM2835 System Timer 3 ticks.
  - `kinit2()`: Adds remaining physical RAM to the free page allocator.
  - `userinit()`: Creates process 1 (`initcode.S` -> `usr/_init`).
  - `scheduler()`: Starts the preemptive round-robin scheduler.

### 3.2 Storage & File System
- **Embedded Root FS (`memide.c`)**: `fs.img` is compiled into the kernel binary (`_binary_fs_img_start`). The standard xv6 filesystem operates on this memory buffer, eliminating the immediate requirement for an SD card driver to run userland binaries.

### 3.3 Graphics & The Window Manager Subsystem
- **Framebuffer Driver (`framebuffer.c`)**: Exposes device `/dev/fb` (major 3). Supports both `fbwrite()` (bulk scanline blit, solid fill rect, character/pixel draw) and `fbread()` (scanline pixel readback from GPU memory to userspace via `copyout`).
- **Mouse Device Driver (`mouse.c`)**: Exposes device `/dev/mouse` (major 4) with a 64-event circular ring buffer (`mouse_event_t`), non-blocking reads, and write capabilities for event injection.
- **Window Manager (`usr/wm.c`)**:
  - Implements overlapping window rectangles using a 2D rectangle intersection/subtraction clipping algorithm (`rect_intersect`, `rect_subtract`, `compute_visible_rects`).
  - Renders a full desktop environment: teal wallpaper (`0x0410`), bottom system taskbar with window tabs, brand button, and live status indicator, and 20px window title bars with active (deep royal blue) and inactive (slate gray) highlights.
  - **Zero-Flicker Mouse Cursor**: Composites an $11 \times 16$ classic pointer arrow, saving the background with `fbread()` and restoring it with `fbwrite()` on every move for zero-ghosting, tear-free rendering.
  - **Window Interaction**: Full mouse hit-testing, active window raising on click, real-time title bar click-and-drag, `[X]` window closing, taskbar tab switching, and brand button terminal spawning.
  - **Dual Input Stream**: In addition to native USB `/dev/mouse` packets, supports full terminal keyboard mouse simulation (ANSI Arrow keys move cursor, Home/End click & drag, backtick toggles WASD mouse mode) alongside direct shell typing.
- **Optimizations Completed**:
  1. **Batch Scanline Blit & Read**: Replaced 120,000 per-pixel `copyin` system calls with bulk system calls (`fbwrite`/`fbread`).
  2. **Non-blocking I/O & Raw Keyboard**: Added `fcntl` (`O_NONBLOCK`) to pipes and immediate raw keystroke delivery in `uart_keyboard.c`.
  3. **Driver Switch Table**: Properly registered `devsw[FRAMEBUFFER]` and `devsw[MOUSE]` in `main.c`.

---

## 4. Subsystem Target Architecture

```
+-------------------------------------------------------------------+
|                           User Space                              |
|  +-------------------+  +------------------+  +----------------+  |
|  |     Writer        |  |     Drawer       |  | Simple Browser |  |
|  |  (Text Editor)    |  |  (Paint Tool)    |  |  (HTTP/HTML)   |  |
|  +---------+---------+  +--------+---------+  +-------+--------+  |
|            |                     |                    |           |
|            +-----------------+   |   +----------------+           |
|                              |   |   |                            |
|                     +--------v---v---v-------+                    |
|                     |     GUI Toolkit /      |                    |
|                     |     libgui / libdraw   |                    |
|                     +------------+-----------+                    |
|                                  | (IPC: Shared Mem & Event Msgs) |
|                     +------------v-----------+                    |
|                     |     Window Manager     |                    |
|                     |     Server (wm)        |                    |
|                     +---+----------------+---+                    |
+-------------------------|----------------|------------------------+
| Kernel Boundary         |                |                        |
+-------------------------v----------------v------------------------+
|                     Syscalls & Device Drivers                     |
|  +-----------------+  +------------------+  +------------------+  |
|  |   Framebuffer   |  |    USB Input     |  |   TCP/IP Stack   |  |
|  |   (/dev/fb or   |  |  (DWC2 Host, HID |  |  (SMSC LAN9512,  |  |
|  |    mmap shm)    |  |  Keyboard/Mouse) |  |   lwIP/Sockets)  |  |
|  +--------+--------+  +--------+---------+  +--------+---------+  |
|           |                    |                     |            |
|  +--------v--------------------v---------------------v---------+  |
|  |           BCM2835 SoC Hardware (Mailbox, USB, DMA)          |  |
+-------------------------------------------------------------------+
```
