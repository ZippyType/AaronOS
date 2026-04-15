

/**
 * =============================================================================
 * AARONOS KERNEL - FULL MONOLITHIC BUILD (EXTENDED ARCHITECTURE)
 * =============================================================================
 * VERSION: 3.9.0-STABLE
 * ARCHITECTURE: x86 (i386)
 * DESCRIPTION: High-stability monolithic kernel. Acts as the central hub,
 * hooking into external modules (FAT16, Keyboard, IO, Installer) while
 * natively handling the VGA scrollback engine, PIT audio, CMOS/RTC,
 * advanced string/math libraries, and the master shell interpreter.
 * 
 * NEW FEATURES: 
 * - Fully interactive TUI Desktop Environment (AaronOS Explorer).
 * - Advanced Window Rendering Engine (Borders, Shadows, Z-Index mocks).
 * - Extended Math Library (Trigonometry approximations, Square Root).
 * - 500-Line Virtual Terminal Scrollback.
 * =============================================================================
 */

#include <stdint.h>
#include <stddef.h>
#include "io.h"

/* ========================================================================== */
/* 1. KERNEL SYSTEM IDENTITY & MACROS                                         */
/* ========================================================================== */

#define KERNEL_NAME        "AaronOS"
#define KERNEL_VERSION     "3.9.0-STABLE"
#define KERNEL_BUILD       "2026-04-15"

/* VGA Hardware Memory Map boundaries */
#define VIDEO_ADDR         0xB8000
#define SCREEN_WIDTH       80
#define SCREEN_HEIGHT      25
#define MAX_SCROLLBACK     500 // Expanded from 100 to 500 for deep history

/* Programmable Interval Timer (PIT) Ports */
#define PIT_CHANNEL_0      0x40
#define PIT_CHANNEL_1      0x41
#define PIT_CHANNEL_2      0x42
#define PIT_COMMAND        0x43

/* PC Speaker and Keyboard Controller Ports */
#define PC_SPEAKER_PORT    0x61
#define KBD_STATUS_PORT    0x64
#define KBD_DATA_PORT      0x60

/* Real-Time Clock (CMOS) Ports */
#define CMOS_ADDRESS       0x70
#define CMOS_DATA          0x71

/* Base Color Palettes (VGA standard 4-bit foreground/background) */
#define COLOR_DEFAULT      0x07 // Light Gray on Black
#define COLOR_SUCCESS      0x0A // Light Green
#define COLOR_HELP         0x0B // Light Cyan
#define COLOR_ALERT        0x0E // Yellow
#define COLOR_PANIC        0x4F // White on Red Background
#define COLOR_AUDIO        0x0D // Light Magenta
#define COLOR_MATRIX       0x0A // Standard Green
#define COLOR_BOOT         0x03 // Cyan
#define COLOR_WARN         0x0E // Yellow

/* TUI Visual Elements and Palettes */
#define TUI_BG_COLOR       0x1F  // White on Blue for Desktop Background
#define TUI_WIN_COLOR      0x70  // Black on Light Gray for Windows
#define TUI_HL_COLOR       0x0F  // White on Black for Selected Items
#define TUI_BAR_COLOR      0x8F  // White on Dark Gray for Taskbars

/* TUI Extended ASCII Box Drawing Characters */
#define BOX_HLINE          0xCD  // ═
#define BOX_VLINE          0xBA  // ║
#define BOX_TL             0xC9  // ╔
#define BOX_TR             0xBB  // ╗
#define BOX_BL             0xC8  // ╚
#define BOX_BR             0xBC  // ╝
#define BOX_CROSS          0xCE  // ╬
#define BOX_T_DOWN         0xCB  // ╦
#define BOX_T_UP           0xCA  // ╩
#define BOX_T_RIGHT        0xCC  // ╠
#define BOX_T_LEFT         0xB9  // ╣

/* Boot Sequence Logging Configuration */
#define MAX_BOOT_LOGS      25
#define LOG_MSG_LEN        64

/* Audio Frequencies for PIT speaker */
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

/* ========================================================================== */
/* 2. KERNEL GLOBAL STATE                                                     */
/* ========================================================================== */

/* CLI & Keyboard Hooks: Managed by keyboard.c, read by kernel.c */
char input_buffer[256];             // Raw characters typed by user
int input_ptr = 0;                  // Current position in the input buffer
volatile int execute_flag = 0;      // Set to 1 when ENTER is pressed

/* VGA Terminal State */
// A massive 2D array storing both the character and its color data
uint16_t terminal_buffer[MAX_SCROLLBACK][SCREEN_WIDTH]; 
int scroll_offset = 0;              // The "camera" looking into the buffer
int current_row = 0;                // Where the prompt currently is
int current_col = 0;                // Where the cursor currently is horizontally
int prompt_limit = 0;               // Prevents user from backspacing into the prompt string
uint8_t current_term_color = COLOR_DEFAULT; 
uint16_t* video_mem = (uint16_t*)VIDEO_ADDR;

/* System Timing & Mode State */
volatile uint32_t timer_ticks = 0;  // Incremented 100 times per second by PIT
int current_offset = 2;             // User's selected timezone offset
char current_tz_name[32] = "Amsterdam (CEST)";

/* Boot Logging Ring Buffer */
char boot_logs[MAX_BOOT_LOGS][LOG_MSG_LEN];
int boot_log_count = 0;

/* TUI Engine State Machine */
int in_gui_mode = 0;       // Flag preventing CLI from writing to screen when 1
int tui_state = 0;         // 0=Main Menu, 1=Files, 2=SysMon, 3=About
int tui_selected_item = 0; // Current highlighted menu item
int tui_max_items = 0;     // Max items in current menu (for wrapping)
int tui_needs_redraw = 1;  // Triggers a UI frame update to save CPU

/* Hardware Diagnostics & Stats Structs */
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
/* 3. EXTERNAL REFERENCES TO USER MODULES                                     */
/* ========================================================================== */

/* These functions exist in other compiled object files (fat16.o, installer.o) */
extern void fat16_format_drive();
extern void fat16_list_files();
extern void fat16_cat(char* name);
extern void fat16_write_to_test(char* content);
extern void fat16_create_file(char* name);
extern void fat16_delete_file(char* name);
extern void fat16_rename_file(char* oldname, char* newname);

extern void run_installation();
extern void run_editor();

