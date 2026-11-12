# Design Document: Networking & TCP/IP Stack

## 1. Physical Layer & Ethernet Hardware

### 1.1 SMSC LAN9512 (Raspberry Pi 1 Model B)
On physical hardware, the Ethernet interface is provided by the **SMSC LAN9512** controller:
- Connects directly as a high-speed USB device to the internal USB hub.
- Operates as a USB-to-Ethernet bridge (`Vendor ID: 0x0424, Product ID: 0xec00`).
- Provides 10/100 Mbps full-duplex Ethernet with integrated PHY and MAC.
- Packets are transmitted and received over USB bulk endpoints:
  - **TX**: USB Bulk OUT endpoint (formatted with a 4-byte command header specifying packet length).
  - **RX**: USB Bulk IN endpoint (delivers Ethernet frames prefixed with a 4-byte status header).

### 1.2 QEMU Emulation Alternatives
For fast simulation in QEMU:
1. **USB Network Emulation**: `-device usb-net,netdev=net0 -netdev user,id=net0`.
2. **SLIP (Serial Line Internet Protocol)**: Ethernet frames or raw IP packets tunneled over a secondary UART (`/dev/ttyS1`), allowing easy bridging to the host machine via `slirp` or `socat` without complex hardware simulation.

---

## 2. TCP/IP Protocol Stack Design

```
+-----------------------------------------------------------+
|               User Space Application Layer                |
|            (Simple Browser, HTTP Client, Ping)            |
+-----------------------------+-----------------------------+
                              | Socket Syscalls
+-----------------------------v-----------------------------+
|                     BSD Socket API Layer                  |
|     socket(), bind(), connect(), listen(), send(), recv() |
+-----------------------------+-----------------------------+
                              |
+-----------------------------v-----------------------------+
|                     TCP / UDP Protocols                   |
|  - TCP: Connections, ACK, Sequence Tracking, Windowing    |
|  - UDP: Datagram Send/Receive, DNS Queries                |
+-----------------------------+-----------------------------+
                              |
+-----------------------------v-----------------------------+
|                      IPv4 / ICMP Layer                    |
|  - IPv4: Packet Addressing, Checksums, Routing            |
|  - ICMP: Echo Request / Echo Reply (ping)                 |
+-----------------------------+-----------------------------+
                              |
+-----------------------------v-----------------------------+
|                         ARP Layer                         |
|  - ARP Table (IP <-> MAC mapping), Resolution Requests     |
+-----------------------------+-----------------------------+
                              | Ethernet Frames
+-----------------------------v-----------------------------+
|                    Network Device Driver                  |
|             (LAN9512 USB Driver / SLIP Interface)         |
+-----------------------------------------------------------+
```

### 2.1 Stack Implementation Options
1. **lwIP (Lightweight IP)**:
   - Widely adopted, robust, fully conforming open-source TCP/IP stack tailored for embedded systems.
   - Provides minimal memory footprint (configurable buffers).
   - Implements full IPv4, ARP, ICMP, UDP, TCP, and BSD socket abstraction.
2. **Custom Minimal xv6 Stack**:
   - A lean implementation handling ARP, IPv4, ICMP, UDP, and client-mode TCP.
   - Ideal for pedagogical transparency and deep integration with xv6 processes.

### 2.2 Socket Syscalls to Add
```c
int socket(int domain, int type, int protocol);
int connect(int sockfd, const struct sockaddr *addr, int addrlen);
int send(int sockfd, const void *buf, int len, int flags);
int recv(int sockfd, void *buf, int len, int flags);
int closesocket(int sockfd);
```
These map cleanly into the xv6 `file` abstraction where a socket is represented by `FD_SOCKET` in `struct file`.
