/* AliceOS 3 - desktop, window manager, taskbar, start menu, menus, dialogs and event loop. */
#include "os.h"

#define MAXWIN   16
#define TASK_H   48
#define RADIUS   10
#define BTN_W    46
#define BG_THEME 0xFF000000u      /* window background follows the theme */

/* ---------------------------------------------------------------- theme */
theme_t th;
int set_wallpaper = 0, set_accent = 0;
const u32 accent_colors[6] = { RGB(124, 92, 255), RGB(59, 130, 246), RGB(20, 184, 166),
                               RGB(34, 197, 94), RGB(249, 115, 22), RGB(236, 72, 153) };
const image_t *wallpapers[4] = { &wp_dunes, &wp_aurora, &wp_ocean, &wp_minimal };
const char *wallpaper_names[4] = { "Dunes", "Aurora", "Ocean", "Minimal" };

void theme_apply(int dark, int accent) {
    th.dark = dark; set_accent = accent;
    th.accent = accent_colors[accent];
    if (dark) {
        th.win_bg = RGB(31, 31, 40); th.title_bg = RGB(42, 42, 54); th.title_bg_inactive = RGB(36, 36, 46);
        th.title_fg = RGB(240, 240, 246); th.title_fg_inactive = RGB(138, 138, 153);
        th.text = RGB(232, 232, 240); th.subtext = RGB(160, 160, 176); th.panel = RGB(38, 38, 50);
        th.panel2 = RGB(46, 46, 60); th.line = RGB(56, 56, 70); th.hover = RGB(51, 51, 66);
        th.input_bg = RGB(22, 22, 30); th.border_inactive = RGB(70, 70, 86);
    } else {
        th.win_bg = RGB(255, 255, 255); th.title_bg = RGB(244, 244, 248); th.title_bg_inactive = RGB(233, 233, 238);
        th.title_fg = RGB(30, 32, 40); th.title_fg_inactive = RGB(138, 140, 150);
        th.text = RGB(34, 36, 46); th.subtext = RGB(107, 110, 122); th.panel = RGB(245, 245, 249);
        th.panel2 = RGB(235, 235, 241); th.line = RGB(226, 226, 234); th.hover = RGB(236, 236, 244);
        th.input_bg = RGB(255, 255, 255); th.border_inactive = RGB(150, 150, 165);
    }
    th.accent_soft = gfx_mix(th.win_bg, th.accent, dark ? 90 : 50);
    th.select = th.accent_soft;
    th.border = gfx_mix(th.accent, dark ? 0 : 0xFFFFFF, 90);
}

/* ------------------------------------------------------------- state */
static window_t wins[MAXWIN];
static int order[MAXWIN], norder = 0;          /* z-order: bottom .. top */
static int dirty = 1, next_pid = 100;
static int mx, my, mbtn;
static int drag = -1, drag_dx, drag_dy;        /* moving a window */
static int rsz = -1, rsz_mode, rsz_x0, rsz_y0, rsz_w0, rsz_h0;
static int capture = -1;
static int hover = -1;
static int menu_open = 0;
static int sel_icon = -1, last_icon = -1; static u32 last_click = 0;
static int last_win_click = -1; static u32 last_win_tick = 0; static int last_cx, last_cy;
static char toast_t[48], toast_m[96]; static u32 toast_until = 0;
static int snap = 0;                           /* 0 none, 1 max, 2 left, 3 right */
static int switching = 0, sw_sel = 0;          /* Alt+Tab */
static u32 sw_tick = 0;

u64 cpu_busy_pct_x10 = 0;
int ev_wheel = 0;
u32 cpu_hist[60], mem_hist[60]; int hist_n = 0;

/* context menu */
static struct { int open, x, y, n, hov; char items[8][40]; menu_cb cb; window_t *w; void *ctx; } cm;

void gui_dirty(void) { dirty = 1; }

static window_t *top_window(void) {
    for (int i = norder - 1; i >= 0; i--) {
        window_t *w = &wins[order[i]];
        if (w->visible && !w->minimized) return w;
    }
    return NULL;
}
int gui_focused(window_t *w) { return top_window() == w; }

void gui_toast(const char *t, const char *m) {
    strncpy0(toast_t, t, sizeof(toast_t)); strncpy0(toast_m, m, sizeof(toast_m));
    toast_until = ticks + 5 * HZ; dirty = 1;
}

int gui_client_w(window_t *w) { return w->maximized ? w->w : w->w - 2; }
int gui_client_h(window_t *w) { return w->maximized ? w->h - TITLE_H : w->h - TITLE_H - 1; }

int gui_windows(window_t **out, int max) {
    int n = 0;
    for (int i = 0; i < MAXWIN && n < max; i++) if (wins[i].visible) out[n++] = &wins[i];
    return n;
}

/* ------------------------------------------------------------ windows */
static void raise(int idx) {
    int k = -1;
    for (int i = 0; i < norder; i++) if (order[i] == idx) k = i;
    if (k < 0) return;
    for (int i = k; i < norder - 1; i++) order[i] = order[i + 1];
    order[norder - 1] = idx;
    dirty = 1;
}
void gui_focus(window_t *w) { w->minimized = 0; raise((int)(w - wins)); }

window_t *gui_find(int app) {
    for (int i = 0; i < MAXWIN; i++) if (wins[i].visible && wins[i].app == app) return &wins[i];
    return NULL;
}

