# Stage84+ SGI/IRQ Timer Boundary Planning

## Objective

Extend Stage83's live kernel virtual address space (L1+L2 pmap with high-virtual mapping at `0x80000000`) to cross the interrupt boundary by enabling GIC interrupt delivery and proving Stage-owned IRQ handling works under the live pmap.

Stage84+ should install a Stage-owned IRQ vector handler, configure GIC for interrupt delivery, enable a timer interrupt source, and verify that IRQs fire, are handled correctly, and return to the main execution path without corrupting the live pmap or control registers.

## Scope

### What Stage84+ Must Prove

1. **GIC configuration for IRQ delivery**:
   - GIC distributor and CPU interface initialized for interrupt routing
   - At least one interrupt source (timer) enabled and configured
   - Interrupt priority, target CPU, and enable bits correctly set

2. **ARMv7 IRQ vector installation**:
   - Stage-owned IRQ handler installed in vector table at offset `0x18` (IRQ exception)
   - Vector table base matches VBAR or reset vector base
   - Handler is PC-relative or high-virtual addressable under the live pmap

3. **Stage-owned IRQ handler execution**:
   - Handler fires when timer interrupt triggers
   - Handler reads GIC interrupt acknowledge register (GICC_IAR) to identify source
   - Handler services the interrupt (e.g., increments a counter, clears timer status)
   - Handler writes GIC end-of-interrupt register (GICC_EOIR) to signal completion
   - Handler returns via `subs pc, lr, #4` (IRQ return instruction)

4. **Timer interrupt source configuration**:
   - Use MSM8974 platform timer (qtimer or GPT) or ARM generic timer (if accessible)
   - Configure timer for periodic interrupt at known interval (e.g., 10ms, 100ms)
   - Verify timer interrupt ID matches GIC configuration

5. **IRQ delivery verification under live pmap**:
   - Install live pmap (TTBR0 switch, TLB invalidate)
   - Enable IRQs (`cpsie i`)
   - Wait for timer interrupt (polling or delay loop)
   - Verify IRQ handler fired (check counter incremented)
   - Disable IRQs (`cpsid i`)
   - Restore original pmap

6. **Safety boundaries**:
   - IRQ handler must not call public XNU runtime code
   - IRQ handler must not perform persistent writes
   - IRQ handler must preserve caller-saved registers
   - IRQ handler must not corrupt stack or MMU state
   - IRQs must be disabled during pmap install/restore transitions
   - Original IRQ state must be restored before returning to loader

### What Stage84+ Must NOT Do

- Call public XNU IRQ/timer runtime (`PE_init_clock`, `clock_init`, `rtclock_intr`)
- Execute generated Mach-O code in IRQ handler
- Perform flash/partition/persistent writes in IRQ handler
- Enable IRQs during TTBR0/TLB transitions (race window)
- Leave IRQs enabled after live pmap restore
- Mutate public XNU interrupt state or GIC registers beyond Stage-owned setup

## Technical Background

### ARMv7 Exception Vector Table

Standard layout (8 entries × 4 bytes = 32 bytes):
```
0x00: Reset
0x04: Undefined instruction
0x08: Supervisor call (SVC)
0x0c: Prefetch abort
0x10: Data abort
0x14: Reserved
0x18: IRQ                    ← Stage84 IRQ handler here
0x1c: FIQ
```

Each entry is typically a branch instruction: `b <handler>` or `ldr pc, [pc, #offset]`.

### GIC (ARM Generic Interrupt Controller) v2 Registers

**Distributor (GICD_BASE = `0xf9001000` on MSM8974)**:
- `GICD_CTLR` (`+0x000`): Global enable (bit 0)
- `GICD_ISENABLER[n]` (`+0x100 + 4*n`): Interrupt enable set (32 interrupts per register)
- `GICD_IPRIORITYR[n]` (`+0x400 + n`): Interrupt priority (8 bits per interrupt)
- `GICD_ITARGETSR[n]` (`+0x800 + n`): Target CPU mask (8 bits per interrupt)
- `GICD_ICFGR[n]` (`+0xc00 + 4*n`): Edge/level configuration

**CPU Interface (GICC_BASE = `0xf9002000` on MSM8974)**:
- `GICC_CTLR` (`+0x000`): CPU interface enable (bit 0)
- `GICC_PMR` (`+0x004`): Priority mask (interrupts below this priority are masked)
- `GICC_IAR` (`+0x00c`): Interrupt acknowledge (read to get interrupt ID)
- `GICC_EOIR` (`+0x010`): End of interrupt (write interrupt ID to signal completion)

### MSM8974 Timer Sources

