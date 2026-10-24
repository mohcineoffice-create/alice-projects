/* AliceOS 3 - Notes: a text editor that opens and saves files. */
#include "os.h"

#define N_TB  44
#define N_PAD 14
#define N_ST  28
#define GUT   46
typedef struct {
    char *text; u32 len, cap;
    int cur, top;
    fnode_t *file;
    int modified; u32 warn;
} notes_t;

static int lh(void) { return font_mono.h + 4; }
static int adv(void) { return font_mono.g[0].adv; }

static int ensure(notes_t *n, u32 need) {
    if (need <= n->cap) return 1;
    u32 cap = n->cap ? n->cap : 1024; while (cap < need) cap *= 2;
    char *t = krealloc(n->text, cap); if (!t) return 0;
    n->text = t; n->cap = cap; return 1;
}

static void set_title(window_t *w) {
    notes_t *n = w->state;
    strcpy(w->title, n->modified ? "*" : "");
    strcat(w->title, n->file && fs_alive(n->file) ? n->file->name : "Untitled");
    strcat(w->title, " - Notes");
}

static void linecol(notes_t *n, int pos, int *line, int *col) {
    int l = 0, c = 0;
    for (int i = 0; i < pos; i++) { if (n->text[i] == '\n') { l++; c = 0; } else c++; }
    *line = l; *col = c;
}
static int pos_of(notes_t *n, int line, int col) {
    int i = 0, l = 0;
    while (i < (int)n->len && l < line) { if (n->text[i] == '\n') l++; i++; }
    if (l < line) return n->len;
    int c = 0; while (i < (int)n->len && n->text[i] != '\n' && c < col) { i++; c++; }
    return i;
}
static int nlines(notes_t *n) { int l = 1; for (u32 i = 0; i < n->len; i++) if (n->text[i] == '\n') l++; return l; }

static void load(window_t *w, fnode_t *f) {
    notes_t *n = w->state;
    n->file = f; n->len = 0; n->cur = 0; n->top = 0; n->modified = 0;
    if (f && f->size && ensure(n, f->size + 1)) { memcpy(n->text, f->data, f->size); n->len = f->size; }
    if (ensure(n, n->len + 1)) n->text[n->len] = 0;
    set_title(w);
}

/* -------------------------------------------------------------- saving */
static void do_save(window_t *w, fnode_t *f) {
    notes_t *n = w->state;
    int e = fs_write(f, n->text, n->len);
    if (e) { gui_toast("Could not save", fs_strerror(e)); return; }
    n->file = f; n->modified = 0; set_title(w);
    char p[128]; fs_pretty_path(f, p, sizeof(p)); gui_toast("Saved", p);
    gui_dirty();
}

static void save_as_done(window_t *w, const char *text) {
    if (!*text) return;
    char path[128]; strncpy0(path, text, 120);
    char *base = strrchr(path, '/'); base = base ? base + 1 : path;
    if (!strrchr(base, '.')) strcat(path, ".txt");
    fnode_t *f; int e = fs_create(path, fs_home, &f);
    if (e) { gui_toast("Could not save", fs_strerror(e)); return; }
    do_save(w, f);
}

static void save(window_t *w, int as) {
    notes_t *n = w->state;
    if (!as && n->file && fs_alive(n->file)) { do_save(w, n->file); return; }
    char def[96] = "~/Documents/untitled.txt";
    if (n->file && fs_alive(n->file)) fs_pretty_path(n->file, def, sizeof(def));
    gui_prompt(w, "Save as (path from your home folder)", def, save_as_done);
}

/* ------------------------------------------------------------ drawing */
static void tbtn(int x, int y, int w, const char *s) {
    gfx_fill_round(x, y, w, 30, 7, th.panel2);
    gfx_text_center(x, y + (30 - font_ui.h) / 2, w, s, th.text, &font_ui);
}

