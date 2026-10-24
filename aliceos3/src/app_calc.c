/* AliceOS 3 - Calculator: decimal calculator with fixed-point arithmetic. */
#include "os.h"

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


void calc_setup(window_t *w) {
    w->w = 340; w->h = 520; w->bg = RGB(30, 29, 42);
    w->draw = calc_draw; w->mouse = calc_mouse; w->key = calc_key; w->tick = calc_tick;
}
