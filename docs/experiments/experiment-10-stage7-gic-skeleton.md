# Experiment 10 — Stage7 GIC Driver Skeleton (Read-Only)

Date: 2026-06-04

Goal: begin modeling the MSM8974 GICv2 interrupt-controller path from the Stage6 XNU-adjacent kernel skeleton, without enabling interrupt delivery or modifying GIC state.

Stage7 still does **not** run XNU or iOS. It adds a read-only GIC driver skeleton/state snapshot that can later be evolved into an XNU-style interrupt controller path.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- GIC probing remains read-only.
- `GICC_IAR` is not read because it can acknowledge/consume an interrupt.
- IRQ/FIQ remain masked.
- Stage6 data-abort vector safety net remains installed.

## What Stage7 adds over Stage6

Stage6 proved:

- PE_state-like platform state,
- data-abort recovery.

Stage7 adds:

- `struct gic_state_snapshot GIC_state_stage7`,
- `gic_readonly_snapshot(dist_base, cpu_base)`,
- `gic_log_snapshot()`,
- `gic_validate_snapshot()`,
- read-only logging of selected GICD/GICC registers.

## Source file added

```text
stage7/gic.c
```

Registers read:

```text
GICD_CTLR       0x000
GICD_TYPER      0x004
GICD_IIDR       0x008
GICD_ISENABLER0 0x100
GICD_ISPENDR0   0x200
GICD_IPRIORITY0 0x400
GICD_ITARGETS0  0x800
GICC_CTLR       0x000
GICC_PMR        0x004
GICC_BPR        0x008
GICC_IIDR       0x0fc
```

## Built image

```bash
./stage7/build.sh
```

Successful local build:

```text
out/stage7/stage7-qcdt.img
sha256=ddca0bd43baf350cd3d24760fd1dadd2648d7666668cc88d7de11c08a5c7b35d
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=16612 (0x40e4)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage7 mi4ios6=stage7 c-runtime apple-dt
```

Important symbols:

```text
00008000 T _start
000080a0 T stage7_vectors
0000a054 T gic_readonly_snapshot
0000a1a4 T gic_log_snapshot
0000a2c8 T gic_validate_snapshot
0000a360 T kernel_entry
0000a778 T stage7_main
00011140 B GIC_state_stage7
00013000 B __stage7_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage7/stage7-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2482 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.089s
```

The phone returned to Android automatically after ~30 seconds; ADB transport id changed from 31 to 32.

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage7-last_kmsg.txt
```

The recovered log was 4623 bytes and contained:

```text
100 MI4IOS6_STAGE7 markers
79 MI4IOS6_STAGE7_XNU markers
```

## Key recovered GIC state

```text
MI4IOS6_STAGE7_XNU gic snapshot begin (read-only)
MI4IOS6_STAGE7_XNU gic pre read GICD_CTLR
MI4IOS6_STAGE7_XNU gic pre read GICD_TYPER
MI4IOS6_STAGE7_XNU gic pre read GICD_IIDR
MI4IOS6_STAGE7_XNU gic pre read GICD_ISENABLER0
MI4IOS6_STAGE7_XNU gic pre read GICD_ISPENDR0
MI4IOS6_STAGE7_XNU gic pre read GICD_IPRIORITY0
MI4IOS6_STAGE7_XNU gic pre read GICD_ITARGETS0
MI4IOS6_STAGE7_XNU gic pre read GICC_CTLR
MI4IOS6_STAGE7_XNU gic pre read GICC_PMR
MI4IOS6_STAGE7_XNU gic pre read GICC_BPR
MI4IOS6_STAGE7_XNU gic pre read GICC_IIDR
MI4IOS6_STAGE7_XNU gic snapshot end (read-only)
```

Summary:

```text
gic_distBase=0xf9000000
gic_cpuBase=0xf9002000
gic_distCtlr=0x00000001
gic_distTyper=0x00000468
gic_distIidr=0x00001070
gic_cpuIidr=0x00020070
gic_cpuCtlr=0x00000001
gic_cpuPmr=0x000000f0
gic_cpuBpr=0x00000003
gic_irqCount=0x00000120
gic_cpuInterfaceCount=0x00000004
gic_isenabler0=0x00007fff
gic_ispendr0=0x20400000
gic_priority0=0xa0a0a0a0
gic_targets0=0x01010101
```

Final success markers:

```text
MI4IOS6_STAGE7_XNU gic validate ok
MI4IOS6_STAGE7_XNU ml timebase ok
MI4IOS6_STAGE7_XNU kernel_entry ok
MI4IOS6_STAGE7 kernel_entry returned success
MI4IOS6_STAGE7 attempting MSM8974 PS_HOLD reset
```

## Interpretation

The MSM8974 GIC distributor and CPU-interface state can be read safely from the XNU-adjacent kernel skeleton. The state matches expectations:

- GICD/GICC base addresses match Apple-DT and PE_state.
- Distributor and CPU interface are already enabled by the boot environment.
- IRQ count is 288.
- CPU interface count is 4.
- Priority/target state for the first interrupt group is readable.

No GIC state was modified in Stage7.

## Success criteria — met

1. bootloader accepted `stage7-qcdt.img`: yes
2. Stage6 PE_state and kernel_entry path still succeeded: yes
3. read-only GIC snapshot completed without abort: yes
4. GIC snapshot validation passed: yes
5. payload reset the phone through PS_HOLD: yes

## Next stage

Stage8 can attempt controlled SGI/IRQ delivery:

- install an IRQ handler that reads GICC_IAR and writes GICC_EOIR,
- enable only SGI0 as a self-interrupt test,
- keep external IRQ delivery masked/unmodified,
- send SGI0 to the current CPU via GICD_SGIR,
- verify the IRQ handler logs the interrupt ID and returns to C.

This is the first step from interrupt-controller modeling toward actual interrupt handling needed by an XNU platform path.
