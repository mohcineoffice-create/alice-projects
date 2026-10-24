/* AliceOS 2 - graphics: double-buffered framebuffer, anti-aliased shapes and text, mouse cursor. */
#include "os.h"

#define MAXW 1920
#define MAXH 1200

int scr_w, scr_h;
static u8 *fb;
static u32 fb_pitch, fb_bpp;
static u32 back[MAXW * MAXH];
static u32 wall[MAXW * MAXH];
static int cx0, cy0, cx1, cy1;      /* clip rectangle, exclusive end */

static inline u32 blend(u32 d, u32 s, int a) {          /* a: 0..256 */
    u32 rb = (((s & 0xFF00FF) * a + (d & 0xFF00FF) * (256 - a)) >> 8) & 0xFF00FF;
    u32 g  = (((s & 0x00FF00) * a + (d & 0x00FF00) * (256 - a)) >> 8) & 0x00FF00;
    return rb | g;
}
static inline int a256(int a) { return a + (a >> 7); }  /* 0..255 -> 0..256 */

/* ----------------------------------------------------------------- init */
int gfx_init(u32 addr, u32 pitch, u32 w, u32 h, u32 bpp) {
    if (w > MAXW || h > MAXH || (bpp != 32 && bpp != 24)) return 0;
    fb = (u8 *)addr; fb_pitch = pitch; fb_bpp = bpp; scr_w = w; scr_h = h;
    gfx_reset_clip();
    return 1;
}

u32 gfx_mix(u32 a, u32 b, int t) { return blend(a, b, t); }

/* bilinear upscale of a wallpaper image to the screen, fixed point 16.16 */
void gfx_set_wallpaper(const image_t *src) {
    u32 stx = ((u32)(src->w - 1) << 16) / (u32)(scr_w > 1 ? scr_w - 1 : 1);
    u32 sty = ((u32)(src->h - 1) << 16) / (u32)(scr_h > 1 ? scr_h - 1 : 1);
    for (int y = 0; y < scr_h; y++) {
        u32 fy = (u32)y * sty;
        int y0 = fy >> 16, y1 = y0 + 1 < src->h ? y0 + 1 : y0; int wy = (fy >> 8) & 0xFF;
        const u32 *r0 = &src->px[y0 * src->w], *r1 = &src->px[y1 * src->w];
        u32 *out = &wall[y * scr_w];
        for (int x = 0; x < scr_w; x++) {
            u32 fx = (u32)x * stx;
            int x0 = fx >> 16, x1 = x0 + 1 < src->w ? x0 + 1 : x0; int wx = (fx >> 8) & 0xFF;
            u32 top = blend(r0[x0], r0[x1], wx), bot = blend(r1[x0], r1[x1], wx);
            out[x] = blend(top, bot, wy);
        }
    }
}

/* ------------------------------------------------------------- clipping */
void gfx_reset_clip(void) { cx0 = 0; cy0 = 0; cx1 = scr_w; cy1 = scr_h; }
void gfx_get_clip(int *x0, int *y0, int *x1, int *y1) { *x0 = cx0; *y0 = cy0; *x1 = cx1; *y1 = cy1; }
void gfx_clip_intersect(int x, int y, int w, int h) {
    if (x > cx0) cx0 = x;
    if (y > cy0) cy0 = y;
    if (x + w < cx1) cx1 = x + w;
    if (y + h < cy1) cy1 = y + h;
    if (cx1 < cx0) cx1 = cx0;
    if (cy1 < cy0) cy1 = cy0;
}
void gfx_set_clip(int x, int y, int w, int h) {
    cx0 = x < 0 ? 0 : x; cy0 = y < 0 ? 0 : y;
    cx1 = x + w > scr_w ? scr_w : x + w; cy1 = y + h > scr_h ? scr_h : y + h;
}

/* ------------------------------------------------------------ primitives */
void gfx_pixel(int x, int y, u32 c) {
    if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) back[y * scr_w + x] = c;
}
u32 gfx_get(int x, int y) { return (x >= 0 && x < scr_w && y >= 0 && y < scr_h) ? back[y * scr_w + x] : 0; }

