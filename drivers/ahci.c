#include "ahci.h"
#include "io.h"
#include <stddef.h>

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsvd;
    uint32_t dbc;
} __attribute__((packed)) prd_t;

typedef struct {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    prd_t prdt[];
} __attribute__((packed)) cmd_table_t;

typedef struct {
    uint16_t opts;
    uint16_t prdtl;
    uint32_t bytes;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
} __attribute__((packed)) cmd_header_t;

static cmd_header_t* cl = (cmd_header_t*)0;
static cmd_table_t* ct = (cmd_table_t*)0;
static int ahci_present = 0;
static int active_port = -1;
static uint32_t hba_base = 0;

extern void print(const char* str);
extern void print_col(const char* str, uint8_t col);
extern void print_hex(uint32_t val);
extern void kmemset(void* dest, uint8_t val, uint32_t len);
extern void* malloc(uint32_t size);

static uint32_t ahci_read(uint32_t reg) {
    return *(volatile uint32_t*)(hba_base + reg);
}
static void ahci_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(hba_base + reg) = val;
}
static uint32_t port_read(int p, uint32_t reg) {
    return *(volatile uint32_t*)(hba_base + 0x100 + p * 0x80 + reg);
}
static void port_write(int p, uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(hba_base + 0x100 + p * 0x80 + reg) = val;
}
#define SIG_SATA 0x00000101
#define CMD_ST 1
#define CMD_FRE 0x10
#define CMD_POD 2
#define CMD_SUD 4

int ahci_init() {
    uint32_t bar5 = 0;
    for (int bus = 0; bus < 256 && !bar5; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t addr = 0x80000000 | (bus << 16) | (slot << 11);
            outl(0xCF8, addr);
            uint16_t vendor = (uint16_t)(inl(0xCFC) & 0xFFFF);
            if (vendor == 0xFFFF) continue;
            outl(0xCF8, addr | 0x08);
            uint32_t rc = inl(0xCFC);
            if ((rc >> 24) == 0x01 && ((rc >> 16) & 0xFF) == 0x06) {
                outl(0xCF8, addr | 0x24);
                bar5 = inl(0xCFC);
                if (bar5 & 1) bar5 &= ~0x3F; else bar5 &= ~0x0F;
                outl(0xCF8, addr | 0x04);
                uint16_t cmd = (uint16_t)(inl(0xCFC) & 0xFFFF);
                cmd |= 7;
                outw(0xCFC, cmd);
            }
        }
    }
    if (!bar5) return 0;
    hba_base = bar5;

    ahci_write(0x04, ahci_read(0x04) | 1);
    while (ahci_read(0x04) & 1);
    ahci_write(0x04, ahci_read(0x04) | 0x80000000);
    uint32_t pi = ahci_read(0x0C);

    cl = (cmd_header_t*)malloc(1024);
    ct = (cmd_table_t*)malloc(256);
    if (!cl || !ct) return 0;
    kmemset(cl, 0, 1024);
    kmemset(ct, 0, 256);

    for (int p = 0; p < 32; p++) {
        if (!(pi & (1 << p))) continue;
        uint32_t sts = port_read(p, 0x28);
        if ((sts & 0xF) != 3) continue;

        port_write(p, 0x18, port_read(p, 0x18) | CMD_POD | CMD_SUD);
        port_write(p, 0x00, (uint32_t)cl);
        port_write(p, 0x08, (uint32_t)ct);
        port_write(p, 0x30, 0xFFFFFFFF);
        port_write(p, 0x18, port_read(p, 0x18) | CMD_FRE | CMD_ST);
        active_port = p;
        break;
    }
    if (active_port < 0) return 0;
    ahci_present = 1;
    return 1;
}

int ahci_is_present() { return ahci_present; }

int ahci_read_sector(uint32_t lba, uint8_t* buffer) {
    if (!ahci_present) return 0;
    int p = active_port;
    uint32_t sig = port_read(p, 0x24);
    if (sig != SIG_SATA) return 0;

    kmemset(ct, 0, 256);
    ct->cfis[0] = 0x27;
    ct->cfis[1] = 0x80;
    ct->cfis[2] = 0x20;
    ct->cfis[3] = 1;
    ct->cfis[4] = (uint8_t)lba;
    ct->cfis[5] = (uint8_t)(lba >> 8);
    ct->cfis[6] = (uint8_t)(lba >> 16);
    ct->cfis[7] = 0xE0 | ((lba >> 24) & 0x0F);

    cl->opts = 5;
    cl->prdtl = 1;
    cl->ctba = (uint32_t)ct;
    ct->prdt[0].dba = (uint32_t)buffer;
    ct->prdt[0].dbc = (512 - 1) | 0x80000000;

    port_write(p, 0x38, 1);
    while (port_read(p, 0x38) & 1);
    return 1;
}
