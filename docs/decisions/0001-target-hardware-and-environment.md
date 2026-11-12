# ADR 0001: Target Hardware and Emulation Strategy

## Status
Accepted

## Context
The goal is to build an interactive operating system with a graphical window manager, USB mouse/keyboard support, networking, and bundled GUI applications. 

The target board is the **Raspberry Pi 1 Model B** based on the Broadcom BCM2835 SoC (ARM1176JZF-S CPU core, ARMv6 architecture). This board presents specific hardware characteristics:
1. Video output is managed through the VideoCore IV GPU via the Mailbox property interface.
2. The USB host controller is the Synopsys DWC2 OTG controller.
3. The on-board 10/100 Ethernet interface is an SMSC LAN9512 chip wired internally to one of the downstream ports of the USB root hub.
4. Flashing an SD card for every single edit/compile cycle is slow and hampers rapid iterative testing.

## Decision
1. **Primary Target**: BCM2835 / Raspberry Pi 1 (Model B). All hardware drivers (Mailbox, Framebuffer, DWC2 USB host, LAN9512) will adhere to the BCM2835 peripheral specifications (physical base `0x20000000`, mapped to virtual `0xFE000000`).
2. **Dual-Environment Strategy (QEMU Emulation)**:
   - For rapid iterative development, testing, and debugging, we will configure QEMU (`qemu-system-arm`).
   - QEMU supports the `raspi0` and `raspi1ap` machine types, which emulate the BCM2835 SoC and VideoCore framebuffer.
   - For environments where serial / simple I/O is preferred, fallback emulation using `versatilepb` or headless UART can be used.
3. **Firmware & Deployment**:
   - For real hardware execution, standard Raspberry Pi boot firmware (`bootcode.bin`, `start.elf`, `config.txt`) on a FAT32 SD card will load our compiled `kernel.img`.
4. **Documentation**:
   - Every hardware choice, register mapping, and driver design must be documented under `docs/` with references to BCM2835 datasheets and verified open-source implementations (e.g. Baking Pi, Plan 9 bcm, Linux dwc2).

## Consequences
- High-speed development feedback loop via QEMU without physical re-flashing.
- Hardware-accurate drivers that boot on bare-metal Raspberry Pi 1 hardware.
