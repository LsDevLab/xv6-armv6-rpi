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
    printf(1, "usage: fetch <host> [port] [path]\n");
    printf(1, "example: fetch 1.1.1.1 /\n");
    printf(1, "         fetch 10.0.2.2 8000 /\n");
    exit();
  }

  char *host = argv[1];
  int port = 80;
  char *path = "/";

  if (argc >= 3) {
    int is_num = 1;
    for (int i = 0; argv[2][i]; i++) {
      if (argv[2][i] < '0' || argv[2][i] > '9') {
        is_num = 0;
        break;
      }
    }
    if (is_num) {
      port = atoi(argv[2]);
      if (argc >= 4) path = argv[3];
    } else {
      path = argv[2];
    }
  }

  uchar ip[4];
  if (parse_ip(host, ip) != 0) {
    printf(1, "fetch: resolving %s via DNS...\n", host);
    if (dns(host, ip) != 0) {
      printf(1, "fetch: cannot resolve hostname %s\n", host);
      exit();
    }
  }

  printf(1, "fetch: connecting to %s (%d.%d.%d.%d:%d)...\n",
         host, ip[0], ip[1], ip[2], ip[3], port);

  int fd = socket();
  if (fd < 0) {
    printf(1, "fetch: socket() failed\n");
    exit();
  }

  if (connect(fd, ip, port) < 0) {
    printf(1, "fetch: connect() failed\n");
    close(fd);
    exit();
  }

  printf(1, "fetch: TCP connection established! Sending HTTP GET %s...\n", path);

  char req[512];
  char *p = req;
  strcpy(p, "GET "); p += strlen(p);
  strcpy(p, path); p += strlen(p);
  strcpy(p, " HTTP/1.1\r\nHost: "); p += strlen(p);
  strcpy(p, host); p += strlen(p);
  strcpy(p, "\r\nUser-Agent: xv6-antigravity/1.0\r\nConnection: close\r\n\r\n");

  if (write(fd, req, strlen(req)) < 0) {
    printf(1, "fetch: write() failed\n");
    close(fd);
    exit();
  }

  printf(1, "fetch: HTTP Response:\n-------------------------------------------------------\n");
  char buf[256];
  int n;
  int total = 0;
  while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
    write(1, buf, n);
    total += n;
  }

  printf(1, "\n-------------------------------------------------------\n");
  printf(1, "fetch: completed (%d bytes received)\n", total);
  close(fd);
  exit();
}
