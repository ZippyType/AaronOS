#include <stdint.h>
#include <stddef.h>
#include "io.h"
#include "version.h"
#include "fat16.h"
#include "drivers/ps2m.h"

#define SCREEN_WIDTH       80
#define SCREEN_HEIGHT      25
#define TUI_BG_COLOR       0x1F
#define TUI_WIN_COLOR      0x70
#define TUI_HL_COLOR       0x0F
#define TUI_BAR_COLOR      0x8F
#define BOX_HLINE          0xCD
#define BOX_VLINE          0xBA
#define BOX_TL             0xC9
#define BOX_TR             0xBB
#define BOX_BL             0xC8
#define BOX_BR             0xBC

typedef struct { uint8_t s, m, h, d, mo; uint32_t y; } rtc_t;
extern rtc_t system_time;
extern volatile uint32_t timer_ticks;
extern uint16_t* video_mem;
extern char input_buffer[256];
extern int input_ptr;
extern volatile int execute_flag;
extern int current_col;
extern int prompt_limit;

void putchar_at(char c, uint8_t color, int x, int y);
void print_at(const char* str, uint8_t color, int x, int y);
void print(const char* str);
void print_col(const char* str, uint8_t col);
void refresh_screen();
void update_cursor_relative();
void read_rtc();

void sleep(uint32_t ticks);
void itoa(int num, char* str, int base);
size_t kstrlen(const char* str);
void kstrcpy(char* dst, const char* src);
int kstrcmp(const char* s1, const char* s2);
int kstrncmp(const char* s1, const char* s2, size_t n);
void fat16_cd(char* name);
void fat16_get_cwd(char* buf, int max);
void fat16_cat(char* name);
void run_command(char* cmd);
extern int pipe_output_active;
extern char pipe_buffer[4096];
extern int pipe_buffer_len;

int in_gui_mode = 0;
int tui_state = 0;
int tui_selected_item = 0;
int tui_max_items = 0;
int tui_needs_redraw = 1;
int file_scroll = 0;
int fb_entry_count = 0;
fat16_entry_t fb_entries[128];
char fb_cwd[128] = "";
char calc_input[32];
int calc_input_len = 0;

static int day_of_week(int y, int m, int d) {
    static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    y -= m < 3;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

static int days_in_month(int m, int y) {
    if (m == 2) return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 29 : 28;
    if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
    return 31;
}

static void refresh_file_list() {
    fb_entry_count = fat16_collect_entries(fb_entries, 128);
    fat16_get_cwd(fb_cwd, 128);
    file_scroll = 0;
}

void tui_draw_desktop() {
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++)
        video_mem[i] = (uint16_t)0xB1 | (TUI_BG_COLOR << 8);

    for (int x = 0; x < SCREEN_WIDTH; x++) putchar_at(' ', TUI_BAR_COLOR, x, 0);
    print_at(" AaronOS Desktop Environment", TUI_BAR_COLOR, 0, 0);

    read_rtc();
    char tbuf[32];
    itoa(system_time.h, tbuf, 10);
    int ti = 0;
    while (tbuf[ti]) ti++;
    tbuf[ti++] = ':';
    if (system_time.m < 10) tbuf[ti++] = '0';
    itoa(system_time.m, tbuf + ti, 10);
    while (tbuf[ti]) ti++;
    tbuf[ti++] = ':';
    if (system_time.s < 10) tbuf[ti++] = '0';
    itoa(system_time.s, tbuf + ti, 10);
    while (tbuf[ti]) ti++;
    tbuf[ti] = '\0';
    print_at(tbuf, TUI_BAR_COLOR, SCREEN_WIDTH - 10, 0);

    for (int x = 0; x < SCREEN_WIDTH; x++) putchar_at(' ', TUI_BAR_COLOR, x, SCREEN_HEIGHT - 1);
    print_at(" \x18\x19 Navigate   \x11 Select   ESC Back/Exit", TUI_BAR_COLOR, 0, SCREEN_HEIGHT - 1);
}

