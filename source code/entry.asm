BITS 32
global _start
extern kmain
extern _bss_start
extern _bss_end

section .text.entry
_start:
   mov ax, 0x10
   mov es, ax
   mov edi, _bss_start
   mov ecx, _bss_end
   sub ecx, edi
   xor eax, eax
   reb stosb
   call kmain

.hang:
    cli
    hlt
    jmp .hang
