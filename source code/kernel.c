#include <stdint.h>
#include <stddef.h>

extern char _bss_start[];
extern char _bss_end[];

/* ================================================================
 * Порты ввода-вывода
 * ================================================================ */
static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p));
}
static inline uint8_t inb(uint16_t p) {
    uint8_t r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(p));
    return r;
}
static inline void outw(uint16_t p, uint16_t v) {
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(p));
}
static inline uint16_t inw(uint16_t p) {
    uint16_t r;
    __asm__ volatile("inw %1, %0" : "=a"(r) : "Nd"(p));
    return r;
}

/* ================================================================
 * VGA + цвета
 * ================================================================ */
#define VGA ((volatile uint16_t*)0xB8000)
#define COLS 80
#define ROWS 25

#define BLACK         0
#define BLUE          1
#define GREEN         2
#define CYAN          3
#define RED           4
#define MAGENTA       5
#define BROWN         6
#define LIGHT_GRAY    7
#define DARK_GRAY     8
#define LIGHT_BLUE    9
#define LIGHT_GREEN   10
#define LIGHT_CYAN    11
#define LIGHT_RED     12
#define LIGHT_MAGENTA 13
#define YELLOW        14
#define WHITE         15
#define ATTR(fg, bg) ((uint8_t)(((bg) << 4) | ((fg) & 0x0F)))

static int cx = 0, cy = 0;
static uint8_t color = ATTR(LIGHT_GRAY, BLACK);
static int config_clock_enabled = 1;
static uint8_t config_fg = LIGHT_GRAY;
static uint8_t config_bg = BLACK;
static uint8_t clock_last_sec = 255;

static void update_cursor(void) {
    uint16_t pos = (uint16_t)(cy * COLS + cx);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)(pos >> 8));
}

static void clear_screen(void) {
    for (int i = 0; i < COLS * ROWS; i++)
        VGA[i] = (color << 8) | ' ';
    cx = cy = 0;
    clock_last_sec = 255;
    update_cursor();
}

static void scroll(void) {
    if (cy < ROWS) return;
    for (int i = 0; i < (ROWS - 1) * COLS; i++) VGA[i] = VGA[i + COLS];
    for (int i = (ROWS - 1) * COLS; i < ROWS * COLS; i++)
        VGA[i] = (color << 8) | ' ';
    cy = ROWS - 1;
}

static void putc_(char c) {
    if (c == '\n') { cx = 0; cy++; }
    else if (c == '\r') { cx = 0; }
    else if (c == '\b') {
        if (cx > 0) { cx--; VGA[cy*COLS+cx] = (color<<8)|' '; }
        else if (cy > 0) { cy--; cx = COLS-1; VGA[cy*COLS+cx] = (color<<8)|' '; }
    } else {
        VGA[cy*COLS+cx] = (color<<8) | (uint8_t)c;
        if (++cx >= COLS) { cx = 0; cy++; }
    }
    scroll();
    update_cursor();
}

static void print(const char *s) { while (*s) putc_(*s++); }
static int strlen_(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void set_color(uint8_t fg, uint8_t bg) { color = ATTR(fg, bg); }
static void reset_color(void)                  { color = ATTR(LIGHT_GRAY, BLACK); }

static void print_ok(const char *s)   { set_color(LIGHT_GREEN, BLACK); print(s); reset_color(); }
static void print_err(const char *s)  { set_color(LIGHT_RED, BLACK);   print(s); reset_color(); }
static void print_warn(const char *s) { set_color(YELLOW, BLACK);      print(s); reset_color(); }
static void print_info(const char *s) { set_color(LIGHT_CYAN, BLACK);  print(s); reset_color(); }
static void print_dim(const char *s)  { set_color(DARK_GRAY, BLACK);   print(s); reset_color(); }

static void print_dec(uint32_t v) {
    char buf[11];
    int i = 0;
    if (v == 0) { putc_('0'); return; }
    while (v && i < 10) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i--) putc_(buf[i]);
}

static void print_hex32(uint32_t v) {
    const char *h = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) putc_(h[(v >> i) & 0xF]);
}

/* ================================================================
 * CMOS RTC
 * ================================================================ */
static uint8_t cmos_read(uint8_t reg) { outb(0x70, reg); return inb(0x71); }
static void cmos_wait_ready(void)     { while (cmos_read(0x0A) & 0x80); }
static uint8_t bcd_to_bin(uint8_t b)  { return (uint8_t)(((b >> 4) & 0x0F) * 10 + (b & 0x0F)); }

typedef struct {
    uint8_t second, minute, hour, day, month;
    uint16_t year;
} rtc_time_t;

static void rtc_read(rtc_time_t *t) {
    cmos_wait_ready();
    t->second = cmos_read(0x00);
    t->minute = cmos_read(0x02);
    t->hour   = cmos_read(0x04);
    t->day    = cmos_read(0x07);
    t->month  = cmos_read(0x08);
    t->year   = cmos_read(0x09);

    uint8_t status_b = cmos_read(0x0B);
    if (!(status_b & 0x04)) {
        t->second = bcd_to_bin(t->second);
        t->minute = bcd_to_bin(t->minute);
        t->hour   = (uint8_t)(((t->hour & 0x0F) + ((t->hour >> 4) & 0x07) * 10)
                              | (t->hour & 0x80));
        t->day    = bcd_to_bin(t->day);
        t->month  = bcd_to_bin(t->month);
        t->year   = (uint16_t)bcd_to_bin((uint8_t)t->year);
    }
    if (!(status_b & 0x02)) {
        int pm = (t->hour & 0x80) != 0;
        t->hour &= 0x7F;
        if (pm && t->hour != 12) t->hour += 12;
        if (!pm && t->hour == 12) t->hour = 0;
    }
    t->year += 2000;
}

static void draw_clock_force(void) {
     if (!config_clock_enabled) return;

    rtc_time_t t;
    rtc_read(&t);
    clock_last_sec = t.second;
    int base = 71;
    uint16_t attr = (uint16_t)(ATTR(BLACK, LIGHT_GRAY) << 8);
    VGA[base + 0] = attr | ' ';
    VGA[base + 1] = attr | (uint8_t)('0' + (t.hour   / 10) % 10);
    VGA[base + 2] = attr | (uint8_t)('0' + (t.hour   % 10));
    VGA[base + 3] = attr | ':';
    VGA[base + 4] = attr | (uint8_t)('0' + (t.minute / 10) % 10);
    VGA[base + 5] = attr | (uint8_t)('0' + (t.minute % 10));
    VGA[base + 6] = attr | ':';
    VGA[base + 7] = attr | (uint8_t)('0' + (t.second / 10) % 10);
    VGA[base + 8] = attr | (uint8_t)('0' + (t.second % 10));
}

static void draw_clock(void) {
    if (!config_clock_enabled) return;
    uint8_t s = cmos_read(0x00);
    if (s != clock_last_sec) draw_clock_force();
}

static void print_2digits(uint8_t n) { if (n < 10) putc_('0'); print_dec(n); }
static void print_date(void) {
    rtc_time_t t; rtc_read(&t);
    print_2digits(t.day); putc_('.');
    print_2digits(t.month); putc_('.');
    print_dec(t.year);
}
static void print_time(void) {
    rtc_time_t t; rtc_read(&t);
    print_2digits(t.hour);   putc_(':');
    print_2digits(t.minute); putc_(':');
    print_2digits(t.second);
}

