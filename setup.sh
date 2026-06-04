#!/bin/bash

NOGRAPHIC=0
if [ "$1" = "--nographic" ]; then NOGRAPHIC=1; fi

# --- 1. COMMIT STEP ---
echo "Cleaning up..."
rm -f *.o kernel.elf aaron_os.iso aaronos.pcap
ERRORS=""
echo "Checking for changes..."
if [ -n "$(git status --porcelain)" ]; then
    echo "Files modified. Enter commit message (leave blank to skip) (Always test before commiting):"
    read commit_msg
    if [ -n "$commit_msg" ]; then
        git rm -r --cached . >/dev/null
        git add .
        git commit -m "$commit_msg"
        git push -u origin main --force 
    fi
fi

# --- 2. COMPILE & ERROR CHECKING ---

compile_file() {
    echo "Compiling $1..."
    gcc -m32 -c "$1" -o "$2" -ffreestanding -O2 -fno-stack-protector
    if [ $? -ne 0 ]; then ERRORS+="$1 "; fi
}

nasm -f elf32 boot.s -o boot.o
if [ $? -ne 0 ]; then ERRORS+="boot.s "; fi
nasm -f bin -o trampoline.bin trampoline.s
if [ $? -ne 0 ]; then ERRORS+="trampoline.s "; fi

# Auto-detect all .c files (exclude auto-generated embed.c)
C_SRCS=()
for f in *.c; do
    case "$f" in
        embed.c|embed_real.c) continue;;
        *) C_SRCS+=("$f");;
    esac
done

C_OBJS=()
for f in "${C_SRCS[@]}"; do
    compile_file "$f" "${f%.c}.o"
    C_OBJS+=("${f%.c}.o")
done

if [ -n "$ERRORS" ]; then
    echo "ABORT: Errors detected in: $ERRORS"; exit 1
fi

# Create stub embedded kernel symbols for first pass
echo '#include <stdint.h>' > embed.c
echo 'const uint8_t embedded_kernel[] = {0};' >> embed.c
echo 'const uint32_t embedded_kernel_len = 0;' >> embed.c
echo 'const uint8_t grub_boot_img[] = {0};' >> embed.c
echo 'const uint32_t grub_boot_img_len = 0;' >> embed.c
echo 'const uint8_t grub_core_img[] = {0};' >> embed.c
echo 'const uint32_t grub_core_img_len = 0;' >> embed.c
echo 'const uint8_t embedded_trampoline[] = {0};' >> embed.c
echo 'const uint32_t embedded_trampoline_len = 0;' >> embed.c
gcc -m32 -c embed.c -o embed.o -ffreestanding -O2 -nostdlib 2>/dev/null

# First pass: link kernel with stub embed.o
ld -m elf_i386 -T linker.ld -o kernel.elf boot.o "${C_OBJS[@]}" embed.o --no-warn-rwx-segments

# Generate real embedded kernel binary from first-pass kernel
python3 -c "
import sys
kdata = open('kernel.elf', 'rb').read()
with open('embed_real.c', 'w') as f:
    f.write('#include <stdint.h>\n')
    f.write('const uint8_t embedded_kernel[] = {\n')
    for i in range(0, len(kdata), 12):
        f.write('  ' + ', '.join(f'0x{b:02x}' for b in kdata[i:i+12]) + ',\n')
    f.write('};\n')
    f.write('const uint32_t embedded_kernel_len = ' + str(len(kdata)) + ';\n')
    # GRUB boot.img
    bdata = open('/usr/lib/grub/i386-pc/boot.img', 'rb').read()
    f.write('const uint8_t grub_boot_img[] = {\n')
    for i in range(0, len(bdata), 12):
        f.write('  ' + ', '.join(f'0x{b:02x}' for b in bdata[i:i+12]) + ',\n')
    f.write('};\n')
    f.write('const uint32_t grub_boot_img_len = ' + str(len(bdata)) + ';\n')
    # SMP trampoline
    tdata = open('trampoline.bin', 'rb').read()
    f.write('const uint8_t embedded_trampoline[] = {\n')
    for i in range(0, len(tdata), 12):
        f.write('  ' + ', '.join(f'0x{b:02x}' for b in tdata[i:i+12]) + ',\n')
    f.write('};\n')
    f.write('const uint32_t embedded_trampoline_len = ' + str(len(tdata)) + ';\n')
"

# Also generate core.img using grub-mkimage
rm -f core.img 2>/dev/null
grub-mkimage -O i386-pc -o core.img -p /boot/grub fat multiboot biosdisk 2>/dev/null
if [ -f core.img ]; then
    python3 -c "
cdata = open('core.img', 'rb').read()
with open('embed_real.c', 'a') as f:
    f.write('const uint8_t grub_core_img[] = {\n')
    for i in range(0, len(cdata), 12):
        f.write('  ' + ', '.join(f'0x{b:02x}' for b in cdata[i:i+12]) + ',\n')
    f.write('};\n')
    f.write('const uint32_t grub_core_img_len = ' + str(len(cdata)) + ';\n')
"
    echo "GRUB core.img embedded ("$(wc -c < core.img)" bytes)."
else
    echo "Warning: grub-mkimage failed, using stub."
fi
rm -f core.img

# Compile real embedded data and relink
gcc -m32 -c embed_real.c -o embed_real.o -ffreestanding -O2 -nostdlib 2>/dev/null
if [ $? -eq 0 ]; then
    mv embed_real.o embed.o
ld -m elf_i386 -T linker.ld -o kernel.elf boot.o "${C_OBJS[@]}" embed.o --no-warn-rwx-segments
    echo "Embedded kernel + GRUB data included."
