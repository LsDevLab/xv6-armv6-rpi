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


  if(open("/dev/uart_keyboard", O_RDONLY) < 0){
    mknod("/dev/uart_keyboard", UART_KEYBOARD, 0);
    open("/dev/uart_keyboard", O_RDONLY);
  }
  int fb_fd = -1;
  if(open("/dev/fb", O_RDWR) < 0){
    mknod("/dev/fb", FRAMEBUFFER, 0);
    open("/dev/fb", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr


  fb_pixel_t p;
  p.x = 10;
  p.y = 10;
  p.color = 0xF800;  // Red
  p.ch = 'A';
  p.w = p.h = 0;
  p.buffer = 0;
  write(fb_fd, &p, sizeof(p));

  for(;;){

    fb_pixel_t p;
    p.x = 10;
    p.y = 10;
    p.color = 0xF800;  // Red
    p.ch = 'B';
    p.w = p.h = 0;
    p.buffer = 0;
    write(fb_fd, &p, sizeof(p));
    printf(1, "init: starting sh\n");
    pid = fork();
    if(pid < 0){

      fb_pixel_t p;
      p.x = 10;
      p.y = 10;
      p.color = 0xF800;  // Red
      p.ch = 'C';
      p.w = p.h = 0;
      p.buffer = 0;
      write(fb_fd, &p, sizeof(p));
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){
      exec("wm", argv);

      fb_pixel_t p;
      p.x = 10;
      p.y = 10;
      p.color = 0xF800;  // Red
      p.ch = 'D';
      p.w = p.h = 0;
      p.buffer = 0;
      write(fb_fd, &p, sizeof(p));
      printf(1, "init: exec wm failed\n");
      exit();
    }
    while((wpid=wait()) >= 0 && wpid != pid)
      printf(1, "zombie!\n");
  }
}
