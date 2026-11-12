# specify path to QEMU, installed with MacPorts 
QEMU = qemu-system-arm

include makefile.inc

# link the libgcc.a for __aeabi_idiv. ARM has no native support for div
LIBS = $(LIBGCC) libcsud.a

OBJS = \
	bio.o\
	console.o\
	exception.o\
	exec.o\
	file.o\
	fs.o\
        kalloc.o\
        #keyboard.o\
	log.o\
	mailbox.o\
	main.o\
	memide.o\
	mmu.o\
	pipe.o\
	proc.o\
	spinlock.o\
	string.o\
	syscall.o\
	sysfile.o\
	sysproc.o\
	timer.o\
	trap.o\
	uart.o\
	wrapper.o\
	vm.o \

#device/picirq.o \

KERNEL_SRC = bio.c console.c exception.c exec.c file.c fs.c kalloc.c \
             log.c mailbox.c main.c memide.c mmu.c pipe.c \
             proc.c spinlock.c string.c syscall.c sysfile.c sysproc.c \
             timer.c trap.c uart.c wrapper.c vm.c framebuffer.c uart_keyboard.c mouse.c

KERN_OBJS = $(patsubst %.c,%.o,$(KERNEL_SRC)) entry.o keyboard.o

libcsud.a:
	make -C csud driver CONFIG=FINAL TYPE=STANDALONE TARGET=RPI GNU=arm-none-eabi-
	cp -f csud/libcsud.a .

kernel.elf: $(addprefix build/,$(KERN_OBJS)) kernel.ld build/initcode build/fs.img libcsud.a
	cp -f build/initcode initcode
	cp -f build/fs.img fs.img
	$(call LINK_BIN, kernel.ld, kernel.elf, \
		$(addprefix build/,$(KERN_OBJS)), \
		initcode fs.img)
	$(OBJCOPY) kernel.elf -O binary kernel.img
	@echo kernel image has been built.
	@$(OBJDUMP) -d kernel.elf > kernel.dis
	$(OBJDUMP) -S kernel.elf > kernel.asm
	$(OBJDUMP) -t kernel.elf | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > kernel.sym
	rm -f initcode fs.img

qemu: kernel.elf
	@echo "=================================================================="
	@echo "  Starting Antigravity OS in QEMU (1024x768 GUI Display)"
	@echo "=================================================================="
	@echo "  MOUSE & KEYBOARD INSTRUCTIONS:"
	@echo "  1. MOUSE IN QEMU WINDOW: Move your physical mouse and click!"
	@echo "  2. ARROW KEYS IN TERMINAL: Up/Down/Left/Right moves cursor!"
	@echo "  3. [Home] / [End]: Click / Drag window."
	@echo "  4. Press [1] or [2] (or Tab) to switch terminal window focus."
	@echo "  5. Press Ctrl-A then X in your terminal to quit QEMU."
	@echo "==================================================================\n"
	$(QEMU) -M raspi0 -kernel kernel.elf -usb -device usb-mouse -device usb-kbd -serial null -serial mon:stdio

qemu-nox: kernel.elf
	@echo "Starting QEMU headless (-display none)..."
	@echo "Serial console is active on this terminal. Press Ctrl-A then X to terminate.\n"
	$(QEMU) -M raspi0 -kernel kernel.elf -display none -serial null -serial stdio

qemu-gdb: kernel.elf
	@echo "Starting QEMU waiting for GDB on tcp::1234..."
	$(QEMU) -M raspi0 -kernel kernel.elf -display none -serial null -serial stdio -S -gdb tcp::1234

INITCODE_OBJ = initcode.o
$(addprefix build/,$(INITCODE_OBJ)): initcode.S
	$(call build-directory)
	$(call AS_WITH, -nostdinc -I.)

#initcode is linked into the kernel, it will be used to craft the first process
build/initcode: $(addprefix build/,$(INITCODE_OBJ))
	$(call LINK_INIT, -N -e start -Ttext 0)
	$(call OBJCOPY_INIT)
	$(OBJDUMP) -S $< > initcode.asm

.PHONY: build/fs.img qemu qemu-nox clean

build/fs.img:
	make -C tools
	make -C usr

clean: 
	rm -rf build
	rm -f *.o *.d *.asm *.sym vectors.S bootblock \
	initcode initcode.out fs.img kernel.elf kernel.dis kernel.img
	make -C tools clean
	make -C usr clean
