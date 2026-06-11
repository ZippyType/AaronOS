
#include <stdint.h>
#include "io.h"
#include "fat16.h"
#include "version.h"

extern void print(const char* str);
extern void print_col(const char* str, uint8_t col);
extern void play_sound(uint32_t freq);
extern void nosound(void);
extern void sleep(uint32_t ticks);
extern volatile int ata_irq_flag;

extern const uint8_t embedded_kernel[];
extern const uint32_t embedded_kernel_len;
extern const uint8_t grub_boot_img[];
extern const uint32_t grub_boot_img_len;
extern const uint8_t grub_core_img[];
extern const uint32_t grub_core_img_len;

char get_scan_code() {
    while (!(inb(0x64) & 1));
    return inb(0x60);
}

static void ata_write_lba(uint32_t lba, const uint8_t* data, int sectors) {
    for (int s = 0; s < sectors; s++) {
        int timeout = 0;
        while ((inb(0x1F7) & 0x80) && timeout < 100000) timeout++;
        outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
        outb(0x1F2, 1);
        outb(0x1F3, (uint8_t)lba);
        outb(0x1F4, (uint8_t)(lba >> 8));
        outb(0x1F5, (uint8_t)(lba >> 16));
        ata_irq_flag = 0;
        outb(0x1F7, 0x30);
        if (ata_irq_flag) {
            while (!ata_irq_flag) { asm volatile("sti; hlt; cli"); }
            ata_irq_flag = 0;
            inb(0x1F7);
        } else {
            while ((inb(0x1F7) & 0x80) || !(inb(0x1F7) & 0x08));
        }
        uint16_t* ptr = (uint16_t*)(data + s * 512);
        for (int i = 0; i < 256; i++) { outw(0x1F0, ptr[i]); }
        lba++;
    }
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

    /* STEP 1: Format Drive (creates FAT16 with 128 reserved sectors) */
    print("[1/5] Formatting (FAT16)... ");
    fat16_format_drive();
    sleep(20);
    print_col("DONE\n", 0x0A);

    /* STEP 2: Write GRUB boot.img to LBA 0 */
    print("[2/5] Writing GRUB boot sector... ");
    uint8_t mbr[512];
    for (int i = 0; i < 512; i++) mbr[i] = (i < (int)grub_boot_img_len) ? grub_boot_img[i] : 0;
    mbr[510] = 0x55; mbr[511] = 0xAA;
    ata_write_lba(0, mbr, 1);
    sleep(10);
    print_col("DONE\n", 0x0A);

    /* STEP 3: Write GRUB core.img to reserved sectors (LBAs 1-127) */
    print("[3/5] Writing GRUB core image... ");
    int core_sectors = ((int)grub_core_img_len + 511) / 512;
    if (core_sectors > 383) core_sectors = 383;
    uint8_t core_buf[512];
    for (int s = 0; s < core_sectors; s++) {
        for (int i = 0; i < 512; i++) {
            int off = s * 512 + i;
            core_buf[i] = (off < (int)grub_core_img_len) ? grub_core_img[off] : 0;
        }
        ata_write_lba(1 + s, core_buf, 1);
    }
    sleep(10);
    print_col("DONE\n", 0x0A);

    /* STEP 4: Create /boot directory + GRUB config */
    print("[4/5] Creating boot files... ");
    fat16_mkdir("BOOT");
    fat16_cd("BOOT");
    fat16_mkdir("GRUB");
    fat16_cd("GRUB");
    const char* grub_cfg =
        "set timeout=0\n"
        "set default=0\n"
        "menuentry \"AaronOS\" {\n"
        "  multiboot /boot/AARONOS.ELF\n"
        "  boot\n"
        "}\n";
    int cfg_len = 0;
    while (grub_cfg[cfg_len]) cfg_len++;
    fat16_write_binary("GRUB.CFG", (const uint8_t*)grub_cfg, cfg_len);
    fat16_cd("..");
    fat16_cd("..");
    print_col("BOOT FILES DONE\n", 0x0A);

    /* STEP 5: Copy kernel to /boot/AARONOS.ELF */
    print("[5/5] Deploying AARONOS.ELF... ");
    fat16_cd("BOOT");
    fat16_write_binary("AARONOS.ELF", embedded_kernel, embedded_kernel_len);
    fat16_cd("..");
    sleep(10);
    print_col("DONE\n", 0x0A);

    /* Finalization */
    print_col("\n[ " KERNEL_STRING " successfully installed to hardware! ]\n", 0x0A);
    print("You may safely reboot the system.\n");

    uint32_t victory[] = {523, 659, 783, 1046};
    for(int i = 0; i < 4; i++) {
        play_sound(victory[i]);
        sleep(10);
    }
    nosound();
}
