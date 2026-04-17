/**
 * =============================================================================
 * AARONOS KEYBOARD DRIVER (keyboard.c)
 * =============================================================================
 */

#include <stdint.h>
#include "io.h"

extern void print(const char* str);
extern void putchar_col(char c, uint8_t color);
extern void sys_reboot();
extern void scroll_up();
extern void scroll_down();

extern char input_buffer[256];
extern int input_ptr;
extern volatile int execute_flag;

static int shift_active = 0;
static int ctrl_active = 0;

/* Mapping Table for standard keys */
unsigned char kbd_standard[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

/* Mapping Table for Shift+keys */
unsigned char kbd_shifted[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

/**
 * Main IRQ1 Entry point.
 * Scans the keyboard data port and updates kernel state.
 */
void keyboard_handler_main() {
    uint8_t scancode = inb(0x60);

    /* Phase 1: Catch Releases */
    if (scancode & 0x80) {
        uint8_t key = scancode & 0x7F;
        if (key == 0x1D) ctrl_active = 0;
        if (key == 0x2A || key == 0x36) shift_active = 0;
        goto finished;
    }

    /* Phase 2: Catch Modifiers */
    if (scancode == 0x1D) { ctrl_active = 1; goto finished; }
    if (scancode == 0x2A || scancode == 0x36) { shift_active = 1; goto finished; }

    /* Phase 3: THE INTERRUPTS */
    /* CTRL+C = REBOOT */
    if (ctrl_active && scancode == 0x2E) { 
        print("\n[REBOOT] User triggered SIGINT. System restarting...\n");
        sys_reboot();
    }

    /* Arrow Keys = Scroll */
    if (scancode == 0x48) { scroll_up(); goto finished; }   
    if (scancode == 0x50) { scroll_down(); goto finished; } 

    /* Phase 4: Text Processing */
    char ascii = shift_active ? kbd_shifted[scancode] : kbd_standard[scancode];
    
    if (ascii != 0) {
        if (ascii == '\n') {
            execute_flag = 1; 
        } else if (ascii == '\b') {
            if (input_ptr > 0) {
                input_ptr--;
                putchar_col('\b', 0x07);
            }
        } else if (input_ptr < 254) {
            input_buffer[input_ptr++] = ascii;
            putchar_col(ascii, 0x07);
        }
    }

finished:
    outb(0x20, 0x20); /* Signal EOI to Master PIC */
}