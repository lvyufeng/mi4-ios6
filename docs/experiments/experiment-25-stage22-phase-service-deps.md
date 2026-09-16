# Experiment 25 — Stage22 Phase-Service Dependencies

Date: 2026-06-05

Goal: make the high-root phase table consume service-table state explicitly by giving each phase a required-service mask and validating those dependencies in high virtual execution.

Stage22 still does **not** run XNU or iOS. It extends the Stage21 high-root service-table path with phase-service dependency records. The high root now verifies that the logging/timebase/platform/interrupt services are available before marking dependent phases complete, records dependency/satisfaction masks, and the identity verifier checks those results through both identity and high-alias views.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage22 adds over Stage21

Stage21 proved:

- high-root service table,
- service mask `0x0000000f`,
- service checksum `0x0000000b`,
- root step mask `0x000000ff`,
- status `0x21000001`.

Stage22 adds explicit phase-service dependencies:

- phase 0 / validate requires services `0x00000001` (logging),
- phase 1 / DT summary requires services `0x00000005` (logging + platform),
- phase 2 / platform result requires services `0x00000007` (logging + platform + timebase),
- phase 3 / return-ready requires services `0x0000000f` (logging + timebase + platform + interrupts),
- dependency union mask: `0x0000000f`,
- satisfied phase mask: `0x0000000f`,
- phase-service checksum: `0x00000008`,
- phase-service dependency status: `0x22000001`,
- root step mask extends to `0x000001ff`,
- final root status `0x22000001`.

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
0x00000100 phase-service dependencies complete
```

Complete root-step mask: `0x000001ff`.

## Built image

```bash
./stage22/build.sh
```

Successful local build:

```text
out/stage22/stage22-qcdt.img
sha256=6bbe3426708ded32b9f0b0e609e02ab059698ffbc2eb4c78b7881003fa613bc8
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=39360 (0x99c0)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage22 mi4ios6=stage22 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage22_vectors
0000ab68 t stage22_kernel_root
0000c400 T mmu_high_bootstrap_selftest
0000d23c T kernel_entry
0000d564 T test_kernel_entry
0000d6fc T stage22_main
00020000 b stage22_l1_table
00026000 B __stage22_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage22/stage22-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2504 KB)                       OKAY [  0.080s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.089s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage22-last_kmsg.txt
```

The recovered log was 20557 bytes and contained:

```text
369 MI4IOS6_STAGE22 markers
343 MI4IOS6_STAGE22_XNU markers
```

## Key recovered high-root phase-service markers

```text
MI4IOS6_STAGE22_XNU high virtual kernel_root entered
MI4IOS6_STAGE22_XNU high root service table begin
MI4IOS6_STAGE22_XNU high root service table ok
MI4IOS6_STAGE22_XNU high root phase-service dependencies begin
MI4IOS6_STAGE22_XNU high root phase-service dependencies ok
MI4IOS6_STAGE22_XNU high root phase table begin
MI4IOS6_STAGE22_XNU high root phase table ok
MI4IOS6_STAGE22_XNU high init sequence complete
MI4IOS6_STAGE22_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE22_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE22_XNU high_bootstrap_init_status=0x22000001
MI4IOS6_STAGE22_XNU high_phase_completed_mask=0x0000000f
MI4IOS6_STAGE22_XNU high_phase_status_checksum=0x0000000b
MI4IOS6_STAGE22_XNU high_phase0_required_services=0x00000001
MI4IOS6_STAGE22_XNU high_phase1_required_services=0x00000005
MI4IOS6_STAGE22_XNU high_phase2_required_services=0x00000007
MI4IOS6_STAGE22_XNU high_phase3_required_services=0x0000000f
MI4IOS6_STAGE22_XNU high_phase_service_dependency_mask=0x0000000f
MI4IOS6_STAGE22_XNU high_phase_service_satisfied_mask=0x0000000f
MI4IOS6_STAGE22_XNU high_phase_service_status_checksum=0x00000008
MI4IOS6_STAGE22_XNU high_phase_service_dependency_status=0x22000001
MI4IOS6_STAGE22_XNU high_service_count=0x00000004
MI4IOS6_STAGE22_XNU high_service_available_mask=0x0000000f
MI4IOS6_STAGE22_XNU high_service_status_checksum=0x0000000b
MI4IOS6_STAGE22_XNU high_service_logging_status=0x22000001
MI4IOS6_STAGE22_XNU high_service_timebase_status=0x22000001
MI4IOS6_STAGE22_XNU high_service_platform_status=0x22000001
MI4IOS6_STAGE22_XNU high_service_interrupts_status=0x22000001
MI4IOS6_STAGE22_XNU high_bootstrap_status=0x22000001
MI4IOS6_STAGE22_XNU high_bootstrap_checksum=0x055022bc
MI4IOS6_STAGE22_XNU high virtual kernel_root leaving
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_result=0x22000001
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_expected_checksum=0x055022bc
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_magic_id=0x22002200
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_root_steps_id=0x000001ff
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_root_status_id=0x22000001
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_phase_count_id=0x00000004
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_phase_mask_id=0x0000000f
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_phase_checksum_id=0x0000000b
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_phase0_required_services_id=0x00000001
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_phase1_required_services_id=0x00000005
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_phase2_required_services_id=0x00000007
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_phase3_required_services_id=0x0000000f
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_phase_service_dependency_mask_id=0x0000000f
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_phase_service_satisfied_mask_id=0x0000000f
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_phase_service_checksum_id=0x00000008
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_phase_service_status_id=0x22000001
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_service_count_id=0x00000004
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_service_mask_id=0x0000000f
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_service_checksum_id=0x0000000b
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_status_id=0x22000001
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_checksum_id=0x055022bc
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_magic_alias=0x22002200
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_root_steps_alias=0x000001ff
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_phase_service_mask_alias=0x0000000f
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_phase_service_satisfied_alias=0x0000000f
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_phase_service_status_alias=0x22000001
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_status_alias=0x22000001
MI4IOS6_STAGE22_XNU mmu_high_bootstrap_checksum_alias=0x055022bc
MI4IOS6_STAGE22_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the phase-service dependency root path:

```text
MI4IOS6_STAGE22_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the phase-service dependency root path:

```text
MI4IOS6_STAGE22_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE22_XNU kernel_entry ok
MI4IOS6_STAGE22 kernel_entry returned success
MI4IOS6_STAGE22 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage22 makes the high-root phase table depend on service-table availability rather than only raw init/global status. The root path now has a small but explicit bootstrap contract:

1. discover services,
2. compute each phase's required-service mask,
3. check which phases have dependencies satisfied,
4. only mark phases complete when both their semantic condition and service dependencies are satisfied,
5. preserve the result through identity and high-alias verification.

This is closer to an XNU-like early bootstrap shape where subsystems consume published platform services through ordered init records instead of directly relying on ad hoc globals.

## Success criteria — met

1. bootloader accepted `stage22-qcdt.img`: yes
2. high virtual root entry ran: yes
3. high-root service table ran: yes
4. phase-service dependency validation ran: yes
5. dependency union mask was complete (`0x0000000f`): yes
6. satisfied phase mask was complete (`0x0000000f`): yes
7. phase-service checksum matched (`0x00000008`): yes
8. phase-service dependency status was `0x22000001`: yes
9. root step mask was complete (`0x000001ff`): yes
10. validation mask was zero: yes
11. high root returned status `0x22000001`: yes
12. checksum matched through identity and alias views: yes
13. SGI and timer IRQ paths still worked after high root: yes
14. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage23 can turn the phase table into a descriptor-driven high-root dispatcher:

- keep identity/recovery mappings and caches disabled,
- keep phase-service dependency masks from Stage22,
- describe each phase with an ID, required services, handler result, and status slot,
- execute those descriptors from high virtual execution rather than open-coded per-phase blocks,
- keep identity-side verification and recovery,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
