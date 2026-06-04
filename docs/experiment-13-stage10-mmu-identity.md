# Experiment 13 — Stage10 ARMv7 MMU Identity Mapping

Date: 2026-06-04

Goal: enable the ARMv7 MMU for the first time under the custom XNU-adjacent payload while keeping a conservative identity map, caches disabled, and all already-proven logging/GIC/timer paths operational.

Stage10 still does **not** run XNU or iOS. It proves the first virtual-memory primitive needed by a future MSM8974 XNU platform path: controlled translation enable.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- Identity mapping only; no virtual relocation yet.
- Caches stay disabled (`SCTLR.C=0`, `SCTLR.I=0`).
- Only a small set of sections needed by the payload/debug path are mapped.
- `ram_console`, IMEM, GIC, timer, and PS_HOLD MMIO remain identity-mapped.
- Stage8/Stage9 returnable IRQ path remains installed and is re-tested after MMU enable.
- Abort handlers remain installed and mapped.

## What Stage10 adds over Stage9

Stage9 proved:

- returnable IRQ vector path,
- SGI0 interrupt delivery,
- ARM generic physical timer one-shot IRQ delivery.

Stage10 adds:

- a 16 KiB aligned ARMv7 short-descriptor L1 table,
- 1 MiB section identity mappings,
- TTBR0/TTBCR/DACR setup,
- TLB invalidation,
- `SCTLR.M` enable while keeping caches disabled,
- post-MMU RAM, ram_console, IMEM, GIC, and timer read checks,
- post-MMU SGI and timer IRQ retests.

## Identity-mapped sections

The first MMU map is deliberately small:

```text
0x00000000  payload/code/data/BSS/stack/VBAR/L1 table
0x0fa00000  MSM IMEM restart-reason section
0xde500000  Android ram_console section 0
0xde600000  Android ram_console section 1
0xf9000000  MSM8974 GIC and ARM timer MMIO section
0xfc400000  MSM8974 PS_HOLD reset section
```

Each section descriptor uses domain 0, full access, strongly-ordered/shareable attributes, and no XN bit. Domain 0 is temporarily set to manager mode for first bring-up:

```text
L1 section descriptor base attributes: 0x00010c02
DACR after enable: 0x00000003
```

## Built image

```bash
./stage10/build.sh
```

Successful local build:

```text
out/stage10/stage10-qcdt.img
sha256=c0bf61afade2bc7b8904f13c21356cdf4b3ea6f054e02c1d558d4eaddac4b334
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=21648 (0x5490)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage10 mi4ios6=stage10 c-runtime apple-dt
```

Important symbols:

```text
00008000 T _start
000080a0 T stage10_vectors
0000a36c T stage10_irq_c_handler
0000a784 T gic_timer_selftest
0000ab20 T mmu_identity_selftest
0000ae14 T kernel_entry
0000b280 T stage10_main
0001c000 b stage10_l1_table
00022000 B __stage10_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage10/stage10-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2486 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.089s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage10-last_kmsg.txt
```

The recovered log was 8072 bytes and contained:

```text
164 MI4IOS6_STAGE10 markers
141 MI4IOS6_STAGE10_XNU markers
```

## Key recovered MMU markers

```text
MI4IOS6_STAGE10_XNU mmu identity selftest begin
MI4IOS6_STAGE10_XNU mmu_sctlr_before=0x00c5487a
MI4IOS6_STAGE10_XNU mmu_ttbr0_before=0x0f210000
MI4IOS6_STAGE10_XNU mmu_dacr_before=0x00000001
MI4IOS6_STAGE10_XNU mmu_l1_table=0x0001c000
MI4IOS6_STAGE10_XNU mmu_entry_low=0x00010c02
MI4IOS6_STAGE10_XNU mmu_entry_ram_console=0xde510c02
MI4IOS6_STAGE10_XNU mmu_entry_imem=0x0fa10c02
MI4IOS6_STAGE10_XNU mmu_entry_gic=0xf9010c02
MI4IOS6_STAGE10_XNU mmu_entry_timer=0xf9010c02
MI4IOS6_STAGE10_XNU mmu_entry_pshold=0xfc410c02
MI4IOS6_STAGE10_XNU mmu_sctlr_after=0x00c5487b
MI4IOS6_STAGE10_XNU mmu_ttbr0_after=0x0001c000
MI4IOS6_STAGE10_XNU mmu_dacr_after=0x00000003
MI4IOS6_STAGE10_XNU mmu_probe_word=0x10aa55ff
MI4IOS6_STAGE10_XNU mmu_ram_console_sig=0x43474244
MI4IOS6_STAGE10_XNU mmu_restart_reason_read=0x00000000
MI4IOS6_STAGE10_XNU mmu_gicd_ctlr_read=0x00000001
MI4IOS6_STAGE10_XNU mmu_gicc_ctlr_read=0x00000001
MI4IOS6_STAGE10_XNU mmu_timer_freq_check=0x0124f800
MI4IOS6_STAGE10_XNU mmu identity selftest ok
```

`SCTLR.M` changed from clear to set:

```text
before: 0x00c5487a
 after: 0x00c5487b
```

The ram_console signature, restart-reason IMEM read, GIC reads, and timer frequency check all worked after MMU enable.

## Post-MMU IRQ retests

The already-proven interrupt path was retested after MMU enable.

SGI0 still delivered:

```text
MI4IOS6_STAGE10 irq handler iar=0x00000000 id=0x00000000 count=0x00000001 timer_count=0x00000000
MI4IOS6_STAGE10_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered:

```text
MI4IOS6_STAGE10 irq handler iar=0x00000013 id=0x00000013 count=0x00000001 timer_count=0x00000001
MI4IOS6_STAGE10_XNU gic_timer_timer_count=0x00000001
MI4IOS6_STAGE10_XNU gic_timer_spurious_count=0x00000000
MI4IOS6_STAGE10_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE10_XNU kernel_entry ok
MI4IOS6_STAGE10 kernel_entry returned success
MI4IOS6_STAGE10 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage10 successfully enabled ARMv7 translation under the bare-metal payload and continued executing through the XNU-adjacent kernel skeleton. The same persistent debug and MMIO paths survived the transition:

- the payload and stacks remained executable/accesssible through the low identity section,
- ram_console logging continued under translation,
- IMEM and PS_HOLD remained mapped for controlled reboot,
- GIC and timer MMIO remained mapped,
- returnable SGI and timer IRQ paths still worked post-MMU.

This is the first concrete step from flat physical execution toward an XNU-style virtual memory bootstrap.

## Success criteria — met

1. bootloader accepted `stage10-qcdt.img`: yes
2. L1 section table built and TTBR0 pointed at it: yes (`TTBR0=0x0001c000`)
3. `SCTLR.M` set after enable: yes (`0x00c5487b`)
4. ram_console signature readable after enable: yes (`0x43474244`)
5. GIC/timer MMIO readable after enable: yes
6. SGI0 retest succeeded after enable: yes
7. timer IRQ retest succeeded after enable: yes
8. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage11 can begin moving from a flat identity map toward an XNU-like bootstrap VM layout:

- keep the Stage10 identity map for the early debug path,
- add a higher-half/offset mapping for the payload or selected kernel window,
- validate virtual aliases for code/data and MMIO one at a time,
- keep caches disabled until alias behavior is understood,
- preserve ram_console/PS_HOLD as identity-mapped recovery paths.