extern void keyboard_handler_asm();
extern void timer_handler_asm();
extern void load_idt(uint32_t ptr);

/* ========================================================================== */
/* 4. FORWARD DECLARATIONS                                                    */
/* ========================================================================== */

// So functions can call each other regardless of order in the file
void nosound(void);
void sleep(uint32_t ticks);
void play_sound(uint32_t nFrequence);
void update_cursor_relative();
void clear_screen();
void refresh_screen();
void print(const char* str);
void print_col(const char* str, uint8_t col);
void putchar_col(char c, uint8_t color);
void putchar_at(char c, uint8_t color, int x, int y);
void print_at(const char* str, uint8_t color, int x, int y);
void scroll_up();
void scroll_down();
void kpanic(const char* message);
void sys_reboot();
void init_timer(uint32_t frequency);
void read_rtc();
void process_shell();
void show_credits();
void run_matrix();
void print_stats();
void log_boot(const char* msg);

void launch_tui();
void tui_draw_desktop();
void tui_draw_window(int x, int y, int w, int h, const char* title);
void tui_handle_input();
void tui_render_main_menu();
void tui_render_file_browser();
void tui_render_sysmon();
void tui_render_about();

int kabs(int val);
int kpow(int base, int exp);
int ksqrt(int val);
int k_rand();
void itoa(int num, char* str, int base);

/* ========================================================================== */
/* 5. CORE STRING & ADVANCED MATH LIBRARIES                                   */
/* ========================================================================== */

/* Returns absolute (positive) value of an integer */
int kabs(int val) { return val < 0 ? -val : val; }

/* Calculates exponents (e.g. base^exp) */
int kpow(int base, int exp) {
    if (exp == 0) return 1;
    int res = 1;
    for (int i = 0; i < exp; i++) res *= base;
    return res;
}

/* Calculates rough square root via simple iterative multiplication */
int ksqrt(int val) {
    if (val < 0) return -1; // Error state
    if (val == 0 || val == 1) return val;
    int i = 1, result = 1;
    while (result <= val) { i++; result = i * i; }
    return i - 1;
}

/* Taylor series approximation for Sine (scaled by 1000 for fixed-point integer math) */
int ksin(int degrees) {
    // Normalize degrees
    while (degrees < 0) degrees += 360;
    while (degrees >= 360) degrees -= 360;
    int sign = 1;
    if (degrees > 180) { degrees -= 180; sign = -1; }
    if (degrees > 90) { degrees = 180 - degrees; }
    
    // Taylor Series calculation: x - x^3/3! + x^5/5!
    int val = (degrees * 314159) / 180000; 
    int term1 = val;
    int term2 = (kpow(val, 3) / 6000000);
    int term3 = (kpow(val, 5) / 120000000);
    return sign * (term1 - term2 + term3);
}

/* Cosine approximation (Sin shifted by 90 degrees) */
int kcos(int degrees) {
    return ksin(degrees + 90);
}

/* Linear congruential generator for pseudo-random numbers */
static uint32_t rand_seed = 123456789;
int k_rand() {
    rand_seed = (rand_seed * 1103515245 + 12345) & 0x7FFFFFFF;
    return rand_seed;
}

/* String comparison: Returns 0 if identical */
int kstrcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

/* Compare strings up to n characters */
int kstrncmp(const char* s1, const char* s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

/* Overwrite memory with a specific byte value */
void kmemset(void* dest, uint8_t val, size_t len) {
    uint8_t* ptr = (uint8_t*)dest;
    while(len--) *ptr++ = val;
}

/* Copy blocks of memory */
void kmemcpy(void* dest, const void* src, size_t len) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while(len--) *d++ = *s++;
}

/* Convert Ascii string to Integer */
int katoi(const char* str) {
    int res = 0, sign = 1, i = 0;
    if (str[0] == '-') { sign = -1; i++; }
    for (; str[i] >= '0' && str[i] <= '9'; ++i) res = res * 10 + str[i] - '0';
    return res * sign;
}

/* Convert Hexadecimal string to Integer */
int katohex(const char* str) {
    int res = 0, i = 0;
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) i = 2; // Skip 0x
    for (; str[i] != '\0'; ++i) {
        if (str[i] >= '0' && str[i] <= '9') res = res * 16 + (str[i] - '0');
        else if (str[i] >= 'a' && str[i] <= 'f') res = res * 16 + (str[i] - 'a' + 10);
        else if (str[i] >= 'A' && str[i] <= 'F') res = res * 16 + (str[i] - 'A' + 10);
        else break;
    }
    return res;
}

/* Get string length */
size_t kstrlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

/* Copy string from src to dest */
void kstrcpy(char* dest, const char* src) {
    while (*src) *dest++ = *src++;
    *dest = '\0';
}

/* Find first occurrence of a character in a string */
char* kstrchr(const char *s, int c) {
    while (*s != (char)c) {
        if (!*s++) return NULL;
    }
    return (char *)s;
}

/* Reverse a string in place */
void reverse(char str[], int length) {
    int start = 0, end = length - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        end--; start++;
    }
}

