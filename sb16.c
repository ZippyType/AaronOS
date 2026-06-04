#include "sb16.h"
#include "io.h"
#include "dma.h"

/* Standard SB16 base port = 0x220 */
#define SB_BASE 0x220
#define DSP_RESET   (SB_BASE + 6)
#define DSP_READ    (SB_BASE + 0xA)
#define DSP_WRITE   (SB_BASE + 0xC)
#define DSP_BUFSTAT (SB_BASE + 0xE)
#define DSP_IRQACK  (SB_BASE + 0xE)

#define DMA_CHANNEL 1  /* 8-bit DMA channel 1 */

static int sb16_present = 0;

extern void print(const char* str);
extern void print_col(const char* str, uint8_t col);

static void dsp_write(uint8_t val) {
    int timeout = 10000;
    while ((inb(DSP_WRITE) & 0x80) && timeout--);
    outb(DSP_WRITE, val);
}

static uint8_t dsp_read() {
    int timeout = 10000;
    while (!(inb(DSP_BUFSTAT) & 0x80) && timeout--);
    return inb(DSP_READ);
}

int sb16_init() {
    /* Reset DSP */
    outb(DSP_RESET, 1);
    for (volatile int d = 0; d < 1000; d++);
    outb(DSP_RESET, 0);

    /* Check for 0xAA response */
    if (dsp_read() != 0xAA) {
        print("SB16: No DSP detected at 0x220\n");
        return 0;
    }

    /* Read version */
    dsp_write(0xE1);
    uint8_t maj = dsp_read();
    uint8_t min = dsp_read();
    print("SB16: DSP v"); print_col("?", 0x09);
    sb16_present = 1;

    dma_init();
    return 1;
}

int sb16_is_present() { return sb16_present; }

void sb16_play_dma(uint8_t* data, uint32_t len, uint32_t freq) {
    if (!sb16_present) return;

    dma_prepare(DMA_CHANNEL, (uint32_t)data, len, 0x49);
    dma_start(DMA_CHANNEL);

    /* Set time constant: 256 - (1000000 / freq) */
    uint16_t tc = 256 - (1000000 / freq);

    /* Program DSP for 8-bit single-cycle DMA playback */
    dsp_write(0x40);
    dsp_write((uint8_t)tc);

    dsp_write(0xD1);  /* speaker on */

    dsp_write(0xC0);  /* 8-bit output, mono, unsigned, FIFO off */
    dsp_write((uint8_t)(len & 0xFF));
    dsp_write((uint8_t)((len >> 8) & 0xFF));

    dsp_write(0xD0);  /* speaker off */
    dma_stop(DMA_CHANNEL);
}

void sb16_stop() {
    dsp_write(0xD0);
    dma_stop(DMA_CHANNEL);
}
