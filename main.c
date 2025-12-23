/*****************************************************************
*       main.c
*       by Zhiyi Huang, hzy@cs.otago.ac.nz
*       University of Otago
*
********************************************************************/


#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "arm.h"
#include "mailbox.h"

extern char end[]; // first address after kernel loaded from ELF file
extern pde_t *kpgdir;
extern FBI fbinfo;
extern volatile uint *mailbuffer;

void OkLoop()
{
   setgpiofunc(16, 1); // gpio 16, set as an output
   while(1){
        setgpioval(16, 0);
        delay(1000000);
        setgpioval(16, 1);
        delay(1000000);
   }
}

void NotOkLoop()
{
   setgpiofunc(16, 1); // gpio 16, set as an output
   while(1){
        setgpioval(16, 0);
        delay(100000);
        setgpioval(16, 1);
        delay(100000);
   }
}

void machinit(void)
{
    memset(cpus, 0, sizeof(struct cpu)*NCPU);
}


void enableirqminiuart(void);

const char *lsxv6_art[] = {
  "                                     ",
  "              X     X          6666  ",
  "               X   X  V     V 6      ",
  "L       SSSS    X X   V     V 6      ",
  "L      S         X    V     V  66666 ",
  "L       SSSS    X X    V   V  6     6",
  "L           S  X   X    V V   6     6",
  "LLLLLL  SSSS  X     X    V     66666 ",
  "                                     "
  };

u16 get_letter_color(char c) {
  switch(c) {
    case 'L': case 'S':
        return 0xFFE0; // yellow
    case 'X': case 'V':
        return 0x001F; // blue
    case '6':
      return 0xF800; // red
    default:
      return 0x0000; // black (space or other)
  }
}

void draw_logo_colored() {

  int lines = 9;
  for (int i = 0; i < lines; i++) {
    const char *line = lsxv6_art[i];
    for (int j = 0; line[j] != '\0'; j++) {
      char c = line[j];
      u16 color = get_letter_color(c);
      gpuputcolored(c, color); // automatically moves cursor
    }
    gpuputc('\n');
  }
 }

int cmain( uint r0)
{
  
  mmuinit1();
  machinit();
  uartinit();
  dsb_barrier();
  framebufferinit(); // init graphics framebuffer
  uartkbdinit();
  draw_logo_colored();
  kinit1(end, P2V(8*1024*1024));  // reserve 8 pages for PGDIR
  kpgdir=p2v(K_PDX_BASE);

  mailboxinit();
  create_request(mailbuffer, MPI_TAG_GET_ARM_MEMORY, 8, 0, 0);
  writemailbox((uint *)mailbuffer, 8);
  readmailbox(8);
  if(mailbuffer[1] != 0x80000000) 
    cprintf("new error readmailbox\n");
  else
    cprintf("ARM memory is %x %x\n", mailbuffer[MB_HEADER_LENGTH + TAG_HEADER_LENGTH], mailbuffer[MB_HEADER_LENGTH + TAG_HEADER_LENGTH+1]);

  pinit();
  tvinit();
  cprintf("it is ok after tvinit\n");
  binit();
cprintf("it is ok after binit\n");
  fileinit();
cprintf("it is ok after fileinit\n");
  iinit();
cprintf("it is ok after iinit\n");
  ideinit();
cprintf("it is ok after ideinit\n");
  timer3init();
  kinit2(P2V(8*1024*1024), P2V(PHYSTOP));
cprintf("it is ok after kinit2\n");
  userinit();
cprintf("it is ok after userinit\n");
  scheduler();


  NotOkLoop();

  return 0;
}


