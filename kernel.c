

/**
 * =============================================================================
 * AARONOS KERNEL - FULL MONOLITHIC BUILD (EXTENDED ARCHITECTURE)
 * =============================================================================
 * VERSION: 1
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
#include "commands.h"
#include "version.h"
#include "elf.h"

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


/* 
 * LOAD CUSTOM FONT: 
 * This reconfigures the VGA sequencer to upload our custom bitmap 
 * data (from font_data.h) directly into VGA Plane 2. 
 */
/* ========================================================================== */
/* 2. KERNEL GLOBAL STATE                                                     */
/* ========================================================================== */

/* CLI & Keyboard Hooks: Managed by keyboard.c, read by kernel.c */
char input_buffer[256];             // Raw characters typed by user
int input_ptr = 0;                  // Current position in the input buffer
volatile int execute_flag = 0;      // Set to 1 when ENTER is pressed
volatile int ctrl_c_flag = 0;       // Set to 1 when Ctrl+C is pressed

/* VGA Terminal State */
// A massive 2D array storing both the character and its color data
uint16_t terminal_buffer[MAX_SCROLLBACK][SCREEN_WIDTH]; 
int scroll_offset = 0;              // The "camera" looking into the buffer
int current_row = 0;                // Where the prompt currently is
int current_col = 0;                // Where the cursor currently is horizontally
int prompt_limit = 0;               // Prevents user from backspacing into the prompt string
int prompt_row = 0;                 // Row where the prompt was printed (for backspace wrapping)
uint8_t current_term_color = COLOR_DEFAULT; 

/* Serial output mirror (off by default, toggle with 'serial on') */
int serial_mirror = 0;

/* Shell variables */
#define MAX_VARS 32
#define VAR_NAME_LEN 32
#define VAR_VAL_LEN 128
typedef struct { char name[VAR_NAME_LEN]; char value[VAR_VAL_LEN]; } shell_var_t;
shell_var_t shell_vars[MAX_VARS];
int shell_var_count = 0;

/* Pipe support: capture command output and feed as input to next command */
#define PIPE_BUF_SIZE 4096
char pipe_buffer[PIPE_BUF_SIZE];
int pipe_buffer_len = 0;
volatile int pipe_output_active = 0;
volatile int ata_irq_flag = 0;
char stdin_buffer[PIPE_BUF_SIZE];
int stdin_buffer_len = 0;
uint16_t* video_mem = (uint16_t*)VIDEO_ADDR;

/* System Timing & Mode State */
volatile uint32_t timer_ticks = 0;  // Incremented 100 times per second by PIT
int current_offset = 2;             // User's selected timezone offset
char current_tz_name[32] = "Amsterdam (CEST)";

/* Boot Logging Ring Buffer */
char boot_logs[MAX_BOOT_LOGS][LOG_MSG_LEN];
int boot_log_count = 0;

/* TUI Engine State Machine */
extern int in_gui_mode;
extern int tui_state;
extern int tui_selected_item;
extern int tui_max_items;
extern int tui_needs_redraw;

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
extern void fat16_write_file(char* name, char* content);
extern void fat16_create_file(char* name);
extern void fat16_delete_file(char* name);
extern void fat16_rename_file(char* oldname, char* newname);
extern void fat16_copy_file(char* src, char* dst);
extern void fat16_move_file(char* src, char* dst);
extern void fat16_mkdir(char* name);
extern void fat16_rmdir(char* name);
extern int fat16_read_file(char* name, char* buffer, int max_len);
extern void fat16_cd(char* name);
extern void fat16_attrib(char* args);
extern void fat16_get_cwd(char* buf, int max);
extern uint16_t fat16_get_cwd_cluster();
extern int fat16_collect_display_names(char (*names)[13], int max);
extern int ata_init();

extern void run_installation();
extern void run_editor(char* filename);

extern void keyboard_handler_asm();
extern void mouse_handler_asm();
extern void timer_handler_asm();
extern void syscall_handler_asm();
extern void ata_handler_asm();
extern void* malloc(size_t size);
extern void load_idt(uint32_t ptr);

/* Hardware driver externs */
extern int acpi_init();
extern void acpi_poweroff();
extern void acpi_reboot();
extern int acpi_is_available();
extern void dma_init();
extern int sb16_init();
extern int ahci_init();
extern int ahci_is_present();
extern int smp_init();
extern int smp_cpu_count();
extern int mouse_init();
extern void mouse_render();
extern void mouse_clear();
extern void mouse_invalidate();
extern int sb16_is_present();
extern void sb16_play_dma(uint8_t* data, uint32_t len, uint32_t freq);

/* GUI functions */
/* GUI module references (from gui.c) */
extern void launch_tui();
extern void tui_draw_desktop();
extern void tui_draw_window(int x, int y, int w, int h, const char* title);
extern void tui_handle_input();
extern void tui_render_main_menu();
extern void tui_render_file_browser();
extern void tui_render_sysmon();
extern void tui_render_about();

/* Global variables shared with gui.c */
extern int in_gui_mode;
extern int tui_selected_item;
extern int tui_max_items;
extern int tui_needs_redraw;


/* Network Drivers */
/* Networking Subsystem Hooks */

/* Memory buffers (Must be contiguous physical memory, aligned for DMA) */
/* Networking Subsystem Hooks */
extern void net_init(uint32_t io_base);
extern void net_send_raw_packet(uint8_t* dest_mac, uint16_t protocol, uint8_t* payload, uint32_t payload_len);
extern uint8_t my_mac[6];
extern void net_ping(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3);// Current TX buffer
/* Networking & Browser Subsystem Hooks */
extern void net_poll();
extern void net_init(uint32_t io_base);
extern void run_browser(char* ip_str);
extern uint8_t my_mac[6];
extern int browser_ready;
extern char browser_buffer[2048];
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
void run_command(char* cmd);
void run_script(char* filename);
void cmd_grep(char* pattern);
void cmd_head(char* args);
void cmd_wc();
void cmd_sort();
void cmd_dir_glob(char* pattern);
int match_glob(const char* pattern, const char* name);
int has_glob(const char* str);
void expand_glob(char* glob, char (*results)[13], int* count);
void redraw_input_line();
void handle_history_up();
void handle_history_down();
void handle_tab_completion();
void save_to_history(char* cmd);
void set_default_vars();
char* get_var(const char* name);
void set_var(const char* name, const char* value);
void expand_vars(char* input, char* output, int max_out);
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

/* Find substring in string */
char* kstrstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    while (*haystack) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
        haystack++;
    }
    return NULL;
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

void print_hex(uint32_t val) {
    char buf[11];
    int i = 0;
    buf[i++] = '0'; buf[i++] = 'x';
    for (int j = 7; j >= 0; j--) {
        uint8_t nib = (val >> (j * 4)) & 0xF;
        buf[i++] = nib < 10 ? '0' + nib : 'a' + nib - 10;
    }
    buf[i] = 0;
    print(buf);
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
    mouse_invalidate();
    mouse_render();
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
    
    // Ensure cursor shape is visible (underline, scanlines 14-15)
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | 14);
    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | 15);

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
    if (in_gui_mode) return;

    /* Pipe capture mode: write to buffer instead of screen */
    if (pipe_output_active) {
        if (c == '\b') {
            if (pipe_buffer_len > 0) pipe_buffer_len--;
        } else {
            if (pipe_buffer_len < PIPE_BUF_SIZE - 1)
                pipe_buffer[pipe_buffer_len++] = c;
        }
        return;
    }

    if (c == '\n') {
        current_col = 0; current_row++;
    } else if (c == '\b') {
        if (current_row > prompt_row || current_col > prompt_limit) {
            if (current_col > 0) {
                current_col--;
            } else if (current_row > prompt_row) {
                current_row--;
                current_col = SCREEN_WIDTH - 1;
            }
            terminal_buffer[current_row][current_col] = ' ' | (color << 8);
        }
    } else {
        terminal_buffer[current_row][current_col] = (uint16_t)c | (color << 8);
        current_col++;
        if (current_col >= SCREEN_WIDTH) { current_col = 0; current_row++; }
    }
    auto_scroll();
    refresh_screen();
}

