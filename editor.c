#include <stdint.h>
#include <stddef.h>
#include "io.h"
#include "fat16.h"

extern void print(const char* str);
extern void clear_screen();
extern void putchar_at(char c, uint8_t color, int x, int y);
extern void print_at(const char* str, uint8_t color, int x, int y);
extern volatile int execute_flag;
extern char input_buffer[256];
extern int input_ptr;

#define VIDEO_ADDR 0xB8000
#define SW 80
#define SH 25
#define BUF_MAX 4096
#define LINE_MAX 128
#define NUM_GUTTER 4
#define UNDO_MAX 32

static char buf[BUF_MAX];
static uint16_t line_starts[LINE_MAX];
static int line_count, cur_row, cur_col, scroll, modified;

static char undo_stack[UNDO_MAX][BUF_MAX];
static int undo_pos, undo_count;

static void calc_lines();
static int line_len(int ln);
static int hl_dirty;

static void save_undo() {
    if (undo_pos < undo_count - 1) undo_count = undo_pos + 1;
    if (undo_count >= UNDO_MAX) {
        for (int i = 0; i < UNDO_MAX - 1; i++) {
            int l = 0; while (l < BUF_MAX && undo_stack[i+1][l]) { undo_stack[i][l] = undo_stack[i+1][l]; l++; }
            undo_stack[i][l] = 0;
        }
        undo_count--;
        if (undo_pos > 0) undo_pos--;
    }
    int bl = 0; while (bl < BUF_MAX && buf[bl]) bl++;
    for (int i = 0; i <= bl; i++) undo_stack[undo_count][i] = buf[i];
    undo_pos = undo_count;
    undo_count++;
}

static void undo_one() {
    if (undo_pos <= 0) return;
    undo_pos--;
    for (int i = 0; i < BUF_MAX; i++) { buf[i] = undo_stack[undo_pos][i]; if (!buf[i]) break; }
    calc_lines();
    if (cur_row >= line_count) cur_row = line_count - 1;
    if (cur_row < 0) cur_row = 0;
    int ll = line_len(cur_row);
    if (cur_col > ll) cur_col = ll;
    if (cur_row < scroll) scroll = cur_row;
    modified = 1; hl_dirty = 1;
}

static void redo_one() {
    if (undo_pos >= undo_count - 1) return;
    undo_pos++;
    for (int i = 0; i < BUF_MAX; i++) { buf[i] = undo_stack[undo_pos][i]; if (!buf[i]) break; }
    calc_lines();
    if (cur_row >= line_count) cur_row = line_count - 1;
    if (cur_row < 0) cur_row = 0;
    int ll = line_len(cur_row);
    if (cur_col > ll) cur_col = ll;
    if (cur_row < scroll) scroll = cur_row;
    modified = 1; hl_dirty = 1;
}

#define COL_DEF 0x07
#define COL_KEYW 0x09
#define COL_STR 0x0C
#define COL_CMT 0x0A
#define COL_NUM 0x0B
#define COL_PREP 0x0D
#define COL_LNUM 0x08
#define COL_CURS 0x0F
#define COL_HL 0x0E
#define COL_BG 0x1E

static uint8_t char_col[BUF_MAX];
static int hl_dirty;

static const char* ckeywords[] = {
    "auto","break","case","char","const","continue","default","do","double",
    "else","enum","extern","float","for","goto","if","int","long","register",
    "return","short","signed","sizeof","static","struct","switch","typedef",
    "union","unsigned","void","volatile","while","include","define","ifdef",
    "ifndef","endif","undef","pragma","error","line",0
};

static int is_keyword(const char* s, int len) {
    for (int i = 0; ckeywords[i]; i++) {
        const char* kw = ckeywords[i];
        int j = 0;
        while (kw[j] && j < len && kw[j] == s[j]) j++;
        if (kw[j] == 0 && j == len) return 1;
    }
    return 0;
}

static int is_ident_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }

static void compute_hl() {
    int bl = 0;
    while (bl < BUF_MAX && buf[bl]) bl++;
    int in_block_cmt = 0;
    for (int i = 0; i < bl; i++) {
        if (in_block_cmt) {
            char_col[i] = COL_CMT;
            if (buf[i] == '*' && i + 1 < bl && buf[i+1] == '/') { char_col[i+1] = COL_CMT; i++; in_block_cmt = 0; }
            continue;
        }
        if (buf[i] == '#' && (i == 0 || buf[i-1] == '\n')) {
            char_col[i] = COL_PREP;
            i++;
            while (i < bl && buf[i] != '\n') { char_col[i] = COL_PREP; i++; }
            if (i < bl) { char_col[i] = COL_DEF; }
            continue;
        }
        if (buf[i] == '/' && i + 1 < bl && buf[i+1] == '/') {
            char_col[i] = COL_CMT; i++;
            while (i < bl && buf[i] != '\n') { char_col[i] = COL_CMT; i++; }
            if (i < bl) char_col[i] = COL_DEF;
            continue;
        }
        if (buf[i] == '/' && i + 1 < bl && buf[i+1] == '*') {
            char_col[i] = COL_CMT; char_col[i+1] = COL_CMT; i += 2; in_block_cmt = 1;
            while (i < bl && !(buf[i-1] == '*' && buf[i] == '/')) { char_col[i] = COL_CMT; i++; }
            if (i < bl && buf[i] == '/') { char_col[i] = COL_CMT; in_block_cmt = 0; }
            continue;
        }
        if (buf[i] == '"') {
            char_col[i] = COL_STR; i++;
            while (i < bl && buf[i] != '"') {
                if (buf[i] == '\\' && i + 1 < bl) { char_col[i] = COL_STR; i++; }
                char_col[i] = COL_STR; i++;
            }
            if (i < bl) char_col[i] = COL_STR;
            continue;
        }
        if (buf[i] == '\'') {
            char_col[i] = COL_STR; i++;
            if (i < bl && buf[i] == '\\') { char_col[i] = COL_STR; i++; }
            if (i < bl) char_col[i] = COL_STR; i++;
            if (i < bl && buf[i] == '\'') { char_col[i] = COL_STR; }
            continue;
        }
        if (is_digit(buf[i]) && (i == 0 || !is_ident_char(buf[i-1]))) {
            char_col[i] = COL_NUM; i++;
            while (i < bl && (is_digit(buf[i]) || buf[i] == 'x' || buf[i] == 'X' || buf[i] == 'a' || buf[i] == 'f' || buf[i] == 'A' || buf[i] == 'F' || buf[i] == '.')) {
                char_col[i] = COL_NUM; i++;
            }
            i--;
            continue;
        }
        if (is_ident_char(buf[i])) {
            int s = i;
            while (i < bl && (is_ident_char(buf[i]) || is_digit(buf[i]))) i++;
            if (is_keyword(&buf[s], i - s)) {
                for (int j = s; j < i; j++) char_col[j] = COL_KEYW;
            } else {
                for (int j = s; j < i; j++) char_col[j] = COL_DEF;
            }
            i--;
            continue;
        }
        char_col[i] = COL_DEF;
    }
    hl_dirty = 0;
}

