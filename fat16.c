#include "fat16.h"
#include "io.h"
#include <stddef.h>

#define SECTOR_SIZE 512
#define FAT_SECTOR 384
#define FAT_SECTOR_COUNT 80
#define ROOT_SECTOR 544
#define ROOT_SECTOR_COUNT 32
#define DATA_START_SECTOR 576

#define FAT16_FREE 0x0000
#define FAT16_EOF 0xFFFF

extern void print(const char* str);
extern void print_col(const char* str, uint8_t color);
extern void putchar_col(char c, uint8_t color);
extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);
extern uint16_t inw(uint16_t port);
extern int kstrcmp(const char* a, const char* b);

static uint16_t next_free_cluster = 2;
static uint16_t current_dir_cluster = 0;
static char current_path[128] = "";

/* Forward declarations for VFAT helpers */
static int get_entry_at_in(int index, struct FAT16_DirEntry* out, uint16_t dir_cluster);
static int set_entry_at_in(int index, struct FAT16_DirEntry* entry, uint16_t dir_cluster);
static int find_file_in(char* name, uint16_t dir_cluster);
static int read_dir_sector(uint16_t dir_cluster, uint32_t s, uint8_t* buf);
static int dir_sector_count(uint16_t dir_cluster);

/* VFAT / LFN helpers */
#define LFN_ATTR 0x0F
#define LFN_LAST 0x40

static uint8_t vfat_checksum(const char* short_name) {
    uint8_t sum = 0;
    for (int i = 11; i > 0; i--)
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + (uint8_t)(short_name[i - 1]);
    return sum;
}

static int is_lfn_entry(const struct FAT16_DirEntry* e) {
    return e->attributes == LFN_ATTR;
}

static int read_vfat_name(uint16_t dir_cluster, int entry_idx, char* out, int max) {
    int lfn_count = 0;
    int first_lfn = entry_idx;
    while (first_lfn > 0) {
        struct FAT16_DirEntry e;
        if (!get_entry_at_in(first_lfn - 1, &e, dir_cluster)) break;
        if (!is_lfn_entry(&e)) break;
        lfn_count++;
        first_lfn--;
    }
    if (lfn_count == 0) return 0;

    int pos = 0;
    for (int i = entry_idx - 1; i >= first_lfn; i--) {
        struct FAT16_DirEntry e;
        if (!get_entry_at_in(i, &e, dir_cluster)) break;
        uint8_t* raw = (uint8_t*)&e;
        uint16_t chars[13];
        int ci = 0;
        for (int j = 1; j <= 10; j += 2) chars[ci++] = raw[j] | (raw[j+1] << 8);
        for (int j = 14; j <= 25; j += 2) chars[ci++] = raw[j] | (raw[j+1] << 8);
        for (int j = 28; j <= 31; j += 2) chars[ci++] = raw[j] | (raw[j+1] << 8);

        for (int k = 0; k < ci && k < 13 && pos < max - 1; k++) {
            if (chars[k] < 0x80) out[pos++] = (char)chars[k];
        }
    }
    out[pos] = '\0';
    return 1;
}

static int is_short_name(const char* name) {
    int len = 0;
    const char* p;
    for (p = name; *p; p++) len++;
    if (len > 12) return 0;
    int dot_count = 0;
    for (p = name; *p; p++) if (*p == '.') dot_count++;
    if (dot_count > 1) return 0;
    return 1;
}

static int kstrlen_local(const char* s) {
    int n = 0;
    while (*s++) n++;
    return n;
}

