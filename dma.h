#ifndef DMA_H
#define DMA_H
#include <stdint.h>

void dma_init();
void dma_prepare(uint8_t channel, uint32_t addr, uint32_t count, uint8_t mode);
void dma_start(uint8_t channel);
void dma_stop(uint8_t channel);

#endif
