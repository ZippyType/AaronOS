#ifndef FAT16_H
#define FAT16_H

#include <stdint.h>

// Ensure the compiler does not add padding between members
#define PACKED __attribute__((packed))

// FAT16 Bios Parameter Block (BPB)
struct FAT16_BPB {
    uint8_t  jmp[3];            // Jump instruction to boot code
    char     oem_name[8];       // OEM Name (e.g., "AaronOS ")
    uint16_t bytes_per_sector;  // Usually 512
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;  // Sectors before the first FAT
    uint8_t  fat_count;         // Number of FATs (usually 2)
    uint16_t root_entry_count;  // Max files in root directory (usually 512)
    uint16_t total_sectors_16;  // Total sectors if < 32MB
    uint8_t  media_type;        // 0xF8 for Hard Drive
    uint16_t sectors_per_fat;   // Size of one FAT table
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;  // Total sectors if > 32MB

    // Extended Boot Record
    uint8_t  drive_number;      // 0x80 for first HD
    uint8_t  reserved;
    uint8_t  boot_signature;    // 0x29
    uint32_t volume_id;
    char     volume_label[11];  // "AARONOS    "
    char     fs_type[8];        // "FAT16   "
} PACKED;

// FAT16 Directory Entry
struct FAT16_DirEntry {
    char     filename[8];
    char     extension[3];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  create_time_ms;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high; // Always 0 in FAT16
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint16_t first_cluster_low;  // The important one!
    uint32_t file_size;
} PACKED;

#endif