#ifndef OS_H
#define OS_H
#include "types.h"
#include "assets.h"

#define OS_NAME    "AliceOS"
#define OS_VERSION "3.0"

/* ================================================================ kernel */
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline void outw(u16 p, u16 v) { __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline u64 rdtsc(void) { u32 lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((u64)hi << 32) | lo; }

#define HZ 100
extern volatile u32 ticks;
extern u32 mem_total_kb;
extern char cpu_vendor[13];
extern char cpu_brand[49];

typedef struct { int sec, min, hour, day, mon, year, wday; } datetime_t;
void rtc_read(datetime_t *t);         /* hardware clock (UTC) */
void clock_now(datetime_t *t);        /* local time (timezone applied) */
extern int tz_offset;                 /* hours */

/* keyboard: ASCII, or the special codes below */
#define K_UP     0x80
#define K_DOWN   0x81
#define K_LEFT   0x82
#define K_RIGHT  0x83
#define K_HOME   0x84
#define K_END    0x85
#define K_DEL    0x86
#define K_F2     0x87
#define K_PGUP   0x88
#define K_PGDN   0x89
#define K_ALTTAB 0x90
#define K_SUPER  0x91
#define K_CTRL(c) (0x200 | (c))       /* Ctrl + lowercase letter */
int key_get(void);                    /* -1 if none */
int kbd_alt(void);

typedef struct { int dx, dy, dz; u8 buttons; } mouse_pkt_t;
int mouse_get(mouse_pkt_t *p);

void sys_reboot(void);
void sys_shutdown(void);
u32 rand32(void);

/* ================================================================ memory */
void  heap_init(void);
void *kmalloc(u32 n);
void *kzalloc(u32 n);
void *krealloc(void *p, u32 n);
void  kfree(void *p);
u32   heap_used(void);
u32   heap_total(void);
u32   kernel_image_size(void);

/* ================================================================ libc-ish */
void *memset(void *d, int c, u32 n);
void *memcpy(void *d, const void *s, u32 n);
void *memmove(void *d, const void *s, u32 n);
int strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, int n);
void strcpy(char *d, const char *s);
void strncpy0(char *d, const char *s, int max);    /* always terminates */
void strcat(char *d, const char *s);
char *strrchr(const char *s, int c);
int  ends_with(const char *s, const char *suffix);
void itoa(s64 v, char *out);
void fmt2(int v, char *out);
void fmt_size(u32 bytes, char *out);
void fmt_datetime(const datetime_t *t, char *out); /* dd/mm/yyyy hh:mm */

/* ================================================================ file system */
#define FT_DIR  1
#define FT_FILE 2
#define OWN_ROOT  0
#define OWN_ALICE 1
typedef struct fnode fnode_t;
struct fnode {
    char name[48];
    u8 type, owner;
    fnode_t *parent, *child, *next;
    u8 *data; u32 size, cap;
    datetime_t mtime;
};
enum { FS_OK = 0, FS_ENOENT = -1, FS_EEXIST = -2, FS_EPERM = -3, FS_ENOTDIR = -4, FS_EISDIR = -5,
       FS_ENOTEMPTY = -6, FS_EINVAL = -7, FS_ENOMEM = -8 };
extern fnode_t *fs_root, *fs_home;
void fs_init(void);
fnode_t *fs_resolve(const char *path, fnode_t *cwd);
fnode_t *fs_child(fnode_t *dir, const char *name);
int  fs_mkdir(const char *path, fnode_t *cwd, fnode_t **out);
int  fs_create(const char *path, fnode_t *cwd, fnode_t **out);  /* existing file is reused */
int  fs_write(fnode_t *f, const void *data, u32 len);
int  fs_append(fnode_t *f, const void *data, u32 len);
int  fs_remove(fnode_t *n, int recursive);
int  fs_move(fnode_t *n, const char *dest, fnode_t *cwd);
int  fs_copy(fnode_t *n, const char *dest, fnode_t *cwd);
int  fs_rename(fnode_t *n, const char *newname);
int  fs_can_write(fnode_t *dir);
void fs_path(fnode_t *n, char *out, int max);
void fs_pretty_path(fnode_t *n, char *out, int max);        /* /home/alice -> ~ */
int  fs_list(fnode_t *dir, fnode_t **out, int max);         /* dirs first, by name */
int  fs_is_ancestor(fnode_t *a, fnode_t *b);
int  fs_alive(fnode_t *n);
u32  fs_usage(void);
u32  fs_count(void);
const char *fs_strerror(int e);
int  fs_valid_name(const char *s);
enum { FK_DIR, FK_TEXT, FK_IMAGE, FK_PROG, FK_OTHER };
int  fs_kind(fnode_t *n);

/* ================================================================ graphics */
extern int scr_w, scr_h;
#define RGB(r,g,b) (((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

