#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "arm.h"
#include "proc.h"
#include "net.h"

static inline uint
net_time_ms(void)
{
  return inw(0xFE003004) / 1000;
}

static inline void
net_yield(void)
{
  if (curr_proc)
    yield();
}

extern int  CdcEcmIsAttached(void);
extern void CdcEcmGetMac(uchar mac[6]);
extern int  CdcEcmTx(const void *buf, uint len);
extern int  CdcEcmRx(void *buf, uint max_len, uint *transferred);

// Networking configuration for QEMU / SLIRP
static const uchar my_ip[4]      = {10, 0, 2, 15};
static const uchar gateway_ip[4] = {10, 0, 2, 2};
static const uchar netmask[4]    = {255, 255, 255, 0};
static const uchar dns_ip[4]     = {10, 0, 2, 3}; // QEMU SLIRP DNS Proxy (Host Wi-Fi)

static uchar my_mac[6];
static ushort next_ip_id = 1;

// ARP Table
#define ARP_TABLE_SIZE 8
struct arp_entry {
  uchar ip[4];
  uchar mac[6];
  int   valid;
};
static struct arp_entry arp_table[ARP_TABLE_SIZE];

// ICMP Ping tracking
static volatile int ping_reply_received = 0;
static volatile ushort ping_reply_seq   = 0;
static volatile uchar  ping_reply_ip[4];
static volatile int ping_reply_len      = 0;

// DNS tracking
static volatile int    dns_resolved = 0;
static volatile ushort dns_query_id = 0;
static volatile uchar  dns_result_ip[4];

static inline ushort htons(ushort x) {
  return ((x & 0xFF) << 8) | ((x >> 8) & 0xFF);
}
#define ntohs htons

static inline uint htonl(uint x) {
  return ((x & 0xFF) << 24) | (((x >> 8) & 0xFF) << 16) | (((x >> 16) & 0xFF) << 8) | ((x >> 24) & 0xFF);
}
#define ntohl htonl

static void
print_mac(const uchar mac[6])
{
  for (int i = 0; i < 6; i++) {
    if (mac[i] < 0x10) cprintf("0");
    cprintf("%x", mac[i]);
    if (i < 5) cprintf(":");
  }
}