void print(const char* str) {
    for (int i = 0; str[i]; i++) {
        putchar_col(str[i], current_term_color);
        if (serial_mirror && !pipe_output_active) serial_putchar(str[i]);
    }
}

void print_col(const char* str, uint8_t col) { 
    for (int i = 0; str[i]; i++) putchar_col(str[i], col); 
}

void clear_screen() {
    mouse_clear();
    /* Direct VGA wipe — kills any stale glyphs GRUB may have left */
    kmemset(video_mem, 0, SCREEN_WIDTH * SCREEN_HEIGHT * 2);
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
#define MAX_PROCS 32
#define PROC_STACK 4096

typedef struct {
    uint32_t esp;
    int state; /* 0=dead,1=ready,2=running */
    int pid;
    int ring; /* 0=ring0,3=ring3 */
    uint8_t stack[PROC_STACK];
} pcb_t;

static pcb_t procs[MAX_PROCS];
static int proc_count;
static int cur_pid;
extern uint8_t tss[];

uint32_t scheduler(uint32_t* frame);

uint32_t timer_callback(uint32_t* frame) {
    timer_ticks++;
    return scheduler(frame);
}

int create_user_process(void (*entry)(), uint32_t user_stack) {
    if (proc_count >= MAX_PROCS) return -1;
    int pid = proc_count++;
    pcb_t* p = &procs[pid];
    p->pid = pid; p->state = 1; p->ring = 3;
    uint32_t* sp = (uint32_t*)(p->stack + PROC_STACK);
    *--sp = 0x23;                /* ss (user data | ring3) */
    *--sp = user_stack;          /* user esp */
    *--sp = 0x202;               /* eflags (IF=1) */
    *--sp = 0x1B;                /* cs (user code | ring3) */
    *--sp = (uint32_t)entry;     /* eip */
    *--sp = 0; *--sp = 0;       /* eax, ecx */
    *--sp = 0; *--sp = 0;       /* edx, ebx */
    *--sp = 0;                   /* old esp */
    *--sp = 0; *--sp = 0;       /* ebp, esi */
    *--sp = 0;                   /* edi */
    p->esp = (uint32_t)sp;
    return pid;
}

void set_user_tss(int pid) {
    *(uint32_t*)(tss + 4) = (uint32_t)(procs[pid].stack + PROC_STACK);
}

uint32_t scheduler(uint32_t* frame) {
    if (proc_count == 0) return (uint32_t)frame;
    procs[cur_pid].esp = (uint32_t)frame;
    procs[cur_pid].state = 1;
    int next = (cur_pid + 1) % proc_count;
    int tried = 0;
    while (procs[next].state != 1 && tried < proc_count) {
        next = (next + 1) % proc_count;
        tried++;
    }
    if (procs[next].state != 1) next = cur_pid;
    cur_pid = next;
    procs[cur_pid].state = 2;
    if (procs[cur_pid].ring == 3) set_user_tss(cur_pid);
    return procs[cur_pid].esp;
}

__attribute__((noreturn)) void user_test_proc() {
    const char* m1 = "User mode PID ";
    const char* m2 = " running!\n";
    asm volatile("int $0x80" : : "a"(1), "b"(1), "c"((uint32_t)m1), "d"(14));
    asm volatile("int $0x80" : : "a"(1), "b"(1), "c"((uint32_t)m2), "d"(10));
    asm volatile("int $0x80" : : "a"(3), "b"(0));
    while(1) asm("hlt");
}

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

/* ========================================================================== */
/* 11. THE COMMAND INTERPRETER & CLI SHELL                                    */
/* ========================================================================== */

void show_credits() {
    print_col("\n    ___                             ____  ____ \n", COLOR_HELP);
    print_col("   /   |  ____ __________  ____    / __ \\/ ___|\n", COLOR_HELP);
    print_col("  / /| | / __ `/ ___/ __ \\/ __ \\  / / / /\\___ \\\n", COLOR_HELP);
    print_col(" / ___ |/ /_/ / /  / /_/ / / / / /  /_/ / ___/ /\n", COLOR_HELP);
    print_col("/_/  |_|\\__,_/_/   \\____/_/ /_/  \\____/ |____/ \n", COLOR_HELP);
    print_col("\n\n", COLOR_DEFAULT);
    print(" Lead Developer: Aaron\n");
    print(" Kernel Version: "); print(KERNEL_VERSION); print("\n");
    print(" Github: github.com/ZippyType/AaronOS (please support it!) \n");
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
    for (int i = 0; i < NUM_COMMANDS; i++) {
        print(commands[i].name);
        print(" - ");
        print(commands[i].description);
        print("\n");
    }
    print("Use arrow keys to scroll up and down.\n");
}

int evaluate_condition(char* cond) {
    while (*cond == ' ') cond++;
    char expanded[256];
    expand_vars(cond, expanded, 256);
    char* eq = kstrstr(expanded, " == ");
    if (eq) {
        *eq = '\0';
        char* left = expanded; char* right = eq + 4;
        while (*left == ' ') left++; while (*right == ' ') right++;
        int rl = kstrlen(right);
        while (rl > 0 && right[rl-1] == ' ') right[--rl] = '\0';
        return kstrcmp(left, right) == 0;
    }
    char* neq = kstrstr(expanded, " != ");
    if (neq) {
        *neq = '\0';
        char* left = expanded; char* right = neq + 4;
        while (*left == ' ') left++; while (*right == ' ') right++;
        int rl = kstrlen(right);
        while (rl > 0 && right[rl-1] == ' ') right[--rl] = '\0';
        return kstrcmp(left, right) != 0;
    }
    return expanded[0] != '\0';
}

/* Master Routing Logic. Fired when Execute_flag is active. */
void run_script(char* filename) {
    char buf[4096];
    int len = fat16_read_file(filename, buf, 4096);
    if (len <= 0) { print("Script not found.\n"); return; }
    buf[len] = '\0';
    int i = 0;
    while (buf[i] == ' ' || buf[i] == '\t') i++;
    if (buf[i] != '#' || buf[i+1] != '!') { print("Not an AaronScript file.\n"); return; }
    i += 2;
    if (kstrncmp(&buf[i], "AaronScript", 11) != 0) { print("Not an AaronScript file.\n"); return; }
    while (buf[i] && buf[i] != '\n') i++;
    if (buf[i] == '\n') i++;

    int if_depth = 0;
    int if_skip[16];

    while (buf[i]) {
        while (buf[i] == ' ' || buf[i] == '\t') i++;
        if (buf[i] == '\n' || buf[i] == '\0') { if (buf[i] == '\n') i++; continue; }
        if (buf[i] == '#') { while (buf[i] && buf[i] != '\n') i++; if (buf[i] == '\n') i++; continue; }

        int start = i;
        while (buf[i] && buf[i] != '\n') i++;
        char saved = buf[i];
        buf[i] = '\0';

        int sk = 0;
        for (int d = 0; d < if_depth; d++) if (if_skip[d]) sk = 1;

        char* line = &buf[start];
        if (kstrncmp(line, "if ", 3) == 0) {
            if (sk) { if_skip[if_depth] = 1; } else { if_skip[if_depth] = evaluate_condition(line + 3) ? 0 : 1; }
            if_depth++;
        } else if (kstrcmp(line, "else") == 0) {
            if (if_depth > 0) if_skip[if_depth - 1] = !if_skip[if_depth - 1];
        } else if (kstrcmp(line, "endif") == 0) {
            if (if_depth > 0) if_depth--;
        } else if (!sk) {
            char expanded[256];
            expand_vars(line, expanded, 256);
            run_command(expanded);
        }

        buf[i] = saved;
        if (saved == '\n') i++;
    }
}

void run_command(char* cmd) {
    if (kstrcmp(cmd, "help") == 0) print_help();
    else if (kstrcmp(cmd, "gui") == 0) { launch_tui(); }
    else if (kstrcmp(cmd, "ver") == 0) {
        print_col(KERNEL_NAME, COLOR_SUCCESS); print(" ["); print(KERNEL_VERSION); print("]\n");
        print("Architecture: i386 Monolithic\nBuild: "); print(KERNEL_BUILD);
    }
    else if (kstrcmp(cmd, "reboot") == 0) {
        print_col("Rebooting...", COLOR_ALERT);
        sleep(10);
        acpi_reboot();
    }
    else if (kstrcmp(cmd, "shutdown") == 0 || kstrcmp(cmd, "poweroff") == 0) {
        print_col("Powering off...", COLOR_ALERT);
        sleep(10);
        acpi_poweroff();
    }
    else if (kstrcmp(cmd, "cls") == 0) clear_screen();
    else if (kstrcmp(cmd, "dmesg") == 0) {
        print_col("--- KERNEL BOOT LOG ---\n", COLOR_HELP);
        for(int i = 0; i < boot_log_count; i++) { print("[OK] "); print(boot_logs[i]); print("\n"); }
    }
    else if (kstrcmp(cmd, "install") == 0) run_installation(); 
    else if (kstrcmp(cmd, "edit") == 0) run_editor(""); 
    else if (kstrncmp(cmd, "edit ", 5) == 0) run_editor(&cmd[5]); 
    else if (kstrcmp(cmd, "write") == 0) run_editor(""); 
    else if (kstrcmp(cmd, "panic") == 0) kpanic("USER_INITIATED_TEST");
    else if (kstrcmp(cmd, "cpu") == 0) {
        uint32_t ebx, ecx, edx;
        asm volatile("cpuid" : "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
        char vendor[13];
        *((uint32_t*)vendor) = ebx; *((uint32_t*)(vendor + 4)) = edx; *((uint32_t*)(vendor + 8)) = ecx;
        vendor[12] = '\0';
        print("Processor: "); print_col(vendor, COLOR_HELP);
        char buf[16];
        int nc = smp_cpu_count();
        print("\nCPU Count: "); itoa(nc, buf, 10); print(buf);
    }
    else if (kstrcmp(cmd, "credits") == 0) show_credits();
    else if (kstrcmp(cmd, "stats") == 0) print_stats();
    else if (kstrncmp(cmd, "script ", 7) == 0) run_script(&cmd[7]);
    else if (kstrncmp(cmd, "exec ", 5) == 0) {
        uint32_t entry;
        if (elf_load(&cmd[5], &entry) == 0) {
            print("Running ELF at 0x"); print_hex(entry); print("\n");
            void (*f)() = (void (*)())entry;
            f();
        }
    }
    
    else if (kstrcmp(cmd, "time") == 0) {
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
    else if (kstrncmp(cmd, "tz", 2) == 0) {
        char* city = &cmd[2]; 
        if (kstrcmp(city, " amsterdam") == 0) { current_offset = 2; kstrcpy(current_tz_name, "Amsterdam (CEST)"); print("Timezone set to Amsterdam."); }
        else if (kstrcmp(city, " london") == 0) { current_offset = 1; kstrcpy(current_tz_name, "London (BST)"); print("Timezone set to London."); }
        else if (kstrcmp(city, " newyork") == 0) { current_offset = -4; kstrcpy(current_tz_name, "New York (EDT)"); print("Timezone set to New York."); }
        else if (kstrcmp(city, " tokyo") == 0) { current_offset = 9; kstrcpy(current_tz_name, "Tokyo (JST)"); print("Timezone set to Tokyo."); }
        else print("Unknown city. Defaults: amsterdam, london, newyork, tokyo");
    }
    
    else if (kstrcmp(cmd, "dir") == 0 || kstrcmp(cmd, "ls") == 0) { print_col("DIRECTORY LISTING:\n", COLOR_HELP); fat16_list_files(); }
    else if (kstrncmp(cmd, "dir ", 4) == 0) { cmd_dir_glob(&cmd[4]); }
    else if (kstrncmp(cmd, "ls ", 3) == 0) { cmd_dir_glob(&cmd[3]); }
    else if (kstrncmp(cmd, "cat ", 4) == 0) {
        if (has_glob(&cmd[4])) {
            char matches[128][13]; int count;
            expand_glob(&cmd[4], matches, &count);
            if (count == 0) print("No matches.\n");
            else for (int i = 0; i < count; i++) { fat16_cat(matches[i]); print("\n"); }
        } else fat16_cat(&cmd[4]);
    }
    else if (kstrncmp(cmd, "rm ", 3) == 0) {
        if (has_glob(&cmd[3])) {
            char matches[128][13]; int count;
            expand_glob(&cmd[3], matches, &count);
            if (count == 0) print("No matches.\n");
            else for (int i = 0; i < count; i++) fat16_delete_file(matches[i]);
        } else fat16_delete_file(&cmd[3]);
    }
    else if (kstrncmp(cmd, "write ", 6) == 0) run_editor(&cmd[6]);
    else if (kstrncmp(cmd, "touch ", 6) == 0) fat16_create_file(&cmd[6]);
    else if (kstrncmp(cmd, "rename ", 7) == 0) {
        char* args = &cmd[7]; char* space = kstrchr(args, ' ');
        if (space) { *space = '\0'; fat16_rename_file(args, space + 1); } else print("Syntax: rename [old] [new]");
    }
    else if (kstrncmp(cmd, "cp ", 3) == 0) {
        char* args = &cmd[3]; char* space = kstrchr(args, ' ');
        if (space) { *space = '\0'; fat16_copy_file(args, space + 1); } else print("Syntax: cp [src] [dst]");
    }
    else if (kstrncmp(cmd, "mv ", 3) == 0) {
        char* args = &cmd[3]; char* space = kstrchr(args, ' ');
        if (space) { *space = '\0'; fat16_move_file(args, space + 1); } else print("Syntax: mv [src] [dst]");
    }
    else if (kstrncmp(cmd, "cd ", 3) == 0) fat16_cd(&cmd[3]);
    else if (kstrcmp(cmd, "cd") == 0) fat16_cd("/");
    else if (kstrncmp(cmd, "mkdir ", 6) == 0) fat16_mkdir(&cmd[6]);
    else if (kstrncmp(cmd, "rmdir ", 6) == 0) fat16_rmdir(&cmd[6]);
    else if (kstrcmp(cmd, "format") == 0) {
        print("Formatting drive...\n");
        fat16_format_drive();
    }
    else if (kstrncmp(cmd, "attrib", 6) == 0) {
        if (cmd[6] == ' ' || cmd[6] == '\0') fat16_attrib(cmd[6] ? &cmd[7] : "");
        else print("Usage: attrib [+-RHS] filename\n");
    }
    
    else if (kstrncmp(cmd, "echo ", 5) == 0) print(&cmd[5]);
    else if (kstrcmp(cmd, "rand") == 0) {
        char buf[16]; itoa(k_rand(), buf, 10); print("Entropy Output: "); print(buf);
    }
    else if (kstrncmp(cmd, "color ", 6) == 0) {
        int new_color = katohex(&cmd[6]);
        if (new_color >= 0 && new_color <= 0xFF) { current_term_color = (uint8_t)new_color; print("Terminal color updated."); }
        else print("Invalid color code. Use hex format (e.g. color 0A)");
    }
    
    else if (kstrncmp(cmd, "calc ", 5) == 0) {
        char* expr = &cmd[5]; int a = 0, b = 0, res = 0, i = 0; char op = 0;
        while(expr[i] == ' ') i++;
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
    
    else if (kstrncmp(cmd, "beep ", 5) == 0) {
        int freq = katoi(&cmd[5]);
        if (freq > 0 && freq < 20000) { print("Tuning PIT to "); print(&cmd[5]); print(" Hz."); play_sound(freq); sleep(40); nosound(); }
        else print("Freq Out of Range (1-20000)");
    }
    else if (kstrcmp(cmd, "beep") == 0) boot_jingle();
    else if (kstrcmp(cmd, "music") == 0) {
        print("Audio Stream: Victory Theme"); uint32_t notes[] = {NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5}; uint32_t durations[] = {10, 10, 10, 30};
        play_song(notes, durations, 4);
    }
    else if (kstrncmp(cmd, "sb16 ", 5) == 0) {
        if (sb16_is_present()) {
            if (kstrcmp(&cmd[5], "test") == 0) {
                print("SB16: Playing test tone via DMA...\n");
                uint8_t wave[4096];
                for (int i = 0; i < 4096; i++) {
                    wave[i] = (uint8_t)(128 + 120 * ksin(i * 440 / 11));
                }
                sb16_play_dma(wave, 4096, 8000);
            } else { print("Usage: sb16 test\n"); }
        } else { print("SB16 not available.\n"); }
    }
    else if (kstrcmp(cmd, "siren") == 0) {
        print("Generating Waveform: Siren"); uint32_t notes[] = {880, 440, 880, 440, 880, 440}; uint32_t durations[] = {35, 35, 35, 35, 35, 35};
        play_song(notes, durations, 6);
    }
    else if (kstrcmp(cmd, "serial on") == 0) { serial_mirror = 1; print("Serial mirror ON.\n"); }
    else if (kstrcmp(cmd, "serial off") == 0) { serial_mirror = 0; print("Serial mirror OFF.\n"); }
    else if (kstrcmp(cmd, "serial") == 0) { print("Usage: serial on|off\nCurrently: "); print(serial_mirror ? "ON" : "OFF"); print("\n"); }
    else if (kstrcmp(cmd, "set") == 0) {
        print_col("Shell Variables:\n", COLOR_HELP);
        for (int i = 0; i < shell_var_count; i++) {
            print(shell_vars[i].name); print("="); print(shell_vars[i].value); print("\n");
        }
    }
    else if (kstrncmp(cmd, "export ", 7) == 0) {
        char* args = &cmd[7];
        char* eq2 = kstrchr(args, '=');
        if (eq2) {
            *eq2 = '\0';
            set_var(args, eq2 + 1);
            *eq2 = '=';
            print("Exported.\n");
        } else { print("Usage: export NAME=VALUE\n"); }
    }
    else if (kstrcmp(cmd, "clear") == 0) clear_screen();
    else if (kstrncmp(cmd, "grep ", 5) == 0) cmd_grep(&cmd[5]);
    else if (kstrcmp(cmd, "grep") == 0) cmd_grep("");
    else if (kstrncmp(cmd, "head ", 5) == 0) cmd_head(&cmd[5]);
    else if (kstrcmp(cmd, "head") == 0) cmd_head("");
    else if (kstrcmp(cmd, "wc") == 0) cmd_wc();
    else if (kstrcmp(cmd, "sort") == 0) cmd_sort();
    else if (kstrcmp(cmd, "spawn") == 0) {
        uint32_t* us = malloc(4096);
        if (!us) { print("malloc failed\n"); }
        else {
            int pid = create_user_process(user_test_proc, (uint32_t)(us + 1024));
            print("Spawned user process PID "); print_hex(pid); print("\n");
        }
    }
    else if (kstrcmp(cmd, "ps") == 0) {
        for (int i = 0; i < proc_count; i++) {
            print(" PID "); print_hex(i); print(": ");
            print(procs[i].state == 2 ? "RUNNING" : procs[i].state == 1 ? "READY" : "DEAD");
            print(" ring"); print_hex(procs[i].ring);
            print("\n");
        }
    }
    else if (kstrncmp(cmd, "kill ", 5) == 0) {
        int kpid = 0;
        for (int i = 5; cmd[i]; i++) { if (cmd[i] >= '0' && cmd[i] <= '9') kpid = kpid * 10 + (cmd[i] - '0'); }
        if (kpid > 0 && kpid < proc_count) {
            procs[kpid].state = 0;
            print("Killed PID "); print_hex(kpid); print("\n");
        } else { print("Invalid PID\n"); }
    }
    else if (kstrcmp(cmd, "matrix") == 0) run_matrix();
    else if (kstrcmp(cmd, "netstat") == 0) {
        print_col("--- AaronOS Network Stack ---\n", COLOR_HELP);
        print("Interface: RTL8139 (PCI)\n");
        print("IP Address: 10.0.2.15\n");
        print("Status: Link Active\n");
    }
    else if (kstrncmp(cmd, "web ", 4) == 0) { run_browser(&cmd[4]); }
    else if (kstrcmp(cmd, "web") == 0) { print("Usage: web [target_ip]\nExample: web 93.184.216.34\n"); }
    else if (kstrncmp(cmd, "ping ", 5) == 0) {
        char* ip = &cmd[5];
        int i0 = katoi(ip); while(*ip != '.' && *ip != '\0') ip++; ip++;
        int i1 = katoi(ip); while(*ip != '.' && *ip != '\0') ip++; ip++;
        int i2 = katoi(ip); while(*ip != '.' && *ip != '\0') ip++; ip++;
        int i3 = katoi(ip);
        net_ping(i0, i1, i2, i3);
    }
    else print("Unknown command. Type help for commands.");
}

/* ================================================================== */
/* PIPE-AWARE COMMANDS (grep, head, wc, sort)                        */
/* ================================================================== */

void cmd_grep(char* pattern) {
    while (*pattern == ' ') pattern++;
    if (!*pattern) { print("Usage: grep <pattern>\n"); return; }
    char line[256];
    int li = 0;
    int found = 0;
    for (int i = 0; i <= stdin_buffer_len; i++) {
        char c = (i < stdin_buffer_len) ? stdin_buffer[i] : '\n';
        if (c == '\n') {
            line[li] = '\0';
            if (kstrstr(line, pattern)) {
                print(line); print("\n");
                found++;
            }
            li = 0;
        } else {
            if (li < 255) line[li++] = c;
        }
    }
    if (!found && stdin_buffer_len > 0) { /* no match */ }
}

void cmd_head(char* args) {
    int n = 10;
    if (*args) { while (*args == ' ') args++; if (*args) n = katoi(args); if (n <= 0) n = 10; }
    int lines = 0;
    for (int i = 0; i < stdin_buffer_len && lines < n; i++) {
        putchar_col(stdin_buffer[i], current_term_color);
        if (stdin_buffer[i] == '\n') lines++;
    }
    if (stdin_buffer_len > 0 && stdin_buffer[stdin_buffer_len-1] != '\n') putchar_col('\n', current_term_color);
}

void cmd_wc() {
    int lines = 0, words = 0, chars = stdin_buffer_len;
    int in_word = 0;
    for (int i = 0; i < stdin_buffer_len; i++) {
        char c = stdin_buffer[i];
        if (c == '\n') lines++;
        if (c == ' ' || c == '\n' || c == '\t') { in_word = 0; }
        else if (!in_word) { in_word = 1; words++; }
    }
    char buf[16];
    itoa(lines, buf, 10); print(buf); print(" ");
    itoa(words, buf, 10); print(buf); print(" ");
    itoa(chars, buf, 10); print(buf); print("\n");
}

void cmd_sort() {
    /* Collect lines into array */
    char lines[128][256];
    int line_count = 0;
    char line[256];
    int li = 0;
    for (int i = 0; i <= stdin_buffer_len; i++) {
        char c = (i < stdin_buffer_len) ? stdin_buffer[i] : '\n';
        if (c == '\n') {
            line[li] = '\0';
            if (line_count < 128) {
                kstrcpy(lines[line_count], line);
                line_count++;
            }
            li = 0;
        } else {
            if (li < 255) line[li++] = c;
        }
    }
    /* Bubble sort */
    for (int i = 0; i < line_count - 1; i++) {
        for (int j = 0; j < line_count - 1 - i; j++) {
            if (kstrcmp(lines[j], lines[j+1]) > 0) {
                char tmp[256];
                kstrcpy(tmp, lines[j]);
                kstrcpy(lines[j], lines[j+1]);
                kstrcpy(lines[j+1], tmp);
            }
        }
    }
    for (int i = 0; i < line_count; i++) {
        print(lines[i]); print("\n");
    }
}

/* ================================================================== */
/* GLOB / WILDCARD MATCHING & EXPANSION                               */
/* ================================================================== */

int match_glob(const char* pattern, const char* name) {
    while (*pattern) {
        if (*pattern == '*') {
            pattern++;
            if (!*pattern) return 1;
            while (*name) {
                if (match_glob(pattern, name)) return 1;
                name++;
            }
            return 0;
        } else if (*pattern == '?' || *pattern == *name) {
            pattern++; name++;
        } else {
            return 0;
        }
    }
    return *name == '\0';
}

int has_glob(const char* str) {
    while (*str) { if (*str == '*' || *str == '?') return 1; str++; }
    return 0;
}

void expand_glob(char* glob, char (*results)[13], int* count) {
    *count = 0;
    char names[128][13];
    int num_files = fat16_collect_display_names(names, 128);
    for (int i = 0; i < num_files; i++) {
        if (match_glob(glob, names[i])) {
            kstrcpy(results[*count], names[i]);
            (*count)++;
        }
    }
}

/* Glob-aware dir: filter listing by pattern */
void cmd_dir_glob(char* pattern) {
    char names[128][13];
    int num_files = fat16_collect_display_names(names, 128);
    int found = 0;
    print("Files:\n");
    for (int i = 0; i < num_files; i++) {
        if (match_glob(pattern, names[i])) {
            char* end = names[i];
            while (*end) end++;
            int is_dir = (end > names[i] && *(end-1) == '/');
            if (is_dir) *(end-1) = '\0';
            print_col(names[i], is_dir ? 0x0A : 0x07);
            if (is_dir) print_col("  <DIR>\n", 0x0A);
            else print("\n");
            found = 1;
        }
    }
    if (!found) print("  (no matches)\n");
}

/* ================================================================== */
/* SHELL VARIABLES - initialization                                   */
/* ================================================================== */

void set_default_vars() {
    if (shell_var_count > 0) return;
    kstrcpy(shell_vars[shell_var_count].name, "PATH"); kstrcpy(shell_vars[shell_var_count].value, "/bin"); shell_var_count++;
    kstrcpy(shell_vars[shell_var_count].name, "HOME"); kstrcpy(shell_vars[shell_var_count].value, "/"); shell_var_count++;
    kstrcpy(shell_vars[shell_var_count].name, "USER"); kstrcpy(shell_vars[shell_var_count].value, "aaron"); shell_var_count++;
    kstrcpy(shell_vars[shell_var_count].name, "SHELL"); kstrcpy(shell_vars[shell_var_count].value, "/bin/sh"); shell_var_count++;
}

char* get_var(const char* name) {
    for (int i = 0; i < shell_var_count; i++)
        if (kstrcmp(shell_vars[i].name, name) == 0) return shell_vars[i].value;
    return NULL;
}

void set_var(const char* name, const char* value) {
    for (int i = 0; i < shell_var_count; i++) {
        if (kstrcmp(shell_vars[i].name, name) == 0) {
            kstrcpy(shell_vars[i].value, value);
            return;
        }
    }
    if (shell_var_count < MAX_VARS) {
        kstrcpy(shell_vars[shell_var_count].name, name);
        kstrcpy(shell_vars[shell_var_count].value, value);
        shell_var_count++;
    }
}

void expand_vars(char* input, char* output, int max_out) {
    int oi = 0;
    for (int i = 0; input[i] && oi < max_out - 1; i++) {
        if (input[i] == '$') {
            i++;
            char varname[VAR_NAME_LEN];
            int vi = 0;
            while (input[i] && ((input[i] >= 'A' && input[i] <= 'Z') || (input[i] >= 'a' && input[i] <= 'z') ||
                   (input[i] >= '0' && input[i] <= '9') || input[i] == '_')) {
                if (vi < VAR_NAME_LEN - 1) varname[vi++] = input[i];
                i++;
            }
            varname[vi] = '\0';
            i--;
            char* val = get_var(varname);
            if (val) for (int j = 0; val[j] && oi < max_out - 1; j++) output[oi++] = val[j];
        } else {
            output[oi++] = input[i];
        }
    }
    output[oi] = '\0';
}

/* ================================================================== */
/* COMMAND HISTORY                                                    */
/* ================================================================== */
#define MAX_HISTORY 16
char history[MAX_HISTORY][256];
int history_count = 0;
int history_pos = -1;
char history_temp[256];

void save_to_history(char* cmd) {
    if (!cmd || !*cmd) return;
    if (history_count > 0 && kstrcmp(history[history_count - 1], cmd) == 0) return;
    if (history_count < MAX_HISTORY) {
        kstrcpy(history[history_count], cmd);
        history_count++;
    } else {
        for (int i = 1; i < MAX_HISTORY; i++) kstrcpy(history[i - 1], history[i]);
        kstrcpy(history[MAX_HISTORY - 1], cmd);
    }
}

void redraw_input_line() {
    current_row = prompt_row;
    current_col = prompt_limit;
    int clr_col = current_col, clr_row = current_row;
    for (int i = 0; i < 256; i++) {
        if (clr_col >= SCREEN_WIDTH) { clr_col = 0; clr_row++; }
        if (clr_row >= MAX_SCROLLBACK) break;
        terminal_buffer[clr_row][clr_col] = ' ' | (current_term_color << 8);
        clr_col++;
    }
    current_row = prompt_row;
    current_col = prompt_limit;
    for (int i = 0; i < input_ptr; i++) {
        if (!input_buffer[i]) break;
        if (current_col >= SCREEN_WIDTH) { current_col = 0; current_row++; }
        terminal_buffer[current_row][current_col] = (uint16_t)input_buffer[i] | (current_term_color << 8);
        current_col++;
    }
    auto_scroll();
    refresh_screen();
}

void handle_history_up() {
    if (history_count == 0) return;
    if (history_pos == -1) {
        kstrcpy(history_temp, input_buffer);
        history_pos = history_count - 1;
    } else if (history_pos > 0) {
        history_pos--;
    } else {
        return;
    }
    kstrcpy(input_buffer, history[history_pos]);
    input_ptr = kstrlen(input_buffer);
    redraw_input_line();
}

void handle_history_down() {
    if (history_pos == -1) return;
    history_pos++;
    if (history_pos >= history_count) {
        kstrcpy(input_buffer, history_temp);
        input_ptr = kstrlen(input_buffer);
        history_pos = -1;
    } else {
        kstrcpy(input_buffer, history[history_pos]);
        input_ptr = kstrlen(input_buffer);
    }
    redraw_input_line();
}

/* ================================================================== */
/* TAB COMPLETION                                                     */
/* ================================================================== */

void handle_tab_completion() {
    if (input_ptr == 0) return;
    int word_start = 0;
    for (int i = input_ptr - 1; i >= 0; i--) {
        if (input_buffer[i] == ' ') { word_start = i + 1; break; }
    }
    int word_len = input_ptr - word_start;
    if (word_len == 0) return;

    char prefix[64];
    int pi = 0;
    for (int i = word_start; i < input_ptr && pi < 63; i++) prefix[pi++] = input_buffer[i];
    prefix[pi] = '\0';
    for (int i = 0; prefix[i]; i++) if (prefix[i] >= 'A' && prefix[i] <= 'Z') prefix[i] += 32;

    char candidates[64][64];
    int num_candidates = 0;

    /* Match commands (first word only) */
    if (word_start == 0) {
        for (int i = 0; i < NUM_COMMANDS && num_candidates < 64; i++) {
            const char* cn = commands[i].name;
            int match = 1;
            for (int j = 0; prefix[j]; j++) {
                char c = cn[j]; if (c >= 'A' && c <= 'Z') c += 32;
                if (c != prefix[j]) { match = 0; break; }
            }
            if (match && cn[0]) kstrcpy(candidates[num_candidates++], cn);
        }
    }

    /* Match filenames */
    char fnames[128][13];
    int nf = fat16_collect_display_names(fnames, 128);
    for (int i = 0; i < nf && num_candidates < 64; i++) {
        char name[13];
        kstrcpy(name, fnames[i]);
        int nl = kstrlen(name);
        if (nl > 0 && name[nl-1] == '/') name[nl-1] = '\0';
        int match = 1;
        for (int j = 0; prefix[j]; j++) {
            char nc = name[j]; if (nc >= 'A' && nc <= 'Z') nc += 32;
            if (nc != prefix[j]) { match = 0; break; }
        }
        if (match) kstrcpy(candidates[num_candidates++], name);
    }

    if (num_candidates == 0) return;

    /* Show all candidates if multiple */
    if (num_candidates > 1) {
        print("\n");
        for (int i = 0; i < num_candidates; i++) { print(candidates[i]); print("  "); }
        print("\n");
    }

    /* Find longest common prefix */
    char completion[64];
    kstrcpy(completion, candidates[0]);
    if (num_candidates > 1) {
        for (int i = 1; i < num_candidates; i++) {
            int j = 0;
            while (completion[j] && candidates[i][j] && completion[j] == candidates[i][j]) j++;
            completion[j] = '\0';
        }
    }

    int comp_len = kstrlen(completion);
    input_ptr = word_start;
    for (int i = 0; i < comp_len && input_ptr < 254; i++) input_buffer[input_ptr++] = completion[i];
    input_buffer[input_ptr] = '\0';

    redraw_input_line();
}

void process_shell() {
    if (ctrl_c_flag) {
        ctrl_c_flag = 0;
        print("^C\n");
        goto reset_prompt;
    }
    print("\n");
    sys_stats.total_commands++;

    if (input_ptr > 0) {
        input_buffer[input_ptr] = '\0';
        save_to_history(input_buffer);

        /* Check for ./script syntax */
        if (input_ptr >= 2 && input_buffer[0] == '.' && input_buffer[1] == '/') {
            run_script(&input_buffer[2]);
            goto reset_prompt;
        }

        if (kstrcmp(input_buffer, "gui") == 0) { launch_tui(); return; }
        if (kstrncmp(input_buffer, "web ", 4) == 0) { run_browser(&input_buffer[4]); return; }

        /* Check for variable assignment (NAME=VALUE) */
        char* eq = kstrchr(input_buffer, '=');
        if (eq && eq > input_buffer) {
            int has_space = 0;
            for (char* p = input_buffer; p < eq; p++) if (*p == ' ') { has_space = 1; break; }
            if (!has_space) {
                char vn[64]; int vnl = eq - input_buffer;
                for (int i = 0; i < vnl && i < 63; i++) vn[i] = input_buffer[i];
                vn[vnl < 63 ? vnl : 63] = '\0';
                set_var(vn, eq + 1);
                print("Variable set.\n");
                goto reset_prompt;
            }
        }

        /* Expand $VARIABLE references */
        char expanded[256];
        expand_vars(input_buffer, expanded, 256);
        if (kstrcmp(input_buffer, expanded) != 0) {
            kstrcpy(input_buffer, expanded);
            input_ptr = kstrlen(input_buffer);
        }

        /* Check for I/O redirection (>, >>, <) */
        {
            char out_file[128] = "", in_file[128] = "";
            int append = 0, has_redir = 0;
            char* gt = NULL, *lt = NULL;
            for (int i = input_ptr - 1; i >= 0; i--) {
                if (input_buffer[i] == '>' && !gt) { gt = &input_buffer[i]; }
                if (input_buffer[i] == '<' && !lt) { lt = &input_buffer[i]; }
            }
            if (gt) {
                has_redir = 1;
                char* fp = gt + 1;
                if (*fp == '>') { append = 1; fp++; }
                while (*fp == ' ') fp++;
                int fi = 0;
                while (*fp && *fp != ' ' && fi < 127) out_file[fi++] = *fp++;
                out_file[fi] = '\0';
                *gt = '\0';
                input_ptr = gt - input_buffer;
                while (input_ptr > 0 && input_buffer[input_ptr-1] == ' ') input_ptr--;
                input_buffer[input_ptr] = '\0';
            }
            if (lt) {
                has_redir = 1;
                char* fp = lt + 1;
                while (*fp == ' ') fp++;
                int fi = 0;
                while (*fp && *fp != ' ' && fi < 127) in_file[fi++] = *fp++;
                in_file[fi] = '\0';
                *lt = '\0';
                input_ptr = lt - input_buffer;
                while (input_ptr > 0 && input_buffer[input_ptr-1] == ' ') input_ptr--;
                input_buffer[input_ptr] = '\0';
            }
            if (has_redir) {
                if (in_file[0]) {
                    int flen = fat16_read_file(in_file, stdin_buffer, PIPE_BUF_SIZE);
                    if (flen < 0) { print("Input file not found.\n"); goto reset_prompt; }
                    stdin_buffer_len = flen;
                    stdin_buffer[flen] = '\0';
                }
                if (out_file[0]) {
                    pipe_output_active = 1;
                    pipe_buffer_len = 0;
                }
                run_command(input_buffer);
                if (out_file[0]) {
                    pipe_output_active = 0;
                    pipe_buffer[pipe_buffer_len] = '\0';
                    if (append) {
                        char existing[PIPE_BUF_SIZE];
                        int exlen = fat16_read_file(out_file, existing, PIPE_BUF_SIZE);
                        if (exlen < 0) exlen = 0;
                        existing[exlen] = '\0';
                        char combined[PIPE_BUF_SIZE];
                        int ci = 0;
                        for (int i = 0; existing[i] && ci < PIPE_BUF_SIZE-2; i++) combined[ci++] = existing[i];
                        for (int i = 0; pipe_buffer[i] && ci < PIPE_BUF_SIZE-2; i++) combined[ci++] = pipe_buffer[i];
                        combined[ci] = '\0';
                        fat16_write_file(out_file, combined);
                    } else {
                        fat16_write_file(out_file, pipe_buffer);
                    }
                }
                goto reset_prompt;
            }
        }

        /* Check for pipe (|) */
        char* pipe_pos = kstrchr(input_buffer, '|');
        if (pipe_pos) {
            *pipe_pos = '\0';
            char* cmd1 = input_buffer;
            char* cmd2 = pipe_pos + 1;
            while (*cmd2 == ' ') cmd2++;

            /* Run first command with output capture */
            pipe_output_active = 1;
            pipe_buffer_len = 0;
            run_command(cmd1);
            pipe_output_active = 0;
            pipe_buffer[pipe_buffer_len] = '\0';

            /* Copy captured output to stdin buffer */
            kstrcpy(stdin_buffer, pipe_buffer);
            stdin_buffer_len = pipe_buffer_len;

            /* Trim trailing newlines from stdin buffer for cleaner display */
            while (stdin_buffer_len > 0 && stdin_buffer[stdin_buffer_len-1] == '\n')
                stdin_buffer[--stdin_buffer_len] = '\0';

            /* Run second command */
            *pipe_pos = '|'; /* restore input_buffer */
            run_command(cmd2);
            goto reset_prompt;
        }

        run_command(input_buffer);
    }
    
reset_prompt:
    char cwd_buf[128];
    fat16_get_cwd(cwd_buf, 128);
    print("\n");
    prompt_row = current_row;
    print_col("AaronOS", COLOR_SUCCESS);
    if (cwd_buf[0]) { print_col(":", COLOR_DEFAULT); print_col(cwd_buf, COLOR_HELP); }
    print_col("> ", COLOR_DEFAULT);
    input_ptr = 0;
    execute_flag = 0;
    ctrl_c_flag = 0;
    prompt_limit = current_col;
    update_cursor_relative();
}

/* ========================================================================== */
/* 12. SEGMENTATION (GDT) & INTERRUPTS (IDT)                                  */
/* ========================================================================== */

/* Global Descriptor Table layout */
struct gdt_entry {
    uint16_t limit_low; uint16_t base_low; uint8_t base_middle; uint8_t access;
    uint8_t granularity; uint8_t base_high;
} __attribute__((packed)) gdt[6];

struct gdt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed)) gp;

uint8_t tss[104];

/* Fills out a single GDT entry */
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF); gdt[num].base_middle = (base >> 16) & 0xFF; gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF); gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0); gdt[num].access = access;
}

