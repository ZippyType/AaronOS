# AaronOS

A hobby x86 operating system built from scratch. Features a Unix‑like shell, FAT16 filesystem with VFAT long names, preemptive multitasking, GUI desktop, TCP networking, audio, and an ELF loader for third‑party apps.

---

## Quick Start

```bash
git clone https://github.com/ZippyType/AaronOS.git
cd AaronOS
./setup.sh --nographic
```

Requires: `gcc` (i386), `nasm`, `binutils`, `grub-mkrescue`, `qemu-system-x86_64`, `python3`.

---

## Features

### Shell
- Command interpreter with up/down history, tab completion, 500‑line scrollback
- Environment variables (`set`, `export`)
- I/O redirection (`>`, `>>`, `<`) and pipes (`|`)
- AaronScript interpreter with `if`/`else`/`endif`, `$VAR`, comments

### Filesystem (FAT16)
- Full driver: create, delete, rename, copy, move files and directories
- **VFAT long filenames** — up to 255 characters, auto‑generated 8.3 SFN
- File attributes: read‑only, hidden, system, archive — view/set with `attrib`
- Commands: `ls`, `dir`, `cat`, `touch`, `write`, `edit`, `cp`, `mv`, `mkdir`, `rmdir`, `format`

### GUI Desktop (TUI)
- Window manager with file browser, system monitor, clock, mouse cursor
- PS/2 mouse with configurable sensitivity
- Launch apps from the desktop

### Editor
- Full‑screen text editor with cursor navigation, line numbers
- Syntax highlighting (C keywords, strings, comments, preprocessor)
- Find & replace (Ctrl+F, Ctrl+R, F3 next), undo/redo

### Networking
- RTL8139 PCI NIC driver with polling mode
- ARP, ICMP echo (ping), TCP
- Built‑in web browser (`web ip:port`)
- Commands: `ping`, `web`, `netstat`

### Audio
- PC speaker via PIT channel 2 — melodies, siren, beeps
- **Sound Blaster 16 DSP** — 8‑bit DMA playback with completion polling
- **WAV player** — `play file.wav` parses RIFF/WAV, converts stereo→mono and 16‑bit→8‑bit on the fly

### Process Model
- Preemptive multitasking (round‑robin scheduler, 100 Hz PIT timer)
- Ring 3 user mode with TSS stack switching
- **ELF loader** — load and execute 32‑bit ELF executables from disk (`exec program.elf`)
- Syscalls via `int 0x80`: write, exit, pipes, shared memory
- IPC: pipes, shared memory, signals

### Hardware
- ATA PIO (polled + IRQ), ACPI (poweroff/reboot), CMOS/RTC
- PS/2 keyboard & mouse, VGA text mode (80×25), serial (COM1)
- PCI bus scan, AHCI (PIO read), SMP (APIC) — early stage
- DMA (8237), SB16 DSP

### Memory
- Flat GDT with ring 3 segments + TSS
- Heap allocator (`malloc`/`free`/`calloc`/`realloc` with coalescing)

---

## Commands

```
help       gui        ver        reboot     shutdown   cls
clear      dmesg      edit       panic      cpu        credits
stats      time       tz         dir        ls         cat
write      touch      rm         rename     cp         mv
mkdir      rmdir      format     echo       rand       color
calc       beep       music      siren      matrix     netstat
web        ping       serial     set        export     grep
head       wc         sort       poweroff   install    sb16
play       attrib     exec
```

---

## Writing Apps for AaronOS

AaronOS loads 32‑bit ELF executables. There is no libc — you use raw syscalls via `int 0x80`.

### Syscall ABI