static int to_upper_char(int c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

static int is_valid_sfn_char(int c) {
    c = to_upper_char(c);
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           (c == '_' || c == '^' || c == '$' || c == '~' ||
            c == '!' || c == '#' || c == '%' || c == '&' ||
            c == '-' || c == '{' || c == '}' || c == '@' ||
            c == '\'' || c == '(' || c == ')');
}

static void generate_sfn(const char* long_name, char* sfn_base, char* sfn_ext) {
    for (int i = 0; i < 8; i++) sfn_base[i] = ' ';
    for (int i = 0; i < 3; i++) sfn_ext[i] = ' ';

    const char* dot = 0;
    for (const char* p = long_name; *p; p++) if (*p == '.') dot = p;

    int base_len = dot ? (int)(dot - long_name) : kstrlen_local(long_name);
    if (base_len > 6) base_len = 6;

    int bi = 0;
    for (int i = 0; i < base_len && bi < 6; i++) {
        if (is_valid_sfn_char(long_name[i]))
            sfn_base[bi++] = to_upper_char(long_name[i]);
        else
            sfn_base[bi++] = '_';
    }
    sfn_base[bi] = '~';
    sfn_base[bi+1] = '1';

    if (dot && *(dot+1)) {
        int ei = 0;
        for (const char* p = dot + 1; *p && ei < 3; p++, ei++) {
            if (is_valid_sfn_char(*p))
                sfn_ext[ei] = to_upper_char(*p);
            else
                sfn_ext[ei] = '_';
        }
    } else if (!dot) {
        /* No extension at all — default to TXT */
        sfn_ext[0] = 'T'; sfn_ext[1] = 'X'; sfn_ext[2] = 'T';
    }
    /* If dot is last char or extension empty, also default to TXT */
    if (dot && !*(dot+1)) {
        sfn_ext[0] = 'T'; sfn_ext[1] = 'X'; sfn_ext[2] = 'T';
    }
}

static int write_lfn_entries(int idx, const char* name, uint8_t checksum, int lfn_count, uint16_t dir_cluster) {
    int name_len = kstrlen_local(name);
    for (int s = lfn_count; s >= 1; s--) {
        int entry_idx = idx - lfn_count + (lfn_count - s);
        struct FAT16_DirEntry e;
        for (int i = 0; i < 32; i++) ((uint8_t*)&e)[i] = 0;

        uint8_t* raw = (uint8_t*)&e;
        raw[0] = (s == lfn_count) ? (s | LFN_LAST) : s;
        raw[11] = LFN_ATTR;

        raw[13] = checksum;

        int char_start = (s - 1) * 13;
        int char_pos = 0;
        int buf_idx = 1;
        for (int c = 0; c < 5 && char_pos < 13; c++, char_pos++) {
            if (char_start + char_pos < name_len) {
                raw[buf_idx] = (uint8_t)name[char_start + char_pos];
                raw[buf_idx + 1] = 0;
            } else {
                raw[buf_idx] = 0xFF;
                raw[buf_idx + 1] = 0xFF;
            }
            buf_idx += 2;
        }

        buf_idx = 14;
        for (int c = 0; c < 6 && char_pos < 13; c++, char_pos++) {
            if (char_start + char_pos < name_len) {
                raw[buf_idx] = (uint8_t)name[char_start + char_pos];
                raw[buf_idx + 1] = 0;
            } else {
                raw[buf_idx] = 0xFF;
                raw[buf_idx + 1] = 0xFF;
            }
            buf_idx += 2;
        }

        buf_idx = 28;
        for (int c = 0; c < 2 && char_pos < 13; c++, char_pos++) {
            if (char_start + char_pos < name_len) {
                raw[buf_idx] = (uint8_t)name[char_start + char_pos];
                raw[buf_idx + 1] = 0;
            } else {
                raw[buf_idx] = 0xFF;
                raw[buf_idx + 1] = 0xFF;
            }
            buf_idx += 2;
        }

        if (!set_entry_at_in(entry_idx, &e, dir_cluster)) return 0;
    }
    return 1;
}

static int find_free_entries_in(uint16_t dir_cluster, int needed) {
    uint8_t buf[512];
    int max = dir_sector_count(dir_cluster);
    for (uint32_t s = 0; s < max; s++) {
        if (!read_dir_sector(dir_cluster, s, buf)) return -1;
        struct FAT16_DirEntry* entries = (struct FAT16_DirEntry*)buf;
        int run = 0;
        int run_start = -1;
        for (int i = 0; i < 16; i++) {
            if (entries[i].filename[0] == 0 || entries[i].filename[0] == (char)0xE5) {
                if (run == 0) run_start = (int)(s * 16 + i);
                run++;
                if (run >= needed) return run_start;
            } else {
                run = 0;
            }
        }
    }
    return -1;
}

extern volatile int ata_irq_flag;

int drive_wait() {
    if (ata_irq_flag) {
        while (!ata_irq_flag) { asm volatile("sti; hlt; cli"); }
        ata_irq_flag = 0;
        uint8_t st = inb(0x1F7);
        if (st & 0x01) return 0;
        if (st & 0x20) return 0;
        return 1;
    }
    int timeout = 0;
    uint8_t status;
    do {
        status = inb(0x1F7);
        if (status & 0x01) return 0;
        if (status & 0x20) return 0;
        if (++timeout > 100000) return 0;
    } while ((status & 0x80) || !(status & 0x08));
    return 1;
}

int disk_ready() {
    uint8_t status = inb(0x1F7);
    if (status == 0xFF || status == 0x00) return 0;
    return (status & 0x40) != 0;
}

static void ata_delay() {
    for (volatile int d = 0; d < 5000; d++);
}

int ata_init() {
    outb(0x3F6, 0x04);
    ata_delay();
    outb(0x3F6, 0x00);
    ata_delay();
    int timeout = 0;
    while ((inb(0x1F7) & 0x80) && timeout < 100000) timeout++;
    uint8_t st = inb(0x1F7);
    if (st == 0xFF) return 0;

    outb(0x1F6, 0xE0);
    outb(0x1F2, 0); outb(0x1F3, 0); outb(0x1F4, 0); outb(0x1F5, 0);
    ata_delay();
    outb(0x1F7, 0xEC);
    ata_delay();
    timeout = 0;
    while ((inb(0x1F7) & 0x80) && timeout < 100000) timeout++;
    st = inb(0x1F7);
    if (timeout >= 100000 || st == 0xFF || st == 0x00) return 0;
    if (!(st & 0x08)) return 0;
    uint16_t identify_buf[256];
    ata_delay();
    for (int i = 0; i < 256; i++) identify_buf[i] = inw(0x1F0);
    return 1;
}

int ata_read_sector(uint32_t lba, uint8_t* buffer) {
    if (!disk_ready()) return 0;
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, 1);
    outb(0x1F3, (uint8_t)lba);
    outb(0x1F4, (uint8_t)(lba >> 8));
    outb(0x1F5, (uint8_t)(lba >> 16));
    ata_irq_flag = 0;
    outb(0x1F7, 0x20);
    if (!drive_wait()) return 0;
    uint16_t* ptr = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++) ptr[i] = inw(0x1F0);
    return 1;
}

int ata_write_sector(uint32_t lba, const uint8_t* buffer) {
    if (!disk_ready()) return 0;
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, 1);
    outb(0x1F3, (uint8_t)lba);
    outb(0x1F4, (uint8_t)(lba >> 8));
    outb(0x1F5, (uint8_t)(lba >> 16));
    ata_irq_flag = 0;
    outb(0x1F7, 0x30);
    if (!drive_wait()) return 0;
    uint16_t* ptr = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        uint16_t data = ptr[i];
        asm volatile("outw %0, %1" : : "a"(data), "Nd"(0x1F0));
    }
    uint32_t timeout = 0;
    while ((inb(0x1F7) & 0x80) && timeout < 100000) timeout++;
    if (timeout >= 100000) return 0;
    io_wait();
    return 1;
}

static void format_name(char* in, char* out) {
    for (int i = 0; i < 8; i++) out[i] = ' ';
    for (int i = 0; i < 8 && in[i] != '\0' && in[i] != '.'; i++) out[i] = in[i];
}

static uint16_t fat_read_entry(uint16_t cluster) {
    uint8_t buf[512];
    uint32_t fat_sector = FAT_SECTOR + (cluster * 2 / 512);
    uint32_t offset = (cluster * 2) % 512;
    if (!ata_read_sector(fat_sector, buf)) return 0;
    return *(uint16_t*)(buf + offset);
}

static int fat_write_entry(uint16_t cluster, uint16_t value) {
    uint8_t buf[512];
    uint32_t fat_sector = FAT_SECTOR + (cluster * 2 / 512);
    uint32_t offset = (cluster * 2) % 512;

    if (!ata_read_sector(fat_sector, buf)) return 0;
    *(uint16_t*)(buf + offset) = value;
    if (!ata_write_sector(fat_sector, buf)) return 0;

    if (!ata_read_sector(fat_sector + FAT_SECTOR_COUNT, buf)) return 0;
    *(uint16_t*)(buf + offset) = value;
    if (!ata_write_sector(fat_sector + FAT_SECTOR_COUNT, buf)) return 0;

    return 1;
}

