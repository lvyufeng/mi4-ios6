# Experiment 89: Stage86 — High-Virtual IRQ Handler Relocation at 0x80000000 (L2 Pages)

**Date:** 2026-06-15
**Device:** Xiaomi Mi 4 cancro (MSM8974)
**Boot method:** Non-persistent `fastboot boot` (no flash, clean recovery to Android)
**Status:** ✅ **ACHIEVED** — `kernel_entry returned success`

## Goal

Prove that ARM exception handlers can execute from the **L2-page-mapped high-virtual
kernel address** at `0x80000000+offset` by relocating VBAR (Vector Base Address
Register) to the high-VA projection of the exception vectors and verifying a timer
IRQ is delivered and handled correctly.

This builds on Stage85:
- Stage85 proved: High-VA **code execution** at `0x80000000` via L2 pages (function call)
- Stage85 proved: High-VA **data access** at `0x80000000` via L2 pages
- **Stage86 proves: High-VA exception handling** — IRQ vector table + handler execution at `0x80000000`

## Result

The high-VA IRQ handler relocation test runs to completion and the timer IRQ is
successfully delivered and handled through the high-VA vector table:

```
stage86_xnu_arm_vm_init_high_va_irq_handler_status=0x86000001
stage86_xnu_arm_vm_init_high_va_irq_handler_satisfied_mask=0x0000003f
stage86_xnu_arm_vm_init_high_va_irq_handler_failure_mask=0x00000000
stage86_xnu_arm_vm_init_high_va_irq_handler_vectors_phys=0x000080a0
stage86_xnu_arm_vm_init_high_va_irq_handler_vectors_high_va=0x800080a0
stage86_xnu_arm_vm_init_high_va_irq_handler_high_va_vbar_set=0x00000001
stage86_xnu_arm_vm_init_high_va_irq_handler_irq_delivered=0x00000001
stage86_xnu_arm_vm_init_high_va_irq_handler_vbar_restored=0x00000001
loader_status=0x86000001
kernel_entry ok
kernel_entry returned success
```

`irq_delivered=0x00000001` is the proof: the exception vector table at high-VA
`0x800080a0` successfully handled a timer IRQ, proving that:
1. Exception vectors can be fetched from L2-mapped high-VA addresses
2. PC-relative branch from high-VA vector to high-VA C handler works
3. Exception handling (IRQ stack, register save/restore) works under high-VA pmap

## IRQ Handler Invocation Log

Four IRQ handler invocations observed during boot:

```
MI4IOS6_STAGE86 irq handler iar=0x00000000 id=0x00000000 count=0x00000001 timer_count=0x00000000
MI4IOS6_STAGE86 irq handler iar=0x00000013 id=0x00000013 count=0x00000001 timer_count=0x00000001
MI4IOS6_STAGE86 irq handler iar=0x00000013 id=0x00000013 count=0x00000002 timer_count=0x00000002
MI4IOS6_STAGE86 irq handler iar=0x00000013 id=0x00000013 count=0x00000003 timer_count=0x00000003
```

1. First IRQ: Early SGI selftest (`iar=0x00000000`)
2. Second IRQ: gic_timer_selftest (`iar=0x00000013`, timer PPI 19)
3. **Third IRQ: Stage86 low-VA VBAR test** (`count=2`) — diagnostic test
4. **Fourth IRQ: Stage86 high-VA VBAR test** (`count=3`) — **the proof** ✨

The timer IRQ counter incremented from 2 to 3 during the high-VA VBAR window,
confirming that the IRQ was successfully handled through the high-VA vector table.

## Root Cause Fixed: Timer PPI Disabled After Selftest

Initial attempts failed because `gic_timer_selftest` (in `mmu.c`) disables the
timer PPI in the GIC after completion:

```c
// From gic_timer_selftest:
if ((enable_before & (1u << GIC_TIMER_PPI0_ID)) == 0u ||
    (enable_before & (1u << GIC_TIMER_PPI1_ID)) == 0u) {
    mmio_write32(dist_base + GICD_ICENABLER0, GIC_TIMER_PPI_MASK & ~enable_before);
    barrier_dsb_isb();
}
```

Log evidence: `gic_timer_isenabler0_restored=0x00007fff` — timer PPI bits 18/19
are cleared.

**Solution**: Re-enable timer PPI in `GICD_ISENABLER0` before arming the timer
in the high-VA IRQ handler window:

```c
/* Re-enable timer PPI in GIC (gic_timer_selftest disabled it after completion) */
hvir_mmio_write32(0xf9000000u + GICD_ISENABLER0, GIC_TIMER_PPI_MASK);
hvir_dsb_isb();
```

## Implementation Details

### High-VA IRQ Handler Relocation Window

New file: `stage86/xnu_arm_vm_init_high_va_irq_handler.c`

