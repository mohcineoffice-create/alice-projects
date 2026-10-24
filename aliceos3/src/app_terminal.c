/* AliceOS 3 - Terminal: a Linux-style shell on the AliceOS file system. */
#include "os.h"

#define T_ROWS 400
#define T_COLS 100
#define WRAP   96
typedef struct { char t[T_COLS]; u32 c1, c2; u8 split; } tline_t;
typedef struct {
    tline_t l[T_ROWS]; int n, scroll;
    char in[96]; int ilen;
    char hist[24][96]; int hn, hpos;
    fnode_t *cwd;
} term_t;

#define C_TXT  RGB(220, 222, 235)
#define C_DIM  RGB(140, 144, 170)
#define C_OK   RGB(80, 250, 123)
#define C_ERR  RGB(255, 110, 110)
#define C_DIR  RGB(110, 170, 255)
#define C_YEL  RGB(241, 250, 140)
#define C_PUR  RGB(189, 147, 249)
#define C_IMG  RGB(255, 150, 200)

static term_t *T;                 /* terminal currently executing a command */
static char *sink; static u32 sink_len, sink_cap;   /* output redirection */

static void sink_add(const char *s) {
    u32 n = strlen(s);
    if (sink_len + n + 2 > sink_cap) {
        u32 cap = sink_cap ? sink_cap * 2 : 256;
        while (cap < sink_len + n + 2) cap *= 2;
        char *d = krealloc(sink, cap); if (!d) return;
        sink = d; sink_cap = cap;
    }
    memcpy(sink + sink_len, s, n); sink_len += n; sink[sink_len++] = '\n'; sink[sink_len] = 0;
}

static void line2(const char *s, u32 c1, int split, u32 c2) {
    if (sink_cap && sink) { sink_add(s); return; }
    if (T->n == T_ROWS) { memmove(&T->l[0], &T->l[1], sizeof(tline_t) * (T_ROWS - 1)); T->n--; }
    tline_t *l = &T->l[T->n++];
    strncpy0(l->t, s, T_COLS); l->c1 = c1; l->c2 = c2; l->split = (u8)split;
}
static void out(const char *s, u32 c) {
    char buf[T_COLS]; int n = 0;
    for (;; s++) {
        if (*s == '\n' || *s == 0 || n == WRAP) {
            buf[n] = 0; line2(buf, c, 0, c); n = 0;
            if (*s == 0) break;
            if (*s != '\n') buf[n++] = *s;
            if (*s == '\n' && s[1] == 0) break;
        } else if (*s == '\t') { for (int i = 0; i < 4 && n < WRAP; i++) buf[n++] = ' '; }
        else buf[n++] = *s;
    }
}
static void err(const char *cmd, const char *arg, int e) {
    char b[160]; strcpy(b, cmd); strcat(b, ": ");
    if (arg) { strncpy0(b + strlen(b), arg, 60); strcat(b, ": "); }
    strcat(b, fs_strerror(e)); out(b, C_ERR);
}

/* ------------------------------------------------------------ parsing */
#define MAXARG 12
static int split_args(char *s, char **av) {
    int n = 0;
    while (*s && n < MAXARG) {
        while (*s == ' ') s++;
        if (!*s) break;
        if (*s == '"') { s++; av[n++] = s; while (*s && *s != '"') s++; }
        else { av[n++] = s; while (*s && *s != ' ') s++; }
        if (*s) *s++ = 0;
    }
    return n;
}