static uint16_t allocate_cluster() {
    for (uint16_t c = next_free_cluster; c < 20480; c++) {
        if (fat_read_entry(c) == FAT16_FREE) {
            if (!fat_write_entry(c, FAT16_EOF)) return 0;
            next_free_cluster = c + 1;
            return c;
        }
    }
    return 0;
}

static void free_cluster(uint16_t cluster) {
    if (cluster >= 2) {
        fat_write_entry(cluster, FAT16_FREE);
        if (cluster < next_free_cluster) next_free_cluster = cluster;
    }
}

static int read_dir_sector(uint16_t dir_cluster, uint32_t s, uint8_t* buf) {
    if (dir_cluster == 0) return ata_read_sector(ROOT_SECTOR + s, buf);
    uint16_t c = dir_cluster;
    for (uint32_t i = 0; i < s; i++) {
        c = fat_read_entry(c);
        if (c < 2 || c >= 0xFFF0) return 0;
    }
    return ata_read_sector(DATA_START_SECTOR + c - 2, buf);
}

static int dir_sector_count(uint16_t dir_cluster) {
    if (dir_cluster == 0) return ROOT_SECTOR_COUNT;
    int count = 0;
    uint16_t c = dir_cluster;
    while (c >= 2 && c < 0xFFF0) { count++; c = fat_read_entry(c); }
    return count;
}

static int find_file_in(char* name, uint16_t dir_cluster) {
    char target[8], target_ext[3];
    format_name(name, target);
    for (int i = 0; i < 3; i++) target_ext[i] = ' ';
    int dot = 0, ei = 0;
    for (int i = 0; name[i]; i++) {
        if (name[i] == '.') { dot = 1; continue; }
        if (dot && ei < 3) target_ext[ei++] = name[i];
    }
    uint8_t buf[512];
    int max = dir_sector_count(dir_cluster);
    for (uint32_t s = 0; s < max; s++) {
        if (!read_dir_sector(dir_cluster, s, buf)) return -1;
        struct FAT16_DirEntry* entries = (struct FAT16_DirEntry*)buf;
        for (int i = 0; i < 16; i++) {
            if (entries[i].filename[0] == 0) return -1;
            if (entries[i].filename[0] == (char)0xE5) continue;
            int match = 1;
            for (int j = 0; j < 8; j++) {
                if (entries[i].filename[j] != target[j]) { match = 0; break; }
            }
            if (match) {
                for (int j = 0; j < 3; j++) {
                    if (entries[i].extension[j] != target_ext[j]) { match = 0; break; }
                }
            }
            if (match) return (int)(s * 16 + i);
        }
    }
    return -1;
}

static int find_free_entry_in(uint16_t dir_cluster) {
    uint8_t buf[512];
    int max = dir_sector_count(dir_cluster);
    for (uint32_t s = 0; s < max; s++) {
        if (!read_dir_sector(dir_cluster, s, buf)) return -1;
        struct FAT16_DirEntry* entries = (struct FAT16_DirEntry*)buf;
        for (int i = 0; i < 16; i++) {
            if (entries[i].filename[0] == 0 || entries[i].filename[0] == (char)0xE5) {
                return (int)(s * 16 + i);
            }
        }
    }
    return -1;
}

static int set_entry_at_in(int index, struct FAT16_DirEntry* entry, uint16_t dir_cluster) {
    if (index < 0) return 0;
    uint32_t sector;
    if (dir_cluster == 0) {
        sector = ROOT_SECTOR + index / 16;
    } else {
        uint16_t c = dir_cluster;
        uint32_t s = index / 16;
        for (uint32_t i = 0; i < s; i++) {
            c = fat_read_entry(c);
            if (c < 2 || c >= 0xFFF0) return 0;
        }
        sector = DATA_START_SECTOR + c - 2;
    }
    int offset = index % 16;
    uint8_t buf[512];
    if (!ata_read_sector(sector, buf)) return 0;
    ((struct FAT16_DirEntry*)buf)[offset] = *entry;
    return ata_write_sector(sector, buf);
}

static int get_entry_at_in(int index, struct FAT16_DirEntry* out, uint16_t dir_cluster) {
    if (index < 0) return 0;
    uint32_t sector;
    if (dir_cluster == 0) {
        sector = ROOT_SECTOR + index / 16;
    } else {
        uint16_t c = dir_cluster;
        uint32_t s = index / 16;
        for (uint32_t i = 0; i < s; i++) {
            c = fat_read_entry(c);
            if (c < 2 || c >= 0xFFF0) return 0;
        }
        sector = DATA_START_SECTOR + c - 2;
    }
    int offset = index % 16;
    uint8_t buf[512];
    if (!ata_read_sector(sector, buf)) return 0;
    *out = ((struct FAT16_DirEntry*)buf)[offset];
    return 1;
}

static int find_file(char* name) { return find_file_in(name, current_dir_cluster); }
static int find_free_entry() { return find_free_entry_in(current_dir_cluster); }
static int set_entry_at(int index, struct FAT16_DirEntry* entry) { return set_entry_at_in(index, entry, current_dir_cluster); }
static int get_entry_at(int index, struct FAT16_DirEntry* out) { return get_entry_at_in(index, out, current_dir_cluster); }

