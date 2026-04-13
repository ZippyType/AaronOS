#include <stdint.h>
#include "io.h"

/* --- 1. FUNCTION DECLARATIONS --- */
extern void print(const char* str);
extern void putchar_col(char c, uint8_t color);
extern void sys_reboot();

/* --- 2. SHARED SHELL STATE --- */
extern char input_buffer[256];
extern int input_ptr;
extern volatile int execute_flag;

/* --- 3. LOCAL STATE --- */
static int shift_pressed = 0;
static int ctrl_pressed = 0;

unsigned char kbd_us[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

void keyboard_handler_main() {
    uint8_t scancode = inb(0x60);

    // --- CTRL Key Logic ---
    if (scancode == 0x1D) { ctrl_pressed = 1; goto finalize; }
    if (scancode == 0x9D) { ctrl_pressed = 0; goto finalize; }

    // --- SHIFT Key Logic ---
    if (scancode == 0x2A || scancode == 0x36) { shift_pressed = 1; goto finalize; }
    if (scancode == 0xAA || scancode == 0xB6) { shift_pressed = 0; goto finalize; }

    // --- THE "NUCLEAR" CTRL+C ---
    if (ctrl_pressed && scancode == 0x2E) { // 0x2E is 'C'
        print("\n[CTRL+C] REBOOTING...\n");
        sys_reboot();
        
        // Triple Fault Fallback (if sys_reboot fails)
        asm volatile ("lidt (%0)" : : "r" (0));
        asm volatile ("int $3");
    }

    // --- Normal Character Input ---
    if (!(scancode & 0x80)) {
        char c = kbd_us[scancode];
        if (c == '\n') {
            execute_flag = 1;
        } else if (c == '\b') {
            if (input_ptr > 0) {
                input_ptr--;
                putchar_col('\b', 0x07);
            }
        } else if (input_ptr < 255) {
            input_buffer[input_ptr++] = c;
            putchar_col(c, 0x07);
        }
    }

finalize:
    outb(0x20, 0x20); // Send EOI to PIC
}