static int work_h(void) { return scr_h - TASK_H; }

window_t *gui_new(int app) {
    for (int i = 0; i < MAXWIN; i++) {
        if (wins[i].visible) continue;
        window_t *w = &wins[i];
        memset(w, 0, sizeof(*w));
        w->visible = 1; w->app = app; w->bg = BG_THEME; w->pid = next_pid++;
        w->min_w = 260; w->min_h = 180;
        app_setup(w, app);
        int n = 0; for (int k = 0; k < norder; k++) if (wins[order[k]].visible) n++;
        if (w->w > scr_w - 20) w->w = scr_w - 20;
        if (w->h > work_h() - 20) w->h = work_h() - 20;
        w->x = (scr_w - w->w) / 2 + (n % 6) * 28 - 70;
        w->y = (work_h() - w->h) / 2 + (n % 6) * 24 - 50;
        if (w->x < 4) w->x = 4;
        if (w->y < 4) w->y = 4;
        order[norder++] = i;
        dirty = 1;
        return w;
    }
    gui_toast("Too many windows", "Close a window before opening another one.");
    return NULL;
}

window_t *gui_open(int app) {
    if (!app_multi[app]) {
        window_t *w = gui_find(app);
        if (w) { gui_focus(w); return w; }
    }
    return gui_new(app);
}

void gui_close(window_t *w) {
    if (w->close) w->close(w);
    if (w->state) { kfree(w->state); w->state = NULL; }
    int idx = (int)(w - wins);
    w->visible = 0;
    int k = 0;
    for (int i = 0; i < norder; i++) if (order[i] != idx) order[k++] = order[i];
    norder = k;
    if (drag == idx) drag = -1;
    if (rsz == idx) rsz = -1;
    if (capture == idx) capture = -1;
    if (cm.open && cm.w == w) cm.open = 0;
    dirty = 1;
}

static void maximize(window_t *w, int mode) {         /* mode 0 restore, 1 max, 2 left, 3 right */
    if (!w->resizable) return;
    if (mode && !w->maximized) { w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h; }
    w->maximized = mode;
    if (mode == 1) { w->x = 0; w->y = 0; w->w = scr_w; w->h = work_h(); }
    else if (mode == 2) { w->x = 0; w->y = 0; w->w = scr_w / 2; w->h = work_h(); }
    else if (mode == 3) { w->x = scr_w / 2; w->y = 0; w->w = scr_w - scr_w / 2; w->h = work_h(); }
    else { w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh; }
    dirty = 1;
}

/* ------------------------------------------------------------- dialogs */
void gui_prompt(window_t *w, const char *title, const char *initial, void (*done)(window_t *, const char *)) {
    w->pr_active = 1; strncpy0(w->pr_title, title, sizeof(w->pr_title));
    strncpy0(w->pr_buf, initial, sizeof(w->pr_buf)); w->pr_len = strlen(w->pr_buf);
    w->pr_done = done; gui_focus(w); dirty = 1;
}

void gui_menu(int x, int y, const char **items, int n, menu_cb cb, window_t *w, void *ctx) {
    if (n > 8) n = 8;
    cm.open = 1; cm.n = n; cm.cb = cb; cm.w = w; cm.ctx = ctx; cm.hov = -1;
    for (int i = 0; i < n; i++) strncpy0(cm.items[i], items[i], 40);
    int mw = 210, mh = n * 32 + 8;
    cm.x = x + mw > scr_w ? scr_w - mw - 4 : x;
    cm.y = y + mh > scr_h - TASK_H ? y - mh : y;
    dirty = 1;
}

/* ----------------------------------------------------- hover ids */
enum { H_CLOSE = 100, H_MAX = 200, H_MIN = 300, H_START = 400, H_TASK = 500, H_MENU = 600, H_REBOOT = 700,
       H_OFF = 701, H_ICON = 800, H_RESIZE = 900, H_CM = 1000 };

/* ----------------------------------------------------------- drawing */
static void draw_x(int cx, int cy, int s, u32 c) {
    for (int t = 0; t < 2; t++) {
        gfx_line(cx - s + t, cy - s, cx + s + t, cy + s, c);
        gfx_line(cx - s + t, cy + s, cx + s + t, cy - s, c);
    }
}

static void draw_prompt(window_t *w, int cx, int cy, int cw, int ch) {
    gfx_fill_alpha(cx, cy, cw, ch, 0, 110);
    int bw = cw - 60 < 380 ? cw - 60 : 380, bh = 132, bx = cx + (cw - bw) / 2, by = cy + (ch - bh) / 2;
    gfx_shadow(bx, by, bw, bh, 12, 12, 100);
    gfx_fill_round(bx, by, bw, bh, 12, th.win_bg);
    gfx_round_border(bx, by, bw, bh, 12, th.line);
    gfx_text(bx + 18, by + 16, w->pr_title, th.text, &font_ui_b);
    gfx_fill_round(bx + 18, by + 46, bw - 36, 34, 8, th.input_bg);
    gfx_round_border(bx + 18, by + 46, bw - 36, 34, 8, th.accent);
    int tx = gfx_text(bx + 28, by + 46 + (34 - font_ui.h) / 2, w->pr_buf, th.text, &font_ui);
    if ((ticks / 50) % 2 == 0) gfx_fill(tx + 1, by + 54, 2, 18, th.accent);
    gfx_text(bx + 18, by + 96, "Enter to confirm  -  Esc to cancel", th.subtext, &font_small);
}