void fat16_format_drive() {
    if (!disk_ready()) { print("Error: No disk detected.\n"); return; }

    uint8_t s0[512] = {0};

    s0[0] = 0xEB; s0[1] = 0xFE; s0[2] = 0x90;
    for (int i = 0; i < 8; i++) s0[3 + i] = "AARONOS "[i];

    *(uint16_t*)(s0 + 11) = 512;
    s0[13] = 1;
    *(uint16_t*)(s0 + 14) = 384;
    s0[16] = 2;
    *(uint16_t*)(s0 + 17) = 512;
    *(uint16_t*)(s0 + 19) = 20480;
    s0[21] = 0xF8;
    *(uint16_t*)(s0 + 22) = 80;
    *(uint16_t*)(s0 + 24) = 63;
    *(uint16_t*)(s0 + 26) = 16;
    *(uint32_t*)(s0 + 28) = 0;
    *(uint32_t*)(s0 + 32) = 0;
    s0[36] = 0x80;
    s0[37] = 0;
    s0[38] = 0x29;
    *(uint32_t*)(s0 + 39) = 0x20260525;
    for (int i = 0; i < 11; i++) s0[43 + i] = "AARONOS    "[i];
    for (int i = 0; i < 8; i++) s0[54 + i] = "FAT16   "[i];

    s0[510] = 0x55;
    s0[511] = 0xAA;

    if (!ata_write_sector(0, s0)) { print("Error: Cannot write BPB.\n"); return; }
    io_wait();

    uint8_t fat_buf[512] = {0};
    ((uint16_t*)fat_buf)[0] = 0xFFF8;
    ((uint16_t*)fat_buf)[1] = 0xFFFF;
    /* First sector of each FAT copy holds the reserved entries */
    if (!ata_write_sector(FAT_SECTOR, fat_buf)) return;
    io_wait();
    if (!ata_write_sector(FAT_SECTOR + FAT_SECTOR_COUNT, fat_buf)) return;
    io_wait();
    /* Remaining FAT sectors must be zero */
    fat_buf[0] = 0; fat_buf[1] = 0; fat_buf[2] = 0; fat_buf[3] = 0;
    for (uint32_t s = 1; s < FAT_SECTOR_COUNT; s++) {
        if (!ata_write_sector(FAT_SECTOR + s, fat_buf)) return;
        io_wait();
        if (!ata_write_sector(FAT_SECTOR + FAT_SECTOR_COUNT + s, fat_buf)) return;
        io_wait();
    }

    uint8_t empty[512] = {0};
    for (uint32_t s = 0; s < ROOT_SECTOR_COUNT; s++) {
        if (!ata_write_sector(ROOT_SECTOR + s, empty)) return;
        io_wait();
    }

    /* Verify root directory was zeroed */
    uint8_t verify[512];
    if (!ata_read_sector(ROOT_SECTOR, verify)) { print("Error: Cannot verify format.\n"); return; }
    for (int i = 0; i < 512; i++) {
        if (verify[i] != 0) { print("Error: Format verification failed.\n"); return; }
    }

    next_free_cluster = 2;
    current_dir_cluster = 0;
    current_path[0] = '\0';
    print("Format complete.\n");
}

void fat16_list_files() {
    if (!disk_ready()) { print("Error: No disk detected.\n"); return; }
    uint8_t buf[512];
    int found = 0;
    print("Files:\n");
    int max = dir_sector_count(current_dir_cluster);
    for (uint32_t s = 0; s < max; s++) {
        if (!read_dir_sector(current_dir_cluster, s, buf)) { print("Error: Cannot read disk.\n"); return; }
        struct FAT16_DirEntry* entries = (struct FAT16_DirEntry*)buf;
        for (int i = 0; i < 16; i++) {
            if (entries[i].filename[0] == 0) { if (found == 0) print("  (empty)\n"); return; }
            if (entries[i].filename[0] == (char)0xE5) continue;
            if (entries[i].attributes == LFN_ATTR) continue;
            if (current_dir_cluster != 0 && i < 2 && entries[i].filename[0] == '.') continue;
            found = 1;

            int global_idx = (int)(s * 16 + i);
            char long_name[256];
            int has_long = read_vfat_name(current_dir_cluster, global_idx, long_name, 256);

            uint8_t fg = (entries[i].attributes & ATTR_DIRECTORY) ? 0x0A : 0x07;
            uint8_t ro_fg = (entries[i].attributes & ATTR_READ_ONLY) ? 0x0E : fg;

            if (has_long) {
                print_col(long_name, ro_fg);
            } else {
                for (int j = 0; j < 8; j++) if (entries[i].filename[j] != ' ') putchar_col(entries[i].filename[j], ro_fg);
                if (entries[i].extension[0] != ' ') {
                    putchar_col('.', ro_fg);
                    for (int j = 0; j < 3; j++) if (entries[i].extension[j] != ' ') putchar_col(entries[i].extension[j], ro_fg);
                }
            }

            print("  ");
            if (entries[i].attributes & ATTR_DIRECTORY) print_col("<DIR>", 0x0A);
            if (entries[i].attributes & ATTR_READ_ONLY) print_col(" R", 0x0E);
            if (entries[i].attributes & ATTR_HIDDEN)    print_col(" H", 0x08);
            if (entries[i].attributes & ATTR_SYSTEM)    print_col(" S", 0x0C);
            if (entries[i].attributes & ATTR_ARCHIVE)   print_col(" A", 0x07);
            print("\n");
        }
    }
    if (!found) print("  (empty)\n");
}

void fat16_cat(char* name) {
    if (!disk_ready()) { print("Error: No disk detected.\n"); return; }
    int idx = find_file(name);
    if (idx < 0) { print("File not found.\n"); return; }
    struct FAT16_DirEntry entry;
    if (!get_entry_at(idx, &entry)) { print("Error: Cannot read file.\n"); return; }
    uint16_t cluster = entry.first_cluster_low;
    if (cluster < 2) { print("(empty)\n"); return; }

    while (cluster >= 2 && cluster < 0xFFF0) {
        uint32_t sector = DATA_START_SECTOR + cluster - 2;
        uint8_t data_buf[512];
        if (!ata_read_sector(sector, data_buf)) { print("Error: Cannot read file.\n"); return; }
        for (int i = 0; i < 512; i++) {
            if (data_buf[i] == 0) break;
            putchar_col(data_buf[i], 0x07);
        }
        cluster = fat_read_entry(cluster);
    }
    print("\n");
}

void fat16_write_file(char* name, char* content) {
    if (!disk_ready()) { print("Error: No disk detected.\n"); return; }

    int idx = find_file(name);
    struct FAT16_DirEntry entry = {0};

    if (idx >= 0) {
        if (!get_entry_at(idx, &entry)) { print("Error: Cannot read directory.\n"); return; }
        if (entry.attributes & ATTR_READ_ONLY) {
            print("Access denied: file is read-only.\n");
            return;
        }
        uint16_t old = entry.first_cluster_low;
        while (old >= 2 && old < 0xFFF0) {
            uint16_t next = fat_read_entry(old);
            free_cluster(old);
            old = next;
        }
        entry.first_cluster_low = 0;
    } else {
        idx = find_free_entry();
        if (idx < 0) { print("Error: Directory full.\n"); return; }
        char target[8];
        format_name(name, target);
        for (int j = 0; j < 8; j++) entry.filename[j] = target[j];
        for (int j = 0; j < 3; j++) entry.extension[j] = "TXT"[j];
        entry.attributes = 0;
    }

    uint16_t cluster = allocate_cluster();
    if (cluster == 0) { print("Error: Disk full.\n"); return; }
    entry.first_cluster_low = cluster;

    uint8_t data_buf[512] = {0};
    int len = 0;
    for (; content[len] && len < 511; len++) data_buf[len] = content[len];
    if (!ata_write_sector(DATA_START_SECTOR + cluster - 2, data_buf)) { print("Error: Cannot write data.\n"); return; }

    entry.file_size = len;
    if (!set_entry_at(idx, &entry)) { print("Error: Cannot write directory.\n"); return; }
    print("Saved.\n");
}

