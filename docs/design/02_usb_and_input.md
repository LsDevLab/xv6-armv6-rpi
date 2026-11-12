# Design Document: USB Subsystem & Input Drivers

## 1. Hardware Architecture: BCM2835 USB

### 1.1 Synopsys DesignWare Hi-Speed USB 2.0 OTG (DWC2)
The Broadcom BCM2835 SoC on the Raspberry Pi 1 integrates a single Synopsys DWC2 USB host controller.
- **Physical Base Address**: `0x20980000` (mapped to virtual `0xFE980000`).
- **Interrupt Line**: BCM2835 IRQ 9 (Basic IRQ pending register or USB interrupt bit 9 in IRQ pending 1/2).
- **USB Architecture**:
  - The SoC provides only **one** physical USB OTG port.
  - On the Raspberry Pi Model B, this single port is hardwired to an on-board **SMSC LAN9512** controller, which acts as:
    1. A 3-port USB 2.0 Hub (downstream Port 1 = USB port A, Port 2 = USB port B, Port 3 = internal Ethernet MAC/PHY).
    2. A 10/100 Mbps Ethernet controller.

```
       +-----------------------+
       |   BCM2835 SoC CPU     |
       +-----------+-----------+
                   |
       +-----------v-----------+
       |   DWC2 USB Controller |
       +-----------+-----------+
                   | USB Host Port
       +-----------v-----------+
       |    SMSC LAN9512 Hub   |
       +---+---------------+---+
           |               |               |
     +-----v-----+   +-----v-----+   +-----v-----+
     | Downstream|   | Downstream|   | Internal  |
     | Port 1    |   | Port 2    |   | Ethernet  |
     | (USB Keyb)|   | (USB Mouse|   | MAC/PHY   |
     +-----------+   +-----------+   +-----------+
```

---

## 2. Driver Implementation Strategy

### 2.1 Driver Options for Bare-Metal / Minimal OS
1. **CSUD (Chadderz's Simple USB Driver)**:
   - Written by Alex Chadwick and Chadderz for the Cambridge "Baking Pi" operating system course.
   - Specifically written for Raspberry Pi 1 (BCM2835).
   - Features:
     - DWC2 host controller initialization.
     - Hub detection and port enumeration.
     - Human Interface Device (HID) class driver for USB Keyboards and USB Mice.
     - Modular C-callable assembly interface (`UsbInitialise()`, `KeyboardUpdate()`, `MouseUpdate()`).
   - Advantage: Pre-tailored for ARMv6 BCM2835, minimal memory footprint.
2. **USPi / Circle**:
   - Written by Rene Stange. C++ based clean DWC2 stack with LAN9512 driver.

### 2.2 Input Flow into the Window Server
```
Kernel Level
[USB Interrupt / Timer Poll]
          |
  [DWC2 Host Controller]
          |
  [HID Class Driver]
     /          \
[HID Keyboard]   [HID Mouse]
     |                 |
[/dev/kbd]       [/dev/mouse]
     |                 |
--------------------------------- User Space Boundary
     \                 /
     [ Window Server (wm) ]
               |
  (Updates Cursor Position,
   Click Detection, Focus,
   Dispatches Events to Clients)
```

### 2.3 Mouse Packet Format (`/dev/mouse`)
A standard USB HID mouse report delivers a 3- to 4-byte packet:
```c
struct mouse_packet {
    u8 buttons;    // Bit 0: Left, Bit 1: Right, Bit 2: Middle
    int8_t dx;     // Relative motion X (-128 to +127)
    int8_t dy;     // Relative motion Y (-128 to +127)
    int8_t wheel;  // Optional scroll wheel motion
};
```
The Window Server reads packets from `/dev/mouse`, clamps the cursor coordinate $(X, Y)$ within screen boundaries $[0 \dots 1024, 0 \dots 768]$, and routes clicks to the topmost window covering $(X, Y)$.

---

## 3. Emulation Strategy (QEMU)
To enable development without physical hardware:
1. In QEMU BCM2835 machines (`raspi0`, `raspi1ap`), USB devices can be attached using `-device usb-kbd` and `-device usb-mouse`.
2. Fallback: Serial UART Keyboard driver (`uart_keyboard.c`) remains available as a secondary input channel, ensuring terminal-based debugging never breaks.