static void draw_window(int idx) {
    window_t *w = &wins[idx];
    int focused = gui_focused(w);
    int x = w->x, y = w->y, r = w->maximized ? 0 : RADIUS;
    u32 bg = w->bg == BG_THEME ? th.win_bg : w->bg;
    gfx_reset_clip();
    if (!w->maximized) gfx_shadow(x, y, w->w, w->h, r, focused ? 16 : 10, focused ? 110 : 70);
    gfx_fill_round(x, y, w->w, w->h, r, bg);
    u32 tcol = focused ? th.title_bg : th.title_bg_inactive;
    gfx_fill_round(x, y, w->w, TITLE_H + RADIUS, r, tcol);
    gfx_fill(x, y + TITLE_H, w->w, RADIUS, bg);
    gfx_hline(x, y + TITLE_H - 1, w->w, th.line);
    if (w->icon) gfx_image(x + 12, y + (TITLE_H - 20) / 2, w->icon);
    int nb = w->resizable ? 3 : 2;
    gfx_set_clip(x, y, w->w - nb * BTN_W - 4, TITLE_H);
    gfx_text_fit(x + 40, y + (TITLE_H - font_ui_b.h) / 2, w->w - nb * BTN_W - 48, w->title,
                 focused ? th.title_fg : th.title_fg_inactive, &font_ui_b);
    gfx_reset_clip();

    u32 ic = focused ? th.title_fg : th.title_fg_inactive;
    int bx = x + w->w - BTN_W, by = y;
    if (hover == H_CLOSE + idx) {
        gfx_set_clip(bx, by, BTN_W, TITLE_H - 1);
        gfx_fill_round(bx - RADIUS, by, BTN_W + RADIUS, TITLE_H + RADIUS, r, RGB(232, 17, 35));
        gfx_reset_clip();
    }
    draw_x(bx + BTN_W / 2, by + TITLE_H / 2, 5, hover == H_CLOSE + idx ? RGB(255, 255, 255) : ic);
    if (w->resizable) {
        bx -= BTN_W;
        if (hover == H_MAX + idx) gfx_fill(bx, by, BTN_W, TITLE_H - 1, th.hover);
        if (w->maximized == 1) {
            gfx_rect(bx + BTN_W / 2 - 3, by + TITLE_H / 2 - 7, 9, 9, ic);
            gfx_fill(bx + BTN_W / 2 - 6, by + TITLE_H / 2 - 4, 9, 9, focused ? th.title_bg : th.title_bg_inactive);
            gfx_rect(bx + BTN_W / 2 - 6, by + TITLE_H / 2 - 4, 9, 9, ic);
        } else gfx_rect(bx + BTN_W / 2 - 5, by + TITLE_H / 2 - 5, 11, 11, ic);
    }
    bx -= BTN_W;
    if (hover == H_MIN + idx) gfx_fill(bx, by, BTN_W, TITLE_H - 1, th.hover);
    gfx_fill(bx + BTN_W / 2 - 6, by + TITLE_H / 2, 12, 2, ic);

    int cx = x + 1, cy = y + TITLE_H, cw = w->w - 2, ch = w->h - TITLE_H - 1;
    if (w->maximized) { cx = x; cw = w->w; ch = w->h - TITLE_H; }
    if (w->draw) {
        if (r) {
            gfx_set_clip(cx, cy, cw, ch - RADIUS);
            w->draw(w, cx, cy, cw, ch);
            gfx_set_clip(cx + RADIUS, cy + ch - RADIUS, cw - 2 * RADIUS, RADIUS);
            w->draw(w, cx, cy, cw, ch);
        } else {
            gfx_set_clip(cx, cy, cw, ch);
            w->draw(w, cx, cy, cw, ch);
        }
        gfx_reset_clip();
    }
    if (w->pr_active) { gfx_set_clip(cx, cy, cw, ch); draw_prompt(w, cx, cy, cw, ch); gfx_reset_clip(); }
    if (!w->maximized) {
        gfx_round_border(x, y, w->w, w->h, RADIUS, focused ? th.border : th.border_inactive);
        if (w->resizable)                                  /* grip */
            for (int i = 0; i < 3; i++) gfx_fill(x + w->w - 6 - i * 4, y + w->h - 6, 2, 2, th.subtext),
                                        gfx_fill(x + w->w - 6, y + w->h - 6 - i * 4, 2, 2, th.subtext);
    }
}

/* desktop icons: columns of 6 */
#define ICON_W 92
#define ICON_H 96
static int icon_x(int i) { return 16 + (i / 6) * (ICON_W + 8); }
static int icon_y(int i) { return 16 + (i % 6) * (ICON_H + 8); }