**Option 1: ARM Generic Timer** (if accessible from EL1/non-secure):
- Physical timer: CNTP_CTL, CNTP_CVAL, CNTP_TVAL
- Virtual timer: CNTV_CTL, CNTV_CVAL, CNTV_TVAL
- Interrupt ID: typically PPI 29 (physical) or 27 (virtual)
- Frequency: read from CNTFRQ (usually 19.2 MHz on MSM8974)

**Option 2: QTimer (Qualcomm MSM timer)**:
- Base address: `0xf9020000` (check device tree)
- Registers: QTMR_V1_CNTPCT_LO, QTMR_V1_CNTP_CVAL_LO, QTMR_V1_CNTP_CTL
- Interrupt ID: varies by MSM8974 configuration (check device tree interrupt-map)

**Option 3: GPT (General Purpose Timer)**:
- Legacy MSM timer, less common on MSM8974
- Prefer ARM generic timer or QTimer if available

**Recommended**: Start with ARM generic timer (CNTP) if accessible. If secure-mode restrictions prevent access, fall back to QTimer with device tree interrupt ID lookup.

## Implementation Strategy

### Phase 1: GIC Initialization (Stage-owned)

Create `stage84/gic_irq.c`:

```c
void stage84_gic_init(void) {
    volatile uint32_t *gicd_ctlr = (volatile uint32_t *)0xf9001000;
    volatile uint32_t *gicc_ctlr = (volatile uint32_t *)0xf9002000;
    volatile uint32_t *gicc_pmr = (volatile uint32_t *)0xf9002004;

    /* Enable GIC distributor */
    *gicd_ctlr = 1;

    /* Enable GIC CPU interface, set priority mask to allow all */
    *gicc_ctlr = 1;
    *gicc_pmr = 0xff;  /* Allow all priorities */
}

void stage84_gic_enable_interrupt(uint32_t irq_id) {
    volatile uint32_t *gicd_isenabler = (volatile uint32_t *)(0xf9001100 + 4 * (irq_id / 32));
    volatile uint32_t *gicd_ipriorityr = (volatile uint8_t *)(0xf9001400 + irq_id);
    volatile uint32_t *gicd_itargetsr = (volatile uint8_t *)(0xf9001800 + irq_id);

    /* Set priority (0x80 = medium) */
    *gicd_ipriorityr = 0x80;

    /* Target CPU0 */
    *gicd_itargetsr = 0x01;

    /* Enable interrupt */
    *gicd_isenabler |= (1u << (irq_id % 32));
}
```

### Phase 2: Timer Configuration (Stage-owned)

Create `stage84/timer_irq.c`:

```c
#define ARM_TIMER_PPI 29  /* Physical timer PPI */

void stage84_timer_init(uint32_t interval_us) {
    uint32_t cntfrq;
    uint64_t ticks;

    /* Read timer frequency */
    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(cntfrq));

    /* Calculate ticks for interval */
    ticks = ((uint64_t)cntfrq * interval_us) / 1000000;

    /* Set timer value and enable */
    __asm__ volatile("mcr p15, 0, %0, c14, c2, 0" : : "r"((uint32_t)ticks));  /* CNTP_TVAL */
    __asm__ volatile("mcr p15, 0, %0, c14, c2, 1" : : "r"(1));                /* CNTP_CTL enable */
}

void stage84_timer_clear(void) {
    uint32_t ctl;
    __asm__ volatile("mrc p15, 0, %0, c14, c2, 1" : "=r"(ctl));  /* Read CNTP_CTL */
    ctl &= ~0x02;  /* Clear ISTATUS bit */
    __asm__ volatile("mcr p15, 0, %0, c14, c2, 1" : : "r"(ctl));
}
```

### Phase 3: IRQ Vector Handler (Stage-owned assembly)

Extend `stage84/vectors.S`:

```asm
.global stage84_irq_handler_entry
.type stage84_irq_handler_entry, %function

stage84_irq_handler_entry:
    /* Save context */
    sub     lr, lr, #4              /* Adjust LR for IRQ return */
    srsdb   sp!, #0x13              /* Save LR_irq and SPSR_irq to SVC stack */
    cps     #0x13                   /* Switch to SVC mode */
    push    {r0-r3, r12, lr}        /* Save caller-saved registers */

    /* Call C handler */
    bl      stage84_irq_handler_c

    /* Restore context */
    pop     {r0-r3, r12, lr}
    rfeia   sp!                     /* Return from exception */

.size stage84_irq_handler_entry, .-stage84_irq_handler_entry
```

Create `stage84/irq_handler.c`:

