#!/bin/bash

# --- 1. COMMIT STEP ---
echo "Cleaning up..."
rm -f *.o kernel.elf aaron_os.iso aaronos.pcap
ERRORS=""
echo "Checking for changes..."
if [ -n "$(git status --porcelain)" ]; then
    echo "Files modified. Enter commit message (leave blank to skip) (Always test before commiting):"
    read commit_msg
    if [ -n "$commit_msg" ]; then
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

compile_file "keyboard.c" "keyboard.o"
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

ld -m elf_i386 -T linker.ld -o kernel.elf boot.o keyboard.o installer.o editor.o fat16.o memory.o gui.o kernel.o net.o browser.o --no-warn-rwx-segments
mkdir -p iso_root/boot/grub
cp kernel.elf iso_root/boot/
grub-mkrescue -o aaron_os.iso iso_root

# --- 3. DYNAMIC RELEASE MANAGER ---
# We use /dev/tty to force the script to wait for REAL keyboard input
echo "Build successful! Create a GitHub draft release? [y/N]"
read release_choice < /dev/tty

if [[ "$release_choice" == [Yy]* ]]; then
    
    # 1. TAG SELECTION
    echo "Existing Tags:"
    tags=($(git tag -l))
    if [ ${#tags[@]} -eq 0 ]; then
        echo "No tags found."
        read -p "Type a new tag name (e.g. v1.0.0): " tag < /dev/tty
    else
        for i in "${!tags[@]}"; do printf "%d) %s\n" "$((i+1))" "${tags[$i]}"; done
        read -p "Select tag number OR type a new name: " tag_input < /dev/tty
        if [[ "$tag_input" =~ ^[0-9]+$ ]] && [ "$tag_input" -le "${#tags[@]}" ]; then
            tag="${tags[$((tag_input-1))]}"
        else
            tag="$tag_input"
        fi
    fi

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
# Ensure QEMU only starts after the logic above is finished
if [ ! -f hd.img ]; then qemu-img create -f raw hd.img 10M; fi
echo "Finalizing... Starting AaronOS Emulator."
qemu-system-x86_64 -cdrom aaron_os.iso -drive file=hd.img,format=raw -boot order=cd -m 256M -machine pc -audiodev pa,id=speaker -machine pcspk-audiodev=speaker -netdev user,id=n1 -device rtl8139,netdev=n1 -object filter-dump,id=f1,netdev=n1,file=aaronos.pcap