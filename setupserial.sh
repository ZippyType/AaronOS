#!/bin/bash
# Serial output version of setup.sh for QEMU CLI mode (-nographic)

echo "Cleaning up..."
rm -f *.o kernel.elf aaron_os.iso aaronos.pcap serial.log
ERRORS=""

echo "Compiling..."
nasm -f elf32 boot.s -o boot.o 2>&1 || ERRORS+="boot.s "

compile_file() {
    gcc -m32 -c "$1" -o "$2" -ffreestanding -O2 -fno-stack-protector
    if [ $? -ne 0 ]; then ERRORS+="$1 "; fi
}

compile_file "mouse_keyboard.c" "mouse_keyboard.o"
compile_file "installer.c" "installer.o"
compile_file "editor.c" "editor.o"
compile_file "fat16.c" "fat16.o"
compile_file "memory.c" "memory.o"
compile_file "gui.c" "gui.o"
compile_file "kernel.c" "kernel.o"
compile_file "net.c" "net.o"
compile_file "browser.c" "browser.o"

if [ -n "$ERRORS" ]; then
    echo "ABORT: Errors detected in: $ERRORS"; exit 1
fi

ld -m elf_i386 -T linker.ld -o kernel.elf boot.o mouse_keyboard.o installer.o editor.o fat16.o memory.o gui.o kernel.o net.o browser.o --no-warn-rwx-segments
mkdir -p iso_root/boot/grub
cp kernel.elf iso_root/boot/
grub-mkrescue -o aaron_os.iso iso_root

if [ ! -f hd.img ]; then qemu-img create -f raw hd.img 10M; fi

echo "Starting AaronOS in CLI mode (serial output)..."
echo "To exit: press Ctrl+A then X"
echo ""

# Run in CLI mode with serial output to file
qemu-system-x86_64 \
    -cdrom aaron_os.iso \
    -hda hd.img \
    -display none \
    -serial stdio