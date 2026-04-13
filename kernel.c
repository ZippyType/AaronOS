/**
 * =============================================================================
 * AARONOS KERNEL - FULL MONOLITHIC BUILD 
 * =============================================================================
 * VERSION: 3.8.5-STABLE
 * ARCHITECTURE: x86 (i386)
 * DESCRIPTION: High-stability monolithic kernel with persistent storage hooks,
 * PIT-based audio engine, deep hardware monitoring, and comprehensive 
 * hardware interrupt handling.
 * =============================================================================
 */

#include <stdint.h>
#include <stddef.h>
#include "io.h"      
#include "fat16.h"   

/* ========================================================================== */
/* 1. KERNEL SYSTEM IDENTITY                                                  */
/* ========================================================================== */

#define KERNEL_NAME        "AaronOS"
#define KERNEL_VERSION     "3.8.5-STABLE"
#define KERNEL_BUILD       "2026-04-11-QEMU"

/* ========================================================================== */
/* 2. HARDWARE MEMORY & PORTS                                                 */
/* ========================================================================== */

#define VIDEO_ADDR         0xB8000
#define SCREEN_WIDTH       80
#define SCREEN_HEIGHT      25

#define PIT_CHANNEL_0      0x40
#define PIT_CHANNEL_1      0x41
#define PIT_CHANNEL_2      0x42
#define PIT_COMMAND        0x43

#define PC_SPEAKER_PORT    0x61
#define KBD_STATUS_PORT    0x64
#define KBD_DATA_PORT      0x60

#define CMOS_ADDRESS       0x70
#define CMOS_DATA          0x71

#define COLOR_DEFAULT      0x07 
#define COLOR_SUCCESS      0x0A 
#define COLOR_HELP         0x0B 
#define COLOR_ALERT        0x0E 
#define COLOR_PANIC        0x4F 
#define COLOR_AUDIO        0x0D 

#define NOTE_C4            261
#define NOTE_D4            294
#define NOTE_E4            329
#define NOTE_F4            349
#define NOTE_G4            392
#define NOTE_A4            440
#define NOTE_B4            493
#define NOTE_C5            523

/* TUI stuff. */
#define TUI_COLOR       0x1F  // White text on Blue background (Classic BIOS look)
#define BOX_HLINE       0xCD  // ═
#define BOX_VLINE       0xBA  // ║
#define BOX_TL          0xC9  // ╔
#define BOX_TR          0xBB  // ╗
#define BOX_BL          0xC8  // ╚
#define BOX_BR          0xBC  // ╝

/* ========================================================================== */
/* 3. FORWARD DECLARATIONS                                                    */
/* ========================================================================== */

void nosound();
void sleep(uint32_t ticks);
void play_sound(uint32_t nFrequence);
void update_cursor();
void clear_screen();
void print(const char* str);
void print_col(const char* str, uint8_t col);
void kpanic(const char* message);
void sys_reboot();
void init_timer(uint32_t frequency);
void read_rtc();
void process_shell();

/* --- Global Hardware State --- */
volatile uint32_t timer_ticks = 0; 

void timer_callback() {
    timer_ticks++;
}

/* --- Timezone Configuration --- */
int current_offset = 2; 
char current_tz_name[32] = "Amsterdam (CEST)";

/* ========================================================================== */
/* 4. KERNEL DATA STRUCTURES                                                  */
/* ========================================================================== */

typedef struct {
    uint32_t uptime_ticks;
    uint32_t total_commands;
    uint32_t last_freq;
    uint8_t  speaker_state;
    uint8_t  disk_presence;
} kernel_health_t;

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint32_t year;
} rtc_time_t;

uint16_t* video_mem = (uint16_t*)VIDEO_ADDR;
int cursor_x = 0;
int cursor_y = 0;
int prompt_limit = 0;

char input_buffer[256];             
int input_ptr = 0;                  
volatile int execute_flag = 0;      
int in_gui_mode = 0; // Tracks if we are in TUI mode
kernel_health_t sys_stats;
rtc_time_t system_time;

/* ========================================================================== */
/* 5. CORE STRING & MEMORY LIBRARIES                                          */
/* ========================================================================== */

int kstrcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++; s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int kstrncmp(const char* s1, const char* s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++; s2++; n--;
    }
    if (n == 0) return 0;
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void kmemset(void* dest, uint8_t val, size_t len) {
    uint8_t* ptr = (uint8_t*)dest;
    while(len--) *ptr++ = val;
}

int katoi(const char* str) {
    int res = 0;
    int sign = 1;
    int i = 0;
    if (str[0] == '-') { sign = -1; i++; }
    for (; str[i] >= '0' && str[i] <= '9'; ++i) {
        res = res * 10 + str[i] - '0';
    }
    return res * sign;
}

size_t kstrlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

void kstrcpy(char* dest, const char* src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

void reverse(char str[], int length) {
    int start = 0;
    int end = length - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        end--;
        start++;
    }
}

void itoa(int num, char* str, int base) {
    int i = 0;
    int isNegative = 0;
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }
    if (num < 0 && base == 10) {
        isNegative = 1;
        num = -num;
    }
    while (num != 0) {
        int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }
    if (isNegative) str[i++] = '-';
    str[i] = '\0';
    reverse(str, i);
}

/* ========================================================================== */
/* 6. REAL TIME CLOCK (CMOS) HARDWARE                                         */
/* ========================================================================== */

int get_update_in_progress_flag() {
    outb(CMOS_ADDRESS, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

uint8_t get_rtc_register(int reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

void read_rtc() {
    uint8_t last_second, last_minute, last_hour, last_day, last_month, last_year, registerB;
    
    while (get_update_in_progress_flag());
    system_time.second = get_rtc_register(0x00);
    system_time.minute = get_rtc_register(0x02);
    system_time.hour = get_rtc_register(0x04);
    system_time.day = get_rtc_register(0x07);
    system_time.month = get_rtc_register(0x08);
    system_time.year = get_rtc_register(0x09);

    do {
        last_second = system_time.second;
        last_minute = system_time.minute;
        last_hour = system_time.hour;
        last_day = system_time.day;
        last_month = system_time.month;
        last_year = system_time.year;

        while (get_update_in_progress_flag());
        system_time.second = get_rtc_register(0x00);
        system_time.minute = get_rtc_register(0x02);
        system_time.hour = get_rtc_register(0x04);
        system_time.day = get_rtc_register(0x07);
        system_time.month = get_rtc_register(0x08);
        system_time.year = get_rtc_register(0x09);
    } while ((last_second != system_time.second) || (last_minute != system_time.minute) || 
             (last_hour != system_time.hour) || (last_day != system_time.day) || 
             (last_month != system_time.month) || (last_year != system_time.year));

    registerB = get_rtc_register(0x0B);

    if (!(registerB & 0x04)) {
        system_time.second = (system_time.second & 0x0F) + ((system_time.second / 16) * 10);
        system_time.minute = (system_time.minute & 0x0F) + ((system_time.minute / 16) * 10);
        system_time.hour = ( (system_time.hour & 0x0F) + (((system_time.hour & 0x70) / 16) * 10) ) | (system_time.hour & 0x80);
        system_time.day = (system_time.day & 0x0F) + ((system_time.day / 16) * 10);
        system_time.month = (system_time.month & 0x0F) + ((system_time.month / 16) * 10);
        system_time.year = (system_time.year & 0x0F) + ((system_time.year / 16) * 10);
    }

    int raw_h = (int)system_time.hour;
    raw_h += current_offset;
    if (raw_h >= 24) raw_h -= 24;
    if (raw_h < 0) raw_h += 24;
    system_time.hour = (uint8_t)raw_h;

    if (!(registerB & 0x02) && (system_time.hour & 0x80)) {
        system_time.hour = ((system_time.hour & 0x7F) + 12) % 24;
    }
    system_time.year += 2000;
}

/* ========================================================================== */
/* 7. VGA TERMINAL ENGINE                                                     */
/* ========================================================================== */

void scroll() {
    if (cursor_y >= SCREEN_HEIGHT) {
        for (int i = 0; i < (SCREEN_HEIGHT - 1) * SCREEN_WIDTH; i++) {
            video_mem[i] = video_mem[i + SCREEN_WIDTH];
        }
        for (int i = (SCREEN_HEIGHT - 1) * SCREEN_WIDTH; i < SCREEN_HEIGHT * SCREEN_WIDTH; i++) {
            video_mem[i] = (uint16_t)' ' | (COLOR_DEFAULT << 8);
        }
        cursor_y = SCREEN_HEIGHT - 1;
    }
}

void update_cursor() {
    uint16_t pos = cursor_y * SCREEN_WIDTH + cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void putchar_col(char c, uint8_t color) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        if (cursor_x > prompt_limit) {
            cursor_x--;
            video_mem[cursor_y * SCREEN_WIDTH + cursor_x] = (uint16_t)' ' | (color << 8);
        }
    } else {
        video_mem[cursor_y * SCREEN_WIDTH + cursor_x] = (uint16_t)c | (color << 8);
        cursor_x++;
    }
    scroll();
    update_cursor();
}

void print(const char* str) {
    for (int i = 0; str[i]; i++) putchar_col(str[i], COLOR_DEFAULT);
}

void print_col(const char* str, uint8_t col) {
    for (int i = 0; str[i]; i++) putchar_col(str[i], col);
}

void clear_screen() {
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        video_mem[i] = (uint16_t)' ' | (COLOR_DEFAULT << 8);
    }
    cursor_x = 0; cursor_y = 0;
    update_cursor();
}

void putchar_at(char c, uint8_t color, int x, int y) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        video_mem[y * SCREEN_WIDTH + x] = (uint16_t)c | (color << 8);
    }
}

