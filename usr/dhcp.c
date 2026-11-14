#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  printf(1, "dhcp: requesting IP lease from DHCP server...\n");
  int res = dhcp();
  if (res == 0) {
    printf(1, "dhcp: IP lease acquired successfully.\n");
  } else {
    printf(1, "dhcp: DHCP negotiation failed or timed out (using static fallback).\n");
  }
  exit();
}