static void parse_name_8_3(char* name, char* out_base, char* out_ext) {
    for (int i = 0; i < 8; i++) out_base[i] = ' ';
    for (int i = 0; i < 3; i++) out_ext[i] = ' ';
    int dot = 0, bi = 0, ei = 0;
    for (int i = 0; name[i]; i++) {
        if (name[i] == '.') { dot = 1; continue; }
        if (!dot) { if (bi < 8) out_base[bi++] = name[i]; }
        else { if (ei < 3) out_ext[ei++] = name[i]; }
    }
}

void fat16_write_binary(char* name, const uint8_t* data, uint32_t len) {
    if (!disk_ready()) { print("Error: No disk detected.\n"); return; }

    int idx = find_file(name);
    struct FAT16_DirEntry entry = {0};

    if (idx >= 0) {
        if (!get_entry_at(idx, &entry)) { print("Error: Cannot read directory.\n"); return; }
        if (entry.attributes & ATTR_READ_ONLY) {
            print("Access denied: file is read-only.\n");
            return;
        }
        uint16_t old = entry.first_cluster_low;
        while (old >= 2 && old < 0xFFF0) {
            uint16_t next = fat_read_entry(old);
            free_cluster(old);
            old = next;
        }
        entry.first_cluster_low = 0;
    } else {
        idx = find_free_entry();
        if (idx < 0) { print("Error: Directory full.\n"); return; }
        char base[8], ext[3];
        parse_name_8_3(name, base, ext);
        for (int j = 0; j < 8; j++) entry.filename[j] = base[j];
        for (int j = 0; j < 3; j++) entry.extension[j] = ext[j];
        entry.attributes = 0;
    }

    if (len == 0) { entry.file_size = 0; set_entry_at(idx, &entry); return; }

    uint32_t sectors_needed = (len + 511) / 512;
    uint16_t first = 0, prev = 0;
    for (uint32_t s = 0; s < sectors_needed; s++) {
        uint16_t c = allocate_cluster();
        if (c == 0) { print("Error: Disk full.\n"); return; }
        if (first == 0) first = c;
        if (prev != 0) fat_write_entry(prev, c);
        prev = c;

        uint8_t buf[512];
        for (int i = 0; i < 512; i++) {
            uint32_t off = s * 512 + i;
            buf[i] = (off < len) ? data[off] : 0;
        }
        if (!ata_write_sector(DATA_START_SECTOR + c - 2, buf)) { print("Error: Write failed.\n"); return; }
    }
    if (prev != 0) fat_write_entry(prev, FAT16_EOF);

    entry.first_cluster_low = first;
    entry.file_size = len;
    if (!set_entry_at(idx, &entry)) { print("Error: Cannot write directory.\n"); return; }
    print("Written.\n");
}

void fat16_create_file(char* name) {
    if (!disk_ready()) { print("Error: No disk detected.\n"); return; }
    if (find_file(name) >= 0) { print("File already exists.\n"); return; }

    struct FAT16_DirEntry entry = {0};
    char sfn_base[8], sfn_ext[3];

    if (is_short_name(name)) {
        char target[8];
        format_name(name, target);
        for (int j = 0; j < 8; j++) entry.filename[j] = target[j];
        for (int j = 0; j < 3; j++) entry.extension[j] = "TXT"[j];
        entry.attributes = 0;

        int idx = find_free_entry();
        if (idx < 0) { print("Error: Directory full.\n"); return; }

        uint16_t cluster = allocate_cluster();
        if (cluster == 0) { print("Error: Disk full.\n"); return; }
        entry.first_cluster_low = cluster;

        uint8_t empty[512] = {0};
        ata_write_sector(DATA_START_SECTOR + cluster - 2, empty);

        if (!set_entry_at(idx, &entry)) { print("Error: Cannot write disk.\n"); return; }
    } else {
        generate_sfn(name, sfn_base, sfn_ext);

        int tilde = 1;
        char test_name[13];
        while (tilde < 100) {
            int ok = 1;
            /* Build test SFN string */
            char test_sfn[13];
            int ti = 0;
            for (int j = 0; j < 8 && sfn_base[j] != ' ' && ti < 12; j++) test_sfn[ti++] = sfn_base[j];
            /* Find the tilde position and replace number */
            int tp = 0;
            while (tp < 8 && sfn_base[tp] != '~') tp++;
            if (tp < 8) {
                test_sfn[tp] = '~';
                if (tilde < 10) { test_sfn[tp+1] = '0' + tilde; test_sfn[tp+2] = '\0'; }
                else { test_sfn[tp+1] = '0' + tilde/10; test_sfn[tp+2] = '0' + tilde%10; test_sfn[tp+3] = '\0'; }
            } else {
                test_sfn[ti] = '\0';
            }
            test_sfn[12] = '\0';

            /* Check collision against current directory */
            uint8_t buf[512];
            int max_sec = dir_sector_count(current_dir_cluster);
            for (uint32_t s = 0; s < max_sec && ok; s++) {
                if (!read_dir_sector(current_dir_cluster, s, buf)) break;
                struct FAT16_DirEntry* de = (struct FAT16_DirEntry*)buf;
                for (int i = 0; i < 16 && ok; i++) {
                    if (de[i].filename[0] == 0 || de[i].filename[0] == (char)0xE5) continue;
                    if (de[i].attributes == LFN_ATTR) continue;
                    int match = 1;
                    for (int j = 0; j < 8; j++)
                        if (de[i].filename[j] != (test_sfn[j] ? test_sfn[j] : ' ')) { match = 0; break; }
                    if (match) {
                        /* Check extension too for full uniqueness */
                        char test_ext[3];
                        for (int j = 0; j < 3; j++) test_ext[j] = sfn_ext[j];
                        for (int j = 0; j < 3; j++)
                            if (de[i].extension[j] != test_ext[j]) { match = 0; break; }
                    }
                    if (match) ok = 0;
                }
            }
            if (ok) {
                /* Found unique SFN */
                for (int j = 0; j < 8; j++) {
                    if (j < tp) entry.filename[j] = sfn_base[j];
                    else if (j == tp) entry.filename[j] = '~';
                    else if (j == tp + 1) entry.filename[j] = '0' + (tilde / 10 ? (tilde/10) : tilde);
                    else if (tilde >= 10 && j == tp + 2) entry.filename[j] = '0' + (tilde % 10);
                    else entry.filename[j] = ' ';
                }
                for (int j = 0; j < 3; j++) entry.extension[j] = sfn_ext[j];
                break;
            }
            tilde++;
        }
        if (tilde >= 100) { print("Error: Cannot generate unique short name.\n"); return; }

        /* Calculate VFAT checksum on the final SFN */
        char sfn_11[11];
        for (int j = 0; j < 8; j++) sfn_11[j] = entry.filename[j];
        for (int j = 0; j < 3; j++) sfn_11[8 + j] = entry.extension[j];
        uint8_t checksum = vfat_checksum(sfn_11);

        entry.attributes = 0;

        int name_len = kstrlen_local(name);
        int lfn_count = (name_len + 12) / 13;
        int needed = lfn_count + 1;

        int idx = find_free_entries_in(current_dir_cluster, needed);
        if (idx < 0) { print("Error: Directory full (need contiguous entries for LFN).\n"); return; }

        uint16_t cluster = allocate_cluster();
        if (cluster == 0) { print("Error: Disk full.\n"); return; }
        entry.first_cluster_low = cluster;

        uint8_t empty[512] = {0};
        ata_write_sector(DATA_START_SECTOR + cluster - 2, empty);

        if (!write_lfn_entries(idx, name, checksum, lfn_count, current_dir_cluster)) {
            print("Error: Cannot write LFN entries.\n");
            free_cluster(cluster);
            return;
        }

        if (!set_entry_at_in(idx + lfn_count, &entry, current_dir_cluster)) {
            print("Error: Cannot write disk.\n");
            free_cluster(cluster);
            return;
        }
    }
    print("File created.\n");
}

