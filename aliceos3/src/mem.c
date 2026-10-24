/* AliceOS 3 - kernel heap: first-fit allocator with block splitting and merging. */
#include "os.h"

extern char kernel_start[], kernel_end[];

typedef struct block {
    u32 size;                 /* payload size in bytes */
    u32 free;
    struct block *next;
    u32 magic;
} block_t;
#define MAGIC 0xA11CE05u
#define HDR   ((u32)sizeof(block_t))
#define ALIGN16(n) (((n) + 15u) & ~15u)

static block_t *head = NULL;
static u32 total = 0, used = 0;

u32 kernel_image_size(void) { return (u32)(kernel_end - kernel_start); }

void heap_init(void) {
    u32 start = ALIGN16((u32)kernel_end);
    u32 top = mem_total_kb ? mem_total_kb * 1024u : start + 32u * 1024 * 1024;
    if (top > 0xF0000000u) top = 0xF0000000u;           /* stay below device memory */
    if (top - start > 512u * 1024 * 1024) top = start + 512u * 1024 * 1024;
    if (top <= start + 4 * 1024 * 1024) top = start + 4 * 1024 * 1024;
    head = (block_t *)start;
    head->size = top - start - HDR; head->free = 1; head->next = NULL; head->magic = MAGIC;
    total = head->size; used = 0;
}

void *kmalloc(u32 n) {
    if (!n) n = 1;
    n = ALIGN16(n);
    for (block_t *b = head; b; b = b->next) {
        if (!b->free || b->size < n) continue;
        if (b->size >= n + HDR + 64) {                   /* split */
            block_t *nb = (block_t *)((u8 *)b + HDR + n);
            nb->size = b->size - n - HDR; nb->free = 1; nb->next = b->next; nb->magic = MAGIC;
            b->size = n; b->next = nb;
            used += HDR;
        }
        b->free = 0; used += b->size;
        return (u8 *)b + HDR;
    }
    return NULL;
}

void *kzalloc(u32 n) { void *p = kmalloc(n); if (p) memset(p, 0, n); return p; }

void kfree(void *p) {
    if (!p) return;
    block_t *b = (block_t *)((u8 *)p - HDR);
    if (b->magic != MAGIC || b->free) return;           /* ignore bad or double free */
    b->free = 1; used -= b->size;
    /* merge every run of adjacent free blocks */
    for (block_t *c = head; c; c = c->next) {
        while (c->free && c->next && c->next->free && (u8 *)c + HDR + c->size == (u8 *)c->next) {
            c->size += HDR + c->next->size; used -= HDR;
            c->next = c->next->next;
        }
    }
}

void *krealloc(void *p, u32 n) {
    if (!p) return kmalloc(n);
    block_t *b = (block_t *)((u8 *)p - HDR);
    if (b->size >= n) return p;
    void *q = kmalloc(n);
    if (!q) return NULL;
    memcpy(q, p, b->size);
    kfree(p);
    return q;
}

u32 heap_used(void) { return used; }
u32 heap_total(void) { return total; }
