BITS 16
ORG 0x7C00

STAGE2_ADDR  equ 0x8000
STAGE2_SECTS equ 300         ; сколько всего хотим загрузить
CHUNK        equ 32          ; сколько за раз (безопасно, < 127)

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    mov si, msg_loading
    call print

    ; --- Читаем STAGE2_SECTS секторов порциями по CHUNK ---
    mov word [remaining], STAGE2_SECTS
    mov word [cur_lba], 1
    mov word [cur_addr], STAGE2_ADDR
    mov word [cur_seg], 0

.read_loop:
    mov ax, [remaining]
    test ax, ax
    jz  .done

    mov bx, CHUNK
    cmp ax, bx
    jae .have_chunk
    mov bx, ax
.have_chunk:
    ; BX = сколько секторов в этой порции (1..32)
    mov ax, [cur_lba]
    mov [dap_lba], ax
    mov ax, [cur_addr]
    mov [dap_off], ax
    mov ax, [cur_seg]
    mov [dap_seg], ax
    mov [dap_count], bx

    push bx
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    pop bx
    jc  disk_error

    ; --- Обновляем счётчики ---
    sub [remaining], bx
    add [cur_lba], bx

    ; cur_addr += bx * 512
    mov ax, bx
    mov cl, 9
    shl ax, cl
    add [cur_addr], ax
    jnc .read_loop
    ; переполнение 16-бит offset → двигаем сегмент
    mov ax, [cur_seg]
    add ax, 0x1000
    mov [cur_seg], ax
    jmp .read_loop

.done:
    mov dl, [boot_drive]
    jmp STAGE2_ADDR

disk_error:
    mov si, msg_err
    call print
    cli
    hlt
    jmp $

print:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print
.done:
    ret

; --- Disk Address Packet ---
dap:
    db 0x10, 0
dap_count: dw 1
dap_off:   dw 0
dap_seg:   dw 0
dap_lba:   dq 0

remaining dw 0
cur_lba   dw 1
cur_addr  dw STAGE2_ADDR
cur_seg   dw 0

boot_drive  db 0
msg_loading db "Loading MyOS...", 0x0D, 0x0A, 0
msg_err     db "Disk read error!", 0x0D, 0x0A, 0

times 510 - ($ - $$) db 0
dw 0xAA55
