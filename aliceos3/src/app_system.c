/* AliceOS 3 - Task Manager and Settings. */
#include "os.h"

/* ================================================================ TASK MANAGER */
static int tm_sel = -1;
typedef struct { int pid; char name[48]; const char *state; u32 mem; window_t *w; } proc_t;

static int tm_procs(proc_t *p) {
    int n = 0;
    p[n].pid = 1; strcpy(p[n].name, "kernel"); p[n].state = "running"; p[n].mem = kernel_image_size(); p[n++].w = NULL;
    p[n].pid = 2; strcpy(p[n].name, "window-manager"); p[n].state = "running"; p[n].mem = 0; p[n++].w = NULL;
    p[n].pid = 3; strcpy(p[n].name, "ramfs (file system)"); p[n].state = "running"; p[n].mem = fs_usage(); p[n++].w = NULL;
    window_t *ws[16]; int c = gui_windows(ws, 16);
    for (int i = 0; i < c; i++) {
        p[n].pid = ws[i]->pid; strncpy0(p[n].name, ws[i]->title, 48);
        p[n].state = ws[i]->minimized ? "minimised" : gui_focused(ws[i]) ? "focused" : "running";
        p[n].mem = ws[i]->mem; p[n].w = ws[i]; n++;
    }
    return n;
}

static void chart(int x, int y, int w, int h, u32 *hist, u32 col) {
    gfx_fill_round(x, y, w, h, 8, th.input_bg);
    for (int i = 1; i < 4; i++) gfx_hline(x + 1, y + h * i / 4, w - 2, th.line);
    int px = -1, py = -1;
    for (int i = 0; i < 60 && i < hist_n; i++) {
        int xx = x + w - 2 - i * (w - 4) / 59;
        int v = (int)hist[i]; if (v > 1000) v = 1000;
        int yy = y + h - 2 - v * (h - 4) / 1000;
        gfx_fill_alpha(xx - (w - 4) / 59, yy, (w - 4) / 59 + 1, y + h - 2 - yy, col, 50);
        if (px >= 0) { gfx_line(px, py, xx, yy, col); gfx_line(px, py - 1, xx, yy - 1, col); }
        px = xx; py = yy;
    }
    gfx_round_border(x, y, w, h, 8, th.line);
}

