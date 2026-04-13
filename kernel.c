/**
 * =============================================================================
 * AARONOS KERNEL - FULL MONOLITHIC BUILD 
 * =============================================================================
 * VERSION: 3.9.0-STABLE
 * ARCHITECTURE: x86 (i386)
 * DESCRIPTION: High-stability monolithic kernel with persistent storage hooks,
 * PIT-based audio engine, deep hardware monitoring, comprehensive 
 * hardware interrupt handling, an extended utility shell, and an advanced
 * 500-line scrollback virtual terminal.
 * 
 * NOTE: IO, FAT16, Keyboard, Memory, and Installer logic are isolated into 
 * respective modules. This file only contains kernel core routines, string/math 
 * utilities, terminal rendering, and the command interpreter.
 * =============================================================================
 */

#include <stdint.h>
#include <stddef.h>

/* Bring in the hardware IO and FAT16 structs from external files */
#include "io.h"
#include "fat16.h"

/* ========================================================================== */
/* 1. KERNEL SYSTEM IDENTITY & MACROS                                         */
/* ========================================================================== */

#define KERNEL_NAME        "AaronOS"
#define KERNEL_VERSION     "3.9.0-STABLE"
#define KERNEL_BUILD       "2026-04-13-QEMU/UTM"

#define VIDEO_ADDR         0xB8000
#define SCREEN_WIDTH       80
#define SCREEN_HEIGHT      25
#define MAX_SCROLLBACK     500

#define PIT_CHANNEL_0      0x40
#define PIT_CHANNEL_1      0x41
#define PIT_CHANNEL_2      0x42
#define PIT_COMMAND        0x43

#define PC_SPEAKER_PORT    0x61
#define KBD_STATUS_PORT    0x64
#define KBD_DATA_PORT      0x60

#define CMOS_ADDRESS       0x70
#define CMOS_DATA          0x71

/* Base Color Palettes */
#define COLOR_DEFAULT      0x07 
#define COLOR_SUCCESS      0x0A 
#define COLOR_HELP         0x0B 
#define COLOR_ALERT        0x0E 
#define COLOR_PANIC        0x4F 
#define COLOR_AUDIO        0x0D 
#define COLOR_MATRIX       0x0A
#define COLOR_BOOT         0x03
#define COLOR_WARN         0x0E

/* Audio Frequencies */
#define NOTE_C4            261
#define NOTE_D4            294
#define NOTE_E4            329
#define NOTE_F4            349
#define NOTE_G4            392
#define NOTE_A4            440
#define NOTE_B4            493
#define NOTE_C5            523
#define NOTE_D5            587
#define NOTE_E5            659
#define NOTE_F5            698
#define NOTE_G5            784

/* TUI Visual Elements */
#define TUI_COLOR       0x1F  
#define BOX_HLINE       0xCD  
#define BOX_VLINE       0xBA  
#define BOX_TL          0xC9  
#define BOX_TR          0xBB  
#define BOX_BL          0xC8  
#define BOX_BR          0xBC  

/* ========================================================================== */
/* 2. KERNEL GLOBAL STATE                                                     */
/* ========================================================================== */

uint16_t terminal_buffer[MAX_SCROLLBACK][SCREEN_WIDTH]; 
int scroll_offset = 0;
int current_row = 0;
int current_col = 0;
int prompt_limit = 0;
uint8_t current_term_color = COLOR_DEFAULT; 
uint16_t* video_mem = (uint16_t*)VIDEO_ADDR;

volatile uint32_t timer_ticks = 0; 
char input_buffer[256];             
int input_ptr = 0;                  
volatile int execute_flag = 0;      
int in_gui_mode = 0; 

int current_offset = 2; 
char current_tz_name[32] = "Amsterdam (CEST)";

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

kernel_health_t sys_stats;
rtc_time_t system_time;

/* ========================================================================== */
/* 3. EXTERNAL REFERENCES                                                     */
/* ========================================================================== */

extern void fat16_list_files();
extern void fat16_cat(char* name);
extern void fat16_write_to_test(char* content);
extern void fat16_create_file(char* name);
extern void fat16_delete_file(char* name);
extern void fat16_rename_file(char* oldname, char* newname);
extern void fat16_format_drive();
extern void run_installation();
extern void keyboard_handler_asm();
extern void timer_handler_asm();
extern void load_idt(uint32_t ptr);

