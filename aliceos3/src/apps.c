/* AliceOS 3 - application table, file opening, About. */
#include "os.h"

const char *app_names[APP_COUNT] = { "Files", "Terminal", "Notes", "Paint", "Calculator", "Snake",
                                     "Task Manager", "Settings", "About" };
const image_t *app_icons[APP_COUNT] = { &ic_files, &ic_terminal, &ic_notes, &ic_paint, &ic_calc, &ic_snake,
                                        &ic_taskmgr, &ic_settings, &ic_about };
const image_t *app_icons_s[APP_COUNT] = { &ic_files_s, &ic_terminal_s, &ic_notes_s, &ic_paint_s, &ic_calc_s,
                                          &ic_snake_s, &ic_taskmgr_s, &ic_settings_s, &ic_about_s };
const int app_multi[APP_COUNT] = { 1, 1, 1, 0, 0, 0, 0, 0, 0 };

void app_setup(window_t *w, int app) {
    strcpy(w->title, app_names[app]);
    w->icon = app_icons_s[app];
    switch (app) {
        case APP_FILES:    files_setup(w); break;
        case APP_TERMINAL: term_setup(w); break;
        case APP_NOTES:    notes_setup(w); break;
        case APP_PAINT:    paint_setup(w); break;
        case APP_CALC:     calc_setup(w); break;
        case APP_SNAKE:    snake_setup(w); break;
        case APP_TASKMGR:  taskmgr_setup(w); break;
        case APP_SETTINGS: settings_setup(w); break;
        case APP_ABOUT:    about_setup(w); break;
    }
}

void open_node(fnode_t *n) {
    if (!n) return;
    switch (fs_kind(n)) {
        case FK_DIR: { window_t *w = gui_new(APP_FILES); if (w) files_set_dir(w, n); break; }
        case FK_IMAGE: paint_open(n); break;
        case FK_PROG: {
            window_t *w = gui_new(APP_TERMINAL);
            if (w) gui_toast(n->name, "This is a built-in command: type it in the Terminal.");
            break;
        }
        default: notes_open(n); break;
    }
}

/* ====================================================================== ABOUT */
static void about_row(int x, int y, const char *k, const char *v) {
    gfx_text(x, y, k, th.subtext, &font_ui);
    gfx_text_fit(x + 120, y, 330, v, th.text, &font_ui);
}

static void uptime_text(char *o) {
    u32 s = ticks / HZ; char b[16];
    itoa(s / 3600, b); strcpy(o, b); strcat(o, "h ");
    itoa(s / 60 % 60, b); strcat(o, b); strcat(o, "m ");
    itoa(s % 60, b); strcat(o, b); strcat(o, "s");
}

static void about_draw(window_t *w, int cx, int cy, int cw, int ch) {
    (void)w; (void)ch;
    gfx_vgradient(cx, cy, cw, 120, gfx_mix(th.accent, 0, 120), th.accent);
    gfx_image(cx + 28, cy + 28, &img_logo);
    gfx_text(cx + 112, cy + 32, OS_NAME " " OS_VERSION, RGB(255, 255, 255), &font_title);
    gfx_text(cx + 112, cy + 66, "The best of Windows and Linux, built from scratch", RGB(235, 230, 255), &font_ui);
    char b[64], n[16];
    int x = cx + 32, y = cy + 144;
    about_row(x, y, "Kernel", "32-bit x86, written from scratch"); y += 28;
    about_row(x, y, "Processor", cpu_brand); y += 28;
    fmt_size(heap_used(), b); strcat(b, " used of "); fmt_size(mem_total_kb * 1024, n); strcat(b, n);
    about_row(x, y, "Memory", b); y += 28;
    itoa(scr_w, n); strcpy(b, n); strcat(b, " x "); itoa(scr_h, n); strcat(b, n); strcat(b, ", 32-bit colour");
    about_row(x, y, "Display", b); y += 28;
    itoa(fs_count(), n); strcpy(b, n); strcat(b, " files and folders, "); fmt_size(fs_usage(), n); strcat(b, n);
    about_row(x, y, "Storage", b); y += 28;
    uptime_text(b); about_row(x, y, "Uptime", b); y += 28;
    about_row(x, y, "Created by", "Alice"); y += 40;
    gfx_text(x, y, "C + x86 assembly. No Linux, no libraries.", th.accent, &font_ui_b);
}
static void about_tick(window_t *w) { if (!w->minimized && ticks % HZ == 0) gui_dirty(); }

void about_setup(window_t *w) {
    w->w = 540; w->h = 470;
    w->draw = about_draw; w->tick = about_tick;
}
