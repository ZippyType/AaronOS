
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

static int shift_pressed = 0;
static int ctrl_pressed = 0;

unsigned char kbd_us[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

unsigned char kbd_us_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

void keyboard_handler_main() {
    uint8_t scancode = inb(0x60);

    // --- 1. Update Modifier States ---
    if (scancode == 0x1D) { ctrl_pressed = 1; goto finalize; }
    if (scancode == 0x9D) { ctrl_pressed = 0; goto finalize; }
    if (scancode == 0x2A || scancode == 0x36) { shift_pressed = 1; goto finalize; }
    if (scancode == 0xAA || scancode == 0xB6) { shift_pressed = 0; goto finalize; }

    // --- 2. Arrow Keys (Scrolling) ---
    if (scancode == 0x48) { scroll_up(); goto finalize; }   
    if (scancode == 0x50) { scroll_down(); goto finalize; } 

    // --- 3. THE INTERRUPT (Ctrl+C) ---
    // We check this BEFORE anything else
    if (ctrl_pressed && scancode == 0x2E) { 
        print("\n[SIGINT] CTRL+C Detected. Rebooting...\n");
        sys_reboot();
    }

    // --- 4. Character Input ---
    if (!(scancode & 0x80)) {
        char c = shift_pressed ? kbd_us_shift[scancode] : kbd_us[scancode];
        if (c != 0) {
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
    }

finalize:
    outb(0x20, 0x20); // EOI
}