#include <stdint.h>
#include <stddef.h>
#include "io.h"

/* TUI Engine State Machine */
int in_gui_mode = 0;       
int tui_state = 0;         
int tui_selected_item = 0; 
int tui_max_items = 0;     
int tui_needs_redraw = 1;  

void tui_draw_desktop() {
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        video_mem[i] = (uint16_t)0xB1 | (TUI_BG_COLOR << 8); 
    }
    
    for(int x = 0; x < SCREEN_WIDTH; x++) putchar_at(' ', TUI_BAR_COLOR, x, 0);
    print_at(" AaronOS Desktop Environment", TUI_BAR_COLOR, 0, 0);
    
    char time_str[32] = "Tck: ";
    char buf[16]; itoa(timer_ticks, buf, 10); 
    for(int i=0; buf[i]; i++) time_str[5+i] = buf[i];
    time_str[5+kstrlen(buf)] = '\0';
    print_at(time_str, TUI_BAR_COLOR, SCREEN_WIDTH - 20, 0);

    for(int x = 0; x < SCREEN_WIDTH; x++) putchar_at(' ', TUI_BAR_COLOR, x, SCREEN_HEIGHT - 1);
    print_at(" \x18\x19 Navigate   \x11 Select   ESC Return", TUI_BAR_COLOR, 0, SCREEN_HEIGHT - 1);
}

void tui_draw_window(int x, int y, int w, int h, const char* title) {
    for(int r = 1; r < h; r++) {
        for(int c = 1; c < w; c++) {
            putchar_at(' ', 0x08, x + c + 1, y + r + 1); 
        }
    }
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
    int t_len = kstrlen(title);
    int t_pos = x + (w - t_len) / 2;
    print_at(title, TUI_WIN_COLOR | 0x0F, t_pos, y);
}

void tui_render_main_menu() {
    tui_max_items = 4; 
    int w = 40, h = 10;
    int x = (SCREEN_WIDTH - w) / 2, y = (SCREEN_HEIGHT - h) / 2;
    
    tui_draw_window(x, y, w, h, " Main Menu ");
    
    char* items[] = {
        " 1. Browse Filesystem    ",
        " 2. System Monitor       ",
        " 3. About AaronOS        ",
        " 4. Exit to Terminal     "
    };

    for(int i = 0; i < 4; i++) {
        uint8_t col = (i == tui_selected_item) ? TUI_HL_COLOR : TUI_WIN_COLOR;
        print_at(items[i], col, x + 5, y + 2 + (i*1));
    }
}

void tui_render_file_browser() {
    tui_max_items = 4;
    int w = 50, h = 12;
    int x = (SCREEN_WIDTH - w) / 2, y = (SCREEN_HEIGHT - h) / 2;
    
    tui_draw_window(x, y, w, h, " File Explorer (A:\\) ");
    print_at(" Name          Size      Type ", TUI_WIN_COLOR, x + 2, y + 2);
    
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

void tui_render_sysmon() {
    tui_max_items = 1; 
    int w = 46, h = 12;
    int x = (SCREEN_WIDTH - w) / 2, y = (SCREEN_HEIGHT - h) / 2;
    
    tui_draw_window(x, y, w, h, " System Monitor ");
    read_rtc(); 

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

void tui_handle_input() {
    if (execute_flag == 1) {
        execute_flag = 0;
        input_ptr = 0; 
        
        if (tui_state == 0) { 
            if (tui_selected_item == 0) { tui_state = 1; tui_selected_item = 0; }
            else if (tui_selected_item == 1) { tui_state = 2; tui_selected_item = 0; }
            else if (tui_selected_item == 2) { tui_state = 3; tui_selected_item = 0; }
            else if (tui_selected_item == 3) { in_gui_mode = 0; } 
        }
        else if (tui_state == 1) { 
            if (tui_selected_item == 3) { tui_state = 0; tui_selected_item = 0; } 
            else if (tui_selected_item == 2) {
                print_at(" Opening README.TXT...       ", 0x4F, 15, 20);
                sleep(70);
            } else {
                print_at(" ERR: System files locked.   ", 0x4F, 15, 20);
                sleep(70);
            }
        }
        else if (tui_state == 2 || tui_state == 3) { 
            if (tui_selected_item == 0) { tui_state = 0; tui_selected_item = 0; } 
        }
        tui_needs_redraw = 1;
    }
    
    if (input_ptr > 0 && input_buffer[input_ptr-1] == 27) {
        input_ptr = 0;
        if (tui_state != 0) { tui_state = 0; tui_selected_item = 0; tui_needs_redraw = 1; }
        else { in_gui_mode = 0; } 
    }
}

void launch_tui() {
    in_gui_mode = 1;         
    tui_state = 0;           
    tui_selected_item = 0;
    tui_needs_redraw = 1;
    execute_flag = 0;
    input_ptr = 0;
    update_cursor_relative(); 
    
    while(in_gui_mode) {
        if (tui_needs_redraw) {
            tui_draw_desktop();
            if (tui_state == 0) tui_render_main_menu();
            else if (tui_state == 1) tui_render_file_browser();
            else if (tui_state == 2) tui_render_sysmon();
            else if (tui_state == 3) tui_render_about();
            tui_needs_redraw = 0;
        }
        
        tui_handle_input();
        
        if (timer_ticks % 100 == 0) tui_needs_redraw = 1; 
        
        asm volatile("hlt"); 
    }

    execute_flag = 0; 
    input_ptr = 0;    
    refresh_screen();    
    print("\nAaronOS> ");
    prompt_limit = current_col;
    update_cursor_relative();
}