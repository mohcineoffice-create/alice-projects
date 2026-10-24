/* AliceOS 2 - applications: Terminal, Calculator, Notes, Paint, Snake, About. */
#include "os.h"

const char *app_names[APP_COUNT] = { "Terminal", "Calculator", "Notes", "Paint", "Snake", "About" };
const image_t *app_icons[APP_COUNT] = { &ic_terminal, &ic_calc, &ic_notes, &ic_paint, &ic_snake, &ic_about };
const image_t *app_icons_s[APP_COUNT] = { &ic_terminal_s, &ic_calc_s, &ic_notes_s, &ic_paint_s, &ic_snake_s, &ic_about_s };

static int blink(void) { return (ticks / 50) % 2 == 0; }

/* =================================================================== TERMINAL */
#define T_ROWS 300
#define T_COLS 84
typedef struct { char t[T_COLS]; u32 c1, c2; u8 split; } tline_t;
static tline_t tl[T_ROWS];
static int tn = 0;
static char tin[80]; static int tilen = 0;
static char thist[16][80]; static int thn = 0, thpos = 0;

#define C_TXT  RGB(220, 222, 235)
#define C_DIM  RGB(140, 144, 170)
#define C_OK   RGB(80, 250, 123)
#define C_ERR  RGB(255, 110, 110)
#define C_INFO RGB(139, 233, 253)
#define C_YEL  RGB(241, 250, 140)
#define C_PUR  RGB(189, 147, 249)

static void t_line2(const char *s, u32 c1, int split, u32 c2) {
    if (tn == T_ROWS) { for (int i = 1; i < T_ROWS; i++) tl[i - 1] = tl[i]; tn--; }
    int n = 0; while (s[n] && n < T_COLS - 1) { tl[tn].t[n] = s[n]; n++; }
    tl[tn].t[n] = 0; tl[tn].c1 = c1; tl[tn].c2 = c2; tl[tn].split = (u8)split;
    tn++;
}
static void t_print(const char *s, u32 c) {          /* handles \n and wrapping */
    char buf[T_COLS]; int n = 0;
    for (;; s++) {
        if (*s == '\n' || *s == 0 || n == 80) {
            buf[n] = 0; t_line2(buf, c, 0, c); n = 0;
            if (*s == 0) break;
            if (*s != '\n') buf[n++] = *s;
        } else buf[n++] = *s;
    }
}

/* integer expression parser: + - * / % ( ) */
static const char *ep; static int eerr;
static s64 e_expr(void);
static void e_ws(void) { while (*ep == ' ') ep++; }
static s64 e_factor(void) {
    e_ws();
    if (*ep == '-') { ep++; return -e_factor(); }
    if (*ep == '+') { ep++; return e_factor(); }
    if (*ep == '(') { ep++; s64 v = e_expr(); e_ws(); if (*ep == ')') ep++; else eerr = 1; return v; }
    if (*ep < '0' || *ep > '9') { eerr = 1; return 0; }
    s64 v = 0; while (*ep >= '0' && *ep <= '9') v = v * 10 + (*ep++ - '0');
    return v;
}
static s64 e_term(void) {
    s64 v = e_factor();
    for (;;) {
        e_ws(); char op = *ep;
        if (op != '*' && op != '/' && op != '%') return v;
        ep++; s64 r = e_factor();
        if (op == '*') v *= r;
        else if (r == 0) { eerr = 2; return 0; }
        else v = op == '/' ? v / r : v % r;
    }
}
static s64 e_expr(void) {
    s64 v = e_term();
    for (;;) {
        e_ws(); char op = *ep;
        if (op != '+' && op != '-') return v;
        ep++; s64 r = e_term();
        v = op == '+' ? v + r : v - r;
    }
}

static void uptime_str(char *o) {
    u32 s = ticks / HZ; char b[16];
    itoa(s / 3600, b); strcpy(o, b); strcat(o, "h ");
    itoa(s / 60 % 60, b); strcat(o, b); strcat(o, "m ");
    itoa(s % 60, b); strcat(o, b); strcat(o, "s");
}

static void neofetch(void) {
    static const char *art[] = {
        "        /\\        ", "       /  \\       ", "      / /\\ \\      ", "     / /__\\ \\     ",
        "    / ______ \\    ", "   /_/      \\_\\   ", "                  ", "                  " };
    char info[8][64], b[24];
    strcpy(info[0], "alice@aliceos");
    strcpy(info[1], "-------------");
    strcpy(info[2], "OS: " OS_NAME " " OS_VERSION " i386");
    strcpy(info[3], "Kernel: written from scratch");
    strcpy(info[4], "Uptime: "); uptime_str(info[4] + 8);
    strcpy(info[5], "Resolution: "); itoa(scr_w, b); strcat(info[5], b); strcat(info[5], "x"); itoa(scr_h, b); strcat(info[5], b);
    strcpy(info[6], "CPU: "); strcat(info[6], cpu_brand); info[6][60] = 0;
    strcpy(info[7], "Memory: "); itoa(mem_total_kb / 1024, b); strcat(info[7], b); strcat(info[7], " MB");
    for (int i = 0; i < 8; i++) {
        char line[T_COLS]; strcpy(line, art[i]); strcat(line, info[i]);
        t_line2(line, C_PUR, 18, i == 0 ? C_OK : i == 1 ? C_DIM : C_TXT);
    }
}