/* Convert Integer to Ascii string */
void itoa(int num, char* str, int base) {
    int i = 0, isNegative = 0;
    if (num == 0) { str[i++] = '0'; str[i] = '\0'; return; }
    if (num < 0 && base == 10) { isNegative = 1; num = -num; }
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

/* Check if the RTC is currently updating so we don't read garbage data */
int get_update_in_progress_flag() { 
    outb(CMOS_ADDRESS, 0x0A); 
    return (inb(CMOS_DATA) & 0x80); 
}

/* Fetch a specific register from the CMOS chip */
uint8_t get_rtc_register(int reg) { 
    outb(CMOS_ADDRESS, reg); 
    return inb(CMOS_DATA); 
}

/* Read the hardware clock and populate the system_time struct */
void read_rtc() {
    uint8_t last_second, last_minute, last_hour, last_day, last_month, last_year, registerB;
    
    while (get_update_in_progress_flag()); // Block until ready
    system_time.second = get_rtc_register(0x00);
    system_time.minute = get_rtc_register(0x02);
    system_time.hour = get_rtc_register(0x04);
    system_time.day = get_rtc_register(0x07);
    system_time.month = get_rtc_register(0x08);
    system_time.year = get_rtc_register(0x09);

    /* Read twice to ensure values didn't change while reading */
    do {
        last_second = system_time.second; last_minute = system_time.minute; last_hour = system_time.hour;
        last_day = system_time.day; last_month = system_time.month; last_year = system_time.year;

        while (get_update_in_progress_flag());
        system_time.second = get_rtc_register(0x00); system_time.minute = get_rtc_register(0x02);
        system_time.hour = get_rtc_register(0x04); system_time.day = get_rtc_register(0x07);
        system_time.month = get_rtc_register(0x08); system_time.year = get_rtc_register(0x09);
    } while ((last_second != system_time.second) || (last_minute != system_time.minute) || 
             (last_hour != system_time.hour) || (last_day != system_time.day) || 
             (last_month != system_time.month) || (last_year != system_time.year));

    registerB = get_rtc_register(0x0B);

    /* Convert BCD (Binary Coded Decimal) to raw binary values if necessary */
    if (!(registerB & 0x04)) {
        system_time.second = (system_time.second & 0x0F) + ((system_time.second / 16) * 10);
        system_time.minute = (system_time.minute & 0x0F) + ((system_time.minute / 16) * 10);
        system_time.hour = ( (system_time.hour & 0x0F) + (((system_time.hour & 0x70) / 16) * 10) ) | (system_time.hour & 0x80);
        system_time.day = (system_time.day & 0x0F) + ((system_time.day / 16) * 10);
        system_time.month = (system_time.month & 0x0F) + ((system_time.month / 16) * 10);
        system_time.year = (system_time.year & 0x0F) + ((system_time.year / 16) * 10);
    }

    /* Apply user timezone offset safely */
    int raw_h = (int)system_time.hour;
    raw_h += current_offset;
    if (raw_h >= 24) raw_h -= 24; // Handle day wrap forward
    if (raw_h < 0) raw_h += 24;   // Handle day wrap backward
    system_time.hour = (uint8_t)raw_h;

    /* Adjust if RTC is in 12-hour mode */
    if (!(registerB & 0x02) && (system_time.hour & 0x80)) {
        system_time.hour = ((system_time.hour & 0x7F) + 12) % 24;
    }
    system_time.year += 2000;
}

/* ========================================================================== */
/* 7. VGA TERMINAL ENGINE & 500-LINE SCROLLING LOGIC                          */
/* ========================================================================== */

/**
 * Called by keyboard.c when user presses UP Arrow (0x48)
 * In CLI: Moves the viewport backward in time.
 * In TUI: Moves the selection cursor up.
 */
void scroll_up() {
    if (in_gui_mode) {
        if (tui_selected_item > 0) tui_selected_item--;
        else tui_selected_item = tui_max_items - 1; // Wrap around to bottom
        tui_needs_redraw = 1;
        return;
    }
    if (scroll_offset > 0) { 
        scroll_offset--; 
        refresh_screen(); 
    }
}

/**
 * Called by keyboard.c when user presses DOWN Arrow (0x50)
 * In CLI: Moves the viewport forward in time.
 * In TUI: Moves the selection cursor down.
 */
void scroll_down() {
    if (in_gui_mode) {
        if (tui_selected_item < tui_max_items - 1) tui_selected_item++; 
        else tui_selected_item = 0; // Wrap around to top
        tui_needs_redraw = 1;
        return;
    }
    // Limit downward scroll so the bottom of the viewport aligns with current row
    if (scroll_offset < (MAX_SCROLLBACK - SCREEN_HEIGHT)) {
        if (scroll_offset < current_row - SCREEN_HEIGHT + 1) {
            scroll_offset++; 
            refresh_screen();
        }
    }
}

/**
 * Pushes all 500 lines of data up by 1 if the user hits the bottom of the RAM buffer.
 * Automatically pins the camera to the newest typing line.
 */
void auto_scroll() {
    if (current_row >= MAX_SCROLLBACK) {
        for (int i = 1; i < MAX_SCROLLBACK; i++) {
            for (int j = 0; j < SCREEN_WIDTH; j++) {
                terminal_buffer[i-1][j] = terminal_buffer[i][j];
            }
        }
        for (int j = 0; j < SCREEN_WIDTH; j++) {
            terminal_buffer[MAX_SCROLLBACK - 1][j] = ' ' | (current_term_color << 8);
        }
        current_row = MAX_SCROLLBACK - 1;
    }
    if (current_row >= scroll_offset + SCREEN_HEIGHT) {
        scroll_offset = current_row - SCREEN_HEIGHT + 1;
    }
}

/**
 * Writes the active portion of the 500-line buffer to the 0xB8000 VGA chip
 */
void refresh_screen() {
    if (in_gui_mode) return; // Prevent CLI background rendering from ruining GUI visuals
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

/**
 * Moves the blinking hardware cursor.
 * Accounts for where the user is currently scrolled to.
 */
void update_cursor_relative() {
    if (in_gui_mode) {
        // Move the cursor off-screen so it doesn't blink randomly in the GUI
        uint16_t pos = SCREEN_HEIGHT * SCREEN_WIDTH; 
        outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
        outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
        return;
    }
    
    int visual_row = current_row - scroll_offset;
    
    // Only show cursor if the line we are typing on is currently visible on screen
    if (visual_row >= 0 && visual_row < SCREEN_HEIGHT) {
        uint16_t pos = (visual_row * SCREEN_WIDTH) + current_col;
        outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
        outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
    } else {
        // Hide cursor off-screen
        uint16_t pos = SCREEN_HEIGHT * SCREEN_WIDTH; 
        outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
        outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
    }
}

/**
 * Primary text writing function. Handles newlines and backspaces.
 */
void putchar_col(char c, uint8_t color) {
    if (in_gui_mode) return; // Discard CLI input text while in GUI

    if (c == '\n') {
        current_col = 0; current_row++;
    } else if (c == '\b') {
        // Don't backspace past the AaronOS> prompt
        if (current_col > prompt_limit) {
            current_col--; 
            terminal_buffer[current_row][current_col] = ' ' | (color << 8);
        }
    } else {
        // Write char to buffer
        terminal_buffer[current_row][current_col] = (uint16_t)c | (color << 8);
        current_col++;
        if (current_col >= SCREEN_WIDTH) { current_col = 0; current_row++; }
    }
    auto_scroll();
    refresh_screen();
}

void print(const char* str) { 
    for (int i = 0; str[i]; i++) putchar_col(str[i], current_term_color); 
}

void print_col(const char* str, uint8_t col) { 
    for (int i = 0; str[i]; i++) putchar_col(str[i], col); 
}

void clear_screen() {
    for (int i = 0; i < MAX_SCROLLBACK; i++) {
        for (int j = 0; j < SCREEN_WIDTH; j++) {
            terminal_buffer[i][j] = ' ' | (current_term_color << 8);
        }
    }
    current_col = 0; current_row = 0; scroll_offset = 0;
    refresh_screen();
}

/* Bypass the buffer and write straight to VGA (Used heavily by TUI) */
void putchar_at(char c, uint8_t color, int x, int y) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        video_mem[y * SCREEN_WIDTH + x] = (uint16_t)c | (color << 8);
    }
}

