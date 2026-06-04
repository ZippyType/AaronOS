# AaronOS — Compacted Context (May 27 2026)

## Summary
Hobby x86 OS with FAT16 filesystem, VGA text-mode terminal, PC speaker audio, RTL8139 networking, TCP browser, and a TUI desktop. Built for QEMU (`-nographic` serial output) and real hardware.

## Recent Changes
- **Format fixed**: BPB rewritten byte-by-byte with proper 0x29 signature, 0x55AA boot sig, correct geometry (512 B/sector, 1 SPC, 2 FATs, 512 root entries, 20480 total, 32 FAT sectors, 63 SPT, 16 heads, 0x80 drive).
- **ATA write fix**: Post-write BSY wait added to `ata_write_sector()` to fix format cluster zeroing.
- **Cursor fix**: Editor saves/restores VGA cursor shape, shell always shows blinking cursor.
- **`write` fix**: Bare `write` invokes editor.
- **cp/mv/mkdir/rmdir**: All four commands implemented in `fat16.c` and dispatched in `kernel.c`.

## FAT16 Layout (10 MB disk)
- Sector 0: BPB (512 bytes)
- Sectors 1–32: FAT1 (32 sectors, cluster 0=0xFFF8, cluster 1=0xFFFF)
- Sectors 33–64: FAT2 (mirror)
- Sectors 65–96: Root dir (32 sectors × 16 entries = 512 entries)
- Sectors 97+: Data region (cluster = sector - 97 + 2)

## New Commands
| Command | Handler | Description |
|---------|---------|-------------|
| `cp src dst` | `fat16_copy_file()` | Copies cluster chain, allocates new clusters |
| `mv src dst` | `fat16_move_file()` | Calls `fat16_rename_file()` (flat FS, no hierarchy yet) |
| `mkdir name` | `fat16_mkdir()` | Creates dir entry with attr=0x10, writes `.`/`..` cluster |
| `rmdir name` | `fat16_rmdir()` | Checks empty (only `.`/`..`), frees chain, marks deleted |

## Relevant Files
- `/workspaces/workspaces/fat16.c` — FAT16 driver (format, list, cat, write, create, delete, rename, copy, move, mkdir, rmdir)
- `/workspaces/workspaces/fat16.h` — BPB/DirEntry structs + function prototypes
- `/workspaces/workspaces/kernel.c` — GDT/IDT, VGA engine, shell dispatch (~1140 lines)
- `/workspaces/workspaces/commands.h` — Command table (37 commands)
- `/workspaces/workspaces/editor.c` — Full-screen editor with cursor management
- `/workspaces/workspaces/io.h` — Port I/O, serial output (COM1), `init_serial()`
- `/workspaces/workspaces/todo.md` — Full feature roadmap

## Build
```
rm -f *.o kernel.elf
nasm -f elf32 boot.s -o boot.o
gcc -m32 -c *.c -ffreestanding -O2 -fno-stack-protector
ld -m elf_i386 -T linker.ld -o kernel.elf *.o --no-warn-rwx-segments
grub-mkrescue -o aaron_os.iso iso_root
```
Run: `qemu-system-x86_64 -cdrom aaron_os.iso -drive file=hd.img,format=raw -nographic`

Exit QEMU: Ctrl+A then X

## Known Issues
- **Serial input**: COM1 TX works (used by `print()`), but RX not wired to keyboard handler. Cannot pipe commands into `-nographic` mode — need to wire serial RX to input buffer or use `expect`.
- **No subdirectory traversal**: `ls`/`cat`/`rm` etc. only search root dir. `mkdir` creates proper `.`/`..` entries but shell has no `cd` command yet.
- **8.3 names only**: No LFN support. `format_name()` strips extension, `find_file()` ignores extension.
