#include <stdint.h>
#include "io.h"
#include "fat16.h"

extern void print(const char* str);
extern void print_col(const char* str, uint8_t col);
extern void play_sound(uint32_t freq);
extern void nosound(void);
extern void sleep(uint32_t ticks);

// Link to your FAT16 driver functions
extern void fat16_format_drive();
extern void ata_write_sector(uint32_t lba, const uint8_t* buffer);

char get_scan_code() {
    while (!(inb(0x64) & 1)); // Wait for keyboard buffer
    return inb(0x60);         // Read scancode
}

void run_installation() {
    print_col("\n[ AARONOS REAL INSTALLER ]\n", 0x0B); // Cyan
    print_col("Will format drive as FAT16. Continue? [y/n]: ", 0x0E); // Yellow

    char code = 0;
    while(1) {
        code = get_scan_code();
        if (code == 0x15) { // 'y' key scancode
            print("y\n");
            break; 
        } else if (code == 0x31) { // 'n' key scancode
            print("n\nInstallation aborted.\n");
            return;
        }
    }

    // 1. Wipe and Format
    print("Formatting Drive as FAT16... ");
    fat16_format_drive();
    print_col("DONE\n", 0x0A); // Green

    // 2. The Bootloader Commit
    print("Writing MBR Boot Signature 0xAA55... ");
    uint8_t mbr_sig[512] = {0};
    mbr_sig[510] = 0x55;
    mbr_sig[511] = 0xAA;
    ata_write_sector(0, mbr_sig);
    print_col("COMMITTED\n", 0x0A);

    // 3. Victory Melody
    print("\nAaronOS is now installed on the hardware.\n");
    uint32_t victory[] = {523, 659, 783, 1046}; // C-E-G-C
    for(int i = 0; i < 4; i++) {
        play_sound(victory[i]);
        sleep(1);
    }
    nosound();
}