/* ========================================================================== */
/* 4. FORWARD DECLARATIONS                                                    */
/* ========================================================================== */

void nosound();
void sleep(uint32_t ticks);
void play_sound(uint32_t nFrequence);
void update_cursor_relative();
void clear_screen();
void print(const char* str);
void print_col(const char* str, uint8_t col);
void putchar_col(char c, uint8_t color);
void putchar_at(char c, uint8_t color, int x, int y);
void print_at(const char* str, uint8_t color, int x, int y);
void kpanic(const char* message);
void sys_reboot();
void init_timer(uint32_t frequency);
void refresh_screen();
void read_rtc();
void process_shell();
void show_credits();
void run_matrix();
void print_stats();
void log_boot_hal(const char* msg);

int kabs(int val);
int kpow(int base, int exp);
int k_rand();
void itoa(int num, char* str, int base);

/* ========================================================================== */
/* 5. CORE STRING & MATH LIBRARIES                                            */
/* ========================================================================== */

int kabs(int val) {
    return val < 0 ? -val : val;
}

int kpow(int base, int exp) {
    int res = 1;
    for (int i = 0; i < exp; i++) {
        res *= base;
    }
    return res;
}

static uint32_t rand_seed = 123456789;
int k_rand() {
    rand_seed = (rand_seed * 1103515245 + 12345) & 0x7FFFFFFF;
    return rand_seed;
}

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

int katohex(const char* str) {
    int res = 0;
    int i = 0;
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) i = 2;
    for (; str[i] != '\0'; ++i) {
        if (str[i] >= '0' && str[i] <= '9') {
            res = res * 16 + (str[i] - '0');
        } else if (str[i] >= 'a' && str[i] <= 'f') {
            res = res * 16 + (str[i] - 'a' + 10);
        } else if (str[i] >= 'A' && str[i] <= 'F') {
            res = res * 16 + (str[i] - 'A' + 10);
        } else {
            break;
        }
    }
    return res;
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

char* kstrchr(const char *s, int c) {
    while (*s != (char)c) {
        if (!*s++) {
            return NULL;
        }
    }
    return (char *)s;
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

    /* Apply timezone offset safely */
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
/* 7. VGA TERMINAL ENGINE & 500-LINE SCROLLING LOGIC                          */
/* ========================================================================== */

void scroll_up() {
    if (scroll_offset > 0) {
        scroll_offset--;
        refresh_screen();
    }
}

void scroll_down() {
    // Cannot scroll down past where the bottom of the screen meets the current typing row
    if (scroll_offset < (MAX_SCROLLBACK - SCREEN_HEIGHT)) {
        if (scroll_offset < current_row - SCREEN_HEIGHT + 1) {
            scroll_offset++;
            refresh_screen();
        }
    }
}

void auto_scroll() {
    if (current_row >= MAX_SCROLLBACK) {
        // Shift entire 500-line buffer up by 1
        for (int i = 1; i < MAX_SCROLLBACK; i++) {
            for (int j = 0; j < SCREEN_WIDTH; j++) {
                terminal_buffer[i-1][j] = terminal_buffer[i][j];
            }
        }
        // Clear the bottom line
        for (int j = 0; j < SCREEN_WIDTH; j++) {
            terminal_buffer[MAX_SCROLLBACK - 1][j] = ' ' | (current_term_color << 8);
        }
        current_row = MAX_SCROLLBACK - 1;
    }
    
    // Auto adjust scroll offset to lock to the bottom typing area
    if (current_row >= scroll_offset + SCREEN_HEIGHT) {
        scroll_offset = current_row - SCREEN_HEIGHT + 1;
    }
}

void refresh_screen() {
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            int buffer_line = y + scroll_offset;
            if (buffer_line < MAX_SCROLLBACK) {
                video_mem[y * SCREEN_WIDTH + x] = terminal_buffer[buffer_line][x];
            } else {
                video_mem[y * SCREEN_WIDTH + x] = ' ' | (current_term_color << 8); 
            }
        }
    }
    update_cursor_relative();
}

