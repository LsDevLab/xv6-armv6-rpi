/*****************************************************************
*       uart_keyboard.c
*
********************************************************************/



#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "fs.h"        // struct inode
#include "file.h"      // devsw[]
#include "proc.h"      // sleep(), wakeup(), killed

#define INPUT_BUF 128
#define C(x)  ((x)-'@')   // Control-x

static struct {
  struct spinlock lock;
  char buf[INPUT_BUF];
  uint r;   // Read index
  uint w;   // Write index
  uint e;   // Edit index
} input;


/*
 * Called from UART interrupt handler
 */
void
uartkbdintr(int (*getc)(void))
{
  int c;

  acquire(&input.lock);
  while((c = getc()) >= 0){
    switch(c){
    case C('P'):    // process dump
      procdump();
      break;

    case C('U'):    // kill line
      while(input.e != input.w &&
            input.buf[(input.e - 1) % INPUT_BUF] != '\n'){
        input.e--;
            }
      break;

    case C('H'):
    case '\x7f':    // backspace
      if(input.e != input.w){
        input.e--;
      }
      break;

    default:
      if(c != 0 && input.e - input.r < INPUT_BUF){
        c = (c == '\r') ? '\n' : c;
        input.buf[input.e++ % INPUT_BUF] = c;

        if(c == '\n' || c == C('D') ||
           input.e == input.r + INPUT_BUF){
          input.w = input.e;
          wakeup(&input.r);
           }
      }
      break;
    }
  }
  release(&input.lock);
}

/*
 * Read interface for /dev/uart_keyboard
 */
int
uartkbdread(struct inode *ip, char *dst, int n)
{
  uint target;
  int c;

  iunlock(ip);
  target = n;

  acquire(&input.lock);
  while(n > 0){
    while(input.r == input.w){
      if(curr_proc->killed){
        release(&input.lock);
        ilock(ip);
        return -1;
      }
      sleep(&input.r, &input.lock);
    }

    c = input.buf[input.r++ % INPUT_BUF];

    if(c == C('D')){  // EOF
      if(n < target)
        input.r--;
      break;
    }

    *dst++ = c;
    n--;

    if(c == '\n')
      break;
  }
  release(&input.lock);
  ilock(ip);

  return target - n;
}

/*
 * Initialization
 */
void
uartkbdinit(void)
{
  initlock(&input.lock, "uartkbd");
  input.r = input.w = input.e = 0;

  devsw[UART_KEYBOARD].read  = uartkbdread;
  devsw[UART_KEYBOARD].write = 0;   // read-only device
}