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

#define MIXER_ADDR  (SB_BASE + 4)
#define MIXER_DATA  (SB_BASE + 5)

#define DMA_CHANNEL 1  /* 8-bit DMA channel 1 */

static int sb16_present = 0;

extern void print(const char* str);
extern void print_col(const char* str, uint8_t col);
extern volatile uint32_t timer_ticks;

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

static void dsp_wait() {
    for (volatile int d = 0; d < 100; d++);
}

static void mixer_write(uint8_t reg, uint8_t val) {
    outb(MIXER_ADDR, reg);
    for (volatile int d = 0; d < 10; d++);
    outb(MIXER_DATA, val);
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
    print("SB16: DSP v"); 
    {
        char vb[6]; vb[0] = '0' + maj; vb[1] = '.'; 
        if (min < 10) { vb[2] = '0' + min; vb[3] = '\0'; }
        else { vb[2] = '0' + (min / 10); vb[3] = '0' + (min % 10); vb[4] = '\0'; }
        print_col(vb, 0x09);
    }
    print("\n");
    sb16_present = 1;

    /* Initialize mixer volumes to max */
    sb16_set_volume(15, 15);
    sb16_set_dac_volume(15, 15);
    mixer_write(0x28, 0xFF);  /* CD volume: max */
    mixer_write(0x2E, 0xFF);  /* Line volume: max */
    mixer_write(0x04, 0x00);  /* Mic volume +20dB boost off */
    mixer_write(0x0C, 0x00);  /* IRQ config (none) */
    mixer_write(0x36, 0x00);  /* Output control: line=FM, no filter */

    dma_init();
    return 1;
}

int sb16_is_present() { return sb16_present; }

/* Blocking 8-bit single-cycle DMA playback */
void sb16_play_dma(uint8_t* data, uint32_t len, uint32_t freq) {
    if (!sb16_present || len == 0) return;

    /* Calculate duration in timer ticks (100 Hz) */
    uint32_t duration_ticks = (len * 100) / freq + 1;

    /* Prevent IRQ interference */
    asm volatile("cli");

    /* DMA: single-cycle, write transfer (memory→I/O), ch1 */
    dma_prepare(DMA_CHANNEL, (uint32_t)data, len - 1, 0x45);
    dma_start(DMA_CHANNEL);

    uint16_t tc = 256 - (1000000 / freq);

    dsp_write(0x40);
    dsp_write((uint8_t)tc);
    dsp_wait();

    dsp_write(0xD1);
    dsp_wait();

    /* 8-bit single-cycle DMA output */
    dsp_write(0xC0);
    dsp_write((uint8_t)((len - 1) & 0xFF));
    dsp_write((uint8_t)(((len - 1) >> 8) & 0xFF));

    /* Re-enable interrupts — DMA runs in background */
    asm volatile("sti");

    /* Wait for expected duration using PIT timer ticks */
    uint32_t target = timer_ticks + duration_ticks;
    while (timer_ticks < target) {
        asm volatile("hlt");
    }

    /* Stop playback */
    asm volatile("cli");
    dsp_write(0xD0);
    dma_stop(DMA_CHANNEL);
    asm volatile("sti");
}

/* PIO playback — writes data directly to DSP, bypasses DMA */
void sb16_play_pio(uint8_t* data, uint32_t len, uint32_t freq) {
    if (!sb16_present || len == 0) return;

    uint16_t tc = 256 - (1000000 / freq);

    print("PIO: starting playback...\n");

    dsp_write(0x40);
    dsp_write((uint8_t)tc);
    dsp_wait();
    dsp_write(0xD1);
    dsp_wait();

    for (uint32_t i = 0; i < len; i++) {
        dsp_write(0x10);
        dsp_write(data[i]);
        if (i % 1000 == 0) { dsp_wait(); }
    }

    dsp_write(0xD0);
    print("PIO: done.\n");
}

void sb16_stop() {
    dsp_write(0xD0);
    dsp_write(0xD3);
    dma_stop(DMA_CHANNEL);
}

void sb16_set_volume(uint8_t master_l, uint8_t master_r) {
    if (!sb16_present) return;
    uint8_t val = ((master_r & 0x0F) << 4) | (master_l & 0x0F);
    mixer_write(0x22, val);
    mixer_write(0x02, master_l & 0x3F);
    mixer_write(0x03, master_r & 0x3F);
}

void sb16_set_dac_volume(uint8_t left, uint8_t right) {
    if (!sb16_present) return;
    uint8_t val = ((right & 0x0F) << 4) | (left & 0x0F);
    mixer_write(0x30, val);
}

