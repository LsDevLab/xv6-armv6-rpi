// /*****************************************************************
// *       console.c
// *       adapted from MIT xv6 by Zhiyi Huang, hzy@cs.otago.ac.nz
// *       University of Otago
// *
// ********************************************************************/
//
//
//
// #include "types.h"
// #include "defs.h"
// #include "param.h"
// #include "traps.h"
// #include "spinlock.h"
// #include "fs.h"
// #include "file.h"
// #include "memlayout.h"
// #include "mmu.h"
// #include "proc.h"
// #include "arm.h"
//
// #define BACKSPACE 0x100
//
// uint cursor_x=0, cursor_y=0;
// uint console_frameheight=768, console_framewidth=1024;
// uint fontheight=16, fontwidth=8;
//
// static int panicked = 0;
//
// static struct {
//   struct spinlock lock;
//   int locking;
// } cons;
//
// #define INPUT_BUF 128
// struct {
//   struct spinlock lock;
//   char buf[INPUT_BUF];
//   uint r;  // Read index
//   uint w;  // Write index
//   uint e;  // Edit index
// } input;
//
// int
// consolewrite(struct inode *ip, char *buf, int n) {
//   int i;
//
// //  cprintf("consolewrite is called: ip=%x buf=%x, n=%x", ip, buf, n);
//   iunlock(ip);
//   acquire(&cons.lock);
//   for(i = 0; i < n; i++){
//     gpuputc(buf[i] & 0xff);
//     uartputc(buf[i] & 0xff);
//   }
//   release(&cons.lock);
//   ilock(ip);
//
//   return n;
// }
//
//
// static void
// printint(int xx, int base, int sign)
// {
//   static u8 digits[] = "0123456789abcdef";
//   u8 buf[16];
//   int i;
//   uint x, y, b;
//
//   if(sign && (sign = xx < 0))
//     x = -xx;
//   else
//     x = xx;
//
//   b = base;
//   i = 0;
//   do{
//     y = div(x, b);
//     buf[i++] = digits[x - y * b];
//   }while((x = y) != 0);
//
//   if(sign)
//     buf[i++] = '-';
//
//   while(--i >= 0){
//     gpuputc(buf[i]);
//     uartputc(buf[i]);
//   }
// }
//
//
// // Print to the console. only understands %d, %x, %p, %s.
// void
// cprintf(char *fmt, ...)
// {
//   int i, c;
//   int locking;
//   uint *argp;
//   char *s;
//
//   locking = cons.locking;
//   if(locking)
//     acquire(&cons.lock);
//
//   if (fmt == 0)
//     panic("null fmt");
//
//   argp = (uint *)(void*)(&fmt + 1);
//   for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
//     if(c != '%'){
//         gpuputc(c);
// 	uartputc(c);
//       continue;
//     }
//     c = fmt[++i] & 0xff;
//     if(c == 0)
//       break;
//     switch(c){
//     case 'd':
//       printint(*argp++, 10, 1);
//       break;
//     case 'x':
//     case 'p':
//       printint(*argp++, 16, 0);
//       break;
//     case 's':
//       if((s = (char*)*argp++) == 0)
//         s = "(null)";
//       for(; *s; s++){
//         gpuputc(*s);
// 	uartputc(*s);
//       }
//       break;
//     case '%':
// 	gpuputc('%');
// 	uartputc('%');
//       break;
//     default:
//       // Print unknown % sequence to draw attention.
// 	gpuputc('%');
// 	uartputc('%');
// 	gpuputc(c);
// 	uartputc(c);
//       break;
//     }
//   }
//   if(locking)
//     release(&cons.lock);
// }
//
// void
// panic(char *s)
// {
//   int i;
//   uint pcs[10];
//
//   cprintf("cpu%d: panic: ", 0);
//   cprintf(s);
//   cprintf("\n");
//   getcallerpcs(&s, pcs);
//   for(i=0; i<10; i++)
//     cprintf(" %p", pcs[i]);
//   panicked = 1; // freeze other CPU
//
//   for(;;)
//     ;
// }
//
// #define C(x)  ((x)-'@')  // Control-x
//
// void
// consputc(int c)
// {
//   if(panicked){
//     cli();
//     for(;;)
//       ;
//   }
//
//   if(c == BACKSPACE){
//     gpuputc(BACKSPACE);
//   } else if(c == C('D')) {
//     gpuputc('^'); gpuputc('D');
//     uartputc('^'); uartputc('D');
//   } else {
//     gpuputc(c);
//     uartputc(c);
//   }
// }
//
//
// void
// consoleintr(int (*getc)(void))
// {
//   int c;
//
//   acquire(&input.lock);
//   while((c = getc()) >= 0){
//     switch(c){
//     case C('P'):  // Process listing.
//       procdump();
//       break;
//     case C('U'):  // Kill line.
//       while(input.e != input.w &&
//             input.buf[(input.e-1) % INPUT_BUF] != '\n'){
//         input.e--;
//         consputc(BACKSPACE);
//       }
//       break;
//     case C('H'): case '\x7f':  // Backspace
//       if(input.e != input.w){
//         input.e--;
//         consputc(BACKSPACE);
//       }
//       break;
//     default:
//       if(c != 0 && input.e-input.r < INPUT_BUF){
// 	if(c == 0xa) break;
//         c = (c == 0xd) ? '\n' : c;
//         input.buf[input.e++ % INPUT_BUF] = c;
//         consputc(c);
//         if(c == '\n' || c == C('D') || input.e == input.r+INPUT_BUF){
//           input.w = input.e;
//           wakeup(&input.r);
//         }
//       }
//       break;
//     }
//   }
//   release(&input.lock);
// }
//
// int
// consoleread(struct inode *ip, char *dst, int n)
// {
//   uint target;
//   int c;
//
// //cprintf("inside consoleread\n");
//   iunlock(ip);
//   target = n;
//   acquire(&input.lock);
//   while(n > 0){
//     while(input.r == input.w){
//       if(curr_proc->killed){
//         release(&input.lock);
//         ilock(ip);
//         return -1;
//       }
//       sleep(&input.r, &input.lock);
//     }
//     c = input.buf[input.r++ % INPUT_BUF];
//     if(c == C('D')){  // EOF
//       if(n < target){
//         // Save ^D for next time, to make sure
//         // caller gets a 0-byte result.
//         input.r--;
//       }
//       break;
//     }
//     *dst++ = c;
//     --n;
//     if(c == '\n')
//       break;
//   }
//   release(&input.lock);
//   ilock(ip);
//
//   return target - n;
// }
//
//
// void gpuputc(uint c) {
//   gpuputcolored(c, 0xffff);
// }
//
// //static void
// // introduced visual backspaces (so that drawcursor() is no more needed)
// void gpuputcolored(uint c, u16 color) {
//   if(c == '\n') {
//     cursor_x = 0;
//     cursor_y += fontheight;
//     if(cursor_y >= console_frameheight) {
//       fb_scroll_text_region(
//     	console_framewidth,
//     	console_frameheight,
//     	fontheight,
//     	0   // black background
// 	  );
//       cursor_y = console_frameheight - fontheight;
//     }
//   }
//   else if(c == BACKSPACE) {
//     if(cursor_x >= fontwidth) {
//       cursor_x -= fontwidth;
//       // erase previous character
//       setgpucolour(0);  // black
//       for(uint row = 0; row < fontheight; row++)
//         for(uint col = 0; col < fontwidth; col++)
//           drawpixel(cursor_x + col, cursor_y + row);
//       setgpucolour(color); // restore drawing color
//     }
//   }
//   else {
//     if(c != ' '){
//       setgpucolour(color);
//       drawcharacter(c, cursor_x, cursor_y);
//     }
//     cursor_x += fontwidth;
//     if(cursor_x >= console_framewidth) {
//       cursor_x = 0;
//       cursor_y += fontheight;
//       if(cursor_y >= console_frameheight) {
//         fb_scroll_text_region(
//     		console_framewidth,
//     		console_frameheight,
//     		fontheight,
//     		0   // black background
// 	  	);
//         cursor_y = console_frameheight - fontheight;
//       }
//     }
//   }
// }
//
//
// void consoleinit(void)
// {
//   initlock(&cons.lock, "console");
//   memset(&input, 0, sizeof(input));
//   initlock(&input.lock, "input");
//
//   memset(devsw, 0, sizeof(struct devsw)*NDEV);
//   devsw[CONSOLE].write = consolewrite;
//   devsw[CONSOLE].read = consoleread;
//   cons.locking = 1;
//   panicked = 0; // must initialize in code since the compiler does not
//
//   cursor_x=cursor_y=0;
// }
//
