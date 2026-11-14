#ifndef _NET_H_
#define _NET_H_

#include "types.h"

#define ETH_ALEN 6
#define ETH_HLEN 14
#define ETH_FRAME_LEN 1514

#define ETHERTYPE_IP  0x0800
#define ETHERTYPE_ARP 0x0806

#define IPPROTO_ICMP 1
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17

#define ARP_REQUEST 1
#define ARP_REPLY   2

#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8

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

struct ip_hdr {
  uchar  v_ihl;       // Version (4) + Header length in 32-bit words (5) -> 0x45
  uchar  tos;         // Type of service
  ushort total_len;   // Total length of IP packet in bytes
  ushort id;          // Identification
  ushort flags_frag;  // Flags (3 bits) + Fragment offset (13 bits)
  uchar  ttl;         // Time to live
  uchar  proto;       // Protocol (1=ICMP, 6=TCP, 17=UDP)
  ushort csum;        // Header checksum
  uchar  src[4];      // Source IPv4 address
  uchar  dst[4];      // Destination IPv4 address
} __attribute__((packed));

struct icmp_hdr {
  uchar  type;        // Message type (8=Echo Request, 0=Echo Reply)
  uchar  code;        // Sub-code
  ushort csum;        // Checksum
  ushort id;          // Identifier
  ushort seq;         // Sequence number
} __attribute__((packed));

struct udp_hdr {
  ushort src_port;
  ushort dst_port;
  ushort length;
  ushort csum;
} __attribute__((packed));

struct tcp_hdr {
  ushort src_port;
  ushort dst_port;
  uint   seq_no;
  uint   ack_no;
  uchar  data_offset; // Data offset (4 bits) + Reserved (4 bits)
  uchar  flags;       // FIN, SYN, RST, PSH, ACK, URG
  ushort window;
  ushort csum;
  ushort urgent_ptr;
} __attribute__((packed));

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10
#define TCP_URG 0x20

#define MAX_TCP_CONNS 8
#define TCP_RX_BUF_SIZE 4096

enum tcp_state {
  TCP_CLOSED,
  TCP_LISTEN,
  TCP_SYN_SENT,
  TCP_SYN_RCVD,
  TCP_ESTABLISHED,
  TCP_FIN_WAIT_1,
  TCP_FIN_WAIT_2,
  TCP_CLOSE_WAIT,
  TCP_CLOSING,
  TCP_LAST_ACK,
  TCP_TIME_WAIT
};

struct tcp_sock {
  int in_use;
  enum tcp_state state;
  uchar dst_ip[4];
  ushort src_port;
  ushort dst_port;
  uint seq;         // Next sequence number to send
  uint ack;         // Next sequence number expected from peer
  
  // Receive ring buffer
  uchar rx_buf[TCP_RX_BUF_SIZE];
  int rx_head;      // Write index
  int rx_tail;      // Read index
  int rx_count;     // Bytes queued
  
  int peer_closed;  // Received FIN
  int err;          // RST or error
};

// Function prototypes
void net_init(void);
int  net_is_up(void);
void net_get_mac(uchar mac[ETH_ALEN]);
int  net_tx(const void *buf, int len);
int  net_rx(void *buf, int max_len);
void net_poll(void);

// Higher level networking
ushort ip_checksum(const void *data, int len);
ushort tcp_checksum(const uchar src_ip[4], const uchar dst_ip[4], const void *tcp_data, int len);
int  net_arp_resolve(const uchar ip[4], uchar mac[6]);
int  net_ip_send(const uchar dst_ip[4], uchar proto, const void *payload, int len);
int  net_udp_send(const uchar dst_ip[4], ushort src_port, ushort dst_port, const void *payload, int len);
int  net_dhcp_acquire(void);
int  net_dns_resolve(const char *hostname, uchar resolved_ip[4]);
int  net_ping(const uchar dst_ip[4], int seq, int timeout_ticks);
int  net_ping_test(void);
int  net_dns_test(void);

// TCP Socket API
int  net_tcp_socket(void);
int  net_tcp_connect(int sock_id, const uchar dst_ip[4], ushort dst_port);
int  net_tcp_send(int sock_id, const void *data, int len);
int  net_tcp_recv(int sock_id, void *buf, int max_len, int nonblock);
int  net_tcp_close(int sock_id);
int  net_http_test(void);

#endif