static inline void pixel_a(int x, int y, u32 c, int a) {
    if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) {
        u32 *p = &back[y * scr_w + x]; *p = blend(*p, c, a);
    }
}

void gfx_fill(int x, int y, int w, int h, u32 c) {
    int xa = x < cx0 ? cx0 : x, xb = x + w > cx1 ? cx1 : x + w;
    int ya = y < cy0 ? cy0 : y, yb = y + h > cy1 ? cy1 : y + h;
    if (xa >= xb || ya >= yb) return;
    for (int j = ya; j < yb; j++) {
        u32 *p = &back[j * scr_w + xa]; int n = xb - xa;
        __asm__ volatile("rep stosl" : "+D"(p), "+c"(n) : "a"(c) : "memory");
    }
}

void gfx_fill_alpha(int x, int y, int w, int h, u32 c, int a) {
    if (a >= 255) { gfx_fill(x, y, w, h, c); return; }
    a = a256(a);
    int xa = x < cx0 ? cx0 : x, xb = x + w > cx1 ? cx1 : x + w;
    int ya = y < cy0 ? cy0 : y, yb = y + h > cy1 ? cy1 : y + h;
    for (int j = ya; j < yb; j++) {
        u32 *p = &back[j * scr_w];
        for (int i = xa; i < xb; i++) p[i] = blend(p[i], c, a);
    }
}

void gfx_hline(int x, int y, int w, u32 c) { gfx_fill(x, y, w, 1, c); }
void gfx_vline(int x, int y, int h, u32 c) { gfx_fill(x, y, 1, h, c); }
void gfx_rect(int x, int y, int w, int h, u32 c) {
    gfx_hline(x, y, w, c); gfx_hline(x, y + h - 1, w, c);
    gfx_vline(x, y, h, c); gfx_vline(x + w - 1, y, h, c);
}

/* coverage (0..16) of corner pixel (i,j) of a quarter circle with radius r,
 * where (0,0) is the outermost corner pixel */
static int corner_cov(int i, int j, int r) {
    int n = 0, R = 64 * r * r;
    for (int sy = 0; sy < 4; sy++) {
        int Y = 8 * r - (8 * j + 2 * sy + 1);
        for (int sx = 0; sx < 4; sx++) {
            int X = 8 * r - (8 * i + 2 * sx + 1);
            if (X * X + Y * Y <= R) n++;
        }
    }
    return n;
}

/* rounded shape: filled (border=0) or 1px outline (border=1) with alpha */
static void round_shape(int x, int y, int w, int h, int r, u32 c, int a, int border) {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    int A = a256(a);
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < cy0 || yy >= cy1) continue;
        int cj = j < r ? j : (j >= h - r ? h - 1 - j : -1);
        if (cj < 0) {                                  /* middle band */
            if (border) { pixel_a(x, yy, c, A); pixel_a(x + w - 1, yy, c, A); }
            else if (a >= 255) gfx_fill(x, yy, w, 1, c);
            else gfx_fill_alpha(x, yy, w, 1, c, a);
            continue;
        }
        for (int i = 0; i < r; i++) {                  /* corners */
            int cov = corner_cov(i, cj, r);
            if (border) cov -= (i > 0 && cj > 0 && r > 1) ? corner_cov(i - 1, cj - 1, r - 1) : 0;
            if (cov <= 0) continue;
            int aa = A * cov / 16;
            pixel_a(x + i, yy, c, aa); pixel_a(x + w - 1 - i, yy, c, aa);
        }
        if (border) {
            if (cj == 0) {
                int xa = x + r, n = w - 2 * r;
                if (a >= 255) gfx_fill(xa, yy, n, 1, c); else gfx_fill_alpha(xa, yy, n, 1, c, a);
            }
        } else {
            if (a >= 255) gfx_fill(x + r, yy, w - 2 * r, 1, c);
            else gfx_fill_alpha(x + r, yy, w - 2 * r, 1, c, a);
        }
    }
}