/* ================================================================
 * IDT + RSOD
 * ================================================================ */
typedef struct {
    uint16_t offset_low, selector;
    uint8_t  zero, type_attr;
    uint16_t offset_high;
} __attribute__((packed)) idt_entry_t;

typedef struct { uint16_t limit; uint32_t base; } __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt[256];
static idt_ptr_t   idtp;

extern void idt_load(idt_ptr_t *);
extern uint32_t isr_stub_table[32];

static void idt_set_gate(int n, uint32_t handler) {
    idt[n].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[n].selector    = 0x08;
    idt[n].zero        = 0;
    idt[n].type_attr   = 0x8E;
    idt[n].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
}

static void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;
    for (int i = 0; i < 256; i++) {
        idt[i].offset_low = idt[i].selector = idt[i].zero = idt[i].type_attr = idt[i].offset_high = 0;
    }
    for (int i = 0; i < 32; i++) idt_set_gate(i, isr_stub_table[i]);
    idt_load(&idtp);
}

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} __attribute__((packed)) regs_t;

static const char *exception_names[32] = {
    "Divide by zero", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound range exceeded", "Invalid opcode",
    "Device not available", "Double fault", "Coprocessor overrun",
    "Invalid TSS", "Segment not present", "Stack-segment fault",
    "General protection fault", "Page fault", "Reserved",
    "x87 FPU error", "Alignment check", "Machine check",
    "SIMD FPU error", "Virtualization", "Control protection",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved"
};

void isr_handler(regs_t *r) {
    __asm__ volatile("cli");
    volatile uint16_t *vga = (volatile uint16_t*)0xB8000;
    int pos = 0;
    for (int i = 0; i < 80 * 25; i++) vga[i] = (0x4F << 8) | ' ';
    const char *n = (r->int_no < 32) ? exception_names[r->int_no] : "Unknown";
    while (*n) vga[pos++] = (0x4F << 8) | (uint8_t)*n++;
    vga[pos++] = (0x4F << 8) | ' ';
    vga[pos++] = (0x4F << 8) | '(';
    vga[pos++] = (0x4F << 8) | (uint8_t)('0' + (r->int_no / 10) % 10);
    vga[pos++] = (0x4F << 8) | (uint8_t)('0' + r->int_no % 10);
    vga[pos++] = (0x4F << 8) | ')';
    cx = 0; cy = 2; color = 0x4F;
    print("EIP = "); print_hex32(r->eip);       putc_('\n');
    print("CS  = "); print_hex32(r->cs);        putc_('\n');
    print("ERR = "); print_hex32(r->err_code);  putc_('\n');
    print("EAX = "); print_hex32(r->eax);       putc_('\n');
    print("EBX = "); print_hex32(r->ebx);       putc_('\n');
    print("ESP = "); print_hex32(r->esp_dummy); putc_('\n');
    print("\nSystem halted. Reboot manually.\n");
    for (;;) __asm__ volatile("hlt");
}

/* ================================================================
 * Строки
 * ================================================================ */
static int strcmp_(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static const char *skip_ws(const char *s) { while (*s == ' ') s++; return s; }
static const char *next_token(const char *s, char *out, int maxlen) {
    s = skip_ws(s);
    int i = 0;
    while (*s && *s != ' ' && i < maxlen - 1) out[i++] = *s++;
    out[i] = 0;
    return s;
}
static int parse_name(const char *s, char out[11]) {
    s = skip_ws(s);
    if (!*s) return 0;
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0;
    while (*s && *s != '.' && *s != ' ' && i < 8) {
        char c = *s++;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        out[i++] = c;
    }
    if (*s == '.') {
        s++; i = 8;
        while (*s && *s != ' ' && i < 11) {
            char c = *s++;
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            out[i++] = c;
        }
    }
    return 1;
}

/* ================================================================
 * Переменные окружения и флаги
 * ================================================================ */
#define ENV_MAX 8
static char env_names[ENV_MAX][16];
static char env_values[ENV_MAX][64];
static int  env_count = 0;


static int  echo_enabled = 1;
static char prompt_fmt[64] = "$P$G";

static const char *env_get(const char *name) {
    for (int i = 0; i < env_count; i++)
        if (strcmp_(env_names[i], name) == 0) return env_values[i];
    return 0;
}

static void env_set(const char *name, const char *value) {
    for (int i = 0; i < env_count; i++) {
        if (strcmp_(env_names[i], name) == 0) {
            int j = 0;
            while (value[j] && j < 63) { env_values[i][j] = value[j]; j++; }
            env_values[i][j] = 0;
            return;
        }
    }
    if (env_count >= ENV_MAX) return;
    int i = env_count++;
    int j = 0;
    while (name[j] && j < 15) { env_names[i][j] = name[j]; j++; }
    env_names[i][j] = 0;
    j = 0;
    while (value[j] && j < 63) { env_values[i][j] = value[j]; j++; }
    env_values[i][j] = 0;
}

/* ================================================================
 * Клавиатура
 * ================================================================ */
#define KEY_UP    0x100
#define KEY_DOWN  0x101
#define KEY_LEFT  0x102
#define KEY_RIGHT 0x103
#define KEY_HOME  0x104
#define KEY_END   0x105
#define KEY_DEL   0x106
#define KEY_PGUP  0x107
#define KEY_PGDN  0x108
#define KEY_F2    0x109
#define KEY_F10   0x10A

static const char keymap[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' '
};
static const char keymap_shift[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' '
};

static int shift_pressed = 0;

static char getchar_(void) {
    uint32_t tick = 0;
    for (;;) {
        if (inb(0x64) & 1) {
            uint8_t sc = inb(0x60);

            if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; continue; }
            if (sc == 0xAA || sc == 0xB6) { shift_pressed = 0; continue; }

            if (sc < 128) {
                char c = shift_pressed ? keymap_shift[sc] : keymap[sc];
                if (c) return c;
            }
        }
        if (++tick > 500000) { tick = 0; draw_clock(); }
    }
}

static uint32_t getkey(void) {
    uint32_t tick = 0;
    for (;;) {
        if (!(inb(0x64) & 1)) {
            if (++tick > 500000) { tick = 0; draw_clock(); }
            continue;
        }
        tick = 0;
        uint8_t sc = inb(0x60);

        /* Shift (нажат/отпущен) */
        if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; continue; }
        if (sc == 0xAA || sc == 0xB6) { shift_pressed = 0; continue; }

        if (sc & 0x80) continue;

        if (sc == 0xE0) {
            while (!(inb(0x64) & 1)) { }
            uint8_t ext = inb(0x60);
            if (ext & 0x80) continue;
            switch (ext) {
                case 0x48: return KEY_UP;
                case 0x50: return KEY_DOWN;
                case 0x4B: return KEY_LEFT;
                case 0x4D: return KEY_RIGHT;
                case 0x47: return KEY_HOME;
                case 0x4F: return KEY_END;
                case 0x53: return KEY_DEL;
                case 0x49: return KEY_PGUP;
                case 0x51: return KEY_PGDN;
            }
            continue;
        }

        if (sc == 0x3C) return KEY_F2;
        if (sc == 0x44) return KEY_F10;

        if (sc < 128) {
            char c = shift_pressed ? keymap_shift[sc] : keymap[sc];
            if (c) return (uint32_t)c;
        }
    }
}

/* ================================================================
 * ATA PIO
 * ================================================================ */
