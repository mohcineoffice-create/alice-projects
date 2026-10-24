/* AliceOS 3 - in-memory file system with a Linux-style tree and simple ownership. */
#include "os.h"

fnode_t *fs_root, *fs_home;

static const char *errs[] = { "Success", "No such file or directory", "File exists", "Permission denied",
    "Not a directory", "Is a directory", "Directory not empty", "Invalid name", "Out of memory" };
const char *fs_strerror(int e) { return (e <= 0 && e >= -8) ? errs[-e] : "Error"; }

static fnode_t *node_new(fnode_t *parent, const char *name, u8 type, u8 owner) {
    fnode_t *n = kzalloc(sizeof(fnode_t));
    if (!n) return NULL;
    strncpy0(n->name, name, sizeof(n->name));
    n->type = type; n->owner = owner; n->parent = parent;
    clock_now(&n->mtime);
    if (parent) { n->next = parent->child; parent->child = n; clock_now(&parent->mtime); }
    return n;
}

int fs_valid_name(const char *s) {
    if (!*s || strlen(s) >= 48 || !strcmp(s, ".") || !strcmp(s, "..")) return 0;
    for (; *s; s++) if (*s == '/' || *s < 32 || *s > 126) return 0;
    return 1;
}

fnode_t *fs_child(fnode_t *dir, const char *name) {
    if (!dir || dir->type != FT_DIR) return NULL;
    for (fnode_t *c = dir->child; c; c = c->next) if (!strcmp(c->name, name)) return c;
    return NULL;
}

fnode_t *fs_resolve(const char *path, fnode_t *cwd) {
    if (!path || !*path) return cwd;
    fnode_t *n = cwd ? cwd : fs_root;
    if (path[0] == '/') { n = fs_root; path++; }
    else if (path[0] == '~' && (path[1] == '/' || !path[1])) { n = fs_home; path += path[1] ? 2 : 1; }
    char part[48];
    while (*path) {
        int i = 0;
        while (*path && *path != '/') { if (i < 47) part[i++] = *path; path++; }
        part[i] = 0;
        if (*path == '/') path++;
        if (!i || !strcmp(part, ".")) continue;
        if (!strcmp(part, "..")) { if (n->parent) n = n->parent; continue; }
        if (n->type != FT_DIR) return NULL;
        n = fs_child(n, part);
        if (!n) return NULL;
    }
    return n;
}

/* split "a/b/c" into the parent directory node and the last name */
static int split(const char *path, fnode_t *cwd, fnode_t **dir, char *name) {
    char buf[256]; strncpy0(buf, path, sizeof(buf));
    int L = strlen(buf);
    while (L > 1 && buf[L - 1] == '/') buf[--L] = 0;
    char *slash = strrchr(buf, '/');
    if (slash) {
        strncpy0(name, slash + 1, 48);
        if (slash == buf) *dir = fs_root;
        else { *slash = 0; *dir = fs_resolve(buf, cwd); }
    } else { strncpy0(name, buf, 48); *dir = (buf[0] == '~' && !buf[1]) ? NULL : cwd; }
    if (!*dir) return FS_ENOENT;
    if ((*dir)->type != FT_DIR) return FS_ENOTDIR;
    if (!fs_valid_name(name)) return FS_EINVAL;
    return FS_OK;
}

int fs_can_write(fnode_t *dir) { return dir && dir->owner == OWN_ALICE; }

int fs_mkdir(const char *path, fnode_t *cwd, fnode_t **out) {
    fnode_t *dir; char name[48];
    int e = split(path, cwd, &dir, name); if (e) return e;
    if (fs_child(dir, name)) return FS_EEXIST;
    if (!fs_can_write(dir)) return FS_EPERM;
    fnode_t *n = node_new(dir, name, FT_DIR, OWN_ALICE);
    if (!n) return FS_ENOMEM;
    if (out) *out = n;
    return FS_OK;
}

int fs_create(const char *path, fnode_t *cwd, fnode_t **out) {
    fnode_t *dir; char name[48];
    int e = split(path, cwd, &dir, name); if (e) return e;
    fnode_t *ex = fs_child(dir, name);
    if (ex) { if (ex->type == FT_DIR) return FS_EISDIR; if (out) *out = ex; return FS_OK; }
    if (!fs_can_write(dir)) return FS_EPERM;
    fnode_t *n = node_new(dir, name, FT_FILE, OWN_ALICE);
    if (!n) return FS_ENOMEM;
    if (out) *out = n;
    return FS_OK;
}

static int reserve(fnode_t *f, u32 need) {
    if (need <= f->cap) return FS_OK;
    u32 cap = f->cap ? f->cap : 64;
    while (cap < need) cap *= 2;
    u8 *d = krealloc(f->data, cap);
    if (!d) return FS_ENOMEM;
    f->data = d; f->cap = cap;
    return FS_OK;
}

int fs_write(fnode_t *f, const void *data, u32 len) {
    if (f->type != FT_FILE) return FS_EISDIR;
    if (f->owner != OWN_ALICE) return FS_EPERM;
    if (reserve(f, len + 1)) return FS_ENOMEM;
    memcpy(f->data, data, len); f->size = len; f->data[len] = 0;
    clock_now(&f->mtime);
    return FS_OK;
}

