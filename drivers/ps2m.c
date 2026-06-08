#include <stdint.h>
#include "io.h"
#include "ps2m.h"

#define MOUSE_DIV 3

int mouse_x = 40, mouse_y = 12, mouse_buttons = 0;
static int mouse_cycle = 0;
static uint8_t mouse_packet[3];
int mouse_enabled = 0;
static int mouse_old_x = -1, mouse_old_y = -1;

extern uint16_t* video_mem;
extern int in_gui_mode;

static void ps2_wait_write() {
    int timeout = 10000;
    while ((inb(0x64) & 2) && timeout--);
}

static void ps2_wait_read() {
    int timeout = 10000;
    while (!(inb(0x64) & 1) && timeout--);
}

int mouse_init() {
    ps2_wait_write();
    outb(0x64, 0xA8);

    ps2_wait_write();
    outb(0x64, 0x20);
    ps2_wait_read();
    uint8_t status = inb(0x60);
    status |= 2;
    status &= ~0x20;
    ps2_wait_write();
    outb(0x64, 0x60);
    ps2_wait_write();
    outb(0x60, status);

    ps2_wait_write();
    outb(0x64, 0xD4);
    ps2_wait_write();
    outb(0x60, 0xF4);

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

    if (in_gui_mode) {
        if (mouse_old_x >= 0) {
            video_mem[mouse_old_y * 80 + mouse_old_x] ^= 0x7000;
            mouse_old_x = -1;
        }
        /* Fall through — draw cursor in TUI mode too */
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
