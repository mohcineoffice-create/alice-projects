/* AliceOS 2 - desktop, window manager, taskbar, start menu and event loop. */
#include "os.h"

#define MAXWIN   12
#define TASK_H   48
#define RADIUS   10
#define BTN_W    46

static window_t wins[MAXWIN];
static int order[MAXWIN], norder = 0;       /* z-order: bottom .. top */
static int dirty = 1;

static int mx, my, mbtn;                    /* mouse */
static int drag = -1, drag_dx, drag_dy;     /* window being moved */
static int capture = -1;                    /* window receiving client drag */
static int hover = -1;                      /* hovered UI element id */
static int menu_open = 0;
static int sel_icon = -1, last_icon = -1; static u32 last_click = 0;
static char toast_t[48], toast_m[96]; static u32 toast_until = 0;

#define ACCENT RGB(124, 92, 255)

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
    strcpy(toast_t, t); strcpy(toast_m, m); toast_until = ticks + 6 * HZ; dirty = 1;
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

window_t *gui_open(int app) {
    for (int i = 0; i < MAXWIN; i++)                 /* single instance per app */
        if (wins[i].visible && wins[i].app == app) {
            wins[i].minimized = 0; raise(i); return &wins[i];
        }
    for (int i = 0; i < MAXWIN; i++) {
        if (wins[i].visible) continue;
        window_t *w = &wins[i];
        memset(w, 0, sizeof(*w));
        w->visible = 1; w->app = app; w->bg = RGB(255, 255, 255);
        app_setup(w, app);
        int n = 0; for (int k = 0; k < norder; k++) if (wins[order[k]].visible) n++;
        w->x = (scr_w - w->w) / 2 + (n % 5) * 30 - 60;
        w->y = (scr_h - TASK_H - w->h) / 2 + (n % 5) * 26 - 50;
        if (w->x < 4) w->x = 4;
        if (w->y < 4) w->y = 4;
        order[norder++] = i;
        dirty = 1;
        return w;
    }
    gui_toast("Too many windows", "Close a window before opening another one.");
    return NULL;
}

void gui_close(window_t *w) {
    int idx = (int)(w - wins);
    w->visible = 0;
    int k = 0;
    for (int i = 0; i < norder; i++) if (order[i] != idx) order[k++] = order[i];
    norder = k;
    if (drag == idx) drag = -1;
    if (capture == idx) capture = -1;
    dirty = 1;
}

/* ---------------------------------------------------- hover id scheme */
enum { H_CLOSE = 100, H_MIN = 200, H_START = 300, H_TASK = 400, H_MENU = 500, H_REBOOT = 600,
       H_OFF = 601, H_ICON = 700 };

/* ----------------------------------------------------------- drawing */
static void draw_x(int cx, int cy, int s, u32 c) {
    for (int t = 0; t < 2; t++) {
        gfx_line(cx - s + t, cy - s, cx + s + t, cy + s, c);
        gfx_line(cx - s + t, cy + s, cx + s + t, cy - s, c);
    }
}