/* Initializes flat memory model with ring 3 support */
void init_gdt() {
    gp.limit = (sizeof(struct gdt_entry) * 6) - 1; gp.base = (uint32_t)&gdt;
    gdt_set_gate(0, 0, 0, 0, 0);          /* Null */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); /* Kernel code 0x08 */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); /* Kernel data 0x10 */
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); /* User code   0x18 */
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); /* User data   0x20 */

    uint32_t tss_base = (uint32_t)&tss;
    gdt_set_gate(5, tss_base, 103, 0x89, 0x00); /* TSS         0x28 */

    asm volatile("lgdt %0" : : "m"(gp));
    asm volatile("ljmp $0x08, $1f\n1:" : : : "memory");
    asm volatile("mov $0x10, %%ax; mov %%ax, %%ds; mov %%ax, %%es; mov %%ax, %%fs; mov %%ax, %%gs; mov %%ax, %%ss" : : : "eax");

    *(uint16_t*)(tss + 0) = 0;
    *(uint32_t*)(tss + 4) = 0;
    *(uint16_t*)(tss + 8) = 0x10;
    *(uint16_t*)(tss + 88) = 104;

    asm volatile("ltr %%ax" : : "a"(0x28));
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
    
    // IRQ14 (Primary ATA)
    uint32_t ah = (uint32_t)ata_handler_asm;
    idt[46].base_lo = ah & 0xFFFF; idt[46].base_hi = (ah >> 16) & 0xFFFF; idt[46].sel = 0x08; idt[46].always0 = 0; idt[46].flags = 0x8E;

    // IRQ12 (PS/2 Mouse)
    uint32_t mh = (uint32_t)mouse_handler_asm;
    idt[44].base_lo = mh & 0xFFFF; idt[44].base_hi = (mh >> 16) & 0xFFFF;
    idt[44].sel = 0x08; idt[44].always0 = 0; idt[44].flags = 0x8E;

    // Syscall (int 0x80) — DPL=3 so user mode can call it
    uint32_t sch = (uint32_t)syscall_handler_asm;
    idt[0x80].base_lo = sch & 0xFFFF; idt[0x80].base_hi = (sch >> 16) & 0xFFFF;
    idt[0x80].sel = 0x08; idt[0x80].always0 = 0; idt[0x80].flags = 0xEE;

    load_idt((uint32_t)&idtp); // Assembly call to load IDT
}

