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
 * VGA text mode 80x25
 * ================================================================ */
#define VGA ((volatile uint16_t*)0xB8000)
#define COLS 80
#define ROWS 25

static int cx = 0, cy = 0;
static uint8_t color = 0x07;

static void update_cursor(void) {
    uint16_t pos = (uint16_t)(cy * COLS + cx);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)(pos >> 8));
}

static void clear_screen(void) {
    for (int i = 0; i < COLS * ROWS; i++)
        VGA[i] = (color << 8) | ' ';
    cx = cy = 0;
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

static void print_dec(uint32_t v) {
    char buf[11];
    int i = 0;
    if (v == 0) { putc_('0'); return; }
    while (v && i < 10) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i--) putc_(buf[i]);
}

static void print_hex8(uint8_t v) {
    const char *h = "0123456789ABCDEF";
    putc_(h[(v >> 4) & 0xF]);
    putc_(h[v & 0xF]);
}

static void print_hex32(uint32_t v) {
    const char *h = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4)
        putc_(h[(v >> i) & 0xF]);
}

/* ================================================================
 * IDT и обработчик исключений
 * ================================================================ */
typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

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
        idt[i].offset_low  = 0;
        idt[i].selector    = 0;
        idt[i].zero        = 0;
        idt[i].type_attr   = 0;
        idt[i].offset_high = 0;
    }
    for (int i = 0; i < 32; i++)
        idt_set_gate(i, isr_stub_table[i]);
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
    for (int i = 0; i < 80 * 25; i++)
        vga[i] = (0x4F << 8) | ' ';

    const char *n = (r->int_no < 32) ? exception_names[r->int_no] : "Unknown";
    while (*n) vga[pos++] = (0x4F << 8) | (uint8_t)*n++;

    vga[pos++] = (0x4F << 8) | ' ';
    vga[pos++] = (0x4F << 8) | '(';
    vga[pos++] = (0x4F << 8) | (uint8_t)('0' + (r->int_no / 10) % 10);
    vga[pos++] = (0x4F << 8) | (uint8_t)('0' + r->int_no % 10);
    vga[pos++] = (0x4F << 8) | ')';

    cx = 0;
    cy = 2;
    color = 0x4F;
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
 * Строки и разбор ввода
 * ================================================================ */
static int strcmp_(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static const char *skip_ws(const char *s) {
    while (*s == ' ') s++;
    return s;
}

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
        s++;
        i = 8;
        while (*s && *s != ' ' && i < 11) {
            char c = *s++;
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            out[i++] = c;
        }
    }
    return 1;
}

/* ================================================================
 * Клавиатура
 * ================================================================ */