/* integer calculator: + - * / % ( ) */
static const char *ep; static int eerr;
static s64 e_expr(void);
static void e_ws(void) { while (*ep == ' ') ep++; }
static s64 e_factor(void) {
    e_ws();
    if (*ep == '-') { ep++; return -e_factor(); }
    if (*ep == '+') { ep++; return e_factor(); }
    if (*ep == '(') { ep++; s64 v = e_expr(); e_ws(); if (*ep == ')') ep++; else eerr = 1; return v; }
    if (*ep < '0' || *ep > '9') { eerr = 1; return 0; }
    s64 v = 0; while (*ep >= '0' && *ep <= '9') v = v * 10 + (*ep++ - '0');
    return v;
}
static s64 e_term(void) {
    s64 v = e_factor();
    for (;;) {
        e_ws(); char op = *ep;
        if (op != '*' && op != '/' && op != '%') return v;
        ep++; s64 r = e_factor();
        if (op == '*') v *= r;
        else if (r == 0) { eerr = 2; return 0; }
        else v = op == '/' ? v / r : v % r;
    }
}
static s64 e_expr(void) {
    s64 v = e_term();
    for (;;) {
        e_ws(); char op = *ep;
        if (op != '+' && op != '-') return v;
        ep++; s64 r = e_term();
        v = op == '+' ? v + r : v - r;
    }
}

/* ------------------------------------------------------------ commands */
static u32 kind_color(fnode_t *n) {
    int k = fs_kind(n);
    return k == FK_DIR ? C_DIR : k == FK_PROG ? C_OK : k == FK_IMAGE ? C_IMG : C_TXT;
}

static void ls_long(fnode_t *n) {
    char b[T_COLS], s[16], d[24];
    strcpy(b, n->type == FT_DIR ? "d" : "-");
    strcat(b, n->owner == OWN_ALICE ? "rwxr-xr-x " : "r-xr-xr-x ");
    strcat(b, n->owner == OWN_ALICE ? "alice " : "root  ");
    if (n->type == FT_DIR) strcpy(s, "-"); else fmt_size(n->size, s);
    int pad = 9 - strlen(s); while (pad-- > 0) strcat(b, " ");
    strcat(b, s); strcat(b, "  ");
    fmt_datetime(&n->mtime, d); strcat(b, d); strcat(b, "  ");
    int split = strlen(b);
    strcat(b, n->name); if (n->type == FT_DIR) strcat(b, "/");
    line2(b, C_DIM, split, kind_color(n));
}

static void cmd_ls(int ac, char **av) {
    int lng = 0, all = 0; const char *path = NULL;
    for (int i = 1; i < ac; i++) {
        if (av[i][0] == '-') { for (char *p = av[i] + 1; *p; p++) { if (*p == 'l') lng = 1; if (*p == 'a') all = 1; } }
        else path = av[i];
    }
    fnode_t *d = fs_resolve(path ? path : ".", T->cwd);
    if (!d) { err("ls", path, FS_ENOENT); return; }
    if (d->type != FT_DIR) { if (lng) ls_long(d); else line2(d->name, kind_color(d), 0, 0); return; }
    fnode_t *items[256]; int n = fs_list(d, items, 256);
    if (lng) { for (int i = 0; i < n; i++) if (all || items[i]->name[0] != '.') ls_long(items[i]); return; }
    /* short form: 4 columns; folders come first, so each line has at most one colour change */
    char b[T_COLS]; b[0] = 0; int col = 0, split = -1;
    for (int i = 0; i < n; i++) {
        if (!all && items[i]->name[0] == '.') continue;
        if (sink_cap) { sink_add(items[i]->name); continue; }
        char name[52]; strcpy(name, items[i]->name); if (items[i]->type == FT_DIR) strcat(name, "/");
        if (col == 4) { line2(b, split == 0 ? C_TXT : C_DIR, split > 0 ? split : 0, C_TXT); b[0] = 0; col = 0; split = -1; }
        if (items[i]->type != FT_DIR && split < 0) split = strlen(b);
        strcat(b, name); int pad = 22 - strlen(name); if (pad < 2) pad = 2; while (pad--) strcat(b, " ");
        col++;
    }
    if (col) line2(b, split == 0 ? C_TXT : C_DIR, split > 0 ? split : 0, C_TXT);
}

static void tree_rec(fnode_t *d, char *prefix, int depth) {
    fnode_t *items[128]; int n = fs_list(d, items, 128);
    for (int i = 0; i < n; i++) {
        if (items[i]->name[0] == '.') continue;
        int last = (i == n - 1);
        char b[T_COLS]; strncpy0(b, prefix, 60); strcat(b, last ? "`-- " : "|-- ");
        int split = strlen(b); strncpy0(b + split, items[i]->name, 40);
        line2(b, C_DIM, split, kind_color(items[i]));
        if (items[i]->type == FT_DIR && depth < 6) {
            char np[80]; strncpy0(np, prefix, 60); strcat(np, last ? "    " : "|   ");
            tree_rec(items[i], np, depth + 1);
        }
    }
}