static int app_by_name(const char *s) {
    if (!strcmp(s, "terminal")) return APP_TERMINAL;
    if (!strcmp(s, "calculator") || !strcmp(s, "calc")) return APP_CALC;
    if (!strcmp(s, "notes")) return APP_NOTES;
    if (!strcmp(s, "paint")) return APP_PAINT;
    if (!strcmp(s, "snake")) return APP_SNAKE;
    if (!strcmp(s, "about")) return APP_ABOUT;
    return -1;
}

static void t_run(char *s) {
    while (*s == ' ') s++;
    if (!*s) return;
    char b[96];
    if (!strcmp(s, "help")) {
        t_print("Available commands:", C_YEL);
        t_print("  help          this list            clear        clear the screen", C_TXT);
        t_print("  echo <text>   print text           date         date and time", C_TXT);
        t_print("  uptime        time since boot      whoami       current user", C_TXT);
        t_print("  neofetch      system summary       uname        kernel name", C_TXT);
        t_print("  calc <expr>   e.g. calc (12+3)*4   apps         list applications", C_TXT);
        t_print("  open <app>    open an application  history      previous commands", C_TXT);
        t_print("  reboot        restart              shutdown     power off", C_TXT);
    } else if (!strcmp(s, "clear")) tn = 0;
    else if (!strncmp(s, "echo", 4) && (s[4] == ' ' || !s[4])) t_print(s[4] ? s + 5 : "", C_TXT);
    else if (!strcmp(s, "date")) {
        datetime_t t; rtc_read(&t); char x[4];
        static const char *wd[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
        strcpy(b, wd[t.wday]); strcat(b, " ");
        fmt2(t.day, x); strcat(b, x); strcat(b, "/"); fmt2(t.mon, x); strcat(b, x); strcat(b, "/");
        itoa(t.year, x + 0); strcat(b, x); strcat(b, "  ");
        fmt2(t.hour, x); strcat(b, x); strcat(b, ":"); fmt2(t.min, x); strcat(b, x); strcat(b, ":");
        fmt2(t.sec, x); strcat(b, x); strcat(b, " (RTC)");
        t_print(b, C_TXT);
    } else if (!strcmp(s, "uptime")) { strcpy(b, "up "); uptime_str(b + 3); t_print(b, C_TXT); }
    else if (!strcmp(s, "whoami")) t_print("alice", C_TXT);
    else if (!strcmp(s, "uname") || !strcmp(s, "uname -a")) t_print(OS_NAME " " OS_VERSION " i386 (from-scratch kernel)", C_TXT);
    else if (!strcmp(s, "neofetch")) neofetch();
    else if (!strncmp(s, "calc", 4)) {
        ep = s + 4; eerr = 0; s64 v = e_expr(); e_ws();
        if (eerr == 2) t_print("error: division by zero", C_ERR);
        else if (eerr || *ep) t_print("usage: calc <expression>   e.g. calc (12+3)*4 - 7", C_ERR);
        else { strcpy(b, "= "); itoa(v, b + 2); t_print(b, C_OK); }
    } else if (!strcmp(s, "apps")) {
        for (int i = 0; i < APP_COUNT; i++) { strcpy(b, "  "); strcat(b, app_names[i]); t_print(b, C_INFO); }
    } else if (!strncmp(s, "open ", 5)) {
        char n[32]; int i = 0; const char *p = s + 5;
        while (*p == ' ') p++;
        while (*p && i < 31) { char c = *p++; n[i++] = (c >= 'A' && c <= 'Z') ? c + 32 : c; }
        n[i] = 0;
        int a = app_by_name(n);
        if (a < 0) t_print("open: unknown application (type 'apps')", C_ERR);
        else { gui_open(a); strcpy(b, "opened "); strcat(b, app_names[a]); t_print(b, C_OK); }
    } else if (!strcmp(s, "history")) {
        for (int i = 0; i < thn; i++) { strcpy(b, "  "); itoa(i + 1, b + 2); strcat(b, "  "); strcat(b, thist[i]); t_print(b, C_DIM); }
    } else if (!strcmp(s, "reboot")) sys_reboot();
    else if (!strcmp(s, "shutdown")) sys_shutdown();
    else { strcpy(b, s); strcat(b, ": command not found"); t_print(b, C_ERR); }
}

static void term_draw(window_t *w, int cx, int cy, int cw, int ch) {
    (void)w; (void)cw;
    int lh = font_mono.h + 1, x = cx + 12;
    int rows = (ch - 16) / lh - 1;
    int first = tn - rows; if (first < 0) first = 0;
    int y = cy + 8;
    int adv = font_mono.g[0].adv;
    for (int i = first; i < tn; i++, y += lh) {
        if (tl[i].split) {
            char a[T_COLS]; int k; for (k = 0; k < tl[i].split && tl[i].t[k]; k++) a[k] = tl[i].t[k]; a[k] = 0;
            gfx_text(x, y, a, tl[i].c1, &font_mono);
            if (k == tl[i].split) gfx_text(x + k * adv, y, tl[i].t + k, tl[i].c2, &font_mono);
        } else gfx_text(x, y, tl[i].t, tl[i].c1, &font_mono);
    }
    int px = gfx_text(x, y, "alice@aliceos", C_OK, &font_mono);
    px = gfx_text(px, y, ":", C_TXT, &font_mono);
    px = gfx_text(px, y, "~", C_INFO, &font_mono);
    px = gfx_text(px, y, "$ ", C_TXT, &font_mono);
    tin[tilen] = 0;
    px = gfx_text(px, y, tin, RGB(255, 255, 255), &font_mono);
    if (gui_focused(w) && blink()) gfx_fill(px + 1, y + 2, adv - 1, lh - 3, RGB(220, 222, 235));
}

static void term_key(window_t *w, int k) {
    (void)w;
    if (k == '\n') {
        tin[tilen] = 0;
        char line[T_COLS]; strcpy(line, "alice@aliceos:~$ "); strcat(line, tin);
        t_line2(line, C_OK, 13, C_TXT);
        if (tilen) {
            if (thn == 16) { for (int i = 1; i < 16; i++) strcpy(thist[i - 1], thist[i]); thn--; }
            strcpy(thist[thn++], tin);
        }
        thpos = thn;
        char cmd[80]; strcpy(cmd, tin); tilen = 0; tin[0] = 0;
        t_run(cmd);
    } else if (k == '\b') { if (tilen) tilen--; }
    else if (k == K_UP) { if (thpos > 0) { thpos--; strcpy(tin, thist[thpos]); tilen = strlen(tin); } }
    else if (k == K_DOWN) {
        if (thpos < thn - 1) { thpos++; strcpy(tin, thist[thpos]); tilen = strlen(tin); }
        else { thpos = thn; tilen = 0; }
    } else if (k >= 32 && k < 127 && tilen < 70) tin[tilen++] = (char)k;
    gui_dirty();
}

static void term_tick(window_t *w) { if (gui_focused(w) && ticks % 50 == 0) gui_dirty(); }

/* ================================================================= CALCULATOR */
#define SCALE 1000000LL
static s64 c_acc, c_cur; static char c_op; static int c_entering, c_err, c_flash = -1; static u32 c_flash_t;
static char c_entry[24], c_hist[64];

static void c_fmt(s64 v, char *o) {
    if (v < 0) { *o++ = '-'; v = -v; }
    s64 ip = v / SCALE, fp = v % SCALE;
    itoa(ip, o); o += strlen(o);
    if (fp) {
        char f[8]; s64 d = SCALE / 10; int n = 0;
        while (d && fp) { f[n++] = '0' + (char)(fp / d); fp %= d; d /= 10; }
        *o++ = '.'; for (int i = 0; i < n; i++) *o++ = f[i];
    }
    *o = 0;
}
static s64 c_parse(const char *s) {
    s64 ip = 0, fp = 0, d = SCALE / 10; int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') ip = ip * 10 + (*s++ - '0');
    if (*s == '.') { s++; while (*s >= '0' && *s <= '9' && d) { fp += (*s++ - '0') * d; d /= 10; } }
    s64 v = ip * SCALE + fp; return neg ? -v : v;
}
static s64 c_value(void) { return c_entering ? c_parse(c_entry) : c_cur; }
#define C_LIMIT (1000000000000LL * SCALE)
static s64 c_apply(s64 a, s64 b, char op) {
    s64 r = 0;
    if (op == '+') r = a + b;
    else if (op == '-') r = a - b;
    else if (op == '*') {
        s64 aa = a < 0 ? -a : a, bb = b < 0 ? -b : b;
        if (aa / SCALE > 0 && bb > C_LIMIT / (aa / SCALE + 1)) { c_err = 1; return 0; }
        r = (aa / SCALE) * bb + (aa % SCALE) * (bb / SCALE) + (aa % SCALE) * (bb % SCALE) / SCALE;
        if ((a < 0) != (b < 0)) r = -r;
    } else if (op == '/') {
        if (b == 0) { c_err = 2; return 0; }
        s64 aa = a < 0 ? -a : a, bb = b < 0 ? -b : b;
        s64 q = aa / bb, rem = aa % bb; r = q * SCALE;
        for (s64 p = SCALE / 10; p; p /= 10) { rem *= 10; r += (rem / bb) * p; rem %= bb; }
        if ((a < 0) != (b < 0)) r = -r;
    }
    if (r > C_LIMIT || r < -C_LIMIT) c_err = 1;
    return r;
}
static const char *op_sym(char op) { return op == '*' ? " x " : op == '/' ? " / " : op == '+' ? " + " : " - "; }

static void c_input(int k) {
    if (c_err && k != 'C' && k != 27) return;
    if ((k >= '0' && k <= '9') || k == '.') {
        if (!c_entering) { c_entering = 1; c_entry[0] = 0; }
        int n = strlen(c_entry);
        if (k == '.') { for (int i = 0; i < n; i++) if (c_entry[i] == '.') return; if (!n) { strcpy(c_entry, "0"); n = 1; } }
        if (n >= 15) return;
        if (n == 1 && c_entry[0] == '0' && k != '.') n = 0;
        c_entry[n] = (char)k; c_entry[n + 1] = 0;
    } else if (k == '+' || k == '-' || k == '*' || k == '/') {
        s64 v = c_value();
        if (c_op && c_entering) v = c_apply(c_acc, v, c_op);
        else if (c_op && !c_entering) v = c_acc;
        c_acc = v; c_cur = v; c_op = (char)k; c_entering = 0;
        c_fmt(c_acc, c_hist); strcat(c_hist, op_sym(c_op));
    } else if (k == '=' || k == '\n') {
        if (!c_op) return;
        s64 b = c_value(), r = c_apply(c_acc, b, c_op);
        char t[24]; c_fmt(c_acc, c_hist); strcat(c_hist, op_sym(c_op)); c_fmt(b, t); strcat(c_hist, t); strcat(c_hist, " =");
        c_cur = r; c_acc = r; c_op = 0; c_entering = 0;
    } else if (k == 'C' || k == 27) { c_acc = c_cur = 0; c_op = 0; c_entering = 0; c_err = 0; c_hist[0] = 0; }
    else if (k == 'N') {                               /* +/- */
        if (c_entering) {
            if (c_entry[0] == '-') strcpy(c_entry, c_entry + 1);
            else { char t[24] = "-"; strcat(t, c_entry); strcpy(c_entry, t); }
        } else c_cur = -c_cur;
    } else if (k == '%') { s64 v = c_value(); c_cur = v / 100; c_entering = 0; }
    else if (k == '\b') { if (c_entering) { int n = strlen(c_entry); if (n) c_entry[n - 1] = 0; } }
}

static const char *c_lbl[20] = { "C", "+/-", "%", "/", "7", "8", "9", "x", "4", "5", "6", "-",
                                 "1", "2", "3", "+", "0", "", ".", "=" };
static const int c_key[20] = { 'C', 'N', '%', '/', '7', '8', '9', '*', '4', '5', '6', '-',
                               '1', '2', '3', '+', '0', 0, '.', '=' };
#define C_DISP 128
static void c_btn_rect(int i, int cw, int ch, int *x, int *y, int *w, int *h) {
    int pad = 12, gap = 8;
    int bw = (cw - 2 * pad - 3 * gap) / 4, bh = (ch - C_DISP - pad - 4 * gap) / 5;
    int r = i / 4, c = i % 4;
    *x = pad + c * (bw + gap); *y = C_DISP + r * (bh + gap); *w = bw; *h = bh;
    if (i == 16) *w = 2 * bw + gap;
}

static void calc_draw(window_t *w, int cx, int cy, int cw, int ch) {
    (void)w;
    char disp[32];
    if (c_err) strcpy(disp, c_err == 2 ? "Cannot divide by 0" : "Overflow");
    else if (c_entering) strcpy(disp, c_entry[0] ? c_entry : "0");
    else c_fmt(c_cur, disp);
    gfx_text(cx + cw - 16 - gfx_text_w(c_hist, &font_ui), cy + 22, c_hist, RGB(150, 150, 175), &font_ui);
    const font_t *f = gfx_text_w(disp, &font_big) > cw - 32 ? &font_title : &font_big;
    gfx_text(cx + cw - 16 - gfx_text_w(disp, f), cy + 50 + (f == &font_title ? 10 : 0), disp, RGB(255, 255, 255), f);
    for (int i = 0; i < 20; i++) {
        if (i == 17) continue;
        int x, y, bw, bh; c_btn_rect(i, cw, ch, &x, &y, &bw, &bh);
        int op = (i % 4 == 3), fn = i < 3;
        u32 col = op ? RGB(124, 92, 255) : fn ? RGB(70, 68, 92) : RGB(46, 45, 62);
        if (i == 19) col = RGB(255, 140, 66);
        if (c_flash == i && ticks - c_flash_t < 12) col = op || i == 19 ? RGB(170, 150, 255) : RGB(96, 94, 124);
        else if (op && c_op && c_key[i] == c_op && !c_entering) col = RGB(160, 140, 255);
        gfx_fill_round(cx + x, cy + y, bw, bh, 10, col);
        gfx_text_center(cx + x, cy + y + (bh - font_ui_b.h) / 2, bw, c_lbl[i], RGB(255, 255, 255), &font_ui_b);
    }
}

static void calc_press(int i) { c_flash = i; c_flash_t = ticks; c_input(c_key[i]); gui_dirty(); }

static void calc_mouse(window_t *w, int x, int y, int ev) {
    if (ev != EV_DOWN) return;
    int cw = w->w - 2, ch = w->h - TITLE_H - 1;
    for (int i = 0; i < 20; i++) {
        if (i == 17) continue;
        int bx, by, bw, bh; c_btn_rect(i, cw, ch, &bx, &by, &bw, &bh);
        if (x >= bx && y >= by && x < bx + bw && y < by + bh) { calc_press(i); return; }
    }
}

static void calc_key(window_t *w, int k) {
    (void)w;
    if (k == 'c' || k == 'C') k = 'C';
    if (k == 'x' || k == 'X') k = '*';
    for (int i = 0; i < 20; i++) if (c_key[i] == k || (k == '\n' && i == 19)) { calc_press(i); return; }
    c_input(k); gui_dirty();
}
static void calc_tick(window_t *w) { (void)w; if (c_flash >= 0 && ticks - c_flash_t == 12) gui_dirty(); }

/* ====================================================================== NOTES */
#define N_MAX 8192
static char ntext[N_MAX]; static int nlen = 0, ncur = 0, ntop = 0, ninit = 0;

static void n_linecol(int pos, int *line, int *col) {
    int l = 0, c = 0;
    for (int i = 0; i < pos; i++) { if (ntext[i] == '\n') { l++; c = 0; } else c++; }
    *line = l; *col = c;
}
static int n_pos(int line, int col) {
    int i = 0, l = 0;
    while (i < nlen && l < line) { if (ntext[i] == '\n') l++; i++; }
    if (l < line) return nlen;
    int c = 0; while (i < nlen && ntext[i] != '\n' && c < col) { i++; c++; }
    return i;
}
static int n_lines(void) { int l = 1; for (int i = 0; i < nlen; i++) if (ntext[i] == '\n') l++; return l; }

#define N_PAD 16
#define N_STATUS 28
static int n_lh(void) { return font_mono.h + 4; }

static void notes_draw(window_t *w, int cx, int cy, int cw, int ch) {
    int lh = n_lh(), adv = font_mono.g[0].adv;
    int rows = (ch - N_STATUS - N_PAD) / lh;
    int cl, cc; n_linecol(ncur, &cl, &cc);
    if (cl < ntop) ntop = cl;
    if (cl >= ntop + rows) ntop = cl - rows + 1;
    /* line numbers gutter */
    gfx_fill(cx, cy, 44, ch - N_STATUS, RGB(246, 246, 250));
    gfx_vline(cx + 44, cy, ch - N_STATUS, RGB(228, 228, 236));
    int line = 0, i = 0;
    while (line < ntop && i < nlen) { if (ntext[i] == '\n') line++; i++; }
    int y = cy + N_PAD / 2 + 4;
    for (int r = 0; r < rows && line <= n_lines() - 1; r++, line++) {
        char num[8]; itoa(line + 1, num);
        gfx_text(cx + 36 - gfx_text_w(num, &font_small), y + 2, num, line == cl ? RGB(90, 90, 120) : RGB(175, 175, 190), &font_small);
        if (line == cl) gfx_fill_alpha(cx + 45, y - 2, cw - 45, lh, RGB(124, 92, 255), 14);
        int x = cx + 44 + N_PAD;
        while (i < nlen && ntext[i] != '\n') { gfx_char(x, y, ntext[i], RGB(34, 36, 48), &font_mono); x += adv; i++; }
        i++;
        y += lh;
    }
    if (gui_focused(w) && blink())
        gfx_fill(cx + 44 + N_PAD + cc * adv, cy + N_PAD / 2 + 4 + (cl - ntop) * lh - 1, 2, lh - 2, RGB(124, 92, 255));
    int sy = cy + ch - N_STATUS;
    gfx_fill(cx, sy, cw, N_STATUS, RGB(242, 242, 247));
    gfx_hline(cx, sy, cw, RGB(226, 226, 234));
    char st[64], b[12]; strcpy(st, "Ln "); itoa(cl + 1, b); strcat(st, b); strcat(st, ", Col "); itoa(cc + 1, b); strcat(st, b);
    gfx_text(cx + 14, sy + (N_STATUS - font_small.h) / 2, st, RGB(100, 100, 120), &font_small);
    strcpy(st, ""); itoa(nlen, b); strcat(st, b); strcat(st, " characters   |   Plain text");
    gfx_text(cx + cw - 14 - gfx_text_w(st, &font_small), sy + (N_STATUS - font_small.h) / 2, st, RGB(100, 100, 120), &font_small);
}

static void n_insert(char c) {
    if (nlen >= N_MAX - 1) return;
    for (int i = nlen; i > ncur; i--) ntext[i] = ntext[i - 1];
    ntext[ncur++] = c; nlen++;
}
static void notes_key(window_t *w, int k) {
    (void)w;
    int l, c; n_linecol(ncur, &l, &c);
    if (k == '\b') { if (ncur > 0) { for (int i = ncur - 1; i < nlen - 1; i++) ntext[i] = ntext[i + 1]; ncur--; nlen--; } }
    else if (k == K_DEL) { if (ncur < nlen) { for (int i = ncur; i < nlen - 1; i++) ntext[i] = ntext[i + 1]; nlen--; } }
    else if (k == K_LEFT) { if (ncur > 0) ncur--; }
    else if (k == K_RIGHT) { if (ncur < nlen) ncur++; }
    else if (k == K_UP) { if (l > 0) ncur = n_pos(l - 1, c); }
    else if (k == K_DOWN) { ncur = n_pos(l + 1, c); }
    else if (k == K_HOME) ncur = n_pos(l, 0);
    else if (k == K_END) ncur = n_pos(l, 10000);
    else if (k == '\t') { for (int i = 0; i < 4; i++) n_insert(' '); }
    else if (k == '\n' || (k >= 32 && k < 127)) n_insert((char)k);
    ntext[nlen] = 0;
    gui_dirty();
}
static void notes_mouse(window_t *w, int x, int y, int ev) {
    (void)w;
    if (ev == EV_UP) return;
    int lh = n_lh(), adv = font_mono.g[0].adv;
    int line = ntop + (y - N_PAD / 2 - 4) / lh; if (line < 0) line = 0;
    int col = (x - 44 - N_PAD + adv / 2) / adv; if (col < 0) col = 0;
    ncur = n_pos(line, col); gui_dirty();
}
static void notes_tick(window_t *w) { if (gui_focused(w) && ticks % 50 == 0) gui_dirty(); }

/* ====================================================================== PAINT */
#define P_W 718
#define P_H 452
#define P_BAR 52
static u32 canvas[P_W * P_H];
static u32 p_color = RGB(30, 30, 40); static int p_size = 4, p_last_x = -1, p_last_y = -1, p_init = 0, p_eraser = 0;
static const u32 p_pal[12] = { RGB(30, 30, 40), RGB(120, 124, 140), RGB(255, 255, 255), RGB(239, 68, 68),
    RGB(249, 115, 22), RGB(250, 204, 21), RGB(34, 197, 94), RGB(20, 184, 166), RGB(59, 130, 246),
    RGB(124, 92, 255), RGB(236, 72, 153), RGB(146, 64, 14) };
static const int p_sizes[3] = { 2, 5, 10 };

static void p_clear(void) { for (int i = 0; i < P_W * P_H; i++) canvas[i] = 0xFFFFFF; }
static void p_stamp(int x, int y) {
    int r = p_size; u32 c = p_eraser ? 0xFFFFFF : p_color;
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++)
            if (i * i + j * j <= r * r + r) {
                int xx = x + i, yy = y + j;
                if (xx >= 0 && yy >= 0 && xx < P_W && yy < P_H) canvas[yy * P_W + xx] = c;
            }
}
static void p_line(int x0, int y0, int x1, int y1) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        p_stamp(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void paint_draw(window_t *w, int cx, int cy, int cw, int ch) {
    (void)w; (void)ch;
    gfx_fill(cx, cy, cw, P_BAR, RGB(244, 244, 248));
    gfx_hline(cx, cy + P_BAR - 1, cw, RGB(224, 224, 232));
    for (int i = 0; i < 12; i++) {
        int x = cx + 16 + i * 32, y = cy + P_BAR / 2;
        if (!p_eraser && p_pal[i] == p_color) gfx_circle_fill(x + 12, y, 15, RGB(124, 92, 255));
        gfx_circle_fill(x + 12, y, 12, p_pal[i] == 0xFFFFFF ? RGB(210, 210, 220) : p_pal[i]);
        if (p_pal[i] == 0xFFFFFF) gfx_circle_fill(x + 12, y, 11, 0xFFFFFF);
    }
    int sx = cx + 16 + 12 * 32 + 16;
    for (int i = 0; i < 3; i++) {
        int x = sx + i * 36;
        if (p_sizes[i] == p_size) gfx_fill_round(x, cy + 10, 32, 32, 8, RGB(226, 220, 255));
        gfx_circle_fill(x + 16, cy + 26, p_sizes[i] < 3 ? 3 : p_sizes[i] > 8 ? 9 : p_sizes[i], RGB(40, 40, 52));
    }
    int ex = sx + 3 * 36 + 10;
    gfx_fill_round(ex, cy + 10, 76, 32, 8, p_eraser ? RGB(226, 220, 255) : RGB(232, 232, 240));
    gfx_text_center(ex, cy + 10 + (32 - font_ui.h) / 2, 76, "Eraser", RGB(40, 40, 52), &font_ui);
    int clx = ex + 84;
    gfx_fill_round(clx, cy + 10, 64, 32, 8, RGB(232, 232, 240));
    gfx_text_center(clx, cy + 10 + (32 - font_ui.h) / 2, 64, "Clear", RGB(40, 40, 52), &font_ui);
    gfx_blit(cx, cy + P_BAR, P_W, P_H, canvas);
}

static void paint_mouse(window_t *w, int x, int y, int ev) {
    (void)w;
    if (ev == EV_DOWN && y < P_BAR) {
        for (int i = 0; i < 12; i++) {
            int bx = 16 + i * 32 + 12, by = P_BAR / 2;
            if ((x - bx) * (x - bx) + (y - by) * (y - by) <= 14 * 14) { p_color = p_pal[i]; p_eraser = 0; gui_dirty(); return; }
        }
        int sx = 16 + 12 * 32 + 16;
        for (int i = 0; i < 3; i++) if (x >= sx + i * 36 && x < sx + i * 36 + 32) { p_size = p_sizes[i]; gui_dirty(); return; }
        int ex = sx + 3 * 36 + 10;
        if (x >= ex && x < ex + 76) { p_eraser = !p_eraser; gui_dirty(); return; }
        if (x >= ex + 84 && x < ex + 84 + 64) { p_clear(); gui_dirty(); return; }
        return;
    }
    int px = x, py = y - P_BAR;
    if (ev == EV_DOWN) { p_last_x = px; p_last_y = py; p_stamp(px, py); gui_dirty(); }
    else if (ev == EV_MOVE && p_last_x >= 0) { p_line(p_last_x, p_last_y, px, py); p_last_x = px; p_last_y = py; gui_dirty(); }
    else if (ev == EV_UP) p_last_x = -1;
}

/* ====================================================================== SNAKE */
#define S_CW 20
#define S_GW 22
#define S_GH 18
#define S_HDR 44
typedef struct { int x, y; } pt_t;
static pt_t sn[S_GW * S_GH]; static int s_len, s_dir, s_ndir, s_state, s_score, s_best; static pt_t s_food;
static const int s_dx[4] = { 1, 0, -1, 0 }, s_dy[4] = { 0, 1, 0, -1 };

static void s_place_food(void) {
    for (;;) {
        pt_t f = { (int)(rand32() % S_GW), (int)(rand32() % S_GH) };
        int ok = 1; for (int i = 0; i < s_len; i++) if (sn[i].x == f.x && sn[i].y == f.y) ok = 0;
        if (ok) { s_food = f; return; }
    }
}
static void s_reset(void) {
    s_len = 4; s_dir = s_ndir = 0; s_score = 0;
    for (int i = 0; i < s_len; i++) { sn[i].x = 8 - i; sn[i].y = S_GH / 2; }
    s_place_food();
}
static void s_step(void) {
    s_dir = s_ndir;
    pt_t h = { sn[0].x + s_dx[s_dir], sn[0].y + s_dy[s_dir] };
    if (h.x < 0 || h.y < 0 || h.x >= S_GW || h.y >= S_GH) { s_state = 2; return; }
    for (int i = 0; i < s_len - 1; i++) if (sn[i].x == h.x && sn[i].y == h.y) { s_state = 2; return; }
    int grow = (h.x == s_food.x && h.y == s_food.y);
    if (grow && s_len < S_GW * S_GH) s_len++;
    for (int i = s_len - 1; i > 0; i--) sn[i] = sn[i - 1];
    sn[0] = h;
    if (grow) { s_score += 10; if (s_score > s_best) s_best = s_score; s_place_food(); }
}

static void snake_draw(window_t *w, int cx, int cy, int cw, int ch) {
    (void)w; (void)ch;
    char b[48], n[12];
    strcpy(b, "Score  "); itoa(s_score, n); strcat(b, n);
    gfx_text(cx + 16, cy + (S_HDR - font_ui_b.h) / 2, b, RGB(220, 255, 220), &font_ui_b);
    strcpy(b, "Best  "); itoa(s_best, n); strcat(b, n);
    gfx_text(cx + cw - 16 - gfx_text_w(b, &font_ui), cy + (S_HDR - font_ui.h) / 2, b, RGB(160, 210, 170), &font_ui);
    int bx = cx, by = cy + S_HDR;
    for (int j = 0; j < S_GH; j++)
        for (int i = 0; i < S_GW; i++)
            gfx_fill(bx + i * S_CW, by + j * S_CW, S_CW, S_CW, ((i + j) & 1) ? RGB(26, 58, 38) : RGB(30, 66, 43));
    gfx_circle_fill(bx + s_food.x * S_CW + 10, by + s_food.y * S_CW + 10, 7, RGB(255, 80, 80));
    gfx_fill(bx + s_food.x * S_CW + 10, by + s_food.y * S_CW + 1, 2, 4, RGB(120, 70, 30));
    for (int i = s_len - 1; i >= 0; i--) {
        u32 c = i == 0 ? RGB(190, 255, 140) : (i % 2 ? RGB(120, 220, 110) : RGB(105, 205, 98));
        gfx_fill_round(bx + sn[i].x * S_CW + 1, by + sn[i].y * S_CW + 1, S_CW - 2, S_CW - 2, 6, c);
    }
    int hx = bx + sn[0].x * S_CW + 10, hy = by + sn[0].y * S_CW + 10;
    int ex = s_dy[s_dir] ? 4 : 0, ey = s_dx[s_dir] ? 4 : 0;
    gfx_fill(hx - ex + s_dx[s_dir] * 3 - 1, hy - ey + s_dy[s_dir] * 3 - 1, 3, 3, RGB(20, 30, 20));
    gfx_fill(hx + ex + s_dx[s_dir] * 3 - 1, hy + ey + s_dy[s_dir] * 3 - 1, 3, 3, RGB(20, 30, 20));
    if (s_state != 1) {
        const char *t = s_state == 0 ? "SNAKE" : s_state == 2 ? "GAME OVER" : "PAUSED";
        const char *m = s_state == 3 ? "Press P to continue" : "Press SPACE to play  -  arrows to move";
        int pw = 330, ph = 110, px = cx + (cw - pw) / 2, py = by + (S_GH * S_CW - ph) / 2;
        gfx_fill_round_alpha(px, py, pw, ph, 14, RGB(10, 20, 14), 210);
        gfx_text_center(px, py + 22, pw, t, RGB(255, 255, 255), &font_title);
        gfx_text_center(px, py + 66, pw, m, RGB(190, 230, 200), &font_ui);
    }
}
static void snake_key(window_t *w, int k) {
    (void)w;
    int nd = -1;
    if (k == K_RIGHT || k == 'd') nd = 0; else if (k == K_DOWN || k == 's') nd = 1;
    else if (k == K_LEFT || k == 'a') nd = 2; else if (k == K_UP || k == 'w') nd = 3;
    if (nd >= 0 && s_state == 1 && (nd + 2) % 4 != s_dir) s_ndir = nd;
    if (k == ' ' && (s_state == 0 || s_state == 2)) { s_reset(); s_state = 1; }
    if ((k == 'p' || k == 'P') && (s_state == 1 || s_state == 3)) s_state = s_state == 1 ? 3 : 1;
    gui_dirty();
}
static void snake_tick(window_t *w) {
    if (s_state == 1 && !gui_focused(w)) { s_state = 3; gui_dirty(); return; }
    if (s_state == 1 && ticks % 9 == 0) { s_step(); gui_dirty(); }
}

/* ====================================================================== ABOUT */
static void about_row(int x, int y, const char *k, const char *v) {
    gfx_text(x, y, k, RGB(130, 130, 150), &font_ui);
    gfx_text(x + 110, y, v, RGB(30, 32, 44), &font_ui);
}
static void about_draw(window_t *w, int cx, int cy, int cw, int ch) {
    (void)w; (void)ch;
    gfx_vgradient(cx, cy, cw, 120, RGB(52, 30, 110), RGB(120, 70, 190));
    gfx_image(cx + 28, cy + 28, &img_logo);
    gfx_text(cx + 112, cy + 32, OS_NAME, RGB(255, 255, 255), &font_title);
    gfx_text(cx + 112, cy + 66, "Version " OS_VERSION " - built from scratch", RGB(225, 215, 255), &font_ui);
    char b[64], n[16];
    int x = cx + 32, y = cy + 146;
    about_row(x, y, "Kernel", "32-bit, x86, own drivers"); y += 28;
    about_row(x, y, "Processor", cpu_brand); y += 28;
    itoa(mem_total_kb / 1024, n); strcpy(b, n); strcat(b, " MB");
    about_row(x, y, "Memory", b); y += 28;
    itoa(scr_w, n); strcpy(b, n); strcat(b, " x "); itoa(scr_h, n); strcat(b, n); strcat(b, ", 32-bit colour");
    about_row(x, y, "Display", b); y += 28;
    uptime_str(b); about_row(x, y, "Uptime", b); y += 28;
    about_row(x, y, "Created by", "Alice"); y += 40;
    gfx_text(x, y, "C + x86 assembly. No Linux, no libraries.", RGB(124, 92, 255), &font_ui_b);
}
static void about_tick(window_t *w) { if (gui_focused(w) && ticks % HZ == 0) gui_dirty(); }

/* ====================================================================== setup */
void app_setup(window_t *w, int app) {
    strcpy(w->title, app_names[app]);
    w->icon = app_icons_s[app];
    switch (app) {
    case APP_TERMINAL:
        w->w = 700; w->h = 440; w->bg = RGB(27, 26, 40);
        w->draw = term_draw; w->key = term_key; w->tick = term_tick;
        if (!tn) {
            t_print(OS_NAME " " OS_VERSION " terminal. Type 'help' to see the commands.", C_DIM);
            t_print("", C_TXT);
        }
        break;
    case APP_CALC:
        w->w = 340; w->h = 520; w->bg = RGB(30, 29, 42);
        w->draw = calc_draw; w->mouse = calc_mouse; w->key = calc_key; w->tick = calc_tick;
        break;
    case APP_NOTES:
        w->w = 600; w->h = 440; w->bg = RGB(255, 255, 255);
        w->draw = notes_draw; w->key = notes_key; w->mouse = notes_mouse; w->tick = notes_tick;
        if (!ninit) {
            ninit = 1;
            strcpy(ntext, "Welcome to Notes!\n\nThis editor runs on AliceOS, an operating system\n"
                          "written from scratch in C and assembly.\n\n"
                          "- Type anywhere to edit\n- Arrows, Home and End move the cursor\n"
                          "- Click to place the cursor\n");
            nlen = strlen(ntext); ncur = nlen;
        }
        break;
    case APP_PAINT:
        w->w = P_W + 2; w->h = TITLE_H + P_BAR + P_H + 1; w->bg = RGB(255, 255, 255);
        w->draw = paint_draw; w->mouse = paint_mouse;
        if (!p_init) { p_init = 1; p_clear(); }
        break;
    case APP_SNAKE:
        w->w = S_GW * S_CW + 2; w->h = TITLE_H + S_HDR + S_GH * S_CW + 1; w->bg = RGB(28, 62, 40);
        w->draw = snake_draw; w->key = snake_key; w->tick = snake_tick;
        if (!s_len) { s_reset(); s_state = 0; }
        break;
    case APP_ABOUT:
        w->w = 520; w->h = 450; w->bg = RGB(255, 255, 255);
        w->draw = about_draw; w->tick = about_tick;
        break;
    }
}
