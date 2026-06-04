# AaronOS Agent Instructions

You are the specialized AI engine for **AaronOS**. Your primary job is the development, maintenance, and expansion of the AaronOS kernel and its ecosystem.

## Core Identity & Constraints
- **Project Name:** AaronOS
- **Kernel Integrity:** `kernel.c` must always be higher then the previous after editing. Do not truncate or simplify the core logic.

## Operational Workflow (Mandatory)
You must follow this three-step process for every request:

1. **PLAN FIRST:** Analyze the request and outline exactly what changes will be made across which files. List the logic and any potential impacts.
2. **CONFIRMATION:** Stop and wait for the user to say "proceed," "go," or give a confirmation nod before touching any code.
3. **EXECUTE:** Only after confirmation, perform the file edits and provide the updated full files.

## Technical Standards
- Maintain high standards for C code within the kernel.
- Ensure all newly added commands are registered in the internal help system.
- Make sure all code and commands work in QMEU CLI (no x11 display) mode. It should be configured to ouput to a serial port too.
## After testing and code edits
1. Ask the user if he wants to Commit changes to Github.
2. If the user agrees, then write a commit message and tell the user the commit message.
3. Final confirmation. 
4. Add all files (git add .) then commit with the commit message.

## How to run AaronOS.
- here is an explenation:
(2. The No-Graphic Mode

If AaronOS is configured to output to a serial port (it should be), you can use the -nographic flag. This disables all graphical output and redirects the serial port to your current terminal’s input/output.
```
Bash
qemu-system-x86_64 -hda aaronos.img -nographic
```
Note: To exit this mode, press Ctrl+A then X.)
## ABOUT THE CHAT
- Always add the full chat history to chat.json.
- Full chat history. Like this: 
```
chathistory {
chat1 {
Model {
 <model name>}
 user {
 first thing}
 }
 chat2 {
    user {
        hi
    }
    ai {
        whats up
    }
 }
 etc.
}
```
That is the way how to store chat.json.
Make sure chat.json is in the gitignore.

## About subagents to make your job easier
- You may use/make subagents. Here is how to do it:
1. ALWAYS READ " https://opencode.ai/docs/agents/ ". That is everything to know.
2. Make the subagent (if you need to.) run ``` opencode agent list ``` to see all agents. Then make an agent if that is required with ``` opencode agent create ```. Follow all on-screen instructions.


--------------------------------------------------------------------------------------------------------------------------------

# Session Progress (May 30 2026)

## Active Todo
See [`todo.md`](./todo.md) for the current task list, what's completed, in progress, and blocked.

## Latest Changes (May 30)
- **SMP trampoline**: Flat binary `[ORG 0x8000]`, position-independent, `lgdt [cs:gdt_desc]`
- **ACPI**: RSDP/RSDT/FADT parser, `acpi_poweroff()`, `acpi_reboot()`
- **DMA**: 8237 ISA DMA init + channel setup
- **SB16**: DSP reset + version check (graceful absent)
- **AHCI**: PCI scan class 0x0106, ABAR mapping, PIO read
- **SMP**: APIC MSR enable, SVR set, trampoline copy; IPI send skipped (hangs)
- **PS/2 mouse**: Enable, Compaq status, IRQ12 handler (3-byte packets)
- **Serial**: `serial_mirror=0` by default (fixes VGA white overflow); auto-detect via QEMU `-device loader,addr=0x500,data=0xBB,data-len=1`
- **GRUB**: timeout changed to 5s; `grub.cfg` written by `setup.sh`
- **Build**: `.gitignore` updated; `setup.sh --nographic` convenience flag

## Key Files
- `kernel.c` — serial mirror auto-detect, smp/acpi/dma/sb16/ahci init calls
- `trampoline.s` — Flat binary AP boot stub at 0x8000
- `smp.c` — APIC init, trampoline copy, IPI (skipped)
- `acpi.c`, `dma.c`, `sb16.c`, `ahci.c` — New HW drivers
- `keyboard.c` — PS/2 mouse init + IRQ12 handler (lines 114-182)
- `setup.sh` — Flat binary assembly, grub.cfg, `--nographic` flag
- `todo.md` — Current task tracking

## Blocked
- APIC ICR write (MMIO 0xFEE00300) hangs — needs #GP handler or x2APIC MSR ICR

## Git History
```
87ef0ce GUI overhaul
6877905 Serial toggle + mount_hd.sh
18e9f85 Wildcard glob support
d7777bf AaronScript interpreter
4bec5a9 AaronScript: $VAR, if/else/endif
fcdf98b I/O redirection
e90bc5d Heap allocator
0a4ef72 ELF loader
1e7fa25 Syscall (int 0x80)
edb950f Preemptive multitasking
3951c10 User mode (ring 3)
e5a904e Editor overhaul
```

## FAT16 Geometry (10 MB disk)
- 20480 total sectors, 512 B/sector, 1 SPC, 2 FATs, 512 root entries
- BPB: `sectors_per_fat=80`, `reserved_sectors=384`
- Constants: `FAT_SECTOR=384`, `ROOT_SECTOR=544`, `DATA_START_SECTOR=576`

# About ALPKG
## Alpkg is a package manager for Linux Systems.
### It is a shell script, and is called alpkg.
The file name is "alpkg" and it is in the current directory. 
Like with AaronOS, you may use subagents, and alpkg should be in the git ignore.