static void uptime_str(char *o) {
    u32 s = ticks / HZ; char b[16];
    itoa(s / 3600, b); strcpy(o, b); strcat(o, "h ");
    itoa(s / 60 % 60, b); strcat(o, b); strcat(o, "m ");
    itoa(s % 60, b); strcat(o, b); strcat(o, "s");
}

static void neofetch(void) {
    static const char *art[] = {
        "        /\\        ", "       /  \\       ", "      / /\\ \\      ", "     / /__\\ \\     ",
        "    / ______ \\    ", "   /_/      \\_\\   ", "                  ", "                  ", "                  " };
    char info[9][72], b[24];
    strcpy(info[0], "alice@aliceos");
    strcpy(info[1], "-------------");
    strcpy(info[2], "OS: " OS_NAME " " OS_VERSION " i386");
    strcpy(info[3], "Kernel: written from scratch");
    strcpy(info[4], "Uptime: "); uptime_str(info[4] + 8);
    strcpy(info[5], "Resolution: "); itoa(scr_w, b); strcat(info[5], b); strcat(info[5], "x"); itoa(scr_h, b); strcat(info[5], b);
    strcpy(info[6], "CPU: "); strncpy0(info[6] + 5, cpu_brand, 60);
    strcpy(info[7], "Memory: "); fmt_size(heap_used(), b); strcat(info[7], b); strcat(info[7], " / ");
    fmt_size(mem_total_kb * 1024, b); strcat(info[7], b);
    strcpy(info[8], "Theme: "); strcat(info[8], th.dark ? "dark" : "light");
    for (int i = 0; i < 9; i++) {
        char line[T_COLS]; strcpy(line, art[i]); strcat(line, info[i]);
        line2(line, C_PUR, 18, i == 0 ? C_OK : i == 1 ? C_DIM : C_TXT);
    }
}

static int app_by_name(const char *s) {
    char n[32]; int i = 0;
    for (; s[i] && i < 31; i++) n[i] = (s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i];
    n[i] = 0;
    static const char *alias[] = { "files", "terminal", "notes", "paint", "calculator", "snake", "taskmgr", "settings", "about" };
    for (int a = 0; a < APP_COUNT; a++) if (!strcmp(n, alias[a])) return a;
    if (!strcmp(n, "calc")) return APP_CALC;
    if (!strcmp(n, "explorer")) return APP_FILES;
    return -1;
}

static void help(void) {
    out("Files (like Linux):", C_YEL);
    out("  ls [-l] [-a] [dir]   list files        cd [dir]          change folder", C_TXT);
    out("  pwd                  current folder    tree [dir]        folder tree", C_TXT);
    out("  mkdir <dir>          new folder        touch <file>      new empty file", C_TXT);
    out("  cat <file>           show a file       edit <file>       open in Notes", C_TXT);
    out("  rm [-r] <path>       delete            mv <src> <dst>    move / rename", C_TXT);
    out("  cp <src> <dst>       copy              open <app|file>   open with an app", C_TXT);
    out("  echo text > file     write to a file (>> appends; works with ls and cat too)", C_TXT);
    out("System:", C_YEL);
    out("  ps   kill <pid>   free   df   uptime   date   uname -a   whoami   neofetch", C_TXT);
    out("  calc (12+3)*4     theme dark|light     history     clear     reboot     shutdown", C_TXT);
    out("Keys: Tab completes names, Up/Down history, PgUp/PgDn scroll.", C_DIM);
}