void update_cursor_relative() {
    // visual_row determines where the physical cursor blinks on the 80x25 screen
    int visual_row = current_row - scroll_offset;
    if (visual_row >= 0 && visual_row < SCREEN_HEIGHT) {
        uint16_t pos = (visual_row * SCREEN_WIDTH) + current_col;
        outb(0x3D4, 0x0F);
        outb(0x3D5, (uint8_t)(pos & 0xFF));
        outb(0x3D4, 0x0E);
        outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
    } else {
        // Hide cursor off-screen if user scrolled away from the active prompt
        uint16_t pos = SCREEN_HEIGHT * SCREEN_WIDTH; 
        outb(0x3D4, 0x0F);
        outb(0x3D5, (uint8_t)(pos & 0xFF));
        outb(0x3D4, 0x0E);
        outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
    }
}

void putchar_col(char c, uint8_t color) {
    if (c == '\n') {
        current_col = 0;
        current_row++;
    } else if (c == '\b') {
        if (current_col > prompt_limit) {
            current_col--;
            terminal_buffer[current_row][current_col] = ' ' | (color << 8);
        }
    } else {
        terminal_buffer[current_row][current_col] = (uint16_t)c | (color << 8);
        current_col++;
        if (current_col >= SCREEN_WIDTH) {
            current_col = 0;
            current_row++;
        }
    }
    auto_scroll();
    refresh_screen();
}

void print(const char* str) {
    for (int i = 0; str[i]; i++) {
        putchar_col(str[i], current_term_color);
    }
}

void print_col(const char* str, uint8_t col) {
    for (int i = 0; str[i]; i++) {
        putchar_col(str[i], col);
    }
}

void clear_screen() {
    for (int i = 0; i < MAX_SCROLLBACK; i++) {
        for (int j = 0; j < SCREEN_WIDTH; j++) {
            terminal_buffer[i][j] = ' ' | (current_term_color << 8);
        }
    }
    current_col = 0; 
    current_row = 0; 
    scroll_offset = 0;
    refresh_screen();
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

void timer_callback() {
    timer_ticks++;
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
        // Brief pause between notes to make melodies clear
        for(volatile int d = 0; d < 500000; d++); 
    }
}

void boot_jingle() {
    play_sound(523); sleep(25); 
    play_sound(659); sleep(25); 
    play_sound(783); sleep(25); 
    play_sound(1046); sleep(45); 
    nosound();
}

/* ========================================================================== */
/* 9. SYSTEM RECOVERY & DIAGNOSTICS                                           */
/* ========================================================================== */

void log_boot_hal(const char* msg) {
    print_col("[HAL] ", COLOR_BOOT);
    print(msg);
    print_col(" - OK\n", COLOR_SUCCESS);
}

void kpanic(const char* message) {
    kmemset(video_mem, 0, SCREEN_WIDTH * SCREEN_HEIGHT * 2);
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        video_mem[i] = (uint16_t)' ' | (COLOR_PANIC << 8);
    }
    current_col = 0; 
    current_row = 0; 
    scroll_offset = 0;
    
    print_at("CRITICAL_KERNEL_HALT (0xDEADBEEF)", COLOR_PANIC, 0, 0);
    print_at("The system has been halted to prevent hardware damage.", COLOR_PANIC, 0, 1);
    
    print_at("REASON: ", COLOR_PANIC, 0, 3); 
    print_at(message, COLOR_PANIC, 8, 3);
    
    print_at("PROCESSOR STATE DUMP:", COLOR_PANIC, 0, 5);
    print_at("EAX: 00000000   EBX: 00000000", COLOR_PANIC, 2, 6);
    print_at("ECX: 00000000   EDX: 00000000", COLOR_PANIC, 2, 7);
    print_at("ESI: 00000000   EDI: 00000000", COLOR_PANIC, 2, 8);
    
    print_at("Please capture this screen and submit a bug report.", COLOR_PANIC, 0, 10);
    print_at("Press RESET on your machine to restart.", COLOR_PANIC, 0, 12);
    
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