void gfx_fill_round(int x, int y, int w, int h, int r, u32 c) { round_shape(x, y, w, h, r, c, 255, 0); }
void gfx_fill_round_alpha(int x, int y, int w, int h, int r, u32 c, int a) { round_shape(x, y, w, h, r, c, a, 0); }
void gfx_round_border(int x, int y, int w, int h, int r, u32 c) { round_shape(x, y, w, h, r, c, 255, 1); }
void gfx_circle_fill(int cx, int cy, int r, u32 c) { round_shape(cx - r, cy - r, 2 * r, 2 * r, r, c, 255, 0); }

void gfx_shadow(int x, int y, int w, int h, int r, int size, int strength) {
    for (int i = size; i >= 1; i--) {
        int a = strength * (size - i + 1) * (size - i + 1) / (size * size);
        if (a < 1) continue;
        round_shape(x - i, y - i + size / 3, w + 2 * i, h + 2 * i, r + i, 0, a, 1);
    }
}

void gfx_line(int x0, int y0, int x1, int y1, u32 c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_vgradient(int x, int y, int w, int h, u32 top, u32 bot) {
    for (int j = 0; j < h; j++) gfx_fill(x, y + j, w, 1, blend(top, bot, h > 1 ? j * 256 / (h - 1) : 0));
}

void gfx_image(int x, int y, const image_t *img) {
    for (int j = 0; j < img->h; j++) {
        int yy = y + j; if (yy < cy0 || yy >= cy1) continue;
        for (int i = 0; i < img->w; i++) {
            int xx = x + i; if (xx < cx0 || xx >= cx1) continue;
            u32 p = img->px[j * img->w + i]; int a = p >> 24;
            if (!a) continue;
            u32 *d = &back[yy * scr_w + xx];
            *d = a == 255 ? (p & 0xFFFFFF) : blend(*d, p, a256(a));
        }
    }
}

/* nearest-neighbour scaled image; opaque images (alpha 0 everywhere, e.g. wallpapers) are copied */
void gfx_image_scaled(int x, int y, int w, int h, const image_t *img) {
    int opaque = (img->px[0] >> 24) == 0;
    for (int j = 0; j < h; j++) {
        int yy = y + j; if (yy < cy0 || yy >= cy1) continue;
        const u32 *row = &img->px[(j * img->h / h) * img->w];
        u32 *dst = &back[yy * scr_w];
        for (int i = 0; i < w; i++) {
            int xx = x + i; if (xx < cx0 || xx >= cx1) continue;
            u32 p = row[i * img->w / w];
            if (opaque) { dst[xx] = p & 0xFFFFFF; continue; }
            int a = p >> 24; if (!a) continue;
            dst[xx] = a == 255 ? (p & 0xFFFFFF) : blend(dst[xx], p, a256(a));
        }
    }
}

void gfx_blit(int x, int y, int w, int h, const u32 *px) {
    int xa = x < cx0 ? cx0 : x, xb = x + w > cx1 ? cx1 : x + w;
    int ya = y < cy0 ? cy0 : y, yb = y + h > cy1 ? cy1 : y + h;
    if (xa >= xb || ya >= yb) return;
    for (int j = ya; j < yb; j++)
        memcpy(&back[j * scr_w + xa], &px[(j - y) * w + (xa - x)], (u32)(xb - xa) * 4);
}

void gfx_wallpaper(void) { memcpy(back, wall, (u32)scr_w * scr_h * 4); }

/* ------------------------------------------------------------------ text */
void gfx_char(int x, int y, char ch, u32 c, const font_t *f) {
    if (ch < 32 || ch > 126) ch = '?';
    const glyph_t *g = &f->g[ch - 32];
    const u8 *d = f->data + g->off;
    int x0 = x + g->left;
    for (int j = 0; j < f->h; j++) {
        int yy = y + j; if (yy < cy0 || yy >= cy1) continue;
        u32 *row = &back[yy * scr_w];
        for (int i = 0; i < g->w; i++) {
            int a = d[j * g->w + i]; if (!a) continue;
            int xx = x0 + i; if (xx < cx0 || xx >= cx1) continue;
            row[xx] = a == 255 ? c : blend(row[xx], c, a256(a));
        }
    }
}

int gfx_text(int x, int y, const char *s, u32 c, const font_t *f) {
    for (; *s; s++) {
        char ch = (*s < 32 || *s > 126) ? '?' : *s;
        gfx_char(x, y, ch, c, f);
        x += f->g[ch - 32].adv;
    }
    return x;
}

int gfx_text_w(const char *s, const font_t *f) {
    int w = 0;
    for (; *s; s++) { char ch = (*s < 32 || *s > 126) ? '?' : *s; w += f->g[ch - 32].adv; }
    return w;
}

void gfx_text_fit(int x, int y, int maxw, const char *s, u32 c, const font_t *f) {
    if (gfx_text_w(s, f) <= maxw) { gfx_text(x, y, s, c, f); return; }
    int dots = gfx_text_w("...", f), w = 0;
    for (; *s; s++) {
        char ch = (*s < 32 || *s > 126) ? '?' : *s;
        int a = f->g[ch - 32].adv;
        if (w + a > maxw - dots) break;
        gfx_char(x + w, y, ch, c, f); w += a;
    }
    gfx_text(x + w, y, "...", c, f);
}

void gfx_text_center(int x, int y, int w, const char *s, u32 c, const font_t *f) {
    gfx_text(x + (w - gfx_text_w(s, f)) / 2, y, s, c, f);
}

/* --------------------------------------------------------------- present */
void gfx_present_rect(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > scr_w) w = scr_w - x;
    if (y + h > scr_h) h = scr_h - y;
    if (w <= 0 || h <= 0) return;
    for (int j = y; j < y + h; j++) {
        u32 *src = &back[j * scr_w + x];
        if (fb_bpp == 32) {
            u32 *dst = (u32 *)(fb + j * fb_pitch) + x; int n = w;
            __asm__ volatile("rep movsl" : "+D"(dst), "+S"(src), "+c"(n) :: "memory");
        } else {
            u8 *dst = fb + j * fb_pitch + x * 3;
            for (int i = 0; i < w; i++) { u32 p = src[i]; dst[0] = p; dst[1] = p >> 8; dst[2] = p >> 16; dst += 3; }
        }
    }
}
void gfx_present(void) { gfx_present_rect(0, 0, scr_w, scr_h); }