static void run(term_t *t, char *line, window_t *win) {
    T = t;
    /* output redirection */
    char *redir = NULL; int append = 0;
    for (char *p = line; *p; p++) {
        if (*p == '"') { p++; while (*p && *p != '"') p++; if (!*p) break; continue; }
        if (*p == '>') { *p = 0; append = p[1] == '>'; redir = p + (append ? 2 : 1); break; }
    }
    char *av[MAXARG]; int ac = split_args(line, av);
    if (!ac) return;
    char target[96] = "";
    if (redir) {
        while (*redir == ' ') redir++;
        strncpy0(target, redir, sizeof(target));
        int L = strlen(target); while (L && target[L - 1] == ' ') target[--L] = 0;
        if (!L) { out("syntax error: missing file after >", C_ERR); return; }
        sink_len = 0;
        sink = kmalloc(256); sink_cap = sink ? 256 : 0; if (sink) sink[0] = 0;
    }
    const char *c = av[0];
    char b[200];
    if (!strcmp(c, "help")) help();
    else if (!strcmp(c, "clear")) { t->n = 0; t->scroll = 0; }
    else if (!strcmp(c, "echo")) {
        b[0] = 0;
        for (int i = 1; i < ac; i++) { if (i > 1) strcat(b, " "); strncpy0(b + strlen(b), av[i], 190 - strlen(b)); }
        out(b, C_TXT);
    } else if (!strcmp(c, "pwd")) { fs_path(t->cwd, b, sizeof(b)); out(b, C_TXT); }
    else if (!strcmp(c, "cd")) {
        fnode_t *d = fs_resolve(ac > 1 ? av[1] : "~", t->cwd);
        if (!d) err("cd", av[1], FS_ENOENT);
        else if (d->type != FT_DIR) err("cd", av[1], FS_ENOTDIR);
        else t->cwd = d;
    } else if (!strcmp(c, "ls") || !strcmp(c, "dir")) cmd_ls(ac, av);
    else if (!strcmp(c, "tree")) {
        fnode_t *d = fs_resolve(ac > 1 ? av[1] : ".", t->cwd);
        if (!d || d->type != FT_DIR) err("tree", ac > 1 ? av[1] : ".", d ? FS_ENOTDIR : FS_ENOENT);
        else { fs_pretty_path(d, b, sizeof(b)); out(b, C_DIR); char pre[4] = ""; tree_rec(d, pre, 0); }
    } else if (!strcmp(c, "mkdir")) {
        if (ac < 2) out("usage: mkdir <folder>", C_ERR);
        for (int i = 1; i < ac; i++) { int e = fs_mkdir(av[i], t->cwd, NULL); if (e) err("mkdir", av[i], e); }
    } else if (!strcmp(c, "touch")) {
        if (ac < 2) out("usage: touch <file>", C_ERR);
        for (int i = 1; i < ac; i++) {
            fnode_t *f; int e = fs_create(av[i], t->cwd, &f);
            if (e) err("touch", av[i], e); else clock_now(&f->mtime);
        }
    } else if (!strcmp(c, "cat")) {
        if (ac < 2) out("usage: cat <file>", C_ERR);
        for (int i = 1; i < ac; i++) {
            fnode_t *f = fs_resolve(av[i], t->cwd);
            if (!f) err("cat", av[i], FS_ENOENT);
            else if (f->type == FT_DIR) err("cat", av[i], FS_EISDIR);
            else if (fs_kind(f) == FK_IMAGE) out("cat: this is an image; use 'open' to view it", C_ERR);
            else if (f->size) {
                if (sink_cap) { char *s = (char *)f->data; u32 L = f->size; if (L && s[L - 1] == '\n') L--;
                                char *tmp = kmalloc(L + 1); if (tmp) { memcpy(tmp, s, L); tmp[L] = 0; sink_add(tmp); kfree(tmp); } }
                else out((char *)f->data, C_TXT);
            }
        }
    } else if (!strcmp(c, "rm") || !strcmp(c, "rmdir")) {
        int rec = 0, any = 0;
        for (int i = 1; i < ac; i++) {
            if (!strcmp(av[i], "-r") || !strcmp(av[i], "-rf") || !strcmp(av[i], "-fr")) { rec = 1; continue; }
            any = 1;
            fnode_t *n = fs_resolve(av[i], t->cwd);
            if (!n) { err("rm", av[i], FS_ENOENT); continue; }
            if (n->type == FT_DIR && !rec && strcmp(c, "rmdir")) { err("rm", av[i], FS_EISDIR); continue; }
            if (fs_is_ancestor(n, t->cwd)) { out("rm: refusing to remove the current folder", C_ERR); continue; }
            int e = fs_remove(n, rec); if (e) err("rm", av[i], e);
        }
        if (!any) out("usage: rm [-r] <path>", C_ERR);
    } else if (!strcmp(c, "mv") || !strcmp(c, "cp")) {
        if (ac < 3) { strcpy(b, "usage: "); strcat(b, c); strcat(b, " <source> <destination>"); out(b, C_ERR); }
        else {
            int k = (ac > 3 && av[1][0] == '-') ? 2 : 1;
            fnode_t *n = fs_resolve(av[k], t->cwd);
            if (!n) err(c, av[k], FS_ENOENT);
            else { int e = c[0] == 'm' ? fs_move(n, av[ac - 1], t->cwd) : fs_copy(n, av[ac - 1], t->cwd); if (e) err(c, av[ac - 1], e); }
        }
    } else if (!strcmp(c, "edit") || !strcmp(c, "nano")) {
        if (ac < 2) { gui_new(APP_NOTES); }
        else {
            fnode_t *f = fs_resolve(av[1], t->cwd);
            if (!f) { int e = fs_create(av[1], t->cwd, &f); if (e) { err(c, av[1], e); f = NULL; } }
            if (f && f->type == FT_DIR) err(c, av[1], FS_EISDIR);
            else if (f) notes_open(f);
        }
    } else if (!strcmp(c, "open") || !strcmp(c, "start")) {
        if (ac < 2) out("usage: open <app|file|folder>   (apps: files terminal notes paint calc snake taskmgr settings about)", C_ERR);
        else {
            fnode_t *n = fs_resolve(av[1], t->cwd);
            int a = app_by_name(av[1]);
            if (n) open_node(n);
            else if (a >= 0) gui_open(a);
            else out("open: no such application, file or folder", C_ERR);
        }
    } else if (!strcmp(c, "ps")) {
        window_t *ws[16]; int n = gui_windows(ws, 16);
        line2("  PID  NAME                  STATE       MEMORY", C_YEL, 0, 0);
        char m[16];
        strcpy(b, "    1  kernel                running     "); fmt_size(kernel_image_size(), m); strcat(b, m); out(b, C_TXT);
        strcpy(b, "    2  window-manager        running     -"); out(b, C_TXT);
        strcpy(b, "    3  ramfs                 running     "); fmt_size(fs_usage(), m); strcat(b, m); out(b, C_TXT);
        for (int i = 0; i < n; i++) {
            char p[8]; itoa(ws[i]->pid, p);
            strcpy(b, "  "); int pad = 3 - strlen(p); while (pad-- > 0) strcat(b, " "); strcat(b, p); strcat(b, "  ");
            char nm[24]; strncpy0(nm, ws[i]->title, 21); strcat(b, nm); pad = 22 - strlen(nm); while (pad-- > 0) strcat(b, " ");
            strcat(b, ws[i]->minimized ? "minimised   " : gui_focused(ws[i]) ? "focused     " : "running     ");
            fmt_size(ws[i]->mem, m); strcat(b, m);
            out(b, C_TXT);
        }
    } else if (!strcmp(c, "kill")) {
        if (ac < 2) out("usage: kill <pid>   (see ps)", C_ERR);
        else {
            int pid = 0; for (char *p = av[1]; *p >= '0' && *p <= '9'; p++) pid = pid * 10 + (*p - '0');
            if (pid >= 1 && pid <= 3) out("kill: operation not permitted (system process)", C_ERR);
            else {
                window_t *ws[16]; int n = gui_windows(ws, 16), found = 0;
                for (int i = 0; i < n; i++) if (ws[i]->pid == pid) {
                    found = 1;
                    if (ws[i] == win) out("kill: closing this terminal", C_DIM);
                    gui_close(ws[i]); break;
                }
                if (!found) out("kill: no such process", C_ERR);
                if (found && !win->visible) return;
            }
        }
    } else if (!strcmp(c, "free")) {
        char a[16], u[16], f[16];
        fmt_size(heap_total(), a); fmt_size(heap_used(), u); fmt_size(heap_total() - heap_used(), f);
        line2("            total        used        free", C_YEL, 0, 0);
        strcpy(b, "Heap:   "); strcat(b, a); int pad = 13 - strlen(a); while (pad-- > 0) strcat(b, " ");
        strcat(b, u); pad = 12 - strlen(u); while (pad-- > 0) strcat(b, " "); strcat(b, f); out(b, C_TXT);
        strcpy(b, "Kernel: "); fmt_size(kernel_image_size(), a); strcat(b, a); strcat(b, "  (code + screen buffers)"); out(b, C_TXT);
        strcpy(b, "RAM:    "); fmt_size(mem_total_kb * 1024, a); strcat(b, a); out(b, C_TXT);
    } else if (!strcmp(c, "df")) {
        char u[16], n[16]; fmt_size(fs_usage(), u); itoa(fs_count(), n);
        line2("Filesystem  Type   Used        Files   Mounted on", C_YEL, 0, 0);
        strcpy(b, "ramfs       ram    "); strcat(b, u); int pad = 12 - strlen(u); while (pad-- > 0) strcat(b, " ");
        strcat(b, n); pad = 8 - strlen(n); while (pad-- > 0) strcat(b, " "); strcat(b, "/"); out(b, C_TXT);
        out("(files live in memory and are lost at shutdown)", C_DIM);
    } else if (!strcmp(c, "date")) {
        datetime_t d; clock_now(&d); char x[24];
        static const char *wd[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
        strcpy(b, wd[d.wday]); strcat(b, " "); fmt_datetime(&d, x); strcat(b, x); strcat(b, ":");
        fmt2(d.sec, x); strcat(b, x); strcat(b, " UTC"); if (tz_offset >= 0) strcat(b, "+"); itoa(tz_offset, x); strcat(b, x);
        out(b, C_TXT);
    } else if (!strcmp(c, "uptime")) { strcpy(b, "up "); uptime_str(b + 3); out(b, C_TXT); }
    else if (!strcmp(c, "whoami")) out("alice", C_TXT);
    else if (!strcmp(c, "hostname")) out("aliceos", C_TXT);
    else if (!strcmp(c, "uname")) out(ac > 1 ? OS_NAME " aliceos " OS_VERSION " i386 (from-scratch kernel)" : OS_NAME, C_TXT);
    else if (!strcmp(c, "neofetch")) neofetch();
    else if (!strcmp(c, "calc")) {
        char expr[160] = ""; for (int i = 1; i < ac; i++) { strcat(expr, av[i]); strcat(expr, " "); }
        ep = expr; eerr = 0; s64 v = e_expr(); e_ws();
        if (eerr == 2) out("calc: division by zero", C_ERR);
        else if (eerr || *ep || ac < 2) out("usage: calc <expression>   e.g. calc (12+3)*4 - 7", C_ERR);
        else { strcpy(b, "= "); itoa(v, b + 2); out(b, C_OK); }
    } else if (!strcmp(c, "theme")) {
        if (ac > 1 && !strcmp(av[1], "dark")) { theme_apply(1, set_accent); gui_dirty(); out("theme: dark", C_OK); }
        else if (ac > 1 && !strcmp(av[1], "light")) { theme_apply(0, set_accent); gui_dirty(); out("theme: light", C_OK); }
        else out("usage: theme dark|light", C_ERR);
    } else if (!strcmp(c, "apps")) {
        for (int i = 0; i < APP_COUNT; i++) { strcpy(b, "  "); strcat(b, app_names[i]); out(b, C_DIR); }
    } else if (!strcmp(c, "history")) {
        for (int i = 0; i < t->hn; i++) { strcpy(b, "  "); itoa(i + 1, b + 2); strcat(b, "  "); strcat(b, t->hist[i]); out(b, C_DIM); }
    } else if (!strcmp(c, "exit")) { gui_close(win); }
    else if (!strcmp(c, "reboot")) sys_reboot();
    else if (!strcmp(c, "shutdown") || !strcmp(c, "poweroff")) sys_shutdown();
    else if (!strcmp(c, "sudo")) out("alice is not in the sudoers file. This incident will be reported.", C_ERR);
    else { strcpy(b, c); strcat(b, ": command not found (type 'help')"); out(b, C_ERR); }

    if (redir && win->visible) {
        char *data = sink; u32 len = sink_len;
        sink = NULL; sink_cap = 0; sink_len = 0;
        fnode_t *f; int e = fs_create(target, t->cwd, &f);
        if (e) err("redirect", target, e);
        else {
            e = append ? fs_append(f, data ? data : "", len) : fs_write(f, data ? data : "", len);
            if (e) err("redirect", target, e);
        }
        kfree(data);
    }
    if (sink) { kfree(sink); sink = NULL; } sink_cap = 0; sink_len = 0;
}

/* ------------------------------------------------------------ completion */
static void complete(term_t *t) {
    t->in[t->ilen] = 0;
    int start = t->ilen; while (start > 0 && t->in[start - 1] != ' ') start--;
    char word[96]; strcpy(word, t->in + start);
    char *slash = strrchr(word, '/');
    char dirpart[96] = "", base[64];
    if (slash) { int L = (int)(slash - word) + 1; memcpy(dirpart, word, L); dirpart[L] = 0; strncpy0(base, slash + 1, 64); }
    else strncpy0(base, word, 64);
    fnode_t *d = dirpart[0] ? fs_resolve(dirpart, t->cwd) : t->cwd;
    if (!d || d->type != FT_DIR) return;
    fnode_t *items[256]; int n = fs_list(d, items, 256), cnt = 0; fnode_t *m = NULL;
    int bl = strlen(base);
    for (int i = 0; i < n; i++) if (!strncmp(items[i]->name, base, bl) && (bl || items[i]->name[0] != '.')) { cnt++; m = items[i]; }
    if (cnt == 1) {
        char add[64]; strcpy(add, m->name + bl); if (m->type == FT_DIR) strcat(add, "/");
        if (t->ilen + strlen(add) < 90) { strcpy(t->in + t->ilen, add); t->ilen += strlen(add); }
    } else if (cnt > 1) {
        T = t; char line[T_COLS] = "";
        for (int i = 0; i < n; i++) if (!strncmp(items[i]->name, base, bl)) {
            if (strlen(line) + strlen(items[i]->name) > 90) { out(line, C_DIM); line[0] = 0; }
            strcat(line, items[i]->name); strcat(line, "  ");
        }
        if (line[0]) out(line, C_DIM);
    }
}

/* ------------------------------------------------------------ window */
static void prompt_text(term_t *t, char *b) {
    char p[96]; fs_pretty_path(t->cwd, p, sizeof(p));
    strcpy(b, "alice@aliceos:"); strcat(b, p); strcat(b, "$ ");
}

static void term_draw(window_t *w, int cx, int cy, int cw, int ch) {
    term_t *t = w->state;
    int lh = font_mono.h + 1, x = cx + 12, adv = font_mono.g[0].adv;
    int rows = (ch - 16) / lh - 1;
    if (t->scroll > t->n - rows) t->scroll = t->n - rows;
    if (t->scroll < 0) t->scroll = 0;
    int first = t->n - rows - t->scroll; if (first < 0) first = 0;
    int last = first + rows; if (last > t->n) last = t->n;
    int y = cy + 8;
    for (int i = first; i < last; i++, y += lh) {
        tline_t *l = &t->l[i];
        if (l->split) {
            char a[T_COLS]; int k; for (k = 0; k < l->split && l->t[k]; k++) a[k] = l->t[k]; a[k] = 0;
            gfx_text(x, y, a, l->c1, &font_mono);
            if (k == l->split) gfx_text(x + k * adv, y, l->t + k, l->c2, &font_mono);
        } else gfx_text(x, y, l->t, l->c1, &font_mono);
    }
    if (t->scroll) {
        gfx_text(cx + cw - 150, cy + 8, "-- scrolled --", C_DIM, &font_small);
        return;
    }
    char pr[128]; prompt_text(t, pr);
    char *colon = strrchr(pr, ':');
    *colon = 0;
    int px = gfx_text(x, y, pr, C_OK, &font_mono);
    px = gfx_text(px, y, ":", C_TXT, &font_mono);
    char *dollar = strrchr(colon + 1, '$'); *dollar = 0;
    px = gfx_text(px, y, colon + 1, C_DIR, &font_mono);
    px = gfx_text(px, y, "$ ", C_TXT, &font_mono);
    t->in[t->ilen] = 0;
    px = gfx_text(px, y, t->in, RGB(255, 255, 255), &font_mono);
    if (gui_focused(w) && (ticks / 50) % 2 == 0) gfx_fill(px + 1, y + 2, adv - 1, lh - 3, C_TXT);
}

static void term_key(window_t *w, int k) {
    term_t *t = w->state;
    T = t;
    if (k == '\n') {
        t->in[t->ilen] = 0;
        char pr[128]; prompt_text(t, pr);
        char line[T_COLS]; strncpy0(line, pr, T_COLS); strncpy0(line + strlen(line), t->in, T_COLS - strlen(line));
        int split = strlen(pr);
        line2(line, C_OK, split, C_TXT);
        if (t->ilen) {
            if (t->hn == 24) { memmove(t->hist[0], t->hist[1], sizeof(t->hist[0]) * 23); t->hn--; }
            strcpy(t->hist[t->hn++], t->in);
        }
        t->hpos = t->hn; t->scroll = 0;
        char cmd[96]; strcpy(cmd, t->in); t->ilen = 0; t->in[0] = 0;
        run(t, cmd, w);
    } else if (k == '\b') { if (t->ilen) t->ilen--; }
    else if (k == '\t') complete(t);
    else if (k == K_UP) { if (t->hpos > 0) { t->hpos--; strcpy(t->in, t->hist[t->hpos]); t->ilen = strlen(t->in); } }
    else if (k == K_DOWN) {
        if (t->hpos < t->hn - 1) { t->hpos++; strcpy(t->in, t->hist[t->hpos]); t->ilen = strlen(t->in); }
        else { t->hpos = t->hn; t->ilen = 0; }
    } else if (k == K_PGUP) t->scroll += 10;
    else if (k == K_PGDN) { t->scroll -= 10; if (t->scroll < 0) t->scroll = 0; }
    else if (k == K_CTRL('l')) { t->n = 0; t->scroll = 0; }
    else if (k == K_CTRL('c')) { t->in[t->ilen] = 0; char l[T_COLS]; strcpy(l, "^C"); line2(l, C_DIM, 0, 0); t->ilen = 0; }
    else if (k >= 32 && k < 127 && t->ilen < 90) { t->in[t->ilen++] = (char)k; t->scroll = 0; }
    gui_dirty();
}

static void term_mouse(window_t *w, int x, int y, int ev) {
    (void)x; (void)y;
    term_t *t = w->state;
    if (ev == EV_WHEEL) { t->scroll -= ev_wheel * 3; if (t->scroll < 0) t->scroll = 0; gui_dirty(); }
}

static void term_tick(window_t *w) { if (gui_focused(w) && ticks % 50 == 0) gui_dirty(); }

void term_set_cwd(window_t *w, fnode_t *dir) { ((term_t *)w->state)->cwd = dir; }

void term_setup(window_t *w) {
    w->w = 740; w->h = 460; w->bg = RGB(27, 26, 40); w->resizable = 1; w->min_w = 420; w->min_h = 220;
    w->draw = term_draw; w->key = term_key; w->tick = term_tick; w->mouse = term_mouse;
    term_t *t = kzalloc(sizeof(term_t));
    w->state = t; w->mem = sizeof(term_t);
    if (!t) return;
    t->cwd = fs_home;
    T = t;
    out(OS_NAME " " OS_VERSION " terminal - Linux-style commands on a from-scratch kernel.", C_DIM);
    out("Type 'help' to see the commands, or 'neofetch' for a system summary.", C_DIM);
    out("", C_TXT);
}
