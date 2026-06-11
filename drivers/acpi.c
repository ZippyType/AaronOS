#include "acpi.h"
#include "io.h"

typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed)) rsdp_t;

typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) sdt_header_t;

typedef struct {
    sdt_header_t header;
    uint32_t entries[];
} __attribute__((packed)) rsdt_t;

typedef struct {
    sdt_header_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved1;
    uint8_t preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_len;
    uint8_t gpe1_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alrm;
    uint8_t mon_alrm;
    uint8_t century;
    uint16_t iapc_boot_arch;
    uint8_t reserved2;
    uint32_t flags;
    uint8_t reset_reg[12];
    uint8_t reset_value;
    uint8_t reserved3[3];
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
    uint8_t x_pm1a_cnt_blk[12];
    uint8_t x_pm1b_cnt_blk[12];
} __attribute__((packed)) fadt_t;

static int acpi_ok = 0;
static uint32_t pm1a_cnt = 0;
static uint32_t smi_cmd = 0;
static uint8_t acpi_enable_val = 0;
static uint32_t reset_addr = 0xCF9;
static uint8_t reset_val = 0x0E;
static int has_acpi_reboot = 0;

extern void print(const char* str);
extern void print_col(const char* str, uint8_t col);

static uint8_t acpi_sum(void* tbl, uint32_t len) {
    uint8_t s = 0;
    for (uint32_t i = 0; i < len; i++) s += ((uint8_t*)tbl)[i];
    return s;
}

int acpi_init() {
    rsdp_t* rsdp = 0;
    for (uint32_t p = 0x000E0000; p < 0x00100000; p += 16) {
        uint8_t* bp = (uint8_t*)p;
        if (bp[0]=='R'&&bp[1]=='S'&&bp[2]=='D'&&bp[3]==' '&&bp[4]=='P'&&bp[5]=='T'&&bp[6]=='R'&&bp[7]==' ') {
            if (acpi_sum((void*)p, 20) == 0) {
                rsdp = (rsdp_t*)p;
                break;
            }
        }
    }
    if (!rsdp) {
        for (uint32_t p = 0x000F0000; p < 0x00100000; p += 16) {
            uint8_t* bp = (uint8_t*)p;
            if (bp[0]=='R'&&bp[1]=='S'&&bp[2]=='D'&&bp[3]==' '&&bp[4]=='P'&&bp[5]=='T'&&bp[6]=='R'&&bp[7]==' ') {
                if (acpi_sum((void*)p, 20) == 0) {
                    rsdp = (rsdp_t*)p;
                    break;
                }
            }
        }
    }
    if (!rsdp) return 0;

    rsdt_t* rsdt = (rsdt_t*)(uint32_t)rsdp->rsdt_address;
    if (acpi_sum(rsdt, rsdt->header.length) != 0) return 0;

    uint32_t entry_count = (rsdt->header.length - sizeof(sdt_header_t)) / 4;
    fadt_t* fadt = 0;
    for (uint32_t i = 0; i < entry_count; i++) {
        sdt_header_t* h = (sdt_header_t*)(uint32_t)rsdt->entries[i];
        if (h->signature[0]=='F'&&h->signature[1]=='A'&&h->signature[2]=='C'&&h->signature[3]=='P') {
            fadt = (fadt_t*)h;
            break;
        }
    }
    if (!fadt) return 0;

    pm1a_cnt = fadt->pm1a_cnt_blk;
    smi_cmd = fadt->smi_cmd;
    acpi_enable_val = fadt->acpi_enable;

    if (fadt->reset_reg[0] == 1) {
        reset_addr = *(uint32_t*)&fadt->reset_reg[4];
        reset_val = fadt->reset_value;
        has_acpi_reboot = 1;
    }

    acpi_ok = 1;
    return 1;
}

int acpi_is_available() { return acpi_ok; }

void acpi_poweroff() {
    if (smi_cmd && acpi_enable_val) {
        outb(smi_cmd, acpi_enable_val);
        io_wait();
    }
    if (pm1a_cnt) {
        outw(pm1a_cnt, 0x2400 | (7 << 10));
    }
    /* QEMU-specific fallback */
    outw(0xB004, 0x2000);
    outw(0x604, 0x2000);
}

void acpi_reboot() {
    if (has_acpi_reboot) {
        outb(reset_addr, reset_val);
    }
    outb(0x64, 0xFE);
}