void fat16_delete_file(char* name) {
    if (!disk_ready()) { print("Error: No disk detected.\n"); return; }
    int idx = find_file(name);
    if (idx < 0) { print("File not found.\n"); return; }

    struct FAT16_DirEntry entry;
    get_entry_at(idx, &entry);

    if (entry.attributes & ATTR_READ_ONLY) {
        print("Access denied: file is read-only.\n");
        return;
    }

    uint16_t cluster = entry.first_cluster_low;
    while (cluster >= 2 && cluster < 0xFFF0) {
        uint16_t next = fat_read_entry(cluster);
        free_cluster(cluster);
        cluster = next;
    }

    entry.filename[0] = (char)0xE5;
    set_entry_at(idx, &entry);
    print("File deleted.\n");
}

void fat16_rename_file(char* old, char* newn) {
    if (!disk_ready()) { print("Error: No disk detected.\n"); return; }
    int idx = find_file(old);
    if (idx < 0) { print("File not found.\n"); return; }

    struct FAT16_DirEntry entry;
    get_entry_at(idx, &entry);
    char target[8];
    format_name(newn, target);
    for (int j = 0; j < 8; j++) entry.filename[j] = target[j];
    if (!set_entry_at(idx, &entry)) { print("Error: Cannot write disk.\n"); return; }
    print("File renamed.\n");
}

void fat16_copy_file(char* src, char* dst) {
    if (!disk_ready()) { print("Error: No disk detected.\n"); return; }
    int src_idx = find_file(src);
    if (src_idx < 0) { print("Source not found.\n"); return; }
    if (find_file(dst) >= 0) { print("Destination already exists.\n"); return; }

    struct FAT16_DirEntry src_entry;
    if (!get_entry_at(src_idx, &src_entry)) { print("Error: Cannot read source.\n"); return; }

    int dst_idx = find_free_entry();
    if (dst_idx < 0) { print("Error: Directory full.\n"); return; }

    struct FAT16_DirEntry dst_entry;
    char target[8];
    format_name(dst, target);
    for (int j = 0; j < 8; j++) dst_entry.filename[j] = target[j];
    for (int j = 0; j < 3; j++) dst_entry.extension[j] = src_entry.extension[j];
    dst_entry.attributes = src_entry.attributes;
    dst_entry.file_size = src_entry.file_size;
    dst_entry.first_cluster_low = 0;

    uint16_t src_cluster = src_entry.first_cluster_low;
    uint16_t prev_dst = 0;
    uint16_t first_dst = 0;
    uint8_t buf[512];

    while (src_cluster >= 2 && src_cluster < 0xFFF0) {
        uint16_t dst_cluster = allocate_cluster();
        if (dst_cluster == 0) {
            uint16_t c = first_dst;
            while (c >= 2 && c < 0xFFF0) {
                uint16_t n = fat_read_entry(c);
                free_cluster(c);
                c = n;
            }
            print("Error: Disk full.\n");
            return;
        }
        if (first_dst == 0) first_dst = dst_cluster;
        if (prev_dst != 0) fat_write_entry(prev_dst, dst_cluster);
        prev_dst = dst_cluster;

        if (!ata_read_sector(DATA_START_SECTOR + src_cluster - 2, buf)) {
            print("Error: Read failed.\n");
            return;
        }
        if (!ata_write_sector(DATA_START_SECTOR + dst_cluster - 2, buf)) {
            print("Error: Write failed.\n");
            return;
        }

        src_cluster = fat_read_entry(src_cluster);
    }
    if (prev_dst != 0) fat_write_entry(prev_dst, FAT16_EOF);

    dst_entry.first_cluster_low = first_dst;
    if (!set_entry_at(dst_idx, &dst_entry)) { print("Error: Cannot write directory.\n"); return; }
    print("File copied.\n");
}

void fat16_move_file(char* src, char* dst) {
    fat16_rename_file(src, dst);
}