int  gfx_init(u32 fb_addr, u32 pitch, u32 w, u32 h, u32 bpp);
void gfx_set_wallpaper(const image_t *img);
void gfx_set_clip(int x, int y, int w, int h);
void gfx_reset_clip(void);
void gfx_get_clip(int *x0, int *y0, int *x1, int *y1);
void gfx_clip_intersect(int x, int y, int w, int h);
void gfx_pixel(int x, int y, u32 c);
u32  gfx_get(int x, int y);
void gfx_fill(int x, int y, int w, int h, u32 c);
void gfx_fill_alpha(int x, int y, int w, int h, u32 c, int a);
void gfx_fill_round(int x, int y, int w, int h, int r, u32 c);
void gfx_fill_round_alpha(int x, int y, int w, int h, int r, u32 c, int a);
void gfx_rect(int x, int y, int w, int h, u32 c);
void gfx_round_border(int x, int y, int w, int h, int r, u32 c);
void gfx_hline(int x, int y, int w, u32 c);
void gfx_vline(int x, int y, int h, u32 c);
void gfx_line(int x0, int y0, int x1, int y1, u32 c);
void gfx_circle_fill(int cx, int cy, int r, u32 c);
void gfx_vgradient(int x, int y, int w, int h, u32 top, u32 bottom);
void gfx_shadow(int x, int y, int w, int h, int r, int size, int strength);
void gfx_image(int x, int y, const image_t *img);
void gfx_image_scaled(int x, int y, int w, int h, const image_t *img);
void gfx_wallpaper(void);
void gfx_blit(int x, int y, int w, int h, const u32 *px);
int  gfx_text(int x, int y, const char *s, u32 c, const font_t *f);
int  gfx_text_w(const char *s, const font_t *f);
void gfx_text_center(int x, int y, int w, const char *s, u32 c, const font_t *f);
void gfx_text_fit(int x, int y, int maxw, const char *s, u32 c, const font_t *f);  /* adds "..." */
void gfx_char(int x, int y, char ch, u32 c, const font_t *f);
void gfx_present(void);
void gfx_present_rect(int x, int y, int w, int h);
void gfx_cursor(int x, int y, int shape);
void gfx_cursor_restore(int x, int y);
u32  gfx_mix(u32 a, u32 b, int t);   /* t 0..256 */

/* ================================================================ theme */
typedef struct {
    int dark;
    u32 accent, accent_soft;
    u32 win_bg, title_bg, title_bg_inactive, title_fg, title_fg_inactive, border, border_inactive;
    u32 text, subtext, panel, panel2, line, hover, select, input_bg;
} theme_t;
extern theme_t th;
extern int set_wallpaper, set_accent;
extern const u32 accent_colors[6];
extern const image_t *wallpapers[4];
extern const char *wallpaper_names[4];
void theme_apply(int dark, int accent);

/* ================================================================ GUI */
typedef struct window window_t;
struct window {
    int x, y, w, h;
    int rx, ry, rw, rh;               /* restore rect when maximised */
    int maximized, resizable, min_w, min_h;
    char title[64];
    const image_t *icon;
    int visible, minimized, app, pid;
    u32 bg;
    void (*draw)(window_t *w, int cx, int cy, int cw, int ch);
    void (*mouse)(window_t *w, int x, int y, int ev);
    void (*key)(window_t *w, int k);
    void (*tick)(window_t *w);
    void (*close)(window_t *w);       /* free state */
    int  (*can_close)(window_t *w);
    void *state; u32 mem;
    /* input dialog */
    int pr_active; char pr_title[64], pr_buf[64]; int pr_len;
    void (*pr_done)(window_t *w, const char *text);
};
#define TITLE_H 36
#define EV_DOWN   1
#define EV_MOVE   2
#define EV_UP     3
#define EV_RDOWN  4
#define EV_DBL    5
#define EV_WHEEL  6                   /* amount in ev_wheel: +down / -up */
extern int ev_wheel;

window_t *gui_open(int app);
window_t *gui_new(int app);           /* always a new instance */
window_t *gui_find(int app);
void gui_close(window_t *w);
void gui_dirty(void);
int  gui_focused(window_t *w);
void gui_focus(window_t *w);
void gui_run(void);
void gui_toast(const char *title, const char *text);
void gui_prompt(window_t *w, const char *title, const char *initial, void (*done)(window_t *, const char *));
typedef void (*menu_cb)(window_t *w, int item, void *ctx);
void gui_menu(int x, int y, const char **items, int n, menu_cb cb, window_t *w, void *ctx);
int  gui_windows(window_t **out, int max);
int  gui_client_w(window_t *w);
int  gui_client_h(window_t *w);
extern u64 cpu_busy_pct_x10;           /* CPU use, tenths of a percent */
extern u32 cpu_hist[60]; extern u32 mem_hist[60]; extern int hist_n;

/* ================================================================ apps */
enum { APP_FILES, APP_TERMINAL, APP_NOTES, APP_PAINT, APP_CALC, APP_SNAKE, APP_TASKMGR, APP_SETTINGS, APP_ABOUT, APP_COUNT };
extern const char *app_names[APP_COUNT];
extern const image_t *app_icons[APP_COUNT];
extern const image_t *app_icons_s[APP_COUNT];
extern const int app_multi[APP_COUNT];
void app_setup(window_t *w, int app);

/* per-app entry points used by other apps */
void term_setup(window_t *w);
void term_set_cwd(window_t *w, fnode_t *dir);
void files_setup(window_t *w);
void files_set_dir(window_t *w, fnode_t *dir);
void notes_setup(window_t *w);
window_t *notes_open(fnode_t *f);
void paint_setup(window_t *w);
window_t *paint_open(fnode_t *f);
void calc_setup(window_t *w);
void snake_setup(window_t *w);
void taskmgr_setup(window_t *w);
void settings_setup(window_t *w);
void about_setup(window_t *w);
void open_node(fnode_t *n);           /* open a file with the right app */

#endif
