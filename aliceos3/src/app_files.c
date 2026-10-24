/* AliceOS 3 - Files: a Windows-style file explorer for the AliceOS file system. */
#include "os.h"

#define TB_H   50
#define SIDE_W 172
#define ST_H   28
#define CELL_W 104
#define CELL_H 100
#define MAXI   256

enum { OP_NONE, OP_NEWDIR, OP_NEWFILE, OP_RENAME };
typedef struct {
    fnode_t *dir, *hist[32]; int nh;
    fnode_t *items[MAXI]; int n;
    int sel, scroll, op;
    fnode_t *target;
} files_t;

static const char *place_names[] = { "Home", "Desktop", "Documents", "Pictures", "Computer", "tmp" };
static const char *place_paths[] = { "~", "~/Desktop", "~/Documents", "~/Pictures", "/", "/tmp" };
#define NPLACES 6

static const image_t *kind_icon(fnode_t *n, int small) {
    switch (fs_kind(n)) {
        case FK_DIR: return small ? &fi_folder_s : &fi_folder;
        case FK_IMAGE: return small ? &fi_image_s : &fi_image;
        case FK_PROG: return small ? &fi_prog_s : &fi_prog;
        default: return small ? &fi_text_s : &fi_text;
    }
}

static void refresh(files_t *f) {
    if (!fs_alive(f->dir)) { f->dir = fs_home; f->nh = 0; }
    fnode_t *all[MAXI]; int n = fs_list(f->dir, all, MAXI);
    f->n = 0;
    for (int i = 0; i < n; i++) if (all[i]->name[0] != '.') f->items[f->n++] = all[i];
    if (f->sel >= f->n) f->sel = f->n - 1;
}

static void set_title(window_t *w) {
    files_t *f = w->state;
    strcpy(w->title, f->dir == fs_root ? "Computer" : f->dir == fs_home ? "Home" : f->dir->name);
    strcat(w->title, " - Files");
}

static void go(window_t *w, fnode_t *d, int push) {
    files_t *f = w->state;
    if (!d || d->type != FT_DIR || d == f->dir) return;
    if (push) { if (f->nh == 32) { memmove(f->hist, f->hist + 1, sizeof(f->hist[0]) * 31); f->nh--; } f->hist[f->nh++] = f->dir; }
    f->dir = d; f->sel = -1; f->scroll = 0;
    refresh(f); set_title(w); gui_dirty();
}

void files_set_dir(window_t *w, fnode_t *dir) { files_t *f = w->state; f->dir = dir; f->nh = 0; refresh(f); set_title(w); }

/* ------------------------------------------------------------ layout */
static int grid_cols(int cw) { int c = (cw - SIDE_W - 16) / CELL_W; return c < 1 ? 1 : c; }
static void cell_pos(int i, int cw, int scroll, int *x, int *y) {
    int cols = grid_cols(cw);
    *x = SIDE_W + 12 + (i % cols) * CELL_W;
    *y = TB_H + 10 + (i / cols - scroll) * CELL_H;
}
static int item_at(files_t *f, int cw, int ch, int x, int y) {
    if (x < SIDE_W || y < TB_H || y >= ch - ST_H) return -1;
    for (int i = 0; i < f->n; i++) {
        int ix, iy; cell_pos(i, cw, f->scroll, &ix, &iy);
        if (x >= ix && x < ix + CELL_W - 6 && y >= iy && y < iy + CELL_H - 6) return i;
    }
    return -1;
}
static int visible_rows(int ch) { int r = (ch - TB_H - ST_H - 10) / CELL_H; return r < 1 ? 1 : r; }

static void keep_visible(files_t *f, int cw, int ch) {
    if (f->sel < 0) return;
    int row = f->sel / grid_cols(cw), vr = visible_rows(ch);
    if (row < f->scroll) f->scroll = row;
    if (row >= f->scroll + vr) f->scroll = row - vr + 1;
}

/* ------------------------------------------------------------ drawing */
static void tb_button(int x, int y, int w, const char *label, int enabled) {
    gfx_fill_round(x, y, w, 32, 8, th.panel2);
    gfx_text_center(x, y + (32 - font_ui.h) / 2, w, label, enabled ? th.text : th.subtext, &font_ui);
}
static void chevron(int cx, int cy, int dir, u32 c) {      /* dir 0 left, 1 up */
    for (int t = 0; t < 2; t++) {
        if (dir == 0) { gfx_line(cx + 3 + t, cy - 6, cx - 3 + t, cy, c); gfx_line(cx - 3 + t, cy, cx + 3 + t, cy + 6, c); }
        else { gfx_line(cx - 6, cy + 3 + t, cx, cy - 3 + t, c); gfx_line(cx, cy - 3 + t, cx + 6, cy + 3 + t, c); }
    }
}