else
    echo "Warning: embed_real.o failed, using first-pass kernel."
fi

mkdir -p iso_root/boot/grub
cp kernel.elf iso_root/boot/
cat > iso_root/boot/grub/grub.cfg << 'GRUBEOF'
set timeout=10
set default=0

set menu_color_normal=cyan/black
set menu_color_highlight=yellow/blue

menuentry "Boot AaronOS" {
    multiboot2 /boot/kernel.elf
    boot
}

menuentry "Boot From Hard Disk" {
    insmod part_msdos
    set root=(hd0)
    chainloader +1
    boot
}

menuentry "Boot From CD" {
    insmod iso9660
    set root=(cd0)
    chainloader +1
    boot
}

menuentry "Shutdown" {
    halt
}
GRUBEOF
grub-mkrescue -o aaron_os.iso iso_root

# --- 3. DYNAMIC RELEASE MANAGER ---
# We use /dev/tty to force the script to wait for REAL keyboard input
echo "Build successful! Create a GitHub draft release? [y/N]"
read release_choice < /dev/tty

if [[ "$release_choice" == [Yy]* ]]; then
    
    # 1. TAG SELECTION
    while true; do
        echo "Syncing tags from remote..."
        git fetch --tags --force 2>/dev/null
        echo ""
        echo "Existing Tags:"
        tags=($(git tag -l | sort -V))
        if [ ${#tags[@]} -eq 0 ]; then
            echo "No tags found."
            read -p "Type a new tag name (e.g. v1.0.0): " tag < /dev/tty
            break
        else
            for i in "${!tags[@]}"; do printf "%d) %s\n" "$((i+1))" "${tags[$i]}"; done
            echo "$((${#tags[@]}+1))) Delete a tag"
            echo "$((${#tags[@]}+2))) Refresh tag list"
            read -p "Select tag number, type a new name, delete, or refresh: " tag_input < /dev/tty
            if [ "$tag_input" = "$((${#tags[@]}+1))" ]; then
                read -p "Tag number to delete: " del_num < /dev/tty
                if [[ "$del_num" =~ ^[0-9]+$ ]] && [ "$del_num" -le "${#tags[@]}" ]; then
                    del_tag="${tags[$((del_num-1))]}"
                    git tag -d "$del_tag" 2>/dev/null
                    git push origin ":refs/tags/$del_tag" 2>/dev/null
                    echo "Deleted tag: $del_tag"
                fi
                echo ""
                continue
            elif [ "$tag_input" = "$((${#tags[@]}+2))" ]; then
                echo ""
                continue
            elif [[ "$tag_input" =~ ^[0-9]+$ ]] && [ "$tag_input" -le "${#tags[@]}" ]; then
                tag="${tags[$((tag_input-1))]}"
                break
            else
                tag="$tag_input"
                break
            fi
        fi
    done

    # 2. CUSTOM TITLE
    read -p "Enter Release Title: " custom_title < /dev/tty
    if [ -z "$custom_title" ]; then custom_title="Release $tag"; fi
    
    # 3. RELEASE NOTES
    echo "Release Notes: 1) Select .MD from home, 2) Write custom"
    read note_choice < /dev/tty
    
    notes=""
    if [ "$note_choice" == "1" ]; then
        echo "Scanning for .MD files..."
        mapfile -t md_files < <(find $HOME -maxdepth 3 -name "*.md")
        for i in "${!md_files[@]}"; do printf "%d) %s\n" "$((i+1))" "${md_files[$i]}"; done
        read -p "Select file number: " md_choice < /dev/tty
        notes=$(cat "${md_files[$((md_choice-1))]}")
    else
        echo "Enter Markdown notes (Press ENTER, then Ctrl+D):"
        notes=$(cat)
    fi

    # 4. EXTRA FILES
    read -p "Type extra files to upload (blank for none): " extra_files < /dev/tty
    
    # 5. EXECUTE RELEASE
    gh release create "$tag" aaron_os.iso $extra_files --title "$custom_title" --draft --notes "$notes"
    echo "Draft release '$custom_title' created."
fi

# --- 4. BOOT ---
# Ask about hard disk image
echo "Create a new 10M hard disk image (hd.img)? [y/N]"
read disk_choice < /dev/tty
if [[ "$disk_choice" == [Yy]* ]]; then
    qemu-img create -f raw hd.img 10M 2>/dev/null && echo "New hd.img created (10 MB)."
else
    echo "Using existing hd.img (if any)."
fi
echo "Finalizing... Starting AaronOS Emulator, and deleting the .o files"
rm -f *.o *.elf embed*.c core.img 2>/dev/null
if [ "$NOGRAPHIC" -eq 1 ]; then
    qemu-system-x86_64 -cdrom aaron_os.iso -drive file=hd.img,format=raw,if=ide,index=0,media=disk -boot order=cd -m 256M -machine pc -audiodev pa,id=speaker -machine pcspk-audiodev=speaker -netdev user,id=n1 -device rtl8139,netdev=n1 -object filter-dump,id=f1,netdev=n1,file=aaronos.pcaps -nographic -device loader,addr=0x500,data=0xBB,data-len=1 -no-reboot
    echo "Exit with Ctrl+A then X"
else
    qemu-system-x86_64 -cdrom aaron_os.iso -drive file=hd.img,format=raw,if=ide,index=0,media=disk -boot order=cd -m 256M -machine pc -audiodev pa,id=speaker -machine pcspk-audiodev=speaker -netdev user,id=n1 -device rtl8139,netdev=n1 -object filter-dump,id=f1,netdev=n1,file=aaronos.pcaps
fi