void print_at(const char* str, uint8_t color, int x, int y) {
    for (int i = 0; str[i]; i++) {
        putchar_at(str[i], color, x + i, y);
    }
}

/* ========================================================================== */
/* 8. AUDIO & SOUND ENGINE                                                    */
/* ========================================================================== */

void init_timer(uint32_t frequency) {
    uint32_t divisor = 1193180 / frequency;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void play_sound(uint32_t nFrequence) {
    if (nFrequence == 0) return;
    uint32_t Div = 1193180 / nFrequence;
    outb(PIT_COMMAND, 0xB6);
    outb(PIT_CHANNEL_2, (uint8_t)(Div));
    outb(PIT_CHANNEL_2, (uint8_t)(Div >> 8));

    uint8_t tmp = inb(PC_SPEAKER_PORT);
    if (tmp != (tmp | 3)) {
        outb(PC_SPEAKER_PORT, tmp | 3);
    }
    sys_stats.last_freq = nFrequence;
    sys_stats.speaker_state = 1;
}

void nosound() {
    uint8_t tmp = inb(PC_SPEAKER_PORT) & 0xFC;
    outb(PC_SPEAKER_PORT, tmp);
    sys_stats.speaker_state = 0;
}

void sleep(uint32_t ticks) {
    uint32_t eticks = timer_ticks + ticks;
    while(timer_ticks < eticks) {
        asm volatile("hlt"); 
    }
}

void play_song(uint32_t* notes, uint32_t* durations, int length) {
    for (int i = 0; i < length; i++) {
        if (notes[i] == 0) {
            nosound();
        } else {
            play_sound(notes[i]);
        }
        sleep(durations[i]);
        nosound();
        for(volatile int d = 0; d < 500000; d++); 
    }
}

void boot_jingle() {
    print_col("[System] Initializing Audio Hardware...\n", COLOR_AUDIO);
    play_sound(523); sleep(25); 
    play_sound(659); sleep(25); 
    play_sound(783); sleep(25); 
    play_sound(1046); sleep(45); 
    nosound();
}

/* ========================================================================== */
/* 9. SYSTEM RECOVERY                                                         */
/* ========================================================================== */

void kpanic(const char* message) {
    kmemset(video_mem, 0, SCREEN_WIDTH * SCREEN_HEIGHT * 2);
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        video_mem[i] = (uint16_t)' ' | (COLOR_PANIC << 8);
    }
    cursor_x = 0; cursor_y = 0;
    print("CRITICAL_KERNEL_HALT (0xDEADBEEF)\n");
    print("The system has been halted to prevent hardware damage.\n\n");
    print("REASON: "); print(message);
    print("\n\nPress RESET on your machine to restart.");
    while(1) { asm volatile("cli; hlt"); }
}