static void files_draw(window_t *w, int cx, int cy, int cw, int ch) {
    files_t *f = w->state;
    refresh(f);
    /* toolbar */
    gfx_fill(cx, cy, cw, TB_H, th.panel);
    gfx_hline(cx, cy + TB_H - 1, cw, th.line);
    gfx_fill_round(cx + 10, cy + 9, 34, 32, 8, th.panel2); chevron(cx + 27, cy + 25, 0, f->nh ? th.text : th.subtext);
    gfx_fill_round(cx + 48, cy + 9, 34, 32, 8, th.panel2); chevron(cx + 65, cy + 25, 1, f->dir->parent ? th.text : th.subtext);
    int pw = cw - 90 - 236;
    gfx_fill_round(cx + 90, cy + 9, pw, 32, 8, th.input_bg);
    gfx_round_border(cx + 90, cy + 9, pw, 32, 8, th.line);
    gfx_image(cx + 98, cy + 15, &fi_folder_s);
    char path[200]; fs_path(f->dir, path, sizeof(path));
    gfx_text_fit(cx + 126, cy + 9 + (32 - font_ui.h) / 2, pw - 44, path, th.text, &font_ui);
    int writable = fs_can_write(f->dir);
    tb_button(cx + cw - 230, cy + 9, 112, "+ New folder", writable);
    tb_button(cx + cw - 112, cy + 9, 102, "+ New file", writable);

    /* sidebar */
    gfx_fill(cx, cy + TB_H, SIDE_W, ch - TB_H, th.panel);
    gfx_vline(cx + SIDE_W - 1, cy + TB_H, ch - TB_H, th.line);
    gfx_text(cx + 16, cy + TB_H + 12, "PLACES", th.subtext, &font_small);
    for (int i = 0; i < NPLACES; i++) {
        int y = cy + TB_H + 34 + i * 36 + (i >= 4 ? 22 : 0);
        if (i == 4) gfx_text(cx + 16, y - 20, "SYSTEM", th.subtext, &font_small);
        fnode_t *p = fs_resolve(place_paths[i], NULL);
        if (p == f->dir) gfx_fill_round(cx + 8, y, SIDE_W - 16, 32, 8, th.accent_soft);
        gfx_image(cx + 18, y + 6, &fi_folder_s);
        gfx_text(cx + 46, y + (32 - font_ui.h) / 2, place_names[i], th.text, &font_ui);
    }

    /* items */
    int gx = cx + SIDE_W, gw = cw - SIDE_W;
    int k0, k1, k2, k3; gfx_get_clip(&k0, &k1, &k2, &k3);
    gfx_clip_intersect(gx, cy + TB_H, gw, ch - TB_H - ST_H);
    if (!f->n) gfx_text_center(gx, cy + TB_H + 60, gw, "This folder is empty", th.subtext, &font_ui);
    for (int i = 0; i < f->n; i++) {
        int ix, iy; cell_pos(i, cw, f->scroll, &ix, &iy);
        ix += cx; iy += cy;
        if (iy + CELL_H < cy + TB_H || iy > cy + ch) continue;
        if (i == f->sel) {
            gfx_fill_round(ix, iy, CELL_W - 6, CELL_H - 6, 10, th.accent_soft);
            gfx_round_border(ix, iy, CELL_W - 6, CELL_H - 6, 10, th.accent);
        }
        gfx_image(ix + (CELL_W - 6 - 48) / 2, iy + 8, kind_icon(f->items[i], 0));
        const char *nm = f->items[i]->name;
        int tw = gfx_text_w(nm, &font_small);
        if (tw <= CELL_W - 14) gfx_text(ix + (CELL_W - 6 - tw) / 2, iy + 64, nm, th.text, &font_small);
        else gfx_text_fit(ix + 4, iy + 64, CELL_W - 14, nm, th.text, &font_small);
    }
    gfx_set_clip(k0, k1, k2 - k0, k3 - k1);

    /* status bar */
    int sy = cy + ch - ST_H;
    gfx_fill(gx, sy, gw, ST_H, th.panel);
    gfx_hline(gx, sy, gw, th.line);
    char st[96], n[16]; itoa(f->n, n); strcpy(st, n); strcat(st, f->n == 1 ? " item" : " items");
    if (f->sel >= 0 && f->sel < f->n) {
        fnode_t *s = f->items[f->sel];
        strcat(st, "   |   "); strcat(st, s->name);
        if (s->type == FT_FILE) { strcat(st, "  ("); fmt_size(s->size, n); strcat(st, n); strcat(st, ")"); }
    }
    if (!writable) strcat(st, "   |   read-only (owned by root)");
    gfx_text_fit(gx + 12, sy + (ST_H - font_small.h) / 2, gw - 24, st, th.subtext, &font_small);
}

