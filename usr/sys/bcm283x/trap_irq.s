.arm
.syntax unified
.section .text

.equ MODE_MASK, 0x0000001f
.equ USR_MODE, 0x00000010
.equ IRQ_MODE, 0x00000012
.equ SVC_MODE, 0x00000013

.extern c_entry_irq
.extern panic

.global _entry_irq
_entry_irq:
    sub     lr, lr, #4
    str     r0, [sp, #-4]
    str     lr, [sp, #-8]
    mrs     lr, spsr
    str     lr, [sp, #-12]

    mrs     r0, cpsr
    eor     r0, r0, #(IRQ_MODE ^ SVC_MODE)
    msr     spsr_cxsf, r0

    mov     r0, sp

    and     lr, lr, #MODE_MASK

    cmp     lr, #SVC_MODE
    adreq   lr, __irq_svc
    beq     1f

    cmp     lr, #USR_MODE
    adreq   lr, __irq_usr
    beq     1f

    adr     lr, __irq_invalid

1:
    clrex
    movs    pc, lr

__irq_usr:
    sub     sp, sp, #(4*17)
    stmia   sp, {r1 - r12}

    ldr     r3, [r0, #-4]
    str     r3, [sp, #-4]!

    add     sp, sp, #(4*13)
    stmia   sp, {sp, lr}^
    mov     r0, r0
    sub     sp, sp, #(4*13)

    ldr     r3, [r0, #-8]
    str     r3, [sp, #(4*15)]

    ldr     r3, [r0, #-12]
    str     r3, [sp, #(4*16)]

    mov     r0, sp
    bl      c_entry_irq
    b       __eret_user

__irq_svc:
    str     sp, [r0, #-16]
    bic     sp, sp, #7
    sub     sp, sp, #(4*17)
    stmia   sp, {r1 - r12}

    ldr     r3, [r0, #-4]
    str     r3, [sp, #-4]!

    ldr     r3, [r0, #-16]
    str     r3, [sp, #(4*13)]

    str     lr, [sp, #(4*14)]

    ldr     r3, [r0, #-8]
    str     r3, [sp, #(4*15)]

    ldr     r3, [r0, #-12]
    str     r3, [sp, #(4*16)]

    mov     r0, sp
    bl      c_entry_irq
    b       __eret_kernel

__irq_invalid:
    ldr     r0, addr_of_msg_irq_invalid
    b       panic
addr_of_msg_irq_invalid: .word msg_irq_invalid

.data
.balign 4
msg_irq_invalid: .asciz "The irq spsr must be either usr or svc mode"
