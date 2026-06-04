/*
 * Xiaomi Mi 4 (cancro) Stage1 boot-wrapper payload
 *
 * Stage1 is the second milestone after Stage0. It is still a self-contained
 * non-Linux ARMv7 payload loaded as the Android boot image `kernel` through
 * non-persistent `fastboot boot`. It does NOT run XNU yet.
 *
 * What Stage1 proves on real cancro hardware:
 *   1. We can set up a stack and zero .bss from bare metal.
 *   2. We can synthesize an XNU-style ARM `boot_args` structure in memory,
 *      modeled on external/xnu-4570.1.46/pexpert/pexpert/arm/boot.h.
 *   3. We can build a minimal Apple-format flattened device tree stub and
 *      reference it from boot_args (deviceTreeP / deviceTreeLength).
 *   4. We can hand off to a test "kernel entry" stub with r0 = boot_args*,
 *      matching the XNU ARM calling convention (arm_init(boot_args*)).
 *   5. The handoff stub validates key boot_args fields and logs progress
 *      into the Android ram_console persistent buffer.
 *   6. We reboot through MSM8974 PS_HOLD so the test stays non-persistent.
 *
 * All log output goes to the Android persistent_ram_buffer at 0xde500000
 * (signature 'DBGC'), which the next normal Android boot exposes through
 * /proc/last_kmsg. No UART or teardown required.
 */

.syntax unified
.cpu cortex-a15
.arch armv7-a
.arm

/* ---- cancro / MSM8974 platform constants (confirmed by Stage0) ---- */
.equ STAGE1_BASE,        0x00008000
.equ RAM_CONSOLE_BASE,   0xde500000
.equ RAM_CONSOLE_SIG,    0x43474244      /* 'DBGC' little-endian */
.equ MSM8974_PSHOLD,     0xfc4ab000
.equ MSM_IMEM_BASE_PHYS, 0x0fa00000
.equ RESTART_REASON,     (MSM_IMEM_BASE_PHYS + 0x65c)
.equ RESTART_NORMAL,     0x77665501

/* Observed System RAM: 0x80000000 .. 0xde6fffff. Top 2 MiB is ram_console. */
.equ RAM_PHYS_BASE,      0x80000000
.equ RAM_TOP,            0xde700000
.equ RAM_CONSOLE_SIZE,   0x00200000

/* XNU ARM boot_args (boot.h): Revision/Version 2 carry bootFlags+memSizeActual. */
.equ BOOT_ARGS_REVISION, 2
.equ BOOT_ARGS_VERSION,  2
.equ BOOT_LINE_LENGTH,   256
/* Experimental machine tag. NOT an Apple machine ID; marks MSM8974 bring-up. */
.equ MACHINE_TYPE_MSM8974, 0x8974

/* Stage1 private stack is a payload-local .bss buffer (grows down). */
.equ STAGE1_STACK_SIZE,  4096

.global _start
.type _start, %function
_start:
    cpsid   if                      /* mask IRQ/FIQ for the whole test */

    /* Minimal runtime: stack + .bss zeroing so we behave like C-capable code. */
    ldr     sp, =stage1_stack_top
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    bcs     2f
    str     r2, [r0], #4
    b       1b
2:

    /* Initialize the persistent ram_console buffer (fresh header). */
    bl      rc_init

    ldr     r0, = msg_stage1
    bl      rc_puts

    /* Build the XNU-style boot_args image in static .data. */
    bl      build_boot_args

    ldr     r0, = msg_built
    bl      rc_puts

    /* Hand off to the test kernel entry with r0 = boot_args*, like XNU. */
    ldr     r0, =boot_args_struct
    bl      test_kernel_entry

    ldr     r0, = msg_returned
    bl      rc_puts

    /* Restart reason: normal reboot. */
    ldr     r0, =RESTART_REASON
    ldr     r1, =RESTART_NORMAL
    str     r1, [r0]
    dsb     sy

    ldr     r0, = msg_reset
    bl      rc_puts

    /* MSM8974 restart v2: lower PS_HOLD. */
    ldr     r0, =MSM8974_PSHOLD
    mov     r1, #0
    str     r1, [r0]
    dsb     sy

3:  wfe
    b       3b


/* ====================================================================
 * test_kernel_entry(boot_args *args)  [r0 = args]
 *
 * Stand-in for the future XNU ARM arm_init(boot_args*) entry. Validates the
 * handoff contract and logs the result. Returns normally on success.
 * ==================================================================== */