/* ------------------------------------------------------------ actions */
static void unique_name(fnode_t *dir, const char *base, const char *ext, char *out) {
    strcpy(out, base); strcat(out, ext);
    for (int i = 2; fs_child(dir, out) && i < 100; i++) { char n[8]; itoa(i, n); strcpy(out, base); strcat(out, " "); strcat(out, n); strcat(out, ext); }
}

static void prompt_done(window_t *w, const char *text) {
    files_t *f = w->state;
    if (!*text) return;
    int e = FS_OK; fnode_t *n = NULL;
    char path[256]; fs_path(f->dir, path, 200); if (strcmp(path, "/")) strcat(path, "/"); strncpy0(path + strlen(path), text, 50);
    if (f->op == OP_NEWDIR) e = fs_mkdir(path, NULL, &n);
    else if (f->op == OP_NEWFILE) { if (fs_child(f->dir, text)) e = FS_EEXIST; else e = fs_create(path, NULL, &n); }
    else if (f->op == OP_RENAME) { if (fs_alive(f->target)) { e = fs_rename(f->target, text); n = f->target; } }
    if (e) gui_toast("Could not complete the action", fs_strerror(e));
    refresh(f);
    for (int i = 0; i < f->n; i++) if (f->items[i] == n) f->sel = i;
    f->op = OP_NONE; gui_dirty();
}

static void new_item(window_t *w, int dir) {
    files_t *f = w->state;
    if (!fs_can_write(f->dir)) { gui_toast("Permission denied", "This folder belongs to root. Use your home folder."); return; }
    char name[64]; unique_name(f->dir, dir ? "New folder" : "New file", dir ? "" : ".txt", name);
    f->op = dir ? OP_NEWDIR : OP_NEWFILE;
    gui_prompt(w, dir ? "Name of the new folder" : "Name of the new file", name, prompt_done);
}

static void delete_item(window_t *w, fnode_t *n) {
    files_t *f = w->state;
    char msg[96]; strcpy(msg, n->name);
    int e = fs_remove(n, 1);
    if (e) gui_toast("Could not delete", fs_strerror(e));
    else { strcat(msg, " was deleted"); gui_toast("Deleted", msg); }
    refresh(f); gui_dirty();
}

static void rename_item(window_t *w, fnode_t *n) {
    files_t *f = w->state;
    f->op = OP_RENAME; f->target = n;
    gui_prompt(w, "Rename to", n->name, prompt_done);
}

static void open_item(window_t *w, fnode_t *n) {
    if (n->type == FT_DIR) go(w, n, 1); else open_node(n);
}

static void properties(fnode_t *n) {
    char t[96], m[128], b[24];
    fs_path(n, t, sizeof(t));
    strcpy(m, n->type == FT_DIR ? "Folder" : "File");
    if (n->type == FT_FILE) { strcat(m, ", "); fmt_size(n->size, b); strcat(m, b); }
    strcat(m, ", owner "); strcat(m, n->owner == OWN_ALICE ? "alice" : "root");
    strcat(m, ", modified "); fmt_datetime(&n->mtime, b); strcat(m, b);
    gui_toast(t, m);
}

