#ifndef OS_H
#define OS_H
#include "types.h"
#include "assets.h"

#define OS_NAME    "AliceOS"
#define OS_VERSION "2.0"

/* ---------------- kernel / hardware (kernel.c) */
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline void outw(u16 p, u16 v) { __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }

#define HZ 100
extern volatile u32 ticks;
extern u32 mem_total_kb;
extern char cpu_vendor[13];
extern char cpu_brand[49];

typedef struct { int sec, min, hour, day, mon, year, wday; } datetime_t;
void rtc_read(datetime_t *t);

/* keyboard: ASCII, or special codes below */
#define K_UP    0x80
#define K_DOWN  0x81
#define K_LEFT  0x82
#define K_RIGHT 0x83
#define K_HOME  0x84
#define K_END   0x85
#define K_DEL   0x86
int key_get(void);                /* -1 if none */

/* mouse events */
typedef struct { int dx, dy; u8 buttons; } mouse_pkt_t;
int mouse_get(mouse_pkt_t *p);    /* 0 if none */

void sys_reboot(void);
void sys_shutdown(void);
u32 rand32(void);

/* ---------------- libc-ish */
void *memset(void *d, int c, u32 n);
void *memcpy(void *d, const void *s, u32 n);
int strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, int n);
void strcpy(char *d, const char *s);
void strcat(char *d, const char *s);
void itoa(s64 v, char *out);
void fmt2(int v, char *out);      /* 2-digit, zero padded */

/* ---------------- graphics (gfx.c) */
extern int scr_w, scr_h;
#define RGB(r,g,b) (((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

int  gfx_init(u32 fb_addr, u32 pitch, u32 w, u32 h, u32 bpp);
void gfx_set_clip(int x, int y, int w, int h);
void gfx_reset_clip(void);
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
void gfx_wallpaper(void);
void gfx_blit(int x, int y, int w, int h, const u32 *px);
int  gfx_text(int x, int y, const char *s, u32 c, const font_t *f);
int  gfx_text_w(const char *s, const font_t *f);
void gfx_text_center(int x, int y, int w, const char *s, u32 c, const font_t *f);
void gfx_char(int x, int y, char ch, u32 c, const font_t *f);
void gfx_present(void);
void gfx_present_rect(int x, int y, int w, int h);
void gfx_cursor(int x, int y);
void gfx_cursor_restore(int x, int y);

/* ---------------- GUI (gui.c) */
typedef struct window window_t;
struct window {
    int x, y, w, h;               /* outer frame */
    char title[40];
    const image_t *icon;
    int visible, minimized, app;
    u32 bg;                       /* client background colour */
    void (*draw)(window_t *w, int cx, int cy, int cw, int ch);
    void (*mouse)(window_t *w, int x, int y, int ev);   /* client-relative */
    void (*key)(window_t *w, int k);
    void (*tick)(window_t *w);
    void *state;
};
#define TITLE_H 36
#define EV_DOWN 1
#define EV_MOVE 2
#define EV_UP   3

window_t *gui_open(int app);
void gui_close(window_t *w);
void gui_dirty(void);
int  gui_focused(window_t *w);
void gui_run(void);
void gui_toast(const char *title, const char *text);

/* ---------------- apps (apps.c) */
enum { APP_TERMINAL, APP_CALC, APP_NOTES, APP_PAINT, APP_SNAKE, APP_ABOUT, APP_COUNT };
extern const char *app_names[APP_COUNT];
extern const image_t *app_icons[APP_COUNT];
extern const image_t *app_icons_s[APP_COUNT];
void app_setup(window_t *w, int app);

#endif
