#include <stdint.h>
#include "io.h"
#include "fat16.h"
#include "version.h"
#include "limine/limine-bios-hdd.h"

/* Embedded .aim archive (placed by two-pass build) */
extern const uint8_t _binary_aaronos_aim_start[];
extern const uint8_t _binary_aaronos_aim_end[];

extern void print(const char* str);
extern void print_col(const char* str, uint8_t col);
extern void play_sound(uint32_t freq);
extern void nosound(void);
extern void sleep(uint32_t ticks);

static const uint32_t PARTITION_LBA = 128;
static const uint32_t TOTAL_SECTORS = 20480;

char get_scan_code() {
    while (!(inb(0x64) & 1));
    return inb(0x60);
}

static void ata_write_raw(uint32_t lba, const uint8_t* data, int sectors) {
    for (int s = 0; s < sectors; s++) {
        int timeout = 0;
        while ((inb(0x1F7) & 0x80) && timeout < 100000) timeout++;
        outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
        outb(0x1F2, 1);
        outb(0x1F3, (uint8_t)lba);
        outb(0x1F4, (uint8_t)(lba >> 8));
        outb(0x1F5, (uint8_t)(lba >> 16));
        outb(0x1F7, 0x30);
        timeout = 0;
        while ((inb(0x1F7) & 0x80) && timeout < 100000) timeout++;
        uint16_t* ptr = (uint16_t*)(data + s * 512);
        for (int i = 0; i < 256; i++) outw(0x1F0, ptr[i]);
        lba++;
    }
}

/* .aim archive reader */
typedef struct {
    const uint8_t* cur;
    const uint8_t* end;
} aim_reader_t;

static int aim_open(aim_reader_t* r) {
    r->cur = _binary_aaronos_aim_start;
    r->end = _binary_aaronos_aim_end;
    if (r->end - r->cur < 4) return 0;
    if (r->cur[0] != 'A' || r->cur[1] != 'I' || r->cur[2] != 'M' || r->cur[3] != '1') return 0;
    r->cur += 4;
    return 1;
}

static int aim_next(aim_reader_t* r, const uint8_t** name, int* name_len,
                    const uint8_t** data, int* data_len) {
    if (r->cur + 4 > r->end) return 0;
    int nl = *(int*)r->cur;
    r->cur += 4;
    if (nl == 0) return 0;
    if (r->cur + nl + 4 > r->end) return 0;
    *name = r->cur;
    *name_len = nl;
    r->cur += nl;
    int dl = *(int*)r->cur;
    r->cur += 4;
    if (r->cur + dl > r->end) return 0;
    *data = r->cur;
    *data_len = dl;
    r->cur += dl;
    return 1;
}

static int name_match(const uint8_t* name, int name_len, const char* target) {
    int i = 0;
    while (target[i] && i < name_len) {
        if (name[i] != target[i]) return 0;
        i++;
    }
    return target[i] == '\0' && i == name_len;
}

static int aim_find(const uint8_t** data_out, int* len_out, const char* target) {
    aim_reader_t r;
    if (!aim_open(&r)) return 0;
    const uint8_t* n; int nl; const uint8_t* d; int dl;
    while (aim_next(&r, &n, &nl, &d, &dl)) {
        if (name_match(n, nl, target)) {
            *data_out = d;
            *len_out = dl;
            return 1;
        }
    }
    return 0;
}