static void item_menu_cb(window_t *w, int i, void *ctx) {
    fnode_t *n = ctx;
    if (!fs_alive(n)) return;
    if (i == 0) open_item(w, n);
    else if (i == 1) rename_item(w, n);
    else if (i == 2) delete_item(w, n);
    else if (i == 3) { window_t *t = gui_new(APP_TERMINAL); if (t) term_set_cwd(t, n->type == FT_DIR ? n : n->parent); }
    else properties(n);
}
static void empty_menu_cb(window_t *w, int i, void *ctx) {
    (void)ctx; files_t *f = w->state;
    if (i == 0) new_item(w, 1);
    else if (i == 1) new_item(w, 0);
    else if (i == 2) { window_t *t = gui_new(APP_TERMINAL); if (t) term_set_cwd(t, f->dir); }
    else properties(f->dir);
}

static void files_mouse(window_t *w, int x, int y, int ev) {
    files_t *f = w->state;
    int cw = gui_client_w(w), ch = gui_client_h(w);
    if (ev == EV_WHEEL) {
        int rows = (f->n + grid_cols(cw) - 1) / grid_cols(cw);
        f->scroll += ev_wheel; if (f->scroll > rows - visible_rows(ch)) f->scroll = rows - visible_rows(ch);
        if (f->scroll < 0) f->scroll = 0;
        gui_dirty(); return;
    }
    if (ev == EV_DOWN) {
        if (y < TB_H) {
            if (x >= 10 && x < 44 && f->nh) { fnode_t *d = f->hist[--f->nh]; if (!fs_alive(d)) d = fs_home; go(w, d, 0); }
            else if (x >= 48 && x < 82 && f->dir->parent) go(w, f->dir->parent, 1);
            else if (x >= cw - 230 && x < cw - 118) new_item(w, 1);
            else if (x >= cw - 112 && x < cw - 10) new_item(w, 0);
            return;
        }
        if (x < SIDE_W) {
            for (int i = 0; i < NPLACES; i++) {
                int py = TB_H + 34 + i * 36 + (i >= 4 ? 22 : 0);
                if (y >= py && y < py + 32) { go(w, fs_resolve(place_paths[i], NULL), 1); return; }
            }
            return;
        }
        f->sel = item_at(f, cw, ch, x, y); gui_dirty();
    } else if (ev == EV_DBL) {
        int i = item_at(f, cw, ch, x, y);
        if (i >= 0) open_item(w, f->items[i]);
    } else if (ev == EV_RDOWN) {
        int i = item_at(f, cw, ch, x, y);
        int sx = w->x + 1 + x, sy = w->y + TITLE_H + y;
        if (i >= 0) {
            f->sel = i;
            static const char *it_dir[] = { "Open", "Rename", "Delete", "Open in Terminal", "Properties" };
            gui_menu(sx, sy, it_dir, 5, item_menu_cb, w, f->items[i]);
        } else if (x >= SIDE_W && y >= TB_H) {
            static const char *it[] = { "New folder", "New text file", "Open Terminal here", "Properties" };
            gui_menu(sx, sy, it, 4, empty_menu_cb, w, NULL);
        }
        gui_dirty();
    }
}

static void files_key(window_t *w, int k) {
    files_t *f = w->state;
    int cw = gui_client_w(w), ch = gui_client_h(w), cols = grid_cols(cw);
    refresh(f);
    if (k == K_RIGHT && f->sel < f->n - 1) f->sel++;
    else if (k == K_LEFT && f->sel > 0) f->sel--;
    else if (k == K_DOWN) f->sel = f->sel < 0 ? 0 : (f->sel + cols < f->n ? f->sel + cols : f->sel);
    else if (k == K_UP && f->sel - cols >= 0) f->sel -= cols;
    else if (k == '\n' && f->sel >= 0) { open_item(w, f->items[f->sel]); return; }
    else if (k == '\b' && f->dir->parent) { go(w, f->dir->parent, 1); return; }
    else if (k == K_DEL && f->sel >= 0) { delete_item(w, f->items[f->sel]); return; }
    else if (k == K_F2 && f->sel >= 0) { rename_item(w, f->items[f->sel]); return; }
    else if (k == K_CTRL('n')) { new_item(w, 0); return; }
    keep_visible(f, cw, ch);
    gui_dirty();
}

void files_setup(window_t *w) {
    w->w = 820; w->h = 520; w->resizable = 1; w->min_w = 520; w->min_h = 300;
    w->draw = files_draw; w->mouse = files_mouse; w->key = files_key;
    files_t *f = kzalloc(sizeof(files_t));
    w->state = f; w->mem = sizeof(files_t);
    if (!f) return;
    f->dir = fs_home; f->sel = -1;
    refresh(f); set_title(w);
}
