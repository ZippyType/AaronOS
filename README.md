# AaronOS - Always the top of the list.
## Needed: 
### x86/32 bit processor, .iso flashing tool (like Rufus), the latest AaronOS .iso, a DVD/CD, Floppy Disk, or USB Stick, and at least 32 MB of RAM. (Can run on 4 MB with not very good performance.)
## What it has/Features
1. A Unix type shell ( not based on Unix though!)
2. The GRUB bootloader
3. Some basic file system commands
4. Can make some basic music
5. Can show cpu infomation
6. A calculator
## How to install/try
1. Go to the releases page.
2. Download the latest AaronOS iso.
3. Flash it to an iso, or use QMEU (a emulator) with this command:
   ``` qemu-img create -f raw hd.img 10M && qemu-system-x86_64 -cdrom aaron_os.iso -drive file=hd.img,format=raw -boot order=cd -m 256M -machine pc -audiodev pa,id=speaker -machine pcspk-audiodev=speaker ```
4. Boot from the .iso (go to your BIOS or just do nothing on QMEU.)
5. In the GRUB bootloader, click "Try or Install AaronOS."
6. You should hear a boot sound and a shell like this: ``` AaronOS>_ ```
7. To install, type ``` install ``` and then ↵. To see commands, type ``` help ```.
8. ENJOY!