#define ATA 0x1F0
static void ata_wait_bsy(void) { while (inb(ATA + 7) & 0x80); }
static void ata_wait_drq(void) { while (!(inb(ATA + 7) & 0x08)); }

static void ata_read(uint32_t lba, uint8_t *buf) {
    ata_wait_bsy();
    outb(ATA + 6, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA + 2, 1);
    outb(ATA + 3, (uint8_t)(lba & 0xFF));
    outb(ATA + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA + 7, 0x20);
    ata_wait_drq();
    for (int i = 0; i < 256; i++) {
        uint16_t w = inw(ATA);
        buf[i*2] = (uint8_t)(w & 0xFF);
        buf[i*2+1] = (uint8_t)(w >> 8);
    }
}

static void ata_write(uint32_t lba, const uint8_t *buf) {
    ata_wait_bsy();
    outb(ATA + 6, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA + 2, 1);
    outb(ATA + 3, (uint8_t)(lba & 0xFF));
    outb(ATA + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA + 7, 0x30);
    ata_wait_drq();
    for (int i = 0; i < 256; i++) {
        uint16_t w = (uint16_t)buf[i*2] | ((uint16_t)buf[i*2+1] << 8);
        outw(ATA, w);
    }
    outb(ATA + 7, 0xE7);
    ata_wait_bsy();
}

/* ================================================================
 * Файловая система
 * ================================================================ */
#define FS_BITMAP_LBA 400
#define FS_ROOT_LBA   401
#define FS_DATA_START 500
#define FS_MAX_SECTOR 4096
#define DIR_ENTRIES   16
#define ATTR_FILE     0x00
#define ATTR_DIR      0x10
#define MAX_FILE      4096

typedef struct {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint32_t size;
    uint32_t lba;
    uint16_t blocks;
    uint8_t  reserved[10];
} __attribute__((packed)) dirent_t;

static uint8_t bitmap[512];
static uint8_t dir_buf[512];
static uint8_t file_buf[MAX_FILE];

static uint32_t cur_dir_lba = FS_ROOT_LBA;
static char cur_dir_name[8] = {' ',' ',' ',' ',' ',' ',' ',' '};

#define MAX_DEPTH 8
static uint32_t dir_stack_lba[MAX_DEPTH];
static char     dir_stack_name[MAX_DEPTH][8];
static int      dir_sp = 0;

static void bitmap_set(uint32_t lba) { bitmap[lba >> 3] |=  (uint8_t)(1 << (lba & 7)); }
static void bitmap_clr(uint32_t lba) { bitmap[lba >> 3] &= (uint8_t)~(1 << (lba & 7)); }
static int  bitmap_get(uint32_t lba) { return (bitmap[lba >> 3] >> (lba & 7)) & 1; }
static void save_bitmap(void)        { ata_write(FS_BITMAP_LBA, bitmap); }

static int alloc_chain(uint32_t n) {
    if (n == 0) return -1;
    for (uint32_t start = FS_DATA_START; start + n <= FS_MAX_SECTOR; start++) {
        int ok = 1;
        for (uint32_t i = 0; i < n; i++) if (bitmap_get(start + i)) { ok = 0; break; }
        if (ok) {
            for (uint32_t i = 0; i < n; i++) bitmap_set(start + i);
            save_bitmap();
            return (int)start;
        }
    }
    return -1;
}

static void free_chain(uint32_t lba, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) bitmap_clr(lba + i);
    save_bitmap();
}

static void write_chain(uint32_t lba, const uint8_t *buf, uint32_t size) {
    uint32_t sectors = (size + 511) / 512;
    uint8_t sector[512];
    for (uint32_t s = 0; s < sectors; s++) {
        uint32_t off = s * 512;
        uint32_t n = size - off;
        if (n > 512) n = 512;
        for (uint32_t i = 0; i < n; i++) sector[i] = buf[off + i];
        for (uint32_t i = n; i < 512; i++) sector[i] = 0;
        ata_write(lba + s, sector);
    }
}

static void read_chain(uint32_t lba, uint8_t *buf, uint32_t size) {
    uint32_t sectors = (size + 511) / 512;
    uint8_t sector[512];
    for (uint32_t s = 0; s < sectors; s++) {
        ata_read(lba + s, sector);
        uint32_t off = s * 512;
        uint32_t n = size - off;
        if (n > 512) n = 512;
        for (uint32_t i = 0; i < n; i++) buf[off + i] = sector[i];
    }
}

static void fs_init(void) {
    ata_read(FS_BITMAP_LBA, bitmap);
    for (uint32_t l = 0; l <= FS_ROOT_LBA; l++) bitmap_set(l);
    save_bitmap();
}

static void load_dir(void) { ata_read(cur_dir_lba, dir_buf); }
static void save_dir(void) { ata_write(cur_dir_lba, dir_buf); }

static int find_entry(const char name11[11]) {
    dirent_t *d = (dirent_t*)dir_buf;
    for (int i = 0; i < DIR_ENTRIES; i++) {
        if (d[i].name[0] == 0 || d[i].name[0] == ' ') continue;
        int eq = 1;
        for (int j = 0; j < 11; j++)
            if (((char*)&d[i])[j] != name11[j]) { eq = 0; break; }
        if (eq) return i * 32;
    }
    return -1;
}

static int create_entry(const char name11[11], uint8_t attr,
                        uint32_t size, uint32_t lba, uint16_t blocks) {
    dirent_t *d = (dirent_t*)dir_buf;
    for (int i = 0; i < DIR_ENTRIES; i++) {
        if (d[i].name[0] == 0 || d[i].name[0] == ' ') {
            for (int j = 0; j < 11; j++) ((char*)&d[i])[j] = name11[j];
            d[i].attr = attr; d[i].size = size; d[i].lba = lba; d[i].blocks = blocks;
            for (int j = 0; j < 10; j++) d[i].reserved[j] = 0;
            return i * 32;
        }
    }
    return -1;
}

static void delete_entry_at(int off) {
    for (int i = 0; i < 32; i++) dir_buf[off + i] = 0;
}

/* ================================================================
 * Отображение
 * ================================================================ */
static int name_trim_len(const char *n, int max) {
    int len = max;
    while (len > 0 && n[len-1] == ' ') len--;
    return len;
}

/* Приглашение с поддержкой шаблона $P $G $L $N $T */
static void print_prompt_var(char t) {
    if (t == 'P' || t == 'p') {
        putc_('A'); putc_(':'); putc_('\\');
        for (int i = 0; i < dir_sp; i++) {
            int empty = 1;
            for (int k = 0; k < 8; k++) {
                if (dir_stack_name[i][k] != ' ') { empty = 0; break; }
            }
            if (empty) continue;
            int ln = name_trim_len(dir_stack_name[i], 8);
            for (int k = 0; k < ln; k++) putc_(dir_stack_name[i][k]);
            putc_('\\');
        }
        int ln2 = name_trim_len(cur_dir_name, 8);
        for (int k = 0; k < ln2; k++) putc_(cur_dir_name[k]);
    } else if (t == 'G' || t == 'g') {
        putc_('>');
    } else if (t == 'L' || t == 'l') {
        putc_('<');
    } else if (t == 'N' || t == 'n') {
        putc_('A');
    } else if (t == 'T' || t == 't') {
        rtc_time_t tm;
        rtc_read(&tm);
        print_2digits(tm.hour);   putc_(':');
        print_2digits(tm.minute); putc_(':');
        print_2digits(tm.second);
    } else if (t == '$') {
        putc_('$');
    } else {
        putc_(t);
    }
}

