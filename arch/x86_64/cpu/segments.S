/* Aurora v2 - descriptor-table loading and far control transfers. */
.section .text
.code64

.global gdt_load
.type gdt_load, @function
gdt_load:
    lgdt (%rdi)
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    pushq $0x08
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    ret
.size gdt_load, . - gdt_load

.global tss_load
.type tss_load, @function
tss_load:
    movw %di, %ax
    ltr %ax
    ret
.size tss_load, . - tss_load

.global idt_load
.type idt_load, @function
idt_load:
    lidt (%rdi)
    ret
.size idt_load, . - idt_load

.section .note.GNU-stack, "", @progbits