void tui_draw_window(int x, int y, int w, int h, const char* title) {
    for (int r = 1; r < h; r++)
        for (int c = 1; c < w; c++)
            putchar_at(' ', 0x08, x + c + 1, y + r + 1);
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++) {
            char ch = ' ';
            if (r == 0 && c == 0) ch = BOX_TL;
            else if (r == 0 && c == w - 1) ch = BOX_TR;
            else if (r == h - 1 && c == 0) ch = BOX_BL;
            else if (r == h - 1 && c == w - 1) ch = BOX_BR;
            else if (r == 0 || r == h - 1) ch = BOX_HLINE;
            else if (c == 0 || c == w - 1) ch = BOX_VLINE;
            putchar_at(ch, TUI_WIN_COLOR, x + c, y + r);
        }
    int t_len = kstrlen(title);
    print_at(title, TUI_WIN_COLOR | 0x0F, x + (w - t_len) / 2, y);
}

void tui_render_main_menu() {
    tui_max_items = 5;
    int w = 42, h = 12;
    int x = (SCREEN_WIDTH - w) / 2, y = (SCREEN_HEIGHT - h) / 2;
    tui_draw_window(x, y, w, h, " Main Menu ");
    char* items[] = {
        " 1. Browse Filesystem    ",
        " 2. System Monitor       ",
        " 3. Calendar             ",
        " 4. Calculator           ",
        " 5. About AaronOS        ",
        " 6. Exit to Terminal     "
    };
    for (int i = 0; i < 6; i++) {
        uint8_t col = (i == tui_selected_item) ? TUI_HL_COLOR : TUI_WIN_COLOR;
        print_at(items[i], col, x + 5, y + 2 + i);
    }
}

void tui_render_file_browser() {
    tui_max_items = fb_entry_count < 7 ? fb_entry_count : 7;
    int w = 62, h = 14;
    int x = (SCREEN_WIDTH - w) / 2, y = (SCREEN_HEIGHT - h) / 2;
    char title[64];
    int t = 0;
    for (int i = 0; fb_cwd[i] && i < 50; i++) title[t++] = fb_cwd[i];
    title[t] = '\0';
    tui_draw_window(x, y, w, h, " File Browser ");

    /* show current path */
    print_at("Path: ", TUI_WIN_COLOR, x + 2, y + 1);
    print_at(fb_cwd[0] ? fb_cwd : "/", TUI_WIN_COLOR | 0x0B, x + 8, y + 1);

    /* column headers */
    print_at(" Name                          Size     Type ", TUI_WIN_COLOR, x + 2, y + 2);
    for (int i = 2; i < w - 2; i++) putchar_at(BOX_HLINE, TUI_WIN_COLOR, x + i, y + 3);

    int display_count = fb_entry_count < 7 ? fb_entry_count : 7;
    if (fb_entry_count > 7 && tui_selected_item >= 3)
        file_scroll = tui_selected_item - 3;
    if (file_scroll + display_count > fb_entry_count)
        file_scroll = fb_entry_count - display_count;
    if (file_scroll < 0) file_scroll = 0;

    for (int i = 0; i < display_count; i++) {
        int idx = file_scroll + i;
        uint8_t col = (idx == tui_selected_item + file_scroll) ? TUI_HL_COLOR : TUI_WIN_COLOR;
        char line[55];
        int li = 0;
        for (int j = 0; fb_entries[idx].name[j] && j < 28; j++) line[li++] = fb_entries[idx].name[j];
        while (li < 30) line[li++] = ' ';
        char buf[16];
        if (!(fb_entries[idx].attrs & 0x10)) {
            itoa(fb_entries[idx].size, buf, 10);
            for (int j = 0; buf[j]; j++) line[li++] = buf[j];
        }
        while (li < 40) line[li++] = ' ';
        if (fb_entries[idx].attrs & 0x10) { line[li++] = '<'; line[li++] = 'D'; line[li++] = 'I'; line[li++] = 'R'; line[li++] = '>'; }
        else { line[li++] = 'F'; line[li++] = 'I'; line[li++] = 'L'; line[li++] = 'E'; }
        line[li] = '\0';
        print_at(line, col, x + 2, y + 4 + i);
    }

    if (fb_entry_count > 7) {
        char sbar[4];
        itoa(file_scroll + 1, sbar, 10);
        print_at("v", TUI_WIN_COLOR, x + w - 3, y + 4);
        print_at(sbar, TUI_WIN_COLOR, x + w - 8, y + h - 1);
    }
}

void tui_render_sysmon() {
    tui_max_items = 1;
    int w = 50, h = 14;
    int x = (SCREEN_WIDTH - w) / 2, y = (SCREEN_HEIGHT - h) / 2;
    tui_draw_window(x, y, w, h, " System Monitor ");
    read_rtc();

    char buf[32];
    print_at(" AaronOS Engine Core", TUI_WIN_COLOR | 0x0E, x + 2, y + 2);

    print_at(" Uptime (ticks): ", TUI_WIN_COLOR, x + 2, y + 4);
    itoa(timer_ticks, buf, 10); print_at(buf, TUI_WIN_COLOR | 0x09, x + 20, y + 4);

    print_at(" Time: ", TUI_WIN_COLOR, x + 2, y + 5);
    itoa(system_time.h, buf, 10); print_at(buf, TUI_WIN_COLOR | 0x09, x + 10, y + 5);
    print_at(":", TUI_WIN_COLOR | 0x09, x + 12, y + 5);
    itoa(system_time.m, buf, 10); print_at(buf, TUI_WIN_COLOR | 0x09, x + 13, y + 5);
    print_at(":", TUI_WIN_COLOR | 0x09, x + 15, y + 5);
    itoa(system_time.s, buf, 10); print_at(buf, TUI_WIN_COLOR | 0x09, x + 16, y + 5);

    print_at(" Date: ", TUI_WIN_COLOR, x + 2, y + 6);
    itoa(system_time.mo, buf, 10); print_at(buf, TUI_WIN_COLOR | 0x09, x + 10, y + 6);
    print_at("/", TUI_WIN_COLOR | 0x09, x + 12, y + 6);
    itoa(system_time.d, buf, 10); print_at(buf, TUI_WIN_COLOR | 0x09, x + 13, y + 6);
    print_at("/", TUI_WIN_COLOR | 0x09, x + 15, y + 6);
    itoa(system_time.y, buf, 10); print_at(buf, TUI_WIN_COLOR | 0x09, x + 16, y + 6);

    uint32_t ebx, ecx, edx;
    asm volatile("cpuid" : "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    char vendor[13];
    *((uint32_t*)vendor) = ebx; *((uint32_t*)(vendor + 4)) = edx; *((uint32_t*)(vendor + 8)) = ecx;
    vendor[12] = '\0';
    print_at(" CPU: ", TUI_WIN_COLOR, x + 2, y + 8);
    print_at(vendor, TUI_WIN_COLOR | 0x09, x + 8, y + 8);

    print_at(" Disk: ATA Hard Disk @ 10 MB", TUI_WIN_COLOR, x + 2, y + 9);
    print_at(" FS: FAT16 (512 B/sector, 1 SPC)", TUI_WIN_COLOR, x + 2, y + 10);

    uint8_t col = (tui_selected_item == 0) ? TUI_HL_COLOR : TUI_WIN_COLOR;
    print_at(" [ RETURN ] ", col, x + (w - 12) / 2, y + h - 2);
}

void tui_render_about() {
    tui_max_items = 1;
    int w = 42, h = 12;
    int x = (SCREEN_WIDTH - w) / 2, y = (SCREEN_HEIGHT - h) / 2;
    tui_draw_window(x, y, w, h, " About AaronOS ");
    print_at(" " KERNEL_STRING, TUI_WIN_COLOR | 0x09, x + 8, y + 3);
    print_at(" Monolithic x86 - " KERNEL_BUILD, TUI_WIN_COLOR, x + 8, y + 5);
    print_at(" Github: ZippyType/AaronOS", TUI_WIN_COLOR | 0x0B, x + 5, y + 7);

    uint8_t col = (tui_selected_item == 0) ? TUI_HL_COLOR : TUI_WIN_COLOR;
    print_at(" [ OK ] ", col, x + (w - 8) / 2, y + h - 2);
}

void tui_render_calendar() {
    tui_max_items = 42;
    int w = 38, h = 14;
    int x = (SCREEN_WIDTH - w) / 2, y = (SCREEN_HEIGHT - h) / 2;
    tui_draw_window(x, y, w, h, " Calendar ");

    read_rtc();
    int m = system_time.mo, d = system_time.d, yr = system_time.y;
    char buf[32];
    static const char* months[] = {"", "January", "February", "March", "April", "May", "June",
                                    "July", "August", "September", "October", "November", "December"};
    print_at(months[m], TUI_WIN_COLOR | 0x0E, x + 2, y + 1);
    itoa(yr, buf, 10);
    print_at(buf, TUI_WIN_COLOR | 0x0E, x + 2 + kstrlen(months[m]) + 1, y + 1);

    static const char* dow = "Su Mo Tu We Th Fr Sa";
    print_at(dow, TUI_WIN_COLOR, x + 3, y + 3);

    int start_day = day_of_week(yr, m, 1);
    int dim = days_in_month(m, yr);
    int cell = 0;
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 7; col++) {
            int day_num = cell - start_day + 1;
            if (day_num >= 1 && day_num <= dim) {
                itoa(day_num, buf, 10);
                int is_today = (day_num == d);
                uint8_t c = is_today ? (TUI_HL_COLOR | 0x0A) : TUI_WIN_COLOR;
                if (cell < 42 && cell == tui_selected_item) c = TUI_HL_COLOR;
                if (day_num < 10) { putchar_at(' ', c, x + 3 + col * 3, y + 4 + row); print_at(buf, c, x + 4 + col * 3, y + 4 + row); }
                else print_at(buf, c, x + 3 + col * 3, y + 4 + row);
            }
            cell++;
        }
    }
}