static void notes_draw(window_t *w, int cx, int cy, int cw, int ch) {
    notes_t *n = w->state;
    if (n->file && !fs_alive(n->file)) { n->file = NULL; n->modified = 1; set_title(w); }
    gfx_fill(cx, cy, cw, N_TB, th.panel);
    gfx_hline(cx, cy + N_TB - 1, cw, th.line);
    tbtn(cx + 10, cy + 7, 60, "New"); tbtn(cx + 76, cy + 7, 64, "Open"); tbtn(cx + 146, cy + 7, 60, "Save");
    tbtn(cx + 212, cy + 7, 80, "Save as");
    char p[128];
    if (n->file) fs_pretty_path(n->file, p, sizeof(p)); else strcpy(p, "not saved yet");
    gfx_text_fit(cx + 306, cy + (N_TB - font_small.h) / 2, cw - 320, p, th.subtext, &font_small);

    int L = lh(), A = adv(), by = cy + N_TB;
    int bh = ch - N_TB - N_ST;
    int rows = (bh - N_PAD) / L;
    int cl, cc; linecol(n, n->cur, &cl, &cc);
    if (cl < n->top) n->top = cl;
    if (cl >= n->top + rows) n->top = cl - rows + 1;
    gfx_fill(cx, by, GUT, bh, th.panel);
    gfx_vline(cx + GUT, by, bh, th.line);
    int line = 0, i = 0, total = nlines(n);
    while (line < n->top && i < (int)n->len) { if (n->text[i] == '\n') line++; i++; }
    int y = by + N_PAD / 2 + 2;
    for (int r = 0; r < rows && line < total; r++, line++, y += L) {
        char num[8]; itoa(line + 1, num);
        gfx_text(cx + GUT - 8 - gfx_text_w(num, &font_small), y + 2, num, line == cl ? th.text : th.subtext, &font_small);
        if (line == cl) gfx_fill_alpha(cx + GUT + 1, y - 2, cw - GUT - 1, L, th.accent, th.dark ? 30 : 16);
        int x = cx + GUT + N_PAD;
        while (i < (int)n->len && n->text[i] != '\n') {
            if (x < cx + cw) gfx_char(x, y, n->text[i], th.text, &font_mono);
            x += A; i++;
        }
        i++;
    }
    if (gui_focused(w) && (ticks / 50) % 2 == 0 && !w->pr_active)
        gfx_fill(cx + GUT + N_PAD + cc * A, by + N_PAD / 2 + 2 + (cl - n->top) * L - 1, 2, L - 2, th.accent);
    int sy = cy + ch - N_ST;
    gfx_fill(cx, sy, cw, N_ST, th.panel);
    gfx_hline(cx, sy, cw, th.line);
    char st[64], b[12]; strcpy(st, "Ln "); itoa(cl + 1, b); strcat(st, b); strcat(st, ", Col "); itoa(cc + 1, b); strcat(st, b);
    gfx_text(cx + 14, sy + (N_ST - font_small.h) / 2, st, th.subtext, &font_small);
    itoa(n->len, b); strcpy(st, b); strcat(st, " characters   |   Ctrl+S to save");
    gfx_text(cx + cw - 14 - gfx_text_w(st, &font_small), sy + (N_ST - font_small.h) / 2, st, th.subtext, &font_small);
}

/* ------------------------------------------------------------ editing */
static void ins(notes_t *n, char c) {
    if (!ensure(n, n->len + 2)) return;
    memmove(n->text + n->cur + 1, n->text + n->cur, n->len - n->cur);
    n->text[n->cur++] = c; n->len++; n->text[n->len] = 0; n->modified = 1;
}
static void del(notes_t *n, int at) {
    if (at < 0 || at >= (int)n->len) return;
    memmove(n->text + at, n->text + at + 1, n->len - at - 1);
    n->len--; n->text[n->len] = 0; n->modified = 1;
}