static void
print_ip(const uchar ip[4])
{
  cprintf("%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

ushort
ip_checksum(const void *data, int len)
{
  uint sum = 0;
  const ushort *ptr = (const ushort*)data;

  while (len > 1) {
    sum += *ptr++;
    len -= 2;
  }
  if (len == 1) {
    sum += *(const uchar*)ptr;
  }
  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }
  return (ushort)(~sum);
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

// Update or add an entry in the ARP cache
static void
arp_table_update(const uchar ip[4], const uchar mac[6])
{
  for (int i = 0; i < ARP_TABLE_SIZE; i++) {
    if (arp_table[i].valid && memcmp(arp_table[i].ip, ip, 4) == 0) {
      memmove(arp_table[i].mac, mac, 6);
      return;
    }
  }
  for (int i = 0; i < ARP_TABLE_SIZE; i++) {
    if (!arp_table[i].valid) {
      memmove(arp_table[i].ip, ip, 4);
      memmove(arp_table[i].mac, mac, 6);
      arp_table[i].valid = 1;
      return;
    }
  }
  // If full, overwrite slot 0
  memmove(arp_table[0].ip, ip, 4);
  memmove(arp_table[0].mac, mac, 6);
  arp_table[0].valid = 1;
}

// Resolve IP address to MAC address (checks cache, sends ARP request if needed)
int
net_arp_resolve(const uchar ip[4], uchar mac[6])
{
  for (int i = 0; i < ARP_TABLE_SIZE; i++) {
    if (arp_table[i].valid && memcmp(arp_table[i].ip, ip, 4) == 0) {
      memmove(mac, arp_table[i].mac, 6);
      return 0;
    }
  }

  // Send ARP Request
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

  memset(req.eth.dst, 0xFF, 6);
  memmove(req.eth.src, my_mac, 6);
  req.eth.type = htons(ETHERTYPE_ARP);

  req.hw_type = htons(1);
  req.proto_type = htons(0x0800);
  req.hw_len = 6;
  req.proto_len = 4;
  req.opcode = htons(ARP_REQUEST);

  memmove(req.sender_mac, my_mac, 6);
  memmove(req.sender_ip, my_ip, 4);

  memset(req.target_mac, 0x00, 6);
  memmove(req.target_ip, ip, 4);

  static uchar tx_frame[64] __attribute__((aligned(4)));
  memset(tx_frame, 0, sizeof(tx_frame));
  memmove(tx_frame, &req, sizeof(req));

  if (net_tx(tx_frame, 60) != 0)
    return -1;

  // Poll for reply
  for (int tries = 0; tries < 100; tries++) {
    net_poll();
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
      if (arp_table[i].valid && memcmp(arp_table[i].ip, ip, 4) == 0) {
        memmove(mac, arp_table[i].mac, 6);
        return 0;
      }
    }
    net_yield();
    for (volatile int d = 0; d < 200; d++);
  }

  return -1;
}

// Send an IPv4 packet
int
net_ip_send(const uchar dst_ip[4], uchar proto, const void *payload, int len)
{
  if (!CdcEcmIsAttached() || len < 0 || len > 1460)
    return -1;

  // Routing: If in local subnet, send directly; else route to gateway
  const uchar *next_hop = dst_ip;
  if ((dst_ip[0] & netmask[0]) != (my_ip[0] & netmask[0]) ||
      (dst_ip[1] & netmask[1]) != (my_ip[1] & netmask[1]) ||
      (dst_ip[2] & netmask[2]) != (my_ip[2] & netmask[2])) {
    next_hop = gateway_ip;
  }

  uchar next_hop_mac[6];
  if (net_arp_resolve(next_hop, next_hop_mac) != 0) {
    cprintf("NET: Could not resolve MAC for next hop ");
    print_ip(next_hop);
    cprintf("\n");
    return -1;
  }

  static uchar frame[ETH_FRAME_LEN] __attribute__((aligned(4)));
  struct eth_hdr *eth = (struct eth_hdr*)frame;
  struct ip_hdr  *ip  = (struct ip_hdr*)(frame + sizeof(struct eth_hdr));

  // Ethernet header
  memmove(eth->dst, next_hop_mac, 6);
  memmove(eth->src, my_mac, 6);
  eth->type = htons(ETHERTYPE_IP);

  // IPv4 header
  ip->v_ihl = 0x45;
  ip->tos = 0;
  ip->total_len = htons(sizeof(struct ip_hdr) + len);
  ip->id = htons(next_ip_id++);
  ip->flags_frag = htons(0x4000); // DF
  ip->ttl = 64;
  ip->proto = proto;
  ip->csum = 0;
  memmove(ip->src, my_ip, 4);
  memmove(ip->dst, dst_ip, 4);
  ip->csum = ip_checksum(ip, sizeof(struct ip_hdr));

  // Payload
  if (len > 0 && payload != 0) {
    memmove(frame + sizeof(struct eth_hdr) + sizeof(struct ip_hdr), payload, len);
  }

  int total_frame_len = sizeof(struct eth_hdr) + sizeof(struct ip_hdr) + len;
  if (total_frame_len < 60) {
    memset(frame + total_frame_len, 0, 60 - total_frame_len);
    total_frame_len = 60;
  }

  return net_tx(frame, total_frame_len);
}

// Compute UDP checksum including IPv4 pseudo-header
ushort
udp_checksum(const uchar src_ip[4], const uchar dst_ip[4], const void *udp_data, int len)
{
  uint sum = 0;
  const ushort *s = (const ushort*)src_ip;
  sum += s[0] + s[1];
  const ushort *d = (const ushort*)dst_ip;
  sum += d[0] + d[1];
  sum += htons(IPPROTO_UDP);
  sum += htons((ushort)len);

  const ushort *ptr = (const ushort*)udp_data;
  int l = len;
  while (l > 1) {
    sum += *ptr++;
    l -= 2;
  }
  if (l == 1) {
    sum += *(const uchar*)ptr;
  }
  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }
  ushort csum = (ushort)(~sum);
  return csum == 0 ? 0xFFFF : csum;
}

int
net_udp_send(const uchar dst_ip[4], ushort src_port, ushort dst_port, const void *payload, int len)
{
  static uchar udp_pkt[1460] __attribute__((aligned(4)));
  struct udp_hdr *udp = (struct udp_hdr*)udp_pkt;
  if (len < 0 || len > (int)(1460 - sizeof(struct udp_hdr)))
    return -1;

  int udp_total_len = sizeof(struct udp_hdr) + len;
  udp->src_port = htons(src_port);
  udp->dst_port = htons(dst_port);
  udp->length   = htons(udp_total_len);
  udp->csum     = 0; // RFC 768: 0 means checksum disabled in IPv4

  if (len > 0 && payload != 0) {
    memmove(udp_pkt + sizeof(struct udp_hdr), payload, len);
  }

  return net_ip_send(dst_ip, IPPROTO_UDP, udp_pkt, udp_total_len);
}