int fs_append(fnode_t *f, const void *data, u32 len) {
    if (f->type != FT_FILE) return FS_EISDIR;
    if (f->owner != OWN_ALICE) return FS_EPERM;
    if (reserve(f, f->size + len + 1)) return FS_ENOMEM;
    memcpy(f->data + f->size, data, len); f->size += len; f->data[f->size] = 0;
    clock_now(&f->mtime);
    return FS_OK;
}

static void unlink(fnode_t *n) {
    fnode_t **pp = &n->parent->child;
    while (*pp && *pp != n) pp = &(*pp)->next;
    if (*pp) *pp = n->next;
    n->next = NULL;
    clock_now(&n->parent->mtime);
}

static void free_tree(fnode_t *n) {
    fnode_t *c = n->child;
    while (c) { fnode_t *nx = c->next; free_tree(c); c = nx; }
    kfree(n->data); kfree(n);
}

int fs_remove(fnode_t *n, int recursive) {
    if (!n || n == fs_root || !n->parent) return FS_EPERM;
    if (n == fs_home || !fs_can_write(n->parent) || n->owner != OWN_ALICE) return FS_EPERM;
    if (n->type == FT_DIR && n->child && !recursive) return FS_ENOTEMPTY;
    unlink(n); free_tree(n);
    return FS_OK;
}

int fs_is_ancestor(fnode_t *a, fnode_t *b) { for (; b; b = b->parent) if (a == b) return 1; return 0; }

/* destination may be an existing directory (keep name) or a new path */
static int dest_of(fnode_t *n, const char *dest, fnode_t *cwd, fnode_t **dir, char *name) {
    fnode_t *d = fs_resolve(dest, cwd);
    if (d && d->type == FT_DIR) { *dir = d; strcpy(name, n->name); }
    else { int e = split(dest, cwd, dir, name); if (e) return e; }
    if (fs_child(*dir, name)) return FS_EEXIST;
    if (!fs_can_write(*dir)) return FS_EPERM;
    return FS_OK;
}

int fs_move(fnode_t *n, const char *dest, fnode_t *cwd) {
    if (!n || n == fs_home || n->owner != OWN_ALICE || !fs_can_write(n->parent)) return FS_EPERM;
    fnode_t *dir; char name[48];
    int e = dest_of(n, dest, cwd, &dir, name); if (e) return e;
    if (fs_is_ancestor(n, dir)) return FS_EINVAL;
    unlink(n);
    strcpy(n->name, name); n->parent = dir; n->next = dir->child; dir->child = n;
    clock_now(&dir->mtime);
    return FS_OK;
}

static fnode_t *clone(fnode_t *src, fnode_t *dir, const char *name) {
    fnode_t *n = node_new(dir, name, src->type, OWN_ALICE);
    if (!n) return NULL;
    if (src->type == FT_FILE && src->size) {
        n->data = kmalloc(src->size + 1);
        if (n->data) { memcpy(n->data, src->data, src->size); n->size = src->size; n->cap = src->size + 1; n->data[n->size] = 0; }
    }
    for (fnode_t *c = src->child; c; c = c->next) clone(c, n, c->name);
    return n;
}

int fs_copy(fnode_t *n, const char *dest, fnode_t *cwd) {
    if (!n) return FS_ENOENT;
    fnode_t *dir; char name[48];
    int e = dest_of(n, dest, cwd, &dir, name); if (e) return e;
    if (fs_is_ancestor(n, dir)) return FS_EINVAL;
    return clone(n, dir, name) ? FS_OK : FS_ENOMEM;
}

int fs_rename(fnode_t *n, const char *newname) {
    if (!fs_valid_name(newname)) return FS_EINVAL;
    if (!n || n == fs_home || n->owner != OWN_ALICE || !fs_can_write(n->parent)) return FS_EPERM;
    if (fs_child(n->parent, newname)) return FS_EEXIST;
    strcpy(n->name, newname);
    return FS_OK;
}

void fs_path(fnode_t *n, char *out, int max) {
    if (n == fs_root) { strncpy0(out, "/", max); return; }
    char tmp[256]; tmp[0] = 0;
    for (; n && n != fs_root; n = n->parent) {
        char seg[256]; strcpy(seg, "/"); strcat(seg, n->name);
        if (strlen(seg) + strlen(tmp) >= (int)sizeof(tmp) - 1) break;
        strcat(seg, tmp); strcpy(tmp, seg);
    }
    strncpy0(out, tmp, max);
}

void fs_pretty_path(fnode_t *n, char *out, int max) {
    if (fs_is_ancestor(fs_home, n)) {
        char full[256], home[64]; fs_path(n, full, sizeof(full)); fs_path(fs_home, home, sizeof(home));
        char t[256] = "~"; strcat(t, full + strlen(home));
        strncpy0(out, t, max);
    } else fs_path(n, out, max);
}

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int name_cmp(const char *a, const char *b) {
    while (*a && lower(*a) == lower(*b)) { a++; b++; }
    return lower((u8)*a) - lower((u8)*b);
}

