#ifndef PS2K_H
#define PS2K_H
#include <stdint.h>

extern unsigned char kbd_standard[128];
extern unsigned char kbd_shifted[128];

void keyboard_handler_main();

#endif
