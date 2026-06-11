section .multiboot_header
header_start:
    dd 0xe85250d6
    dd 0
    dd header_end - header_start
    dd 0x100000000 - (0xe85250d6 + 0 + (header_end - header_start))
    dw 0
    dw 0
    dd 8
header_end:

section .text
bits 32
extern kernel_main
extern keyboard_handler_main
extern mouse_handler_main
extern timer_callback      ; Moved from BSS
extern syscall_handler_main
extern ata_irq_handler
global _start
global load_idt
global keyboard_handler_asm
global mouse_handler_asm
global timer_handler_asm    ; Moved from BSS
global syscall_handler_asm
global ata_handler_asm

_start:
    cli
    mov esp, stack_top
    lgdt [gdt_descriptor]
    jmp 0x08:.reload_cs

.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    call kernel_main
.hang:
    hlt
    jmp .hang

load_idt:
    mov edx, [esp + 4]
    lidt [edx]
    sti
    ret

keyboard_handler_asm:
    pusha
    call keyboard_handler_main
    mov al, 0x20        ; Send EOI to PIC
    out 0x20, al
    popa
    iret

timer_handler_asm:      ; This is now correctly in the .text section
    pusha
    push esp            ; pass pointer to pusha frame
    call timer_callback ; returns new stack pointer
    mov esp, eax        ; switch to scheduler-chosen stack
    mov al, 0x20        ; Send EOI (End of Interrupt)
    out 0x20, al        ; To the Master PIC
    popa
    iret

syscall_handler_asm:    ; int 0x80 handler
    pusha
    push esp            ; pointer to pusha frame (eax,ecx,edx,ebx,esp,ebp,esi,edi)
    call syscall_handler_main
    add esp, 4          ; pop the pointer
    mov [esp + 28], eax ; store return value for popa -> eax
    popa
    iret

ata_handler_asm:        ; IRQ14 (primary ATA channel)
    pusha
    call ata_irq_handler
    mov al, 0x20
    out 0xA0, al        ; EOI to slave PIC
    out 0x20, al        ; EOI to master PIC
    popa
    iret

mouse_handler_asm:      ; IRQ12 (PS/2 mouse)
    pusha
    call mouse_handler_main
    mov al, 0x20
    out 0xA0, al        ; EOI to slave PIC
    out 0x20, al        ; EOI to master PIC
    popa
    iret

section .data
align 16
gdt_start:
    dq 0x0000000000000000
    dq 0x00cf9a000000ffff ; Code (0x08)
    dq 0x00cf92000000ffff ; Data (0x10)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

section .bss
align 16
stack_bottom:
    resb 32768 ; 32KB Stack
stack_top:

; NO CODE BEYOND THIS POINT IN BSS