.type test_kernel_entry, %function
test_kernel_entry:
    push    {r4, r5, r6, lr}
    mov     r4, r0                  /* r4 = args */

    ldr     r0, = msg_handoff_in
    bl      rc_puts

    /* Check Revision (offset 0, 16-bit) == BOOT_ARGS_REVISION. */
    ldrh    r5, [r4, #0]
    cmp     r5, #BOOT_ARGS_REVISION
    bne     handoff_bad

    /* Check Version (offset 2, 16-bit) == BOOT_ARGS_VERSION. */
    ldrh    r5, [r4, #2]
    cmp     r5, #BOOT_ARGS_VERSION
    bne     handoff_bad

    /* deviceTreeP (offset 48) must be non-NULL. */
    ldr     r5, [r4, #BA_DEVICETREEP]
    cmp     r5, #0
    beq     handoff_bad

    /* deviceTreeLength (offset 52) must be non-zero. */
    ldr     r6, [r4, #BA_DEVICETREELENGTH]
    cmp     r6, #0
    beq     handoff_bad

    /* Apple DT root node sanity: first word is nProperties (>=1). */
    ldr     r0, [r5, #0]
    cmp     r0, #0
    beq     handoff_bad

    ldr     r0, = msg_handoff_ok
    bl      rc_puts
    mov     r0, #0
    pop     {r4, r5, r6, pc}

handoff_bad:
    ldr     r0, = msg_handoff_bad
    bl      rc_puts
    mov     r0, #1
    pop     {r4, r5, r6, pc}
.size test_kernel_entry, . - test_kernel_entry


/* ====================================================================
 * build_boot_args(void)
 *
 * Fill boot_args_struct with the XNU-style ARM contract values and point
 * deviceTreeP at the prebuilt Apple-DT stub. Layout matches boot.h:
 *   off  0  Revision (u16)
 *   off  2  Version  (u16)
 *   off  4  virtBase (u32)
 *   off  8  physBase (u32)
 *   off 12  memSize  (u32)
 *   off 16  topOfKernelData (u32)
 *   off 20  Video (6 * u32 = 24 bytes)
 *   off 44  machineType (u32)
 *   off 48  deviceTreeP (u32)
 *   off 52  deviceTreeLength (u32)
 * See explicit offsets below.
 * ==================================================================== */
.type build_boot_args, %function
build_boot_args:
    push    {r4, lr}
    ldr     r4, =boot_args_struct

    /* Revision (u16) + Version (u16) packed in first word. */
    movw    r0, #BOOT_ARGS_REVISION
    movt    r0, #BOOT_ARGS_VERSION
    str     r0, [r4, #BA_REVISION]

    mov     r0, #0
    str     r0, [r4, #BA_VIRTBASE]          /* virtBase placeholder (no MMU) */

    ldr     r0, =STAGE1_BASE
    str     r0, [r4, #BA_PHYSBASE]          /* physBase = payload base */

    /* memSize = usable RAM below ram_console = 0xde500000 - 0x80000000. */
    ldr     r0, =(RAM_CONSOLE_BASE - RAM_PHYS_BASE)
    str     r0, [r4, #BA_MEMSIZE]

    ldr     r0, =__stage1_image_end
    str     r0, [r4, #BA_TOPOFKERNELDATA]

    /* Video: headless, all zero. */
    add     r0, r4, #BA_VIDEO
    mov     r1, #0
    mov     r2, #6
1:  str     r1, [r0], #4
    subs    r2, r2, #1
    bne     1b

    movw    r0, #MACHINE_TYPE_MSM8974
    str     r0, [r4, #BA_MACHINETYPE]

    ldr     r0, =apple_dt_blob
    str     r0, [r4, #BA_DEVICETREEP]
    ldr     r0, =apple_dt_blob_len
    ldr     r0, [r0]
    str     r0, [r4, #BA_DEVICETREELENGTH]

    /* CommandLine: copy cmdline_str into the 256-byte field. */
    add     r0, r4, #BA_COMMANDLINE
    ldr     r1, =cmdline_str
2:  ldrb    r2, [r1], #1
    strb    r2, [r0], #1
    cmp     r2, #0
    bne     2b

    mov     r0, #0
    str     r0, [r4, #BA_BOOTFLAGS]

    /* memSizeActual = full System RAM span 0x80000000..0xde6fffff. */
    ldr     r0, =(RAM_TOP - RAM_PHYS_BASE)
    str     r0, [r4, #BA_MEMSIZEACTUAL]

    pop     {r4, pc}
.size build_boot_args, . - build_boot_args


/* ====================================================================
 * Persistent ram_console writer (Android persistent_ram_buffer)
 *   struct { u32 sig; u32 start; u32 size; u8 data[]; }
 * rc_init: write signature, start=0, size=0.
 * rc_puts(r0=asciiz): append bytes to data[], advancing size.
 * ==================================================================== */
.type rc_init, %function
rc_init:
    ldr     r0, =RAM_CONSOLE_BASE
    ldr     r1, =RAM_CONSOLE_SIG
    str     r1, [r0, #0]            /* sig */
    mov     r1, #0
    str     r1, [r0, #4]            /* start */
    str     r1, [r0, #8]            /* size */
    dsb     sy
    bx      lr
.size rc_init, . - rc_init

.type rc_puts, %function
rc_puts:
    push    {r4, r5, r6, lr}
    mov     r4, r0                  /* r4 = src */
    ldr     r5, =RAM_CONSOLE_BASE
    ldr     r6, [r5, #8]            /* r6 = current size */
    add     r3, r5, #12             /* data[] base */
    add     r3, r3, r6              /* write cursor = data + size */
1:  ldrb    r0, [r4], #1
    cmp     r0, #0
    beq     2f
    strb    r0, [r3], #1
    add     r6, r6, #1
    b       1b
2:  str     r6, [r5, #8]            /* update size */
    dsb     sy
    pop     {r4, r5, r6, pc}
.size rc_puts, . - rc_puts


/* ---- boot_args field offsets (XNU ARM boot.h) ---- */
.equ BA_REVISION,        0
.equ BA_VIRTBASE,        4
.equ BA_PHYSBASE,        8
.equ BA_MEMSIZE,         12
.equ BA_TOPOFKERNELDATA, 16
.equ BA_VIDEO,           20
.equ BA_MACHINETYPE,     44
.equ BA_DEVICETREEP,     48
.equ BA_DEVICETREELENGTH,52
.equ BA_COMMANDLINE,     56
.equ BA_BOOTFLAGS,       (56 + BOOT_LINE_LENGTH)
.equ BA_MEMSIZEACTUAL,   (56 + BOOT_LINE_LENGTH + 4)
.equ BA_SIZE,            (56 + BOOT_LINE_LENGTH + 8)


/* ---- read-only strings ---- */
.align 2
msg_stage1:
    .asciz "MI4IOS6_STAGE1 v1 entered; stack+bss ready; ram_console live\n"
msg_built:
    .asciz "MI4IOS6_STAGE1 boot_args built (rev2); apple-dt stub attached\n"
msg_handoff_in:
    .asciz "MI4IOS6_STAGE1 handoff -> test_kernel_entry(boot_args*)\n"
msg_handoff_ok:
    .asciz "MI4IOS6_STAGE1 handoff ok: rev/ver/dt validated\n"
msg_handoff_bad:
    .asciz "MI4IOS6_STAGE1 handoff bad: boot_args validation failed\n"
msg_returned:
    .asciz "MI4IOS6_STAGE1 returned from test kernel; preparing reset\n"
msg_reset:
    .asciz "MI4IOS6_STAGE1 attempting MSM8974 PS_HOLD reset\n"

.align 2
cmdline_str:
    .asciz "debug=0x144 serial=0x1 mi4ios6.stage=1 msm8974=cancro"


/* ====================================================================
 * Minimal Apple flattened device tree stub.
 *
 * Apple DT is NOT Linux FDT. Layout (pexpert/device_tree.h):
 *   DeviceTreeNode { u32 nProperties; u32 nChildren; }
 *   then nProperties * DeviceTreeNodeProperty:
 *     { char name[32]; u32 length; u8 value[length]; pad to 4 bytes }
 *
 * Single root node with 4 string properties; no children.
 * ==================================================================== */
.align 2
apple_dt_blob:
    .word   4                       /* nProperties */
    .word   0                       /* nChildren   */

    /* prop: name = "/" (Apple DT root entry name) */
    .asciz  "name"
    .space  (32 - 5)                /* pad name[] to 32 bytes */
    .word   2
    .asciz  "/"                     /* value "/" + NUL = 2 bytes */
    .byte   0, 0                    /* pad to 4-byte boundary */

    /* prop: compatible */
    .asciz  "compatible"
    .space  (32 - 11)
    .word   24
    .asciz  "qcom,msm8974-xnu-stage1"   /* 23 chars + NUL = 24 bytes */

    /* prop: model */
    .asciz  "model"
    .space  (32 - 6)
    .word   26
    .asciz  "Xiaomi Mi 4 cancro Stage1" /* 25 chars + NUL = 26 bytes */
    .byte   0, 0                    /* pad 26 -> 28 */

    /* prop: target-type */
    .asciz  "target-type"
    .space  (32 - 12)
    .word   7
    .asciz  "cancro"                /* 6 chars + NUL = 7 bytes */
    .byte   0                       /* pad 7 -> 8 */
apple_dt_blob_end:

.align 2
apple_dt_blob_len:
    .word   (apple_dt_blob_end - apple_dt_blob)


/* ---- mutable boot_args image (zeroed at load; filled by build_boot_args) ---- */
.bss
.align 3
.global boot_args_struct
boot_args_struct:
    .space  BA_SIZE

.align 4
stage1_stack:
    .space  STAGE1_STACK_SIZE
stage1_stack_top:

