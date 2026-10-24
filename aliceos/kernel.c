/* AliceOS - a tiny 32-bit operating system written from scratch.
 * Kernel: VGA text console, interrupts (IDT + PIC), timer, keyboard,
 * real-time clock, CPU info and a small command shell. */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define OS_NAME    "AliceOS"
#define OS_VERSION "0.1"

/* ------------------------------------------------------------ ports */
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void io_wait(void) { outb(0x80, 0); }

/* ---------------------------------------------------------- strings */
static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int startswith(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }

/* ------------------------------------------------------ VGA console */
#define VGA ((volatile u16 *)0xB8000)
#define COLS 80
#define ROWS 25
#define TOP  1                       /* row 0 is the status bar */

static int cx = 0, cy = TOP;
static u8 color = 0x0F;              /* white on black */

static void cursor_update(void) {
    u16 pos = cy * COLS + cx;
    outb(0x3D4, 14); outb(0x3D5, pos >> 8);
    outb(0x3D4, 15); outb(0x3D5, pos & 0xFF);
}

static void clear(void) {
    for (int y = TOP; y < ROWS; y++)
        for (int x = 0; x < COLS; x++) VGA[y * COLS + x] = (color << 8) | ' ';
    cx = 0; cy = TOP; cursor_update();
}

static void scroll(void) {
    for (int y = TOP; y < ROWS - 1; y++)
        for (int x = 0; x < COLS; x++) VGA[y * COLS + x] = VGA[(y + 1) * COLS + x];
    for (int x = 0; x < COLS; x++) VGA[(ROWS - 1) * COLS + x] = (color << 8) | ' ';
    cy = ROWS - 1;
}

static void putc(char c) {
    if (c == '\n') { cx = 0; cy++; }
    else if (c == '\b') {
        if (cx > 0) cx--; else if (cy > TOP) { cy--; cx = COLS - 1; }
        VGA[cy * COLS + cx] = (color << 8) | ' ';
    } else {
        VGA[cy * COLS + cx] = (color << 8) | (u8)c;
        if (++cx >= COLS) { cx = 0; cy++; }
    }
    if (cy >= ROWS) scroll();
    cursor_update();
}

static void puts(const char *s) { while (*s) putc(*s++); }
static void putsc(const char *s, u8 c) { u8 old = color; color = c; puts(s); color = old; }

static void putint(int n) {
    char b[12]; int i = 0; u32 u;
    if (n < 0) { putc('-'); u = (u32)(-n); } else u = (u32)n;
    do { b[i++] = '0' + u % 10; u /= 10; } while (u);
    while (i) putc(b[--i]);
}

static void put2(int n) { putc('0' + n / 10); putc('0' + n % 10); }

/* Write a string at a fixed position (used by the status bar). */
static void write_at(int x, int y, const char *s, u8 c) {
    while (*s && x < COLS) VGA[y * COLS + x++] = (c << 8) | (u8)*s++;
}

/* --------------------------------------------------- interrupts/IDT */
struct idt_entry { u16 off_lo, sel; u8 zero, flags; u16 off_hi; } __attribute__((packed));
struct idt_ptr   { u16 limit; u32 base; } __attribute__((packed));
struct regs { u32 edi, esi, ebp, esp, ebx, edx, ecx, eax, vec, err, eip, cs, eflags; };

static struct idt_entry idt[48];
extern u32 isr_table[48];

static void pic_remap(void) {
    outb(0x20, 0x11); io_wait(); outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait(); outb(0xA1, 0x28); io_wait();   /* vectors 32..47 */
    outb(0x21, 0x04); io_wait(); outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait(); outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xFC); outb(0xA1, 0xFF);                         /* only timer + keyboard */
}

static void idt_init(void) {
    for (int i = 0; i < 48; i++) {
        u32 a = isr_table[i];
        idt[i].off_lo = a & 0xFFFF; idt[i].off_hi = a >> 16;
        idt[i].sel = 0x08; idt[i].zero = 0; idt[i].flags = 0x8E;
    }
    struct idt_ptr p = { sizeof(idt) - 1, (u32)idt };
    __asm__ volatile("lidt %0" :: "m"(p));
}

/* ------------------------------------------------------------ timer */
#define HZ 100
static volatile u32 ticks = 0;

static void timer_init(void) {
    u32 div = 1193182 / HZ;
    outb(0x43, 0x36); outb(0x40, div & 0xFF); outb(0x40, div >> 8);
}

/* -------------------------------------------------------------- RTC */
static u8 cmos(u8 reg) { outb(0x70, reg); return inb(0x71); }
static int bcd(u8 v) { return (v & 0x0F) + (v >> 4) * 10; }

struct datetime { int sec, min, hour, day, mon, year; };
static void rtc_read(struct datetime *t) {
    while (cmos(0x0A) & 0x80) ;                  /* wait if update in progress */
    u8 b = cmos(0x0B);
    u8 s = cmos(0), m = cmos(2), h = cmos(4), d = cmos(7), mo = cmos(8), y = cmos(9);
    int isbin = b & 0x04;
    t->sec = isbin ? s : bcd(s);   t->min = isbin ? m : bcd(m);
    t->hour = isbin ? h : bcd(h);  t->day = isbin ? d : bcd(d);
    t->mon = isbin ? mo : bcd(mo); t->year = 2000 + (isbin ? y : bcd(y));
}

/* ------------------------------------------------------- status bar */
static void status_bar(void) {
    for (int x = 0; x < COLS; x++) VGA[x] = (0x1F << 8) | ' ';
    write_at(1, 0, OS_NAME " " OS_VERSION, 0x1E);
    struct datetime t; rtc_read(&t);
    char buf[32]; int i = 0;
    buf[i++] = '0' + t.hour / 10; buf[i++] = '0' + t.hour % 10; buf[i++] = ':';
    buf[i++] = '0' + t.min / 10;  buf[i++] = '0' + t.min % 10;  buf[i++] = ':';
    buf[i++] = '0' + t.sec / 10;  buf[i++] = '0' + t.sec % 10;  buf[i] = 0;
    write_at(COLS - 9, 0, buf, 0x1F);
    u32 up = ticks / HZ; i = 0;
    char ub[24] = "uptime ";  i = 7;
    char tmp[10]; int k = 0; do { tmp[k++] = '0' + up % 10; up /= 10; } while (up);
    while (k) ub[i++] = tmp[--k];
    ub[i++] = 's'; ub[i] = 0;
    write_at(34, 0, ub, 0x1B);
}

/* --------------------------------------------------------- keyboard */
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

static volatile char kbuf[64];
static volatile u8 khead = 0, ktail = 0;
static int shift = 0;