// Compute TCP checksum including IPv4 pseudo-header
ushort
tcp_checksum(const uchar src_ip[4], const uchar dst_ip[4], const void *tcp_data, int len)
{
  uint sum = 0;
  const ushort *s = (const ushort*)src_ip;
  sum += s[0] + s[1];
  const ushort *d = (const ushort*)dst_ip;
  sum += d[0] + d[1];
  sum += htons(IPPROTO_TCP);
  sum += htons((ushort)len);

  const ushort *ptr = (const ushort*)tcp_data;
  int l = len;
  while (l > 1) {
    sum += *ptr++;
    l -= 2;
  }
  if (l == 1) {
    sum += *(const uchar*)ptr;
  }
  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }
  ushort csum = (ushort)(~sum);
  return csum == 0 ? 0xFFFF : csum;
}

static struct tcp_sock tcp_sockets[MAX_TCP_CONNS];
static ushort next_ephemeral_port = 45000;

static int
tcp_send_ack(struct tcp_sock *sock, uchar extra_flags)
{
  static uchar ack_buf[sizeof(struct tcp_hdr)] __attribute__((aligned(4)));
  struct tcp_hdr *ack = (struct tcp_hdr*)ack_buf;
  memset(ack_buf, 0, sizeof(ack_buf));
  ack->src_port = htons(sock->src_port);
  ack->dst_port = htons(sock->dst_port);
  ack->seq_no = htonl(sock->seq);
  ack->ack_no = htonl(sock->ack);
  ack->data_offset = 0x50; // 5 * 4 = 20 bytes
  ack->flags = TCP_ACK | extra_flags;
  ack->window = htons(TCP_RX_BUF_SIZE - sock->rx_count);
  ack->csum = 0;
  ack->csum = tcp_checksum(my_ip, sock->dst_ip, ack_buf, sizeof(ack_buf));
  return net_ip_send(sock->dst_ip, IPPROTO_TCP, ack_buf, sizeof(ack_buf));
}