void print_stats() {
    char buf[16];
    print_col("\n--- AaronOS Engine Health ---\n", COLOR_HELP);
    print("Uptime Ticks:   "); itoa(timer_ticks, buf, 10); print(buf);
    print("\nCommands Run:   "); itoa(sys_stats.total_commands, buf, 10); print(buf);
    print("\nSpeaker Status: "); print(sys_stats.speaker_state ? "ACTIVE" : "IDLE");
    print("\nColor Pallet:   0x"); itoa(current_term_color, buf, 16); print(buf);
    print("\nTerminal Size:  "); itoa(MAX_SCROLLBACK, buf, 10); print(buf); print(" lines capacity");
    print("\nTimezone Offset:"); itoa(current_offset, buf, 10); print(buf); print(" hours");
    print("\n-----------------------------\n");
}

/* ========================================================================== */
/* 10. VISUALS & ENTERTAINMENT                                                */
/* ========================================================================== */

void show_credits() {
    print_col("\n  _____                      ____   _____ \n", COLOR_HELP);
    print_col(" |  _  | ___  ___  ___ ___  |    | |   __|\n", COLOR_HELP);
    print_col(" |     || . ||  _|| . |   | |  |  ||__   |\n", COLOR_HELP);
    print_col(" |__|__||_  ||_|  |___|_|_| |____/ |_____|\n", COLOR_HELP);
    print_col("        |___|  BUILD: ", COLOR_HELP); 
    print_col(KERNEL_BUILD, COLOR_HELP);
    print_col("\n\n", COLOR_DEFAULT);
    print(" Lead Developer: Aaron\n");
    print(" Kernel Version: "); print(KERNEL_VERSION); print("\n");
}

void run_matrix() {
    clear_screen();
    for(int i = 0; i < 400; i++) {
        int x = (timer_ticks * 7) % SCREEN_WIDTH; 
        int y = (timer_ticks / 3) % SCREEN_HEIGHT;
        char c = (timer_ticks % 94) + 33; 
        putchar_at(c, COLOR_MATRIX, x, y);
        sleep(1);
        
        /* Cleanup fading tail effect */
        if (y > 0) putchar_at(' ', COLOR_MATRIX, x, y - 1);
    }
    clear_screen();
}

void launch_tui() {
    in_gui_mode = 1;
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        video_mem[i] = (uint16_t)0xB1 | (0x30 << 8); 
    }
    int win_w = 60, win_h = 15;
    int start_x = (80 - win_w) / 2, start_y = (25 - win_h) / 2;
    uint8_t win_col = 0x70; 

    // Draw Drop Shadow
    for(int i = 1; i < win_h; i++) {
        for(int j = 1; j < win_w; j++) {
            putchar_at(' ', 0x08, start_x + j + 1, start_y + i + 1); 
        }
    }
    
    // Draw Window Box Boundaries
    for(int i = 0; i < win_h; i++) {
        for(int j = 0; j < win_w; j++) {
            char c = ' ';
            if (i == 0 && j == 0) c = BOX_TL; 
            else if (i == 0 && j == win_w - 1) c = BOX_TR; 
            else if (i == win_h - 1 && j == 0) c = BOX_BL; 
            else if (i == win_h - 1 && j == win_w - 1) c = BOX_BR; 
            else if (i == 0 || i == win_h - 1) c = BOX_HLINE; 
            else if (j == 0 || j == win_w - 1) c = BOX_VLINE; 
            putchar_at(c, win_col, start_x + j, start_y + i);
        }
    }
    
    // Window Title
    print_at(" AaronOS Explorer ", win_col | 0x0F, start_x + 2, start_y);
    
    // UI Inner Contents
    print_at(" Name              Size      Type ", win_col, start_x + 2, start_y + 2);
    print_at("----------------------------------", win_col, start_x + 2, start_y + 3);
    print_at(" KERNEL.BIN       1245 KB    SYS  ", win_col, start_x + 2, start_y + 4);
    print_at(" SYSTEM.CFG          4 KB    CFG  ", win_col, start_x + 2, start_y + 5);
    print_at(" README.TXT          1 KB    TXT  ", win_col, start_x + 2, start_y + 6);
    
    print_at(" [ Press CTRL-T to return to terminal ] ", win_col | 0x0E, start_x + 10, start_y + win_h - 2);
}