**Algorithm**:
1. Save original TTBR0 and VBAR
2. Disable IRQs (safety: mask before reconfiguring VBAR)
3. Re-install candidate L1 (with L2-mapped `0x80000000` region)
4. **Diagnostic**: Test timer with low-VA VBAR first to isolate issues
5. If low-VA test succeeds, set VBAR to high-VA (`0x800080a0`)
6. Re-enable timer PPI in GIC
7. Arm generic timer for 500µs IRQ
8. Open IRQ window (unmask CPSR.I)
9. Spin waiting for timer IRQ (timeout 200ms)
10. Close IRQ window
11. Verify IRQ delivered (check `stage86_timer_irq_count` increment)
12. Restore original VBAR and TTBR0

**Key inline helpers**:
```c
static inline void hvir_write_vbar(uint32_t v) {
    __asm__ volatile ("mcr p15, 0, %0, c12, c0, 0" :: "r"(v));
}
static inline void hvir_enable_irqs(void) {
    __asm__ volatile ("cpsie i" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");
}
```

### Vector Table Address Calculation

Physical vectors: `0x000080a0`
High-VA projection: `virt_base + vectors_phys = 0x80000000 + 0x000080a0 = 0x800080a0`

The candidate L2 maps `VA 0x80000000+N → PA 0x00000000+N` (identity offset),
so the high-VA address is simply `virt_base + physical_address`.

### PC-Relative Branch Verification

The `vector_irq` code (in `vectors.S`) does:
```asm
vector_irq:
    sub lr, lr, #4
    stmdb sp!, {r0-r3, r12, lr}
    bl  stage86_irq_c_handler    # PC-relative branch
    ldmia sp!, {r0-r3, r12, lr}
    subs pc, lr, #0
```

When VBAR=`0x800080a0`, the IRQ vector is at `0x800080a0 + 0x18 = 0x800080b8`.
The `bl stage86_irq_c_handler` is a PC-relative branch encoding a ±32MB offset.

**Why it works**: Both the vector (`0x800080b8`) and the C handler
(`stage86_irq_c_handler` at `0x80047xxx`) are in the same L2-mapped high-VA
region. The **relative offset** between vector and handler is identical to the
low-VA case, so the same `bl` instruction works in both contexts.

## L2 Page Descriptor: Execute Permission

```c
#define L2_DESC_PAGE_SO 0x00000012u  /* Strongly-Ordered, AP=11, XN=0, small page type */
```

`0x00000012 = 0b10010`:
- Bit[1:0] = `10b` (small page type, 4KB)
- Bit[0] = 0 ⇒ **XN=0** ⇒ page is **executable**

ARMv7 short-descriptor small page: XN (Execute-Never) is bit[0]. When XN=0, the
page is executable. This is correct for kernel code pages.

## What This Completes

The **high-virtual exception handling** milestone. The L2-mapped region at
`0x80000000` is now proven capable of:
- ✅ Data access (Stage84/85)
- ✅ Code execution via function calls (Stage85)
- ✅ Code execution via exception vectors (Stage86) ← **NEW**

This is the foundation for a full XNU kernel pmap: real kernel exception vectors
would live in this region, and exception handling through L2 translation now
demonstrably works on the target hardware.

## Verification Commands

```bash
# Build
cd stage86 && ./build.sh

# Non-persistent boot
sudo fastboot -s 4a2fe00b boot out/stage86/stage86-qcdt.img

# Capture log (needs root: /proc/last_kmsg is system:log)
sudo adb -s 4a2fe00b shell 'su -c "cat /proc/last_kmsg"' > /tmp/stage86-last_kmsg.txt

# Validate
grep "high_va_irq_handler_status=0x86000001" /tmp/stage86-last_kmsg.txt
grep "irq_delivered=0x00000001"              /tmp/stage86-last_kmsg.txt
grep "kernel_entry returned success"          /tmp/stage86-last_kmsg.txt
```

## Safety Boundaries Preserved

All Stage86 safety invariants hold (verified by the result fields):
- ✅ Stage-owned code only (`xnu_arm_vm_init_high_va_irq_handler.c`)
- ✅ No public XNU execution
- ✅ No Mach-O execution (inert fixture parsed/staged, never executed)
- ✅ No persistent writes
- ✅ Non-persistent boot via `fastboot boot` (no flash)
- ✅ TTBR0 roundtrip: candidate L1 installed, used, restored (matches original)
- ✅ VBAR roundtrip: high-VA VBAR set, used, restored (matches original)
- ✅ Clean device recovery to Android (`boot_completed=1`)

## Next Steps

With high-VA exception handling proven, the next milestones could include:
1. **Stage87**: High-VA data abort handler (prove MMU fault handling at high-VA)
2. **Stage88**: High-VA undefined instruction handler
3. **Stage89**: Full high-VA kernel entry (combine all exception handlers)
4. **Stage90**: High-VA kernel bootstrap (real XNU `_start` at high-VA)