static void print_prompt(void) {
    draw_clock();
    set_color(LIGHT_GREEN, BLACK);
    for (int i = 0; prompt_fmt[i]; i++) {
        char c = prompt_fmt[i];
        if (c == '$' && prompt_fmt[i + 1] != 0) {
            print_prompt_var(prompt_fmt[i + 1]);
            i++;
        } else {
            putc_(c);
        }
    }
    putc_(' ');
    reset_color();
}
static void print_dirent(const dirent_t *e) {
    if (e->attr & ATTR_DIR) {
        set_color(LIGHT_BLUE, BLACK);
        int len = name_trim_len(e->name, 8);
        for (int i = 0; i < len; i++) putc_(e->name[i]);
        for (int i = len; i < 12; i++) putc_(' ');
        print("<DIR>");
        reset_color();
        putc_('\n');
    } else {
        set_color(WHITE, BLACK);
        int len = name_trim_len(e->name, 8);
        for (int i = 0; i < len; i++) putc_(e->name[i]);
        int xlen = name_trim_len(e->ext, 3);
        if (xlen > 0) {
            set_color(DARK_GRAY, BLACK); putc_('.'); reset_color();
            set_color(LIGHT_MAGENTA, BLACK);
            for (int i = 0; i < xlen; i++) putc_(e->ext[i]);
            reset_color();
        }
        int shown = len + (xlen ? 1 + xlen : 0);
        for (int i = shown; i < 12; i++) putc_(' ');
        set_color(LIGHT_CYAN, BLACK);
        print_dec(e->size);
        print(" bytes");
        reset_color();
        putc_('\n');
    }
}

/* ================================================================
 * Редактор
 * ================================================================ */
#define EDIT_MAX_LINES 100
#define EDIT_LINE_LEN  80
#define EDIT_VIEW_ROWS 22

static char edit_buf[EDIT_MAX_LINES][EDIT_LINE_LEN];
static int  edit_lines, edit_cur_line, edit_cur_col, edit_top;
static char edit_filename[32];
static char edit_name11[11];
static int  edit_dirty;

static int edit_load(void) {
    edit_lines = 1; edit_cur_line = 0; edit_cur_col = 0;
    edit_top = 0; edit_dirty = 0; edit_buf[0][0] = 0;
    load_dir();
    int off = find_entry(edit_name11);
    if (off < 0) return 0;
    dirent_t *d = (dirent_t*)(dir_buf + off);
    if (d->attr & ATTR_DIR) return -1;
    uint32_t sz = d->size;
    if (sz == 0) return 0;
    if (sz > MAX_FILE) sz = MAX_FILE;
    read_chain(d->lba, file_buf, sz);
    int line = 0, col = 0;
    for (uint32_t i = 0; i < sz; i++) {
        char c = (char)file_buf[i];
        if (c == '\r') continue;
        if (c == '\n') {
            edit_buf[line][col] = 0; line++;
            if (line >= EDIT_MAX_LINES) { line = EDIT_MAX_LINES - 1; break; }
            col = 0; edit_buf[line][0] = 0;
        } else if (c == '\t') {
            if (col < EDIT_LINE_LEN - 1) edit_buf[line][col++] = ' ';
            while ((col % 4) && col < EDIT_LINE_LEN - 1) edit_buf[line][col++] = ' ';
        } else {
            if (col < EDIT_LINE_LEN - 1) edit_buf[line][col++] = c;
        }
    }
    edit_buf[line][col] = 0;
    edit_lines = line + 1;
    if (edit_lines > EDIT_MAX_LINES) edit_lines = EDIT_MAX_LINES;
    return 1;
}

static int edit_save(void) {
    uint32_t p = 0;
    for (int i = 0; i < edit_lines && p < MAX_FILE - 1; i++) {
        int L = strlen_(edit_buf[i]);
        for (int j = 0; j < L && p < MAX_FILE - 1; j++)
            file_buf[p++] = (uint8_t)edit_buf[i][j];
        if (p < MAX_FILE - 1) file_buf[p++] = '\n';
    }
    if (p == 0) { file_buf[0] = '\n'; p = 1; }
    uint32_t need = (p + 511) / 512;
    if (need == 0) need = 1;
    load_dir();
    int off = find_entry(edit_name11);
    uint32_t old_lba = 0, old_blocks = 0;
    if (off >= 0) {
        dirent_t *d = (dirent_t*)(dir_buf + off);
        old_lba = d->lba; old_blocks = d->blocks;
    }
    int new_lba = alloc_chain(need);
    if (new_lba < 0) return -1;
    write_chain((uint32_t)new_lba, file_buf, p);
    if (off >= 0) {
        dirent_t *d = (dirent_t*)(dir_buf + off);
        d->size = p; d->lba = (uint32_t)new_lba; d->blocks = (uint16_t)need;
        free_chain(old_lba, old_blocks);
    } else {
        if (create_entry(edit_name11, ATTR_FILE, p, (uint32_t)new_lba, (uint16_t)need) < 0) {
            free_chain((uint32_t)new_lba, need); return -1;
        }
    }
    save_dir();
    return 1;
}

static void edit_draw(void) {
    int base = 0;
    for (int i = 0; i < 80; i++) VGA[base + i] = (ATTR(WHITE, BLUE) << 8) | ' ';
    const char *t1 = " MyOS Editor  ";
    int p = 0;
    while (t1[p]) { VGA[base + p] = (ATTR(WHITE, BLUE) << 8) | (uint8_t)t1[p]; p++; }
    int f = 0;
    while (edit_filename[f]) { VGA[base + p] = (ATTR(YELLOW, BLUE) << 8) | (uint8_t)edit_filename[f]; p++; f++; }
    if (edit_dirty) {
        VGA[base + p] = (ATTR(LIGHT_RED, BLUE) << 8) | ' ';
        VGA[base + p + 1] = (ATTR(LIGHT_RED, BLUE) << 8) | '*';
    } else {
        VGA[base + p] = (ATTR(WHITE, BLUE) << 8) | ' ';
        VGA[base + p + 1] = (ATTR(WHITE, BLUE) << 8) | ' ';
    }
    const char *hint = "F2 Save  F10 Exit";
    int hl = strlen_(hint);
    for (int i = 0; i < hl; i++)
        VGA[base + 80 - hl + i] = (ATTR(WHITE, BLUE) << 8) | (uint8_t)hint[i];

    for (int r = 0; r < EDIT_VIEW_ROWS; r++) {
        int line = edit_top + r;
        int row_base = (1 + r) * COLS;
        if (line >= edit_lines) {
            VGA[row_base] = (ATTR(DARK_GRAY, BLACK) << 8) | '~';
            for (int i = 1; i < 80; i++) VGA[row_base + i] = (ATTR(BLACK, BLACK) << 8) | ' ';
        } else {
            const char *s = edit_buf[line];
            int L = strlen_(s);
            for (int i = 0; i < 80; i++) {
                uint8_t ch = (i < L) ? (uint8_t)s[i] : (uint8_t)' ';
                VGA[row_base + i] = (ATTR(LIGHT_GRAY, BLACK) << 8) | ch;
            }
        }
    }
    int sbase = 24 * COLS;
    for (int i = 0; i < 80; i++) VGA[sbase + i] = (ATTR(BLACK, CYAN) << 8) | ' ';
    p = 0;
    const char *s1 = " Ln ";
    for (int i = 0; s1[i]; i++) VGA[sbase + p++] = (ATTR(BLACK, CYAN) << 8) | (uint8_t)s1[i];
    char num[12];
    int n = edit_cur_line + 1, ni = 0;
    if (n == 0) num[ni++] = '0';
    while (n) { num[ni++] = (char)('0' + (n % 10)); n /= 10; }
    while (ni--) VGA[sbase + p++] = (ATTR(BLACK, CYAN) << 8) | (uint8_t)num[ni];
    const char *s2 = "  Col ";
    for (int i = 0; s2[i]; i++) VGA[sbase + p++] = (ATTR(BLACK, CYAN) << 8) | (uint8_t)s2[i];
    n = edit_cur_col + 1; ni = 0;
    if (n == 0) num[ni++] = '0';
    while (n) { num[ni++] = (char)('0' + (n % 10)); n /= 10; }
    while (ni--) VGA[sbase + p++] = (ATTR(BLACK, CYAN) << 8) | (uint8_t)num[ni];
    const char *s3 = "  Lines ";
    for (int i = 0; s3[i]; i++) VGA[sbase + p++] = (ATTR(BLACK, CYAN) << 8) | (uint8_t)s3[i];
    n = edit_lines; ni = 0;
    if (n == 0) num[ni++] = '0';
    while (n) { num[ni++] = (char)('0' + (n % 10)); n /= 10; }
    while (ni--) VGA[sbase + p++] = (ATTR(BLACK, CYAN) << 8) | (uint8_t)num[ni];
    if (edit_dirty) {
        const char *mod = "[modified]";
        int ml = strlen_(mod);
        for (int i = 0; i < ml; i++)
            VGA[sbase + 80 - ml + i] = (ATTR(LIGHT_RED, CYAN) << 8) | (uint8_t)mod[i];
    }
    cx = edit_cur_col;
    cy = 1 + (edit_cur_line - edit_top);
    update_cursor();
    reset_color();
}

static void edit_run(void) {
    for (;;) {
        edit_draw();
        uint32_t key = getkey();
        if (key == KEY_F10) return;
        if (key == KEY_F2) { if (edit_save() > 0) edit_dirty = 0; continue; }
        if (key == KEY_UP) {
            if (edit_cur_line > 0) edit_cur_line--;
            if (edit_cur_line < edit_top) edit_top = edit_cur_line;
            int L = strlen_(edit_buf[edit_cur_line]);
            if (edit_cur_col > L) edit_cur_col = L;
        }
        else if (key == KEY_DOWN) {
            if (edit_cur_line < edit_lines - 1) edit_cur_line++;
            if (edit_cur_line >= edit_top + EDIT_VIEW_ROWS)
                edit_top = edit_cur_line - EDIT_VIEW_ROWS + 1;
            int L = strlen_(edit_buf[edit_cur_line]);
            if (edit_cur_col > L) edit_cur_col = L;
        }
        else if (key == KEY_LEFT) {
            if (edit_cur_col > 0) edit_cur_col--;
            else if (edit_cur_line > 0) {
                edit_cur_line--; edit_cur_col = strlen_(edit_buf[edit_cur_line]);
                if (edit_cur_line < edit_top) edit_top = edit_cur_line;
            }
        }
        else if (key == KEY_RIGHT) {
            int L = strlen_(edit_buf[edit_cur_line]);
            if (edit_cur_col < L) edit_cur_col++;
            else if (edit_cur_line < edit_lines - 1) {
                edit_cur_line++; edit_cur_col = 0;
                if (edit_cur_line >= edit_top + EDIT_VIEW_ROWS)
                    edit_top = edit_cur_line - EDIT_VIEW_ROWS + 1;
            }
        }
        else if (key == KEY_HOME) edit_cur_col = 0;
        else if (key == KEY_END)  edit_cur_col = strlen_(edit_buf[edit_cur_line]);
        else if (key == KEY_PGUP) {
            if (edit_top >= EDIT_VIEW_ROWS) edit_top -= EDIT_VIEW_ROWS; else edit_top = 0;
            if (edit_cur_line > edit_top + EDIT_VIEW_ROWS - 1)
                edit_cur_line = edit_top + EDIT_VIEW_ROWS - 1;
            if (edit_cur_line < edit_top) edit_cur_line = edit_top;
            int L = strlen_(edit_buf[edit_cur_line]);
            if (edit_cur_col > L) edit_cur_col = L;
        }
        else if (key == KEY_PGDN) {
            int max_top = edit_lines - EDIT_VIEW_ROWS;
            if (max_top < 0) max_top = 0;
            if (edit_top + EDIT_VIEW_ROWS <= max_top) edit_top += EDIT_VIEW_ROWS;
            else edit_top = max_top;
            if (edit_cur_line < edit_top) edit_cur_line = edit_top;
            if (edit_cur_line > edit_top + EDIT_VIEW_ROWS - 1)
                edit_cur_line = edit_top + EDIT_VIEW_ROWS - 1;
            int L = strlen_(edit_buf[edit_cur_line]);
            if (edit_cur_col > L) edit_cur_col = L;
        }
        else if (key == KEY_DEL) {
            int L = strlen_(edit_buf[edit_cur_line]);
            if (edit_cur_col < L) {
                for (int i = edit_cur_col; i < L; i++)
                    edit_buf[edit_cur_line][i] = edit_buf[edit_cur_line][i + 1];
                edit_dirty = 1;
            }
        }
        else if (key == 8) {
            if (edit_cur_col > 0) {
                for (int i = edit_cur_col - 1; i < EDIT_LINE_LEN - 1; i++)
                    edit_buf[edit_cur_line][i] = edit_buf[edit_cur_line][i + 1];
                edit_cur_col--; edit_dirty = 1;
            } else if (edit_cur_line > 0) {
                int prev_L = strlen_(edit_buf[edit_cur_line - 1]);
                int cur_L  = strlen_(edit_buf[edit_cur_line]);
                if (prev_L + cur_L < EDIT_LINE_LEN - 1) {
                    for (int i = 0; i < cur_L; i++)
                        edit_buf[edit_cur_line - 1][prev_L + i] = edit_buf[edit_cur_line][i];
                    edit_buf[edit_cur_line - 1][prev_L + cur_L] = 0;
                    for (int i = edit_cur_line; i < edit_lines - 1; i++)
                        for (int j = 0; j < EDIT_LINE_LEN; j++)
                            edit_buf[i][j] = edit_buf[i + 1][j];
                    edit_lines--; edit_cur_line--; edit_cur_col = prev_L;
                    if (edit_cur_line < edit_top) edit_top = edit_cur_line;
                    edit_dirty = 1;
                }
            }
        }
        else if (key == '\n') {
            if (edit_lines >= EDIT_MAX_LINES) continue;
            int L = strlen_(edit_buf[edit_cur_line]);
            for (int i = edit_lines; i > edit_cur_line + 1; i--)
                for (int j = 0; j < EDIT_LINE_LEN; j++)
                    edit_buf[i][j] = edit_buf[i - 1][j];
            int p = 0;
            for (int i = edit_cur_col; i <= L && p < EDIT_LINE_LEN - 1; i++)
                edit_buf[edit_cur_line + 1][p++] = edit_buf[edit_cur_line][i];
            edit_buf[edit_cur_line + 1][p] = 0;
            edit_buf[edit_cur_line][edit_cur_col] = 0;
            edit_lines++; edit_cur_line++; edit_cur_col = 0;
            if (edit_cur_line >= edit_top + EDIT_VIEW_ROWS)
                edit_top = edit_cur_line - EDIT_VIEW_ROWS + 1;
            edit_dirty = 1;
        }
        else if (key == '\t') {
            int L = strlen_(edit_buf[edit_cur_line]);
            if (L + 4 < EDIT_LINE_LEN - 1) {
                for (int i = L; i >= edit_cur_col; i--)
                    edit_buf[edit_cur_line][i + 4] = edit_buf[edit_cur_line][i];
                for (int i = 0; i < 4; i++)
                    edit_buf[edit_cur_line][edit_cur_col + i] = ' ';
                edit_cur_col += 4; edit_dirty = 1;
            }
        }
        else if (key >= 32 && key < 127) {
            int L = strlen_(edit_buf[edit_cur_line]);
            if (L >= EDIT_LINE_LEN - 2) continue;
            for (int i = L; i >= edit_cur_col; i--)
                edit_buf[edit_cur_line][i + 1] = edit_buf[edit_cur_line][i];
            edit_buf[edit_cur_line][edit_cur_col] = (char)key;
            edit_cur_col++; edit_dirty = 1;
        }
    }
}

