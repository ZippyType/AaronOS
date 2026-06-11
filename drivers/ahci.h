#ifndef AHCI_H
#define AHCI_H
#include <stdint.h>

int ahci_init();
int ahci_read_sector(uint32_t lba, uint8_t* buffer);
int ahci_is_present();

#endif