void fat16_mkdir(char* name) {
    if (!disk_ready()) { print("Error: No disk detected.\n"); return; }
    if (find_file(name) >= 0) { print("Already exists.\n"); return; }

    int idx = find_free_entry();
    if (idx < 0) { print("Error: Directory full.\n"); return; }

    struct FAT16_DirEntry entry = {0};
    char target[8];
    format_name(name, target);
    for (int j = 0; j < 8; j++) entry.filename[j] = target[j];
    for (int j = 0; j < 3; j++) entry.extension[j] = ' ';
    entry.attributes = 0x10;

    uint16_t cluster = allocate_cluster();
    if (cluster == 0) { print("Error: Disk full.\n"); return; }
    entry.first_cluster_low = cluster;

    uint8_t data[512] = {0};
    struct FAT16_DirEntry* de = (struct FAT16_DirEntry*)data;

    de[0].filename[0] = '.';
    for (int j = 1; j < 8; j++) de[0].filename[j] = ' ';
    for (int j = 0; j < 3; j++) de[0].extension[j] = ' ';
    de[0].attributes = 0x10;
    de[0].first_cluster_low = cluster;

    de[1].filename[0] = '.';
    de[1].filename[1] = '.';
    for (int j = 2; j < 8; j++) de[1].filename[j] = ' ';
    for (int j = 0; j < 3; j++) de[1].extension[j] = ' ';
    de[1].attributes = 0x10;
    de[1].first_cluster_low = current_dir_cluster;

    if (!ata_write_sector(DATA_START_SECTOR + cluster - 2, data)) {
        free_cluster(cluster);
        print("Error: Cannot write directory data.\n");
        return;
    }

    if (!set_entry_at(idx, &entry)) {
        free_cluster(cluster);
        uint8_t empty[512] = {0};
        ata_write_sector(DATA_START_SECTOR + cluster - 2, empty);
        print("Error: Cannot write directory entry.\n");
        return;
    }
    print("Directory created.\n");
}

void fat16_rmdir(char* name) {
    if (!disk_ready()) { print("Error: No disk detected.\n"); return; }
    int idx = find_file(name);
    if (idx < 0) { print("Not found.\n"); return; }

    struct FAT16_DirEntry entry;
    get_entry_at(idx, &entry);

    if (!(entry.attributes & 0x10)) { print("Not a directory.\n"); return; }

    uint16_t cluster = entry.first_cluster_low;
    if (cluster >= 2) {
        uint8_t buf[512];
        if (!ata_read_sector(DATA_START_SECTOR + cluster - 2, buf)) {
            print("Error: Cannot read directory.\n");
            return;
        }
        struct FAT16_DirEntry* de = (struct FAT16_DirEntry*)buf;
        for (int i = 2; i < 16; i++) {
            if (de[i].filename[0] == 0) break;
            if (de[i].filename[0] != (char)0xE5) {
                print("Directory not empty.\n");
                return;
            }
        }
    }

    while (cluster >= 2 && cluster < 0xFFF0) {
        uint16_t next = fat_read_entry(cluster);
        free_cluster(cluster);
        cluster = next;
    }

    entry.filename[0] = (char)0xE5;
    set_entry_at(idx, &entry);
    print("Directory removed.\n");
}

int fat16_read_file(char* name, char* buffer, int max_len) {
    if (!disk_ready()) return -1;
    int idx = find_file(name);
    if (idx < 0) return -1;
    struct FAT16_DirEntry entry;
    if (!get_entry_at(idx, &entry)) return -1;
    uint32_t remaining = entry.file_size;
    if ((int)remaining > max_len - 1) remaining = max_len - 1;
    uint16_t cluster = entry.first_cluster_low;
    int total = 0;
    while (cluster >= 2 && cluster < 0xFFF0 && total < (int)remaining) {
        uint32_t sector = DATA_START_SECTOR + cluster - 2;
        uint8_t data_buf[512];
        if (!ata_read_sector(sector, data_buf)) break;
        for (int i = 0; i < 512 && total < (int)remaining; i++) {
            buffer[total++] = data_buf[i];
        }
        cluster = fat_read_entry(cluster);
    }
    buffer[total] = '\0';
    return total;
}

uint32_t fat16_file_size(char* name) {
    if (!disk_ready()) return 0;
    int idx = find_file(name);
    if (idx < 0) return 0;
    struct FAT16_DirEntry entry;
    if (!get_entry_at(idx, &entry)) return 0;
    return entry.file_size;
}

int fat16_collect_entries(fat16_entry_t* entries, int max) {
    if (!disk_ready()) return 0;
    uint8_t buf[512];
    int count = 0;
    int max_sec = dir_sector_count(current_dir_cluster);
    for (uint32_t s = 0; s < max_sec; s++) {
        if (!read_dir_sector(current_dir_cluster, s, buf)) return count;
        struct FAT16_DirEntry* de = (struct FAT16_DirEntry*)buf;
        for (int i = 0; i < 16; i++) {
            if (de[i].filename[0] == 0) return count;
            if (de[i].filename[0] == (char)0xE5) continue;
            if (de[i].attributes == LFN_ATTR) continue;
            if (current_dir_cluster != 0 && i < 2 && de[i].filename[0] == '.') continue;
            if (count >= max) return count;
            int ni = 0;
            int global_idx = (int)(s * 16 + i);
            char long_name[256];
            if (read_vfat_name(current_dir_cluster, global_idx, long_name, 256)) {
                int li;
                for (li = 0; long_name[li] && li < 255; li++)
                    entries[count].name[li] = long_name[li];
                entries[count].name[li] = '\0';
            } else {
                for (int j = 0; j < 8 && de[i].filename[j] != ' '; j++) entries[count].name[ni++] = de[i].filename[j];
                int has_ext = 0;
                for (int j = 0; j < 3; j++) if (de[i].extension[j] != ' ') has_ext = 1;
                if (has_ext) {
                    entries[count].name[ni++] = '.';
                    for (int j = 0; j < 3 && de[i].extension[j] != ' '; j++) entries[count].name[ni++] = de[i].extension[j];
                }
                entries[count].name[ni] = '\0';
            }
            entries[count].size = de[i].file_size;
            entries[count].attrs = de[i].attributes;
            count++;
        }
    }
    return count;
}

