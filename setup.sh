#!/bin/bash

NOGRAPHIC=0
if [ "$1" = "--nographic" ]; then NOGRAPHIC=1; fi

# --- 1. COMMIT STEP ---
echo "Cleaning up..."
rm -f *.o drivers/*.o kernel.elf kernel_first.elf aaron_os.iso aaronos.pcap core.img trampoline_embed.c aim_embed.o aim_stub.o aaronos.aim
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
    gcc -m32 -c "$1" -o "$2" -ffreestanding -O1 -fno-stack-protector -I.
    if [ $? -ne 0 ]; then ERRORS+="$1 "; fi
}

nasm -f elf32 boot.s -o boot.o
if [ $? -ne 0 ]; then ERRORS+="boot.s "; fi
nasm -f bin -o trampoline.bin trampoline.s
if [ $? -ne 0 ]; then ERRORS+="trampoline.s "; fi

# Auto-detect all .c files
C_SRCS=()
for f in *.c; do
    C_SRCS+=("$f")
done

C_OBJS=()
for f in "${C_SRCS[@]}"; do
    compile_file "$f" "${f%.c}.o"
    C_OBJS+=("${f%.c}.o")
done

DRV_OBJS=()
for f in drivers/*.c; do
    [ -f "$f" ] || continue
    compile_file "$f" "${f%.c}.o"
    DRV_OBJS+=("${f%.c}.o")
done

if [ -n "$ERRORS" ]; then
    echo "ABORT: Errors detected in: $ERRORS"; exit 1
fi

# --- 3. EMBEDDED DATA (trampoline for SMP) ---

python3 -c "
tdata = open('trampoline.bin', 'rb').read()
with open('trampoline_embed.c', 'w') as f:
    f.write('#include <stdint.h>\n')
    f.write('const uint8_t embedded_trampoline[] = {\n')
    for i in range(0, len(tdata), 12):
        f.write('  ' + ', '.join(f'0x{b:02x}' for b in tdata[i:i+12]) + ',\n')
    f.write('};\n')
    f.write('const uint32_t embedded_trampoline_len = ' + str(len(tdata)) + ';\n')
"

gcc -m32 -c trampoline_embed.c -o trampoline_embed.o -ffreestanding -O2 -nostdlib 2>/dev/null

# --- 4. TWO-PASS BUILD ---

# Create stub .aim symbols for first-pass link (replaced by real aim_embed.o in pass 2)
printf '#include <stdint.h>\nconst uint8_t _binary_aaronos_aim_start[1]={0};\nconst uint8_t _binary_aaronos_aim_end[1]={0};\n' > /tmp/aim_stub.c
gcc -m32 -c /tmp/aim_stub.c -o aim_stub.o -ffreestanding -O2 -nostdlib 2>/dev/null

# Pass 1: Link kernel_first.elf with aim_stub.o (placeholders for .aim symbols)
echo "[Pass 1] Linking kernel_first.elf..."
ld -m elf_i386 -T linker.ld -o kernel_first.elf boot.o "${C_OBJS[@]}" "${DRV_OBJS[@]}" trampoline_embed.o aim_stub.o --no-warn-rwx-segments
if [ $? -ne 0 ]; then echo "ABORT: First-pass link failed"; exit 1; fi

# Create .aim archive with: limine-bios.sys, kernel.elf (from pass 1), limine_installed.conf
echo "[Pass 1] Creating .aim archive..."
python3 tools/aim_pack.py aaronos.aim \
    "limine-bios.sys:limine/limine-bios.sys" \
    "kernel.elf:kernel_first.elf" \
    "limine_installed.conf:limine_installed.conf"
if [ $? -ne 0 ]; then echo "ABORT: .aim creation failed"; exit 1; fi

# Generate object file from .aim archive
echo "[Pass 2] Embedding .aim..."
objcopy -I binary -O elf32-i386 -B i386 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    aaronos.aim aim_embed.o
if [ $? -ne 0 ]; then echo "ABORT: objcopy failed"; exit 1; fi

# Pass 2: Link final kernel.elf replacing stubs with real .aim data
echo "[Pass 2] Linking kernel.elf..."
ld -m elf_i386 -T linker.ld -o kernel.elf boot.o "${C_OBJS[@]}" "${DRV_OBJS[@]}" trampoline_embed.o aim_embed.o --no-warn-rwx-segments
if [ $? -ne 0 ]; then echo "ABORT: Second-pass link failed"; exit 1; fi

echo "[OK] kernel.elf ready ($(stat -c%s kernel.elf) bytes)"

# --- 5. ISO CREATION (Limine) ---
rm -f iso_root/overlay.bmp iso_root/boot/kernel.elf iso_root/boot/limine.conf iso_root/limine-bios.sys iso_root/limine-bios-cd.bin iso_root/limine.conf iso_root/limine.cfg 2>/dev/null
mkdir -p iso_root/boot
cp kernel.elf iso_root/boot/
cp overlay.bmp iso_root/
cp limine/limine-bios.sys iso_root/
cp limine/limine-bios-cd.bin iso_root/
cp limine.fnt iso_root/boot/
cp limine.conf iso_root/boot/

xorriso -as mkisofs \
    -b limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    --protective-msdos-label \
    iso_root \
    -o aaron_os.iso 2>/dev/null

if [ $? -ne 0 ]; then
    echo "ABORT: ISO creation failed"
    exit 1
fi

# Install Limine hybrid MBR so the ISO also boots from USB
limine/limine bios-install aaron_os.iso 2>/dev/null

echo "Build successful!"

# --- 6. DYNAMIC RELEASE MANAGER ---
echo "Create a GitHub draft release? [y/N]"
read release_choice < /dev/tty

if [[ "$release_choice" == [Yy]* ]]; then
    git fetch --tags --force 2>/dev/null
    echo ""
    echo "Existing Tags:"
    tags=($(git tag -l | sort -V))
    if [ ${#tags[@]} -eq 0 ]; then
        echo "No tags found."
        read -p "Type a new tag name (e.g. v1.0.0): " tag < /dev/tty
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
            exit 0
        elif [ "$tag_input" = "$((${#tags[@]}+2))" ]; then
            echo ""
            exit 0
        elif [[ "$tag_input" =~ ^[0-9]+$ ]] && [ "$tag_input" -le "${#tags[@]}" ]; then
            tag="${tags[$((tag_input-1))]}"
        else
            tag="$tag_input"
        fi
    fi

    read -p "Enter Release Title: " custom_title < /dev/tty
    if [ -z "$custom_title" ]; then custom_title="Release $tag"; fi

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

    read -p "Type extra files to upload (blank for none): " extra_files < /dev/tty

    gh release create "$tag" aaron_os.iso $extra_files --title "$custom_title" --draft --notes "$notes"
    echo "Draft release '$custom_title' created."
fi

# --- 7. BOOT ---
echo "Create a new 10M hard disk image (hd.img)? [y/N]"
read disk_choice < /dev/tty
if [[ "$disk_choice" == [Yy]* ]]; then
    qemu-img create -f raw hd.img 10M 2>/dev/null && echo "New hd.img created (10 MB)."
else
    echo "Using existing hd.img (if any)."
fi

echo "Finalizing... Starting AaronOS Emulator"
rm -f *.o drivers/*.o *.elf core.img trampoline_embed.c aim_embed.o aim_stub.o aaronos.aim /tmp/aim_stub.c 2>/dev/null

if [ "$NOGRAPHIC" -eq 1 ]; then
    qemu-system-x86_64 -cdrom aaron_os.iso -drive file=hd.img,format=raw,if=ide,index=0,media=disk -boot order=d -m 256M -machine pc -audiodev pa,id=speaker -machine pcspk-audiodev=speaker -audiodev pa,id=sb16 -device sb16,audiodev=sb16 -netdev user,id=n1 -device rtl8139,netdev=n1 -object filter-dump,id=f1,netdev=n1,file=aaronos.pcap -nographic -device loader,addr=0x500,data=0xBB,data-len=1 -no-reboot
    echo "Exit with Ctrl+A then X"
else
    qemu-system-x86_64 -cdrom aaron_os.iso -drive file=hd.img,format=raw,if=ide,index=0,media=disk -boot order=d -m 256M -machine pc -audiodev pa,id=speaker -machine pcspk-audiodev=speaker -audiodev pa,id=sb16 -device sb16,audiodev=sb16 -netdev user,id=n1 -device rtl8139,netdev=n1 -object filter-dump,id=f1,netdev=n1,file=aaronos.pcap
fi