static void keyboard_irq(void) {
    u8 sc = inb(0x60);
    if (sc == 0x2A || sc == 0x36) { shift = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { shift = 0; return; }
    if (sc & 0x80) return;                       /* key release */
    char c = shift ? kms[sc] : km[sc];
    if (c) { kbuf[khead] = c; khead = (khead + 1) % 64; }
}

static char getch(void) {
    while (khead == ktail) __asm__ volatile("hlt");
    char c = kbuf[ktail]; ktail = (ktail + 1) % 64;
    return c;
}

/* --------------------------------------------- interrupt dispatcher */
static const char *exc_names[] = {
    "Division by zero", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range",
    "Invalid opcode", "Device not available", "Double fault", "Coprocessor",
    "Invalid TSS", "Segment not present", "Stack fault", "General protection fault",
    "Page fault" };

void isr_handler(struct regs *r) {
    if (r->vec < 32) {
        putsc("\n*** KERNEL PANIC: ", 0x4F);
        putsc(r->vec < 15 ? exc_names[r->vec] : "CPU exception", 0x4F);
        putsc(" ***\nSystem halted.", 0x4F);
        for (;;) __asm__ volatile("cli; hlt");
    }
    if (r->vec == 32) { ticks++; if (ticks % HZ == 0) status_bar(); }
    else if (r->vec == 33) keyboard_irq();
    if (r->vec >= 40) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

/* -------------------------------------------------------------- CPU */
static void cpu_vendor(char *out) {
    u32 a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
    u32 *o = (u32 *)out; o[0] = b; o[1] = d; o[2] = c; out[12] = 0;
}

/* ------------------------------------------------------------ shell */
static int parse_int(const char **s, int *ok) {
    while (**s == ' ') (*s)++;
    int neg = 0, v = 0, digits = 0;
    if (**s == '-') { neg = 1; (*s)++; }
    while (**s >= '0' && **s <= '9') { v = v * 10 + (**s - '0'); (*s)++; digits++; }
    *ok = digits > 0;
    return neg ? -v : v;
}

static void cmd_calc(const char *s) {
    int ok1, ok2;
    int a = parse_int(&s, &ok1);
    while (*s == ' ') s++;
    char op = *s ? *s++ : 0;
    int b = parse_int(&s, &ok2);
    if (!ok1 || !ok2 || !op) { puts("usage: calc <a> <+|-|*|/|%> <b>\n"); return; }
    if ((op == '/' || op == '%') && b == 0) { putsc("error: division by zero\n", 0x0C); return; }
    int r;
    switch (op) {
        case '+': r = a + b; break;  case '-': r = a - b; break;
        case '*': r = a * b; break;  case '/': r = a / b; break;
        case '%': r = a % b; break;
        default: puts("unknown operator\n"); return;
    }
    putsc("= ", 0x0A); putint(r); putc('\n');
}

static void cmd_time(void) {
    struct datetime t; rtc_read(&t);
    put2(t.day); putc('/'); put2(t.mon); putc('/'); putint(t.year);
    puts("  "); put2(t.hour); putc(':'); put2(t.min); putc(':'); put2(t.sec);
    puts(" (UTC)\n");
}

static void cmd_sysinfo(void) {
    char v[13]; cpu_vendor(v);
    puts("OS       : " OS_NAME " " OS_VERSION " (32-bit protected mode)\n");
    puts("CPU      : "); puts(v); putc('\n');
    puts("Display  : VGA text mode 80x25\n");
    puts("Timer    : PIT at "); putint(HZ); puts(" Hz\n");
    puts("Uptime   : "); putint(ticks / HZ); puts(" s\n");
}

static void banner(void) {
    putsc("    _    _ _          ___  ____  \n", 0x0B);
    putsc("   / \\  | (_) ___ ___/ _ \\/ ___| \n", 0x0B);
    putsc("  / _ \\ | | |/ __/ _ \\ | | \\___ \\ \n", 0x03);
    putsc(" / ___ \\| | | (_|  __/ |_| |___) |\n", 0x03);
    putsc("/_/   \\_\\_|_|\\___\\___|\\___/|____/ \n", 0x09);
    putsc("\nA tiny operating system written from scratch. Type 'help'.\n\n", 0x07);
}

static void cmd_help(void) {
    putsc("Commands:\n", 0x0E);
    puts("  help            show this list\n");
    puts("  clear           clear the screen\n");
    puts("  about           about this system\n");
    puts("  echo <text>     print text\n");
    puts("  time            current date and time\n");
    puts("  uptime          seconds since boot\n");
    puts("  sysinfo         hardware information\n");
    puts("  calc a op b     calculator (+ - * / %)\n");
    puts("  color <1-15>    change text color\n");
    puts("  panic           trigger a CPU exception (test)\n");
    puts("  reboot          restart the machine\n");
}

static void reboot(void) {
    while (inb(0x64) & 0x02) ;
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile("hlt");
}

static void run(char *line) {
    while (*line == ' ') line++;
    if (!*line) return;
    if (streq(line, "help")) cmd_help();
    else if (streq(line, "clear")) { clear(); }
    else if (streq(line, "about")) {
        puts(OS_NAME " " OS_VERSION " - bootloader, kernel and shell written from scratch\n");
        puts("in x86 assembly and C. No Linux, no libraries.\n");
    }
    else if (startswith(line, "echo")) { puts(line[4] ? line + 5 : ""); putc('\n'); }
    else if (streq(line, "time")) cmd_time();
    else if (streq(line, "uptime")) { putint(ticks / HZ); puts(" seconds\n"); }
    else if (streq(line, "sysinfo")) cmd_sysinfo();
    else if (startswith(line, "calc")) cmd_calc(line + 4);
    else if (startswith(line, "color")) {
        const char *s = line + 5; int ok; int c = parse_int(&s, &ok);
        if (!ok || c < 1 || c > 15) puts("usage: color <1-15>\n");
        else color = (u8)c;
    }
    else if (streq(line, "panic")) { __asm__ volatile("xor %%ecx,%%ecx; mov $1,%%eax; xor %%edx,%%edx; div %%ecx" ::: "eax","ecx","edx"); }
    else if (streq(line, "reboot")) reboot();
    else { putsc("unknown command: ", 0x0C); puts(line); puts("  (type 'help')\n"); }
}

static void shell(void) {
    char line[128];
    for (;;) {
        putsc("alice", 0x0A); putsc("@", 0x07); putsc("os", 0x0B); putsc(" > ", 0x07);
        int n = 0;
        for (;;) {
            char c = getch();
            if (c == '\n') { putc('\n'); break; }
            if (c == '\b') { if (n > 0) { n--; putc('\b'); } continue; }
            if (c == '\t' || c == 27) continue;
            if (n < (int)sizeof(line) - 1) { line[n++] = c; putc(c); }
        }
        line[n] = 0;
        run(line);
    }
}

/* ------------------------------------------------------------- main */
void kmain(void) {
    clear();
    status_bar();
    idt_init();
    pic_remap();
    timer_init();
    __asm__ volatile("sti");
    banner();
    putsc("[ ok ] ", 0x0A); puts("interrupts (IDT + PIC)\n");
    putsc("[ ok ] ", 0x0A); puts("timer 100 Hz\n");
    putsc("[ ok ] ", 0x0A); puts("keyboard\n");
    putsc("[ ok ] ", 0x0A); puts("real-time clock\n\n");
    shell();
}
