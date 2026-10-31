; boot_arm.S ARM32 启动向量
    .section .vector
    ldr pc, reset_handler
    ldr pc, und_handler
    ldr pc, swi_handler
reset_handler:
    mrs r0, cpsr
    bic r0, r0, #0x1f
    orr r0, r0, #0x13
    msr cpsr, r0
    ldr sp, =0x80000000
    b kernel_entry
und_handler:
    b .
swi_handler:
    b .