#define SYS_WRITE 1
#define SYS_READ  2
#define SYS_EXIT  3
#define SYS_PIPE_CREATE  4
#define SYS_PIPE_WRITE   5
#define SYS_PIPE_READ    6
#define SYS_SHM_CREATE   7
#define SYS_SHM_ATTACH   8

#define MAX_PIPES 16
#define PIPE_SIZE 4096
#define MAX_SHM 16

typedef struct {
    int used;
    int read_pos;
    int write_pos;
    uint8_t buf[PIPE_SIZE];
} pipe_t;

static pipe_t pipes[MAX_PIPES];

typedef struct {
    int used;
    uint32_t key;
    uint32_t size;
    uint8_t* data;
} shm_t;

static shm_t shm_regions[MAX_SHM];

int pipe_create() {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].used) {
            pipes[i].used = 1;
            pipes[i].read_pos = 0;
            pipes[i].write_pos = 0;
            return i;
        }
    }
    return -1;
}

int pipe_write(int id, const uint8_t* data, int len) {
    if (id < 0 || id >= MAX_PIPES || !pipes[id].used) return -1;
    pipe_t* p = &pipes[id];
    int written = 0;
    for (int i = 0; i < len; i++) {
        int next = (p->write_pos + 1) % PIPE_SIZE;
        if (next == p->read_pos) break;
        p->buf[p->write_pos] = data[i];
        p->write_pos = next;
        written++;
    }
    return written;
}

