#ifndef FAT16_H
#define FAT16_H

#include <stdint.h>

#define PACKED __attribute__((packed))

#define ATTR_READ_ONLY  0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20
#define ATTR_LFN        0x0F

struct FAT16_BPB {
    uint8_t  jmp[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint8_t  drive_number;
    uint8_t  reserved;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} PACKED;

struct FAT16_DirEntry {
    char     filename[8];
    char     extension[3];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  create_time_ms;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high; 
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} PACKED;

void fat16_format_drive();
void fat16_list_files();
void fat16_cat(char* name);
void fat16_write_file(char* name, char* content);
void fat16_create_file(char* name);
void fat16_delete_file(char* name);
void fat16_rename_file(char* oldname, char* newname);
void fat16_copy_file(char* src, char* dst);
void fat16_move_file(char* src, char* dst);
void fat16_mkdir(char* name);
void fat16_rmdir(char* name);
int fat16_read_file(char* name, char* buffer, int max_len);
uint32_t fat16_file_size(char* name);
void fat16_write_binary(char* name, const uint8_t* data, uint32_t len);
void fat16_cd(char* name);
void fat16_get_cwd(char* buf, int max);
uint16_t fat16_get_cwd_cluster();
int  fat16_collect_display_names(char (*names)[13], int max);
void fat16_attrib(char* args);

typedef struct {
    char name[256];
    uint32_t size;
    uint8_t attrs;
} fat16_entry_t;
int fat16_collect_entries(fat16_entry_t* entries, int max);
int disk_ready();
int ata_init();

void fat16_set_partition_offset(uint32_t lba);

int ata_read_sector(uint32_t lba, uint8_t* buffer);
int ata_write_sector(uint32_t lba, const uint8_t* buffer);

#endif
