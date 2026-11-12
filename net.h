#ifndef _NET_H_
#define _NET_H_

#include "types.h"

#define ETH_ALEN 6
#define ETH_HLEN 14
#define ETH_FRAME_LEN 1514

#define ETHERTYPE_IP  0x0800
#define ETHERTYPE_ARP 0x0806

struct eth_hdr {
  uchar dst[ETH_ALEN];
  uchar src[ETH_ALEN];
  ushort type;
} __attribute__((packed));

struct arp_hdr {
  struct eth_hdr eth;
  ushort hw_type;
  ushort proto_type;
  uchar  hw_len;
  uchar  proto_len;
  ushort opcode;
  uchar  sender_mac[ETH_ALEN];
  uchar  sender_ip[4];
  uchar  target_mac[ETH_ALEN];
  uchar  target_ip[4];
} __attribute__((packed));

#define ARP_REQUEST 1
#define ARP_REPLY   2

void net_init(void);
int  net_is_up(void);
void net_get_mac(uchar mac[ETH_ALEN]);
int  net_tx(const void *buf, int len);
int  net_rx(void *buf, int max_len);
int  net_arp_test(void);

#endif