int pipe_read(int id, uint8_t* data, int len) {
    if (id < 0 || id >= MAX_PIPES || !pipes[id].used) return -1;
    pipe_t* p = &pipes[id];
    int read = 0;
    while (read < len && p->read_pos != p->write_pos) {
        data[read++] = p->buf[p->read_pos];
        p->read_pos = (p->read_pos + 1) % PIPE_SIZE;
    }
    return read;
}

int shm_create(uint32_t key, uint32_t size) {
    for (int i = 0; i < MAX_SHM; i++) {
        if (!shm_regions[i].used) {
            shm_regions[i].data = malloc(size);
            if (!shm_regions[i].data) return -1;
            shm_regions[i].used = 1;
            shm_regions[i].key = key;
            shm_regions[i].size = size;
            return i;
        }
    }
    return -1;
}

uint32_t shm_attach(uint32_t key) {
    for (int i = 0; i < MAX_SHM; i++) {
        if (shm_regions[i].used && shm_regions[i].key == key)
            return (uint32_t)shm_regions[i].data;
    }
    return 0;
}

void syscall_handler_main(uint32_t* frame) {
    uint32_t num = frame[0];  // eax
    uint32_t arg1 = frame[3]; // ebx
    uint32_t arg2 = frame[1]; // ecx
    uint32_t arg3 = frame[2]; // edx
    switch (num) {
        case SYS_WRITE:
            for (uint32_t i = 0; i < arg3; i++) putchar_col(((char*)arg2)[i], 0x07);
            frame[0] = arg3;
            break;
        case SYS_READ:
            frame[0] = 0;
            break;
        case SYS_EXIT:
            procs[cur_pid].state = 0;
            frame[0] = 0;
            break;
        case SYS_PIPE_CREATE:
            frame[0] = pipe_create();
            break;
        case SYS_PIPE_WRITE:
            frame[0] = pipe_write(arg1, (uint8_t*)arg2, arg3);
            break;
        case SYS_PIPE_READ:
            frame[0] = pipe_read(arg1, (uint8_t*)arg2, arg3);
            break;
        case SYS_SHM_CREATE:
            frame[0] = shm_create(arg1, arg2);
            break;
        case SYS_SHM_ATTACH:
            frame[0] = shm_attach(arg1);
            break;
        default:
            frame[0] = -1;
            break;
    }
}