/* Write full string straight to VGA at specific coordinates */
void print_at(const char* str, uint8_t color, int x, int y) {
    for (int i = 0; str[i]; i++) putchar_at(str[i], color, x + i, y);
}

/* ========================================================================== */
/* 8. AUDIO ENGINE (PIT-BASED)                                                */
/* ========================================================================== */

/* Sets the base frequency of the timer interrupt */
void init_timer(uint32_t frequency) {
    uint32_t divisor = 1193180 / frequency;
    outb(0x43, 0x36); 
    outb(0x40, (uint8_t)(divisor & 0xFF)); 
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

/* Fired 100 times per second via IRQ0 */
void timer_callback() { timer_ticks++; }

/* Sets PIT channel 2 frequency and activates PC speaker */
void play_sound(uint32_t nFrequence) {
    if (nFrequence == 0) return;
    uint32_t Div = 1193180 / nFrequence;
    outb(PIT_COMMAND, 0xB6); 
    outb(PIT_CHANNEL_2, (uint8_t)(Div)); 
    outb(PIT_CHANNEL_2, (uint8_t)(Div >> 8));
    
    uint8_t tmp = inb(PC_SPEAKER_PORT);
    if (tmp != (tmp | 3)) outb(PC_SPEAKER_PORT, tmp | 3); // Turn speaker on
    
    sys_stats.last_freq = nFrequence; 
    sys_stats.speaker_state = 1;
}

/* Turns off PC speaker */
void nosound() {
    uint8_t tmp = inb(PC_SPEAKER_PORT) & 0xFC;
    outb(PC_SPEAKER_PORT, tmp);
    sys_stats.speaker_state = 0;
}

/* Blocking sleep function utilizing the timer_ticks variable */
void sleep(uint32_t ticks) {
    uint32_t eticks = timer_ticks + ticks;
    while(timer_ticks < eticks) asm volatile("hlt"); // Yield CPU while waiting
}

/* Play a series of notes */
void play_song(uint32_t* notes, uint32_t* durations, int length) {
    for (int i = 0; i < length; i++) {
        if (notes[i] == 0) nosound(); else play_sound(notes[i]);
        sleep(durations[i]); nosound();
        // Brief busy-loop pause between notes to make melodies clear
        for(volatile int d = 0; d < 500000; d++); 
    }
}

/* The startup sound */
void boot_jingle() {
    play_sound(523); sleep(25); play_sound(659); sleep(25); 
    play_sound(783); sleep(25); play_sound(1046); sleep(45); nosound();
}

/* ========================================================================== */
/* 9. SYSTEM LOGGING & RECOVERY                                               */
/* ========================================================================== */

/* Prints hardware statuses and saves them to the dmesg ring buffer */
void log_boot(const char* msg) {
    print_col("[HAL] ", COLOR_BOOT); 
    print(msg); 
    print_col(" - OK\n", COLOR_SUCCESS);
    
    if (boot_log_count < MAX_BOOT_LOGS) {
        kstrcpy(boot_logs[boot_log_count], msg); 
        boot_log_count++;
    }
}

/* Hard stops the OS in the event of an unrecoverable error */
void kpanic(const char* message) {
    kmemset(video_mem, 0, SCREEN_WIDTH * SCREEN_HEIGHT * 2);
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) video_mem[i] = (uint16_t)' ' | (COLOR_PANIC << 8);
    current_col = 0; current_row = 0; scroll_offset = 0; in_gui_mode = 0;
    
    print_at("CRITICAL_KERNEL_HALT (0xDEADBEEF)", COLOR_PANIC, 0, 0);
    print_at("The system has been halted to prevent hardware damage.", COLOR_PANIC, 0, 1);
    print_at("REASON: ", COLOR_PANIC, 0, 3); print_at(message, COLOR_PANIC, 8, 3);
    
    print_at("PROCESSOR STATE DUMP:", COLOR_PANIC, 0, 5);
    print_at("EAX: 0x00000000   EBX: 0x00000000", COLOR_PANIC, 2, 6);
    print_at("ECX: 0x00000000   EDX: 0x00000000", COLOR_PANIC, 2, 7);
    print_at("ESI: 0x00000000   EDI: 0x00000000", COLOR_PANIC, 2, 8);
    print_at("EIP: 0x00100000   ESP: 0x00080000", COLOR_PANIC, 2, 9);
    
    print_at("Please capture this screen and submit a bug report.", COLOR_PANIC, 0, 12);
    print_at("Press RESET on your machine to restart.", COLOR_PANIC, 0, 14);
    while(1) asm volatile("cli; hlt"); // Stop processor completely
}

/* Warm reboots via the Keyboard controller pulse */
void sys_reboot() {
    print_col("\n[ AaronOS ] System Reboot Initiated...", COLOR_ALERT);
    sleep(20);
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(KBD_STATUS_PORT);
    outb(KBD_STATUS_PORT, 0xFE); 
    kpanic("REBOOT_PULSE_FAILED"); // If we get here, reboot failed
}

/* Displays system uptime and stats */
void print_stats() {
    char buf[16];
    print_col("\n--- AaronOS Engine Health ---\n", COLOR_HELP);
    print("Uptime Ticks:   "); itoa(timer_ticks, buf, 10); print(buf);
    print("\nCommands Run:   "); itoa(sys_stats.total_commands, buf, 10); print(buf);
    print("\nSpeaker Status: "); print(sys_stats.speaker_state ? "ACTIVE" : "IDLE");
    print("\nColor Pallet:   0x"); itoa(current_term_color, buf, 16); print(buf);
    print("\nTerminal Size:  "); itoa(MAX_SCROLLBACK, buf, 10); print(buf); print(" lines capacity");
    print("\n-----------------------------\n");
}