```c
static volatile uint32_t stage84_irq_count = 0;

void stage84_irq_handler_c(void) {
    volatile uint32_t *gicc_iar = (volatile uint32_t *)0xf900200c;
    volatile uint32_t *gicc_eoir = (volatile uint32_t *)0xf9002010;

    /* Read interrupt ID */
    uint32_t irq_id = *gicc_iar;

    /* Increment counter */
    stage84_irq_count++;

    /* If timer interrupt, clear timer status */
    if (irq_id == ARM_TIMER_PPI) {
        stage84_timer_clear();
    }

    /* Signal end of interrupt */
    *gicc_eoir = irq_id;
}

uint32_t stage84_get_irq_count(void) {
    return stage84_irq_count;
}
```

### Phase 4: Vector Table Installation

Update `stage84/mmu.c` or create `stage84/vector_install.c`:

```c
extern void stage84_irq_handler_entry(void);

void stage84_install_irq_vector(void) {
    /* Assuming vector table at 0x00000000 (reset vectors) */
    volatile uint32_t *vector_table = (volatile uint32_t *)0x00000000;
    uint32_t handler_addr = (uint32_t)(uintptr_t)stage84_irq_handler_entry;

    /* Calculate branch offset for vector[6] (IRQ at offset 0x18) */
    /* Branch instruction: 0xea000000 | ((offset >> 2) & 0x00ffffff) */
    uint32_t offset = handler_addr - (0x00000018 + 8);  /* PC+8 in ARM */
    uint32_t branch_instr = 0xea000000 | ((offset >> 2) & 0x00ffffff);

    vector_table[6] = branch_instr;  /* Install at IRQ vector offset */
}
```

**Note**: If VBAR is set (not at reset `0x00000000`), read VBAR and install at `VBAR + 0x18`.

### Phase 5: Live Pmap IRQ Window

Extend `stage84/xnu_arm_vm_init_full_pmap.c` (or create new `xnu_arm_vm_init_irq_window.c`):

```c
int stage84_xnu_arm_vm_init_irq_window_run(struct boot_args *args) {
    uint32_t irq_count_before, irq_count_after;
    uint32_t saved_cpsr;

    /* Initialize GIC and timer */
    stage84_gic_init();
    stage84_gic_enable_interrupt(ARM_TIMER_PPI);
    stage84_timer_init(100000);  /* 100ms interval */

    /* Install IRQ vector */
    stage84_install_irq_vector();

    /* Install live pmap (from Stage83) */
    mmu_stage84_full_pmap_install();

    /* Save CPSR and enable IRQs */
    __asm__ volatile("mrs %0, cpsr" : "=r"(saved_cpsr));
    __asm__ volatile("cpsie i");  /* Enable IRQs */

    /* Record counter before wait */
    irq_count_before = stage84_get_irq_count();

    /* Wait for at least one interrupt (busy loop ~200ms) */
    for (volatile uint32_t i = 0; i < 10000000; i++) {
        /* Spin */
    }

    /* Record counter after wait */
    irq_count_after = stage84_get_irq_count();

    /* Disable IRQs */
    __asm__ volatile("cpsid i");

    /* Restore original pmap */
    mmu_stage84_full_pmap_restore();

    /* Verify IRQ fired */
    if (irq_count_after > irq_count_before) {
        /* Success: at least one IRQ handled */
        return STAGE84_STATUS_OK;
    } else {
        /* Failure: no IRQs received */
        return STAGE84_STATUS_FAIL(STAGE84_FAIL_IRQ_NOT_FIRED);
    }
}
```

### Phase 6: Safety Validation

Add explicit checks in `stage84/macho_probe.c` loader preflight:

```c
/* Verify IRQ handler is Stage-owned, not public XNU */
if (stage84_irq_handler_is_public_xnu()) {
    failure_mask |= STAGE84_LOADER_FAIL_PUBLIC_IRQ_HANDLER;
}

/* Verify no persistent writes in IRQ handler */
if (stage84_irq_handler_persistent_write_attempted()) {
    failure_mask |= STAGE84_LOADER_FAIL_IRQ_PERSISTENT_WRITE;
}

/* Verify IRQs disabled after window */
uint32_t cpsr;
__asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
if ((cpsr & 0x80) == 0) {  /* I bit cleared = IRQs enabled */
    failure_mask |= STAGE84_LOADER_FAIL_IRQ_NOT_DISABLED;
}
```

## Expected Validation Markers

Hardware validation should show:

```
stage84_gic_init_status=0x84000001
stage84_timer_init_status=0x84000001
stage84_irq_vector_installed=0x00000001
stage84_irq_count_before=0x00000000
stage84_irq_count_after=0x00000002  (or higher)
stage84_irq_window_status=0x84000001
stage84_irq_handler_fired=0x00000001
stage84_public_xnu_irq_executed=0x00000000
stage84_persistent_write_in_irq=0x00000000
stage84_irqs_disabled_after_restore=0x00000001
loader_status=0x84000001
kernel_entry returned success
```