| Call | Number | `ebx` | `ecx` | `edx` | Returns |
|------|--------|-------|-------|-------|---------|
| `SYS_WRITE` | 1 | ignored | buffer ptr | length | bytes written (in `eax`) |
| `SYS_READ` | 2 | fd | buffer ptr | max len | 0 (stub) |
| `SYS_EXIT` | 3 | — | — | — | terminates process |
| `SYS_PIPE_CREATE` | 4 | — | — | — | pipe fd |
| `SYS_PIPE_WRITE` | 5 | pipe fd | buffer ptr | length | bytes written |
| `SYS_PIPE_READ` | 6 | pipe fd | buffer ptr | max len | bytes read |
| `SYS_SHM_CREATE` | 7 | key | size (bytes) | — | 0 on success |
| `SYS_SHM_ATTACH` | 8 | key | — | — | pointer to shared memory |

### Minimal example — hello.c

```c
void _start() {
    const char msg[] = "Hello from user mode!\n";
    int len = 0;
    while (msg[len]) len++;

    __asm__ volatile (
        "mov $1, %%eax\n"   /* SYS_WRITE */
        "mov $0, %%ebx\n"
        "mov %0, %%ecx\n"   /* buffer */
        "mov %1, %%edx\n"   /* length */
        "int $0x80\n"
        :
        : "r"(msg), "r"(len)
        : "eax", "ebx", "ecx", "edx"
    );

    __asm__ volatile (
        "mov $3, %%eax\n"   /* SYS_EXIT */
        "int $0x80\n"
        :
        :
        : "eax"
    );
}
```

### Compiling

```bash
gcc -m32 -ffreestanding -nostdlib -fno-stack-protector \
    -Wl,-Ttext=0x1000 -o hello.elf hello.c
```

- `-Ttext=0x1000` — code loads at virtual address `0x1000` (must match an unused range)
- The ELF must be **ET_EXEC** (type 2), 32‑bit, little‑endian
- Only `PT_LOAD` segments are loaded; the kernel copies each segment to its `p_vaddr`
- `.bss` is zero‑filled automatically

### Running

Copy the ELF to the hard disk image:

```bash
# Mount the image (loopback)
sudo losetup -fP hd.img
sudo mount /dev/loop0 /mnt
sudo cp hello.elf /mnt/
sudo umount /mnt
sudo losetup -d /dev/loop0
```

Then in AaronOS:

```
AaronOS> exec hello.elf
Hello from user mode!
```

### AaronScript

You can also write scripts without compiling:

```
echo "Hello from a script!" > test.aos
AaronOS> test.aos
```

Scripts support variables, `if`/`else`/`endif`, and comments (`#`).

---

## Running in QEMU

```bash
# With GUI window
./setup.sh

# CLI mode (serial output on terminal, exit with Ctrl+A then X)
./setup.sh --nographic
```

The script builds the ISO from source, optionally creates a 10 MB hard disk image (`hd.img`), and boots AaronOS in QEMU with networking (RTL8139 user‑mode), audio (PulseAudio), and ACPI support.

### Mounting the hard disk image

```bash
./mount_hd.sh    # mounts hd.img to a temp directory
```

---

## Project Layout

| File | Purpose |
|------|---------|
| `kernel.c` | Main kernel — shell, scheduler, syscalls, IDT, boot |
| `fat16.c` | FAT16 driver with VFAT long filenames |
| `elf.c` | ELF executable loader |
| `sb16.c` | Sound Blaster 16 driver + WAV player |
| `gui.c` | TUI desktop, windows, file browser |
| `editor.c` | Full‑screen text editor |
| `net.c` | RTL8139 driver, ARP, ICMP, TCP |
| `browser.c` | Web browser |
| `acpi.c` | ACPI poweroff/reboot |
| `dma.c` | 8237 DMA controller |
| `ahci.c` | AHCI SATA (early) |
| `smp.c` | SMP / APIC (early) |
| `memory.c` | Heap allocator |
| `mouse_keyboard.c` | PS/2 input drivers |
| `installer.c` | Disk installer |
| `setup.sh` | Build script |

---

## License

AaronOS is free software released under the GNU General Public License v3 or later (GPL‑3.0‑or‑later).
