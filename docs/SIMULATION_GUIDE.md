# Simulation & Testing Guide

This guide details how to emulate, test, and debug the OS using **QEMU**, as well as how to deploy `kernel.img` to physical Raspberry Pi 1 hardware.

---

## 1. Prerequisites

Verify that the required tools are installed on your system:
* **ARM Cross-Compiler**: `arm-none-eabi-gcc`
* **QEMU ARM Emulator**: `qemu-system-arm` (version 7.0+ recommended; on macOS install via `brew install qemu`)

```bash
arm-none-eabi-gcc --version
qemu-system-arm --version
```

---

## 2. QEMU Emulation Modes

The build system provides three convenient simulation targets in the [Makefile](file:///Users/luigisc/Desktop/projects/xv6-rpi-1/Makefile):

### 2.1 Graphical Simulation Mode (`make qemu`)
Runs the OS with a native graphical window displaying the Raspberry Pi's $1024 \times 768$ framebuffer (colored boot logo, desktop, and window manager), while simultaneously connecting the serial console to your active terminal.

```bash
make qemu
```
*Equivalent command:*
```bash
qemu-system-arm -M raspi0 -kernel kernel.elf -serial null -serial stdio
```

* **Display**: A native Cocoa window opens on macOS rendering the VideoCore framebuffer.
* **Keyboard Input**: **Type in the terminal window where `make qemu` is running** (not the Cocoa graphical window). The serial console connects directly to the kernel's keyboard driver `/dev/uart_keyboard`.
  - Press `1` or `2` (or `Tab`) to switch active window focus.
  - Type shell commands (e.g. `ls`, `cat README`) into the active window.
* **To exit**: Focus the terminal window and press `Ctrl-A`, followed by `X` (or close the display window).

---

### 2.2 Headless Simulation Mode (`make qemu-nox`)
Runs the kernel without opening a GUI window. Useful for quick verification, CI/CD scripts, or pure console debugging.

```bash
make qemu-nox
```
*Equivalent command:*
```bash
qemu-system-arm -M raspi0 -kernel kernel.elf -display none -serial null -serial stdio
```

---

### 2.3 GDB Debugging Mode (`make qemu-gdb`)
Starts QEMU in a paused state, waiting for a GDB client to connect on TCP port `1234`.

**Step 1: Start QEMU in listen mode:**
```bash
make qemu-gdb
```

**Step 2: In a separate terminal, launch GDB:**
```bash
arm-none-eabi-gdb kernel.elf
```

**Step 3: Inside GDB, connect to QEMU:**
```gdb
(gdb) target remote :1234
(gdb) b cmain
(gdb) continue
```
You can now set breakpoints, step through assembly instructions (`stepi`), inspect memory (`x/16x $sp`), and print processor registers (`info registers`).

---

## 3. Important Technical Details

### Why `-M raspi0`?
* The Raspberry Pi Zero uses the exact same **Broadcom BCM2835 SoC** and **ARM1176JZF-S CPU** as the original Raspberry Pi 1 Model B.
* QEMU's `raspi0` machine accurately models:
  * ARMv6 instruction set and MMU
  * VideoCore IV Mailbox interface and GPU framebuffer
  * System Timer and Interrupt Controller
  * BCM2835 AUX Mini UART

### Why `-serial null -serial stdio`?
The BCM2835 SoC has two UARTs:
1. **UART0 (PL011)**: First serial device in QEMU (`-serial null`).
2. **UART1 (AUX Mini UART)**: Second serial device in QEMU (`-serial stdio`).
Because our kernel driver ([uart.c](file:///Users/luigisc/Desktop/projects/xv6-rpi-1/uart.c)) uses the Mini UART, the first `-serial` must be set to `null` and the second to `stdio` to see output in the terminal.

### Why `kernel.elf` instead of `kernel.img` in QEMU?
When loading a raw binary (`kernel.img`), QEMU defaults to loading at physical address `0x10000`. By passing the ELF binary (`kernel.elf`), QEMU reads the ELF program headers and automatically places the code at the correct load address (`0x8000`) and entry point `_start`.

---

## 4. Deploying to Physical Raspberry Pi 1 Hardware

To run on a real Raspberry Pi (Model A, B, B+, or Zero):

1. **Format SD Card**: Format a micro-SD card with a **FAT32** partition (MBR partition table).
2. **Download Firmware**: Obtain the official Raspberry Pi boot firmware files:
   * `bootcode.bin`
   * `start.elf`
3. **Build Kernel Binary**:
   ```bash
   make clean && make
   ```
   This generates `kernel.img` in the project root.
4. **Copy to SD Card**:
   Copy `bootcode.bin`, `start.elf`, and `kernel.img` to the root directory of the SD card.
5. **Create `config.txt`**:
   Add a `config.txt` file in the SD root with:
   ```ini
   kernel=kernel.img
   arm_control=0x200
   core_freq=250
   gpu_mem=64
   framebuffer_width=1024
   framebuffer_height=768
   framebuffer_depth=16
   enable_uart=1
   ```
6. **Boot**: Insert the SD card into the Raspberry Pi, connect an HDMI display and USB keyboard/mouse, and power on.
