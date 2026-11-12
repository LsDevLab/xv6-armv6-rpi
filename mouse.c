#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

#define MOUSE_BUF 64

extern void UsbCheckForChange(void);
extern uint MouseCount(void);
extern uint MouseGetAddress(uint index);
extern uint MousePoll(uint mouseAddress);
extern short MouseGetPositionX(uint mouseAddress);
extern short MouseGetPositionY(uint mouseAddress);
extern u8 MouseGetButtons(uint mouseAddress);

/* PL011 UART0 registers (Hardware Serial 0 in BCM2835 / QEMU) */
#define UART0_BASE 0xFE201000
#define UART0_DR   (*(volatile uint*)(UART0_BASE + 0x00))
#define UART0_FR   (*(volatile uint*)(UART0_BASE + 0x18))
#define UART0_LCRH (*(volatile uint*)(UART0_BASE + 0x2C))
#define UART0_CR   (*(volatile uint*)(UART0_BASE + 0x30))

static struct {
  struct spinlock lock;
  mouse_event_t buf[MOUSE_BUF];
  uint r;
  uint w;
} mouse_queue;

static int ms_state = 0;
static u8 ms_bytes[4];

void
mouse_post_event(int dx, int dy, int buttons)
{
  acquire(&mouse_queue.lock);
  if(mouse_queue.w - mouse_queue.r < MOUSE_BUF){
    mouse_event_t *ev = &mouse_queue.buf[mouse_queue.w % MOUSE_BUF];
    ev->dx = dx;
    ev->dy = dy;
    ev->buttons = buttons;
    mouse_queue.w++;
    wakeup(&mouse_queue.r);
  }
  release(&mouse_queue.lock);
}

extern uint KeyboardCount(void);

void LogPrint(char *message, uint length)
{
  for(uint i = 0; i < length; i++){
    uartputc((uint)message[i]);
  }
}

void
pl011_mouse_init(void)
{
  UART0_CR = 0;                              /* Disable UART0 */
  UART0_LCRH = (1 << 4) | (3 << 5);          /* FIFO enable (FEN), 8-bit word */
  UART0_CR = (1 << 0) | (1 << 8) | (1 << 9); /* UARTEN | TXE | RXE */
  ms_state = 0;
}

void
pl011_mouse_poll(void)
{
  /* While RX FIFO is not empty (bit 4 of UART0_FR is RXFE) */
  while(!(UART0_FR & 0x10)){
    u8 b = (u8)(UART0_DR & 0xFF);

    /* Microsoft Serial Mouse protocol:
     * Byte 0 always has bit 6 set (0x40). If bit 6 is set, it synchronizes packet start! */
    if(b & 0x40){
      ms_bytes[0] = b;
      ms_state = 1;
      continue;
    }

    if(ms_state == 1){
      ms_bytes[1] = b;
      ms_state = 2;
      continue;
    }

    if(ms_state == 2){
      ms_bytes[2] = b;
      ms_state = 0;

      /* Microsoft Mouse 3-byte Packet:
       * Byte 0: 0 1 L R Y7 Y6 X7 X6
       * Byte 1: 0 0 X5 X4 X3 X2 X1 X0
       * Byte 2: 0 0 Y5 Y4 Y3 Y2 Y1 Y0 */
      int dx = (signed char)(((ms_bytes[0] & 0x03) << 6) | (ms_bytes[1] & 0x3F));
      int dy = (signed char)(((ms_bytes[0] & 0x0C) << 4) | (ms_bytes[2] & 0x3F));
      int buttons = 0;
      if(ms_bytes[0] & 0x20) buttons |= 1; /* Left button */
      if(ms_bytes[0] & 0x10) buttons |= 2; /* Right button */

      mouse_post_event(dx, dy, buttons);
    }
  }
}

void
usb_mouse_poll(void)
{
  static int chg_tick = 0;
  static u8 last_btn = 0;
  if(++chg_tick >= 100){
    chg_tick = 0;
    UsbCheckForChange();
  }

  uint mc = MouseCount();
  if(mc > 0){
    uint addr = MouseGetAddress(0);
    if(addr != 0){
      if(MousePoll(addr) == 0){ /* OK */
        short raw_dx = MouseGetPositionX(addr);
        short raw_dy = MouseGetPositionY(addr);
        u8 btn = MouseGetButtons(addr);
        if(raw_dx != 0 || raw_dy != 0 || btn != last_btn){
          last_btn = btn;
          /* Smooth pointer ballistics: 2x base speed, 3x for faster movement */
          int abs_x = raw_dx < 0 ? -raw_dx : raw_dx;
          int abs_y = raw_dy < 0 ? -raw_dy : raw_dy;
          int mul_x = (abs_x > 4) ? 3 : 2;
          int mul_y = (abs_y > 4) ? 3 : 2;
          int dx = (int)raw_dx * mul_x;
          int dy = (int)raw_dy * mul_y;
          mouse_post_event(dx, dy, (int)btn);
        }
      }
    }
  }
}

int
mouseread(struct inode *ip, char *dst, int n, int nonblock)
{
  int count = 0;
  iunlock(ip);

  /* Poll hardware / emulated USB mouse before reading queue */
  usb_mouse_poll();

  acquire(&mouse_queue.lock);
  while(n >= (int)sizeof(mouse_event_t)){
    while(mouse_queue.r == mouse_queue.w){
      if(curr_proc->killed){
        release(&mouse_queue.lock);
        ilock(ip);
        return -1;
      }
      if(nonblock){
        release(&mouse_queue.lock);
        ilock(ip);
        return count;
      }
      sleep(&mouse_queue.r, &mouse_queue.lock);
    }

    mouse_event_t ev = mouse_queue.buf[mouse_queue.r % MOUSE_BUF];
    mouse_queue.r++;

    if(copyout(curr_proc->pgdir, (uint)dst + count, &ev, sizeof(mouse_event_t)) < 0){
      release(&mouse_queue.lock);
      ilock(ip);
      return -1;
    }

    count += sizeof(mouse_event_t);
    n -= sizeof(mouse_event_t);

    if(mouse_queue.r == mouse_queue.w)
      break;
  }

  release(&mouse_queue.lock);
  ilock(ip);
  return count;
}

int
mousewrite(struct inode *ip, char *src, int n)
{
  int count = 0;
  while(n >= (int)sizeof(mouse_event_t)){
    mouse_event_t ev;
    if(copyin(curr_proc->pgdir, (char*)&ev, (uint)src + count, sizeof(mouse_event_t)) < 0)
      return -1;
    mouse_post_event(ev.dx, ev.dy, ev.buttons);
    count += sizeof(mouse_event_t);
    n -= sizeof(mouse_event_t);
  }
  return count;
}

void
mouse_init(void)
{
  initlock(&mouse_queue.lock, "mouse");
  mouse_queue.r = 0;
  mouse_queue.w = 0;

  pl011_mouse_init();

  cprintf("Initializing USB subsystem (CSUD)...\n");
  int res = UsbInitialise();
  cprintf("USB subsystem initialized (res=%d).\n", res);
  for(int i = 0; i < 5; i++){
    UsbCheckForChange();
  }
  cprintf("USB devices: MouseCount=%d, KeyboardCount=%d\n", MouseCount(), KeyboardCount());

  devsw[MOUSE].read  = mouseread;
  devsw[MOUSE].write = mousewrite;
}