static void draw_desktop(void) {
    gfx_wallpaper();
    for (int i = 0; i < APP_COUNT; i++) {
        int x = icon_x(i), y = icon_y(i);
        if (sel_icon == i) {
            gfx_fill_round_alpha(x, y, ICON_W, ICON_H, 10, RGB(255, 255, 255), 70);
            gfx_round_border(x, y, ICON_W, ICON_H, 10, RGB(220, 210, 255));
        } else if (hover == H_ICON + i) gfx_fill_round_alpha(x, y, ICON_W, ICON_H, 10, RGB(255, 255, 255), 35);
        gfx_image(x + 22, y + 10, app_icons[i]);
        int tw = gfx_text_w(app_names[i], &font_ui);
        gfx_text(x + (ICON_W - tw) / 2 + 1, y + 66, app_names[i], RGB(0, 0, 0), &font_ui);
        gfx_text(x + (ICON_W - tw) / 2, y + 65, app_names[i], RGB(255, 255, 255), &font_ui);
    }
}

static const char *wdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static int task_btn_w(void) {
    int n = 0; for (int i = 0; i < MAXWIN; i++) if (wins[i].visible) n++;
    int avail = scr_w - 60 - 140;
    int w = n ? avail / n : 180;
    return w > 180 ? 180 : w;
}

static void draw_taskbar(void) {
    int y = scr_h - TASK_H;
    gfx_fill_alpha(0, y, scr_w, TASK_H, RGB(18, 14, 34), 215);
    gfx_fill_alpha(0, y, scr_w, 1, RGB(255, 255, 255), 50);
    if (hover == H_START || menu_open) gfx_fill_round_alpha(6, y + 4, 40, 40, 8, RGB(255, 255, 255), menu_open ? 60 : 35);
    gfx_image(6 + 9, y + 13, &img_logo_s);
    int bw = task_btn_w(), bx = 56;
    window_t *top = top_window();
    for (int i = 0; i < MAXWIN; i++) {
        window_t *w = &wins[i];
        if (!w->visible) continue;
        int act = (w == top);
        if (act) gfx_fill_round_alpha(bx, y + 4, bw - 4, 40, 8, RGB(255, 255, 255), 45);
        else if (hover == H_TASK + i) gfx_fill_round_alpha(bx, y + 4, bw - 4, 40, 8, RGB(255, 255, 255), 25);
        if (w->icon) gfx_image(bx + 10, y + 14, w->icon);
        gfx_text_fit(bx + 38, y + (TASK_H - font_ui.h) / 2, bw - 50, w->title,
                     w->minimized ? RGB(170, 168, 190) : RGB(255, 255, 255), &font_ui);
        gfx_fill_round(bx + (bw - 4) / 2 - (act ? 12 : 4), y + TASK_H - 6, act ? 24 : 8, 3, 1,
                       act ? gfx_mix(th.accent, 0xFFFFFF, 120) : RGB(160, 160, 180));
        bx += bw;
    }
    datetime_t t; clock_now(&t);
    char hm[8], b[4]; fmt2(t.hour, hm); hm[2] = ':'; fmt2(t.min, b); hm[3] = b[0]; hm[4] = b[1]; hm[5] = 0;
    char date[24]; strcpy(date, wdays[t.wday]); strcat(date, " ");
    char d2[8]; itoa(t.day, d2); strcat(date, d2); strcat(date, " "); strcat(date, months[(t.mon + 11) % 12]);
    int cw = 110, cx = scr_w - cw - 8;
    gfx_text_center(cx, y + 6, cw, hm, RGB(255, 255, 255), &font_ui_b);
    gfx_text_center(cx, y + 25, cw, date, RGB(200, 196, 220), &font_small);
}

/* start menu: header + 3-column app grid + power buttons */
#define MENU_W 420
#define MCELL_W 128
#define MCELL_H 96
static int menu_rows(void) { return (APP_COUNT + 2) / 3; }
static int menu_h(void) { return 96 + menu_rows() * MCELL_H + 70; }
static int menu_x(void) { return 8; }
static int menu_y(void) { return scr_h - TASK_H - 8 - menu_h(); }
static void menu_cell(int i, int *x, int *y) { *x = menu_x() + 18 + (i % 3) * MCELL_W; *y = menu_y() + 92 + (i / 3) * MCELL_H; }

static void draw_menu(void) {
    int x = menu_x(), y = menu_y(), h = menu_h();
    gfx_shadow(x, y, MENU_W, h, 14, 18, 120);
    gfx_fill_round_alpha(x, y, MENU_W, h, 14, RGB(28, 24, 46), 242);
    gfx_round_border(x, y, MENU_W, h, 14, RGB(90, 80, 130));
    gfx_image(x + 16, y + 12, &img_logo);
    gfx_text(x + 92, y + 20, OS_NAME " " OS_VERSION, RGB(255, 255, 255), &font_title);
    gfx_text(x + 92, y + 54, "Signed in as alice", RGB(190, 186, 214), &font_small);
    for (int i = 0; i < APP_COUNT; i++) {
        int cx, cy; menu_cell(i, &cx, &cy);
        if (hover == H_MENU + i) gfx_fill_round_alpha(cx, cy, MCELL_W - 8, MCELL_H - 8, 10, RGB(255, 255, 255), 30);
        gfx_image(cx + (MCELL_W - 8 - 48) / 2, cy + 10, app_icons[i]);
        gfx_text_center(cx, cy + 64, MCELL_W - 8, app_names[i], RGB(240, 240, 250), &font_small);
    }
    int fy = y + h - 60;
    gfx_hline(x + 14, fy, MENU_W - 28, RGB(70, 64, 100));
    int bw = (MENU_W - 40) / 2;
    gfx_fill_round_alpha(x + 14, fy + 14, bw, 34, 8, RGB(255, 255, 255), hover == H_REBOOT ? 50 : 22);
    gfx_text_center(x + 14, fy + 14 + (34 - font_ui.h) / 2, bw, "Restart", RGB(255, 255, 255), &font_ui);
    gfx_fill_round(x + 26 + bw, fy + 14, bw, 34, 8, hover == H_OFF ? RGB(232, 60, 80) : RGB(196, 43, 64));
    gfx_text_center(x + 26 + bw, fy + 14 + (34 - font_ui.h) / 2, bw, "Shut down", RGB(255, 255, 255), &font_ui);
}