void tui_render_calculator() {
    tui_max_items = 1;
    int w = 36, h = 12;
    int x = (SCREEN_WIDTH - w) / 2, y = (SCREEN_HEIGHT - h) / 2;
    tui_draw_window(x, y, w, h, " Calculator ");

    print_at(" Type expression, Enter=result", TUI_WIN_COLOR, x + 2, y + 1);
    for (int i = 2; i < w - 2; i++) putchar_at(BOX_HLINE, TUI_WIN_COLOR, x + i, y + 2);

    char display[32];
    int di = 0;
    display[di++] = '>';
    display[di++] = ' ';
    for (int i = 0; i < calc_input_len && i < 24; i++) display[di++] = calc_input[i];
    display[di] = '\0';
    print_at(display, TUI_WIN_COLOR | 0x0A, x + 2, y + 4);

    print_at(" Examples: 5+5, 10*3, 100/4", TUI_WIN_COLOR, x + 2, y + 6);
    print_at(" sin 90, cos 0, sqrt 16, abs -5", TUI_WIN_COLOR, x + 2, y + 7);
    /* result area */
    for (int i = 2; i < w - 2; i++) putchar_at(BOX_HLINE, TUI_WIN_COLOR, x + i, y + 8);

    uint8_t col = (tui_selected_item == 0) ? TUI_HL_COLOR : TUI_WIN_COLOR;
    print_at(" [ RETURN ] ", col, x + (w - 12) / 2, y + h - 2);
}

