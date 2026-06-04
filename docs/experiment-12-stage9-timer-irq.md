# Experiment 12 — Stage9 ARM Generic Timer IRQ

Date: 2026-06-04

Goal: move beyond software-generated interrupts and prove that a real timer interrupt can reach the custom IRQ vector, run the C GIC handler, complete through `GICC_EOIR`, and return to the XNU-adjacent `kernel_entry()` path.

Stage9 still does **not** run XNU or iOS. It proves another low-level primitive required by a future MSM8974 XNU platform path: timer-driven IRQ delivery.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- External device IRQs are not enabled.
- The test enables only the ARM generic timer PPIs in local `GICD_ISENABLER0`.
- The physical timer one-shot is short and bounded.
- IRQs are unmasked only during short selftest windows.
- The timer is masked/disabled again after the IRQ is observed.
- The Stage8 returnable IRQ vector and abort safety net remain installed.

## MSM8974 timer IRQ source

The local cancro kernel source has this `msm8974.dtsi` timer node:

```text
timer {
    compatible = "arm,armv7-timer";
    interrupts = <1 2 0 1 3 0>;
    clock-frequency = <19200000>;
};
```

The Linux GIC OF translator maps PPIs by adding 16 to skip SGIs, so the two timer PPIs are GIC INTID 18 and 19.

Stage9 enables both as a conservative first hardware-timer test:

```text
GIC_TIMER_PPI0_ID = 18
GIC_TIMER_PPI1_ID = 19
GIC_TIMER_PPI_MASK = 0x000c0000
```

The hardware delivered INTID 19 on this run.

## What Stage9 adds over Stage8

Stage8 proved:

- returnable IRQ vector path,
- SGI0 self-interrupt delivery,
- `GICC_IAR` / `GICC_EOIR` handler flow.

Stage9 adds:

- CP15 physical timer control through `CNTP_CTL` and `CNTP_TVAL`,
- a one-shot timer selftest (`gic_timer_selftest()`),
- timer IRQ classification in `stage9_irq_c_handler()`,
- timer IRQ counters and last-control-state logging,
- restoration of the local PPI enable mask after the selftest.

Important CP15 registers used:

```text
CNTFRQ    p15 c14,c0,0  already confirmed 19.2 MHz
CNTP_TVAL p15 c14,c2,0  physical timer relative deadline
CNTP_CTL  p15 c14,c2,1  physical timer enable/mask/status
```

## Built image

```bash
./stage9/build.sh
```

Successful local build:

```text
out/stage9/stage9-qcdt.img
sha256=d24ed68789d61effe23cad41208a4bd26412d2f942b6d49e0c4434c9578f8008
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=20156 (0x4ebc)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage9 mi4ios6=stage9 c-runtime apple-dt
```

Important symbols:

```text
00008000 T _start
000080a0 T stage9_vectors
0000a368 T stage9_irq_c_handler
0000a530 T gic_sgi_selftest
0000a780 T gic_timer_selftest
0000ab1c T kernel_entry
0000af6c T stage9_main
00011f10 B GIC_state_stage9
00014000 B __stage9_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage9/stage9-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2484 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.088s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage9-last_kmsg.txt
```

The recovered log was 6925 bytes and contained:

```text
143 MI4IOS6_STAGE9 markers
120 MI4IOS6_STAGE9_XNU markers
```

## Key recovered timer IRQ markers

```text
MI4IOS6_STAGE9_XNU gic timer selftest begin
MI4IOS6_STAGE9_XNU gic_timer_ppi0_id=0x00000012
MI4IOS6_STAGE9_XNU gic_timer_ppi1_id=0x00000013
MI4IOS6_STAGE9_XNU gic_timer_ppi_mask=0x000c0000
MI4IOS6_STAGE9_XNU gic_timer_isenabler0_before=0x00007fff
MI4IOS6_STAGE9_XNU gic_timer_ispendr0_before=0x20400000
MI4IOS6_STAGE9_XNU gic_timer_priority_word=0xa0a0a0a0
MI4IOS6_STAGE9_XNU gic_timer_cntp_ctl_before=0x00000002
MI4IOS6_STAGE9_XNU gic_timer_cntp_tval_before=0xc69113e1
MI4IOS6_STAGE9_XNU gic_timer_ticks=0x00017700
MI4IOS6_STAGE9_XNU gic_timer_cpsr_before=0x60000193
MI4IOS6_STAGE9_XNU gic_timer_isenabler0_after=0x000c7fff
MI4IOS6_STAGE9_XNU gic_timer_cntp_ctl_armed=0x00000001
MI4IOS6_STAGE9_XNU gic_timer_cntp_tval_armed=0x000175af
MI4IOS6_STAGE9 irq handler iar=0x00000013 id=0x00000013 count=0x00000001 timer_count=0x00000001
MI4IOS6_STAGE9_XNU gic_timer_cpsr_after=0x80000193
MI4IOS6_STAGE9_XNU gic_timer_irq_count=0x00000001
MI4IOS6_STAGE9_XNU gic_timer_timer_count=0x00000001
MI4IOS6_STAGE9_XNU gic_timer_other_count=0x00000000
MI4IOS6_STAGE9_XNU gic_timer_spurious_count=0x00000000
MI4IOS6_STAGE9_XNU gic_timer_last_iar=0x00000013
MI4IOS6_STAGE9_XNU gic_timer_last_irq_id=0x00000013
MI4IOS6_STAGE9_XNU gic_timer_last_timer_id=0x00000013
MI4IOS6_STAGE9_XNU gic_timer_last_timer_ctl=0x00000005
MI4IOS6_STAGE9_XNU gic_timer_cntp_ctl_after=0x00000002
MI4IOS6_STAGE9_XNU gic_timer_isenabler0_restored=0x00007fff
MI4IOS6_STAGE9_XNU gic timer selftest ok
```

The `CNTP_CTL` value observed in the handler was `0x5`, meaning the timer was enabled and the interrupt status bit was set when the handler ran. After shutdown it returned to `0x2` (masked/disabled).

Final success markers:

```text
MI4IOS6_STAGE9_XNU kernel_entry ok
MI4IOS6_STAGE9 kernel_entry returned success
MI4IOS6_STAGE9 attempting MSM8974 PS_HOLD reset
```

## Interpretation

The Stage9 payload successfully programmed the ARM generic physical timer, enabled the corresponding local GIC PPIs, opened a bounded IRQ window, and received a real timer interrupt at INTID 19. The handler masked/disabled the timer, wrote EOIR, returned to C, and the kernel skeleton completed normally.

This proves:

- timer PPI delivery works on cancro under the custom vector table,
- the Stage8 returnable IRQ path handles both SGI and timer interrupts,
- the 19.2 MHz generic timer can serve as an early clockevent source,
- GIC local PPI enable/restore is safe enough for short experiments,
- no unrelated external IRQs were required.

## Success criteria — met

1. bootloader accepted `stage9-qcdt.img`: yes
2. Stage8 SGI0 selftest still succeeded: yes
3. timer PPI was enabled and a one-shot deadline was armed: yes
4. IRQ handler logged `iar=0x00000013 id=0x00000013`: yes
5. timer IRQ count reached 1: yes
6. spurious/other interrupt counts stayed 0: yes
7. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage10 can start memory-management bring-up:

- build a small ARMv7 section page table,
- identity-map the payload RAM, RAM console, IMEM, GIC, timer, and PS_HOLD MMIO regions,
- enable MMU with caches still disabled first,
- verify code/data/MMIO access and ram_console logging continue under translation,
- then return to Android through PS_HOLD.