static void draw_cm(void) {
    int w = 210, h = cm.n * 32 + 8;
    gfx_shadow(cm.x, cm.y, w, h, 10, 10, 90);
    gfx_fill_round(cm.x, cm.y, w, h, 10, th.dark ? RGB(40, 40, 52) : RGB(252, 252, 254));
    gfx_round_border(cm.x, cm.y, w, h, 10, th.line);
    for (int i = 0; i < cm.n; i++) {
        int iy = cm.y + 4 + i * 32;
        if (cm.hov == i) gfx_fill_round(cm.x + 4, iy, w - 8, 30, 6, th.accent_soft);
        gfx_text(cm.x + 14, iy + (30 - font_ui.h) / 2, cm.items[i], th.text, &font_ui);
    }
}

static void draw_toast(void) {
    if (!toast_until || ticks > toast_until) { toast_until = 0; return; }
    int w = 360, h = 76, x = scr_w - w - 16, y = 16;
    gfx_shadow(x, y, w, h, 12, 12, 90);
    gfx_fill_round_alpha(x, y, w, h, 12, RGB(30, 26, 48), 235);
    gfx_round_border(x, y, w, h, 12, RGB(100, 90, 150));
    gfx_image(x + 14, y + 16, &img_logo_s);
    gfx_text_fit(x + 46, y + 14, w - 60, toast_t, RGB(255, 255, 255), &font_ui_b);
    gfx_text_fit(x + 46, y + 38, w - 60, toast_m, RGB(200, 196, 224), &font_small);
}

static int sw_list(window_t **l) {                    /* Alt+Tab order: most recent first */
    int n = 0;
    for (int i = norder - 1; i >= 0; i--) if (wins[order[i]].visible) l[n++] = &wins[order[i]];
    return n;
}

static void draw_switcher(void) {
    window_t *l[MAXWIN]; int n = sw_list(l);
    if (n == 0) return;
    int cell = 110, w = n * cell + 30, h = 150, x = (scr_w - w) / 2, y = (work_h() - h) / 2;
    gfx_shadow(x, y, w, h, 16, 18, 120);
    gfx_fill_round_alpha(x, y, w, h, 16, RGB(24, 22, 40), 235);
    for (int i = 0; i < n; i++) {
        int cx = x + 15 + i * cell;
        if (i == sw_sel % n) { gfx_fill_round_alpha(cx, y + 14, cell - 6, 90, 10, RGB(255, 255, 255), 50);
                               gfx_round_border(cx, y + 14, cell - 6, 90, 10, th.accent); }
        gfx_image(cx + (cell - 6 - 48) / 2, y + 30, app_icons[l[i]->app]);
        if (l[i]->minimized) gfx_text_center(cx, y + 82, cell - 6, "minimised", RGB(170, 168, 190), &font_small);
    }
    gfx_text_center(x, y + 116, w, l[sw_sel % n]->title, RGB(255, 255, 255), &font_ui_b);
}

static void compose(void) {
    gfx_reset_clip();
    draw_desktop();
    for (int i = 0; i < norder; i++) {
        window_t *w = &wins[order[i]];
        if (w->visible && !w->minimized) draw_window(order[i]);
    }
    gfx_reset_clip();
    if (snap) {
        int sx = snap == 3 ? scr_w / 2 : 0, sw = snap == 1 ? scr_w : scr_w / 2;
        gfx_fill_round_alpha(sx + 8, 8, sw - 16, work_h() - 16, 12, th.accent, 60);
        gfx_round_border(sx + 8, 8, sw - 16, work_h() - 16, 12, th.accent);
    }
    draw_taskbar();
    if (menu_open) draw_menu();
    if (cm.open) draw_cm();
    if (switching) draw_switcher();
    draw_toast();
}

/* ---------------------------------------------------------- hit tests */
static int in(int px, int py, int x, int y, int w, int h) { return px >= x && py >= y && px < x + w && py < y + h; }

static int window_at(int px, int py) {
    for (int i = norder - 1; i >= 0; i--) {
        window_t *w = &wins[order[i]];
        if (w->visible && !w->minimized && in(px, py, w->x - 4, w->y, w->w + 8, w->h + 4)) return order[i];
    }
    return -1;
}

/* 1 right edge, 2 bottom edge, 3 corner */
static int resize_zone(window_t *w, int px, int py) {
    if (!w->resizable || w->maximized) return 0;
    int r = px >= w->x + w->w - 6 && px < w->x + w->w + 4;
    int b = py >= w->y + w->h - 6 && py < w->y + w->h + 4;
    if (px >= w->x + w->w - 16 && py >= w->y + w->h - 16 && px < w->x + w->w + 4 && py < w->y + w->h + 4) return 3;
    if (r && py > w->y + TITLE_H) return 1;
    if (b && px > w->x) return 2;
    return 0;
}