static void draw_window(int idx) {
    window_t *w = &wins[idx];
    int focused = gui_focused(w);
    int x = w->x, y = w->y;
    gfx_reset_clip();
    gfx_shadow(x, y, w->w, w->h, RADIUS, focused ? 16 : 10, focused ? 110 : 70);
    gfx_fill_round(x, y, w->w, w->h, RADIUS, w->bg);
    u32 tcol = focused ? RGB(246, 246, 250) : RGB(232, 233, 238);
    gfx_fill_round(x, y, w->w, TITLE_H + RADIUS, RADIUS, tcol);
    gfx_fill(x, y + TITLE_H, w->w, RADIUS, w->bg);
    gfx_hline(x, y + TITLE_H - 1, w->w, RGB(222, 224, 230));
    if (w->icon) gfx_image(x + 12, y + (TITLE_H - 20) / 2, w->icon);
    gfx_set_clip(x, y, w->w - 2 * BTN_W, TITLE_H);
    gfx_text(x + 40, y + (TITLE_H - font_ui_b.h) / 2, w->title,
             focused ? RGB(30, 32, 40) : RGB(130, 132, 140), &font_ui_b);
    gfx_reset_clip();

    /* title buttons */
    int bx = x + w->w - BTN_W, by = y;
    if (hover == H_CLOSE + idx) {
        gfx_set_clip(bx, by, BTN_W, TITLE_H - 1);
        gfx_fill_round(bx - RADIUS, by, BTN_W + RADIUS, TITLE_H + RADIUS, RADIUS, RGB(232, 17, 35));
        gfx_reset_clip();
    }
    draw_x(bx + BTN_W / 2, by + TITLE_H / 2, 5, hover == H_CLOSE + idx ? RGB(255, 255, 255) : RGB(60, 62, 70));
    bx -= BTN_W;
    if (hover == H_MIN + idx) gfx_fill(bx, by, BTN_W, TITLE_H - 1, RGB(222, 224, 232));
    gfx_fill(bx + BTN_W / 2 - 6, by + TITLE_H / 2, 12, 2, RGB(60, 62, 70));

    /* client area, clipped so the rounded bottom corners stay intact */
    int cx = x + 1, cy = y + TITLE_H, cw = w->w - 2, ch = w->h - TITLE_H - 1;
    if (w->draw) {
        gfx_set_clip(cx, cy, cw, ch - RADIUS);
        w->draw(w, cx, cy, cw, ch);
        gfx_set_clip(cx + RADIUS, cy + ch - RADIUS, cw - 2 * RADIUS, RADIUS);
        w->draw(w, cx, cy, cw, ch);
        gfx_reset_clip();
    }
    gfx_round_border(x, y, w->w, w->h, RADIUS, focused ? RGB(150, 140, 200) : RGB(120, 120, 135));
}

static int icon_x(int i) { (void)i; return 20; }
static int icon_y(int i) { return 20 + i * 104; }

static void draw_desktop(void) {
    gfx_wallpaper();
    for (int i = 0; i < APP_COUNT; i++) {
        int x = icon_x(i), y = icon_y(i);
        if (sel_icon == i) {
            gfx_fill_round_alpha(x, y, 92, 96, 10, RGB(255, 255, 255), 70);
            gfx_round_border(x, y, 92, 96, 10, RGB(220, 210, 255));
        } else if (hover == H_ICON + i) {
            gfx_fill_round_alpha(x, y, 92, 96, 10, RGB(255, 255, 255), 35);
        }
        gfx_image(x + 22, y + 10, app_icons[i]);
        int tw = gfx_text_w(app_names[i], &font_ui);
        gfx_text(x + (92 - tw) / 2 + 1, y + 66, app_names[i], RGB(0, 0, 0), &font_ui);
        gfx_text(x + (92 - tw) / 2, y + 65, app_names[i], RGB(255, 255, 255), &font_ui);
    }
}

static const char *wdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static int task_btn_w(void) {
    int n = 0; for (int i = 0; i < norder; i++) if (wins[order[i]].visible) n++;
    int avail = scr_w - 60 - 140;
    int w = n ? avail / n : 180;
    return w > 180 ? 180 : w;
}

