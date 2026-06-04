# AaronOS Roadmap

## Filesystem (FAT16)
- [x] Format drive
- [x] List files (dir/ls)
- [x] Read file (cat)
- [x] Write file (write / edit)
- [x] Create file (touch)
- [x] Delete file (rm)
- [x] Rename file (rename)
- [x] Copy file (cp)
- [x] Move/rename file (mv)
- [x] Create directory (mkdir)
- [x] Remove directory (rmdir)
- [x] cd into subdirectories
- [x] File permissions / attributes (attrib command, read-only protection)
- [x] Long filename support (VFAT read, display, collect_entries)

## Shell & CLI
- [x] Command interpreter
- [x] Help system
- [x] Scrollback (500 lines)
- [x] Tab completion
- [x] Command history with up/down arrows
- [x] Environment variables
- [x] Scripting / batch files (AaronScript with if/else/endif, $VAR)
- [x] I/O redirection (> >> <)
- [x] Pipes (|)
- [x] Serial console input
- [ ] Install command — writes whole AaronOS to disk

## TUI / Desktop
- [x] GUI mode launcher
- [x] Window rendering engine
- [x] File browser
- [x] System monitor
- [x] Mouse support
- [ ] Clickable buttons / widgets
- [ ] Desktop icons
- [ ] Window manager (drag, resize, close, minimize)
- [ ] Taskbar & start menu

## Editor
- [x] Full-screen editor
- [x] Cursor navigation (arrows, page up/down, home, end)
- [x] Save / load files
- [x] Line numbers
- [x] Syntax highlighting (C keywords, strings, comments, preprocessor)
- [x] Find & replace (Ctrl+F, Ctrl+R, F3 next)
- [ ] Multiple file buffers / tabs
- [x] Undo / redo

## Hardware / Drivers
- [x] ATA PIO (read/write)
- [x] PS/2 keyboard
- [x] PIT timer (100 Hz)
- [x] PC speaker (PIT ch 2)
- [x] CMOS / RTC
- [x] VGA text mode (80x25)
- [x] Serial port output (COM1)
- [ ] ATA PIO with IRQ (non-polling)
- [ ] DMA
- [ ] ACPI / power management
- [ ] USB keyboard / mouse (UHCI/EHCI)
- [ ] AHCI / SATA
- [ ] Audio (Sound Blaster 16 / AC97 / HDA)
- [ ] Multi-core / SMP
- [ ] More drivers for different network cards
- [ ] Interface in TUI to select drivers for different stuff (Audio, network, etc.)

## Memory Management
- [x] Global Descriptor Table (flat model) — with ring 3 segments + TSS
- [ ] Paging / virtual memory (page tables)
- [x] Heap allocator (malloc / free / calloc / realloc with coalescing)
- [ ] Memory-mapped files
- [ ] Swap / disk paging

## Process Model
- [x] Preemptive multitasking / scheduler (round-robin, timer IRQ context switch)
- [x] Processes & syscalls (int 0x80: SYS_WRITE, SYS_READ, SYS_EXIT)
- [x] ELF loader (elf_load, exec command)
- [x] User mode (ring 3, TSS stack switching)
- [x] IPC (shared memory, pipes, signals)

## Networking
- [x] RTL8139 PCI NIC driver
- [x] ARP (address resolution)
- [x] ICMP echo (ping)
- [x] TCP (web browser)
- [ ] DNS resolution
- [ ] DHCP (automatic IP config)
- [ ] UDP
- [ ] Socket API for user programs
- [ ] NFS / network file system

## Development Tooling
- [x] alpkg package manager (external repo)
- [ ] On-device assembler (NASM port?)
- [ ] On-device C compiler (TinyCC / pcc port?)
- [ ] Kernel debugger / GDB stub

## Audio
- [x] PC speaker beeps / melodies
- [ ] Sound Blaster 16 DMA playback
- [ ] AC97 / HDA audio
- [ ] WAV / MOD player

## Misc / Polish
- [ ] Boot splash / animation
- [ ] Configuration file (aaronos.cfg)
- [ ] Filesystem journaling
- [ ] init system / service manager
- [ ] Built-in games
- [ ] Installer improvements