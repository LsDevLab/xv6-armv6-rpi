#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "net.h"

extern int  CdcEcmIsAttached(void);
extern void CdcEcmGetMac(uchar mac[6]);
extern int  CdcEcmTx(const void *buf, uint len);
extern int  CdcEcmRx(void *buf, uint max_len, uint *transferred);

static inline ushort htons(ushort x) {
  return ((x & 0xFF) << 8) | ((x >> 8) & 0xFF);
}
#define ntohs htons

static void
print_mac(uchar mac[6])
{
  for (int i = 0; i < 6; i++) {
    if (mac[i] < 0x10) cprintf("0");
    cprintf("%x", mac[i]);
    if (i < 5) cprintf(":");
  }
}

int
net_is_up(void)
{
  return CdcEcmIsAttached();
}

void
net_get_mac(uchar mac[ETH_ALEN])
{
  CdcEcmGetMac(mac);
}

int
net_tx(const void *buf, int len)
{
  static uchar dma_tx[ETH_FRAME_LEN] __attribute__((aligned(4)));
  if (!CdcEcmIsAttached() || len <= 0 || len > ETH_FRAME_LEN)
    return -1;
  memmove(dma_tx, buf, len);
  return CdcEcmTx(dma_tx, len);
}

int
net_rx(void *buf, int max_len)
{
  static uchar dma_rx[ETH_FRAME_LEN] __attribute__((aligned(4)));
  uint transferred = 0;
  if (!CdcEcmIsAttached() || max_len <= 0)
    return 0;

  int res = CdcEcmRx(dma_rx, ETH_FRAME_LEN, &transferred);
  if (res == 0 && transferred > 0) {
    int copy_len = transferred > (uint)max_len ? (uint)max_len : transferred;
    memmove(buf, dma_rx, copy_len);
    return copy_len;
  }
  return 0;
}

int
net_arp_test(void)
{
  cprintf("NET: Testing ARP resolution for QEMU Gateway (10.0.2.2)...\n");

  uchar my_mac[6];
  CdcEcmGetMac(my_mac);

  static struct {
    struct eth_hdr eth;
    ushort hw_type;
    ushort proto_type;
    uchar  hw_len;
    uchar  proto_len;
    ushort opcode;
    uchar  sender_mac[6];
    uchar  sender_ip[4];
    uchar  target_mac[6];
    uchar  target_ip[4];
  } __attribute__((packed, aligned(4))) req;

  // Fill Ethernet header
  memset(req.eth.dst, 0xFF, 6);
  memmove(req.eth.src, my_mac, 6);
  req.eth.type = htons(ETHERTYPE_ARP);

  // Fill ARP payload
  req.hw_type = htons(1);
  req.proto_type = htons(0x0800);
  req.hw_len = 6;
  req.proto_len = 4;
  req.opcode = htons(ARP_REQUEST);

  memmove(req.sender_mac, my_mac, 6);
  req.sender_ip[0] = 10; req.sender_ip[1] = 0; req.sender_ip[2] = 2; req.sender_ip[3] = 15;

  memset(req.target_mac, 0x00, 6);
  req.target_ip[0] = 10; req.target_ip[1] = 0; req.target_ip[2] = 2; req.target_ip[3] = 2;

  // Ethernet frames must be at least 60 bytes (plus 4-byte FCS)
  static uchar tx_frame[64] __attribute__((aligned(4)));
  memset(tx_frame, 0, sizeof(tx_frame));
  memmove(tx_frame, &req, sizeof(req));

  int res = net_tx(tx_frame, 60);
  if (res != 0) {
    cprintf("NET: Failed to transmit ARP request (res=%d).\n", res);
    return -1;
  }
  cprintf("NET: Transmitted ARP Request: Who has 10.0.2.2? Tell 10.0.2.15\n");

  // Poll for ARP Reply
  static uchar rx_frame[1536] __attribute__((aligned(4)));
  for (int tries = 0; tries < 200; tries++) {
    int len = net_rx(rx_frame, sizeof(rx_frame));
    if (len > 0) {
      struct eth_hdr *eh = (struct eth_hdr*)rx_frame;
      if (ntohs(eh->type) == ETHERTYPE_ARP && len >= (int)sizeof(struct arp_hdr)) {
        struct arp_hdr *ah = (struct arp_hdr*)rx_frame;
        if (ntohs(ah->opcode) == ARP_REPLY) {
          cprintf("NET: [SUCCESS] Received ARP Reply from %d.%d.%d.%d!\n",
                  ah->sender_ip[0], ah->sender_ip[1], ah->sender_ip[2], ah->sender_ip[3]);
          cprintf("NET: Gateway MAC: ");
          print_mac(ah->sender_mac);
          cprintf(" (frame len=%d bytes)\n", len);
          return 0;
        }
      }
    }
    // Small pause between polls (~50 microseconds)
    for (volatile int d = 0; d < 500; d++);
  }

  cprintf("NET: ARP test timed out waiting for reply.\n");
  return -1;
}

void
net_init(void)
{
  if (!CdcEcmIsAttached()) {
    cprintf("NET: USB network interface not found.\n");
    return;
  }
  uchar mac[6];
  CdcEcmGetMac(mac);
  cprintf("NET: USB Ethernet interface active (MAC: ");
  print_mac(mac);
  cprintf(")\n");

  net_arp_test();
}