static const char keymap[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' '
};
static char getchar_(void) {
    for (;;) {
        if (inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if (sc < 128 && keymap[sc]) return keymap[sc];
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
        for (uint32_t i = 0; i < n; i++) {
            if (bitmap_get(start + i)) { ok = 0; break; }
        }
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
        for (int j = 0; j < 11; j++) {
            if (((char*)&d[i])[j] != name11[j]) { eq = 0; break; }
        }
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
            d[i].attr   = attr;
            d[i].size   = size;
            d[i].lba    = lba;
            d[i].blocks = blocks;
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

static void print_path(void) {
    putc_('A'); putc_(':'); putc_('\\');
    for (int i = 0; i < dir_sp; i++) {
        int empty = 1;
        for (int k = 0; k < 8; k++) if (dir_stack_name[i][k] != ' ') { empty = 0; break; }
        if (empty) continue;
        int len = name_trim_len(dir_stack_name[i], 8);
        for (int k = 0; k < len; k++) putc_(dir_stack_name[i][k]);
        putc_('\\');
    }
    int len = name_trim_len(cur_dir_name, 8);
    for (int k = 0; k < len; k++) putc_(cur_dir_name[k]);
    putc_('>'); putc_(' ');
}

static void print_dirent(const dirent_t *e) {
    int len = name_trim_len(e->name, 8);
    for (int i = 0; i < len; i++) putc_(e->name[i]);
    int xlen = name_trim_len(e->ext, 3);
    if (xlen > 0) {
        putc_('.');
        for (int i = 0; i < xlen; i++) putc_(e->ext[i]);
    }
    int shown = len + (xlen ? 1 + xlen : 0);
    for (int i = shown; i < 12; i++) putc_(' ');
    if (e->attr & ATTR_DIR) {
        print("<DIR>\n");
    } else {
        print_dec(e->size);
        print(" bytes\n");
    }
}

/* ================================================================
 * Команды ФС
 * ================================================================ */
static void cmd_dir(void) {
    load_dir();
    dirent_t *d = (dirent_t*)dir_buf;
    print("\n Directory of A:\\\n\n");
    for (int i = 0; i < DIR_ENTRIES; i++) {
        if (d[i].name[0] == 0 || d[i].name[0] == ' ') continue;
        print_dirent(&d[i]);
    }
    print("\n");
}

static void cmd_mkdir(const char *arg) {
    char name11[11];
    if (!parse_name(arg, name11)) { print("Error: no name\n"); return; }
    load_dir();
    if (find_entry(name11) >= 0) { print("Error: exists\n"); return; }
    int lba = alloc_chain(1);
    if (lba < 0) { print("Error: no space\n"); return; }
    uint8_t zero[512];
    for (int i = 0; i < 512; i++) zero[i] = 0;
    ata_write((uint32_t)lba, zero);
    if (create_entry(name11, ATTR_DIR, 0, (uint32_t)lba, 1) < 0) {
        print("Error: dir full\n");
        free_chain((uint32_t)lba, 1);
        return;
    }
    save_dir();
    print("OK\n");
}

static void cmd_touch(const char *arg) {
    char name11[11];
    if (!parse_name(arg, name11)) { print("Error: no name\n"); return; }
    load_dir();
    if (find_entry(name11) >= 0) { print("Error: exists\n"); return; }
    int lba = alloc_chain(1);
    if (lba < 0) { print("Error: no space\n"); return; }
    uint8_t zero[512];
    for (int i = 0; i < 512; i++) zero[i] = 0;
    ata_write((uint32_t)lba, zero);
    if (create_entry(name11, ATTR_FILE, 0, (uint32_t)lba, 1) < 0) {
        print("Error: dir full\n");
        free_chain((uint32_t)lba, 1);
        return;
    }
    save_dir();
    print("OK\n");
}

static void cmd_rm(const char *arg) {
    char name11[11];
    if (!parse_name(arg, name11)) { print("Error: no name\n"); return; }
    load_dir();
    int off = find_entry(name11);
    if (off < 0) { print("Error: not found\n"); return; }
    dirent_t *d = (dirent_t*)(dir_buf + off);
    free_chain(d->lba, d->blocks);
    delete_entry_at(off);
    save_dir();
    print("OK\n");
}

static void cmd_type(const char *arg) {
    char name11[11];
    if (!parse_name(arg, name11)) { print("Error: no name\n"); return; }
    load_dir();
    int off = find_entry(name11);
    if (off < 0) { print("Error: not found\n"); return; }
    dirent_t *d = (dirent_t*)(dir_buf + off);
    if (d->attr & ATTR_DIR) { print("Error: is a directory\n"); return; }
    uint32_t sz = d->size;
    if (sz > MAX_FILE) sz = MAX_FILE;
    if (sz == 0) { putc_('\n'); return; }
    read_chain(d->lba, file_buf, sz);
    for (uint32_t i = 0; i < sz; i++) putc_((char)file_buf[i]);
    if (file_buf[sz-1] != '\n') putc_('\n');
}

static void cmd_write(const char *arg) {
    char name[64];
    arg = next_token(arg, name, sizeof(name));
    if (!name[0]) { print("Error: no name\n"); return; }

    char name11[11];
    if (!parse_name(name, name11)) { print("Error: no name\n"); return; }

    const char *rest = skip_ws(arg);
    uint32_t tlen = 0;

    if (*rest) {
        while (*rest && tlen < MAX_FILE - 1)
            file_buf[tlen++] = (uint8_t)*rest++;
        file_buf[tlen++] = '\n';
    } else {
        print("Enter text. Empty line to finish.\n");
        char line[128];
        for (;;) {
            print("| ");
            int len = 0;
            for (;;) {
                char c = getchar_();
                if (c == '\n') { line[len] = 0; putc_('\n'); break; }
                if (c == '\b') { if (len > 0) { len--; putc_('\b'); } }
                else if (c >= ' ' && c < 127) {
                    if (len < 127) { line[len++] = c; putc_(c); }
                }
            }
            if (len == 0) break;
            for (int i = 0; i < len && tlen < MAX_FILE - 1; i++)
                file_buf[tlen++] = (uint8_t)line[i];
            if (tlen < MAX_FILE - 1) file_buf[tlen++] = '\n';
            if (tlen >= MAX_FILE - 1) {
                print("[file size limit reached]\n");
                break;
            }
        }
    }

    if (tlen == 0) { print("Error: no text\n"); return; }

    uint32_t need = (tlen + 511) / 512;
    if (need == 0) need = 1;

    load_dir();
    int off = find_entry(name11);

    uint32_t old_lba = 0, old_blocks = 0;

    if (off >= 0) {
        dirent_t *d = (dirent_t*)(dir_buf + off);
        old_lba = d->lba;
        old_blocks = d->blocks;
    }

    int new_lba = alloc_chain(need);
    if (new_lba < 0) { print("Error: no space\n"); return; }

    write_chain((uint32_t)new_lba, file_buf, tlen);

    if (off >= 0) {
        dirent_t *d = (dirent_t*)(dir_buf + off);
        d->size   = tlen;
        d->lba    = (uint32_t)new_lba;
        d->blocks = (uint16_t)need;
        free_chain(old_lba, old_blocks);
    } else {
        if (create_entry(name11, ATTR_FILE, tlen, (uint32_t)new_lba, (uint16_t)need) < 0) {
            print("Error: dir full\n");
            free_chain((uint32_t)new_lba, need);
            return;
        }
    }
    save_dir();
    print("OK (");
    print_dec(tlen);
    print(" bytes, ");
    print_dec(need);
    print(" sectors)\n");
}

static void cmd_cd(const char *arg) {
    const char *p = skip_ws(arg);

    if (!*p) {
        int len = name_trim_len(cur_dir_name, 8);
        for (int i = 0; i < len; i++) putc_(cur_dir_name[i]);
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
  if (!parse_name(p, name11)) { print("Error: no name\n"); return; }
    load_dir();
    int off = find_entry(name11);
    if (off < 0) { print("Error: not found\n"); return; }
    dirent_t *d = (dirent_t*)(dir_buf + off);
    if (!(d->attr & ATTR_DIR)) { print("Error: not a directory\n"); return; }
    if (dir_sp >= MAX_DEPTH) { print("Error: too deep\n"); return; }

    dir_stack_lba[dir_sp] = cur_dir_lba;
    for (int i = 0; i < 8; i++) dir_stack_name[dir_sp][i] = cur_dir_name[i];
    dir_sp++;
    cur_dir_lba = d->lba;
    for (int i = 0; i < 8; i++) cur_dir_name[i] = d->name[i];
}

/* ================================================================
 * main
 * ================================================================ */
void kmain(void) {
    idt_init();
    clear_screen();
    print("MyOS v3.1 (32-bit, multi-sector files)\n");
    print("Type 'help' for commands.\n\n");

    fs_init();

    char line[128];

    for (;;) {
        print_path();
        int len = 0;

        for (;;) {
            char c = getchar_();
            if (c == '\n') { line[len] = 0; putc_('\n'); break; }
            if (c == '\b') { if (len > 0) { len--; putc_('\b'); } }
            else if (c >= ' ' && c < 127) {
                if (len < 127) { line[len++] = c; putc_(c); }
            }
        }

        char cmd[32];
        const char *rest = next_token(line, cmd, sizeof(cmd));
        if (!cmd[0]) continue;

        if      (!strcmp_(cmd, "help"))     print("help, info, hello, dir, cd, mkdir, rm, touch, type, write, crash, shutdown, reboot\n");
        else if (!strcmp_(cmd, "info"))     print("MyOs v3.1 - 32-bit, multi-sector files\n");
        else if (!strcmp_(cmd, "hello"))    print("hello, user\n");
        else if (!strcmp_(cmd, "dir"))      cmd_dir();
        else if (!strcmp_(cmd, "cd"))       cmd_cd(rest);
        else if (!strcmp_(cmd, "mkdir"))    cmd_mkdir(rest);
        else if (!strcmp_(cmd, "rm"))       cmd_rm(rest);
        else if (!strcmp_(cmd, "touch"))    cmd_touch(rest);
        else if (!strcmp_(cmd, "type"))     cmd_type(rest);
        else if (!strcmp_(cmd, "write"))    cmd_write(rest);
        else if (!strcmp_(cmd, "crash")) {
            print("Triggering divide-by-zero...\n");
            __asm__ volatile("xor %%eax, %%eax; div %%eax" ::: "eax");
        }
        else if (!strcmp_(cmd, "shutdown")) {
            print("System shutting down...\n");
            outw(0x604, 0x2000);
            outw(0xB004, 0x2000);
            for (;;) __asm__ volatile("hlt");
        }
        else if (!strcmp_(cmd, "reboot")) {
            while (inb(0x64) & 2);
            outb(0x64, 0xFE);
        }
        else {
            print("Unknown command: ");
            print(cmd);
            putc_('\n');
        }
    }
}