static void cmd_edit(const char *arg) {
    const char *p = skip_ws(arg);
    if (!*p) { print_err("Error: no filename\n"); return; }
    if (!parse_name(p, edit_name11)) { print_err("Error: bad name\n"); return; }
    int i = 0;
    while (p[i] && p[i] != ' ' && i < 31) { edit_filename[i] = p[i]; i++; }
    edit_filename[i] = 0;
    int r = edit_load();
    if (r < 0) { print_err("Error: is a directory\n"); return; }
    edit_run();
    clear_screen();
}
/* ================================================================
 * Команды ФС
 * ================================================================ */
static void cmd_dir(void) {
    load_dir();
    dirent_t *d = (dirent_t*)dir_buf;
    set_color(CYAN, BLACK);
    print("\n Directory of A:\\\n\n");
    reset_color();
    for (int i = 0; i < DIR_ENTRIES; i++) {
        if (d[i].name[0] == 0 || d[i].name[0] == ' ') continue;
        print_dirent(&d[i]);
    }
    putc_('\n');
}

static void cmd_mkdir(const char *arg) {
    char name11[11];
    if (!parse_name(arg, name11)) { print_err("Error: no name\n"); return; }
    load_dir();
    if (find_entry(name11) >= 0) { print_err("Error: exists\n"); return; }
    int lba = alloc_chain(1);
    if (lba < 0) { print_err("Error: no space\n"); return; }
    uint8_t zero[512];
    for (int i = 0; i < 512; i++) zero[i] = 0;
    ata_write((uint32_t)lba, zero);
    if (create_entry(name11, ATTR_DIR, 0, (uint32_t)lba, 1) < 0) {
        print_err("Error: dir full\n"); free_chain((uint32_t)lba, 1); return;
    }
    save_dir();
    print_ok("OK\n");
}

static void cmd_touch(const char *arg) {
    char name11[11];
    if (!parse_name(arg, name11)) { print_err("Error: no name\n"); return; }
    load_dir();
    if (find_entry(name11) >= 0) { print_err("Error: exists\n"); return; }
    int lba = alloc_chain(1);
    if (lba < 0) { print_err("Error: no space\n"); return; }
    uint8_t zero[512];
    for (int i = 0; i < 512; i++) zero[i] = 0;
    ata_write((uint32_t)lba, zero);
    if (create_entry(name11, ATTR_FILE, 0, (uint32_t)lba, 1) < 0) {
        print_err("Error: dir full\n"); free_chain((uint32_t)lba, 1); return;
    }
    save_dir();
    print_ok("OK\n");
}

static void cmd_rm(const char *arg) {
    char name11[11];
    if (!parse_name(arg, name11)) { print_err("Error: no name\n"); return; }
    load_dir();
    int off = find_entry(name11);
    if (off < 0) { print_err("Error: not found\n"); return; }
    dirent_t *d = (dirent_t*)(dir_buf + off);
    free_chain(d->lba, d->blocks);
    delete_entry_at(off);
    save_dir();
    print_ok("OK\n");
}

static void cmd_type(const char *arg) {
    char name11[11];
    if (!parse_name(arg, name11)) { print_err("Error: no name\n"); return; }
    load_dir();
    int off = find_entry(name11);
    if (off < 0) { print_err("Error: not found\n"); return; }
    dirent_t *d = (dirent_t*)(dir_buf + off);
    if (d->attr & ATTR_DIR) { print_err("Error: is a directory\n"); return; }
    uint32_t sz = d->size;
    if (sz > MAX_FILE) sz = MAX_FILE;
    if (sz == 0) { putc_('\n'); return; }
    read_chain(d->lba, file_buf, sz);
    set_color(LIGHT_GRAY, BLACK);
    for (uint32_t i = 0; i < sz; i++) putc_((char)file_buf[i]);
    reset_color();
    if (file_buf[sz-1] != '\n') putc_('\n');
}

static void cmd_write(const char *arg) {
    char name[64];
    arg = next_token(arg, name, sizeof(name));
    if (!name[0]) { print_err("Error: no name\n"); return; }
    char name11[11];
    if (!parse_name(name, name11)) { print_err("Error: no name\n"); return; }
    const char *rest = skip_ws(arg);
    uint32_t tlen = 0;
    if (*rest) {
        while (*rest && tlen < MAX_FILE - 1) file_buf[tlen++] = (uint8_t)*rest++;
        file_buf[tlen++] = '\n';
    } else {
        print_info("Enter text. Empty line to finish.\n");
        char line[128];
        for (;;) {
            set_color(DARK_GRAY, BLACK); print("| "); reset_color();
            int len = 0;
            for (;;) {
                char c = getchar_();
                if (c == '\n') { line[len] = 0; putc_('\n'); break; }
                if (c == '\b') { if (len > 0) { len--; putc_('\b'); } }
                else if (c >= ' ' && c < 127) { if (len < 127) { line[len++] = c; putc_(c); } }
            }
            if (len == 0) break;
            for (int i = 0; i < len && tlen < MAX_FILE - 1; i++)
                file_buf[tlen++] = (uint8_t)line[i];
            if (tlen < MAX_FILE - 1) file_buf[tlen++] = '\n';
            if (tlen >= MAX_FILE - 1) { print_warn("[file size limit reached]\n"); break; }
        }
    }
    if (tlen == 0) { print_err("Error: no text\n"); return; }
    uint32_t need = (tlen + 511) / 512;
    if (need == 0) need = 1;
    load_dir();
    int off = find_entry(name11);
    uint32_t old_lba = 0, old_blocks = 0;
    if (off >= 0) {
        dirent_t *d = (dirent_t*)(dir_buf + off);
        old_lba = d->lba; old_blocks = d->blocks;
    }
    int new_lba = alloc_chain(need);
    if (new_lba < 0) { print_err("Error: no space\n"); return; }
    write_chain((uint32_t)new_lba, file_buf, tlen);
    if (off >= 0) {
        dirent_t *d = (dirent_t*)(dir_buf + off);
        d->size = tlen; d->lba = (uint32_t)new_lba; d->blocks = (uint16_t)need;
        free_chain(old_lba, old_blocks);
    } else {
        if (create_entry(name11, ATTR_FILE, tlen, (uint32_t)new_lba, (uint16_t)need) < 0) {
            print_err("Error: dir full\n"); free_chain((uint32_t)new_lba, need); return;
        }
    }
    save_dir();
    set_color(LIGHT_GREEN, BLACK); print("OK"); reset_color();
    print(" (");
    set_color(LIGHT_CYAN, BLACK); print_dec(tlen); reset_color();
    print(" bytes, ");
    set_color(LIGHT_CYAN, BLACK); print_dec(need); reset_color();
    print(" sectors)\n");
}

