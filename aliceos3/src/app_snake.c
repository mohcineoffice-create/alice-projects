/* AliceOS 3 - Snake game. */
#include "os.h"

/* ====================================================================== SNAKE */
#define S_CW 20
#define S_GW 22
#define S_GH 18
#define S_HDR 44
typedef struct { int x, y; } pt_t;
static pt_t sn[S_GW * S_GH]; static int s_len, s_dir, s_ndir, s_state, s_score, s_best; static pt_t s_food;
static const int s_dx[4] = { 1, 0, -1, 0 }, s_dy[4] = { 0, 1, 0, -1 };

static void s_place_food(void) {
    for (;;) {
        pt_t f = { (int)(rand32() % S_GW), (int)(rand32() % S_GH) };
        int ok = 1; for (int i = 0; i < s_len; i++) if (sn[i].x == f.x && sn[i].y == f.y) ok = 0;
        if (ok) { s_food = f; return; }
    }
}
static void s_reset(void) {
    s_len = 4; s_dir = s_ndir = 0; s_score = 0;
    for (int i = 0; i < s_len; i++) { sn[i].x = 8 - i; sn[i].y = S_GH / 2; }
    s_place_food();
}
static void s_step(void) {
    s_dir = s_ndir;
    pt_t h = { sn[0].x + s_dx[s_dir], sn[0].y + s_dy[s_dir] };
    if (h.x < 0 || h.y < 0 || h.x >= S_GW || h.y >= S_GH) { s_state = 2; return; }
    for (int i = 0; i < s_len - 1; i++) if (sn[i].x == h.x && sn[i].y == h.y) { s_state = 2; return; }
    int grow = (h.x == s_food.x && h.y == s_food.y);
    if (grow && s_len < S_GW * S_GH) s_len++;
    for (int i = s_len - 1; i > 0; i--) sn[i] = sn[i - 1];
    sn[0] = h;
    if (grow) { s_score += 10; if (s_score > s_best) s_best = s_score; s_place_food(); }
}

static void snake_draw(window_t *w, int cx, int cy, int cw, int ch) {
    (void)w; (void)ch;
    char b[48], n[12];
    strcpy(b, "Score  "); itoa(s_score, n); strcat(b, n);
    gfx_text(cx + 16, cy + (S_HDR - font_ui_b.h) / 2, b, RGB(220, 255, 220), &font_ui_b);
    strcpy(b, "Best  "); itoa(s_best, n); strcat(b, n);
    gfx_text(cx + cw - 16 - gfx_text_w(b, &font_ui), cy + (S_HDR - font_ui.h) / 2, b, RGB(160, 210, 170), &font_ui);
    int bx = cx, by = cy + S_HDR;
    for (int j = 0; j < S_GH; j++)
        for (int i = 0; i < S_GW; i++)
            gfx_fill(bx + i * S_CW, by + j * S_CW, S_CW, S_CW, ((i + j) & 1) ? RGB(26, 58, 38) : RGB(30, 66, 43));
    gfx_circle_fill(bx + s_food.x * S_CW + 10, by + s_food.y * S_CW + 10, 7, RGB(255, 80, 80));
    gfx_fill(bx + s_food.x * S_CW + 10, by + s_food.y * S_CW + 1, 2, 4, RGB(120, 70, 30));
    for (int i = s_len - 1; i >= 0; i--) {
        u32 c = i == 0 ? RGB(190, 255, 140) : (i % 2 ? RGB(120, 220, 110) : RGB(105, 205, 98));
        gfx_fill_round(bx + sn[i].x * S_CW + 1, by + sn[i].y * S_CW + 1, S_CW - 2, S_CW - 2, 6, c);
    }
    int hx = bx + sn[0].x * S_CW + 10, hy = by + sn[0].y * S_CW + 10;
    int ex = s_dy[s_dir] ? 4 : 0, ey = s_dx[s_dir] ? 4 : 0;
    gfx_fill(hx - ex + s_dx[s_dir] * 3 - 1, hy - ey + s_dy[s_dir] * 3 - 1, 3, 3, RGB(20, 30, 20));
    gfx_fill(hx + ex + s_dx[s_dir] * 3 - 1, hy + ey + s_dy[s_dir] * 3 - 1, 3, 3, RGB(20, 30, 20));
    if (s_state != 1) {
        const char *t = s_state == 0 ? "SNAKE" : s_state == 2 ? "GAME OVER" : "PAUSED";
        const char *m = s_state == 3 ? "Press P to continue" : "Press SPACE to play  -  arrows to move";
        int pw = 330, ph = 110, px = cx + (cw - pw) / 2, py = by + (S_GH * S_CW - ph) / 2;
        gfx_fill_round_alpha(px, py, pw, ph, 14, RGB(10, 20, 14), 210);
        gfx_text_center(px, py + 22, pw, t, RGB(255, 255, 255), &font_title);
        gfx_text_center(px, py + 66, pw, m, RGB(190, 230, 200), &font_ui);
    }
}
static void snake_key(window_t *w, int k) {
    (void)w;
    int nd = -1;
    if (k == K_RIGHT || k == 'd') nd = 0; else if (k == K_DOWN || k == 's') nd = 1;
    else if (k == K_LEFT || k == 'a') nd = 2; else if (k == K_UP || k == 'w') nd = 3;
    if (nd >= 0 && s_state == 1 && (nd + 2) % 4 != s_dir) s_ndir = nd;
    if (k == ' ' && (s_state == 0 || s_state == 2)) { s_reset(); s_state = 1; }
    if ((k == 'p' || k == 'P') && (s_state == 1 || s_state == 3)) s_state = s_state == 1 ? 3 : 1;
    gui_dirty();
}
static void snake_tick(window_t *w) {
    if (s_state == 1 && !gui_focused(w)) { s_state = 3; gui_dirty(); return; }
    if (s_state == 1 && ticks % 9 == 0) { s_step(); gui_dirty(); }
}


void snake_setup(window_t *w) {
    w->w = S_GW * S_CW + 2; w->h = TITLE_H + S_HDR + S_GH * S_CW + 1; w->bg = RGB(28, 62, 40);
    w->draw = snake_draw; w->key = snake_key; w->tick = snake_tick;
    if (!s_len) { s_reset(); s_state = 0; }
}