uint8_t sb16_get_master_volume() {
    if (!sb16_present) return 0;
    outb(MIXER_ADDR, 0x22);
    return inb(MIXER_DATA);
}

uint8_t sb16_get_dac_volume() {
    if (!sb16_present) return 0;
    outb(MIXER_ADDR, 0x30);
    return inb(MIXER_DATA);
}

/* WAV header parsing + playback */
int sb16_play_wav(uint8_t* data, uint32_t file_len) {
    if (!sb16_present) return -1;

    /* Minimum WAV size: 44 bytes header */
    if (file_len < 44) { print("WAV: File too small\n"); return -1; }

    /* Check RIFF header */
    if (data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F') {
        print("WAV: Not a RIFF file\n"); return -1;
    }
    if (data[8] != 'W' || data[9] != 'A' || data[10] != 'V' || data[11] != 'E') {
        print("WAV: Not a WAVE file\n"); return -1;
    }

    /* Parse chunks to find fmt and data */
    uint16_t channels = 1;
    uint32_t sample_rate = 22050;
    uint16_t bits_per_sample = 8;
    uint32_t data_size = 0;
    uint32_t data_offset = 0;
    int found_fmt = 0;

    uint32_t offset = 12;
    while (offset + 8 <= file_len) {
        uint32_t chunk_len = (uint32_t)data[offset+4] | ((uint32_t)data[offset+5] << 8) |
                             ((uint32_t)data[offset+6] << 16) | ((uint32_t)data[offset+7] << 24);
        uint32_t chunk_end = offset + 8 + chunk_len;

        if (data[offset] == 'f' && data[offset+1] == 'm' && data[offset+2] == 't' && data[offset+3] == ' ') {
            if (chunk_len < 16) { print("WAV: fmt chunk too small\n"); return -1; }
            uint16_t format = (uint16_t)data[offset+8] | ((uint16_t)data[offset+9] << 8);
            if (format != 1) { print("WAV: Unsupported format (not PCM)\n"); return -1; }
            channels = (uint16_t)data[offset+10] | ((uint16_t)data[offset+11] << 8);
            sample_rate = (uint32_t)data[offset+12] | ((uint32_t)data[offset+13] << 8) |
                          ((uint32_t)data[offset+14] << 16) | ((uint32_t)data[offset+15] << 24);
            bits_per_sample = (uint16_t)data[offset+22] | ((uint16_t)data[offset+23] << 8);
            found_fmt = 1;
        } else if (data[offset] == 'd' && data[offset+1] == 'a' && data[offset+2] == 't' && data[offset+3] == 'a') {
            data_size = chunk_len;
            data_offset = offset + 8;
            break;
        }

        if (chunk_end > file_len) break;
        if (chunk_len == 0) break;
        offset = chunk_end;
        if (offset % 2) offset++;
    }

    if (!found_fmt) { print("WAV: No fmt chunk found\n"); return -1; }
    if (data_size == 0) { print("WAV: No data chunk found\n"); return -1; }

    /* Convert if needed */
    uint8_t* play_buf = data + data_offset;
    uint32_t play_len = data_size;

    /* If stereo, convert to mono by averaging channels */
    if (channels > 1) {
        int step = (bits_per_sample / 8) * channels;
        int mono_step = bits_per_sample / 8;
        uint32_t samples = play_len / step;
        for (uint32_t i = 0; i < samples && i * mono_step < play_len; i++) {
            uint32_t sum = 0;
            for (int c = 0; c < channels; c++) {
                uint32_t src_off = i * step + c * mono_step;
                if (bits_per_sample == 8) sum += play_buf[src_off];
                else sum += play_buf[src_off] | ((uint32_t)play_buf[src_off+1] << 8);
            }
            sum /= channels;
            if (bits_per_sample == 8) play_buf[i] = (uint8_t)sum;
            else { play_buf[i*2] = (uint8_t)(sum & 0xFF); play_buf[i*2+1] = (uint8_t)((sum >> 8) & 0xFF); }
        }
        play_len = samples * mono_step;
    }

    /* Convert 16-bit to 8-bit if needed */
    if (bits_per_sample == 16) {
        uint32_t samples = play_len / 2;
        for (uint32_t i = 0; i < samples; i++) {
            int16_t sample = (int16_t)(play_buf[i*2] | (play_buf[i*2+1] << 8));
            /* Convert signed 16-bit to unsigned 8-bit */
            play_buf[i] = (uint8_t)((sample / 256) + 128);
        }
        play_len = samples;
    }

    print_col("WAV: Playing...\n", 0x0A);
    sb16_play_dma(play_buf, play_len, sample_rate);
    print_col("WAV: Done\n", 0x07);
    return 0;
}