/* ========================================================================== */
/* 11. THE COMMAND INTERPRETER & SHELL                                        */
/* ========================================================================== */

void print_help() {
    print_col("--- AaronOS Command List ---\n", COLOR_HELP);
    print("install   - Run HDD deployment\n");
    print("reboot    - Warm restart\n");
    print("shutdown  - ACPI Power off\n");
    print("ver       - Show system version\n");
    print("time      - Display hardware clock\n");
    print("tz [city] - Set timezone (e.g. tz amsterdam)\n");
    print("cls       - Clear terminal window\n");
    print("panic     - Test kernel crash\n");
    print("beep [f]  - Play tone (ex: beep 440)\n");
    print("dir       - List disk contents\n");
    print("ls        - Enhanced colorized list\n");
    print("cat [f]   - Read text file\n");
    print("write [t] - Append text to disk\n");
    print("touch [f] - Create an empty file\n");
    print("rm [f]    - Delete a file\n");
    print("rename    - Rename a file (syntax: rename old new)\n");
    print("echo [t]  - Print text to screen\n");
    print("cpu       - Show hardware vendor\n");
    print("music     - Plays a bit of music\n");
    print("siren     - Sounds a siren\n");
    print("credits   - Show OS build information\n");
    print("stats     - Show OS health and uptime\n");
    print("rand      - Generate pseudo-random number\n");
    print("matrix    - Enter the matrix\n");
    print("color [h] - Change text color (hex, e.g. color 0A)\n");
    print("calc      - Basic math (e.g. calc 5 + 10)\n");
    print("gui       - Switches to TUI mode\n");
}