/* taskbar buttons follow window creation order (slot index), not z-order */
static void draw_taskbar(void) {
    int y = scr_h - TASK_H;
    gfx_fill_alpha(0, y, scr_w, TASK_H, RGB(18, 14, 34), 215);
    gfx_fill_alpha(0, y, scr_w, 1, RGB(255, 255, 255), 50);

    /* start button */
    if (hover == H_START || menu_open) gfx_fill_round_alpha(6, y + 4, 40, 40, 8, RGB(255, 255, 255), menu_open ? 60 : 35);
    gfx_image(6 + 9, y + 13, &img_logo_s);

    /* window buttons */
    int bw = task_btn_w(), bx = 56;
    window_t *top = top_window();
    for (int i = 0; i < MAXWIN; i++) {
        window_t *w = &wins[i];
        if (!w->visible) continue;
        int act = (w == top);
        if (act) gfx_fill_round_alpha(bx, y + 4, bw - 4, 40, 8, RGB(255, 255, 255), 45);
        else if (hover == H_TASK + i) gfx_fill_round_alpha(bx, y + 4, bw - 4, 40, 8, RGB(255, 255, 255), 25);
        if (w->icon) gfx_image(bx + 10, y + 14, w->icon);
        gfx_set_clip(bx, y, bw - 12, TASK_H);
        gfx_text(bx + 38, y + (TASK_H - font_ui.h) / 2, w->title,
                 w->minimized ? RGB(170, 168, 190) : RGB(255, 255, 255), &font_ui);
        gfx_reset_clip();
        gfx_fill_round(bx + (bw - 4) / 2 - (act ? 12 : 4), y + TASK_H - 6, act ? 24 : 8, 3, 1,
                       act ? RGB(150, 220, 255) : RGB(160, 160, 180));
        bx += bw;
    }

    /* clock */
    datetime_t t; rtc_read(&t);
    char hm[8], b[4]; fmt2(t.hour, hm); hm[2] = ':'; fmt2(t.min, b); hm[3] = b[0]; hm[4] = b[1]; hm[5] = 0;
    char date[24]; strcpy(date, wdays[t.wday]); strcat(date, " ");
    char d2[8]; itoa(t.day, d2); strcat(date, d2); strcat(date, " "); strcat(date, months[(t.mon + 11) % 12]);
    int cw = 110, cx = scr_w - cw - 8;
    gfx_text_center(cx, y + 6, cw, hm, RGB(255, 255, 255), &font_ui_b);
    gfx_text_center(cx, y + 25, cw, date, RGB(200, 196, 220), &font_small);
}

#define MENU_W 320
#define MENU_ITEM 46
static int menu_h(void) { return 84 + APP_COUNT * MENU_ITEM + 64; }
static int menu_x(void) { return 8; }
static int menu_y(void) { return scr_h - TASK_H - 8 - menu_h(); }

static void draw_menu(void) {
    int x = menu_x(), y = menu_y(), h = menu_h();
    gfx_shadow(x, y, MENU_W, h, 14, 18, 120);
    gfx_fill_round_alpha(x, y, MENU_W, h, 14, RGB(28, 24, 46), 240);
    gfx_round_border(x, y, MENU_W, h, 14, RGB(90, 80, 130));
    gfx_image(x + 16, y + 12, &img_logo);
    gfx_text(x + 92, y + 22, OS_NAME " " OS_VERSION, RGB(255, 255, 255), &font_title);
    gfx_text(x + 92, y + 54, "Signed in as alice", RGB(190, 186, 214), &font_small);
    int iy = y + 92;
    for (int i = 0; i < APP_COUNT; i++) {
        if (hover == H_MENU + i) gfx_fill_round_alpha(x + 10, iy, MENU_W - 20, MENU_ITEM - 4, 8, RGB(255, 255, 255), 30);
        gfx_image(x + 22, iy + (MENU_ITEM - 4 - 20) / 2, app_icons_s[i]);
        gfx_text(x + 56, iy + (MENU_ITEM - 4 - font_ui.h) / 2, app_names[i], RGB(240, 240, 250), &font_ui);
        iy += MENU_ITEM;
    }
    int fy = y + h - 56;
    gfx_hline(x + 14, fy, MENU_W - 28, RGB(70, 64, 100));
    int bw = (MENU_W - 40) / 2;
    gfx_fill_round_alpha(x + 14, fy + 12, bw, 34, 8, RGB(255, 255, 255), hover == H_REBOOT ? 50 : 22);
    gfx_text_center(x + 14, fy + 12 + (34 - font_ui.h) / 2, bw, "Restart", RGB(255, 255, 255), &font_ui);
    gfx_fill_round(x + 26 + bw, fy + 12, bw, 34, 8, hover == H_OFF ? RGB(232, 60, 80) : RGB(196, 43, 64));
    gfx_text_center(x + 26 + bw, fy + 12 + (34 - font_ui.h) / 2, bw, "Shut down", RGB(255, 255, 255), &font_ui);
}