/* ========================================================================== */
/* 10. AARON_OS EXPLORER: THE INTERACTIVE TUI ENGINE                          */
/* ========================================================================== */

/* The base wallpaper and taskbars */
void tui_draw_desktop() {
    // Fill Screen Background with pattern
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        video_mem[i] = (uint16_t)0xB1 | (TUI_BG_COLOR << 8); 
    }
    
    // Top Taskbar
    for(int x = 0; x < SCREEN_WIDTH; x++) putchar_at(' ', TUI_BAR_COLOR, x, 0);
    print_at(" AaronOS Desktop Environment", TUI_BAR_COLOR, 0, 0);
    
    // Live Clock injected into Top Taskbar
    char time_str[32] = "Tck: ";
    char buf[16]; itoa(timer_ticks, buf, 10); kstrcpy(time_str + 5, buf);
    print_at(time_str, TUI_BAR_COLOR, SCREEN_WIDTH - 20, 0);

    // Bottom Action Bar
    for(int x = 0; x < SCREEN_WIDTH; x++) putchar_at(' ', TUI_BAR_COLOR, x, SCREEN_HEIGHT - 1);
    print_at(" \x18\x19 Navigate   \x11 Select   ESC Return", TUI_BAR_COLOR, 0, SCREEN_HEIGHT - 1);
}

/* Generates a dynamic floating window with shadow */
void tui_draw_window(int x, int y, int w, int h, const char* title) {
    // Drop Shadow logic
    for(int r = 1; r < h; r++) {
        for(int c = 1; c < w; c++) {
            putchar_at(' ', 0x08, x + c + 1, y + r + 1); 
        }
    }
    // Window Body
    for(int r = 0; r < h; r++) {
        for(int c = 0; c < w; c++) {
            char ch = ' ';
            if (r == 0 && c == 0) ch = BOX_TL; 
            else if (r == 0 && c == w - 1) ch = BOX_TR; 
            else if (r == h - 1 && c == 0) ch = BOX_BL; 
            else if (r == h - 1 && c == w - 1) ch = BOX_BR; 
            else if (r == 0 || r == h - 1) ch = BOX_HLINE; 
            else if (c == 0 || c == w - 1) ch = BOX_VLINE; 
            putchar_at(ch, TUI_WIN_COLOR, x + c, y + r);
        }
    }
    // Centered Title
    int t_len = kstrlen(title);
    int t_pos = x + (w - t_len) / 2;
    print_at(title, TUI_WIN_COLOR | 0x0F, t_pos, y);
}

/* State 0: The Hub */
void tui_render_main_menu() {
    tui_max_items = 4; // Tell the scroll engine there are 4 items
    int w = 40, h = 10;
    int x = (SCREEN_WIDTH - w) / 2, y = (SCREEN_HEIGHT - h) / 2;
    
    tui_draw_window(x, y, w, h, " Main Menu ");
    
    char* items[] = {
        " 1. Browse Filesystem    ",
        " 2. System Monitor       ",
        " 3. About AaronOS        ",
        " 4. Exit to Terminal     "
    };

    // Draw items and highlight the currently selected one
    for(int i = 0; i < 4; i++) {
        uint8_t col = (i == tui_selected_item) ? TUI_HL_COLOR : TUI_WIN_COLOR;
        print_at(items[i], col, x + 5, y + 2 + (i*1));
    }
}

/* State 1: File Browser mock */
void tui_render_file_browser() {
    tui_max_items = 4;
    int w = 50, h = 12;
    int x = (SCREEN_WIDTH - w) / 2, y = (SCREEN_HEIGHT - h) / 2;
    
    tui_draw_window(x, y, w, h, " File Explorer (A:\\) ");
    print_at(" Name          Size      Type ", TUI_WIN_COLOR, x + 2, y + 2);
    
    // Draw a separator line
    for(int i=2; i<w-2; i++) putchar_at(BOX_HLINE, TUI_WIN_COLOR, x+i, y+3);

    char* items[] = {
        " KERNEL.BIN    1245 KB   SYS  ",
        " SYSTEM.CFG       4 KB   CFG  ",
        " README.TXT       1 KB   TXT  ",
        " [ BACK TO MENU ]             "
    };

    for(int i = 0; i < 4; i++) {
        uint8_t col = (i == tui_selected_item) ? TUI_HL_COLOR : TUI_WIN_COLOR;
        print_at(items[i], col, x + 2, y + 4 + i);
    }
}

/* State 2: Hardware Stats */
void tui_render_sysmon() {
    tui_max_items = 1; // Only 1 selectable item (the BACK button)
    int w = 46, h = 12;
    int x = (SCREEN_WIDTH - w) / 2, y = (SCREEN_HEIGHT - h) / 2;
    
    tui_draw_window(x, y, w, h, " System Monitor ");
    read_rtc(); // Get fresh hardware clock

    char buf[16];
    print_at(" AaronOS Engine Core", TUI_WIN_COLOR, x + 2, y + 2);
    
    print_at(" Uptime Ticks : ", TUI_WIN_COLOR, x + 2, y + 4);
    itoa(timer_ticks, buf, 10); print_at(buf, TUI_WIN_COLOR | 0x09, x + 18, y + 4);

    print_at(" Hardware Time: ", TUI_WIN_COLOR, x + 2, y + 5);
    itoa(system_time.hour, buf, 10); print_at(buf, TUI_WIN_COLOR | 0x09, x + 18, y + 5);
    print_at(":", TUI_WIN_COLOR | 0x09, x + 20, y + 5);
    itoa(system_time.minute, buf, 10); print_at(buf, TUI_WIN_COLOR | 0x09, x + 21, y + 5);

    print_at(" RAM Detected : 640 KB Base / 16 MB Ext", TUI_WIN_COLOR, x + 2, y + 7);
    
    uint8_t col = (tui_selected_item == 0) ? TUI_HL_COLOR : TUI_WIN_COLOR;
    print_at(" [ RETURN ] ", col, x + (w - 12)/2, y + 9);
}

/* State 3: Info Screen */
void tui_render_about() {
    tui_max_items = 1;
    int w = 40, h = 12;
    int x = (SCREEN_WIDTH - w) / 2, y = (SCREEN_HEIGHT - h) / 2;
    
    tui_draw_window(x, y, w, h, " About ");
    print_at(" AaronOS v3.9.0-STABLE ", TUI_WIN_COLOR | 0x09, x + 8, y + 3);
    print_at(" Monolithic x86 Build  ", TUI_WIN_COLOR, x + 8, y + 5);
    print_at(" Built: April 2026     ", TUI_WIN_COLOR, x + 8, y + 6);
    
    uint8_t col = (tui_selected_item == 0) ? TUI_HL_COLOR : TUI_WIN_COLOR;
    print_at(" [ OK ] ", col, x + (w - 8)/2, y + 9);
}

