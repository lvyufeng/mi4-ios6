# Experiment 24 — Stage21 High-Root Service Table

Date: 2026-06-05

Goal: move an early service table into high virtual execution and verify service availability from the high-root path.

Stage21 still does **not** run XNU or iOS. It extends the Stage20 high-root phase dispatcher with service records for logging, timebase, platform, and interrupts. The high root validates that those services are available, records a service mask and checksum, and the identity verifier checks the result through both identity and high-alias views.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage21 adds over Stage20

Stage20 proved:

- high-root phase table,
- completed phase mask `0x0000000f`,
- phase checksum `0x0000000b`,
- root step mask `0x0000007f`,
- status `0x20000001`.

Stage21 adds a high-root service table:

- service count: `4`,
- available service mask: `0x0000000f`,
- service status checksum: `0x0000000b`,
- per-service statuses all `0x21000001`,
- root step mask extends to `0x000000ff`,
- final root status `0x21000001`.

Service IDs:

```text
0 logging
1 timebase
2 platform
3 interrupts
```

Root step bits now include:

```text
0x00000001 enter
0x00000002 init complete
0x00000004 result generated
0x00000008 return-ready
0x00000010 high-DT summary complete
0x00000020 platform-result complete
0x00000040 phase-table complete
0x00000080 service-table complete
```

Complete root-step mask: `0x000000ff`.

## Built image

```bash
./stage21/build.sh
```

Successful local build:

```text
out/stage21/stage21-qcdt.img
sha256=843560f923a032758225bfc82818226a78c8d6922ad46e04a75f1f711e26607a
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=37372 (0x91fc)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage21 mi4ios6=stage21 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage21_vectors
0000ab68 t stage21_kernel_root
0000c190 T mmu_high_bootstrap_selftest
0000ce2c T kernel_entry
0000d154 T test_kernel_entry
0000d2ec T stage21_main
00020000 b stage21_l1_table
00026000 B __stage21_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage21/stage21-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2502 KB)                       OKAY [  0.080s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.090s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage21-last_kmsg.txt
```

The recovered log was 19061 bytes and contained:

```text
348 MI4IOS6_STAGE21 markers
322 MI4IOS6_STAGE21_XNU markers
```

## Key recovered high-root service markers

```text
MI4IOS6_STAGE21_XNU high virtual kernel_root entered
MI4IOS6_STAGE21_XNU high root owns init sequence
MI4IOS6_STAGE21_XNU high init sequence begin
MI4IOS6_STAGE21_XNU high init step validate begin
MI4IOS6_STAGE21_XNU high init step validate ok
MI4IOS6_STAGE21_XNU high init step timebase begin
MI4IOS6_STAGE21_XNU high init step timebase ok
MI4IOS6_STAGE21_XNU high init step gic summary begin
MI4IOS6_STAGE21_XNU high init step gic summary ok
MI4IOS6_STAGE21_XNU high root dt summary begin
MI4IOS6_STAGE21_XNU high root dt summary ok
MI4IOS6_STAGE21_XNU high root platform result begin
MI4IOS6_STAGE21_XNU high root platform result ok
MI4IOS6_STAGE21_XNU high root phase table begin
MI4IOS6_STAGE21_XNU high root phase table ok
MI4IOS6_STAGE21_XNU high root service table begin
MI4IOS6_STAGE21_XNU high root service table ok
MI4IOS6_STAGE21_XNU high init sequence complete
MI4IOS6_STAGE21_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE21_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE21_XNU high_bootstrap_init_status=0x21000001
MI4IOS6_STAGE21_XNU high_phase_completed_mask=0x0000000f
MI4IOS6_STAGE21_XNU high_phase_status_checksum=0x0000000b
MI4IOS6_STAGE21_XNU high_service_count=0x00000004
MI4IOS6_STAGE21_XNU high_service_available_mask=0x0000000f
MI4IOS6_STAGE21_XNU high_service_status_checksum=0x0000000b
MI4IOS6_STAGE21_XNU high_service_logging_status=0x21000001
MI4IOS6_STAGE21_XNU high_service_timebase_status=0x21000001
MI4IOS6_STAGE21_XNU high_service_platform_status=0x21000001
MI4IOS6_STAGE21_XNU high_service_interrupts_status=0x21000001
MI4IOS6_STAGE21_XNU high_bootstrap_status=0x21000001
MI4IOS6_STAGE21_XNU high_bootstrap_checksum=0x2750215d
MI4IOS6_STAGE21_XNU high virtual kernel_root leaving
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_result=0x21000001
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_expected_checksum=0x2750215d
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_magic_id=0x21002100
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_validation_id=0x00000000
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_root_steps_id=0x000000ff
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_root_status_id=0x21000001
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_init_steps_id=0x0000000f
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_init_status_id=0x21000001
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_platform_consistency_id=0x0000000f
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_phase_count_id=0x00000004
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_phase_mask_id=0x0000000f
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_phase_checksum_id=0x0000000b
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_service_count_id=0x00000004
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_service_mask_id=0x0000000f
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_service_checksum_id=0x0000000b
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_service_logging_status_id=0x21000001
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_service_timebase_status_id=0x21000001
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_service_platform_status_id=0x21000001
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_service_interrupts_status_id=0x21000001
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_status_id=0x21000001
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_checksum_id=0x2750215d
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_magic_alias=0x21002100
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_root_steps_alias=0x000000ff
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_service_mask_alias=0x0000000f
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_service_checksum_alias=0x0000000b
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_status_alias=0x21000001
MI4IOS6_STAGE21_XNU mmu_high_bootstrap_checksum_alias=0x2750215d
MI4IOS6_STAGE21_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the service-table root path:

```text
MI4IOS6_STAGE21_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the service-table root path:

```text
MI4IOS6_STAGE21_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE21_XNU kernel_entry ok
MI4IOS6_STAGE21 kernel_entry returned success
MI4IOS6_STAGE21 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage21 adds a service-discovery layer to the high-root path. The root path now verifies logging/timebase/platform/interrupt service availability and includes service state in the structured result object.

This prepares the next stages to make higher-level init phases consume named services rather than raw globals directly.

## Success criteria — met

1. bootloader accepted `stage21-qcdt.img`: yes
2. high virtual root entry ran: yes
3. high-root service table ran: yes
4. service mask was complete (`0x0000000f`): yes
5. service checksum matched (`0x0000000b`): yes
6. root step mask was complete (`0x000000ff`): yes
7. validation mask was zero: yes
8. high root returned status `0x21000001`: yes
9. checksum matched through identity and alias views: yes
10. SGI and timer IRQ paths still worked after high root: yes
11. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage22 can make the phase table consume service-table state explicitly:

- keep identity/recovery mappings and caches disabled,
- make each phase declare required service bits,
- validate phase-service dependency masks in high virtual execution,
- keep identity-side verification and recovery,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