static void draw_toast(void) {
    if (!toast_until || ticks > toast_until) { toast_until = 0; return; }
    int w = 340, h = 76, x = scr_w - w - 16, y = 16;
    gfx_shadow(x, y, w, h, 12, 12, 90);
    gfx_fill_round_alpha(x, y, w, h, 12, RGB(30, 26, 48), 235);
    gfx_round_border(x, y, w, h, 12, RGB(100, 90, 150));
    gfx_image(x + 14, y + 16, &img_logo_s);
    gfx_text(x + 46, y + 14, toast_t, RGB(255, 255, 255), &font_ui_b);
    gfx_set_clip(x, y, w - 12, h);
    gfx_text(x + 46, y + 38, toast_m, RGB(200, 196, 224), &font_small);
    gfx_reset_clip();
}

static void compose(void) {
    gfx_reset_clip();
    draw_desktop();
    for (int i = 0; i < norder; i++) {
        window_t *w = &wins[order[i]];
        if (w->visible && !w->minimized) draw_window(order[i]);
    }
    gfx_reset_clip();
    draw_taskbar();
    if (menu_open) draw_menu();
    draw_toast();
}

/* ---------------------------------------------------------- hit tests */
static int in(int px, int py, int x, int y, int w, int h) { return px >= x && py >= y && px < x + w && py < y + h; }

static int window_at(int px, int py) {
    for (int i = norder - 1; i >= 0; i--) {
        window_t *w = &wins[order[i]];
        if (w->visible && !w->minimized && in(px, py, w->x, w->y, w->w, w->h)) return order[i];
    }
    return -1;
}

static int hover_at(int px, int py) {
    if (menu_open) {
        int x = menu_x(), y = menu_y(), h = menu_h();
        if (in(px, py, x, y, MENU_W, h)) {
            int iy = y + 92;
            for (int i = 0; i < APP_COUNT; i++, iy += MENU_ITEM)
                if (in(px, py, x + 10, iy, MENU_W - 20, MENU_ITEM - 4)) return H_MENU + i;
            int fy = y + h - 56, bw = (MENU_W - 40) / 2;
            if (in(px, py, x + 14, fy + 12, bw, 34)) return H_REBOOT;
            if (in(px, py, x + 26 + bw, fy + 12, bw, 34)) return H_OFF;
            return -1;
        }
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
        if (in(px, py, w->x + w->w - BTN_W, w->y, BTN_W, TITLE_H)) return H_CLOSE + wi;
        if (in(px, py, w->x + w->w - 2 * BTN_W, w->y, BTN_W, TITLE_H)) return H_MIN + wi;
        return -1;
    }
    for (int i = 0; i < APP_COUNT; i++) if (in(px, py, icon_x(i), icon_y(i), 92, 96)) return H_ICON + i;
    return -1;
}

/* ------------------------------------------------------------- events */
static void client_event(int wi, int ev) {
    window_t *w = &wins[wi];
    if (w->mouse) w->mouse(w, mx - (w->x + 1), my - (w->y + TITLE_H), ev);
}

static void mouse_down(void) {
    int h = hover_at(mx, my);
    if (menu_open) {
        int x = menu_x(), y = menu_y();
        if (in(mx, my, x, y, MENU_W, menu_h())) {
            if (h >= H_MENU && h < H_MENU + APP_COUNT) { menu_open = 0; gui_open(h - H_MENU); }
            else if (h == H_REBOOT) sys_reboot();
            else if (h == H_OFF) sys_shutdown();
            dirty = 1;
            return;
        }
        menu_open = 0; dirty = 1;
        if (h == H_START) return;
    }
    if (h == H_START) { menu_open = 1; dirty = 1; return; }
    if (h >= H_TASK && h < H_TASK + MAXWIN) {
        int i = h - H_TASK; window_t *w = &wins[i];
        if (w == top_window()) w->minimized = 1;
        else { w->minimized = 0; raise(i); }
        dirty = 1; return;
    }
    if (my >= scr_h - TASK_H) return;
    int wi = window_at(mx, my);
    if (wi >= 0) {
        window_t *w = &wins[wi];
        raise(wi); sel_icon = -1;
        if (h == H_CLOSE + wi) { gui_close(w); hover = -1; return; }
        if (h == H_MIN + wi) { w->minimized = 1; hover = -1; dirty = 1; return; }
        if (my < w->y + TITLE_H) { drag = wi; drag_dx = mx - w->x; drag_dy = my - w->y; return; }
        capture = wi; client_event(wi, EV_DOWN); dirty = 1;
        return;
    }
    /* desktop */
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
        int nx = mx - drag_dx, ny = my - drag_dy;
        if (ny < 0) ny = 0;
        if (ny > scr_h - TASK_H - TITLE_H) ny = scr_h - TASK_H - TITLE_H;
        if (nx < 80 - w->w) nx = 80 - w->w;
        if (nx > scr_w - 80) nx = scr_w - 80;
        if (nx != w->x || ny != w->y) { w->x = nx; w->y = ny; dirty = 1; }
        return;
    }
    if (capture >= 0) { client_event(capture, EV_MOVE); return; }
    int h = hover_at(mx, my);
    if (h != hover) { hover = h; dirty = 1; }
}