/* Logic router for TUI menus */
void tui_handle_input() {
    // execute_flag is raised by keyboard.c when ENTER is pressed
    if (execute_flag == 1) {
        execute_flag = 0;
        input_ptr = 0; // Clear hidden CLI buffer
        
        if (tui_state == 0) { // Main Menu
            if (tui_selected_item == 0) { tui_state = 1; tui_selected_item = 0; }
            else if (tui_selected_item == 1) { tui_state = 2; tui_selected_item = 0; }
            else if (tui_selected_item == 2) { tui_state = 3; tui_selected_item = 0; }
            else if (tui_selected_item == 3) { in_gui_mode = 0; } // Drop back to shell
        }
        else if (tui_state == 1) { // File Browser
            if (tui_selected_item == 3) { tui_state = 0; tui_selected_item = 0; } // Back button
            else if (tui_selected_item == 2) {
                print_at(" Opening README.TXT...       ", 0x4F, 15, 20);
                sleep(70);
            } else {
                print_at(" ERR: System files locked.   ", 0x4F, 15, 20);
                sleep(70);
            }
        }
        else if (tui_state == 2 || tui_state == 3) { // SysMon / About
            if (tui_selected_item == 0) { tui_state = 0; tui_selected_item = 0; } // Back
        }
        tui_needs_redraw = 1;
    }
    
    // Global ESC check: Look inside input buffer for ESC scancode/char equivalent (27)
    if (input_ptr > 0 && input_buffer[input_ptr-1] == 27) {
        input_ptr = 0;
        if (tui_state != 0) { tui_state = 0; tui_selected_item = 0; tui_needs_redraw = 1; }
        else { in_gui_mode = 0; } // Exit GUI if we are already at main menu
    }
}

/* Entry point to the Desktop Environment */
void launch_tui() {
    in_gui_mode = 1;         // Tells terminal engine to stop writing
    tui_state = 0;           // Start at main menu
    tui_selected_item = 0;
    tui_needs_redraw = 1;
    execute_flag = 0;
    input_ptr = 0;
    update_cursor_relative(); // Hide physical cursor
    
    while(in_gui_mode) {
        if (tui_needs_redraw) {
            tui_draw_desktop();
            // State router for active window
            if (tui_state == 0) tui_render_main_menu();
            else if (tui_state == 1) tui_render_file_browser();
            else if (tui_state == 2) tui_render_sysmon();
            else if (tui_state == 3) tui_render_about();
            tui_needs_redraw = 0;
        }
        
        tui_handle_input();
        
        // Dynamic elements updating (like the clock ticking every 100 cycles)
        if (timer_ticks % 100 == 0) tui_needs_redraw = 1; 
        
        asm volatile("hlt"); // Rest processor
    }

    /* GUI exited. Restore CLI State gracefully */
    execute_flag = 0; 
    input_ptr = 0;    
    refresh_screen();    // Redraw the shell scrollback
    print("\nAaronOS> ");
    prompt_limit = current_col;
    update_cursor_relative();
}

/* ========================================================================== */
/* 11. THE COMMAND INTERPRETER & CLI SHELL                                    */
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
    print(" Github: github.com/ZippyType/AaronOS \n");
}

void run_matrix() {
    clear_screen();
    for(int i = 0; i < 400; i++) {
        int x = (timer_ticks * 7) % SCREEN_WIDTH; 
        int y = (timer_ticks / 3) % SCREEN_HEIGHT;
        char c = (timer_ticks % 94) + 33; 
        putchar_at(c, COLOR_MATRIX, x, y);
        sleep(1);
        if (y > 0) putchar_at(' ', COLOR_MATRIX, x, y - 1);
    }
    clear_screen();
}

void print_help() {
    print_col("--- AaronOS Command List ---\n", COLOR_HELP);
    print("gui       - Open the interactive TUI Explorer (Desktop)\n");
    print("install   - Run HDD deployment\n");
    print("reboot    - Warm restart\n");
    print("shutdown  - ACPI Power off\n");
    print("ver       - Show system version\n");
    print("dmesg     - Show internal hardware boot log\n");
    print("time      - Display hardware clock\n");
    print("tz [city] - Set timezone (e.g. tz amsterdam)\n");
    print("cls       - Clear terminal window\n");
    print("panic     - Test kernel crash\n");
    print("beep [f]  - Play tone (ex: beep 440)\n");
    print("dir       - List disk contents\n");
    print("cat [f]   - Read text file\n");
    print("write [t] - Append text to disk\n");
    print("touch [f] - Create an empty file\n");
    print("rm [f]    - Delete a file\n");
    print("rename    - Rename a file (syntax: rename old new)\n");
    print("edit      - Open text editor\n");
    print("echo [t]  - Print text to screen\n");
    print("cpu       - Show hardware vendor\n");
    print("music     - Plays a bit of music\n");
    print("siren     - Sounds a siren\n");
    print("credits   - Show OS build information\n");
    print("stats     - Show OS health and uptime\n");
    print("rand      - Generate pseudo-random number\n");
    print("matrix    - Enter the matrix\n");
    print("color [h] - Change text color (hex, e.g. color 0A)\n");
    print("calc      - Basic math (e.g. calc 5 + 10 or calc sin 90)\n");
}