static int hover_at(int px, int py) {
    if (cm.open && in(px, py, cm.x, cm.y, 210, cm.n * 32 + 8)) {
        int i = (py - cm.y - 4) / 32; return (i >= 0 && i < cm.n) ? H_CM + i : -1;
    }
    if (menu_open && in(px, py, menu_x(), menu_y(), MENU_W, menu_h())) {
        for (int i = 0; i < APP_COUNT; i++) { int cx, cy; menu_cell(i, &cx, &cy); if (in(px, py, cx, cy, MCELL_W - 8, MCELL_H - 8)) return H_MENU + i; }
        int fy = menu_y() + menu_h() - 60, bw = (MENU_W - 40) / 2;
        if (in(px, py, menu_x() + 14, fy + 14, bw, 34)) return H_REBOOT;
        if (in(px, py, menu_x() + 26 + bw, fy + 14, bw, 34)) return H_OFF;
        return -1;
    }
    if (py >= scr_h - TASK_H) {
        if (px < 50) return H_START;
        int bw = task_btn_w(), bx = 56;
        for (int i = 0; i < MAXWIN; i++) {
            if (!wins[i].visible) continue;
            if (in(px, py, bx, scr_h - TASK_H, bw - 4, TASK_H)) return H_TASK + i;
            bx += bw;
        }
        return -1;
    }
    int wi = window_at(px, py);
    if (wi >= 0) {
        window_t *w = &wins[wi];
        if (resize_zone(w, px, py)) return H_RESIZE + wi;
        int bx = w->x + w->w - BTN_W;
        if (in(px, py, bx, w->y, BTN_W, TITLE_H)) return H_CLOSE + wi;
        if (w->resizable) { bx -= BTN_W; if (in(px, py, bx, w->y, BTN_W, TITLE_H)) return H_MAX + wi; }
        bx -= BTN_W;
        if (in(px, py, bx, w->y, BTN_W, TITLE_H)) return H_MIN + wi;
        return -1;
    }
    for (int i = 0; i < APP_COUNT; i++) if (in(px, py, icon_x(i), icon_y(i), ICON_W, ICON_H)) return H_ICON + i;
    return -1;
}

/* ------------------------------------------------------------- events */
static void client_event(int wi, int ev) {
    window_t *w = &wins[wi];
    if (w->pr_active) return;
    if (w->mouse) w->mouse(w, mx - (w->x + (w->maximized ? 0 : 1)), my - (w->y + TITLE_H), ev);
}

static const char *desk_items[] = { "Open Files", "Open Terminal", "Personalise...", "Task Manager", "About AliceOS" };
static void desk_menu_cb(window_t *w, int i, void *ctx) {
    (void)w; (void)ctx;
    static const int apps[] = { APP_FILES, APP_TERMINAL, APP_SETTINGS, APP_TASKMGR, APP_ABOUT };
    gui_open(apps[i]);
}

static void task_menu_cb(window_t *w, int item, void *ctx) {
    (void)item; (void)ctx;
    if (!w->can_close || w->can_close(w)) gui_close(w);
}

static void mouse_down(int right) {
    int h = hover_at(mx, my);
    if (cm.open) {
        if (h >= H_CM && h < H_CM + cm.n && !right) { cm.open = 0; cm.cb(cm.w, h - H_CM, cm.ctx); dirty = 1; return; }
        cm.open = 0; dirty = 1;
        if (in(mx, my, cm.x, cm.y, 210, cm.n * 32 + 8)) return;
    }
    if (menu_open) {
        if (in(mx, my, menu_x(), menu_y(), MENU_W, menu_h())) {
            if (right) return;
            if (h >= H_MENU && h < H_MENU + APP_COUNT) { menu_open = 0; gui_open(h - H_MENU); }
            else if (h == H_REBOOT) sys_reboot();
            else if (h == H_OFF) sys_shutdown();
            dirty = 1; return;
        }
        menu_open = 0; dirty = 1;
        if (h == H_START) return;
    }
    if (h == H_START) { if (!right) { menu_open = 1; dirty = 1; } return; }
    if (h >= H_TASK && h < H_TASK + MAXWIN) {
        int i = h - H_TASK; window_t *w = &wins[i];
        if (right) {
            static const char *it[] = { "Close window" };
            gui_menu(mx, my, it, 1, task_menu_cb, w, NULL);
            return;
        }
        if (w == top_window()) w->minimized = 1; else gui_focus(w);
        dirty = 1; return;
    }
    if (my >= scr_h - TASK_H) return;
    int wi = window_at(mx, my);
    if (wi >= 0) {
        window_t *w = &wins[wi];
        raise(wi); sel_icon = -1;
        if (right) { if (my >= w->y + TITLE_H) client_event(wi, EV_RDOWN); return; }
        if (h == H_RESIZE + wi) {
            rsz = wi; rsz_mode = resize_zone(w, mx, my);
            rsz_x0 = mx; rsz_y0 = my; rsz_w0 = w->w; rsz_h0 = w->h; return;
        }
        if (h == H_CLOSE + wi) { if (!w->can_close || w->can_close(w)) gui_close(w); hover = -1; return; }
        if (h == H_MAX + wi) { maximize(w, w->maximized ? 0 : 1); hover = -1; return; }
        if (h == H_MIN + wi) { w->minimized = 1; hover = -1; dirty = 1; return; }
        int dbl = (last_win_click == wi && ticks - last_win_tick < 45 &&
                   mx - last_cx < 6 && last_cx - mx < 6 && my - last_cy < 6 && last_cy - my < 6);
        last_win_click = wi; last_win_tick = ticks; last_cx = mx; last_cy = my;
        if (my < w->y + TITLE_H) {
            if (dbl && w->resizable) { maximize(w, w->maximized ? 0 : 1); last_win_click = -1; return; }
            drag = wi; drag_dx = mx - w->x; drag_dy = my - w->y; return;
        }
        if (w->pr_active) return;
        capture = wi; client_event(wi, EV_DOWN);
        if (dbl) { client_event(wi, EV_DBL); last_win_click = -1; }
        dirty = 1;
        return;
    }
    if (right) { gui_menu(mx, my, desk_items, 5, desk_menu_cb, NULL, NULL); sel_icon = -1; return; }
    if (h >= H_ICON && h < H_ICON + APP_COUNT) {
        int i = h - H_ICON;
        if (last_icon == i && ticks - last_click < 45) { gui_open(i); sel_icon = -1; last_icon = -1; }
        else { sel_icon = i; last_icon = i; last_click = ticks; }
    } else sel_icon = -1;
    dirty = 1;
}