static unsigned char kbd_norm[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};
static unsigned char kbd_sh[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

static void set_cursor_pos(int x, int y) {
    uint16_t pos = y * SW + x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void set_cursor_shape(int start, int end) {
    outb(0x3D4, 0x0A);
    uint8_t v = inb(0x3D5);
    outb(0x3D5, (v & 0xC0) | (start & 0x1F));
    outb(0x3D4, 0x0B);
    v = inb(0x3D5);
    outb(0x3D5, (v & 0xE0) | (end & 0x1F));
}

static int buf_len() {
    int n = 0;
    while (n < BUF_MAX && buf[n]) n++;
    return n;
}

static void calc_lines() {
    line_count = 1;
    line_starts[0] = 0;
    int p = 0;
    while (buf[p] && line_count < LINE_MAX) {
        if (buf[p] == '\n') { line_starts[line_count] = p + 1; line_count++; }
        p++;
    }
}

static int line_len(int ln) {
    if (ln < 0 || ln >= line_count) return 0;
    int s = line_starts[ln];
    int e = (ln + 1 < line_count) ? line_starts[ln + 1] - 1 : BUF_MAX;
    int n = 0;
    while (s + n < e && buf[s + n] && buf[s + n] != '\n') n++;
    return n;
}

static void ins_char(char c) {
    save_undo();
    int bl = buf_len();
    if (bl >= BUF_MAX - 2) return;
    int off = line_starts[cur_row] + cur_col;
    for (int i = bl; i >= off; i--) buf[i + 1] = buf[i];
    buf[off] = c;
    cur_col++;
    calc_lines();
    modified = 1; hl_dirty = 1;
}

static void del_char() {
    save_undo();
    if (cur_col == 0 && cur_row == 0) return;
    if (cur_col > 0) {
        int off = line_starts[cur_row] + cur_col - 1;
        int bl = buf_len();
        for (int i = off; i < bl; i++) buf[i] = buf[i + 1];
        cur_col--;
    } else {
        int pl = line_len(cur_row - 1);
        int off = line_starts[cur_row];
        int bl = buf_len();
        for (int i = off - 1; i < bl; i++) buf[i] = buf[i + 1];
        cur_row--; cur_col = pl;
    }
    calc_lines();
    modified = 1; hl_dirty = 1;
}

static void do_enter() {
    save_undo();
    int bl = buf_len();
    if (bl >= BUF_MAX - 2) return;
    int off = line_starts[cur_row] + cur_col;
    for (int i = bl; i >= off; i--) buf[i + 1] = buf[i];
    buf[off] = '\n';
    cur_row++; cur_col = 0;
    calc_lines();
    modified = 1; hl_dirty = 1;
}

static void cur_up() {
    if (cur_row > 0) {
        cur_row--;
        int ll = line_len(cur_row);
        if (cur_col > ll) cur_col = ll;
        if (cur_row < scroll) scroll = cur_row;
    }
}

static void cur_down() {
    if (cur_row < line_count - 1) {
        cur_row++;
        int ll = line_len(cur_row);
        if (cur_col > ll) cur_col = ll;
        int last_vis = scroll + SH - 4;
        if (cur_row > last_vis) scroll = cur_row - (SH - 4);
    }
}

static void cur_left() {
    if (cur_col > 0) cur_col--;
    else if (cur_row > 0) { cur_row--; cur_col = line_len(cur_row); }
}

static void cur_right() {
    int ll = line_len(cur_row);
    if (cur_col < ll) cur_col++;
    else if (cur_row < line_count - 1) { cur_row++; cur_col = 0; }
}

static void itoa_s(int v, char* s) {
    int i = 0, neg = 0;
    if (v == 0) { s[i++] = '0'; s[i] = 0; return; }
    if (v < 0) { neg = 1; v = -v; }
    while (v) { s[i++] = '0' + (v % 10); v /= 10; }
    if (neg) s[i++] = '-';
    s[i] = 0;
    for (int j = 0; j < i / 2; j++) { char t = s[j]; s[j] = s[i - 1 - j]; s[i - 1 - j] = t; }
}

static int find_pos; /* store across renders */

static int find_in_buf(const char* needle, int start) {
    if (!needle[0]) return -1;
    int bl = buf_len();
    for (int i = start; i <= bl; i++) {
        int j = 0;
        while (needle[j] && i + j < bl && buf[i + j] == needle[j]) j++;
        if (needle[j] == 0) return i;
    }
    return -1;
}

static int replace_all_in_buf(const char* find, const char* repl) {
    if (!find[0]) return 0;
    save_undo();
    int count = 0;
    char tmp[BUF_MAX];
    int bl = buf_len(), ti = 0;
    int fl = 0, rl = 0;
    while (find[fl]) fl++;
    while (repl[rl]) rl++;
    int i = 0;
    while (i < bl && ti < BUF_MAX - 2) {
        int j = 0;
        while (find[j] && i + j < bl && buf[i + j] == find[j]) j++;
        if (find[j] == 0) {
            for (int k = 0; k < rl && ti < BUF_MAX - 2; k++) tmp[ti++] = repl[k];
            i += fl;
            count++;
        } else {
            tmp[ti++] = buf[i++];
        }
    }
    tmp[ti] = 0;
    for (int k = 0; k <= ti; k++) buf[k] = tmp[k];
    hl_dirty = 1;
    return count;
}

static char find_buf[64];
static char repl_buf[64];
static int find_len, repl_len;
static int find_mode; /* 0=none, 1=find active, 2=replace active */
static int find_fci;  /* cursor within find_buf/repl_buf */

static void render(const char* fn) {
    uint16_t* v = (uint16_t*)VIDEO_ADDR;
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++)
            v[y * SW + x] = ' ' | (COL_BG << 8);

    if (hl_dirty) compute_hl();

    char tmp[SW];
    int ti;
    ti = 0;
    tmp[ti++] = ' ';
    const char* pfx = "AaronOS Editor: ";
    for (int i = 0; pfx[i]; i++) tmp[ti++] = pfx[i];
    for (int i = 0; fn[i]; i++) tmp[ti++] = fn[i];
    if (modified) { tmp[ti++] = ' '; tmp[ti++] = '*'; }
    for (; ti < SW; ti++) tmp[ti] = ' ';
    for (int x = 0; x < SW; x++) v[x] = tmp[x] | (0x1F << 8);

    for (int x = 0; x < SW; x++) v[1 * SW + x] = 0xC4 | (0x1F << 8);

    int vs = scroll;
    for (int ln = 0; ln < SH - 3; ln++) {
        int tl = vs + ln;
        if (tl >= line_count) break;
        int base = (ln + 2) * SW;
        char lnstr[8];
        itoa_s(tl + 1, lnstr);
        int lni = 0;
        for (int x = 0; x < NUM_GUTTER; x++) {
            char c = ' ';
            uint8_t col = COL_LNUM;
            if (lni < 8 && lnstr[lni]) { c = lnstr[lni]; lni++; }
            v[base + x] = c | (col << 8);
        }
        int s = line_starts[tl];
        int c = 0;
        int vis = SW - NUM_GUTTER;
        while (s + c < BUF_MAX && buf[s + c] && buf[s + c] != '\n' && c < vis) {
            uint8_t col = char_col[s + c];
            if (tl == cur_row && c == cur_col) col = COL_CURS;
            v[base + NUM_GUTTER + c] = buf[s + c] | (col << 8);
            c++;
        }
        /* highlight find match on this line */
        if (find_len > 0) {
            int fp = 0;
            while (1) {
                fp = find_in_buf(find_buf, line_starts[tl] + fp);
                if (fp < 0 || fp >= line_starts[tl] + vis) break;
                int match_col = fp - line_starts[tl];
                if (match_col >= 0 && match_col < vis) {
                    for (int k = 0; k < find_len && match_col + k < vis; k++) {
                        uint8_t col = COL_HL;
                        if (tl == cur_row && match_col + k == cur_col) col = COL_CURS;
                        v[base + NUM_GUTTER + match_col + k] = buf[s + match_col + k] | (col << 8);
                    }
                }
                fp += find_len;
            }
        }
        if (tl == cur_row && c == cur_col && c < vis)
            v[base + NUM_GUTTER + c] = ' ' | (COL_CURS << 8);
    }

    ti = 0;
    if (find_mode == 1) {
        const char* h = " Find: ";
        for (int i = 0; h[i]; i++) tmp[ti++] = h[i];
        for (int i = 0; i < find_len; i++) tmp[ti++] = find_buf[i];
        if (find_len == 0) { tmp[ti++] = '_'; }
        for (; ti < SW - 1; ti++) tmp[ti] = ' ';
        tmp[ti] = 0;
    } else if (find_mode == 2) {
        const char* h = " Repl: ";
        for (int i = 0; h[i]; i++) tmp[ti++] = h[i];
        for (int i = 0; i < find_len; i++) tmp[ti++] = find_buf[i];
        const char* h2 = "  With: ";
        for (int i = 0; h2[i]; i++) tmp[ti++] = h2[i];
        for (int i = 0; i < repl_len; i++) tmp[ti++] = repl_buf[i];
        if (find_mode == 2 && find_fci >= find_len + 1) {
            /* cursor on repl side */;
        }
        for (; ti < SW - 1; ti++) tmp[ti] = ' ';
        tmp[ti] = 0;
    } else {
        const char* h = " ^S:Save  ^Q:Quit  ^F:Find  ^R:Replace  F3:Next  Ln ";
        for (int i = 0; h[i]; i++) tmp[ti++] = h[i];
        char ns[16];
        itoa_s(cur_row + 1, ns);
        for (int i = 0; ns[i]; i++) tmp[ti++] = ns[i];
        tmp[ti++] = '/';
        itoa_s(line_count, ns);
        for (int i = 0; ns[i]; i++) tmp[ti++] = ns[i];
        for (; ti < SW; ti++) tmp[ti] = ' ';
        tmp[ti] = 0;
    }
    for (int x = 0; x < SW; x++) v[(SH - 1) * SW + x] = tmp[x] | (0x70 << 8);

    int cx = cur_col + NUM_GUTTER;
    if (cx >= SW) cx = SW - 1;
    int cy = cur_row - scroll + 2;
    if (cy >= SH - 1) cy = SH - 2;
    set_cursor_pos(cx, cy);
}

static void save_file(const char* fn) {
    fat16_write_file((char*)fn, buf);
    modified = 0;
}

void run_editor(char* filename) {
    if (!filename || !filename[0]) {
        print("Usage: write [filename] or edit [filename]\n");
        return;
    }

    uint8_t old_mask = inb(0x21);
    outb(0x21, old_mask | 0x02);

    outb(0x3D4, 0x0A);
    uint8_t old_cur_start = inb(0x3D5);
    outb(0x3D4, 0x0B);
    uint8_t old_cur_end = inb(0x3D5);
    set_cursor_shape(14, 15);

    buf[0] = 0;
    cur_row = 0; cur_col = 0; scroll = 0; modified = 0; hl_dirty = 1;
    find_len = 0; find_mode = 0; find_fci = 0;
    find_buf[0] = 0; repl_buf[0] = 0;
    undo_pos = 0; undo_count = 0;
    fat16_read_file(filename, buf, BUF_MAX);
    calc_lines();
    save_undo(); /* save initial state for undo */

    int ctrl = 0, shift = 0, run = 1;
    while (run) {
        render(filename);

        while (!(inb(0x64) & 1));
        uint8_t sc = inb(0x60);

        if (sc & 0x80) {
            uint8_t k = sc & 0x7F;
            if (k == 0x1D) ctrl = 0;
            if (k == 0x2A || k == 0x36) shift = 0;
            continue;
        }
        if (sc == 0x1D) { ctrl = 1; continue; }
        if (sc == 0x2A || sc == 0x36) { shift = 1; continue; }

        /* find/replace mode input */
        if (find_mode) {
            if (sc == 0x01) { find_mode = 0; continue; } /* Esc */
            if (sc == 0x1C) { /* Enter - execute search */
                if (find_mode == 1) {
                    find_pos = 0;
                    find_pos = find_in_buf(find_buf, 0);
                    if (find_pos >= 0) {
                        /* jump to match */
                        for (int l = 0; l < line_count; l++) {
                            if (line_starts[l] <= find_pos && find_pos < line_starts[l] + line_len(l)) {
                                cur_row = l;
                                cur_col = find_pos - line_starts[l];
                                if (cur_row < scroll) scroll = cur_row;
                                int last_vis = scroll + SH - 4;
                                if (cur_row > last_vis) scroll = cur_row - (SH - 4);
                                break;
                            }
                        }
                    }
                    find_mode = 0;
                } else if (find_mode == 2) {
                    replace_all_in_buf(find_buf, repl_buf);
                    calc_lines();
                    find_mode = 0;
                }
                continue;
            }
            if (sc == 0x0E) { /* Backspace */
                if (find_mode == 1 && find_len > 0) find_buf[--find_len] = 0;
                else if (find_mode == 2) {
                    if (find_fci > find_len) {
                        if (repl_len > 0) repl_buf[--repl_len] = 0;
                        find_fci--;
                    } else if (find_len > 0) {
                        find_buf[--find_len] = 0;
                        find_fci--;
                    }
                }
                continue;
            }
            unsigned char ascii = shift ? kbd_sh[sc] : kbd_norm[sc];
            if (ascii && ascii != '\b' && ascii != '\n' && ascii != '\t' && ascii != 27) {
                if (find_mode == 1 && find_len < 63) { find_buf[find_len++] = ascii; find_buf[find_len] = 0; }
                else if (find_mode == 2) {
                    if (find_fci <= find_len && find_len < 63) {
                        /* still entering find part, after typing "With:" separator */
                        /* in repl mode: first enter find, then after space, enter replace */
                        /* simplified: all input goes to find until separator then repl */
                        find_buf[find_len++] = ascii; find_buf[find_len] = 0;
                        find_fci++;
                    }
                }
            }
            continue;
        }

        if (ctrl && sc == 0x21) { /* Ctrl+F */
            find_mode = 1; find_len = 0; find_buf[0] = 0; find_fci = 0;
            continue;
        }
        if (ctrl && sc == 0x13) { /* Ctrl+R */
            find_mode = 2; find_len = 0; repl_len = 0;
            find_buf[0] = 0; repl_buf[0] = 0; find_fci = 0;
            continue;
        }
        if (sc == 0x3D) { /* F3 */
            if (find_len > 0) {
                int bl = buf_len();
                int start = line_starts[cur_row] + cur_col + 1;
                if (start >= bl) start = 0;
                find_pos = find_in_buf(find_buf, start);
                if (find_pos >= 0) {
                    for (int l = 0; l < line_count; l++) {
                        if (line_starts[l] <= find_pos && find_pos < line_starts[l] + line_len(l)) {
                            cur_row = l; cur_col = find_pos - line_starts[l];
                            if (cur_row < scroll) scroll = cur_row;
                            int last_vis = scroll + SH - 4;
                            if (cur_row > last_vis) scroll = cur_row - (SH - 4);
                            break;
                        }
                    }
                }
            }
            continue;
        }
        if (ctrl && sc == 0x1F) { save_file(filename); continue; }
        if (ctrl && sc == 0x2C) { undo_one(); continue; }  /* Ctrl+Z */
        if (ctrl && sc == 0x15) { redo_one(); continue; }  /* Ctrl+Y */
        if (ctrl && sc == 0x10) { run = 0; continue; }
        if (sc == 0x48) { cur_up(); continue; }
        if (sc == 0x50) { cur_down(); continue; }
        if (sc == 0x4B) { cur_left(); continue; }
        if (sc == 0x4D) { cur_right(); continue; }
        if (sc == 0x1C) { do_enter(); continue; }
        if (sc == 0x0E) { del_char(); continue; }
        if (sc == 0x0F) { ins_char(' '); ins_char(' '); ins_char(' '); ins_char(' '); continue; }
        if (sc == 0x01) { run = 0; continue; }

        unsigned char ascii = shift ? kbd_sh[sc] : kbd_norm[sc];
        if (ascii && ascii != '\b' && ascii != '\n' && ascii != '\t' && ascii != 27) {
            ins_char(ascii);
        }
    }

    set_cursor_shape(old_cur_start, old_cur_end);
    outb(0x21, old_mask);
    execute_flag = 0;
    input_ptr = 0;
    input_buffer[0] = 0;
    clear_screen();
}
