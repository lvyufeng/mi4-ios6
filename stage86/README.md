# Stage84: GIC/IRQ Timer Boundary

Stage84 extends Stage83's live kernel virtual address space (L1+L2 pmap with high-virtual mapping at `0x80000000`) to cross the interrupt boundary by enabling GIC interrupt delivery and proving Stage-owned IRQ handling works under the live pmap.

## Key Validation Results

Hardware validation on `cancro` MSM8974 (serial `4a2fe00b`):

- **Status**: `loader_status=0x86000001` (OK)
- **IRQ window status**: `stage86_xnu_arm_vm_init_irq_window_status=0x86000001` (OK)
- **Satisfied mask**: `0x00003fff` (all required bits set)
- **Failure mask**: `0x00000000` (zero failures)
- **IRQ delivered**: `irq_count=0x00000001`, `timer_irq_count=0x00000001`
- **GIC configured**: `0x00000001` (distributor + CPU interface enabled)
- **Timer configured**: `0x00000001` (ARM generic timer, 100ms interval)
- **IRQ handled**: `0x00000001` (Stage-owned handler executed)
- **GIC restored**: Original GICC_CTLR/GICD_CTLR preserved
- **Safety boundaries**: All preserved (no public runtime, no persistent writes)
- **Device recovery**: Clean return to Android

## Architecture

### GIC Configuration

- **GIC Distributor Base**: `0xf9001000`
- **GIC CPU Interface Base**: `0xf9002000`
- **Operations**:
  - Enable distributor (GICD_CTLR = 1)
  - Enable CPU interface (GICC_CTLR = 1)
  - Set priority mask (GICC_PMR = 0xff, allow all)
  - Enable timer interrupt (PPI 27, priority 0x80, target CPU0)

### ARM Generic Timer

- **Timer source**: ARM Generic Timer (virtual timer)
- **IRQ number**: PPI 27 (GIC interrupt ID)
- **Frequency**: 1.2 MHz (read from CNTFRQ)
- **Interval**: 100ms (100,000 ticks)
- **Configuration**:
  - CNTV_TVAL = interval ticks
  - CNTV_CTL = 0x1 (ENABLE=1, IMASK=0)

### IRQ Vector Handler

ARMv7 IRQ vector at offset `0x18` in vector table:
```asm
vector_irq:
    sub lr, lr, #4              /* Adjust return address */
    stmdb sp!, {r0-r3, r12, lr} /* Save volatile registers */
    bl  stage86_irq_handler_c   /* Call C handler */
    ldmia sp!, {r0-r3, r12, lr} /* Restore registers */
    subs pc, lr, #0             /* Return, restoring CPSR from SPSR */
```

C handler (`stage86_irq_handler_c`):
1. Read GIC IAR to acknowledge and get interrupt ID
2. Increment IRQ counter
3. If timer IRQ (ID 27), clear/re-arm timer
4. Write GIC EOIR to signal end-of-interrupt

### IRQ Window Sequence

1. Check Stage83 full pmap prerequisite (L1+L2 pmap installed)
2. Initialize GIC (distributor + CPU interface)
3. Enable timer interrupt in GIC
4. Configure ARM timer (100ms interval)
5. Save CPSR and enable IRQs (`cpsie i`)
6. Wait for timer interrupt (busy loop ~200ms)
7. Verify IRQ fired (counter incremented)
8. Disable IRQs (`cpsid i`)
9. Disable timer
10. Validate GIC/timer state restored

## Build

```bash
cd /mnt/data/mi4-ios6/stage86
./build.sh
```

Artifacts:
- `out/stage86/stage86.elf` (507KB)
- `out/stage86/stage86.bin` (454KB)
- `out/stage86/stage86-qcdt.img` (2.9MB, includes QCDT device tree)

## Non-Persistent Hardware Boot

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot out/stage86/stage86-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/stage86-last_kmsg.txt
```

Expected pass markers:
```
stage86_xnu_arm_vm_init_irq_window_status=0x86000001
stage86_xnu_arm_vm_init_irq_window_satisfied_mask=0x00003fff
stage86_xnu_arm_vm_init_irq_window_failure_mask=0x00000000
stage86_xnu_arm_vm_init_irq_window_gic_configured=0x00000001
stage86_xnu_arm_vm_init_irq_window_timer_configured=0x00000001
stage86_xnu_arm_vm_init_irq_window_irq_enabled=0x00000001
stage86_xnu_arm_vm_init_irq_window_timer_irq_delivered=0x00000001
stage86_xnu_arm_vm_init_irq_window_irq_handled=0x00000001
loader_status=0x86000001
kernel_entry returned success
```

## Implementation Notes

### Timer Source Selection

Stage84 uses the ARM Generic Timer virtual timer (CNTV) rather than the physical timer (CNTP) or platform-specific QTimer. The virtual timer is accessible from non-secure EL1 and does not require Qualcomm-specific device tree parsing.

### IRQ Safety During Pmap Transitions

Stage84 keeps IRQs disabled during TTBR0/TLB transitions (Stage83 pmap install/restore) to avoid race conditions. IRQs are only enabled after the live pmap is fully installed and verified.

### GIC State Preservation

Original GIC state (GICC_CTLR, GICD_CTLR, GICC_PMR) is read before configuration and restored after the IRQ window closes, ensuring the bootloader/Android kernel sees consistent GIC state on recovery.

## Method-C Safety Boundaries

Stage84 maintains all Method-C constraints:

- **Stage-owned code**: All GIC/timer/IRQ handler code is Stage84-owned, not public XNU
- **No public runtime**: Zero calls to `PE_init_clock`, `rtclock_intr`, or any public IRQ runtime
- **No Mach-O exec**: The inert fixture is never executed
- **No persistent writes**: Zero flash/partition/file writes
- **Fail-closed**: Required-mask validation with explicit failure tracking
- **Non-persistent boot**: Hardware validation via `fastboot boot` only (never `flash`)
- **Clean recovery**: Device returns to Android, no persistent state change

## Progression from Stage83

| Feature | Stage83 | Stage84 |
|---------|---------|---------|
| L1+L2 pmap | ✓ | ✓ |
| High-VA data access | ✓ | ✓ |
| GIC configured | ✗ | ✓ |
| Timer configured | ✗ | ✓ |
| IRQ enabled | ✗ | ✓ |
| IRQ delivered | ✗ | ✓ |
| IRQ handled | ✗ | ✓ |
| Stage-owned IRQ handler | ✗ | ✓ |

## Next Steps

Stage84 enables:

1. **High-virtual code execution**: Execute Stage-owned code from `0x80000000+offset` (not just data access)
2. **Multiple IRQ sources**: Prove handling of multiple interrupt sources (not just timer)
3. **Nested IRQ handling**: Re-enable IRQs within handler for nested interrupt support
4. **Public XNU IRQ handoff**: Transition to public XNU `PE_init_clock()` and `rtclock_intr()`

## References

- Planning: `docs/stage86-sgi-irq-timer-planning.md`
- Hardware validation: `docs/experiment-87-stage86-sgi-irq-timer.md` (to be created)
- Baseline: Stage83 (commit `8abe158`)
- GIC spec: ARM Generic Interrupt Controller Architecture Specification v2.0
- Timer spec: ARM Architecture Reference Manual ARMv7-A, Generic Timer
