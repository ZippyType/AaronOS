# AaronOS v2.0

## New Features

### Audio Playback
- SB16 DMA playback now blocks until completion (no more instant stop)
- WAV file player — parse RIFF/WAVE headers, convert stereo→mono and 16-bit→8-bit
- `play file.wav` command reads and plays WAV files from disk

### File System (FAT16)
- **VFAT Long Filename (LFN) support** — read and display long names in `ls`/`dir`; `touch` with long names creates proper VFAT entries (SFN + checksum + LFN directory entries)
- **`attrib` command** — view/set file attributes (`attrib [+-RHS] filename`): Read-only, Hidden, System, Archive
- **Read-only protection** — `rm`, `write`, `cp` now respect the read-only attribute
- Fixed `fat16_read_file` to read full files by directory size (supports binary data including WAV)

### Build System
- **Auto-detection of C sources** — `setup.sh` and `setupserial.sh` now glob `*.c` automatically; no more manual edits when adding new `.c` files

## Hardware Drivers
- SB16 DSP reset/version check, 8-bit DMA playback with completion polling
- WAV conversion pipeline (stereo→mono, 16-bit→8-bit, any sample rate)

## Fixes
- SB16 playback now properly waits for DMA to finish before stopping
- VFAT read function corrected (reads LFN entries in proper order)
- `read_vfat_name` now reconstructs names correctly from reverse-ordered entries
- File size reported by directory entry is used instead of stopping at null bytes

## Technical Details
- VFAT: seq=1 (first 13 chars) stored closest to 8.3 entry; entries ordered seq=N…seq=1 on disk
- SB16: DSP command 0xC0 with length-1 format; poll bit 5 of DSP_BUFSTAT for completion
- DMA: ISA DMA channel 1, mode 0x49 (single, non-autoinit, write), max 16 MB addressable
