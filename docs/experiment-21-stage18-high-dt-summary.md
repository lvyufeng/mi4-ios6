# Experiment 21 — Stage18 High-Virtual Boot Args and Device-Tree Summary

Date: 2026-06-04

Goal: move more boot_args/device-tree consumption into the high-virtual root path and return a larger kernel-result block.

Stage18 still does **not** run XNU or iOS. It extends the high-virtual root path so it consumes the high-virtual `boot_args` pointer, translates the attached Apple-DT pointer into the high alias, walks selected DT nodes from high virtual execution, and records a larger result object for identity/alias verification.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage18 adds over Stage17

Stage17 proved:

- high-virtual `kernel_root` entry/exit,
- root step mask `0x0000000f`,
- init step mask `0x0000000f`,
- root status `0x17000001`,
- post-root SGI/timer IRQ success.

Stage18 adds a high-virtual device-tree summary inside the root path:

- confirms the root received a high-virtual boot_args pointer,
- constructs a high-virtual Apple-DT pointer from boot args,
- reads root child count from high virtual execution,
- finds `/memory` and `/timer` from high virtual execution,
- records memory base/size and timer frequency from the high-DT view,
- adds a DT-summary root step,
- returns status `0x18000001`.

Root step bits now include:

```text
0x00000001 enter
0x00000002 init complete
0x00000004 result generated
0x00000008 return-ready
0x00000010 high-DT summary complete
```

A complete root-step mask is therefore `0x0000001f`.

## Built image

```bash
./stage18/build.sh
```

Successful local build:

```text
out/stage18/stage18-qcdt.img
sha256=fc6ad4153132cadfd9a6ea6f3d8a757695fd1005f377ce8d1af552da300d4c90
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=31984 (0x7cf0)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage18 mi4ios6=stage18 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage18_vectors
0000ab68 t stage18_kernel_root
0000bad0 T mmu_high_bootstrap_selftest
0000c290 T kernel_entry
0000c5b8 T test_kernel_entry
0000c750 T stage18_main
0001c000 b stage18_l1_table
00022000 B __stage18_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage18/stage18-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2496 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.090s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage18-last_kmsg.txt
```

The recovered log was 15153 bytes and contained:

```text
286 MI4IOS6_STAGE18 markers
260 MI4IOS6_STAGE18_XNU markers
```

## Key recovered high-root / high-DT markers

```text
MI4IOS6_STAGE18_XNU high virtual kernel_root entered
MI4IOS6_STAGE18_XNU high root owns init sequence
MI4IOS6_STAGE18_XNU high init sequence begin
MI4IOS6_STAGE18_XNU high init step validate begin
MI4IOS6_STAGE18_XNU high init step validate ok
MI4IOS6_STAGE18_XNU high init step timebase begin
MI4IOS6_STAGE18_XNU high init step timebase ok
MI4IOS6_STAGE18_XNU high init step gic summary begin
MI4IOS6_STAGE18_XNU high init step gic summary ok
MI4IOS6_STAGE18_XNU high root dt summary begin
MI4IOS6_STAGE18_XNU high root dt summary ok
MI4IOS6_STAGE18_XNU high init sequence complete
MI4IOS6_STAGE18_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE18_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE18_XNU high_bootstrap_init_status=0x18000001
MI4IOS6_STAGE18_XNU high_bootstrap_timebase_freq=0x0124f800
MI4IOS6_STAGE18_XNU high_bootstrap_timebase_delta_us=0x000003ea
MI4IOS6_STAGE18_XNU high_bootstrap_gic_irq_count=0x00000120
MI4IOS6_STAGE18_XNU high_bootstrap_gic_cpu_count=0x00000004
MI4IOS6_STAGE18_XNU high_bootstrap_status=0x18000001
MI4IOS6_STAGE18_XNU high_bootstrap_checksum=0xe024c05a
MI4IOS6_STAGE18_XNU high virtual kernel_root leaving
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_result=0x18000001
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_expected_checksum=0xe024c05a
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_magic_id=0x18001800
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_validation_id=0x00000000
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_root_steps_id=0x0000001f
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_root_status_id=0x18000001
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_init_steps_id=0x0000000f
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_init_status_id=0x18000001
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_root_boot_args_virt_id=0xc0020000
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_root_dt_virt_id=0xc0020140
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_root_dt_children_id=0x00000006
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_root_dt_mem_base_id=0x80000000
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_root_dt_mem_size_id=0x5e500000
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_root_dt_timer_freq_id=0x0124f800
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_root_dt_summary_status_id=0x18000001
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_status_id=0x18000001
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_checksum_id=0xe024c05a
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_magic_alias=0x18001800
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_root_steps_alias=0x0000001f
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_root_status_alias=0x18000001
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_root_dt_virt_alias=0xc0020140
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_root_dt_summary_status_alias=0x18000001
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_init_steps_alias=0x0000000f
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_status_alias=0x18000001
MI4IOS6_STAGE18_XNU mmu_high_bootstrap_checksum_alias=0xe024c05a
MI4IOS6_STAGE18_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the high-DT root path:

```text
MI4IOS6_STAGE18_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the high-DT root path:

```text
MI4IOS6_STAGE18_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE18_XNU kernel_entry ok
MI4IOS6_STAGE18 kernel_entry returned success
MI4IOS6_STAGE18 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage18 moves another part of the bootstrap contract into high virtual execution. The high root now consumes the high `boot_args` pointer, derives a high Apple-DT pointer, walks selected DT state, and records a larger result block that is coherent through both identity and high-alias views.

This makes the high root less of a synthetic selftest and more like a real early kernel path consuming its boot contract.

## Success criteria — met

1. bootloader accepted `stage18-qcdt.img`: yes
2. high virtual root entry ran: yes
3. high-DT summary ran: yes
4. root step mask was complete (`0x0000001f`): yes
5. init step mask was complete (`0x0000000f`): yes
6. validation mask was zero: yes
7. high root returned status `0x18000001`: yes
8. checksum matched through identity and alias views: yes
9. SGI and timer IRQ paths still worked after high root: yes
10. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage19 can continue moving work into high virtual execution by letting the high root build/own an early platform-result object with more PE-like fields:

- keep identity/recovery mappings and caches disabled,
- summarize CPU/GIC/timer/memory from high DT and PE_state together,
- include result object versioning and size fields,
- keep identity-side verification and recovery,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
