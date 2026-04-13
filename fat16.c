#include "fat16.h"
#include "io.h"
#include <stddef.h>

#define SECTOR_SIZE 512
#define ROOT_SECTOR 65
#define DATA_START_SECTOR 97 // Reserved(1) + FATs(2*32) + Root(32)

extern void print(const char* str);
extern void putchar_col(char c, uint8_t color);
extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);
extern uint16_t inw(uint16_t port);
extern void* kmalloc(size_t size);

void drive_wait() {
    while ((inb(0x1F7) & 0x80) || !(inb(0x1F7) & 0x08));
}

void ata_read_sector(uint32_t lba, uint8_t* buffer) {
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, 1);
    outb(0x1F3, (uint8_t)lba);
    outb(0x1F4, (uint8_t)(lba >> 8));
    outb(0x1F5, (uint8_t)(lba >> 16));
    outb(0x1F7, 0x20); 
    drive_wait();
    uint16_t* ptr = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++) ptr[i] = inw(0x1F0);
}

void ata_write_sector(uint32_t lba, const uint8_t* buffer) {
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, 1);
    outb(0x1F3, (uint8_t)lba);
    outb(0x1F4, (uint8_t)(lba >> 8));
    outb(0x1F5, (uint8_t)(lba >> 16));
    outb(0x1F7, 0x30); 
    drive_wait();
    uint16_t* ptr = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        uint16_t data = ptr[i];
        asm volatile("outw %0, %1" : : "a"(data), "Nd"(0x1F0));
    }
}

void fat16_format_drive() {
    struct FAT16_BPB bpb;
    for(int i = 0; i < sizeof(bpb); i++) ((uint8_t*)&bpb)[i] = 0;
    bpb.bytes_per_sector = 512;
    bpb.sectors_per_cluster = 1;
    bpb.reserved_sectors = 1;
    bpb.fat_count = 2;
    bpb.root_entry_count = 512;
    bpb.total_sectors_16 = 20480;
    bpb.sectors_per_fat = 32;
    for(int i = 0; i < 8; i++) bpb.oem_name[i] = "AARONOS "[i];
    uint8_t s0[512] = {0};
    for(int i = 0; i < sizeof(bpb); i++) s0[i] = ((uint8_t*)&bpb)[i];
    ata_write_sector(0, s0);
    uint8_t empty[512] = {0};
    for(uint32_t s = 65; s < 70; s++) ata_write_sector(s, empty);
}

void fat16_list_files() {
    uint8_t buf[512];
    ata_read_sector(ROOT_SECTOR, buf);
    struct FAT16_DirEntry* entries = (struct FAT16_DirEntry*)buf;
    print("Files:\n");
    for (int i = 0; i < 16; i++) {
        if (entries[i].filename[0] == 0) break;
        if (entries[i].filename[0] == (char)0xE5) continue;
        for (int j = 0; j < 8; j++) if(entries[i].filename[j]!=' ') putchar_col(entries[i].filename[j], 0x07);
        print(".");
        for (int j = 0; j < 3; j++) putchar_col(entries[i].extension[j], 0x07);
        print("\n");
    }
}

// Phase 2 New: Read File Content
void fat16_cat(char* name) {
    uint8_t buf[512];
    ata_read_sector(ROOT_SECTOR, buf);
    struct FAT16_DirEntry* entries = (struct FAT16_DirEntry*)buf;
    
    for (int i = 0; i < 16; i++) {
        // Simple comparison (ignoring extension for this helper)
        if (entries[i].filename[0] == name[0]) { 
            uint32_t sector = DATA_START_SECTOR + entries[i].first_cluster_low - 2;
            uint8_t data_buf[512];
            ata_read_sector(sector, data_buf);
            print((char*)data_buf);
            print("\n");
            return;
        }
    }
    print("File not found.\n");
}

void fat16_write_to_test(char* content) {
    uint8_t buf[512];
    ata_read_sector(ROOT_SECTOR, buf);
    struct FAT16_DirEntry* entries = (struct FAT16_DirEntry*)buf;

    for (int i = 0; i < 16; i++) {
        if (entries[i].filename[0] == 0 || entries[i].filename[0] == (char)0xE5) {
            for(int j=0; j<8; j++) entries[i].filename[j] = "TEST    "[j];
            for(int j=0; j<3; j++) entries[i].extension[j] = "TXT"[j];
            entries[i].first_cluster_low = 2; // Fixed cluster for simplicity
            ata_write_sector(ROOT_SECTOR, buf);
            
            uint8_t data_buf[512] = {0};
            for(int j=0; content[j] && j<511; j++) data_buf[j] = content[j];
            ata_write_sector(DATA_START_SECTOR, data_buf);
            print("Saved to TEST.TXT\n");
            return;
        }
    }
}