static void notes_key(window_t *w, int k) {
    notes_t *n = w->state;
    int l, c; linecol(n, n->cur, &l, &c);
    int was = n->modified;
    if (k == K_CTRL('s')) { save(w, 0); return; }
    if (k == K_CTRL('n')) { gui_new(APP_NOTES); return; }
    if (k == K_CTRL('o')) { window_t *f = gui_new(APP_FILES); if (f) files_set_dir(f, fs_resolve("~/Documents", NULL)); return; }
    if (k == '\b') { if (n->cur > 0) { n->cur--; del(n, n->cur); } }
    else if (k == K_DEL) del(n, n->cur);
    else if (k == K_LEFT) { if (n->cur > 0) n->cur--; }
    else if (k == K_RIGHT) { if (n->cur < (int)n->len) n->cur++; }
    else if (k == K_UP) { if (l > 0) n->cur = pos_of(n, l - 1, c); }
    else if (k == K_DOWN) n->cur = pos_of(n, l + 1, c);
    else if (k == K_HOME) n->cur = pos_of(n, l, 0);
    else if (k == K_END) n->cur = pos_of(n, l, 100000);
    else if (k == K_PGUP) n->cur = pos_of(n, l > 15 ? l - 15 : 0, c);
    else if (k == K_PGDN) n->cur = pos_of(n, l + 15, c);
    else if (k == '\t') { for (int i = 0; i < 4; i++) ins(n, ' '); }
    else if (k == '\n' || (k >= 32 && k < 127)) ins(n, (char)k);
    if (n->modified != was) set_title(w);
    gui_dirty();
}

static void notes_mouse(window_t *w, int x, int y, int ev) {
    notes_t *n = w->state;
    if (ev == EV_WHEEL) { n->top += ev_wheel * 3; if (n->top < 0) n->top = 0;
                          int cl, cc; linecol(n, n->cur, &cl, &cc);
                          int rows = (gui_client_h(w) - N_TB - N_ST - N_PAD) / lh();
                          if (n->top > nlines(n) - 1) n->top = nlines(n) - 1;
                          if (cl < n->top) n->cur = pos_of(n, n->top, cc);
                          if (cl >= n->top + rows) n->cur = pos_of(n, n->top + rows - 1, cc);
                          gui_dirty(); return; }
    if (ev != EV_DOWN && ev != EV_MOVE) return;
    if (y < N_TB) {
        if (ev != EV_DOWN) return;
        if (x >= 10 && x < 70) gui_new(APP_NOTES);
        else if (x >= 76 && x < 140) { window_t *f = gui_new(APP_FILES); if (f) files_set_dir(f, fs_resolve("~/Documents", NULL)); }
        else if (x >= 146 && x < 206) save(w, 0);
        else if (x >= 212 && x < 292) save(w, 1);
        return;
    }
    int line = n->top + (y - N_TB - N_PAD / 2 - 2) / lh(); if (line < 0) line = 0;
    int col = (x - GUT - N_PAD + adv() / 2) / adv(); if (col < 0) col = 0;
    n->cur = pos_of(n, line, col); gui_dirty();
}

static void notes_tick(window_t *w) { if (gui_focused(w) && ticks % 50 == 0) gui_dirty(); }

static int notes_can_close(window_t *w) {
    notes_t *n = w->state;
    if (!n->modified || (n->warn && ticks - n->warn < 5 * HZ)) return 1;
    n->warn = ticks;
    gui_toast("Unsaved changes", "Press Ctrl+S to save, or close again to discard them.");
    return 0;
}
static void notes_close(window_t *w) { notes_t *n = w->state; if (n) kfree(n->text); }

void notes_setup(window_t *w) {
    w->w = 640; w->h = 460; w->resizable = 1; w->min_w = 400; w->min_h = 240;
    w->draw = notes_draw; w->key = notes_key; w->mouse = notes_mouse; w->tick = notes_tick;
    w->can_close = notes_can_close; w->close = notes_close;
    notes_t *n = kzalloc(sizeof(notes_t));
    w->state = n; w->mem = sizeof(notes_t);
    if (!n) return;
    ensure(n, 1024); n->text[0] = 0;
    set_title(w);
}

window_t *notes_open(fnode_t *f) {
    window_t *ws[16]; int c = gui_windows(ws, 16);
    for (int i = 0; i < c; i++)
        if (ws[i]->app == APP_NOTES && ((notes_t *)ws[i]->state)->file == f) { gui_focus(ws[i]); return ws[i]; }
    window_t *w = gui_new(APP_NOTES);
    if (w) { load(w, f); w->mem = sizeof(notes_t) + ((notes_t *)w->state)->cap; }
    return w;
}
