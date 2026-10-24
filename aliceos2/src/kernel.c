/* AliceOS 2 - kernel: boot info, interrupts, timer, clock, keyboard, mouse, system calls. */
#include "os.h"

volatile u32 ticks = 0;
u32 mem_total_kb = 0;
char cpu_vendor[13];
char cpu_brand[49];

/* ------------------------------------------------------------ libc-ish */
void *memset(void *d, int c, u32 n) {
    u8 *p = d; while (n--) *p++ = (u8)c; return d;
}
void *memcpy(void *d, const void *s, u32 n) {
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) :: "memory");
    return d;
}
int strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (u8)*a - (u8)*b; }
int strncmp(const char *a, const char *b, int n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (u8)*a - (u8)*b : 0;
}
void strcpy(char *d, const char *s) { while ((*d++ = *s++)) ; }
void strcat(char *d, const char *s) { strcpy(d + strlen(d), s); }
void itoa(s64 v, char *out) {
    char b[24]; int i = 0; u64 u;
    if (v < 0) { *out++ = '-'; u = (u64)(-v); } else u = (u64)v;
    do { b[i++] = '0' + (char)(u % 10); u /= 10; } while (u);
    while (i) *out++ = b[--i];
    *out = 0;
}
void fmt2(int v, char *o) { o[0] = '0' + v / 10 % 10; o[1] = '0' + v % 10; o[2] = 0; }

static u32 seed = 12345;
u32 rand32(void) { seed = seed * 1103515245u + 12345u + ticks; return seed >> 8; }

/* ---------------------------------------------------------------- IDT */
struct idt_entry { u16 lo, sel; u8 zero, flags; u16 hi; } __attribute__((packed));
struct idt_ptr { u16 limit; u32 base; } __attribute__((packed));
struct regs { u32 edi, esi, ebp, esp, ebx, edx, ecx, eax, vec, err, eip, cs, eflags; };
static struct idt_entry idt[48];
extern u32 isr_table[48];

static void io_wait(void) { outb(0x80, 0); }

static void pic_init(void) {
    outb(0x20, 0x11); io_wait(); outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait(); outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait(); outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait(); outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xF8);             /* timer, keyboard, cascade */
    outb(0xA1, 0xEF);             /* IRQ12 mouse */
}

static void idt_init(void) {
    u16 cs; __asm__ volatile("mov %%cs,%0" : "=r"(cs));
    for (int i = 0; i < 48; i++) {
        idt[i].lo = isr_table[i] & 0xFFFF; idt[i].hi = isr_table[i] >> 16;
        idt[i].sel = cs; idt[i].zero = 0; idt[i].flags = 0x8E;
    }
    struct idt_ptr p = { sizeof(idt) - 1, (u32)idt };
    __asm__ volatile("lidt %0" :: "m"(p));
}

/* -------------------------------------------------------------- timer */
static void pit_init(void) {
    u32 d = 1193182 / HZ;
    outb(0x43, 0x36); outb(0x40, d & 0xFF); outb(0x40, d >> 8);
}