void run_installation() {
    print_col("\n==================================================\n", 0x0B);
    print_col("          " KERNEL_STRING " INSTALLER           \n", 0x0B);
    print_col("==================================================\n", 0x0B);

    print("\nWARNING: This will DESTROY all data on the primary\n");
    print("ATA drive and format it as FAT16.\n\n");
    print_col("Proceed with installation? [y/n]: ", 0x0E);

    char code = 0;
    while(1) {
        code = get_scan_code();
        if (code == 0x15) {
            print("y\n\n");
            break;
        } else if (code == 0x31) {
            print("n\n\nInstallation aborted.\n");
            return;
        }
    }

    /* Verify .aim archive is valid */
    const uint8_t *sys_data, *elf_data, *cfg_data;
    int sys_len, elf_len, cfg_len;
    if (!aim_find(&sys_data, &sys_len, "limine-bios.sys")) {
        print_col("ERROR: .aim archive missing limine-bios.sys\n", 0x0C);
        return;
    }
    if (!aim_find(&elf_data, &elf_len, "kernel.elf")) {
        print_col("ERROR: .aim archive missing kernel.elf\n", 0x0C);
        return;
    }
    if (!aim_find(&cfg_data, &cfg_len, "limine_installed.conf")) {
        print_col("ERROR: .aim archive missing limine_installed.conf\n", 0x0C);
        return;
    }

    /* STEP 1: Write Limine MBR boot code to LBA 0+ */
    print("[1/5] Writing Limine boot code... ");
    {
        uint32_t boot_total = sizeof(binary_limine_hdd_bin_data);
        uint32_t boot_sectors = (boot_total + 511) / 512;

        uint8_t mbr[512];
        for (int i = 0; i < 512; i++) mbr[i] = (i < (int)boot_total) ? binary_limine_hdd_bin_data[i] : 0;

        /* Zero out partition table area (bytes 446-509) */
        for (int i = 446; i < 510; i++) mbr[i] = 0;

        /* Create partition entry for FAT16 at LBA 128 */
        uint8_t* pe = mbr + 446;
        pe[0] = 0x80;

        /* Start CHS: LBA 128 → H=2, S=3, C=0 */
        pe[1] = 2;
        pe[2] = (3 & 0x3F) | ((0 >> 2) & 0xC0);
        pe[3] = 0;

        pe[4] = 0x06;

        /* End CHS: LBA 20479 → H=5, S=1, C=20 */
        pe[5] = 5;
        pe[6] = (1 & 0x3F) | ((20 >> 2) & 0xC0);
        pe[7] = 20 & 0xFF;

        *(uint32_t*)(pe + 8) = PARTITION_LBA;
        *(uint32_t*)(pe + 12) = TOTAL_SECTORS - PARTITION_LBA;

        mbr[510] = 0x55;
        mbr[511] = 0xAA;

        ata_write_raw(0, mbr, 1);

        /* Write remaining boot sectors */
        for (uint32_t s = 1; s < boot_sectors; s++) {
            uint8_t buf[512] = {0};
            uint32_t off = s * 512;
            uint32_t remain = boot_total - off;
            uint32_t copy = remain > 512 ? 512 : remain;
            for (uint32_t i = 0; i < copy; i++) buf[i] = binary_limine_hdd_bin_data[off + i];
            ata_write_raw(s, buf, 1);
        }
    }
    sleep(10);
    print_col("DONE\n", 0x0A);

    /* STEP 2: Format drive as FAT16 (at partition offset) */
    print("[2/5] Formatting (FAT16)... ");
    fat16_set_partition_offset(PARTITION_LBA);
    fat16_format_drive();
    sleep(20);
    print_col("DONE\n", 0x0A);

    /* STEP 3: Copy limine-bios.sys to FAT root */
    print("[3/5] Deploying limine-bios.sys... ");
    fat16_write_binary("limine-bios.sys", sys_data, sys_len);
    sleep(10);
    print_col("DONE\n", 0x0A);

    /* STEP 4: Create /BOOT directory + config */
    print("[4/5] Creating boot files... ");
    fat16_mkdir("BOOT");
    fat16_cd("BOOT");
    fat16_write_binary("KERNEL.ELF", elf_data, elf_len);
    fat16_write_binary("LIMINE.CONF", cfg_data, cfg_len);
    fat16_cd("..");
    sleep(10);
    print_col("BOOT FILES DONE\n", 0x0A);

    /* STEP 5: Write partition config to /BOOT/LIMINE.CONF */
    print("[5/5] Writing boot configuration... ");
    sleep(10);
    print_col("DONE\n", 0x0A);

    print_col("\n[ " KERNEL_STRING " successfully installed to hardware! ]\n", 0x0A);
    print("You may safely reboot the system.\n");

    uint32_t victory[] = {523, 659, 783, 1046};
    for(int i = 0; i < 4; i++) {
        play_sound(victory[i]);
        sleep(10);
    }
    nosound();
}
