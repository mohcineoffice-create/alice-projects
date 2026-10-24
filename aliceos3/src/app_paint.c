/* AliceOS 3 - Paint: drawing with colours and brushes, saved as .aimg files (run-length encoded). */
#include "os.h"

#define P_W 718
#define P_H 452
#define P_BAR 52
static u32 canvas[P_W * P_H];
static u32 p_color = RGB(30, 30, 40);
static int p_size = 4, p_last_x = -1, p_last_y = -1, p_init = 0, p_eraser = 0;
static fnode_t *p_file = NULL;
static int p_modified = 0;
static window_t *p_win = NULL;
static const u32 p_pal[12] = { RGB(30, 30, 40), RGB(120, 124, 140), RGB(255, 255, 255), RGB(239, 68, 68),
    RGB(249, 115, 22), RGB(250, 204, 21), RGB(34, 197, 94), RGB(20, 184, 166), RGB(59, 130, 246),
    RGB(124, 92, 255), RGB(236, 72, 153), RGB(146, 64, 14) };
static const int p_sizes[3] = { 2, 5, 10 };

static void p_title(void) {
    if (!p_win) return;
    strcpy(p_win->title, p_modified ? "*" : "");
    strcat(p_win->title, p_file && fs_alive(p_file) ? p_file->name : "Untitled");
    strcat(p_win->title, " - Paint");
}

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

/* ---------------------------------------------------- .aimg format
 * "AIMG" u16 width u16 height, then runs of (u16 count, u8 r, u8 g, u8 b) */
static int p_encode(u8 **out, u32 *len) {
    u32 cap = 4096, n = 0; u8 *b = kmalloc(cap); if (!b) return 0;
    memcpy(b, "AIMG", 4); b[4] = P_W & 255; b[5] = P_W >> 8; b[6] = P_H & 255; b[7] = P_H >> 8; n = 8;
    for (int i = 0; i < P_W * P_H;) {
        u32 c = canvas[i]; int run = 1;
        while (i + run < P_W * P_H && canvas[i + run] == c && run < 65535) run++;
        if (n + 5 > cap) { u8 *nb = krealloc(b, cap * 2); if (!nb) { kfree(b); return 0; } b = nb; cap *= 2; }
        b[n++] = run & 255; b[n++] = run >> 8; b[n++] = c >> 16; b[n++] = c >> 8; b[n++] = c;
        i += run;
    }
    *out = b; *len = n; return 1;
}
static int p_decode(const u8 *d, u32 len) {
    if (len < 8 || d[0] != 'A' || d[1] != 'I' || d[2] != 'M' || d[3] != 'G') return 0;
    int w = d[4] | d[5] << 8, h = d[6] | d[7] << 8;
    p_clear();
    u32 i = 8; int px = 0;
    while (i + 5 <= len && px < w * h) {
        int run = d[i] | d[i + 1] << 8; u32 c = RGB(d[i + 2], d[i + 3], d[i + 4]); i += 5;
        for (int k = 0; k < run && px < w * h; k++, px++) {
            int x = px % w, y = px / w;
            if (x < P_W && y < P_H) canvas[y * P_W + x] = c;
        }
    }
    return 1;
}

static void p_write(fnode_t *f) {
    u8 *data; u32 len;
    if (!p_encode(&data, &len)) { gui_toast("Could not save", "Out of memory"); return; }
    int e = fs_write(f, data, len);
    kfree(data);
    if (e) { gui_toast("Could not save", fs_strerror(e)); return; }
    p_file = f; p_modified = 0; p_title();
    char p[128], s[16]; fs_pretty_path(f, p, sizeof(p)); strcat(p, "  ("); fmt_size(len, s); strcat(p, s); strcat(p, ")");
    gui_toast("Drawing saved", p); gui_dirty();
}
static void save_done(window_t *w, const char *text) {
    (void)w;
    if (!*text) return;
    char path[128]; strncpy0(path, text, 118);
    if (!ends_with(path, ".aimg")) strcat(path, ".aimg");
    fnode_t *f; int e = fs_create(path, fs_home, &f);
    if (e) { gui_toast("Could not save", fs_strerror(e)); return; }
    p_write(f);
}
static void p_save(window_t *w) {
    if (p_file && fs_alive(p_file)) { p_write(p_file); return; }
    char def[64] = "~/Pictures/drawing.aimg";
    for (int i = 2; fs_resolve(def, NULL) && i < 99; i++) { char n[8]; itoa(i, n); strcpy(def, "~/Pictures/drawing-"); strcat(def, n); strcat(def, ".aimg"); }
    gui_prompt(w, "Save drawing as", def, save_done);
}