void process_shell() {
    print("\n");
    sys_stats.total_commands++;

    if (input_ptr > 0) {
        input_buffer[input_ptr] = '\0';
        
        /* --------------------------------------------------------- */
        /* SYSTEM & INFO COMMANDS                                    */
        /* --------------------------------------------------------- */
        if (kstrcmp(input_buffer, "help") == 0) {
            print_help();
        }
        else if (kstrcmp(input_buffer, "gui") == 0) {
            launch_tui();
        }
        else if (kstrcmp(input_buffer, "ver") == 0) {
            print_col(KERNEL_NAME, COLOR_SUCCESS); 
            print(" ["); print(KERNEL_VERSION); print("]\n");
            print("Architecture: i386 Monolithic\n");
            print("Build: "); print(KERNEL_BUILD);
        }
        else if (kstrcmp(input_buffer, "reboot") == 0) {
            sys_reboot();
        }
        else if (kstrcmp(input_buffer, "shutdown") == 0) {
            print_col("Powering off...", COLOR_ALERT);
            outw(0x604, 0x2000); 
        }
        else if (kstrcmp(input_buffer, "cls") == 0) {
            clear_screen();
        }
        else if (kstrcmp(input_buffer, "install") == 0) {
            run_installation();
        }
        else if (kstrcmp(input_buffer, "panic") == 0) {
            kpanic("USER_INITIATED_TEST");
        }
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
        else if (kstrcmp(input_buffer, "credits") == 0) {
            show_credits();
        }
        else if (kstrcmp(input_buffer, "stats") == 0) {
            print_stats();
        }

        /* --------------------------------------------------------- */
        /* TIME & RTC COMMANDS                                       */
        /* --------------------------------------------------------- */
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
            char* city = &input_buffer[2]; 
            if (kstrcmp(city, " amsterdam") == 0) { 
                current_offset = 2; 
                kstrcpy(current_tz_name, "Amsterdam (CEST)"); 
                print("Timezone set to Amsterdam.");
            }
            else if (kstrcmp(city, " london") == 0) { 
                current_offset = 1; 
                kstrcpy(current_tz_name, "London (BST)"); 
                print("Timezone set to London.");
            }
            else if (kstrcmp(city, " newyork") == 0) { 
                current_offset = -4; 
                kstrcpy(current_tz_name, "New York (EDT)"); 
                print("Timezone set to New York.");
            }
            else if (kstrcmp(city, " tokyo") == 0) { 
                current_offset = 9; 
                kstrcpy(current_tz_name, "Tokyo (JST)"); 
                print("Timezone set to Tokyo.");
            }
            else print("Unknown city. Defaults: amsterdam, london, newyork, tokyo");
        }

        /* --------------------------------------------------------- */
        /* FILESYSTEM COMMANDS (MAPPED TO EXTERNAL FAT16)            */
        /* --------------------------------------------------------- */
        else if (kstrcmp(input_buffer, "dir") == 0 || kstrcmp(input_buffer, "ls") == 0) {
            print_col("DIRECTORY LISTING:\n", COLOR_HELP);
            fat16_list_files(); 
        }
        else if (kstrncmp(input_buffer, "cat ", 4) == 0) {
            fat16_cat(&input_buffer[4]);
        }
        else if (kstrncmp(input_buffer, "write ", 6) == 0) {
            fat16_write_to_test(&input_buffer[6]);
        }
        else if (kstrncmp(input_buffer, "touch ", 6) == 0) {
            fat16_create_file(&input_buffer[6]);
        }
        else if (kstrncmp(input_buffer, "rm ", 3) == 0) {
            fat16_delete_file(&input_buffer[3]);
        }
        else if (kstrncmp(input_buffer, "rename ", 7) == 0) {
            char* args = &input_buffer[7];
            char* space = kstrchr(args, ' ');
            if (space) {
                *space = '\0';
                char* new_name = space + 1;
                fat16_rename_file(args, new_name);
            } else {
                print("Syntax: rename [old] [new]");
            }
        }

        /* --------------------------------------------------------- */
        /* UTILITIES & MATH                                          */
        /* --------------------------------------------------------- */
        else if (kstrncmp(input_buffer, "echo ", 5) == 0) {
            print(&input_buffer[5]);
        }
        else if (kstrcmp(input_buffer, "rand") == 0) {
            char buf[16];
            itoa(k_rand(), buf, 10);
            print("Entropy Output: "); print(buf);
        }
        else if (kstrncmp(input_buffer, "color ", 6) == 0) {
            int new_color = katohex(&input_buffer[6]);
            if (new_color >= 0 && new_color <= 0xFF) {
                current_term_color = (uint8_t)new_color;
                print("Terminal color updated.");
            } else {
                print("Invalid color code. Use hex format (e.g. color 0A)");
            }
        }
        else if (kstrncmp(input_buffer, "calc ", 5) == 0) {
            char* expr = &input_buffer[5];
            int a = 0, b = 0, res = 0, i = 0;
            char op = 0;
            
            while(expr[i] == ' ') i++;
            a = katoi(&expr[i]);
            
            while(expr[i] >= '0' && expr[i] <= '9') i++;
            while(expr[i] == ' ') i++;
            
            if (expr[i] == '+' || expr[i] == '-' || expr[i] == '*' || expr[i] == '/' || expr[i] == '^') {
                op = expr[i];
                i++;
                while(expr[i] == ' ') i++;
                b = katoi(&expr[i]);
                
                if (op == '+') res = a + b;
                else if (op == '-') res = a - b;
                else if (op == '*') res = a * b;
                else if (op == '^') res = kpow(a, b);
                else if (op == '/') {
                    if (b != 0) res = a / b;
                    else print("ERR: Div by 0");
                }
                
                if (op != '/' || b != 0) {
                    char buf[16];
                    itoa(res, buf, 10);
                    print("Result: "); print(buf);
                }
            } else {
                print("Syntax: calc [a] [+|-|*|/|^] [b]");
            }
        }

        /* --------------------------------------------------------- */
        /* MULTIMEDIA & VISUALS                                      */
        /* --------------------------------------------------------- */
        else if (kstrncmp(input_buffer, "beep ", 5) == 0) {
            int freq = katoi(&input_buffer[5]);
            if (freq > 0 && freq < 20000) {
                print("Tuning PIT to "); print(&input_buffer[5]); print(" Hz.");
                play_sound(freq); sleep(40); nosound();
            } else {
                print("Freq Out of Range (1-20000)");
            }
        }
        else if (kstrcmp(input_buffer, "beep") == 0) {
            boot_jingle();
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
        else if (kstrcmp(input_buffer, "matrix") == 0) {
            run_matrix();
        }
        else {
            print("Unknown command. Type help for commands.");
        }
    }
    
    print("\nAaronOS> ");
    input_ptr = 0;
    execute_flag = 0;
    prompt_limit = current_col;
    update_cursor_relative();
}