static void tm_draw(window_t *w, int cx, int cy, int cw, int ch) {
    (void)w;
    int cardw = (cw - 48) / 2;
    char b[64], s[16];
    /* CPU */
    int x = cx + 16, y = cy + 14;
    gfx_fill_round(x, y, cardw, 158, 12, th.panel);
    gfx_text(x + 16, y + 12, "CPU", th.subtext, &font_ui_b);
    itoa(cpu_busy_pct_x10 / 10, b); strcat(b, "."); itoa(cpu_busy_pct_x10 % 10, s); strcat(b, s); strcat(b, " %");
    gfx_text(x + cardw - 16 - gfx_text_w(b, &font_title), y + 6, b, th.text, &font_title);
    chart(x + 14, y + 46, cardw - 28, 98, cpu_hist, th.accent);
    /* Memory */
    x = cx + 32 + cardw;
    gfx_fill_round(x, y, cardw, 158, 12, th.panel);
    gfx_text(x + 16, y + 12, "Memory", th.subtext, &font_ui_b);
    fmt_size(heap_used(), b); strcat(b, " / "); fmt_size(heap_total(), s); strcat(b, s);
    gfx_text(x + cardw - 16 - gfx_text_w(b, &font_ui_b), y + 14, b, th.text, &font_ui_b);
    chart(x + 14, y + 46, cardw - 28, 98, mem_hist, RGB(20, 184, 166));

    /* process table */
    proc_t p[20]; int n = tm_procs(p);
    int ty = cy + 188;
    gfx_text(cx + 16, ty, "Processes", th.text, &font_ui_b);
    char cnt[24]; itoa(n, cnt); strcat(cnt, " running");
    gfx_text(cx + 120, ty + 1, cnt, th.subtext, &font_small);
    ty += 28;
    gfx_fill(cx + 16, ty, cw - 32, 28, th.panel2);
    int c1 = cx + 28, c2 = cx + 90, c3 = cx + cw - 300, c4 = cx + cw - 150;
    gfx_text(c1, ty + 7, "PID", th.subtext, &font_small); gfx_text(c2, ty + 7, "Name", th.subtext, &font_small);
    gfx_text(c3, ty + 7, "Status", th.subtext, &font_small); gfx_text(c4, ty + 7, "Memory", th.subtext, &font_small);
    ty += 30;
    int maxrows = (cy + ch - 60 - ty) / 30;
    for (int i = 0; i < n && i < maxrows; i++, ty += 30) {
        if (i == tm_sel) gfx_fill_round(cx + 16, ty, cw - 32, 28, 6, th.accent_soft);
        itoa(p[i].pid, b); gfx_text(c1, ty + 6, b, th.text, &font_ui);
        if (p[i].w && p[i].w->icon) gfx_image(c2, ty + 4, p[i].w->icon);
        gfx_text_fit(c2 + (p[i].w ? 28 : 0), ty + 6, c3 - c2 - 40, p[i].name, th.text, &font_ui);
        gfx_text(c3, ty + 6, p[i].state, p[i].w ? th.text : th.subtext, &font_ui);
        if (p[i].mem) { fmt_size(p[i].mem, b); gfx_text(c4, ty + 6, b, th.text, &font_ui); }
        else gfx_text(c4, ty + 6, "-", th.subtext, &font_ui);
    }
    /* footer */
    int fy = cy + ch - 48;
    gfx_hline(cx, fy - 6, cw, th.line);
    char up[48] = "Uptime "; char u[16]; u32 sec = ticks / HZ;
    itoa(sec / 3600, u); strcat(up, u); strcat(up, "h "); itoa(sec / 60 % 60, u); strcat(up, u); strcat(up, "m "); itoa(sec % 60, u); strcat(up, u); strcat(up, "s");
    gfx_text(cx + 16, fy + 8, up, th.subtext, &font_small);
    int can = tm_sel >= 0 && tm_sel < n && p[tm_sel].w;
    gfx_fill_round(cx + cw - 136, fy, 120, 34, 8, can ? RGB(220, 50, 70) : th.panel2);
    gfx_text_center(cx + cw - 136, fy + (34 - font_ui.h) / 2, 120, "End task", can ? RGB(255, 255, 255) : th.subtext, &font_ui);
}

static void tm_mouse(window_t *w, int x, int y, int ev) {
    if (ev != EV_DOWN) return;
    int cw = gui_client_w(w), ch = gui_client_h(w);
    proc_t p[20]; int n = tm_procs(p);
    if (y >= ch - 48 && x >= cw - 136 && x < cw - 16) {
        if (tm_sel >= 0 && tm_sel < n && p[tm_sel].w) {
            if (p[tm_sel].w == w) return;
            char m[80]; strcpy(m, p[tm_sel].name); strcat(m, " was closed");
            gui_close(p[tm_sel].w); gui_toast("Task ended", m); tm_sel = -1;
        } else if (tm_sel >= 0) gui_toast("Not allowed", "System processes cannot be ended.");
        gui_dirty(); return;
    }
    int ty = 188 + 28 + 30;
    if (y >= ty) { int r = (y - ty) / 30; tm_sel = r < n ? r : -1; gui_dirty(); }
}
static void tm_tick(window_t *w) { if (!w->minimized && ticks % HZ == 5) gui_dirty(); }

void taskmgr_setup(window_t *w) {
    w->w = 720; w->h = 560; w->resizable = 1; w->min_w = 600; w->min_h = 420;
    w->draw = tm_draw; w->mouse = tm_mouse; w->tick = tm_tick;
    tm_sel = -1;
}

/* ===================================================================== SETTINGS */
static int st_page = 0;
static const char *st_pages[] = { "Personalisation", "Date & time", "System" };
#define ST_SIDE 196

static void st_save(void) {
    char b[128], n[8];
    strcpy(b, "theme="); strcat(b, th.dark ? "dark" : "light");
    strcat(b, "\naccent="); itoa(set_accent, n); strcat(b, n);
    strcat(b, "\nwallpaper="); strcat(b, wallpaper_names[set_wallpaper]);
    strcat(b, "\ntimezone=UTC"); if (tz_offset >= 0) strcat(b, "+"); itoa(tz_offset, n); strcat(b, n); strcat(b, "\n");
    fnode_t *f; if (!fs_create("~/.settings", NULL, &f)) fs_write(f, b, strlen(b));
}