void ata_irq_handler() {
    inb(0x1F7);          /* read status to acknowledge interrupt */
    ata_irq_flag = 1;
}

static int ata_irq_initialized = 0;

void ata_init_irq() {
    if (ata_irq_initialized) return;
    /* Unmask IRQ14 on slave PIC (bit 6 = 0x40) */
    uint8_t mask = inb(0xA1);
    outb(0xA1, mask & ~0x40);
    ata_irq_initialized = 1;
    print("ATA IRQ enabled.\n");
}

/* ========================================================================== */
/* 13. KERNEL ENTRY POINT AND NETWORK                                                    */
/* ========================================================================== */

extern void net_init(uint32_t io_base);
extern void net_send_raw_packet(uint8_t* dest_mac, uint16_t protocol, uint8_t* payload, uint32_t payload_len);
extern uint8_t my_mac[6]; // From net.c

/* Reads a 16-bit value from the PCI configuration space */
uint16_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | 0x80000000);
    outl(0xCF8, address);
    return (uint16_t)((inl(0xCFC) >> ((offset & 2) * 8)) & 0xffff);
}

/* Scans all buses/slots for the Realtek 8139 */
void pci_scan() {
    print_col("[PCI] Scanning hardware bus...\n", COLOR_HELP);
    for(int bus = 0; bus < 256; bus++) {
        for(int slot = 0; slot < 32; slot++) {
            uint16_t vendor = pci_config_read_word(bus, slot, 0, 0);
            if(vendor != 0xFFFF) {
                uint16_t device = pci_config_read_word(bus, slot, 0, 2);
                
                if(vendor == 0x10EC && device == 0x8139) {
                    print_col("[PCI] Found RTL8139 Network Card!\n", COLOR_SUCCESS);
                    
                    // 1. Read the PCI Command Register (Offset 0x04)
                    uint16_t cmd = pci_config_read_word(bus, slot, 0, 0x04);
                    
                    // 2. Enable Bus Mastering (Bit 2) and I/O Space (Bit 0)
                    cmd |= 0x0005; 
                    
                    // 3. Write it back to the PCI Bus
                    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (0 << 8) | 0x04 | 0x80000000);
                    outl(0xCF8, address);
                    outw(0xCFC, cmd); // Grant the card permission to read RAM!

                    // 4. Get Base Address (BAR0) and Init
                    uint32_t bar0_low = pci_config_read_word(bus, slot, 0, 0x10);
                    uint32_t bar0_high = pci_config_read_word(bus, slot, 0, 0x12);
                    uint32_t bar0 = (bar0_high << 16) | bar0_low;
                    
                    net_init(bar0 & ~0x3); 
                    return;
                }
            }
        }
    }
    print_col("[PCI] Network card not found.\n", COLOR_ALERT);
}
void kernel_main() {
    /* Wipe VGA memory directly first — kill any GRUB splash artifacts */
    kmemset(video_mem, 0, SCREEN_WIDTH * SCREEN_HEIGHT * 2);
    current_col = 0; current_row = 0; scroll_offset = 0; in_gui_mode = 0; boot_log_count = 0;
    clear_screen();
    
    print_col("\n[ AaronOS Boot Sequence Initiated ]\n\n", COLOR_HELP);

    init_gdt(); log_boot("Global Descriptor Table (GDT) Configured");
    init_serial(); log_boot("Serial Port (COM1) Initialized");
    /* Auto-enable serial mirror if QEMU -device loader set flag at 0x500 */
    if (*(uint8_t*)0x500 == 0xBB) {
        serial_mirror = 1;
        *(volatile uint8_t*)0x500 = 0; /* clear flag */
    }

    /* 8259 PIC Remapping Magic Numbers */
    outb(0x20, 0x11); io_wait(); outb(0x21, 0x20); io_wait(); outb(0x21, 0x04); io_wait(); outb(0x21, 0x01); io_wait();
    outb(0xA0, 0x11); io_wait(); outb(0xA1, 0x28); io_wait(); outb(0xA1, 0x02); io_wait(); outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xFC); outb(0xA1, 0xFF);
    log_boot("8259 Programmable Interrupt Controller Remapped");

    init_idt(); log_boot("Interrupt Descriptor Table (IDT) Loaded");
    init_timer(100); log_boot("Programmable Interval Timer (PIT) bound to 100Hz");

    sys_stats.disk_presence = ata_init();
    if (sys_stats.disk_presence) { log_boot("ATA Disk Detected"); ata_init_irq(); }
    else log_boot("ATA Disk Not Found");

    sys_stats.uptime_ticks = 0; sys_stats.total_commands = 0; sys_stats.speaker_state = 0;
    set_default_vars();

    if (acpi_init()) log_boot("ACPI (Advanced Config & Power Interface) Detected");
    else log_boot("ACPI Not Found");

    dma_init(); log_boot("ISA DMA Controller (8237) Initialized");

    if (sb16_init()) log_boot("Sound Blaster 16 Audio Detected");
    else log_boot("SB16 Audio Not Found");

    if (ahci_init()) log_boot("AHCI SATA Controller Detected");
    else log_boot("AHCI Not Found");

    if (mouse_init()) {
        outb(0x21, inb(0x21) & ~0x04);
        outb(0xA1, inb(0xA1) & ~0x10);
        log_boot("PS/2 Mouse Enabled");
    }
    else log_boot("PS/2 Mouse Not Found");

    smp_init();
    {
        char buf[16];
        int nc = smp_cpu_count();
        if (nc > 1) { print_col("[SMP] ", COLOR_HELP); itoa(nc, buf, 10); print_col(buf, COLOR_SUCCESS); print_col(" CPUs detected\n", COLOR_SUCCESS); }
    }

    log_boot("Virtual Terminal Scrollback Buffer Allocated (500 Lines)");
    log_boot("VGA DMA Hook established at 0xB8000");
    log_boot("Math libraries (ksin, kcos, ksqrt) initialized");
    log_boot("TUI Graphics Engine Loaded");
    log_boot("Network Driver Launched");
    
    // Pause to let user see boot checks
    for(volatile int i=0; i<30000000; i++); 
    
    asm volatile("sti"); // Start accepting hardware interrupts
    log_boot("Hardware Interrupts (STI) Enabled");

    procs[0].state = 1; procs[0].pid = 0; proc_count = 1; cur_pid = 0;

    // Final pause
    for(volatile int i=0; i<15000000; i++); 
    clear_screen();
    
    pci_scan();
    boot_jingle(); // Play startup sound
    print("Welcome to AaronOS! \n Use help for commands.\n");
    print("AaronOS> ");
    prompt_limit = current_col;

    // Infinite idle loop
   /* Master Execution Loop */
    /* Master Loop - Force Net-Poll on every single tick */
    while (1) { 
        if (execute_flag == 1 ) {
            process_shell(); 
            execute_flag = 0;
        }

        // Poll serial RX — only echo to VGA if serial mirror is on
        while (inb(0x3FD) & 1) {
            char c = inb(0x3F8);
            if (serial_mirror) {
                putchar_col(c, 0x07);
                if (c == '\r') { c = '\n'; putchar_col(c, 0x07); }
            }
            if (c == '\b' && input_ptr > 0) { input_ptr--; input_buffer[input_ptr] = 0; }
            else if (c != '\r' && input_ptr < 255) {
                input_buffer[input_ptr++] = c;
                input_buffer[input_ptr] = 0;
            }
            if (c == '\n') execute_flag = 1;
        }

        net_poll();
        net_poll();
        net_poll();

        mouse_render();
        asm volatile("hlt"); 
    }
}
