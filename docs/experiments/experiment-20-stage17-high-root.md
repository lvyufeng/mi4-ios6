# Experiment 20 — Stage17 High-Virtual Root Kernel Path

Date: 2026-06-04

Goal: move from a high-virtual helper/bootstrap call toward a high-virtual root kernel entry that owns the ordered init sequence and returns a structured kernel-result object.

Stage17 still does **not** run XNU or iOS. It keeps the same conservative bring-up shell, but the high-virtual entry is now shaped as a root kernel path rather than a helper leaf: it enters as `kernel_root`, owns the init sequence, records root-step status, and returns a structured result.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage17 adds over Stage16

Stage16 proved that high-virtual bootstrap can run a small ordered init sequence:

1. validate platform facts,
2. initialize/check the timebase path,
3. summarize GIC state,
4. return status `0x16000001`.

Stage17 makes that path more like a kernel root entry:

- renames the high-virtual entry shape to `stage17_kernel_root`,
- logs `high virtual kernel_root entered/leaving`,
- records a root-step bitmask independent of init-step status,
- records root status `0x17000001`,
- returns the root status as the kernel-result object,
- verifies root/init/status fields through identity and high-alias views.

Root step bits:

```text
0x00000001 enter
0x00000002 init complete
0x00000004 result generated
0x00000008 return-ready
```

A complete root-step mask is therefore `0x0000000f`.

## Built image

```bash
./stage17/build.sh
```

Successful local build:

```text
out/stage17/stage17-qcdt.img
sha256=ac6dea9a17cf9d88300ff2054314665bd16f64e699c912bddb42e64616919efb
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=30352 (0x7690)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage17 mi4ios6=stage17 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage17_vectors
0000ab68 t stage17_kernel_root
0000b884 T mmu_high_bootstrap_selftest
0000bf08 T kernel_entry
0000c230 T test_kernel_entry
0000c3c8 T stage17_main
0001c000 b stage17_l1_table
00022000 B __stage17_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage17/stage17-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2494 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.089s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage17-last_kmsg.txt
```

The recovered log was 14017 bytes and contained:

```text
268 MI4IOS6_STAGE17 markers
242 MI4IOS6_STAGE17_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE17_XNU high virtual kernel_root entered
MI4IOS6_STAGE17_XNU high root owns init sequence
MI4IOS6_STAGE17_XNU high init sequence begin
MI4IOS6_STAGE17_XNU high init step validate begin
MI4IOS6_STAGE17_XNU high init step validate ok
MI4IOS6_STAGE17_XNU high init step timebase begin
MI4IOS6_STAGE17_XNU high init step timebase ok
MI4IOS6_STAGE17_XNU high init step gic summary begin
MI4IOS6_STAGE17_XNU high init step gic summary ok
MI4IOS6_STAGE17_XNU high init sequence complete
MI4IOS6_STAGE17_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE17_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE17_XNU high_bootstrap_init_status=0x17000001
MI4IOS6_STAGE17_XNU high_bootstrap_timebase_freq=0x0124f800
MI4IOS6_STAGE17_XNU high_bootstrap_timebase_delta_us=0x000003ea
MI4IOS6_STAGE17_XNU high_bootstrap_gic_irq_count=0x00000120
MI4IOS6_STAGE17_XNU high_bootstrap_gic_cpu_count=0x00000004
MI4IOS6_STAGE17_XNU high_bootstrap_status=0x17000001
MI4IOS6_STAGE17_XNU high_bootstrap_checksum=0x27503611
MI4IOS6_STAGE17_XNU high virtual kernel_root leaving
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_result=0x17000001
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_expected_checksum=0x27503611
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_magic_id=0x17001700
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_validation_id=0x00000000
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_root_steps_id=0x0000000f
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_root_status_id=0x17000001
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_init_steps_id=0x0000000f
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_init_status_id=0x17000001
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_timebase_freq_id=0x0124f800
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_timebase_delta_us_id=0x000003ea
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_gic_irq_count_id=0x00000120
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_gic_cpu_count_id=0x00000004
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_gic_dist_ctlr_id=0x00000001
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_gic_cpu_ctlr_id=0x00000001
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_status_id=0x17000001
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_checksum_id=0x27503611
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_magic_alias=0x17001700
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_root_steps_alias=0x0000000f
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_root_status_alias=0x17000001
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_init_steps_alias=0x0000000f
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_status_alias=0x17000001
MI4IOS6_STAGE17_XNU mmu_high_bootstrap_checksum_alias=0x27503611
MI4IOS6_STAGE17_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the high-virtual root path:

```text
MI4IOS6_STAGE17_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the high-virtual root path:

```text
MI4IOS6_STAGE17_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE17_XNU kernel_entry ok
MI4IOS6_STAGE17 kernel_entry returned success
MI4IOS6_STAGE17 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage17 establishes a cleaner high-virtual root-kernel shape: an identity-side `kernel_entry` prepares and verifies the platform, enables the conservative MMU mapping, then calls a high-virtual root entry. The root entry owns the ordered init sequence and returns a structured status object. The identity side remains a recovery/verifier shell for now.

This is closer to the desired direction of gradually moving the real kernel flow into the high virtual address space.

## Success criteria — met

1. bootloader accepted `stage17-qcdt.img`: yes
2. high virtual root entry ran: yes
3. root step mask was complete (`0x0000000f`): yes
4. init step mask was complete (`0x0000000f`): yes
5. validation mask was zero: yes
6. high root returned status `0x17000001`: yes
7. checksum matched through identity and alias views: yes
8. SGI and timer IRQ paths still worked after high root: yes
9. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage18 can continue moving responsibility out of the identity verifier and into high virtual execution:

- keep identity/recovery mappings and caches disabled,
- make the high root consume a high-virtual boot_args pointer and produce a larger kernel-result block,
- include a high-virtual device-tree summary inside the root path,
- keep identity-side verification and recovery,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
