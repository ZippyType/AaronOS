; SMP trampoline — flat binary, placed at 0x8000
; AP boots here in real mode, enters protected mode, calls function at 0x7FE0

[ORG 0x8000]
[BITS 16]

trampoline_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000

    lgdt [cs:gdt_desc]

    mov eax, cr0
    or al, 1
    mov cr0, eax

    db 0x66
    db 0xEA
    dd prot_mode - trampoline_start + 0x8000
    dw 0x08

[BITS 32]
prot_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7000

    mov eax, [0x7FE0]
    test eax, eax
    jz .halt
    call eax
.halt:
    cli
    hlt
    jmp .halt

[BITS 16]
align 8
gdt:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt
trampoline_end:
