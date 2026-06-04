# Compiler settings
CC = gcc
CFLAGS = -m32 -c -ffreestanding -O2 -fno-stack-protector -nostdlib
LD = ld
LDFLAGS = -m elf_i386 -T linker.ld --no-warn-rwx-segments

# Files
OBJS = boot.o mouse_keyboard.o installer.o editor.o fat16.o memory.o gui.o \
       kernel.o net.o browser.o elf.o acpi.o dma.o sb16.o ahci.o smp.o embed.o

# Build targets
all: kernel.elf

kernel.elf: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

boot.o: boot.s
	nasm -f elf32 $< -o $@

clean:
	rm -f *.o *.elf