/* Master Routing Logic. Fired when Execute_flag is active. */
void process_shell() {
    print("\n");
    sys_stats.total_commands++;

    if (input_ptr > 0) {
        input_buffer[input_ptr] = '\0'; // Null terminate string
        
        // SYSTEM COMMANDS
        if (kstrcmp(input_buffer, "help") == 0) print_help();
        else if (kstrcmp(input_buffer, "gui") == 0) { launch_tui(); return; } // Don't reprint shell prompt on exit
        else if (kstrcmp(input_buffer, "ver") == 0) {
            print_col(KERNEL_NAME, COLOR_SUCCESS); print(" ["); print(KERNEL_VERSION); print("]\n");
            print("Architecture: i386 Monolithic\nBuild: "); print(KERNEL_BUILD);
        }
        else if (kstrcmp(input_buffer, "reboot") == 0) sys_reboot();
        else if (kstrcmp(input_buffer, "shutdown") == 0) { print_col("Powering off...", COLOR_ALERT); outw(0x604, 0x2000); }
        else if (kstrcmp(input_buffer, "cls") == 0) clear_screen();
        else if (kstrcmp(input_buffer, "dmesg") == 0) {
            print_col("--- KERNEL BOOT LOG ---\n", COLOR_HELP);
            for(int i = 0; i < boot_log_count; i++) { print("[OK] "); print(boot_logs[i]); print("\n"); }
        }
        else if (kstrcmp(input_buffer, "install") == 0) run_installation(); 
        else if (kstrcmp(input_buffer, "edit") == 0) run_editor(); 
        else if (kstrcmp(input_buffer, "panic") == 0) kpanic("USER_INITIATED_TEST");
        else if (kstrcmp(input_buffer, "cpu") == 0) {
            uint32_t ebx, ecx, edx;
            asm volatile("cpuid" : "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
            char vendor[13];
            *((uint32_t*)vendor) = ebx; *((uint32_t*)(vendor + 4)) = edx; *((uint32_t*)(vendor + 8)) = ecx;
            vendor[12] = '\0';
            print("Processor: "); print_col(vendor, COLOR_HELP);
        }
        else if (kstrcmp(input_buffer, "credits") == 0) show_credits();
        else if (kstrcmp(input_buffer, "stats") == 0) print_stats();
        
        // TIME COMMANDS
        else if (kstrcmp(input_buffer, "time") == 0) {
            read_rtc(); char time_str[16];
            print("Clock ["); print(current_tz_name); print("]: ");
            itoa(system_time.hour, time_str, 10); print(time_str); print(":");
            if (system_time.minute < 10) print("0"); itoa(system_time.minute, time_str, 10); print(time_str); print(":");
            if (system_time.second < 10) print("0"); itoa(system_time.second, time_str, 10); print(time_str);
            print(" | Date: ");
            itoa(system_time.month, time_str, 10); print(time_str); print("/");
            itoa(system_time.day, time_str, 10); print(time_str); print("/");
            itoa(system_time.year, time_str, 10); print(time_str);
        }
        else if (kstrncmp(input_buffer, "tz", 2) == 0) {
            char* city = &input_buffer[2]; 
            if (kstrcmp(city, " amsterdam") == 0) { current_offset = 2; kstrcpy(current_tz_name, "Amsterdam (CEST)"); print("Timezone set to Amsterdam."); }
            else if (kstrcmp(city, " london") == 0) { current_offset = 1; kstrcpy(current_tz_name, "London (BST)"); print("Timezone set to London."); }
            else if (kstrcmp(city, " newyork") == 0) { current_offset = -4; kstrcpy(current_tz_name, "New York (EDT)"); print("Timezone set to New York."); }
            else if (kstrcmp(city, " tokyo") == 0) { current_offset = 9; kstrcpy(current_tz_name, "Tokyo (JST)"); print("Timezone set to Tokyo."); }
            else print("Unknown city. Defaults: amsterdam, london, newyork, tokyo");
        }
        
        // FILESYSTEM COMMANDS (Passed to external modules)
        else if (kstrcmp(input_buffer, "dir") == 0 || kstrcmp(input_buffer, "ls") == 0) { print_col("DIRECTORY LISTING:\n", COLOR_HELP); fat16_list_files(); }
        else if (kstrncmp(input_buffer, "cat ", 4) == 0) fat16_cat(&input_buffer[4]);
        else if (kstrncmp(input_buffer, "write ", 6) == 0) fat16_write_to_test(&input_buffer[6]);
        else if (kstrncmp(input_buffer, "touch ", 6) == 0) fat16_create_file(&input_buffer[6]);
        else if (kstrncmp(input_buffer, "rm ", 3) == 0) fat16_delete_file(&input_buffer[3]);
        else if (kstrncmp(input_buffer, "rename ", 7) == 0) {
            char* args = &input_buffer[7]; char* space = kstrchr(args, ' ');
            if (space) { *space = '\0'; fat16_rename_file(args, space + 1); } else print("Syntax: rename [old] [new]");
        }
        
        // UTILITIES
        else if (kstrncmp(input_buffer, "echo ", 5) == 0) print(&input_buffer[5]);
        else if (kstrcmp(input_buffer, "rand") == 0) {
            char buf[16]; itoa(k_rand(), buf, 10); print("Entropy Output: "); print(buf);
        }
        else if (kstrncmp(input_buffer, "color ", 6) == 0) {
            int new_color = katohex(&input_buffer[6]);
            if (new_color >= 0 && new_color <= 0xFF) { current_term_color = (uint8_t)new_color; print("Terminal color updated."); }
            else print("Invalid color code. Use hex format (e.g. color 0A)");
        }
        
        // ADVANCED CALCULATOR PARSER
        else if (kstrncmp(input_buffer, "calc ", 5) == 0) {
            char* expr = &input_buffer[5]; int a = 0, b = 0, res = 0, i = 0; char op = 0;
            while(expr[i] == ' ') i++; // skip space
            
            if (kstrncmp(expr, "abs ", 4) == 0) {
                a = katoi(&expr[4]); char buf[16]; itoa(kabs(a), buf, 10); print("Result: "); print(buf);
            } else if (kstrncmp(expr, "sqrt ", 5) == 0) {
                a = katoi(&expr[5]); char buf[16]; itoa(ksqrt(a), buf, 10); print("Result: "); print(buf);
            } else if (kstrncmp(expr, "sin ", 4) == 0) {
                a = katoi(&expr[4]); char buf[16]; itoa(ksin(a), buf, 10); print("Result (x1000): "); print(buf);
            } else if (kstrncmp(expr, "cos ", 4) == 0) {
                a = katoi(&expr[4]); char buf[16]; itoa(kcos(a), buf, 10); print("Result (x1000): "); print(buf);
            } else {
                a = katoi(&expr[i]);
                while(expr[i] >= '0' && expr[i] <= '9') i++; while(expr[i] == ' ') i++;
                if (expr[i] == '+' || expr[i] == '-' || expr[i] == '*' || expr[i] == '/' || expr[i] == '^' || expr[i] == '%') {
                    op = expr[i]; i++; while(expr[i] == ' ') i++; b = katoi(&expr[i]);
                    if (op == '+') res = a + b; else if (op == '-') res = a - b; else if (op == '*') res = a * b;
                    else if (op == '^') res = kpow(a, b); else if (op == '%') res = a % b;
                    else if (op == '/') { if (b != 0) res = a / b; else print("ERR: Div by 0"); }
                    if (op != '/' || b != 0) { char buf[16]; itoa(res, buf, 10); print("Result: "); print(buf); }
                } else print("Syntax: calc [a] [+|-|*|/|^|%] [b] OR calc [abs|sqrt|sin|cos] [a]");
            }
        }
        
        // MULTIMEDIA
        else if (kstrncmp(input_buffer, "beep ", 5) == 0) {
            int freq = katoi(&input_buffer[5]);
            if (freq > 0 && freq < 20000) { print("Tuning PIT to "); print(&input_buffer[5]); print(" Hz."); play_sound(freq); sleep(40); nosound(); }
            else print("Freq Out of Range (1-20000)");
        }
        else if (kstrcmp(input_buffer, "beep") == 0) boot_jingle();
        else if (kstrcmp(input_buffer, "music") == 0) {
            print("Audio Stream: Victory Theme"); uint32_t notes[] = {NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5}; uint32_t durations[] = {10, 10, 10, 30};
            play_song(notes, durations, 4);
        }
        else if (kstrcmp(input_buffer, "siren") == 0) {
            print("Generating Waveform: Siren"); uint32_t notes[] = {880, 440, 880, 440, 880, 440}; uint32_t durations[] = {35, 35, 35, 35, 35, 35};
            play_song(notes, durations, 6);
        }
        else if (kstrcmp(input_buffer, "matrix") == 0) run_matrix();
        else print("Unknown command. Type help for commands.");
    }
    
    // Reset buffer and prepare for next input
    print("\nAaronOS> ");
    input_ptr = 0;
    execute_flag = 0;
    prompt_limit = current_col; // Lock backspace boundary
    update_cursor_relative();
}

/* ========================================================================== */
/* 12. SEGMENTATION (GDT) & INTERRUPTS (IDT)                                  */
/* ========================================================================== */

/* Global Descriptor Table layout */
struct gdt_entry {
    uint16_t limit_low; uint16_t base_low; uint8_t base_middle; uint8_t access;
    uint8_t granularity; uint8_t base_high;
} __attribute__((packed)) gdt[3];

struct gdt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed)) gp;

