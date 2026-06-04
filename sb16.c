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

static void dsp_wait() {
    for (volatile int d = 0; d < 100; d++);
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
        char vb[4]; vb[0] = '0' + maj; vb[1] = '.'; 
        if (min < 10) { vb[2] = '0' + min; vb[3] = '\0'; }
        else { vb[2] = '0' + (min / 10); vb[3] = '0' + (min % 10); vb[4] = '\0'; }
        print_col(vb, 0x09);
    }
    print("\n");
    sb16_present = 1;

    dma_init();
    return 1;
}

int sb16_is_present() { return sb16_present; }

/* Blocking 8-bit single-cycle DMA playback */
void sb16_play_dma(uint8_t* data, uint32_t len, uint32_t freq) {
    if (!sb16_present || len == 0) return;

    uint32_t play_len = len;

    dma_prepare(DMA_CHANNEL, (uint32_t)data, play_len, 0x49);
    dma_start(DMA_CHANNEL);

    uint16_t tc = 256 - (1000000 / freq);

    dsp_write(0x40);
    dsp_write((uint8_t)tc);
    dsp_wait();

    dsp_write(0xD1);
    dsp_wait();

    /* 8-bit single-cycle DMA output, length-1 */
    dsp_write(0xC0);
    dsp_write((uint8_t)((play_len - 1) & 0xFF));
    dsp_write((uint8_t)(((play_len - 1) >> 8) & 0xFF));

    /* Poll DMA status bit (bit 5 of DSP_BUFSTAT) — 0 = done */
    int poll = 0;
    while (poll < 1000000) {
        uint8_t st = inb(DSP_BUFSTAT);
        if (!(st & 0x20)) break;
        poll++;
        for (volatile int d = 0; d < 10; d++);
    }

    dsp_write(0xD0);
    dma_stop(DMA_CHANNEL);
}

void sb16_stop() {
    dsp_write(0xD0);
    dsp_write(0xD3);
    dma_stop(DMA_CHANNEL);
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