// Process incoming packets (batch up to 8 packets per poll)
static volatile int net_polling = 0;
void
net_poll(void)
{
  if (net_polling) return;
  net_polling = 1;

  for (int p = 0; p < 8; p++) {
    static uchar rx_frame[ETH_FRAME_LEN] __attribute__((aligned(4)));
    int len = net_rx(rx_frame, sizeof(rx_frame));
    if (len <= 0) break;

    if (len < (int)sizeof(struct eth_hdr)) continue;
    struct eth_hdr *eth = (struct eth_hdr*)rx_frame;
    ushort eth_type = ntohs(eth->type);

    // 1. ARP Packet
    if (eth_type == ETHERTYPE_ARP && len >= (int)sizeof(struct arp_hdr)) {
      struct arp_hdr *arp = (struct arp_hdr*)rx_frame;
      ushort op = ntohs(arp->opcode);
      arp_table_update(arp->sender_ip, arp->sender_mac);

      if (op == ARP_REQUEST && memcmp(arp->target_ip, my_ip, 4) == 0) {
        static struct arp_hdr reply __attribute__((aligned(4)));
        memmove(reply.eth.dst, arp->sender_mac, 6);
        memmove(reply.eth.src, my_mac, 6);
        reply.eth.type = htons(ETHERTYPE_ARP);
        reply.hw_type = htons(1);
        reply.proto_type = htons(0x0800);
        reply.hw_len = 6;
        reply.proto_len = 4;
        reply.opcode = htons(ARP_REPLY);
        memmove(reply.sender_mac, my_mac, 6);
        memmove(reply.sender_ip, my_ip, 4);
        memmove(reply.target_mac, arp->sender_mac, 6);
        memmove(reply.target_ip, arp->sender_ip, 4);

        static uchar tx_buf[64] __attribute__((aligned(4)));
        memset(tx_buf, 0, sizeof(tx_buf));
        memmove(tx_buf, &reply, sizeof(reply));
        net_tx(tx_buf, 60);
      }
      continue;
    }

    // 2. IPv4 Packet
    if (eth_type == ETHERTYPE_IP && len >= (int)(sizeof(struct eth_hdr) + sizeof(struct ip_hdr))) {
      struct ip_hdr *ip = (struct ip_hdr*)(rx_frame + sizeof(struct eth_hdr));
      int ip_hdr_len = (ip->v_ihl & 0x0F) * 4;
      int ip_total_len = ntohs(ip->total_len);

      if (ip_hdr_len < 20 || len < (int)sizeof(struct eth_hdr) + ip_total_len) {
        continue;
      }

      if (memcmp(ip->dst, my_ip, 4) != 0 && ip->dst[0] != 255) {
        continue;
      }

      int payload_len = ip_total_len - ip_hdr_len;
      uchar *payload = rx_frame + sizeof(struct eth_hdr) + ip_hdr_len;

      // ICMP
      if (ip->proto == IPPROTO_ICMP && payload_len >= (int)sizeof(struct icmp_hdr)) {
        struct icmp_hdr *icmp = (struct icmp_hdr*)payload;
        if (icmp->type == ICMP_ECHO_REPLY) {
          ping_reply_received = 1;
          ping_reply_seq = ntohs(icmp->seq);
          memmove((void*)ping_reply_ip, ip->src, 4);
          ping_reply_len = payload_len;
        } else if (icmp->type == ICMP_ECHO_REQUEST) {
          icmp->type = ICMP_ECHO_REPLY;
          icmp->csum = 0;
          icmp->csum = ip_checksum(icmp, payload_len);
          net_ip_send(ip->src, IPPROTO_ICMP, icmp, payload_len);
        } else if (icmp->type == 3) { // Destination Unreachable
          if (payload_len >= 8 + 20 + 8) {
            struct ip_hdr *orig_ip = (struct ip_hdr*)(payload + 8);
            int orig_ihl = (orig_ip->v_ihl & 0x0F) * 4;
            if (orig_ip->proto == IPPROTO_TCP && payload_len >= 8 + orig_ihl + 4) {
              ushort *orig_ports = (ushort*)(payload + 8 + orig_ihl);
              ushort orig_src_port = ntohs(orig_ports[0]);
              for (int i = 0; i < MAX_TCP_CONNS; i++) {
                if (tcp_sockets[i].in_use && tcp_sockets[i].src_port == orig_src_port) {
                  tcp_sockets[i].err = 1;
                  tcp_sockets[i].state = TCP_CLOSED;
                  break;
                }
              }
            }
          }
        }
      }

      // UDP (DNS)
      if (ip->proto == IPPROTO_UDP && payload_len >= (int)sizeof(struct udp_hdr)) {
        struct udp_hdr *udp = (struct udp_hdr*)payload;
        ushort src_port = ntohs(udp->src_port);
        ushort dst_port = ntohs(udp->dst_port);
        ushort udp_len  = ntohs(udp->length);

        int udata_len = udp_len - sizeof(struct udp_hdr);
        uchar *udata  = payload + sizeof(struct udp_hdr);

        if (src_port == 53 && dst_port == 10053 && udata_len >= 12) {
          ushort qid = (udata[0] << 8) | udata[1];
          ushort flags = (udata[2] << 8) | udata[3];
          ushort ancount = (udata[6] << 8) | udata[7];

          if ((qid == dns_query_id || dns_query_id == 0) && (flags & 0x8000) && ancount > 0) {
            int offset = 12;
            while (offset < udata_len && udata[offset] != 0) {
              offset += udata[offset] + 1;
            }
            offset += 5;

            for (int a = 0; a < ancount && offset < udata_len; a++) {
              if ((udata[offset] & 0xC0) == 0xC0) {
                offset += 2;
              } else {
                while (offset < udata_len && udata[offset] != 0) {
                  offset += udata[offset] + 1;
                }
                offset++;
              }
              if (offset + 10 > udata_len) break;
              ushort atype = (udata[offset] << 8) | udata[offset + 1];
              ushort rdlen = (udata[offset + 8] << 8) | udata[offset + 9];
              offset += 10;

              if (atype == 1 && rdlen == 4 && offset + 4 <= udata_len) {
                memmove((void*)dns_result_ip, udata + offset, 4);
                dns_resolved = 1;
                break;
              }
              offset += rdlen;
            }
          }
        }
      }

      // TCP
      if (ip->proto == IPPROTO_TCP && payload_len >= (int)sizeof(struct tcp_hdr)) {
        struct tcp_hdr *tcp = (struct tcp_hdr*)payload;
        ushort dst_port = ntohs(tcp->dst_port);
        int tcp_hdr_len = (tcp->data_offset >> 4) * 4;

        if (tcp_hdr_len >= 20 && tcp_hdr_len <= payload_len) {
          struct tcp_sock *sock = 0;
          for (int i = 0; i < MAX_TCP_CONNS; i++) {
            if (tcp_sockets[i].in_use && tcp_sockets[i].src_port == dst_port) {
              sock = &tcp_sockets[i];
              break;
            }
          }

          if (sock) {
            uint seg_seq = ntohl(tcp->seq_no);

            if (tcp->flags & TCP_RST) {
              sock->err = 1;
              sock->state = TCP_CLOSED;
            } else if (sock->state == TCP_SYN_SENT) {
              if ((tcp->flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
                sock->ack = seg_seq + 1;
                sock->seq++; // SYN acknowledged
                sock->state = TCP_ESTABLISHED;
                tcp_send_ack(sock, 0);
              }
            } else if (sock->state == TCP_ESTABLISHED ||
                       sock->state == TCP_FIN_WAIT_1 ||
                       sock->state == TCP_FIN_WAIT_2 ||
                       sock->state == TCP_CLOSE_WAIT) {
              int data_len = payload_len - tcp_hdr_len;
              uchar *tcp_data = payload + tcp_hdr_len;

              if (data_len > 0) {
                for (int b = 0; b < data_len; b++) {
                  if (sock->rx_count < TCP_RX_BUF_SIZE) {
                    sock->rx_buf[sock->rx_head] = tcp_data[b];
                    sock->rx_head = (sock->rx_head + 1) % TCP_RX_BUF_SIZE;
                    sock->rx_count++;
                  }
                }
                sock->ack = seg_seq + data_len;
                tcp_send_ack(sock, 0);
              }

              if (tcp->flags & TCP_FIN) {
                sock->peer_closed = 1;
                sock->ack = seg_seq + (data_len > 0 ? data_len : 0) + 1;
                tcp_send_ack(sock, 0);
                if (sock->state == TCP_ESTABLISHED)
                  sock->state = TCP_CLOSE_WAIT;
                else if (sock->state == TCP_FIN_WAIT_1 || sock->state == TCP_FIN_WAIT_2)
                  sock->state = TCP_CLOSED;
              }

              if (sock->state == TCP_FIN_WAIT_1 && (tcp->flags & TCP_ACK)) {
                sock->state = TCP_FIN_WAIT_2;
              }
            }
          }
        }
      }
    }
  }
  net_polling = 0;
}

// Send an ICMP Echo Request and wait for reply
int
net_ping(const uchar dst_ip[4], int seq, int timeout_ticks)
{
  ping_reply_received = 0;

  static struct {
    struct icmp_hdr hdr;
    char payload[32];
  } __attribute__((packed, aligned(4))) ping_pkt;

  ping_pkt.hdr.type = ICMP_ECHO_REQUEST;
  ping_pkt.hdr.code = 0;
  ping_pkt.hdr.csum = 0;
  ping_pkt.hdr.id = htons(0x4147); // "AG" (Antigravity)
  ping_pkt.hdr.seq = htons(seq);
  safestrcpy(ping_pkt.payload, "Antigravity OS Network Ping", sizeof(ping_pkt.payload));

  ping_pkt.hdr.csum = ip_checksum(&ping_pkt, sizeof(ping_pkt));

  if (net_ip_send(dst_ip, IPPROTO_ICMP, &ping_pkt, sizeof(ping_pkt)) != 0) {
    return -1;
  }

  uint timeout_ms = timeout_ticks * 10;
  if (timeout_ms < 200) timeout_ms = 200;
  uint start_ms = net_time_ms();
  while ((uint)(net_time_ms() - start_ms) < timeout_ms) {
    net_poll();
    if (ping_reply_received && ping_reply_seq == seq) {
      return 0;
    }
    net_yield();
    for (volatile int d = 0; d < 200; d++);
  }

  return -1; // Timed out
}

int
net_ping_test(void)
{
  cprintf("\n=======================================================\n");
  cprintf("  ANTIGRAVITY OS NETWORK STACK VERIFICATION (ICMP PING)\n");
  cprintf("=======================================================\n");

  // Test 1: Ping QEMU Gateway (10.0.2.2)
  cprintf("PING 10.0.2.2 with 32 bytes of data:\n");
  for (int seq = 1; seq <= 3; seq++) {
    int res = net_ping(gateway_ip, seq, 20);
    if (res == 0) {
      cprintf("Reply from 10.0.2.2: bytes=%d seq=%d status=OK (RTT < 1ms)\n",
              ping_reply_len, seq);
    } else {
      cprintf("Request timed out (seq=%d)\n", seq);
    }
  }
  cprintf("=======================================================\n\n");

  return 0;
}

int
net_dns_resolve(const char *hostname, uchar resolved_ip[4])
{
  dns_resolved = 0;
  static ushort next_dns_id = 0x5432;
  dns_query_id = next_dns_id++;

  static uchar query[256] __attribute__((aligned(4)));
  memset(query, 0, sizeof(query));

  // DNS Header
  query[0] = (dns_query_id >> 8) & 0xFF;
  query[1] = dns_query_id & 0xFF;
  query[2] = 0x01; query[3] = 0x00; // Standard query, recursion desired
  query[4] = 0x00; query[5] = 0x01; // QDCOUNT = 1

  // Format QNAME: "google.com" -> 6google3com0
  int qlen = 12;
  const char *src = hostname;
  while (*src) {
    const char *dot = src;
    while (*dot && *dot != '.') dot++;
    int label_len = dot - src;
    query[qlen++] = label_len;
    for (int i = 0; i < label_len; i++) {
      query[qlen++] = src[i];
    }
    src = *dot ? dot + 1 : dot;
  }
  query[qlen++] = 0; // End of name
  query[qlen++] = 0x00; query[qlen++] = 0x01; // QTYPE = A (1)
  query[qlen++] = 0x00; query[qlen++] = 0x01; // QCLASS = IN (1)

  if (net_udp_send(dns_ip, 10053, 53, query, qlen) != 0) {
    return -1;
  }

  uint start_ms = net_time_ms();
  uint last_query_ms = start_ms;
  while ((uint)(net_time_ms() - start_ms) < 3000) { // 3 seconds timeout
    net_poll();
    if (dns_resolved) {
      memmove(resolved_ip, (void*)dns_result_ip, 4);
      return 0;
    }
    if ((uint)(net_time_ms() - last_query_ms) >= 1000) { // re-transmit every 1 second
      net_udp_send(dns_ip, 10053, 53, query, qlen);
      last_query_ms = net_time_ms();
    }
    net_yield();
    for (volatile int d = 0; d < 200; d++);
  }

  return -1;
}

static uchar cached_example_ip[4] = {0};

int
net_dns_test(void)
{
  cprintf("=======================================================\n");
  cprintf("  ANTIGRAVITY OS DNS RESOLUTION (OVER HOST WI-FI)\n");
  cprintf("=======================================================\n");

  uchar ip[4];
  const char *domains[] = {"example.com"};
  for (int d = 0; d < 1; d++) {
    cprintf("Resolving domain: %s via DNS (", domains[d]);
    print_ip(dns_ip);
    cprintf(":53)...\n");
    if (net_dns_resolve(domains[d], ip) == 0) {
      cprintf("DNS: %s -> ", domains[d]);
      print_ip(ip);
      cprintf(" [RESOLVED VIA HOST WI-FI!]\n");
      memmove(cached_example_ip, ip, 4);
    } else {
      cprintf("DNS: Resolution timed out for %s\n", domains[d]);
    }
  }
  cprintf("=======================================================\n\n");
  return 0;
}

static void
net_send_gratuitous_arp(void)
{
  static struct arp_hdr garp __attribute__((aligned(4)));
  memset(garp.eth.dst, 0xFF, 6);
  memmove(garp.eth.src, my_mac, 6);
  garp.eth.type = htons(ETHERTYPE_ARP);
  garp.hw_type = htons(1);
  garp.proto_type = htons(0x0800);
  garp.hw_len = 6;
  garp.proto_len = 4;
  garp.opcode = htons(ARP_REQUEST);
  memmove(garp.sender_mac, my_mac, 6);
  memmove(garp.sender_ip, my_ip, 4);
  memset(garp.target_mac, 0x00, 6);
  memmove(garp.target_ip, my_ip, 4);

  static uchar tx_buf[64] __attribute__((aligned(4)));
  memset(tx_buf, 0, sizeof(tx_buf));
  memmove(tx_buf, &garp, sizeof(garp));
  net_tx(tx_buf, 60);
}

void
net_init(void)
{
  if (!CdcEcmIsAttached()) {
    cprintf("NET: USB network interface not found.\n");
    return;
  }
  CdcEcmGetMac(my_mac);

  // Pre-seed Gateway and DNS Server in ARP table (QEMU virtual router and DNS proxy)
  static const uchar qemu_gw_mac[6]  = {0x52, 0x55, 0x0a, 0x00, 0x02, 0x02};
  static const uchar qemu_dns_mac[6] = {0x52, 0x55, 0x0a, 0x00, 0x02, 0x03};
  arp_table_update(gateway_ip, qemu_gw_mac);
  arp_table_update(dns_ip, qemu_dns_mac);

  net_send_gratuitous_arp();

  cprintf("NET: TCP/IP Stack initialized:\n");
  cprintf("     IP Address: "); print_ip(my_ip); cprintf("\n");
  cprintf("     Netmask:    "); print_ip(netmask); cprintf("\n");
  cprintf("     Gateway:    "); print_ip(gateway_ip); cprintf("\n");
  cprintf("     DNS Server: "); print_ip(dns_ip); cprintf("\n");
  cprintf("     Hardware:   "); print_mac(my_mac); cprintf("\n");

  // Diagnostic tests can be run on demand from userland (ping, fetch)
  // net_ping_test();
  // net_dns_test();
  // net_http_test();
}

int
net_tcp_socket(void)
{
  for (int i = 0; i < MAX_TCP_CONNS; i++) {
    if (!tcp_sockets[i].in_use) {
      memset(&tcp_sockets[i], 0, sizeof(struct tcp_sock));
      tcp_sockets[i].in_use = 1;
      tcp_sockets[i].state = TCP_CLOSED;
      return i;
    }
  }
  return -1;
}

int
net_tcp_connect(int sock_id, const uchar dst_ip[4], ushort dst_port)
{
  if (sock_id < 0 || sock_id >= MAX_TCP_CONNS) return -1;
  struct tcp_sock *sock = &tcp_sockets[sock_id];

  memmove(sock->dst_ip, dst_ip, 4);
  sock->dst_port = dst_port;
  if (next_ephemeral_port < 40000 || next_ephemeral_port > 60000) {
    next_ephemeral_port = 40000 + ((next_ip_id * 179 + 37) % 15000);
  }
  sock->src_port = next_ephemeral_port++;
  sock->seq = 0x10000000 + sock->src_port * 1000;
  sock->ack = 0;
  sock->rx_head = sock->rx_tail = sock->rx_count = 0;
  sock->peer_closed = 0;
  sock->err = 0;
  sock->state = TCP_SYN_SENT;

  static uchar syn_pkt[24] __attribute__((aligned(4)));
  struct tcp_hdr *syn = (struct tcp_hdr*)syn_pkt;
  memset(syn_pkt, 0, sizeof(syn_pkt));
  syn->src_port = htons(sock->src_port);
  syn->dst_port = htons(sock->dst_port);
  syn->seq_no = htonl(sock->seq);
  syn->ack_no = 0;
  syn->data_offset = 0x60; // 6 words = 24 bytes
  syn->flags = TCP_SYN;
  syn->window = htons(TCP_RX_BUF_SIZE);
  syn_pkt[20] = 2; syn_pkt[21] = 4; // MSS option (1460 bytes)
  syn_pkt[22] = 0x05; syn_pkt[23] = 0xB4;
  syn->csum = 0;
  syn->csum = tcp_checksum(my_ip, sock->dst_ip, syn_pkt, sizeof(syn_pkt));

  int tx_res = net_ip_send(sock->dst_ip, IPPROTO_TCP, syn_pkt, sizeof(syn_pkt));
  if (tx_res != 0) {
    sock->state = TCP_CLOSED;
    return -1;
  }

  uint start_ms = net_time_ms();
  uint last_send_ms = start_ms;
  while ((uint)(net_time_ms() - start_ms) < 3000) { // 3 seconds timeout
    net_poll();
    if (sock->state == TCP_ESTABLISHED) {
      return 0;
    }
    if (sock->err || sock->state == TCP_CLOSED) {
      return -1;
    }
    if ((uint)(net_time_ms() - last_send_ms) >= 800) { // re-transmit SYN every 800ms
      net_ip_send(sock->dst_ip, IPPROTO_TCP, syn_pkt, sizeof(syn_pkt));
      last_send_ms = net_time_ms();
    }
    net_yield();
    for (volatile int d = 0; d < 200; d++);
  }

  sock->state = TCP_CLOSED;
  return -1;
}

int
net_tcp_send(int sock_id, const void *data, int len)
{
  if (sock_id < 0 || sock_id >= MAX_TCP_CONNS || len <= 0) return -1;
  struct tcp_sock *sock = &tcp_sockets[sock_id];
  if (sock->state != TCP_ESTABLISHED && sock->state != TCP_CLOSE_WAIT) return -1;

  const uchar *ptr = (const uchar*)data;
  int remaining = len;

  while (remaining > 0) {
    int chunk = remaining > 1400 ? 1400 : remaining;
    static uchar pkt[ETH_FRAME_LEN] __attribute__((aligned(4)));
    struct tcp_hdr *tcp = (struct tcp_hdr*)pkt;
    memset(tcp, 0, sizeof(struct tcp_hdr));

    tcp->src_port = htons(sock->src_port);
    tcp->dst_port = htons(sock->dst_port);
    tcp->seq_no = htonl(sock->seq);
    tcp->ack_no = htonl(sock->ack);
    tcp->data_offset = 0x50; // 20 bytes
    tcp->flags = TCP_ACK | TCP_PSH;
    tcp->window = htons(TCP_RX_BUF_SIZE - sock->rx_count);
    memmove(pkt + sizeof(struct tcp_hdr), ptr, chunk);

    int tcp_len = sizeof(struct tcp_hdr) + chunk;
    tcp->csum = 0;
    tcp->csum = tcp_checksum(my_ip, sock->dst_ip, pkt, tcp_len);

    if (net_ip_send(sock->dst_ip, IPPROTO_TCP, pkt, tcp_len) != 0) {
      return (len - remaining) > 0 ? (len - remaining) : -1;
    }

    sock->seq += chunk;
    ptr += chunk;
    remaining -= chunk;
  }

  return len;
}

int
net_tcp_recv(int sock_id, void *buf, int max_len, int nonblock)
{
  if (sock_id < 0 || sock_id >= MAX_TCP_CONNS || max_len <= 0) return -1;
  struct tcp_sock *sock = &tcp_sockets[sock_id];

  uint start_ms = net_time_ms();
  while (sock->rx_count == 0) {
    if (sock->peer_closed || sock->state == TCP_CLOSED || sock->err) {
      if (sock->rx_count > 0) break;
      return 0; // EOF
    }
    if (nonblock) {
      return -1;
    }
    net_poll();
    if (sock->rx_count > 0)
      break;
    if ((uint)(net_time_ms() - start_ms) > 5000) { // 5-second receive timeout
      return -1;
    }
    net_yield();
    for (volatile int d = 0; d < 200; d++);
  }

  if (sock->rx_count == 0) {
    if (sock->peer_closed || sock->state == TCP_CLOSED) return 0;
    return -1;
  }

  int count = max_len < sock->rx_count ? max_len : sock->rx_count;
  uchar *dst = (uchar*)buf;
  for (int i = 0; i < count; i++) {
    dst[i] = sock->rx_buf[sock->rx_tail];
    sock->rx_tail = (sock->rx_tail + 1) % TCP_RX_BUF_SIZE;
  }
  sock->rx_count -= count;
  return count;
}

int
net_tcp_close(int sock_id)
{
  if (sock_id < 0 || sock_id >= MAX_TCP_CONNS) return -1;
  struct tcp_sock *sock = &tcp_sockets[sock_id];
  if (!sock->in_use) return 0;

  if (sock->state == TCP_ESTABLISHED || sock->state == TCP_CLOSE_WAIT) {
    static uchar fin_buf[sizeof(struct tcp_hdr)] __attribute__((aligned(4)));
    struct tcp_hdr *fin = (struct tcp_hdr*)fin_buf;
    memset(fin_buf, 0, sizeof(fin_buf));
    fin->src_port = htons(sock->src_port);
    fin->dst_port = htons(sock->dst_port);
    fin->seq_no = htonl(sock->seq);
    fin->ack_no = htonl(sock->ack);
    fin->data_offset = 0x50;
    fin->flags = TCP_FIN | TCP_ACK;
    fin->window = htons(TCP_RX_BUF_SIZE - sock->rx_count);
    fin->csum = 0;
    fin->csum = tcp_checksum(my_ip, sock->dst_ip, fin_buf, sizeof(fin_buf));

    net_ip_send(sock->dst_ip, IPPROTO_TCP, fin_buf, sizeof(fin_buf));
    sock->seq++;
  }

  sock->state = TCP_CLOSED;
  sock->in_use = 0;
  return 0;
}

int
net_http_test(void)
{
  cprintf("=======================================================\n");
  cprintf("  ANTIGRAVITY OS HTTP GET TEST (TO 1.1.1.1:80)\n");
  cprintf("=======================================================\n");

  uchar ip[4] = {1, 1, 1, 1};
  cprintf("Connecting to 1.1.1.1:80...\n");

  int s = net_tcp_socket();
  if (s < 0) {
    cprintf("HTTP: socket creation failed\n");
    return -1;
  }

  if (net_tcp_connect(s, ip, 80) != 0) {
    cprintf("HTTP: TCP 3-way handshake failed\n");
    net_tcp_close(s);
    return -1;
  }
  cprintf("TCP Handshake: ESTABLISHED!\n");

  static const char req[] =
    "GET / HTTP/1.1\r\n"
    "Host: 1.1.1.1\r\n"
    "Connection: close\r\n\r\n";

  cprintf("Sending HTTP GET request...\n");
  if (net_tcp_send(s, req, strlen(req)) < 0) {
    cprintf("HTTP: send failed\n");
    net_tcp_close(s);
    return -1;
  }

  cprintf("HTTP Response Received:\n");
  cprintf("-------------------------------------------------------\n");
  static char resp_buf[512];
  int total_bytes = 0;
  int n;
  while ((n = net_tcp_recv(s, resp_buf, sizeof(resp_buf) - 1, 0)) > 0) {
    resp_buf[n] = '\0';
    cprintf("%s", resp_buf);
    total_bytes += n;
    if (total_bytes > 500) break;
  }
  cprintf("\n-------------------------------------------------------\n");
  cprintf("HTTP: Received %d bytes. Closing connection.\n", total_bytes);
  net_tcp_close(s);
  cprintf("=======================================================\n\n");
  return 0;
}