/* Fills out a single GDT entry */
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF); gdt[num].base_middle = (base >> 16) & 0xFF; gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF); gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0); gdt[num].access = access;
}

/* Initializes flat memory model */
void init_gdt() {
    gp.limit = (sizeof(struct gdt_entry) * 3) - 1; gp.base = (uint32_t)&gdt;
    gdt_set_gate(0, 0, 0, 0, 0); // Null segment
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // Code Segment
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Data segment
}

/* Interrupt Descriptor Table layout */
struct idt_entry {
    uint16_t base_lo, sel; uint8_t always0, flags; uint16_t base_hi;
} __attribute__((packed)) idt[256];

struct idt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed)) idtp;

/* Binds hardware interrupts to our Assembly handlers */
void init_idt() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1; idtp.base = (uint32_t)&idt; kmemset(idt, 0, sizeof(idt));
    
    // IRQ0 (Timer)
    uint32_t th = (uint32_t)timer_handler_asm;
    idt[32].base_lo = th & 0xFFFF; idt[32].base_hi = (th >> 16) & 0xFFFF; idt[32].sel = 0x08; idt[32].always0 = 0; idt[32].flags = 0x8E;
    
    // IRQ1 (Keyboard)
    uint32_t kh = (uint32_t)keyboard_handler_asm;
    idt[33].base_lo = kh & 0xFFFF; idt[33].base_hi = (kh >> 16) & 0xFFFF; idt[33].sel = 0x08; idt[33].always0 = 0; idt[33].flags = 0x8E;
    
    load_idt((uint32_t)&idtp); // Assembly call to load IDT
}

/* ========================================================================== */
/* 13. KERNEL ENTRY POINT                                                     */
/* ========================================================================== */

void kernel_main() {
    current_col = 0; current_row = 0; scroll_offset = 0; in_gui_mode = 0; boot_log_count = 0;
    clear_screen();
    
    print_col("\n[ AaronOS Boot Sequence Initiated ]\n\n", COLOR_HELP);

    init_gdt(); log_boot("Global Descriptor Table (GDT) Configured");

    /* 8259 PIC Remapping Magic Numbers */
    outb(0x20, 0x11); io_wait(); outb(0x21, 0x20); io_wait(); outb(0x21, 0x04); io_wait(); outb(0x21, 0x01); io_wait();
    outb(0xA0, 0x11); io_wait(); outb(0xA1, 0x28); io_wait(); outb(0xA1, 0x02); io_wait(); outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xFC); outb(0xA1, 0xFF);
    log_boot("8259 Programmable Interrupt Controller Remapped");

    init_idt(); log_boot("Interrupt Descriptor Table (IDT) Loaded");
    init_timer(100); log_boot("Programmable Interval Timer (PIT) bound to 100Hz");

    sys_stats.uptime_ticks = 0; sys_stats.total_commands = 0; sys_stats.speaker_state = 0;
    
    log_boot("Virtual Terminal Scrollback Buffer Allocated (500 Lines)");
    log_boot("VGA DMA Hook established at 0xB8000");
    log_boot("Math libraries (ksin, kcos, ksqrt) initialized");
    log_boot("TUI Graphics Engine Loaded");
    
    // Pause to let user see boot checks
    for(volatile int i=0; i<15000000; i++); 
    
    asm volatile("sti"); // Start accepting hardware interrupts
    log_boot("Hardware Interrupts (STI) Enabled");

    // Final pause
    for(volatile int i=0; i<15000000; i++); 
    clear_screen();
    
    boot_jingle(); // Play startup sound
    print("Welcome to AaronOS! \n Use help for commands.\n");
    print("AaronOS> ");
    prompt_limit = current_col;

    // Infinite idle loop
    while (1) { 
        if (execute_flag == 1 ) { process_shell(); execute_flag = 0; }
        asm volatile("hlt"); // Rest processor until next interrupt
    }
}

/**
 * =============================================================================
 * END OF KERNEL.C
 * =============================================================================
 */