void tui_handle_input() {
    if (execute_flag == 1) {
        execute_flag = 0;
        input_ptr = 0;

        if (tui_state == 0) {
            if (tui_selected_item == 0) { tui_state = 1; tui_selected_item = 0; refresh_file_list(); }
            else if (tui_selected_item == 1) { tui_state = 2; tui_selected_item = 0; }
            else if (tui_selected_item == 2) { tui_state = 4; tui_selected_item = (day_of_week(system_time.y, system_time.mo, 1) + system_time.d - 1); }
            else if (tui_selected_item == 3) { tui_state = 5; tui_selected_item = 0; calc_input_len = 0; calc_input[0] = '\0'; }
            else if (tui_selected_item == 4) { tui_state = 3; tui_selected_item = 0; }
            else if (tui_selected_item == 5) { in_gui_mode = 0; }
        }
        else if (tui_state == 1) {
            int idx = tui_selected_item + file_scroll;
            if (idx >= 0 && idx < fb_entry_count) {
                if (fb_entries[idx].attrs & 0x10) {
                    fat16_cd(fb_entries[idx].name);
                    refresh_file_list();
                } else {
                    print_at(" Opening file...                         ", 0x4F, 15, 20);
                    char fname[13];
                    kstrcpy(fname, fb_entries[idx].name);
                    int slen = kstrlen(fname);
                    if (slen > 0 && fname[slen-1] == '/') fname[slen-1] = '\0';
                    fat16_cat(fname);
                    sleep(100);
                    refresh_file_list();
                }
            }
        }
        else if (tui_state == 4) {
            /* Calendar: Enter highlights that day, stays in calendar */
        }
        else if (tui_state == 5) {
            /* Calculator handles input via keyboard in main loop */
        }
        else if (tui_state == 2 || tui_state == 3) {
            if (tui_selected_item == 0) { tui_state = 0; tui_selected_item = 0; }
        }
        tui_needs_redraw = 1;
    }

    if (input_ptr > 0 && input_buffer[input_ptr - 1] == 27) {
        input_ptr = 0;
        if (tui_state != 0) {
            if (tui_state == 1) {
                fat16_cd("/");
                refresh_file_list();
            }
            tui_state = 0;
            tui_selected_item = 0;
            tui_needs_redraw = 1;
        } else {
            in_gui_mode = 0;
        }
    }

    /* Calculator keyboard input passthrough */
    if (tui_state == 5 && input_ptr > 0) {
        char c = input_buffer[0];
        input_ptr = 0;
        if (c == '\n') {
            if (calc_input_len > 0) {
                calc_input[calc_input_len] = '\0';
                /* evaluate using run_command logic — pipe through print */
                /* simple eval using the calc command internally */
                tui_draw_desktop();
                putchar_at(' ', 0x08, 0, 20);
                for (int i = 0; i < 70; i++) putchar_at(' ', 0x08, i, 20);
                int si = 0;
                char evalbuf[256];
                evalbuf[si++] = 'c'; evalbuf[si++] = 'a'; evalbuf[si++] = 'l'; evalbuf[si++] = 'c'; evalbuf[si++] = ' ';
                for (int i = 0; i < calc_input_len; i++) evalbuf[si++] = calc_input[i];
                evalbuf[si] = '\0';

                extern void run_command(char* cmd);
                extern int pipe_output_active;
                extern char pipe_buffer[4096];
                extern int pipe_buffer_len;

                pipe_output_active = 1;
                pipe_buffer_len = 0;
                run_command(evalbuf);
                pipe_output_active = 0;
                pipe_buffer[pipe_buffer_len] = '\0';

                print_at(" Result:", 0x0B, 15, 20);
                print_at(pipe_buffer, 0x0F, 24, 20);
                sleep(80);
            }
        } else if (c == '\b') {
            if (calc_input_len > 0) calc_input_len--;
        } else if (c >= ' ' && c <= '~' && calc_input_len < 30) {
            calc_input[calc_input_len++] = c;
        }
        tui_needs_redraw = 1;
    }
}

void launch_tui() {
    in_gui_mode = 1;
    mouse_clear();
    tui_state = 0;
    tui_selected_item = 0;
    tui_needs_redraw = 1;
    execute_flag = 0;
    input_ptr = 0;
    update_cursor_relative();

    static int prev_buttons = 0;
    while (in_gui_mode) {
        if (tui_needs_redraw) {
            tui_draw_desktop();
            if (tui_state == 0) tui_render_main_menu();
            else if (tui_state == 1) tui_render_file_browser();
            else if (tui_state == 2) tui_render_sysmon();
            else if (tui_state == 3) tui_render_about();
            else if (tui_state == 4) tui_render_calendar();
            else if (tui_state == 5) tui_render_calculator();
            tui_needs_redraw = 0;
        }

        /* Mouse click handling in GUI mode */
        if (mouse_buttons && !prev_buttons) {
            if (mouse_buttons & 1) {
                if (tui_state == 0) {
                    /* Map mouse Y to main‑menu item */
                    int item_y = mouse_y - ((SCREEN_HEIGHT - 12) / 2 + 2);
                    if (item_y >= 0 && item_y <= 5)
                        tui_selected_item = item_y;
                }
                input_buffer[0] = '\n';
                input_ptr = 1;
                execute_flag = 1;
            } else if (mouse_buttons & 2) {
                input_buffer[0] = 27;
                input_ptr = 1;
            }
        }
        prev_buttons = mouse_buttons;

        mouse_render();
        tui_handle_input();

        if (timer_ticks % 50 == 0) tui_needs_redraw = 1;

        asm volatile("hlt");
    }

    execute_flag = 0;
    input_ptr = 0;
    refresh_screen();
    print_col("\n", 0x07);
    char cwd_buf[128];
    fat16_get_cwd(cwd_buf, 128);
    print_col("AaronOS", 0x0A);
    if (cwd_buf[0]) { print_col(":", 0x07); print_col(cwd_buf, 0x0B); }
    print_col("> ", 0x07);
    prompt_limit = current_col;
    update_cursor_relative();
}
