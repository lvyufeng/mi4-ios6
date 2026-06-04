# Experiment 11 — Stage8 Controlled SGI/IRQ Delivery

Date: 2026-06-04

Goal: prove that the custom ARMv7 IRQ vector path can take a GIC interrupt, acknowledge it through `GICC_IAR`, end it through `GICC_EOIR`, and return to the interrupted C path.

Stage8 still does **not** run XNU or iOS. It is an XNU-adjacent bring-up stage that proves one interrupt-controller primitive needed by a future MSM8974 XNU platform path.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- External device IRQs are not deliberately enabled.
- The test uses SGI0, targeted only to the current CPU via `GICD_SGIR` target filter `self`.
- The IRQ window is short and bounded.
- The abort handlers remain installed as the safety net for unexpected faults.

## What Stage8 adds over Stage7

Stage7 proved read-only GIC discovery and validation. Stage8 adds controlled interrupt delivery:

- a returnable IRQ vector path in `vectors.S`,
- `stage8_irq_c_handler()` in `gic.c`,
- `gic_sgi_selftest()` in `gic.c`,
- global counters for observed IRQs and SGI0 events,
- an XNU-adjacent `kernel_entry()` call to run the SGI selftest after PE/GIC/timebase validation.

The IRQ vector now adjusts `LR_irq`, saves volatile registers, calls C, restores registers, and returns through `subs pc, lr, #0` so CPSR is restored from `SPSR_irq`.

## Source files changed

```text
stage8/vectors.S
stage8/stage8.h
stage8/gic.c
stage8/xnu_kernel.c
stage8/stage8_main.c
stage8/boot_args.c
```

Important new GIC registers used:

```text
GICC_IAR   0x00c  acknowledge/read active interrupt
GICC_EOIR  0x010  signal interrupt completion
GICD_SGIR  0xf00  generate software interrupt
```

The selftest sequence is:

1. verify distributor and CPU interface are already enabled,
2. clear Stage8 IRQ counters,
3. ensure SGI0 is enabled in `GICD_ISENABLER0`,
4. write `GICD_SGIR = (2 << 24) | 0` to send SGI0 to the current CPU,
5. briefly unmask CPSR.I with `cpsie i`,
6. wait until SGI0 is observed or a 20 ms timebase timeout expires,
7. mask IRQs again with `cpsid i`,
8. validate that interrupt ID 0 was handled.

## Built image

```bash
./stage8/build.sh
```

Successful local build:

```text
out/stage8/stage8-qcdt.img
sha256=6a27e0ef89ff944938e11ba3459cbec883295503fb64553a35410f79eeb27790
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=18160 (0x46f0)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage8 mi4ios6=stage8 c-runtime apple-dt
```

Important symbols:

```text
00008000 T _start
000080a0 T stage8_vectors
0000a368 T stage8_irq_c_handler
0000a4b0 T gic_sgi_selftest
0000a6e8 T kernel_entry
0000ab1c T stage8_main
00011740 B GIC_state_stage8
00013000 B __stage8_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage8/stage8-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2482 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.088s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage8-last_kmsg.txt
```

The recovered log was 5442 bytes and contained:

```text
116 MI4IOS6_STAGE8 markers
94 MI4IOS6_STAGE8_XNU markers
```

## Key recovered SGI/IRQ markers

```text
MI4IOS6_STAGE8_XNU gic SGI selftest begin
MI4IOS6_STAGE8_XNU gic_sgi_distCtlr_before=0x00000001
MI4IOS6_STAGE8_XNU gic_sgi_cpuCtlr_before=0x00000001
MI4IOS6_STAGE8_XNU gic_sgi_cpuPmr_before=0x000000f0
MI4IOS6_STAGE8_XNU gic_sgi_isenabler0_before=0x00007fff
MI4IOS6_STAGE8_XNU gic_sgi_cpsr_before=0x60000193
MI4IOS6_STAGE8_XNU gic_sgi_isenabler0_after=0x00007fff
MI4IOS6_STAGE8_XNU gic SGI selftest send SGI0 to self
MI4IOS6_STAGE8 irq handler iar=0x00000000 id=0x00000000 count=0x00000001
MI4IOS6_STAGE8_XNU gic_sgi_cpsr_after=0x20000193
MI4IOS6_STAGE8_XNU gic_sgi_irq_count=0x00000001
MI4IOS6_STAGE8_XNU gic_sgi_sgi0_count=0x00000001
MI4IOS6_STAGE8_XNU gic_sgi_spurious_count=0x00000000
MI4IOS6_STAGE8_XNU gic_sgi_last_iar=0x00000000
MI4IOS6_STAGE8_XNU gic_sgi_last_irq_id=0x00000000
MI4IOS6_STAGE8_XNU gic SGI selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE8_XNU kernel_entry ok
MI4IOS6_STAGE8 kernel_entry returned success
MI4IOS6_STAGE8 attempting MSM8974 PS_HOLD reset
```

## Interpretation

The Stage8 payload successfully entered the IRQ vector, ran the C IRQ handler, acknowledged SGI0 by reading `GICC_IAR`, completed it by writing `GICC_EOIR`, and returned to the interrupted `kernel_entry()` path. This proves that:

- VBAR points to a usable custom IRQ vector,
- the IRQ-mode banked stack works,
- CPSR.I can be opened briefly and re-masked safely,
- GIC SGI delivery to the current CPU works on cancro,
- the C IRQ handler can return safely after EOIR,
- no external IRQ was required for the test.

## Success criteria — met

1. bootloader accepted `stage8-qcdt.img`: yes
2. Stage7 PE/GIC/timebase validation still succeeded: yes
3. SGI0 was generated to the current CPU: yes
4. IRQ handler logged `iar=0x00000000 id=0x00000000`: yes
5. SGI0 count reached 1 and spurious count stayed 0: yes
6. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage9 can move from software-generated IRQs to timer-driven IRQs:

- program the ARM generic physical timer with a short one-shot deadline,
- enable only the corresponding timer PPI in the local GIC bank,
- reuse the Stage8 returnable IRQ vector/EOIR path,
- verify a real hardware timer interrupt is delivered and handled,
- keep IRQ windows short and avoid enabling unrelated external interrupts.