void sys_reboot() {
    print_col("\n[ AaronOS ] System Reboot Initiated...", COLOR_ALERT);
    sleep(20);
    uint8_t good = 0x02;
    while (good & 0x02) {
        good = inb(KBD_STATUS_PORT);
    }
    outb(KBD_STATUS_PORT, 0xFE); 
    kpanic("REBOOT_PULSE_FAILED");
}

/* ========================================================================== */
/* 10. THE SHELL INTERPRETER                                                  */
/* ========================================================================== */

extern void run_installation(); 
extern void fat16_list_files();
extern void fat16_cat(char* name);
extern void fat16_write_to_test(char* content);
extern void keyboard_handler_asm();
extern void timer_handler_asm();

void process_shell() {
    outb(0x20, 0x20); outb(0xA0, 0x20); 
    print("\n");
    sys_stats.total_commands++;

    if (input_ptr > 0) {
        input_buffer[input_ptr] = '\0';
        
        if (kstrcmp(input_buffer, "help") == 0) {
            print_col("--- AaronOS Command List ---\n", COLOR_HELP);
            print("install  - Run HDD deployment\n");
            print("reboot   - Warm restart\n");
            print("shutdown - ACPI Power off\n");
            print("ver      - Show system version\n");
            print("time     - Display hardware clock\n");
            print("tz [city]- Set timezone (e.g. tz amsterdam)\n");
            print("cls      - Clear terminal window\n");
            print("panic    - Test kernel crash\n");
            print("beep [f] - Play tone (ex: beep 440)\n");
            print("dir      - List disk contents\n");
            print("cat [f]  - Read text file\n");
            print("write [t]- Append text to disk\n");
            print("echo [t] - Print text to screen\n");
            print("cpu      - Show hardware vendor\n");
            print("memo     - Makes a memo\n");
            print("music    - Plays a bit of music\n");
            print("siren    - Sounds a siren\n");
            print("gui      - Switches to TUI (GUI like Windows(R) or MacOS(R))\n");
            print("           Use CTRL-T to switch back to shell.\n");
        }
        else if (kstrcmp(input_buffer, "gui") == 0) {
            in_gui_mode = 1;
            // 1. Fill the background with a "Desktop Teal"
            for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
                video_mem[i] = (uint16_t)0xB1 | (0x30 << 8); // Dotted pattern for texture
            }

            // 2. Define Window Dimensions
            int win_w = 60;
            int win_h = 15;
            int start_x = (80 - win_w) / 2;
            int start_y = (25 - win_h) / 2;
            uint8_t win_col = 0x70; // Black text on Light Gray background

            // 3. Draw the Shadow (Classic 90s look)
            for(int i = 1; i < win_h; i++) {
                for(int j = 1; j < win_w; j++) {
                    putchar_at(' ', 0x08, start_x + j + 1, start_y + i + 1); // Dark Gray shadow
                }
            }

            // 4. Draw Window Body & Borders
            for(int i = 0; i < win_h; i++) {
                for(int j = 0; j < win_w; j++) {
                    char c = ' ';
                    if (i == 0 && j == 0) c = 0xC9; // ╔
                    else if (i == 0 && j == win_w - 1) c = 0xBB; // ╗
                    else if (i == win_h - 1 && j == 0) c = 0xC8; // ╚
                    else if (i == win_h - 1 && j == win_w - 1) c = 0xBC; // ╝
                    else if (i == 0 || i == win_h - 1) c = 0xCD; // ═
                    else if (j == 0 || j == win_w - 1) c = 0xBA; // ║
                    
                    putchar_at(c, win_col, start_x + j, start_y + i);
                }
            }

            // 5. Draw the Title Bar Content
            print_at(" AaronOS Explorer", win_col, start_x + 2, start_y);
        }
        else if (kstrcmp(input_buffer, "time") == 0) {
            read_rtc();
            char time_str[16];
            print("Clock ["); print(current_tz_name); print("]: ");
            itoa(system_time.hour, time_str, 10); print(time_str); print(":");
            if (system_time.minute < 10) print("0");
            itoa(system_time.minute, time_str, 10); print(time_str); print(":");
            if (system_time.second < 10) print("0");
            itoa(system_time.second, time_str, 10); print(time_str);
            print(" | Date: ");
            itoa(system_time.month, time_str, 10); print(time_str); print("/");
            itoa(system_time.day, time_str, 10); print(time_str); print("/");
            itoa(system_time.year, time_str, 10); print(time_str);
        }
        else if (kstrncmp(input_buffer, "tz", 2) == 0) {
            if (kstrlen(input_buffer) <= 3) {
                print("Unknown city. Defaults: amsterdam, london, newyork, tokyo");
            } 
            else {
                char* city = &input_buffer[2]; 
                
                if (kstrcmp(city, " amsterdam") == 0) {
                    current_offset = 2; 
                    kstrcpy(current_tz_name, "Amsterdam (CEST)");
                    print("Zone: Europe/Amsterdam (GMT+2)");
                } else if (kstrcmp(city, " london") == 0) {
                    current_offset = 1; 
                    kstrcpy(current_tz_name, "London (BST)");
                    print("Zone: Europe/London (GMT+1)");
                } else if (kstrcmp(city, " newyork") == 0) {
                    current_offset = -4; 
                    kstrcpy(current_tz_name, "New York (EDT)");
                    print("Zone: America/New_York (GMT-4)");
                } else if (kstrcmp(city, " tokyo") == 0) {
                    current_offset = 9; 
                    kstrcpy(current_tz_name, "Tokyo (JST)");
                    print("Zone: Asia/Tokyo (GMT+9)");
                } else {
                    print("Unknown city. Defaults: amsterdam, london, newyork, tokyo");
                }
            }
        }
        else if (kstrncmp(input_buffer, "beep ", 5) == 0) {
            int freq = katoi(&input_buffer[5]);
            if (freq > 0 && freq < 20000) {
                print("Tuning PIT to "); print(&input_buffer[5]); print(" Hz.");
                play_sound(freq); sleep(40); nosound();
            } else {
                print("Freq Out of Range (1-20000)");
            }
        }
        else if (kstrcmp(input_buffer, "beep") == 0) boot_jingle();
        else if (kstrcmp(input_buffer, "reboot") == 0) sys_reboot();
        else if (kstrcmp(input_buffer, "shutdown") == 0) {
            print_col("Powering off...", COLOR_ALERT);
            outw(0x604, 0x2000); 
        }
        else if (kstrcmp(input_buffer, "ver") == 0) {
            print_col(KERNEL_NAME, COLOR_SUCCESS); 
            print(" ["); print(KERNEL_VERSION); print("]\n");
            print("Architecture: i386 Monolithic\n");
            print("Build: "); print(KERNEL_BUILD);
        }
        else if (kstrcmp(input_buffer, "cls") == 0) clear_screen();
        else if (kstrcmp(input_buffer, "install") == 0) run_installation();
        else if (kstrcmp(input_buffer, "panic") == 0) kpanic("USER_INITIATED_TEST");
        else if (kstrcmp(input_buffer, "cpu") == 0) {
            uint32_t ebx, ecx, edx;
            asm volatile("cpuid" : "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
            char vendor[13];
            *((uint32_t*)vendor) = ebx;
            *((uint32_t*)(vendor + 4)) = edx;
            *((uint32_t*)(vendor + 8)) = ecx;
            vendor[12] = '\0';
            print("Processor: "); print_col(vendor, COLOR_HELP);
        }
        else if (kstrcmp(input_buffer, "dir") == 0) fat16_list_files();
        else if (kstrncmp(input_buffer, "cat ", 4) == 0) fat16_cat(&input_buffer[4]);
        else if (kstrncmp(input_buffer, "write ", 6) == 0) fat16_write_to_test(&input_buffer[6]);
        else if (kstrncmp(input_buffer, "echo ", 5) == 0) print(&input_buffer[5]);
        else if (kstrncmp(input_buffer, "memo ", 5) == 0) {
            fat16_write_to_test(&input_buffer[5]);
            print("Data committed to block storage.");
        }
        else if (kstrcmp(input_buffer, "music") == 0) {
            print("Audio Stream: Victory Theme");
            uint32_t notes[] = {NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5};
            uint32_t durations[] = {10, 10, 10, 30};
            play_song(notes, durations, 4);
        }
        else if (kstrcmp(input_buffer, "siren") == 0) {
            print("Generating Waveform: Siren");
            uint32_t notes[] = {880, 440, 880, 440, 880, 440};
            uint32_t durations[] = {35, 35, 35, 35, 35, 35};
            play_song(notes, durations, 6);
        }
        else {
            print("Unknown command. Type help for commands.");
        }
    }
    
    print("\nAaronOS> ");
    input_ptr = 0;
    execute_flag = 0;
    prompt_limit = cursor_x;
    update_cursor();
}