/* ---------------------------------------------------------- mouse cursor */
static const char *cursor[] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ",
    "X....X      ", "X.....X     ", "X......X    ", "X.......X   ", "X........X  ",
    "X.........X ", "X..........X", "X......XXXXX", "X...X..X    ", "X..XX..X    ",
    "X.X  X..X   ", "XX   X..X   ", "X     X..X  ", "      X..X  ", "       XX   " };
#define CUR_W 12
#define CUR_H 20

static void fb_put(int x, int y, u32 c) {
    if (x < 0 || y < 0 || x >= scr_w || y >= scr_h) return;
    if (fb_bpp == 32) ((u32 *)(fb + y * fb_pitch))[x] = c;
    else { u8 *d = fb + y * fb_pitch + x * 3; d[0] = c; d[1] = c >> 8; d[2] = c >> 16; }
}

static const char *cur_resize[] = {
    "XXXXX       ", "X...X       ", "X..X        ", "X.X.X       ", "XX X.X      ", "    X.X     ",
    "     X.X    ", "      X.X XX", "       X.X.X", "        X..X", "       X...X", "       XXXXX",
    "            ", "            ", "            ", "            ", "            ", "            ",
    "            ", "            " };

void gfx_cursor(int x, int y, int shape) {
    const char **cs = shape ? cur_resize : cursor;
    if (shape) { x -= 6; y -= 6; }
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++) {
            char ch = cs[j][i];
            if (ch == 'X') fb_put(x + i, y + j, 0x101010);
            else if (ch == '.') fb_put(x + i, y + j, 0xFFFFFF);
        }
}
void gfx_cursor_restore(int x, int y) { gfx_present_rect(x - 7, y - 8, CUR_W + 9, CUR_H + 9); }