/* ========================================================================== */
/* 12. SEGMENTATION (GDT) & INTERRUPTS (IDT)                                  */
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
    
    // Null Gate
    gdt_set_gate(0, 0, 0, 0, 0);                
    // Code Gate
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); 
    // Data Gate
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

void init_idt() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1; 
    idtp.base = (uint32_t)&idt;
    kmemset(idt, 0, sizeof(idt));
    
    // Wire up the Timer Interrupt Handler
    uint32_t th = (uint32_t)timer_handler_asm;
    idt[32].base_lo = th & 0xFFFF;
    idt[32].base_hi = (th >> 16) & 0xFFFF;
    idt[32].sel = 0x08; idt[32].always0 = 0; idt[32].flags = 0x8E;

    // Wire up the Keyboard Interrupt Handler
    uint32_t kh = (uint32_t)keyboard_handler_asm;
    idt[33].base_lo = kh & 0xFFFF;
    idt[33].base_hi = (kh >> 16) & 0xFFFF;
    idt[33].sel = 0x08; idt[33].always0 = 0; idt[33].flags = 0x8E;
    
    load_idt((uint32_t)&idtp);
}

/* ========================================================================== */
/* 13. KERNEL ENTRY POINT                                                     */
/* ========================================================================== */

void kernel_main() {
    // 1. Initial State setup
    current_col = 0;
    current_row = 0;
    scroll_offset = 0;
    clear_screen();
    
    print_col("\n[ AaronOS Boot Sequence Initiated ]\n\n", COLOR_HELP);

    // 2. Hardware and Architecture Initialization
    init_gdt();
    log_boot_hal("Global Descriptor Table (GDT) Initialized");

    // Remap the 8259 PIC
    outb(0x20, 0x11); io_wait(); outb(0x21, 0x20); io_wait();
    outb(0x21, 0x04); io_wait(); outb(0x21, 0x01); io_wait();
    outb(0xA0, 0x11); io_wait(); outb(0xA1, 0x28); io_wait();
    outb(0xA1, 0x02); io_wait(); outb(0xA1, 0x01); io_wait();
    
    // Mask interrupts initially
    outb(0x21, 0xFC); 
    outb(0xA1, 0xFF);
    log_boot_hal("8259 Programmable Interrupt Controller Remapped");

    init_idt();
    log_boot_hal("Interrupt Descriptor Table (IDT) Initialized");

    init_timer(100); 
    log_boot_hal("Programmable Interval Timer (PIT) configured at 100Hz");

    // 3. Subsystem Preparations
    sys_stats.uptime_ticks = 0;
    sys_stats.total_commands = 0;
    sys_stats.speaker_state = 0;
    
    log_boot_hal("Virtual Terminal Scrollback Buffer Allocated (500 Lines)");
    
    // Wait briefly so user can see boot logs
    for(volatile int i=0; i<10000000; i++); 
    
    // Enable Hardware Interrupts
    asm volatile("sti");
    log_boot_hal("Hardware Interrupts Enabled");

    // Give another brief pause
    for(volatile int i=0; i<20000000; i++); 
    clear_screen();
    
    // 4. Welcome Sequence
    boot_jingle();
    print("Welcome to AaronOS! \n Use help for commands.\n");
    print("AaronOS> ");
    
    // Set prompt limit so user can't backspace into the AaronOS> string
    prompt_limit = current_col;

    // 5. Idle Execution Loop
    while (1) { 
        // Execute flag is set by the external keyboard handler 
        // when the user hits Enter ('\n')
        if (execute_flag == 1 ) {
            process_shell(); 
            execute_flag = 0;
        }
        
        // Halt CPU until next interrupt fires (saves power / CPU cycles)
        asm volatile("hlt"); 
    }
}

/**
 * =============================================================================
 * END OF KERNEL.C
 * =============================================================================
 */