/* ========================================================================== */
/* 11. SEGMENTATION & DESCRIPTORS                                             */
/* ========================================================================== */

struct gdt_entry {
    uint16_t limit_low; uint16_t base_low;
    uint8_t  base_middle; uint8_t  access;
    uint8_t  granularity; uint8_t  base_high;
} __attribute__((packed)) gdt[3];

struct gdt_ptr {
    uint16_t limit; uint32_t base;
} __attribute__((packed)) gp;

void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].access = access;
}

void init_gdt() {
    gp.limit = (sizeof(struct gdt_entry) * 3) - 1;
    gp.base = (uint32_t)&gdt;
    gdt_set_gate(0, 0, 0, 0, 0);                
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); 
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); 
}

struct idt_entry {
    uint16_t base_lo, sel;
    uint8_t always0, flags;
    uint16_t base_hi;
} __attribute__((packed)) idt[256];

struct idt_ptr {
    uint16_t limit; uint32_t base;
} __attribute__((packed)) idtp;

extern void load_idt(uint32_t);

void init_idt() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1; 
    idtp.base = (uint32_t)&idt;
    kmemset(idt, 0, sizeof(idt));
    
    uint32_t th = (uint32_t)timer_handler_asm;
    idt[32].base_lo = th & 0xFFFF;
    idt[32].base_hi = (th >> 16) & 0xFFFF;
    idt[32].sel = 0x08; idt[32].always0 = 0; idt[32].flags = 0x8E;

    uint32_t kh = (uint32_t)keyboard_handler_asm;
    idt[33].base_lo = kh & 0xFFFF;
    idt[33].base_hi = (kh >> 16) & 0xFFFF;
    idt[33].sel = 0x08; idt[33].always0 = 0; idt[33].flags = 0x8E;
    
    load_idt((uint32_t)&idtp);
}

