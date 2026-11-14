include ../makefile.inc

CFLAGS +=  -iquote ../ # -Wno-error=infinite-recursion needed when building on rpi
ASFLAGS += -I ../
ULIB = ulib.o usys.o printf.o umalloc.o

MKFS = ../tools/mkfs
FS_IMAGE = ../build/fs.img

UPROGS=\
	_cat\
	_echo\
	_grep\
	_init\
	_kill\
	_ln\
	_ls\
	_mkdir\
	_rm\
	_sh\
	_stressfs\
	_wc\
	_zombie\
	_wm\
	_mousetest\
	_ping\
	_fetch\

	#_usertests


all: $(FS_IMAGE)

_wm: wm.o wrapper.o $(ULIB)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ wm.o wrapper.o $(ULIB) -L ../ $(LIBGCC)
	$(OBJDUMP) -S $@ > wm.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > wm.sym
	arm-none-eabi-strip --strip-all $@


_%: %.o $(ULIB)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $^  -L ../ $(LIBGCC)
	$(OBJDUMP) -S $@ > $*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $*.sym
	arm-none-eabi-strip --strip-all $@

_forktest: forktest.o $(ULIB)
	# forktest has less library code linked in - needs to be small
	# in order to be able to max out the proc table.
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o _forktest forktest.o ulib.o usys.o
	$(OBJDUMP) -S _forktest > forktest.asm

$(MKFS):
	$(MAKE) -C ../tools

$(FS_IMAGE): $(MKFS) $(UPROGS)
	$(MKFS) $@  $(UPROGS) README
	$(OBJDUMP) -S usys.o > usys.asm

clean: 
	rm -f *.o *.d *.asm *.sym $(FS_IMAGE) \
	.gdbinit \
	$(UPROGS)
