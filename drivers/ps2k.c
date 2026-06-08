#include <stdint.h>
#include "io.h"
#include "ps2k.h"

extern void print(const char* str);
extern void putchar_col(char c, uint8_t color);
extern void sys_reboot();
extern void scroll_up();
extern void scroll_down();
extern int in_gui_mode;
extern void handle_history_up();
extern void handle_history_down();
extern void handle_tab_completion();

extern char input_buffer[256];
extern int input_ptr;
extern volatile int execute_flag;
extern volatile int ctrl_c_flag;

static int shift_active = 0;
static int ctrl_active = 0;

unsigned char kbd_standard[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

unsigned char kbd_shifted[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

void keyboard_handler_main() {
    uint8_t scancode = inb(0x60);

    if (scancode & 0x80) {
        uint8_t key = scancode & 0x7F;
        if (key == 0x1D) ctrl_active = 0;
        if (key == 0x2A || key == 0x36) shift_active = 0;
        goto finished;
    }

    if (scancode == 0x1D) { ctrl_active = 1; goto finished; }
    if (scancode == 0x2A || scancode == 0x36) { shift_active = 1; goto finished; }

    if (ctrl_active && scancode == 0x2E) {
        ctrl_c_flag = 1;
        execute_flag = 1;
    }

    if (scancode == 0x48) { if (in_gui_mode) scroll_up(); else handle_history_up(); goto finished; }
    if (scancode == 0x50) { if (in_gui_mode) scroll_down(); else handle_history_down(); goto finished; }

    char ascii = shift_active ? kbd_shifted[scancode] : kbd_standard[scancode];

    if (ascii != 0) {
        if (ascii == '\n') {
            execute_flag = 1;
        } else if (ascii == '\b') {
            if (input_ptr > 0) {
                input_ptr--;
                putchar_col('\b', 0x07);
            }
        } else if (ascii == '\t') {
            handle_tab_completion();
        } else if (input_ptr < 254) {
            input_buffer[input_ptr++] = ascii;
            putchar_col(ascii, 0x07);
        }
    }

finished:
    outb(0x20, 0x20);
}