window_t *paint_open(fnode_t *f) {
    window_t *w = gui_open(APP_PAINT);
    if (!w) return NULL;
    if (!p_decode(f->data, f->size)) { gui_toast("Cannot open", "This file is not an AliceOS image."); return w; }
    p_file = f; p_modified = 0; p_title(); gui_dirty();
    return w;
}

/* ------------------------------------------------------------ window */
static void btn(int x, int y, int w, const char *s, int on) {
    gfx_fill_round(x, y, w, 32, 8, on ? RGB(226, 220, 255) : RGB(232, 232, 240));
    gfx_text_center(x, y + (32 - font_ui.h) / 2, w, s, RGB(40, 40, 52), &font_ui);
}

#define PS 28
#define SX (16 + 12 * PS + 8)
#define EX (SX + 3 * 32 + 4)
static void paint_draw(window_t *w, int cx, int cy, int cw, int ch) {
    (void)w; (void)ch;
    gfx_fill(cx, cy, cw, P_BAR, RGB(244, 244, 248));
    gfx_hline(cx, cy + P_BAR - 1, cw, RGB(224, 224, 232));
    for (int i = 0; i < 12; i++) {
        int x = cx + 16 + i * PS, y = cy + P_BAR / 2;
        if (!p_eraser && p_pal[i] == p_color) gfx_circle_fill(x + 11, y, 14, th.accent);
        gfx_circle_fill(x + 11, y, 11, p_pal[i] == 0xFFFFFF ? RGB(210, 210, 220) : p_pal[i]);
        if (p_pal[i] == 0xFFFFFF) gfx_circle_fill(x + 11, y, 10, 0xFFFFFF);
    }
    for (int i = 0; i < 3; i++) {
        int x = cx + SX + i * 32;
        if (p_sizes[i] == p_size) gfx_fill_round(x, cy + 11, 30, 30, 8, RGB(226, 220, 255));
        gfx_circle_fill(x + 15, cy + 26, p_sizes[i] < 3 ? 3 : p_sizes[i] > 8 ? 9 : p_sizes[i], RGB(40, 40, 52));
    }
    int ex = cx + EX;
    btn(ex, cy + 10, 60, "Eraser", p_eraser);
    btn(ex + 64, cy + 10, 50, "Clear", 0);
    btn(ex + 118, cy + 10, 52, "Open", 0);
    btn(ex + 174, cy + 10, 50, "Save", 0);
    gfx_blit(cx, cy + P_BAR, P_W, P_H, canvas);
}

static void open_pictures(void) {
    window_t *f = gui_new(APP_FILES);
    if (f) files_set_dir(f, fs_resolve("~/Pictures", NULL));
}

static void paint_mouse(window_t *w, int x, int y, int ev) {
    if (ev == EV_DOWN && y < P_BAR) {
        for (int i = 0; i < 12; i++) {
            int bx = 16 + i * PS + 11, by = P_BAR / 2;
            if ((x - bx) * (x - bx) + (y - by) * (y - by) <= 13 * 13) { p_color = p_pal[i]; p_eraser = 0; gui_dirty(); return; }
        }
        for (int i = 0; i < 3; i++) if (x >= SX + i * 32 && x < SX + i * 32 + 30) { p_size = p_sizes[i]; gui_dirty(); return; }
        if (x >= EX && x < EX + 60) { p_eraser = !p_eraser; gui_dirty(); return; }
        if (x >= EX + 64 && x < EX + 114) { p_clear(); p_modified = 1; p_title(); gui_dirty(); return; }
        if (x >= EX + 118 && x < EX + 170) { open_pictures(); return; }
        if (x >= EX + 174 && x < EX + 224) { p_save(w); return; }
        return;
    }
    int px = x, py = y - P_BAR;
    if (ev == EV_DOWN) { p_last_x = px; p_last_y = py; p_stamp(px, py); if (!p_modified) { p_modified = 1; p_title(); } gui_dirty(); }
    else if (ev == EV_MOVE && p_last_x >= 0) { p_line(p_last_x, p_last_y, px, py); p_last_x = px; p_last_y = py; gui_dirty(); }
    else if (ev == EV_UP) p_last_x = -1;
}

static void paint_key(window_t *w, int k) { if (k == K_CTRL('s')) p_save(w); }
static void paint_close(window_t *w) { (void)w; p_win = NULL; }

void paint_setup(window_t *w) {
    w->w = P_W + 2; w->h = TITLE_H + P_BAR + P_H + 1; w->bg = RGB(255, 255, 255);
    w->draw = paint_draw; w->mouse = paint_mouse; w->key = paint_key; w->close = paint_close;
    w->mem = sizeof(canvas);
    p_win = w;
    if (!p_init) { p_init = 1; p_clear(); }
    p_title();
}
