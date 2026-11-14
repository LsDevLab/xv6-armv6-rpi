#include "types.h"
#include "stat.h"
#include "user.h"

static int
parse_ip(const char *s, uchar ip[4])
{
  int part = 0;
  int val = 0;
  int has_digits = 0;

  for (int i = 0; s[i]; i++) {
    if (s[i] >= '0' && s[i] <= '9') {
      val = val * 10 + (s[i] - '0');
      has_digits = 1;
      if (val > 255) return -1;
    } else if (s[i] == '.') {
      if (!has_digits || part >= 3) return -1;
      ip[part++] = val;
      val = 0;
      has_digits = 0;
    } else {
      return -1;
    }
  }
  if (!has_digits || part != 3) return -1;
  ip[part] = val;
  return 0;
}

int
main(int argc, char *argv[])
{
  if (argc < 2) {
    printf(1, "usage: ping <ip_address or hostname>\n");
    exit();
  }

  uchar ip[4];
  if (parse_ip(argv[1], ip) != 0) {
    printf(1, "Resolving %s via DNS...\n", argv[1]);
    if (dns(argv[1], ip) != 0) {
      printf(1, "ping: unknown host %s\n", argv[1]);
      exit();
    }
  }

  printf(1, "PING %s (%d.%d.%d.%d): 32 data bytes\n",
         argv[1], ip[0], ip[1], ip[2], ip[3]);

  int sent = 0;
  int received = 0;

  for (int seq = 1; seq <= 4; seq++) {
    sent++;
    int res = ping(ip, seq);
    if (res == 0) {
      received++;
      printf(1, "Reply from %d.%d.%d.%d: seq=%d status=OK (rtt < 1ms)\n",
             ip[0], ip[1], ip[2], ip[3], seq);
    } else {
      printf(1, "Request timed out (seq=%d)\n", seq);
    }
    sleep(50);
  }

  printf(1, "--- %s ping statistics ---\n", argv[1]);
  printf(1, "%d packets transmitted, %d received, %d%% packet loss\n",
         sent, received, ((sent - received) * 100) / sent);

  exit();
}
