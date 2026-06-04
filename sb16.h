#ifndef SB16_H
#define SB16_H
#include <stdint.h>

int sb16_init();
void sb16_play_dma(uint8_t* data, uint32_t len, uint32_t freq);
void sb16_stop();
int sb16_is_present();

#endif