## Risks and Mitigations

### Risk 1: ARM Generic Timer Not Accessible (Secure-Mode)

**Symptom**: Reading CNTFRQ or CNTP_CTL traps to monitor mode or returns zero.

**Mitigation**: Fall back to QTimer (Qualcomm platform timer). Parse device tree for QTimer base address and interrupt ID:
```c
/* Device tree: /soc/timer@f9020000 */
#define QTIMER_BASE 0xf9020000
#define QTIMER_IRQ  (device_tree_interrupt_id)
```

### Risk 2: Vector Table Not Writable (MMU Protection)

**Symptom**: Write to `vector_table[6]` causes data abort.

**Mitigation**: Ensure vector table is in writable memory. If at reset vectors (`0x00000000`), it's typically writable SRAM. If VBAR points to read-only region, allocate Stage-owned vector table in BSS and update VBAR:
```c
static uint32_t stage84_vector_table[8] __attribute__((aligned(32)));
/* Copy original vectors, install IRQ handler, set VBAR */
__asm__ volatile("mcr p15, 0, %0, c12, c0, 0" : : "r"(stage84_vector_table));
```

### Risk 3: IRQ Storm (Handler Doesn't Clear Source)

**Symptom**: IRQ handler fires continuously, device hangs.

**Mitigation**: Ensure `stage84_timer_clear()` actually clears the timer interrupt status. For ARM generic timer, clearing ISTATUS bit in CNTP_CTL may require writing to CNTP_TVAL or disabling the timer. Test with single-shot timer first before enabling periodic.

### Risk 4: GIC Already Configured by Bootloader

**Symptom**: Interrupts don't fire, or wrong interrupts fire.

**Mitigation**: Read GIC state before initialization. Log distributor and CPU interface enable bits, priority mask, and enabled interrupt set. If bootloader left GIC in unexpected state, perform soft reset or explicit disable before re-initializing.

## Build Integration

Update `stage84/build.sh`:

```bash
SOURCES=(
  # ... existing sources ...
  xnu_arm_vm_init_full_pmap.c
  gic_irq.c
  timer_irq.c
  irq_handler.c
  vector_install.c
  xnu_arm_vm_init_irq_window.c
  stage84_main.c
)
```

Update `stage84/stage84.h` with IRQ ABI:

```c
#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_VERSION 1u
#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_REQUIRED_MASK 0x0001ffffu

#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_GIC_INIT              0x00000001u
#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_TIMER_INIT            0x00000002u
#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_IRQ_VECTOR_INSTALLED  0x00000004u
#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_IRQ_ENABLED           0x00000008u
#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_IRQ_FIRED             0x00000010u
#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_IRQ_HANDLER_CORRECT   0x00000020u
#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_IRQ_DISABLED          0x00000040u
#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_SAT_PMAP_PRESERVED        0x00000080u

#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_FAIL_GIC_INIT             0x00000001u
#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_FAIL_TIMER_INIT           0x00000002u
#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_FAIL_VECTOR_INSTALL       0x00000004u
#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_FAIL_IRQ_NOT_FIRED        0x00000008u
#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_FAIL_PUBLIC_IRQ_HANDLER   0x00000010u
#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_FAIL_PERSISTENT_WRITE     0x00000020u
#define STAGE84_XNU_ARM_VM_INIT_IRQ_WINDOW_FAIL_IRQ_STILL_ENABLED    0x00000040u
```

## Documentation

Create:
- `stage84/README.md`: Overview of IRQ window implementation
- `docs/experiment-87-stage84-sgi-irq-timer.md`: Hardware validation report

## Next Steps After Stage84

Once Stage84 proves IRQ delivery and handling under the live pmap:

1. **Stage85: High-virtual code execution**: Jump to Stage-owned code at `0x80000000+offset`, not just data access
2. **Stage86: FIQ boundary**: Prove FIQ delivery and handling (if needed for platform)
3. **Stage87: IRQ nesting**: Prove nested IRQ handling (re-enable IRQs in handler)
4. **Stage88: Public XNU IRQ handoff**: Transition to calling public XNU `PE_init_clock()` and `rtclock_intr()`

## Conclusion

Stage84+ crosses the interrupt boundary by proving that Stage-owned IRQ handlers can fire, execute, and return correctly under the Stage83 live L1+L2 pmap. This is a critical step toward full kernel execution, as XNU kernel bootstrap requires functioning IRQ/timer infrastructure for scheduling, timeouts, and device I/O.

The implementation must remain fail-closed, Stage-owned, and non-persistent. All GIC/timer/IRQ handler code is Stage84-owned, not public XNU. Device recovery must remain clean (IRQs disabled, original pmap restored, no persistent writes).
