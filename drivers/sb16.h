#ifndef SB16_H
#define SB16_H
#include <stdint.h>

int sb16_init();
void sb16_play_dma(uint8_t* data, uint32_t len, uint32_t freq);
void sb16_play_pio(uint8_t* data, uint32_t len, uint32_t freq);
void sb16_stop();
int sb16_is_present();
void sb16_set_volume(uint8_t master_l, uint8_t master_r);
void sb16_set_dac_volume(uint8_t left, uint8_t right);
uint8_t sb16_get_master_volume();
uint8_t sb16_get_dac_volume();

#endif