int fs_list(fnode_t *dir, fnode_t **out, int max) {
    int n = 0;
    if (!dir || dir->type != FT_DIR) return 0;
    for (fnode_t *c = dir->child; c && n < max; c = c->next) out[n++] = c;
    for (int i = 1; i < n; i++) {                       /* insertion sort */
        fnode_t *k = out[i]; int j = i - 1;
        while (j >= 0 && ((out[j]->type != k->type) ? out[j]->type > k->type : name_cmp(out[j]->name, k->name) > 0)) {
            out[j + 1] = out[j]; j--;
        }
        out[j + 1] = k;
    }
    return n;
}

static u32 usage_of(fnode_t *n) { u32 s = n->size; for (fnode_t *c = n->child; c; c = c->next) s += usage_of(c); return s; }
static u32 count_of(fnode_t *n) { u32 s = 1; for (fnode_t *c = n->child; c; c = c->next) s += count_of(c); return s; }
u32 fs_usage(void) { return usage_of(fs_root); }
u32 fs_count(void) { return count_of(fs_root); }

int fs_kind(fnode_t *n) {
    if (n->type == FT_DIR) return FK_DIR;
    if (n->parent && n->parent->parent == fs_root && !strcmp(n->parent->name, "bin")) return FK_PROG;
    if (ends_with(n->name, ".aimg")) return FK_IMAGE;
    if (ends_with(n->name, ".txt") || ends_with(n->name, ".md") || ends_with(n->name, ".conf") ||
        ends_with(n->name, ".c") || ends_with(n->name, ".log") || !strrchr(n->name, '.')) return FK_TEXT;
    return FK_OTHER;
}

/* ------------------------------------------------------------------ setup */
static fnode_t *mk(fnode_t *p, const char *name, u8 owner) { return node_new(p, name, FT_DIR, owner); }
static void mkfile(fnode_t *p, const char *name, const char *text, u8 owner) {
    fnode_t *f = node_new(p, name, FT_FILE, OWN_ALICE);
    if (!f) return;
    fs_write(f, text, strlen(text));
    f->owner = owner;
}

void fs_init(void) {
    fs_root = mk(NULL, "", OWN_ROOT);
    fnode_t *bin = mk(fs_root, "bin", OWN_ROOT);
    static const char *cmds[] = { "ls", "cd", "pwd", "mkdir", "touch", "cat", "rm", "mv", "cp", "tree", "echo",
        "ps", "kill", "free", "df", "neofetch", "calc", "open", "clear", "date", "uname", "whoami" };
    for (u32 i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) mkfile(bin, cmds[i], "#!aliceos builtin\n", OWN_ROOT);
    fnode_t *etc = mk(fs_root, "etc", OWN_ROOT);
    mkfile(etc, "hostname", "aliceos\n", OWN_ROOT);
    mkfile(etc, "os-release", "NAME=\"AliceOS\"\nVERSION=\"" OS_VERSION "\"\nID=aliceos\n"
                              "PRETTY_NAME=\"AliceOS " OS_VERSION "\"\n", OWN_ROOT);
    mkfile(etc, "motd", "Welcome to AliceOS! Type 'help' to see the commands.\n", OWN_ROOT);
    fnode_t *home = mk(fs_root, "home", OWN_ROOT);
    fs_home = mk(home, "alice", OWN_ALICE);
    mk(fs_home, "Desktop", OWN_ALICE);
    fnode_t *docs = mk(fs_home, "Documents", OWN_ALICE);
    mk(fs_home, "Pictures", OWN_ALICE);
    fnode_t *tmp = mk(fs_root, "tmp", OWN_ALICE);
    (void)tmp;
    mkfile(docs, "welcome.txt",
        "Welcome to AliceOS 3.0!\n\n"
        "AliceOS mixes ideas from Windows and Linux:\n"
        "- a desktop, a file explorer and a task manager, like Windows\n"
        "- a terminal with ls, cd, cat, mkdir, rm ... like Linux\n\n"
        "Your files live in /home/alice. System folders (/bin, /etc)\n"
        "belong to root and cannot be changed.\n\n"
        "Note: files are kept in memory, so they disappear when you\n"
        "turn the computer off.\n", OWN_ALICE);
    mkfile(docs, "todo.txt", "- Try the terminal: ls, tree, cat welcome.txt\n- Draw something in Paint and save it\n"
                             "- Change the wallpaper in Settings\n", OWN_ALICE);
    mkfile(fs_home, "readme.md", "# Home\n\nThis is your home folder (~).\n", OWN_ALICE);
}

/* is this node still part of the tree? (it may have been deleted elsewhere) */
static int alive_in(fnode_t *d, fnode_t *n) {
    if (d == n) return 1;
    for (fnode_t *c = d->child; c; c = c->next) if (alive_in(c, n)) return 1;
    return 0;
}
int fs_alive(fnode_t *n) { return n && alive_in(fs_root, n); }
