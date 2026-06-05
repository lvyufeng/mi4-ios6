# Experiment 26 — Stage23 Descriptor-Driven Phase Dispatcher

Date: 2026-06-05

Goal: turn the high-root phase table into a descriptor-driven dispatcher while preserving Stage22 service dependency checks.

Stage23 still does **not** run XNU or iOS. It extends the Stage22 phase-service dependency path with a small phase descriptor table. Each descriptor carries a phase ID, required-service mask, semantic condition, status slot, and handler-result slot. The high root dispatches those descriptors from high virtual execution, records handler/order/completion masks, and then validates the phase table from dispatcher output.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage23 adds over Stage22

Stage22 proved:

- phase-service dependency validation,
- dependency union mask `0x0000000f`,
- satisfied phase mask `0x0000000f`,
- phase-service checksum `0x00000008`,
- root step mask `0x000001ff`,
- status `0x22000001`.

Stage23 adds a descriptor-driven phase dispatcher:

- dispatcher count: `4`,
- dispatcher order mask: `0x0000000f`,
- dispatcher handler-success mask: `0x0000000f`,
- dispatcher checksum: `0x00000004`,
- descriptor IDs: `0`, `1`, `2`, `3`,
- handler results: all `0x23000001`,
- dispatcher status: `0x23000001`,
- root step mask extends to `0x000003ff`,
- final root status `0x23000001`.

The Stage22 phase-service masks remain unchanged:

```text
phase 0 validate        requires 0x00000001
phase 1 DT summary     requires 0x00000005
phase 2 platform       requires 0x00000007
phase 3 return-ready   requires 0x0000000f
union dependency mask  0x0000000f
satisfied phase mask   0x0000000f
phase-service checksum 0x00000008
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
0x00000100 phase-service dependencies complete
0x00000200 descriptor phase dispatcher complete
```

Complete root-step mask: `0x000003ff`.

## Built image

```bash
./stage23/build.sh
```

Successful local build:

```text
out/stage23/stage23-qcdt.img
sha256=de6bcc8bbdf97ad3760a7f609b45fe735cefb47bf679420a5ec58ee157c64fc2
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=42148 (0xa4a4)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage23 mi4ios6=stage23 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage23_vectors
0000ab68 t stage23_kernel_root
0000c794 T mmu_high_bootstrap_selftest
0000d84c T kernel_entry
0000db74 T test_kernel_entry
0000dd0c T stage23_main
00020000 b stage23_l1_table
00026000 B __stage23_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage23/stage23-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2506 KB)                       OKAY [  0.080s]
Booting                                            OKAY [  0.006s]
Finished. Total time: 0.091s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage23-last_kmsg.txt
```

The recovered log was 22656 bytes and contained:

```text
400 MI4IOS6_STAGE23 markers
374 MI4IOS6_STAGE23_XNU markers
```

## Key recovered high-root dispatcher markers

