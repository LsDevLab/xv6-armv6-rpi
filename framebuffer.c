/*****************************************************************
*       framebuffer.c
*       adapted from MIT xv6 by Zhiyi Huang, hzy@cs.otago.ac.nz
*       University of Otago
*
********************************************************************/


#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "arm.h"
#include "spinlock.h"

uint frameheight=768, framewidth=1024, framecolors=16;
FBI fbinfo __attribute__ ((aligned (16), nocommon));
extern u8 font[];

static uint gpucolour=0xffff;


uint cursor_x=0, cursor_y=0;
uint console_frameheight=768, console_framewidth=1024;
uint fontheight=16, fontwidth=8;
static struct {
  struct spinlock lock;
  int locking;
} cons;

static int panicked = 0;

void setgpucolour(u16 c)
{
    gpucolour = c;
}

uint initframebuf(uint width, uint height, uint depth)
{
    fbinfo.width = width;
    fbinfo.height = height;
    fbinfo.v_width = width;
    fbinfo.v_height = height;
    fbinfo.pitch = 0;
    fbinfo.depth = depth;
    fbinfo.x = 0;
    fbinfo.y = 0;
    fbinfo.fbp = 0;
    fbinfo.fbs = 0;
    writemailbox((uint *)&fbinfo, 1);
    return readmailbox(1);
}

void drawpixel(uint x, uint y)
{
    u16 *addr;

    if(x >= framewidth || y >= frameheight) return;
    addr = (u16 *) fbinfo.fbp;
//    addr = (u16 *) ((FBI *)FrameBufferInfo)->fbp;
    addr += y*1024 + x;
    *addr = gpucolour;
    return;
}


void drawcursor(uint x, uint y)
{
u8 row, bit;

    for(row=0; row<15; row++)
        for(bit=0; bit<8; bit++)
            drawpixel(x+bit, y+row);
}

void drawcharacter(u8 c, uint x, uint y)
{
u8 *faddr;
u8 row, bit, bits;
uint tv;

    if(c > 127) return;
    tv = ((uint)c) << 4;
    faddr = font + tv;
    for(row=0; row<15; row++){
        bits = *(faddr+row);
        for(bit=0; bit<8; bit++){
            if((bits>>bit) & 1) drawpixel(x+bit, y+row);
        }
    }

}


// Scroll framebuffer up by one text row and clear the bottom row
void fb_scroll_text_region(uint fb_width,
                           uint fb_height,
                           uint row_height,
                           u16 bg_color)
{
    // Move framebuffer content up by one text row
    memmove(
        (u8 *)fbinfo.fbp,
        (u8 *)fbinfo.fbp + fb_width * row_height * 2,
        (fb_height - row_height) * fb_width * 2
    );

    // Clear the bottom row
    u8 *last_row =
        (u8 *)fbinfo.fbp + (fb_height - row_height) * fb_width * 2;

    if (bg_color == 0) {
        // Fast path: black background
        memset(last_row, 0, fb_width * row_height * 2);
    } else {
        // Fill with color
        u16 *p = (u16 *)last_row;
        for (uint i = 0; i < fb_width * row_height; i++)
            p[i] = bg_color;
    }
}


void framebufferinit(void)
{
  uint fbinfoaddr;

  fbinfoaddr = initframebuf(framewidth, frameheight, framecolors);
  if(fbinfoaddr != 0) NotOkLoop();

}

//TODO IMPLEMENT GLOBAL, ON TOP BASIC CONSOLE FOR ERRORS, PANICS, STARTUP

void
panic(char *s)
{
    int i;
    uint pcs[10];

    cprintf("cpu%d: panic: ", 0);
    cprintf(s);
    cprintf("\n");
    getcallerpcs(&s, pcs);
    for(i=0; i<10; i++)
        cprintf(" %p", pcs[i]);
    panicked = 1; // freeze other CPU

    for(;;)
        ;
}


static void
printint(int xx, int base, int sign)
{
  static u8 digits[] = "0123456789abcdef";
  u8 buf[16];
  int i;
  uint x, y, b;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  b = base;
  i = 0;
  do{
    y = div(x, b);
    buf[i++] = digits[x - y * b];
  }while((x = y) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0){
    gpuputc(buf[i]);
    uartputc(buf[i]);
  }
}

