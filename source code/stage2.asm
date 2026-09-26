BITS 16
ORG 0x8000

KERNEL_ADDR equ 0x8400

start:
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7C00

    mov ax, 0xB800
    mov es, ax
    mov word [es:0x100], 0x0731      ; '1' во 2-й строке
    mov word [es:0x102], 0x0741      ; 'A'
    xor ax, ax
    mov es, ax

    mov [boot_drive], dl

    ; --- A20 ---
    in  al, 0x92
    or  al, 2
    out 0x92, al

    mov ax, 0xB800
    mov es, ax
    mov word [es:0x104], 0x0732
    mov word [es:0x106], 0x0741
    xor ax, ax
    mov es, ax

    ; --- GDT ---
    lgdt [gdt_desc]

    mov ax, 0xB800
    mov es, ax
    mov word [es:0x108], 0x0733
    mov word [es:0x10A], 0x0741
    xor ax, ax
    mov es, ax

    ; --- Переход в PM ---
    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    jmp 0x08:pm_entry

BITS 32
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    mov edi, 0xB8000 + 0x10C
    mov word [edi], 0x0734           ; '4'
    mov word [edi+2], 0x0741
    mov word [edi+4], 0x0735         ; '5'
    mov word [edi+6], 0x0741

    mov eax, KERNEL_ADDR
    jmp eax

align 8
gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF, 0
    db 0, 10011010b, 11001111b, 0
gdt_data:
    dw 0xFFFF, 0
    db 0, 10010010b, 11001111b, 0
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

boot_drive db 0

times 1024 - ($ - $$) db 0