static void mouse_move(void) {
    if (drag >= 0) {
        window_t *w = &wins[drag];
        if (w->maximized) {                               /* pull a maximised window out */
            int rw = w->rw;
            maximize(w, 0);
            drag_dx = rw / 2; w->x = mx - drag_dx; w->y = my - drag_dy;
        }
        int nx = mx - drag_dx, ny = my - drag_dy;
        if (ny < 0) ny = 0;
        if (ny > work_h() - TITLE_H) ny = work_h() - TITLE_H;
        if (nx < 80 - w->w) nx = 80 - w->w;
        if (nx > scr_w - 80) nx = scr_w - 80;
        if (nx != w->x || ny != w->y) { w->x = nx; w->y = ny; dirty = 1; }
        int s = 0;
        if (w->resizable) s = my <= 1 ? 1 : mx <= 1 ? 2 : mx >= scr_w - 2 ? 3 : 0;
        if (s != snap) { snap = s; dirty = 1; }
        return;
    }
    if (rsz >= 0) {
        window_t *w = &wins[rsz];
        int nw = rsz_w0 + (rsz_mode & 1 ? mx - rsz_x0 : 0), nh = rsz_h0 + (rsz_mode & 2 ? my - rsz_y0 : 0);
        if (nw < w->min_w) nw = w->min_w;
        if (nh < w->min_h) nh = w->min_h;
        if (w->x + nw > scr_w) nw = scr_w - w->x;
        if (w->y + nh > work_h()) nh = work_h() - w->y;
        if (nw != w->w || nh != w->h) { w->w = nw; w->h = nh; dirty = 1; }
        return;
    }
    if (capture >= 0) { client_event(capture, EV_MOVE); return; }
    int h = hover_at(mx, my);
    if (cm.open) { int c = (h >= H_CM && h < H_CM + cm.n) ? h - H_CM : -1; if (c != cm.hov) { cm.hov = c; dirty = 1; } }
    if (h != hover) { hover = h; dirty = 1; }
}

static void mouse_up(void) {
    if (capture >= 0) { client_event(capture, EV_UP); capture = -1; }
    if (drag >= 0 && snap) { maximize(&wins[drag], snap); snap = 0; dirty = 1; }
    drag = -1; rsz = -1;
}

/* --------------------------------------------------------------- keys */
static void key_event(int k) {
    if (k == K_ALTTAB) {
        window_t *l[MAXWIN]; int n = sw_list(l);
        if (!n) return;
        if (!switching) { switching = 1; sw_sel = n > 1 ? 1 : 0; } else sw_sel = (sw_sel + 1) % n;
        sw_tick = ticks;
        dirty = 1; return;
    }
    if (k == K_SUPER) { menu_open = !menu_open; dirty = 1; return; }
    if (k == 27 && (menu_open || cm.open)) { menu_open = 0; cm.open = 0; dirty = 1; return; }
    window_t *w = top_window();
    if (!w) return;
    if (w->pr_active) {
        if (k == 27) w->pr_active = 0;
        else if (k == '\n') { w->pr_active = 0; w->pr_buf[w->pr_len] = 0; if (w->pr_done) w->pr_done(w, w->pr_buf); }
        else if (k == '\b') { if (w->pr_len) w->pr_buf[--w->pr_len] = 0; }
        else if (k >= 32 && k < 127 && w->pr_len < 47) { w->pr_buf[w->pr_len++] = (char)k; w->pr_buf[w->pr_len] = 0; }
        dirty = 1; return;
    }
    if (k == K_CTRL('w')) { if (!w->can_close || w->can_close(w)) gui_close(w); return; }
    if (w->key) w->key(w, k);
}