/* ---------------------------------------------------------------- RTC */
static u8 cmos(u8 r) { outb(0x70, r); return inb(0x71); }
static int bcd(u8 v) { return (v & 15) + (v >> 4) * 10; }
void rtc_read(datetime_t *t) {
    while (cmos(0x0A) & 0x80) ;
    u8 b = cmos(0x0B), bin = b & 4;
    u8 s = cmos(0), m = cmos(2), h = cmos(4), d = cmos(7), mo = cmos(8), y = cmos(9);
    int pm = h & 0x80; h &= 0x7F;
    t->sec = bin ? s : bcd(s); t->min = bin ? m : bcd(m); t->hour = bin ? h : bcd(h);
    t->day = bin ? d : bcd(d); t->mon = bin ? mo : bcd(mo); t->year = 2000 + (bin ? y : bcd(y));
    if (!(b & 2) && pm) t->hour = (t->hour % 12) + 12;
    /* day of week (Sakamoto), 0 = Sunday */
    static const int tt[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int yy = t->year - (t->mon < 3);
    t->wday = (yy + yy / 4 - yy / 100 + yy / 400 + tt[(t->mon - 1) % 12] + t->day) % 7;
}

/* ----------------------------------------------------------- keyboard */
static const char km[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ' };
static const char kms[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0,'|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ' };

static volatile int kq[64];
static volatile u32 kq_h = 0, kq_t = 0;
static int shift = 0, caps = 0, ext = 0;

static void kpush(int k) { u32 n = (kq_h + 1) % 64; if (n != kq_t) { kq[kq_h] = k; kq_h = n; } }
int key_get(void) {
    if (kq_h == kq_t) return -1;
    int k = kq[kq_t]; kq_t = (kq_t + 1) % 64; return k;
}

static void keyboard_irq(void) {
    u8 sc = inb(0x60);
    if (sc == 0xE0) { ext = 1; return; }
    if (ext) {
        ext = 0;
        if (sc & 0x80) return;
        switch (sc) {
            case 0x48: kpush(K_UP); break;   case 0x50: kpush(K_DOWN); break;
            case 0x4B: kpush(K_LEFT); break; case 0x4D: kpush(K_RIGHT); break;
            case 0x47: kpush(K_HOME); break; case 0x4F: kpush(K_END); break;
            case 0x53: kpush(K_DEL); break;  case 0x1C: kpush('\n'); break;
            case 0x35: kpush('/'); break;
        }
        return;
    }
    if (sc == 0x2A || sc == 0x36) { shift = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { shift = 0; return; }
    if (sc == 0x3A) { caps = !caps; return; }
    if (sc & 0x80) return;
    /* numeric keypad */
    static const char kp[] = { '7','8','9','-','4','5','6','+','1','2','3','0','.' };
    if (sc >= 0x47 && sc <= 0x53) { kpush(kp[sc - 0x47]); return; }
    char c = shift ? kms[sc] : km[sc];
    if (caps && c >= 'a' && c <= 'z' && !shift) c -= 32;
    else if (caps && c >= 'A' && c <= 'Z' && shift) c += 32;
    if (c) kpush(c);
}

/* -------------------------------------------------------------- mouse */
static volatile mouse_pkt_t mq[32];
static volatile u32 mq_h = 0, mq_t = 0;
static u8 mbytes[3]; static int mcycle = 0;

int mouse_get(mouse_pkt_t *p) {
    if (mq_h == mq_t) return 0;
    p->dx = mq[mq_t].dx; p->dy = mq[mq_t].dy; p->buttons = mq[mq_t].buttons;
    mq_t = (mq_t + 1) % 32; return 1;
}

static u32 mlast = 0;
static void mouse_irq(void) {
    if ((inb(0x64) & 0x21) != 0x21) return;          /* no mouse byte waiting */
    u8 b = inb(0x60);
    if (ticks - mlast > 2) mcycle = 0;                /* bytes of one packet arrive together */
    mlast = ticks;
    if (mcycle == 0 && !(b & 0x08)) return;          /* resync */
    mbytes[mcycle++] = b;
    if (mcycle < 3) return;
    mcycle = 0;
    if (mbytes[0] & 0xC0) return;                    /* overflow */
    int dx = (int)mbytes[1] - ((mbytes[0] << 4) & 0x100);
    int dy = (int)mbytes[2] - ((mbytes[0] << 3) & 0x100);
    u32 n = (mq_h + 1) % 32;
    if (n == mq_t) {                                  /* full: merge into last */
        u32 last = (mq_h + 31) % 32;
        mq[last].dx += dx; mq[last].dy -= dy; mq[last].buttons = mbytes[0] & 7;
        return;
    }
    mq[mq_h].dx = dx; mq[mq_h].dy = -dy; mq[mq_h].buttons = mbytes[0] & 7;
    mq_h = n;
}

static void ps2_wait_w(void) { for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) ; }
static void ps2_wait_r(void) { for (int i = 0; i < 100000 && !(inb(0x64) & 1); i++) ; }
static void mouse_cmd(u8 v) { ps2_wait_w(); outb(0x64, 0xD4); ps2_wait_w(); outb(0x60, v); ps2_wait_r(); inb(0x60); }

static void mouse_init(void) {
    ps2_wait_w(); outb(0x64, 0xA8);                  /* enable aux port */
    ps2_wait_w(); outb(0x64, 0x20); ps2_wait_r();
    u8 cfg = inb(0x60);
    cfg |= 0x02; cfg &= ~0x20;                        /* IRQ12 on, mouse clock on */
    ps2_wait_w(); outb(0x64, 0x60); ps2_wait_w(); outb(0x60, cfg);
    mouse_cmd(0xF6);                                  /* defaults */
    mouse_cmd(0xF3); mouse_cmd(100);                  /* 100 samples/s */
    mouse_cmd(0xF4);                                  /* enable streaming */
}

/* ------------------------------------------------------------ dispatch */
static const char *exc[] = { "Division by zero", "Debug", "NMI", "Breakpoint", "Overflow",
    "Bound range", "Invalid opcode", "Device not available", "Double fault", "Coprocessor",
    "Invalid TSS", "Segment not present", "Stack fault", "General protection fault", "Page fault" };

static void panic(const char *msg, u32 eip) {
    gfx_reset_clip();
    gfx_fill(0, 0, scr_w, scr_h, RGB(20, 24, 60));
    gfx_text(80, 120, ":(", RGB(255, 255, 255), &font_huge);
    gfx_text(80, 220, "AliceOS ran into a problem and stopped.", RGB(255, 255, 255), &font_title);
    char buf[64] = "Error: "; strcat(buf, msg);
    gfx_text(80, 280, buf, RGB(255, 200, 200), &font_ui_b);
    char hex[16] = "EIP: 0x"; const char *d = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++) hex[7 + i] = d[(eip >> (28 - 4 * i)) & 15];
    hex[15] = 0;
    gfx_text(80, 306, hex, RGB(200, 200, 230), &font_mono);
    gfx_text(80, 350, "Restart the computer to continue.", RGB(200, 200, 230), &font_ui);
    gfx_present();
    for (;;) __asm__ volatile("cli; hlt");
}

void isr_handler(struct regs *r) {
    if (r->vec < 32) panic(r->vec < 15 ? exc[r->vec] : "CPU exception", r->eip);
    switch (r->vec) {
        case 32: ticks++; break;
        case 33: { u8 st = inb(0x64); if ((st & 1) && !(st & 0x20)) keyboard_irq(); } break;
        case 44: mouse_irq(); break;
    }
    if (r->vec >= 40) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

/* ---------------------------------------------------------------- CPU */
static void cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf));
}
static void cpu_detect(void) {
    u32 a, b, c, d;
    cpuid(0, &a, &b, &c, &d);
    ((u32 *)cpu_vendor)[0] = b; ((u32 *)cpu_vendor)[1] = d; ((u32 *)cpu_vendor)[2] = c; cpu_vendor[12] = 0;
    cpuid(0x80000000, &a, &b, &c, &d);
    if (a >= 0x80000004) {
        u32 *p = (u32 *)cpu_brand;
        for (u32 l = 0; l < 3; l++) { cpuid(0x80000002 + l, &a, &b, &c, &d); p[l*4] = a; p[l*4+1] = b; p[l*4+2] = c; p[l*4+3] = d; }
        cpu_brand[48] = 0;
        char *s = cpu_brand; while (*s == ' ') s++;   /* trim */
        if (s != cpu_brand) strcpy(cpu_brand, s);
    } else strcpy(cpu_brand, cpu_vendor);
}

