#include "dma.h"
#include "io.h"

/* Master 8237: 0x00-0x0F, channels 0-3 (8-bit) */
/* Slave  8237: 0xC0-0xDF, channels 4-7 (16-bit) */
/* Channel 4 is cascade (master→slave link) */

#define DMA_MASK     0x0A
#define DMA_MODE     0x0B
#define DMA_CLR_FF   0x0C
#define DMA_RESET    0x0D
#define DMA_MASK_ALL 0x0F

#define DMA_SLAVE_MASK     0xD4
#define DMA_SLAVE_MODE     0xD6
#define DMA_SLAVE_CLR_FF   0xD8
#define DMA_SLAVE_RESET    0xDA
#define DMA_SLAVE_MASK_ALL 0xDC

static uint8_t dma_channel_port[8] = {0x00,0x02,0x04,0x06,0xC0,0xC4,0xC8,0xCC};
static uint8_t dma_page_port[8] =   {0x87,0x83,0x01,0x02,0x8F,0x8B,0x89,0x8A};

void dma_init() {
    outb(DMA_RESET, 0xFF);
    outb(DMA_SLAVE_RESET, 0xFF);
    outb(DMA_MASK_ALL, 0x0F);
    outb(DMA_SLAVE_MASK_ALL, 0x0F);
}

void dma_prepare(uint8_t channel, uint32_t addr, uint32_t count, uint8_t mode) {
    if (channel > 7) return;
    dma_stop(channel);

    uint8_t is_16bit = (channel >= 4);
    if (is_16bit) {
        count >>= 1;
        addr >>= 1;
    }

    uint8_t mask_reg = (channel < 4) ? DMA_MASK : DMA_SLAVE_MASK;
    uint8_t mode_reg = (channel < 4) ? DMA_MODE : DMA_SLAVE_MODE;
    uint8_t clr_ff   = (channel < 4) ? DMA_CLR_FF : DMA_SLAVE_CLR_FF;
    uint8_t ch       = channel & 3;

    outb(clr_ff, 0xFF);
    outb(mode_reg, (ch & 3) | (mode & 0xFC));
    outb(clr_ff, 0xFF);

    uint16_t port = dma_channel_port[channel];
    outb(port, (uint8_t)(addr & 0xFF));
    outb(port, (uint8_t)((addr >> 8) & 0xFF));

    outb(dma_page_port[channel], (uint8_t)((addr >> 16) & 0xFF));

    outb(clr_ff, 0xFF);
    outb(port, (uint8_t)(count & 0xFF));
    outb(port, (uint8_t)((count >> 8) & 0xFF));

    outb(mask_reg, ch & 3);
}

void dma_start(uint8_t channel) {
    uint8_t mask_reg = (channel < 4) ? DMA_MASK : DMA_SLAVE_MASK;
    outb(mask_reg, channel & 3);
}

void dma_stop(uint8_t channel) {
    uint8_t mask_reg = (channel < 4) ? DMA_MASK : DMA_SLAVE_MASK;
    outb(mask_reg, (channel & 3) | 4);
}