/* ========================================================================== */
/* 12. KERNEL ENTRY POINT                                                     */
/* ========================================================================== */

void kernel_main() {
    init_gdt();
    
    outb(0x20, 0x11); io_wait(); outb(0x21, 0x20); io_wait();
    outb(0x21, 0x04); io_wait(); outb(0x21, 0x01); io_wait();
    outb(0xA0, 0x11); io_wait(); outb(0xA1, 0x28); io_wait();
    outb(0xA1, 0x02); io_wait(); outb(0xA1, 0x01); io_wait();
    
    outb(0x21, 0xFC); 
    outb(0xA1, 0xFF);
    
    init_idt();
    init_timer(100); 

    sys_stats.uptime_ticks = 0;
    sys_stats.total_commands = 0;
    sys_stats.speaker_state = 0;
    
    clear_screen();
    cursor_y = 0;
    update_cursor();
    
    asm volatile("sti");
    boot_jingle();
    
    print("Welcome to AaronOS! \n Use help for commands.\n");
    print("AaronOS> ");

    while (1) { 
        if (execute_flag == 1 ) {
            process_shell(); 
            execute_flag = 0;
        }
        asm volatile("hlt"); 
    }
}

/**
 * =============================================================================
 * END OF KERNEL.C
 * =============================================================================
 */