static void mouse_up(void) {
    if (capture >= 0) { client_event(capture, EV_UP); capture = -1; }
    drag = -1;
}

/* ------------------------------------------------------------- splash */
static void splash(void) {
    u32 start = ticks, dur = 170;
    while (ticks - start < dur) {
        int p = (int)((ticks - start) * 1000 / dur);
        gfx_reset_clip();
        gfx_vgradient(0, 0, scr_w, scr_h, RGB(14, 12, 36), RGB(44, 20, 74));
        int cx = scr_w / 2, cy = scr_h / 2 - 60;
        gfx_image(cx - 64, cy - 64, &img_logo_xl);
        gfx_text_center(0, cy + 84, scr_w, OS_NAME, RGB(255, 255, 255), &font_title);
        int bw = 220, bx = cx - bw / 2, by = cy + 136;
        gfx_fill_round(bx, by, bw, 6, 3, RGB(70, 60, 110));
        int fw = bw * p / 1000; if (fw < 6) fw = 6;
        gfx_fill_round(bx, by, fw, 6, 3, RGB(140, 200, 255));
        const char *msg = p < 300 ? "Starting kernel..." : p < 600 ? "Loading drivers..." : "Preparing desktop...";
        gfx_text_center(0, by + 22, scr_w, msg, RGB(180, 176, 210), &font_small);
        gfx_text_center(0, scr_h - 40, scr_w, "Written from scratch in C and assembly", RGB(120, 116, 160), &font_small);
        gfx_present();
        __asm__ volatile("hlt");
    }
}

/* --------------------------------------------------------------- loop */
void gui_run(void) {
    mx = scr_w / 2; my = scr_h / 2;
    splash();
    gui_toast("Welcome to AliceOS 2.0", "Double-click an icon, or open the start menu.");
    int cur_x = -1, cur_y = -1, last_min = -1;
    u32 last_tick = ticks;
    for (;;) {
        int k;
        while ((k = key_get()) >= 0) {
            if (menu_open && k == 27) { menu_open = 0; dirty = 1; continue; }
            window_t *w = top_window();
            if (w && w->key) w->key(w, k);
        }
        mouse_pkt_t p;
        while (mouse_get(&p)) {
            mx += p.dx; my += p.dy;
            if (mx < 0) mx = 0;
            if (my < 0) my = 0;
            if (mx >= scr_w) mx = scr_w - 1;
            if (my >= scr_h) my = scr_h - 1;
            int b = p.buttons & 1;
            mouse_move();
            if (b && !mbtn) mouse_down();
            if (!b && mbtn) mouse_up();
            mbtn = b;
        }
        if (ticks != last_tick) {
            last_tick = ticks;
            for (int i = 0; i < MAXWIN; i++)
                if (wins[i].visible && wins[i].tick) wins[i].tick(&wins[i]);
            datetime_t t; rtc_read(&t);
            if (t.min != last_min) { last_min = t.min; dirty = 1; }
            if (toast_until && ticks > toast_until) dirty = 1;
        }
        if (dirty) {
            dirty = 0;
            compose();
            gfx_present();
            gfx_cursor(mx, my); cur_x = mx; cur_y = my;
        } else if (mx != cur_x || my != cur_y) {
            gfx_cursor_restore(cur_x, cur_y);
            gfx_cursor(mx, my); cur_x = mx; cur_y = my;
        }
        __asm__ volatile("hlt");
    }
}