void
cprintf(char *fmt, ...)
{
    int i, c;
    int locking;
    uint *argp;
    char *s;

    locking = cons.locking;
    if(locking)
        acquire(&cons.lock);

    if (fmt == 0)
        panic("null fmt");

    argp = (uint *)(void*)(&fmt + 1);
    for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
        if(c != '%'){
            gpuputc(c);
            uartputc(c);
            continue;
        }
        c = fmt[++i] & 0xff;
        if(c == 0)
            break;
        switch(c){
            case 'd':
                printint(*argp++, 10, 1);
            break;
            case 'x':
            case 'p':
              printint(*argp++, 16, 0);
            break;
            case 's':
                if((s = (char*)*argp++) == 0)
                    s = "(null)";
            for(; *s; s++){
                gpuputc(*s);
                uartputc(*s);
            }
            break;
            case '%':
                gpuputc('%');
            uartputc('%');
            break;
            default:
                // Print unknown % sequence to draw attention.
                    gpuputc('%');
            uartputc('%');
            gpuputc(c);
            uartputc(c);
            break;
        }
    }
    if(locking)
        release(&cons.lock);
}

void gpuputc(uint c) {
  gpuputcolored(c, 0xffff);
}

//static void
// introduced visual backspaces (so that drawcursor() is no more needed)
void gpuputcolored(uint c, u16 color) {
  if(c == '\n') {
    cursor_x = 0;
    cursor_y += fontheight;
    if(cursor_y >= console_frameheight) {
      fb_scroll_text_region(
    	console_framewidth,
    	console_frameheight,
    	fontheight,
    	0   // black background
	  );
      cursor_y = console_frameheight - fontheight;
    }
  }
  // else if(c == BACKSPACE) {
  //   if(cursor_x >= fontwidth) {
  //     cursor_x -= fontwidth;
  //     // erase previous character
  //     setgpucolour(0);  // black
  //     for(uint row = 0; row < fontheight; row++)
  //       for(uint col = 0; col < fontwidth; col++)
  //         drawpixel(cursor_x + col, cursor_y + row);
  //     setgpucolour(color); // restore drawing color
  //   }
  // }
  else {
    if(c != ' '){
      setgpucolour(color);
      drawcharacter(c, cursor_x, cursor_y);
    }
    cursor_x += fontwidth;
    if(cursor_x >= console_framewidth) {
      cursor_x = 0;
      cursor_y += fontheight;
      if(cursor_y >= console_frameheight) {
        fb_scroll_text_region(
    		console_framewidth,
    		console_frameheight,
    		fontheight,
    		0   // black background
	  	);
        cursor_y = console_frameheight - fontheight;
      }
    }
  }
}


// Draw a filled rectangle
void fb_fillrect(int x, int y, int w, int h, u16 color) {
    for(int j = y; j < y+h && j < frameheight; j++) {
        for(int i = x; i < x+w && i < framewidth; i++) {
            setgpucolour(color);
            drawpixel(i, j);
        }
    }
}

int
fbwrite(struct inode *ip, char *userbuf, int n)
{
    int off = 0;
    acquire(&cons.lock);

    while(n >= sizeof(fb_pixel_t)) {
        fb_pixel_t pix;

        // copy one pixel/char struct from user space
        if(copyin(curr_proc->pgdir, (char*)&pix, (uint)userbuf + off, sizeof(fb_pixel_t)) < 0) {
            release(&cons.lock);
            return -1;
        }

        if(pix.x < fbinfo.width && pix.y < fbinfo.height) {
            if(pix.ch) {
                // draw character using font bitmap
                setgpucolour(pix.color);
                drawcharacter((u8)pix.ch, pix.x, pix.y);
            } else {
                // draw single pixel
                setgpucolour(pix.color);
                drawpixel(pix.x, pix.y);
            }
        }

        off += sizeof(fb_pixel_t);
        n -= sizeof(fb_pixel_t);
    }

    release(&cons.lock);
    return off;
}


int
fbread(struct inode *ip, char *userbuf, int n)
{
    // Optional: you can implement read to return framebuffer content
    // For now, let's just return 0
    return 0;
}

// Device initialization
void framebuffer_init(void) {
    memset(devsw, 0, sizeof(struct devsw)*NDEV);
    devsw[FRAMEBUFFER].read = fbread;
    devsw[FRAMEBUFFER].write = fbwrite;
}