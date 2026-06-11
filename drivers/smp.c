#include "smp.h"
#include "io.h"

#define LOCAL_APIC   0xFEE00000

#define APIC_SPIV   0xF0
#define APIC_ID     0x20

static volatile int ap_count = 0;

extern const uint8_t embedded_trampoline[];
extern const uint32_t embedded_trampoline_len;

extern void print(const char* str);
extern void print_col(const char* str, uint8_t col);
extern void print_hex(uint32_t val);

static inline void apic_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(LOCAL_APIC + reg) = val;
}
static inline uint32_t apic_read(uint32_t reg) {
    return *(volatile uint32_t*)(LOCAL_APIC + reg);
}

void smp_ap_startup() {
    ap_count++;
    while (1) asm("cli; hlt");
}

int smp_init() {
    /* Enable local APIC via MSR */
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1B));
    lo |= 0x800;
    asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(0x1B));

    /* Enable local APIC via SVR */
    apic_write(APIC_SPIV, apic_read(APIC_SPIV) | 0x100);

    uint32_t bsp_id = apic_read(APIC_ID) >> 24;
    print("SMP: BSP APIC ID ");
    print_hex(bsp_id);
    print("\n");

    /* Copy trampoline to 0x8000 (for future AP bringup) */
    for (uint32_t i = 0; i < embedded_trampoline_len && i < 1024; i++)
        *(uint8_t*)(0x8000 + i) = embedded_trampoline[i];

    /* Store smp_ap_startup address at 0x7FE0 for trampoline to call */
    uint32_t ap_main = (uint32_t)smp_ap_startup;
    *(uint32_t*)0x7FE0 = ap_main;

    /* TODO: Send INIT/STARTUP IPIs when APIC ICR write is fixed */
    print("[SMP] AP bringup requires IPI fix, single-core only\n");

    return 1;
}

int smp_cpu_count() {
    return 1 + ap_count;
}
