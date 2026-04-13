#include <stdint.h>
#include <stddef.h>
#include "limine.h"

// Request a framebuffer (the screen) from Limine
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

// The entry point of your OS
void _start(void) {
    // Check if we actually got a framebuffer
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        for (;;); // Halt if no screen found
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];

    // Draw a small white square in the top left
    for (size_t i = 0; i < 100; i++) {
        uint32_t *fb_ptr = fb->address;
        fb_ptr[i + (i * fb->pitch / 4)] = 0xFFFFFF; 
    }

    // Hang so the OS doesn't exit
    for (;;) {}
}