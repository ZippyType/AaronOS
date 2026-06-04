/**
 * =============================================================================
 * AARONOS KEYBOARD & MOUSE DRIVER (keyboard.c)
 * =============================================================================
 */

#include <stdint.h>
#include "io.h"

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

/* PS/2 Mouse State */
#define MOUSE_DIV 3
int mouse_x = 40, mouse_y = 12, mouse_buttons = 0;
static int mouse_cycle = 0;
static uint8_t mouse_packet[3];
int mouse_enabled = 0;
static int mouse_old_x = -1, mouse_old_y = -1;
extern uint16_t* video_mem;

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
    /* CTRL+C = ABORT COMMAND */
    if (ctrl_active && scancode == 0x2E) {
        ctrl_c_flag = 1;
        execute_flag = 1;
    }

    /* Arrow Keys = History (CLI) or Scroll (GUI) */
    if (scancode == 0x48) { if (in_gui_mode) scroll_up(); else handle_history_up(); goto finished; }
    if (scancode == 0x50) { if (in_gui_mode) scroll_down(); else handle_history_down(); goto finished; }

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
        } else if (ascii == '\t') {
            handle_tab_completion();
        } else if (input_ptr < 254) {
            input_buffer[input_ptr++] = ascii;
            putchar_col(ascii, 0x07);
        }
    }

finished:
    outb(0x20, 0x20); /* Signal EOI to Master PIC */
}

/* Wait for PS/2 controller to accept a command */
static void ps2_wait_write() {
    int timeout = 10000;
    while ((inb(0x64) & 2) && timeout--);
}

static void ps2_wait_read() {
    int timeout = 10000;
    while (!(inb(0x64) & 1) && timeout--);
}

int mouse_init() {
    /* Enable the PS/2 mouse port (AUX) */
    ps2_wait_write();
    outb(0x64, 0xA8);

    /* Get current Compaq Status byte */
    ps2_wait_write();
    outb(0x64, 0x20);
    ps2_wait_read();
    uint8_t status = inb(0x60);
    status |= 2;   /* enable IRQ12 */
    status &= ~0x20; /* disable clock */
    ps2_wait_write();
    outb(0x64, 0x60);
    ps2_wait_write();
    outb(0x60, status);

    /* Send mouse enable command via AUX */
    ps2_wait_write();
    outb(0x64, 0xD4);
    ps2_wait_write();
    outb(0x60, 0xF4);

    /* Check for ACK (0xFA) */
    ps2_wait_read();
    uint8_t ack = inb(0x60);
    if (ack != 0xFA) return 0;

    mouse_x = 40;
    mouse_y = 12;
    mouse_cycle = 0;
    mouse_enabled = 1;
    return 1;
}

void mouse_handler_main() {
    uint8_t data = inb(0x60);

    if (!mouse_enabled) {
        outb(0xA0, 0x20);
        outb(0x20, 0x20);
        return;
    }

    switch (mouse_cycle) {
        case 0:
            if (!(data & 0x08)) { mouse_cycle = 0; break; }
            mouse_packet[0] = data;
            mouse_cycle = 1;
            break;
        case 1:
            mouse_packet[1] = data;
            mouse_cycle = 2;
            break;
        case 2:
            mouse_packet[2] = data;
            mouse_cycle = 0;

            mouse_buttons = mouse_packet[0] & 0x07;

            int dx = (int)(mouse_packet[0] & 0x10 ? mouse_packet[1] | 0xFFFFFF00 : mouse_packet[1]);
            int dy = (int)(mouse_packet[0] & 0x20 ? mouse_packet[2] | 0xFFFFFF00 : mouse_packet[2]);

            mouse_x += dx / MOUSE_DIV;
            mouse_y -= dy / MOUSE_DIV;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_x > 79) mouse_x = 79;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_y > 24) mouse_y = 24;
            break;
    }

    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void mouse_render() {
    if (!mouse_enabled) return;

    /* In GUI mode, clear the old cursor (GUI draws its own) */
    if (in_gui_mode) {
        if (mouse_old_x >= 0) {
            video_mem[mouse_old_y * 80 + mouse_old_x] ^= 0x7000;
            mouse_old_x = -1;
        }
        return;
    }

    if (mouse_old_x == mouse_x && mouse_old_y == mouse_y) return;

    if (mouse_old_x >= 0)
        video_mem[mouse_old_y * 80 + mouse_old_x] ^= 0x7000;

    if (mouse_x >= 0 && mouse_x < 80 && mouse_y >= 0 && mouse_y < 25)
        video_mem[mouse_y * 80 + mouse_x] ^= 0x7000;

    mouse_old_x = mouse_x;
    mouse_old_y = mouse_y;
}

void mouse_invalidate() {
    mouse_old_x = -1;
    mouse_old_y = -1;
}

void mouse_clear() {
    if (mouse_old_x >= 0) {
        video_mem[mouse_old_y * 80 + mouse_old_x] ^= 0x7000;
        mouse_old_x = -1;
    }
}