static void cmd_cd(const char *arg) {
    const char *p = skip_ws(arg);
    if (!*p) {
        int len = name_trim_len(cur_dir_name, 8);
        set_color(LIGHT_BLUE, BLACK);
        for (int i = 0; i < len; i++) putc_(cur_dir_name[i]);
        reset_color();
        putc_('\n');
        return;
    }
    if (p[0] == '.' && p[1] == '.' && (p[2] == 0 || p[2] == ' ')) {
        if (dir_sp == 0) return;
        dir_sp--;
        cur_dir_lba = dir_stack_lba[dir_sp];
        for (int i = 0; i < 8; i++) cur_dir_name[i] = dir_stack_name[dir_sp][i];
        return;
    }
    if (p[0] == '\\' && (p[1] == 0 || p[1] == ' ')) {
        dir_sp = 0;
        cur_dir_lba = FS_ROOT_LBA;
        for (int i = 0; i < 8; i++) cur_dir_name[i] = ' ';
        return;
    }
    char name11[11];
    if (!parse_name(p, name11)) { print_err("Error: no name\n"); return; }
    load_dir();
    int off = find_entry(name11);
    if (off < 0) { print_err("Error: not found\n"); return; }
    dirent_t *d = (dirent_t*)(dir_buf + off);
    if (!(d->attr & ATTR_DIR)) { print_err("Error: not a directory\n"); return; }
    if (dir_sp >= MAX_DEPTH) { print_err("Error: too deep\n"); return; }
    dir_stack_lba[dir_sp] = cur_dir_lba;
    for (int i = 0; i < 8; i++) dir_stack_name[dir_sp][i] = cur_dir_name[i];
    dir_sp++;
    cur_dir_lba = d->lba;
    for (int i = 0; i < 8; i++) cur_dir_name[i] = d->name[i];
}

/* ================================================================
 * Команды времени
 * ================================================================ */
static void cmd_date(const char *arg) {
    (void)arg;
    set_color(YELLOW, BLACK); print("Date: "); reset_color();
    print_date(); putc_('\n');
}
static void cmd_time(const char *arg) {
    (void)arg;
    set_color(YELLOW, BLACK); print("Time: "); reset_color();
    print_time(); putc_('\n');
}
static void cmd_now(const char *arg) {
    (void)arg;
    set_color(YELLOW, BLACK); print("Now: "); reset_color();
    print_date(); print(" "); print_time(); putc_('\n');
}

/* ================================================================
 * Команды окружения: set, prompt, echo, pause
 * ================================================================ */
static void cmd_set(const char *arg) {
    const char *p = skip_ws(arg);
    if (!*p) {
        /* Показать все переменные */
        if (env_count == 0) { print_dim("(no variables)\n"); return; }
        for (int i = 0; i < env_count; i++) {
            set_color(YELLOW, BLACK); print(env_names[i]); reset_color();
            putc_('=');
            print(env_values[i]);
            putc_('\n');
        }
        return;
    }
    /* Разбираем NAME=VALUE */
    char name[16];
    int i = 0;
    while (p[i] && p[i] != '=' && i < 15) { name[i] = p[i]; i++; }
    name[i] = 0;
    if (!p[i]) { print_err("Usage: set NAME=value\n"); return; }
    const char *value = p + i + 1;
    env_set(name, value);
    /* Показать для подтверждения (как делает DOS) */
    set_color(YELLOW, BLACK); print(name); reset_color();
    putc_('=');
    print(value);
    putc_('\n');
}

static void cmd_prompt(const char *arg) {
    const char *p = skip_ws(arg);
    if (!*p) {
        /* Сбросить на дефолт */
        const char *def = "$P$G";
        int i = 0;
        while (def[i]) { prompt_fmt[i] = def[i]; i++; }
        prompt_fmt[i] = 0;
        return;
    }
    int i = 0;
    while (p[i] && i < 63) { prompt_fmt[i] = p[i]; i++; }
    prompt_fmt[i] = 0;
}

static void cmd_echo(const char *arg) {
    const char *p = skip_ws(arg);
    if (!*p) {
        if (echo_enabled) print("ECHO is on\n");
        else              print("ECHO is off\n");
        return;
    }
    if (strcmp_(p, "on") == 0)  { echo_enabled = 1; return; }
    if (strcmp_(p, "off") == 0) { echo_enabled = 0; return; }
    /* Иначе — вывести текст, разворачивая %VAR% */
    for (int i = 0; p[i]; i++) {
        if (p[i] == '%') {
            /* Читаем имя переменной до следующего % */
            char name[16];
            int j = 0;
            i++;
            while (p[i] && p[i] != '%' && j < 15) { name[j++] = p[i++]; }
            name[j] = 0;
            if (p[i] != '%') { putc_('%'); i--; continue; }
            const char *val = env_get(name);
            if (val) print(val);
        } else {
            putc_(p[i]);
        }
    }
    putc_('\n');
}

static void cmd_pause(const char *arg) {
    (void)arg;
    print("Press any key to continue . . . ");
    getchar_();
    putc_('\n');
}

/* ================================================================
 * Заставка
 * ================================================================ */
static void show_logo(void) {
    for (int i = 0; i < 80 * 25; i++) VGA[i] = (ATTR(LIGHT_GRAY, BLACK) << 8) | ' ';
    cx = 0; cy = 0;
    color = ATTR(LIGHT_GRAY, BLACK);
    print("\n");
    print("     ##     ##  ##   ##    ####    #####\n");
    print("     ###   ###   ## ##    ##  ##  ##\n");
    print("     ## # # ##    ###     ##  ##   ####\n");
    print("     ##  #  ##     ##     ##  ##      ##\n");
    print("     ##     ##     ##      ####   #####\n");
    print("\n");
    print("       32-bit Operating System\n");
    print("          version 3.2\n");
    print("\n");
    print("      Press any key to continue...\n");
    for (volatile uint32_t i = 0; i < 80000000; i++) {
        if (inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if (sc == 0xE0) { while (!(inb(0x64) & 1)); inb(0x60); }
            break;
        }
    }
}

/* ================================================================
 * run_command — единая точка входа для команд (интерактивных и из autoexec)
 * ================================================================ */