static void seg(int x, int y, int w, const char *s, int on) {
    gfx_fill_round(x, y, w, 34, 8, on ? th.accent : th.panel2);
    gfx_text_center(x, y + (34 - font_ui.h) / 2, w, s, on ? RGB(255, 255, 255) : th.text, &font_ui);
}

static void st_draw(window_t *w, int cx, int cy, int cw, int ch) {
    (void)w;
    gfx_fill(cx, cy, ST_SIDE, ch, th.panel);
    gfx_vline(cx + ST_SIDE - 1, cy, ch, th.line);
    gfx_image(cx + 16, cy + 16, &ic_settings_s);
    gfx_text(cx + 46, cy + 17, "Settings", th.text, &font_ui_b);
    for (int i = 0; i < 3; i++) {
        int y = cy + 56 + i * 40;
        if (i == st_page) { gfx_fill_round(cx + 10, y, ST_SIDE - 20, 34, 8, th.accent_soft); gfx_fill_round(cx + 10, y + 9, 3, 16, 1, th.accent); }
        gfx_text(cx + 26, y + (34 - font_ui.h) / 2, st_pages[i], th.text, &font_ui);
    }
    int x = cx + ST_SIDE + 28, y = cy + 22;
    gfx_text(x, y, st_pages[st_page], th.text, &font_title); y += 48;
    char b[96], n[24];
    if (st_page == 0) {
        gfx_text(x, y, "Wallpaper", th.subtext, &font_ui_b); y += 26;
        for (int i = 0; i < 4; i++) {
            int tx = x + i * 128, tw = 118, thh = 88;
            if (i == set_wallpaper) gfx_fill_round(tx - 4, y - 4, tw + 8, thh + 8, 10, th.accent);
            gfx_image_scaled(tx, y, tw, thh, wallpapers[i]);
            gfx_text_center(tx, y + thh + 8, tw, wallpaper_names[i], th.text, &font_small);
        }
        y += 132;
        gfx_text(x, y, "Theme", th.subtext, &font_ui_b); y += 26;
        seg(x, y, 110, "Light", !th.dark); seg(x + 118, y, 110, "Dark", th.dark);
        y += 60;
        gfx_text(x, y, "Accent colour", th.subtext, &font_ui_b); y += 28;
        for (int i = 0; i < 6; i++) {
            if (i == set_accent) gfx_circle_fill(x + 16 + i * 44, y + 16, 19, th.text);
            gfx_circle_fill(x + 16 + i * 44, y + 16, 15, accent_colors[i]);
        }
    } else if (st_page == 1) {
        datetime_t t; clock_now(&t);
        fmt2(t.hour, b); strcat(b, ":"); fmt2(t.min, n); strcat(b, n); strcat(b, ":"); fmt2(t.sec, n); strcat(b, n);
        gfx_text(x, y, b, th.text, &font_huge); y += 80;
        static const char *wd[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
        static const char *mo[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
        strcpy(b, wd[t.wday]); strcat(b, ", "); itoa(t.day, n); strcat(b, n); strcat(b, " "); strcat(b, mo[t.mon - 1]);
        strcat(b, " "); itoa(t.year, n); strcat(b, n);
        gfx_text(x, y, b, th.subtext, &font_ui); y += 50;
        gfx_text(x, y, "Time zone", th.subtext, &font_ui_b); y += 28;
        seg(x, y, 40, "-", 0);
        strcpy(b, "UTC"); if (tz_offset >= 0) strcat(b, "+"); itoa(tz_offset, n); strcat(b, n);
        gfx_fill_round(x + 48, y, 120, 34, 8, th.input_bg); gfx_round_border(x + 48, y, 120, 34, 8, th.line);
        gfx_text_center(x + 48, y + (34 - font_ui.h) / 2, 120, b, th.text, &font_ui);
        seg(x + 176, y, 40, "+", 0);
        y += 52;
        gfx_text(x, y, "Morocco is UTC+1 most of the year (UTC+0 during Ramadan).", th.subtext, &font_small); y += 20;
        gfx_text(x, y, "The hardware clock keeps UTC; AliceOS adds the offset.", th.subtext, &font_small);
    } else {
        struct { const char *k; char v[80]; } rows[7];
        rows[0].k = "Operating system"; strcpy(rows[0].v, OS_NAME " " OS_VERSION " (i386, from scratch)");
        rows[1].k = "Processor"; strncpy0(rows[1].v, cpu_brand, 60);
        rows[2].k = "Memory"; fmt_size(heap_used(), rows[2].v); strcat(rows[2].v, " used of "); fmt_size(mem_total_kb * 1024, n); strcat(rows[2].v, n);
        rows[3].k = "Display"; itoa(scr_w, rows[3].v); strcat(rows[3].v, " x "); itoa(scr_h, n); strcat(rows[3].v, n);
        rows[4].k = "Storage"; strcpy(rows[4].v, "ramfs, "); fmt_size(fs_usage(), n); strcat(rows[4].v, n); strcat(rows[4].v, " in "); itoa(fs_count(), n); strcat(rows[4].v, n); strcat(rows[4].v, " items");
        rows[5].k = "Kernel image"; fmt_size(kernel_image_size(), rows[5].v);
        rows[6].k = "Uptime"; u32 s = ticks / HZ; itoa(s / 60, rows[6].v); strcat(rows[6].v, " min "); itoa(s % 60, n); strcat(rows[6].v, n); strcat(rows[6].v, " s");
        for (int i = 0; i < 7; i++, y += 32) {
            gfx_text(x, y, rows[i].k, th.subtext, &font_ui);
            gfx_text_fit(x + 150, y, cw - ST_SIDE - 200, rows[i].v, th.text, &font_ui);
        }
        y += 16;
        seg(x, y, 120, "Restart", 0);
        gfx_fill_round(x + 130, y, 120, 34, 8, RGB(200, 45, 66));
        gfx_text_center(x + 130, y + (34 - font_ui.h) / 2, 120, "Shut down", RGB(255, 255, 255), &font_ui);
    }
    (void)ch;
}

static void st_mouse(window_t *w, int x, int y, int ev) {
    (void)w;
    if (ev != EV_DOWN) return;
    if (x < ST_SIDE) {
        for (int i = 0; i < 3; i++) if (y >= 56 + i * 40 && y < 90 + i * 40) { st_page = i; gui_dirty(); }
        return;
    }
    int bx = ST_SIDE + 28, by = 22 + 48;
    if (st_page == 0) {
        int ty = by + 26;
        for (int i = 0; i < 4; i++)
            if (x >= bx + i * 128 && x < bx + i * 128 + 118 && y >= ty && y < ty + 88 && i != set_wallpaper) {
                set_wallpaper = i; gfx_set_wallpaper(wallpapers[i]); st_save(); gui_dirty(); return;
            }
        int thy = ty + 132 + 26;
        if (y >= thy && y < thy + 34) {
            if (x >= bx && x < bx + 110) theme_apply(0, set_accent);
            else if (x >= bx + 118 && x < bx + 228) theme_apply(1, set_accent);
            st_save(); gui_dirty(); return;
        }
        int ay = thy + 60 + 28;
        for (int i = 0; i < 6; i++) {
            int dx = x - (bx + 16 + i * 44), dy = y - (ay + 16);
            if (dx * dx + dy * dy <= 18 * 18) { theme_apply(th.dark, i); st_save(); gui_dirty(); return; }
        }
    } else if (st_page == 1) {
        int ty = by + 80 + 50 + 28;
        if (y >= ty && y < ty + 34) {
            if (x >= bx && x < bx + 40 && tz_offset > -12) tz_offset--;
            else if (x >= bx + 176 && x < bx + 216 && tz_offset < 14) tz_offset++;
            st_save(); gui_dirty();
        }
    } else {
        int ty = by + 7 * 32 + 16;
        if (y >= ty && y < ty + 34) {
            if (x >= bx && x < bx + 120) sys_reboot();
            else if (x >= bx + 130 && x < bx + 250) sys_shutdown();
        }
    }
}
static void st_tick(window_t *w) { if (!w->minimized && st_page != 0 && ticks % HZ == 0) gui_dirty(); }

void settings_setup(window_t *w) {
    w->w = 780; w->h = 520;
    w->draw = st_draw; w->mouse = st_mouse; w->tick = st_tick;
}
