# Experiment 19 — Stage16 High-Virtual Init Sequence

Date: 2026-06-04

Goal: move beyond a single high-virtual validation/status handoff by running a small ordered XNU-like init sequence from the high-virtual bootstrap path.

Stage16 still does **not** run XNU or iOS. It is another controlled bring-up step: the high-virtual path now performs several ordered init checks itself, records per-step state, returns a structured status, and then the identity path verifies the high state before rerunning IRQ selftests.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual init sequence.

## What Stage16 adds over Stage15

Stage15 proved:

- selected boot_args / PE_state validation inside high-virtual `kernel_bootstrap`,
- validation mask `0x00000000`,
- structured status `0x15000001`,
- identity/alias verification of status/checksum,
- post-bootstrap SGI/timer IRQ success.

Stage16 changes the high-virtual bootstrap from “validate facts and return” into a tiny ordered init sequence:

1. validate boot/platform facts,
2. initialize/check the XNU-like timebase path from high virtual execution,
3. read/summarize GIC distributor and CPU-interface state from high virtual execution,
4. record a completed init-step mask,
5. return structured status `0x16000001`.

The high bootstrap state now records:

- validation mask,
- init step bitmask,
- init status,
- high-virtual timebase frequency,
- high-virtual measured delay,
- high-virtual GIC IRQ count,
- high-virtual GIC CPU-interface count,
- high-virtual GIC distributor/CPU-interface enable states,
- final status,
- checksum.

## Built image

```bash
./stage16/build.sh
```

Successful local build:

```text
out/stage16/stage16-qcdt.img
sha256=1c7ce5adae26a8cc23d8375c83a203db695d18fde7625bac3aafcb1373ce039d
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=29796 (0x7464)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage16 mi4ios6=stage16 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage16_vectors
0000ab68 t stage16_kernel_bootstrap
0000b804 T mmu_high_bootstrap_selftest
0000bde4 T kernel_entry
0000c10c T test_kernel_entry
0000c2a4 T stage16_main
0001c000 b stage16_l1_table
00022000 B __stage16_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage16/stage16-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2494 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.088s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage16-last_kmsg.txt
```

The recovered log was 13572 bytes and contained:

```text
260 MI4IOS6_STAGE16 markers
234 MI4IOS6_STAGE16_XNU markers
```

## Key recovered high-init markers

```text
MI4IOS6_STAGE16_XNU high init sequence begin
MI4IOS6_STAGE16_XNU high init step validate begin
MI4IOS6_STAGE16_XNU high init step validate ok
MI4IOS6_STAGE16_XNU high init step timebase begin
MI4IOS6_STAGE16_XNU high init step timebase ok
MI4IOS6_STAGE16_XNU high init step gic summary begin
MI4IOS6_STAGE16_XNU high init step gic summary ok
MI4IOS6_STAGE16_XNU high init sequence complete
MI4IOS6_STAGE16_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE16_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE16_XNU high_bootstrap_init_status=0x16000001
MI4IOS6_STAGE16_XNU high_bootstrap_timebase_freq=0x0124f800
MI4IOS6_STAGE16_XNU high_bootstrap_timebase_delta_us=0x000003ea
MI4IOS6_STAGE16_XNU high_bootstrap_gic_irq_count=0x00000120
MI4IOS6_STAGE16_XNU high_bootstrap_gic_cpu_count=0x00000004
MI4IOS6_STAGE16_XNU high_bootstrap_status=0x16000001
MI4IOS6_STAGE16_XNU high_bootstrap_checksum=0x3150371f
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_result=0x16000001
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_expected_checksum=0x3150371f
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_magic_id=0x16001600
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_validation_id=0x00000000
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_init_steps_id=0x0000000f
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_init_status_id=0x16000001
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_timebase_freq_id=0x0124f800
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_timebase_delta_us_id=0x000003ea
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_gic_irq_count_id=0x00000120
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_gic_cpu_count_id=0x00000004
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_gic_dist_ctlr_id=0x00000001
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_gic_cpu_ctlr_id=0x00000001
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_status_id=0x16000001
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_checksum_id=0x3150371f
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_magic_alias=0x16001600
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_init_steps_alias=0x0000000f
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_status_alias=0x16000001
MI4IOS6_STAGE16_XNU mmu_high_bootstrap_checksum_alias=0x3150371f
MI4IOS6_STAGE16_XNU mmu high bootstrap selftest ok
```

## Post-init IRQ retests

SGI0 still delivered after the high-virtual init sequence:

```text
MI4IOS6_STAGE16_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the high-virtual init sequence:

```text
MI4IOS6_STAGE16_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE16_XNU kernel_entry ok
MI4IOS6_STAGE16 kernel_entry returned success
MI4IOS6_STAGE16 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage16 confirms that high-virtual execution can do more than validate a handoff record. It can run a small ordered init sequence using existing XNU-adjacent primitives, including timebase initialization and GIC state reads, then return a structured result object that is coherent through identity and high-alias views.

This is a closer shape to an actual XNU bootstrap path: the high-virtual path owns ordered initialization work, not only a leaf selftest.

## Success criteria — met

1. bootloader accepted `stage16-qcdt.img`: yes
2. high virtual init sequence ran: yes
3. validation mask was zero: yes
4. init step mask was complete (`0x0000000f`): yes
5. high bootstrap returned status `0x16000001`: yes
6. checksum matched through identity and alias views: yes
7. SGI and timer IRQ paths still worked after high init: yes
8. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage17 can start moving from a high-virtual helper call toward a high-virtual root kernel path:

- keep identity/recovery mappings and caches disabled,
- call a high-virtual root entry that owns the init sequence,
- let that root entry return a structured kernel-result object,
- keep identity-side verification and recovery,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