static void run_command(const char *line) {
    line = skip_ws(line);
    if (!line[0]) return;
    char cmd[32];
    const char *rest = next_token(line, cmd, sizeof(cmd));
    if (!cmd[0]) return;

    if      (!strcmp_(cmd, "help")) {
        set_color(YELLOW, BLACK); print("Available commands:\n"); reset_color();
        print("  help, info, hello      "); print_dim("- basics\n");
        print("  dir, cd <dir>          "); print_dim("- navigation\n");
        print("  mkdir, rm, touch       "); print_dim("- files\n");
        print("  write <name> [text]    "); print_dim("- write file\n");
        print("  type <name>            "); print_dim("- read file\n");
        print("  edit <name>            "); print_dim("- editor\n");
        print("  date, time, now        "); print_dim("- clock\n");
        print("  set, prompt, echo      "); print_dim("- environment\n");
        print("  pause, reboot, shutdown"); print_dim(" - system\n");
    }
    else if (!strcmp_(cmd, "info"))  { set_color(LIGHT_CYAN, BLACK);
        print("MyOS v3.2 - 32-bit, editor, env vars\n"); reset_color(); }
    else if (!strcmp_(cmd, "hello")) print_ok("hello, user\n");
    else if (!strcmp_(cmd, "dir"))   cmd_dir();
    else if (!strcmp_(cmd, "cd"))    cmd_cd(rest);
    else if (!strcmp_(cmd, "mkdir")) cmd_mkdir(rest);
    else if (!strcmp_(cmd, "rm"))    cmd_rm(rest);
    else if (!strcmp_(cmd, "touch")) cmd_touch(rest);
    else if (!strcmp_(cmd, "type"))  cmd_type(rest);
    else if (!strcmp_(cmd, "write")) cmd_write(rest);
    else if (!strcmp_(cmd, "edit"))  cmd_edit(rest);
    else if (!strcmp_(cmd, "date"))  cmd_date(rest);
    else if (!strcmp_(cmd, "time"))  cmd_time(rest);
    else if (!strcmp_(cmd, "now"))   cmd_now(rest);
    else if (!strcmp_(cmd, "set"))   cmd_set(rest);
    else if (!strcmp_(cmd, "prompt"))cmd_prompt(rest);
    else if (!strcmp_(cmd, "echo"))  cmd_echo(rest);
    else if (!strcmp_(cmd, "pause")) cmd_pause(rest);
    else if (!strcmp_(cmd, "crash")) {
        print_warn("Triggering divide-by-zero...\n");
        __asm__ volatile("xor %%eax, %%eax; div %%eax" ::: "eax");
    }
    else if (!strcmp_(cmd, "shutdown")) {
        print_warn("System shutting down...\n");
        outw(0x604, 0x2000);
        outw(0xB004, 0x2000);
        for (;;) __asm__ volatile("hlt");
    }
    else if (!strcmp_(cmd, "reboot")) {
        print_warn("Rebooting...\n");
        while (inb(0x64) & 2);
        outb(0x64, 0xFE);
    }
    else {
        print_err("Unknown command: ");
        print(cmd);
        putc_('\n');
    }
}
/* ================================================================
 * run_config — читает CONFIG.SYS из корня
 * ================================================================ */
static int parse_int(const char *s, int *out) {
    int v = 0, any = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
        any = 1;
    }
    if (!any) return 0;
    *out = v;
    return 1;
}

static void run_config(void) {
    char name11[11];
    if (!parse_name("config.sys", name11)) return;

    load_dir();
    int off = find_entry(name11);
    if (off < 0) return;

    dirent_t *d = (dirent_t*)(dir_buf + off);
    if (d->attr & ATTR_DIR) return;
    uint32_t sz = d->size;
    if (sz == 0 || sz > MAX_FILE) return;

    read_chain(d->lba, file_buf, sz);

    print_info("[config] reading CONFIG.SYS\n");

    char line[128];
    int len = 0;
    for (uint32_t i = 0; i <= sz; i++) {
        char c = (i < sz) ? (char)file_buf[i] : '\n';
        if (c == '\r') continue;
        if (c == '\n') {
            line[len] = 0;
            len = 0;

            const char *p = skip_ws(line);
            if (!*p || *p == ';' || *p == '#') continue;

            /* Разбираем key=value */
            char key[32];
            int k = 0;
            while (*p && *p != '=' && k < 31) { key[k++] = *p++; }
            key[k] = 0;
            if (*p != '=') continue;
            p++;
            const char *val = skip_ws(p);

            /* Убираем пробелы в конце key */
            while (k > 0 && key[k-1] == ' ') key[--k] = 0;

            if (strcmp_(key, "color") == 0) {
                int fg = 0, bg = 0;
                if (parse_int(val, &fg)) {
                    const char *comma = val;
                    while (*comma && *comma != ',') comma++;
                    if (*comma == ',') parse_int(comma + 1, &bg);
                    if (fg >= 0 && fg <= 15) config_fg = (uint8_t)fg;
                    if (bg >= 0 && bg <= 15) config_bg = (uint8_t)bg;
                }
            }
            else if (strcmp_(key, "prompt") == 0) {
                int j = 0;
                while (val[j] && j < 63) { prompt_fmt[j] = val[j]; j++; }
                prompt_fmt[j] = 0;
            }
            else if (strcmp_(key, "echo") == 0) {
                if (strcmp_(val, "on") == 0)  echo_enabled = 1;
                if (strcmp_(val, "off") == 0) echo_enabled = 0;
            }
            else if (strcmp_(key, "clock") == 0) {
                if (strcmp_(val, "on") == 0)  config_clock_enabled = 1;
                if (strcmp_(val, "off") == 0) config_clock_enabled = 0;
            }
        } else if (len < 127) {
            line[len++] = c;
        }
    }
}
/* ================================================================
 * run_autoexec — выполняет AUTOEXEC.BAT из корня
 * ================================================================ */
static void run_autoexec(void) {
    char name11[11];
    if (!parse_name("autoexec.bat", name11)) return;

    load_dir();
    int off = find_entry(name11);
    if (off < 0) return;

    dirent_t *d = (dirent_t*)(dir_buf + off);
    if (d->attr & ATTR_DIR) return;
    uint32_t sz = d->size;
    if (sz == 0 || sz > MAX_FILE) return;

    read_chain(d->lba, file_buf, sz);

    print_info("[autoexec] running AUTOEXEC.BAT\n");

    char line[128];
    int len = 0;
    for (uint32_t i = 0; i <= sz; i++) {
        char c = (i < sz) ? (char)file_buf[i] : '\n';
        if (c == '\r') continue;
        if (c == '\n') {
            line[len] = 0;
            len = 0;

            const char *p = skip_ws(line);
            if (!*p || *p == ';' || *p == '#') continue;

            int silent = 0;
            if (*p == '@') { silent = 1; p = skip_ws(p + 1); }
            if (!*p) continue;

            if (!silent && echo_enabled) {
                set_color(DARK_GRAY, BLACK);
                putc_('>'); putc_(' ');
                print(p);
                putc_('\n');
                reset_color();
            }
            run_command(p);
        } else if (len < 127) {
            line[len++] = c;
        }
    }
    print_info("[autoexec] done\n\n");
}
/* ================================================================
 * main
 * ================================================================ */
void kmain(void) {
    idt_init();
    show_logo();
    clear_screen();

    set_color(LIGHT_CYAN, BLACK);
    print("========================================\n");
    set_color(LIGHT_GREEN, BLACK);
    print("  MyOS v3.2\n");
    set_color(LIGHT_CYAN, BLACK);
    print("========================================\n");
    reset_color();
    print("Type ");
    set_color(YELLOW, BLACK); print("'help'"); reset_color();
    print(" for commands.\n\n");

    fs_init();

    run_config();
    draw_clock_force();

    run_autoexec();

    char line[128];

    for (;;) {
        print_prompt();
        int len = 0;
        for (;;) {
            char c = getchar_();
            if (c == '\n') { line[len] = 0; putc_('\n'); break; }
            if (c == '\b') { if (len > 0) { len--; putc_('\b'); } }
            else if (c >= ' ' && c < 127) { if (len < 127) { line[len++] = c; putc_(c); } }
        }
        run_command(line);
    }
} 