int fat16_collect_display_names(char (*names)[13], int max) {
    if (!disk_ready()) return 0;
    uint8_t buf[512];
    int count = 0;
    int max_sec = dir_sector_count(current_dir_cluster);
    for (uint32_t s = 0; s < max_sec; s++) {
        if (!read_dir_sector(current_dir_cluster, s, buf)) return count;
        struct FAT16_DirEntry* entries = (struct FAT16_DirEntry*)buf;
        for (int i = 0; i < 16; i++) {
            if (entries[i].filename[0] == 0) return count;
            if (entries[i].filename[0] == (char)0xE5) continue;
            if (current_dir_cluster != 0 && i < 2 && entries[i].filename[0] == '.') continue;
            if (count >= max) return count;

            int ni = 0;
            for (int j = 0; j < 8 && entries[i].filename[j] != ' '; j++) names[count][ni++] = entries[i].filename[j];
            int has_ext = 0;
            for (int j = 0; j < 3; j++) if (entries[i].extension[j] != ' ') has_ext = 1;
            if (has_ext) {
                names[count][ni++] = '.';
                for (int j = 0; j < 3 && entries[i].extension[j] != ' '; j++) names[count][ni++] = entries[i].extension[j];
            }
            names[count][ni] = '\0';
            if (entries[i].attributes & 0x10) names[count][ni] = '/';
            count++;
        }
    }
    return count;
}

uint16_t fat16_get_cwd_cluster() {
    return current_dir_cluster;
}

void fat16_get_cwd(char* buf, int max) {
    int i;
    for (i = 0; current_path[i] && i < max - 1; i++) buf[i] = current_path[i];
    buf[i] = '\0';
}

void fat16_attrib(char* args) {
    if (!disk_ready()) { print("Error: No disk detected.\n"); return; }
    uint8_t set_mask = 0, clear_mask = 0;
    char* name = args;
    while (*name == ' ' || *name == '\t') name++;
    while (*name == '+' || *name == '-') {
        char op = *name++;
        uint8_t flag = 0;
        switch (*name) {
            case 'R': case 'r': flag = ATTR_READ_ONLY; break;
            case 'H': case 'h': flag = ATTR_HIDDEN; break;
            case 'S': case 's': flag = ATTR_SYSTEM; break;
            case 'A': case 'a': flag = ATTR_ARCHIVE; break;
            default: print("Unknown flag: "); putchar_col(*name, 0x07); print("\nUsage: attrib [+-RHS] file\n"); return;
        }
        name++;
        if (op == '+') set_mask |= flag;
        else clear_mask |= flag;
    }
    while (*name == ' ' || *name == '\t') name++;
    if (!*name) { print("Usage: attrib [+-RHS][+-RHS...] filename\n"); return; }

    int idx = find_file(name);
    if (idx < 0) {
        char long_match[256];
        uint8_t buf[512];
        int max_sec = dir_sector_count(current_dir_cluster);
        for (uint32_t s = 0; s < max_sec && idx < 0; s++) {
            if (!read_dir_sector(current_dir_cluster, s, buf)) break;
            struct FAT16_DirEntry* de = (struct FAT16_DirEntry*)buf;
            for (int i = 0; i < 16 && idx < 0; i++) {
                if (de[i].filename[0] == 0 || de[i].filename[0] == (char)0xE5) continue;
                if (de[i].attributes == LFN_ATTR) continue;
                if (read_vfat_name(current_dir_cluster, (int)(s * 16 + i), long_match, 256)) {
                    if (kstrcmp(name, long_match) == 0) idx = (int)(s * 16 + i);
                }
            }
        }
    }

    if (idx < 0) { print("File not found.\n"); return; }

    struct FAT16_DirEntry entry;
    get_entry_at(idx, &entry);

    if (set_mask == 0 && clear_mask == 0) {
        print("Attributes for: ");
        for (int j = 0; j < 8 && entry.filename[j] != ' '; j++) putchar_col(entry.filename[j], 0x07);
        if (entry.extension[0] != ' ') {
            putchar_col('.', 0x07);
            for (int j = 0; j < 3 && entry.extension[j] != ' '; j++) putchar_col(entry.extension[j], 0x07);
        }
        print("\n");
        print("  Read-only:  "); print((entry.attributes & ATTR_READ_ONLY) ? "Yes\n" : "No\n");
        print("  Hidden:     "); print((entry.attributes & ATTR_HIDDEN) ? "Yes\n" : "No\n");
        print("  System:     "); print((entry.attributes & ATTR_SYSTEM) ? "Yes\n" : "No\n");
        print("  Archive:    "); print((entry.attributes & ATTR_ARCHIVE) ? "Yes\n" : "No\n");
        return;
    }

    entry.attributes = (entry.attributes & ~clear_mask) | set_mask;
    if (!set_entry_at(idx, &entry)) { print("Error: Cannot update attributes.\n"); return; }
    print("Attributes updated.\n");
}

void fat16_cd(char* name) {
    if (!disk_ready()) { print("Error: No disk detected.\n"); return; }
    if (name[0] == '\0' || (name[0] == '/' && name[1] == '\0')) {
        current_dir_cluster = 0;
        current_path[0] = '\0';
        return;
    }
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
        if (current_dir_cluster == 0) { return; }
        uint8_t buf[512];
        if (!ata_read_sector(DATA_START_SECTOR + current_dir_cluster - 2, buf)) { print("Error: Cannot read directory.\n"); return; }
        struct FAT16_DirEntry* de = (struct FAT16_DirEntry*)buf;
        current_dir_cluster = de[1].first_cluster_low;
        int i;
        for (i = 127; i >= 0; i--) {
            if (current_path[i] == '/') { current_path[i] = '\0'; break; }
        }
        if (i <= 0) current_path[0] = '\0';
        return;
    }
    int idx = find_file_in(name, current_dir_cluster);
    if (idx < 0) { print("Not found.\n"); return; }
    struct FAT16_DirEntry entry;
    if (!get_entry_at_in(idx, &entry, current_dir_cluster)) { print("Error: Cannot read entry.\n"); return; }
    if (!(entry.attributes & 0x10)) { print("Not a directory.\n"); return; }
    current_dir_cluster = entry.first_cluster_low;
    int plen = 0;
    while (current_path[plen]) plen++;
    if (plen > 0 || (plen == 0 && name[0] != '/')) current_path[plen++] = '/';
    for (int i = 0; name[i] && plen < 127; i++) current_path[plen++] = name[i];
    current_path[plen] = '\0';
}