/* ------------------------------------------------------------- splash */
static void splash(void) {
    u32 start = ticks, dur = 160;
    while (ticks - start < dur) {
        int p = (int)((ticks - start) * 1000 / dur);
        gfx_reset_clip();
        gfx_vgradient(0, 0, scr_w, scr_h, RGB(14, 12, 36), RGB(44, 20, 74));
        int cx = scr_w / 2, cy = scr_h / 2 - 60;
        gfx_image(cx - 64, cy - 64, &img_logo_xl);
        gfx_text_center(0, cy + 84, scr_w, OS_NAME " " OS_VERSION, RGB(255, 255, 255), &font_title);
        int bw = 220, bx = cx - bw / 2, by = cy + 136;
        gfx_fill_round(bx, by, bw, 6, 3, RGB(70, 60, 110));
        int fw = bw * p / 1000; if (fw < 6) fw = 6;
        gfx_fill_round(bx, by, fw, 6, 3, RGB(140, 200, 255));
        const char *msg = p < 250 ? "Starting kernel..." : p < 500 ? "Mounting file system..." :
                          p < 750 ? "Loading drivers..." : "Preparing desktop...";
        gfx_text_center(0, by + 22, scr_w, msg, RGB(180, 176, 210), &font_small);
        gfx_text_center(0, scr_h - 40, scr_w, "Written from scratch in C and assembly", RGB(120, 116, 160), &font_small);
        gfx_present();
        __asm__ volatile("hlt");
    }
}

/* --------------------------------------------------------------- loop */
void gui_run(void) {
    theme_apply(0, 0);
    gfx_set_wallpaper(wallpapers[set_wallpaper]);
    mx = scr_w / 2; my = scr_h / 2;
    splash();
    gui_toast("Welcome to AliceOS 3.0", "Right-click the desktop, or press Alt+Tab to switch windows.");
    int cur_x = -1, cur_y = -1, cur_shape = 0, last_min = -1;
    u32 last_tick = ticks, sec_tick = ticks;
    u64 busy = 0, sec_start = rdtsc();
    for (;;) {
        u64 t0 = rdtsc();
        int k;
        while ((k = key_get()) >= 0) key_event(k);
        if (switching && (!kbd_alt() || ticks - sw_tick > 2 * HZ || mbtn)) {
            window_t *l[MAXWIN]; int n = sw_list(l);
            if (n) gui_focus(l[sw_sel % n]);
            switching = 0; dirty = 1;
        }
        mouse_pkt_t p;
        while (mouse_get(&p)) {
            mx += p.dx; my += p.dy;
            if (mx < 0) mx = 0;
            if (my < 0) my = 0;
            if (mx >= scr_w) mx = scr_w - 1;
            if (my >= scr_h) my = scr_h - 1;
            int b = p.buttons & 3;
            if (p.dx || p.dy) mouse_move();
            if (p.dz) {
                int wi = window_at(mx, my);
                if (wi >= 0 && my >= wins[wi].y + TITLE_H) { ev_wheel = p.dz; client_event(wi, EV_WHEEL); }
            }
            if ((b & 1) && !(mbtn & 1)) mouse_down(0);
            if ((b & 2) && !(mbtn & 2)) mouse_down(1);
            if (!(b & 1) && (mbtn & 1)) mouse_up();
            mbtn = b;
        }
        if (ticks != last_tick) {
            last_tick = ticks;
            for (int i = 0; i < MAXWIN; i++)
                if (wins[i].visible && wins[i].tick) wins[i].tick(&wins[i]);
            for (int i = 0; i < MAXWIN; i++) if (wins[i].visible && wins[i].pr_active && ticks % 50 == 0) dirty = 1;
            datetime_t t; clock_now(&t);
            if (t.min != last_min) { last_min = t.min; dirty = 1; }
            if (toast_until && ticks > toast_until) dirty = 1;
        }
        if (dirty) {
            dirty = 0;
            compose();
            gfx_present();
            cur_shape = (rsz >= 0 || (hover >= H_RESIZE && hover < H_RESIZE + MAXWIN)) ? 1 : 0;
            gfx_cursor(mx, my, cur_shape); cur_x = mx; cur_y = my;
        } else {
            int shape = (rsz >= 0 || (hover >= H_RESIZE && hover < H_RESIZE + MAXWIN)) ? 1 : 0;
            if (mx != cur_x || my != cur_y || shape != cur_shape) {
                gfx_cursor_restore(cur_x, cur_y);
                gfx_cursor(mx, my, shape); cur_x = mx; cur_y = my; cur_shape = shape;
            }
        }
        busy += rdtsc() - t0;
        if (ticks - sec_tick >= HZ) {                      /* CPU and memory statistics, once a second */
            u64 now = rdtsc(), span = now - sec_start;
            u64 pct = span ? busy * 1000 / span : 0;
            cpu_busy_pct_x10 = pct > 1000 ? 1000 : pct;
            for (int i = 59; i > 0; i--) { cpu_hist[i] = cpu_hist[i - 1]; mem_hist[i] = mem_hist[i - 1]; }
            cpu_hist[0] = (u32)cpu_busy_pct_x10;
            mem_hist[0] = heap_total() ? (u32)((u64)heap_used() * 1000 / heap_total()) : 0;
            if (hist_n < 60) hist_n++;
            busy = 0; sec_start = now; sec_tick = ticks;
        }
        __asm__ volatile("hlt");
    }
}
