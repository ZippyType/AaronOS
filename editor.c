#include <stdint.h>
#include <stddef.h>
#include "io.h"

extern void print(const char* str);
extern void putchar_col(char c, uint8_t color);
extern void drive_wait();
extern void outb(uint16_t port, uint8_t val);
extern void outw(uint16_t port, uint16_t val);
extern uint16_t inw(uint16_t port);

void run_editor() {
    print("\n--- AaronEdit ---\n");
    print("Editor is currently in read-only preview.\n");
    print("Press ESC to exit.\n");
    
    while(1) {
        // Simple loop to wait for ESC (0x01)
        uint8_t sc = *(uint8_t*)0x60; // Direct read for demo
        if(sc == 0x01) break;
    }
}