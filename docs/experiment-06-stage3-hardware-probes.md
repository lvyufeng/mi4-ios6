# Experiment 06 — Stage3 Read-Only Hardware Probes

Date: 2026-06-04

Goal: use the Stage2 freestanding C runtime to read real MSM8974 hardware state from our own non-Linux payload, without enabling interrupts or changing device configuration.

Stage3 still does **not** run XNU or iOS. It is a read-only hardware discovery milestone for later XNU/Darwin platform expert work.

## Safety model

- Tested only through non-persistent `fastboot boot`.
- No partition flash/erase/write.
- Hardware probes are read-only.
- GIC interrupt acknowledge register (`GICC_IAR`) is deliberately not read because reading it can acknowledge/consume an interrupt.
- Before every potentially risky MMIO read, Stage3 writes a `pre read ...` breadcrumb into `ram_console`; if a read faults or hangs, `/proc/last_kmsg` should identify the last attempted register.

## Source files

Stage3 was created from Stage2 and adds:

```text
stage3/probes.c
```

Probe groups:

- CP15 / ARM system registers:
  - MIDR
  - MPIDR
  - ID_PFR1
  - SCTLR
  - TTBR0
  - VBAR
  - CPSR
- ARM generic timer:
  - CNTFRQ
  - CNTPCT before/after a busy gap
- MSM8974 GIC:
  - GICD_CTLR
  - GICD_TYPER
  - GICD_IIDR
  - GICC_IIDR

## Built image

```bash
./stage3/build.sh
```

Final successful image:

```text
out/stage3/stage3-qcdt.img
sha256=21e7b0ed34047dc0628e4c8cd9ddf8ce0335c9882c9d9ce69261e41406514eeb
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=8744 (0x2228)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage3 mi4ios6=stage3 c-runtime apple-dt
```

Key linked address:

```text
__stage3_image_end=0x00010000
```

## Non-persistent boot test

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage3/stage3-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2474 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.089s
```

The phone returned to Android automatically after ~25 seconds; ADB transport id changed from 23 to 24.

## Recovered persistent log

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage3-last_kmsg.txt
```

The recovered log was 2756 bytes and contained 66 `MI4IOS6_STAGE3` markers. The full log included successful Stage2-style boot_args/Apple-DT validation plus Stage3 hardware probes.

## Key probe results

### CPU / CP15

```text
cp15_midr=0x512f06f1
cp15_mpidr=0x80000000
cp15_id_pfr1=0x00010021
cp15_sctlr=0x00c5487a
cp15_sctlr_mmu_on=0x00000000
cp15_sctlr_icache_on=0x00000000
cp15_sctlr_dcache_on=0x00000000
cp15_ttbr0=0x0f210000
cp15_vbar=0x0f000000
cpsr=0x80000193
cpsr_mode=0x00000013
```

Interpretation:

- Payload is entered in ARM SVC mode (`0x13`).
- MMU, I-cache, and D-cache are off at our entry point.
- VBAR is already set to `0x0f000000` by earlier firmware/bootloader code.
- ID_PFR1 advertises generic timer support.

### ARM generic timer

```text
id_pfr1_generic_timer_field=0x00000001
cntfrq_hz=0x0124f800
cntpct_a_lo=0x0522c702
cntpct_a_hi=0x00000000
cntpct_b_lo=0x053b8892
cntpct_b_hi=0x00000000
cntpct_delta_lo=0x0018c190
arch-timer counter advancing
```

Interpretation:

- `CNTFRQ=0x0124f800 = 19,200,000 Hz`, matching the expected MSM8974 timer frequency.
- `CNTPCT` advances while our payload is running.
- A later Stage4 can use the ARM generic timer as the early timebase before touching more complex Qualcomm timer blocks.

### GIC / interrupt controller

```text
gicd_ctlr=0x00000001
gicd_typer=0x00000468
gicd_itlines_field=0x00000008
gicd_num_irqs=0x00000120
gicd_cpu_number_field=0x00000003
gicd_iidr=0x00001070
gicc_iidr=0x00020070
```

Interpretation:

- The GIC distributor MMIO window at `0xf9000000` is readable.
- The GIC CPU interface window at `0xf9002000` is readable.
- `GICD_CTLR=1` means the distributor is already enabled by the boot environment.
- `GICD_TYPER.ITLinesNumber=8` means `(8 + 1) * 32 = 288` interrupt lines.
- CPU number field `3` means 4 CPU interfaces, matching quad-core Krait.
- IDs are readable and stable enough for a future platform expert probe.

## Success criteria — met

1. bootloader accepted the Stage3 QCDT image: yes
2. Stage2 boot_args + Apple-DT self-test still passed: yes
3. CP15 reads succeeded: yes
4. generic timer frequency and counter reads succeeded: yes
5. GIC distributor and CPU-interface ID/status reads succeeded: yes
6. payload reset the phone through PS_HOLD: yes

## Next stage

Stage4 can now move from read-only discovery to controlled initialization:

1. Use the ARM generic timer as early `ml_get_timebase` equivalent.
2. Implement a small busy-wait/delay routine based on `CNTPCT/CNTFRQ`.
3. Carefully initialize or preserve GIC state without enabling unexpected interrupts.
4. Optionally install a minimal exception vector table and VBAR so data aborts/prefetch aborts can be logged instead of silently hanging.
5. Add these facts back into the Apple-style device tree and begin shaping an MSM8974 platform expert path.
