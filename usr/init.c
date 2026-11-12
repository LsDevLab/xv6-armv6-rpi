// init: The initial user-level program
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "file.h"

char *argv[] = { "wm", 0 };

int
main(void)
{
  int pid, wpid;

  mkdir("dev");
  if(open("/dev/uart_keyboard", O_RDWR) < 0){
    mknod("/dev/uart_keyboard", UART_KEYBOARD, 0);
    open("/dev/uart_keyboard", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  // Ensure /dev/fb and /dev/mouse exist
  mknod("/dev/fb", FRAMEBUFFER, 0);
  mknod("/dev/mouse", MOUSE, 0);

  for(;;){
    printf(1, "init: starting wm\n");
    pid = fork();
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){
      exec("wm", argv);
      printf(1, "init: exec wm failed\n");
      exit();
    }
    while((wpid=wait()) >= 0 && wpid != pid)
      printf(1, "zombie!\n");
  }
}