/* ------------------------------------------------------------- power */
void sys_reboot(void) {
    __asm__ volatile("cli");
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) ;
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile("hlt");
}
void sys_shutdown(void) {
    outw(0x604, 0x2000);      /* QEMU */
    outw(0xB004, 0x2000);     /* Bochs, old QEMU */
    outw(0x4004, 0x3400);     /* VirtualBox */
    gfx_reset_clip();
    gfx_fill(0, 0, scr_w, scr_h, 0);
    gfx_text_center(0, scr_h / 2 - 12, scr_w, "It is now safe to turn off your computer.", RGB(255, 170, 60), &font_title);
    gfx_present();
    for (;;) __asm__ volatile("cli; hlt");
}

/* --------------------------------------------------------------- main */
static void text_error(const char *s) {
    volatile u16 *v = (u16 *)0xB8000;
    for (int i = 0; s[i]; i++) v[i] = 0x4F00 | (u8)s[i];
    for (;;) __asm__ volatile("cli; hlt");
}

void kmain(u32 magic, u32 *mb) {
    if (magic != 0x2BADB002) text_error("AliceOS: not booted by a Multiboot loader.");
    u32 flags = mb[0];
    if (flags & 1) mem_total_kb = mb[1] + mb[2] + 1024;
    if (!(flags & (1 << 12))) text_error("AliceOS: no graphics framebuffer (need VBE/GOP).");
    u8 *m = (u8 *)mb;
    u32 fb = *(u32 *)(m + 88), pitch = *(u32 *)(m + 96), w = *(u32 *)(m + 100), h = *(u32 *)(m + 104);
    u8 bpp = m[108];
    if (!gfx_init(fb, pitch, w, h, bpp)) text_error("AliceOS: unsupported screen mode.");

    cpu_detect();
    idt_init();
    pic_init();
    pit_init();
    mouse_init();
    while (inb(0x64) & 1) inb(0x60);                 /* flush */
    __asm__ volatile("sti");
    seed ^= mem_total_kb * 2654435761u;
    gui_run();
}
