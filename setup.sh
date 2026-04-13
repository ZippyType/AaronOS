#!/bin/bash

# Clean up previous builds
# We remove the old objects and binaries to ensure a fresh compile
rm -f *.o kernel.elf aaron_os.iso hd.img
echo "Cleaning up..."

# Assemble the bootloader
nasm -f elf32 boot.s -o boot.o

# Compile C files
# Using -O2 for optimization and -ffreestanding since we have no standard library
gcc -m32 -c keyboard.c -o keyboard.o -ffreestanding -O2 -fno-stack-protector
gcc -m32 -c installer.c -o installer.o -ffreestanding -O2 -fno-stack-protector
gcc -m32 -c editor.c -o editor.o -ffreestanding -O2 -fno-stack-protector
gcc -m32 -c fat16.c -o fat16.o -ffreestanding -O2 -fno-stack-protector
gcc -m32 -c memory.c -o memory.o -ffreestanding -O2 -fno-stack-protector
gcc -m32 -c kernel.c -o kernel.o -ffreestanding -O2 -fno-stack-protector


# Link everything together
# We include memory.o and fat16.o to ensure the new features are linked
ld -m elf_i386 -T linker.ld -o kernel.elf boot.o keyboard.o installer.o editor.o fat16.o memory.o kernel.o --no-warn-rwx-segments

# ISO creation
# We assume your grub.cfg is already in the correct location (iso_root/boot/grub/grub.cfg)
mkdir -p iso_root/boot/grub
cp kernel.elf iso_root/boot/

echo "Creating ISO with existing grub.cfg..."
grub-mkrescue -o aaron_os.iso iso_root

# Virtual Drive creation
# Create a 10MB raw disk image if it doesn't exist
if [ ! -f hd.img ]; then 
    echo "Creating virtual drive..."
    qemu-img create -f raw hd.img 10M
fi

echo "Build complete. Booting AaronOS..."

# Launch QEMU with hardware audio and the virtual drive
qemu-system-x86_64 -cdrom aaron_os.iso -drive file=hd.img,format=raw -boot order=cd -m 256M -machine pc -audiodev pa,id=speaker -machine pcspk-audiodev=speaker