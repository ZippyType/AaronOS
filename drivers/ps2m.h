#ifndef PS2M_H
#define PS2M_H
#include <stdint.h>

extern int mouse_x, mouse_y, mouse_buttons;
extern int mouse_enabled;

int mouse_init();
void mouse_handler_main();
void mouse_render();
void mouse_clear();
void mouse_invalidate();

#endif