```text
MI4IOS6_STAGE23_XNU high root phase-service dependencies begin
MI4IOS6_STAGE23_XNU high root phase-service dependencies ok
MI4IOS6_STAGE23_XNU high root phase dispatcher begin
MI4IOS6_STAGE23_XNU high root phase dispatcher ok
MI4IOS6_STAGE23_XNU high root phase table begin
MI4IOS6_STAGE23_XNU high root phase table ok
MI4IOS6_STAGE23_XNU high init sequence complete
MI4IOS6_STAGE23_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE23_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE23_XNU high_bootstrap_init_status=0x23000001
MI4IOS6_STAGE23_XNU high_phase_completed_mask=0x0000000f
MI4IOS6_STAGE23_XNU high_phase_status_checksum=0x0000000b
MI4IOS6_STAGE23_XNU high_phase_service_dependency_mask=0x0000000f
MI4IOS6_STAGE23_XNU high_phase_service_satisfied_mask=0x0000000f
MI4IOS6_STAGE23_XNU high_phase_service_status_checksum=0x00000008
MI4IOS6_STAGE23_XNU high_phase_service_dependency_status=0x23000001
MI4IOS6_STAGE23_XNU high_phase_dispatcher_count=0x00000004
MI4IOS6_STAGE23_XNU high_phase_dispatcher_order_mask=0x0000000f
MI4IOS6_STAGE23_XNU high_phase_dispatcher_handler_mask=0x0000000f
MI4IOS6_STAGE23_XNU high_phase_dispatcher_status_checksum=0x00000004
MI4IOS6_STAGE23_XNU high_phase0_descriptor_id=0x00000000
MI4IOS6_STAGE23_XNU high_phase1_descriptor_id=0x00000001
MI4IOS6_STAGE23_XNU high_phase2_descriptor_id=0x00000002
MI4IOS6_STAGE23_XNU high_phase3_descriptor_id=0x00000003
MI4IOS6_STAGE23_XNU high_phase0_handler_result=0x23000001
MI4IOS6_STAGE23_XNU high_phase1_handler_result=0x23000001
MI4IOS6_STAGE23_XNU high_phase2_handler_result=0x23000001
MI4IOS6_STAGE23_XNU high_phase3_handler_result=0x23000001
MI4IOS6_STAGE23_XNU high_phase_dispatcher_status=0x23000001
MI4IOS6_STAGE23_XNU high_service_available_mask=0x0000000f
MI4IOS6_STAGE23_XNU high_bootstrap_status=0x23000001
MI4IOS6_STAGE23_XNU high_bootstrap_checksum=0x27502189
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_result=0x23000001
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_expected_checksum=0x27502189
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_magic_id=0x23002300
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_root_steps_id=0x000003ff
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_root_status_id=0x23000001
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_phase_count_id=0x00000004
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_phase_mask_id=0x0000000f
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_phase_checksum_id=0x0000000b
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_phase_service_dependency_mask_id=0x0000000f
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_phase_service_satisfied_mask_id=0x0000000f
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_phase_service_checksum_id=0x00000008
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_phase_service_status_id=0x23000001
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_phase_dispatcher_count_id=0x00000004
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_phase_dispatcher_order_mask_id=0x0000000f
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_phase_dispatcher_handler_mask_id=0x0000000f
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_phase_dispatcher_checksum_id=0x00000004
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_phase_dispatcher_status_id=0x23000001
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_status_id=0x23000001
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_checksum_id=0x27502189
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_magic_alias=0x23002300
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_root_steps_alias=0x000003ff
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_phase_dispatcher_order_alias=0x0000000f
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_phase_dispatcher_handler_alias=0x0000000f
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_phase_dispatcher_status_alias=0x23000001
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_status_alias=0x23000001
MI4IOS6_STAGE23_XNU mmu_high_bootstrap_checksum_alias=0x27502189
MI4IOS6_STAGE23_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the descriptor-driven dispatcher root path:

```text
MI4IOS6_STAGE23_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the descriptor-driven dispatcher root path:

```text
MI4IOS6_STAGE23_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE23_XNU kernel_entry ok
MI4IOS6_STAGE23 kernel_entry returned success
MI4IOS6_STAGE23 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage23 moves the high-root phase-table implementation away from open-coded per-phase assignments. The root path now has descriptor records that a small dispatcher consumes to decide phase completion. This better matches a real kernel bootstrap pattern where init phases are declared and then executed/validated by a central dispatcher.

The phase dispatcher still runs in a deliberately tiny static form. It does not yet schedule real XNU subsystems, allocate memory dynamically, or start secondary CPUs. It does prove the next structural step: high virtual root code can consume a table of phase descriptors, validate required services for each phase, write per-phase handler results, and make the phase table depend on those dispatcher results.

## Success criteria — met

1. bootloader accepted `stage23-qcdt.img`: yes
2. high virtual root entry ran: yes
3. phase-service dependency validation still ran: yes
4. descriptor-driven phase dispatcher ran: yes
5. dispatcher order mask was complete (`0x0000000f`): yes
6. dispatcher handler-success mask was complete (`0x0000000f`): yes
7. dispatcher checksum matched (`0x00000004`): yes
8. dispatcher status was `0x23000001`: yes
9. phase table was complete (`0x0000000f`): yes
10. root step mask was complete (`0x000003ff`): yes
11. validation mask was zero: yes
12. high root returned status `0x23000001`: yes
13. checksum matched through identity and alias views: yes
14. SGI and timer IRQ paths still worked after high root: yes
15. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage24 can add descriptor-driven init callbacks for services themselves:

- keep identity/recovery mappings and caches disabled,
- keep the Stage23 phase dispatcher,
- add service descriptors for logging/timebase/platform/interrupts,
- make service availability come from descriptor handlers rather than open-coded assignments,
- feed service descriptor results into the existing phase-service dependency masks,
- keep identity-side verification and recovery,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
