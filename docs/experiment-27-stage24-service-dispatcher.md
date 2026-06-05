# Experiment 27 — Stage24 Descriptor-Driven Service Dispatcher

Date: 2026-06-05

Goal: add descriptor-driven service initialization for the high-root logging, timebase, platform, and interrupt services, then feed those service results into the existing phase-service dependency and phase-dispatcher paths.

Stage24 still does **not** run XNU or iOS. It extends Stage23 by replacing open-coded service availability assignments with a small high-root service descriptor table. Each descriptor carries a service ID, readiness condition, status slot, and handler-result slot. The high root dispatches those service descriptors from high virtual execution, records handler/order/availability masks, then lets the existing phase-service dependency validation consume the descriptor-derived service mask.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage24 adds over Stage23

Stage23 proved:

- descriptor-driven high-root phase dispatcher,
- phase dispatcher order mask `0x0000000f`,
- phase dispatcher handler mask `0x0000000f`,
- phase dispatcher checksum `0x00000004`,
- root step mask `0x000003ff`,
- status `0x23000001`.

Stage24 adds a descriptor-driven service dispatcher:

- service dispatcher count: `4`,
- service dispatcher order mask: `0x0000000f`,
- service dispatcher handler-success mask: `0x0000000f`,
- service dispatcher checksum: `0x00000004`,
- service descriptor IDs: `0`, `1`, `2`, `3`,
- service handler results: all `0x24000001`,
- service dispatcher status: `0x24000001`,
- service table mask remains `0x0000000f`,
- phase-service dependency mask remains `0x0000000f`,
- phase dispatcher still completes mask `0x0000000f`,
- root step mask extends to `0x000007ff`,
- final root status `0x24000001`.

Service descriptor IDs:

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
0x00000100 phase-service dependencies complete
0x00000200 descriptor phase dispatcher complete
0x00000400 descriptor service dispatcher complete
```

Complete root-step mask: `0x000007ff`.

## Built image

```bash
./stage24/build.sh
```

Successful local build:

```text
out/stage24/stage24-qcdt.img
sha256=441f73987bd956cbd6718213e334f195021c17547075603dda7a8eea16a3fea3
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=44948 (0xaf94)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage24 mi4ios6=stage24 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage24_vectors
0000ab68 t stage24_kernel_root
0000cad8 T mmu_high_bootstrap_selftest
0000dd9c T kernel_entry
0000e0c4 T test_kernel_entry
0000e25c T stage24_main
00020000 b stage24_l1_table
00026000 B __stage24_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage24/stage24-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2508 KB)                       OKAY [  0.081s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.091s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage24-last_kmsg.txt
```

The recovered log was 24953 bytes and contained:

```text
431 MI4IOS6_STAGE24 markers
405 MI4IOS6_STAGE24_XNU markers
```

## Key recovered high-root service-dispatcher markers

```text
MI4IOS6_STAGE24_XNU high root service dispatcher begin
MI4IOS6_STAGE24_XNU high root service dispatcher ok
MI4IOS6_STAGE24_XNU high root service table begin
MI4IOS6_STAGE24_XNU high root service table ok
MI4IOS6_STAGE24_XNU high root phase-service dependencies begin
MI4IOS6_STAGE24_XNU high root phase-service dependencies ok
MI4IOS6_STAGE24_XNU high root phase dispatcher begin
MI4IOS6_STAGE24_XNU high root phase dispatcher ok
MI4IOS6_STAGE24_XNU high root phase table begin
MI4IOS6_STAGE24_XNU high root phase table ok
MI4IOS6_STAGE24_XNU high init sequence complete
MI4IOS6_STAGE24_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE24_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE24_XNU high_bootstrap_init_status=0x24000001
MI4IOS6_STAGE24_XNU high_phase_completed_mask=0x0000000f
MI4IOS6_STAGE24_XNU high_phase_status_checksum=0x0000000b
MI4IOS6_STAGE24_XNU high_phase_service_dependency_mask=0x0000000f
MI4IOS6_STAGE24_XNU high_phase_service_satisfied_mask=0x0000000f
MI4IOS6_STAGE24_XNU high_phase_service_status_checksum=0x00000008
MI4IOS6_STAGE24_XNU high_phase_service_dependency_status=0x24000001
MI4IOS6_STAGE24_XNU high_phase_dispatcher_count=0x00000004
MI4IOS6_STAGE24_XNU high_phase_dispatcher_order_mask=0x0000000f
MI4IOS6_STAGE24_XNU high_phase_dispatcher_handler_mask=0x0000000f
MI4IOS6_STAGE24_XNU high_phase_dispatcher_status_checksum=0x00000004
MI4IOS6_STAGE24_XNU high_phase_dispatcher_status=0x24000001
MI4IOS6_STAGE24_XNU high_service_count=0x00000004
MI4IOS6_STAGE24_XNU high_service_available_mask=0x0000000f
MI4IOS6_STAGE24_XNU high_service_status_checksum=0x0000000b
MI4IOS6_STAGE24_XNU high_service_logging_status=0x24000001
MI4IOS6_STAGE24_XNU high_service_timebase_status=0x24000001
MI4IOS6_STAGE24_XNU high_service_platform_status=0x24000001
MI4IOS6_STAGE24_XNU high_service_interrupts_status=0x24000001
MI4IOS6_STAGE24_XNU high_service_dispatcher_count=0x00000004
MI4IOS6_STAGE24_XNU high_service_dispatcher_order_mask=0x0000000f
MI4IOS6_STAGE24_XNU high_service_dispatcher_handler_mask=0x0000000f
MI4IOS6_STAGE24_XNU high_service_dispatcher_status_checksum=0x00000004
MI4IOS6_STAGE24_XNU high_service_logging_descriptor_id=0x00000000
MI4IOS6_STAGE24_XNU high_service_timebase_descriptor_id=0x00000001
MI4IOS6_STAGE24_XNU high_service_platform_descriptor_id=0x00000002
MI4IOS6_STAGE24_XNU high_service_interrupts_descriptor_id=0x00000003
MI4IOS6_STAGE24_XNU high_service_logging_handler_result=0x24000001
MI4IOS6_STAGE24_XNU high_service_timebase_handler_result=0x24000001
MI4IOS6_STAGE24_XNU high_service_platform_handler_result=0x24000001
MI4IOS6_STAGE24_XNU high_service_interrupts_handler_result=0x24000001
MI4IOS6_STAGE24_XNU high_service_dispatcher_status=0x24000001
MI4IOS6_STAGE24_XNU high_bootstrap_status=0x24000001
MI4IOS6_STAGE24_XNU high_bootstrap_checksum=0x035022c4
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_result=0x24000001
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_expected_checksum=0x035022c4
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_magic_id=0x24002400
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_root_steps_id=0x000007ff
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_root_status_id=0x24000001
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_service_dispatcher_count_id=0x00000004
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_service_dispatcher_order_mask_id=0x0000000f
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_service_dispatcher_handler_mask_id=0x0000000f
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_service_dispatcher_checksum_id=0x00000004
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_service_dispatcher_status_id=0x24000001
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_status_id=0x24000001
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_checksum_id=0x035022c4
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_magic_alias=0x24002400
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_root_steps_alias=0x000007ff
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_service_dispatcher_order_alias=0x0000000f
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_service_dispatcher_handler_alias=0x0000000f
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_service_dispatcher_status_alias=0x24000001
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_status_alias=0x24000001
MI4IOS6_STAGE24_XNU mmu_high_bootstrap_checksum_alias=0x035022c4
MI4IOS6_STAGE24_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the descriptor-driven service/phase dispatcher root path:

```text
MI4IOS6_STAGE24_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the descriptor-driven service/phase dispatcher root path:

```text
MI4IOS6_STAGE24_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE24_XNU kernel_entry ok
MI4IOS6_STAGE24 kernel_entry returned success
MI4IOS6_STAGE24 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage24 makes service availability descriptor-driven. The high-root path now has two small dispatchers:

1. a service dispatcher that publishes logging/timebase/platform/interrupt services from descriptor handlers,
2. a phase dispatcher that consumes the service availability mask and completes phase records.

This is still a tiny XNU-adjacent skeleton, but it is closer to a real kernel bootstrap shape: platform services are published through a table, phases consume published services through dependency masks, and identity/high-alias verification validates the whole object graph after returning from high virtual execution.

## Success criteria — met

1. bootloader accepted `stage24-qcdt.img`: yes
2. high virtual root entry ran: yes
3. descriptor-driven service dispatcher ran: yes
4. service dispatcher order mask was complete (`0x0000000f`): yes
5. service dispatcher handler-success mask was complete (`0x0000000f`): yes
6. service dispatcher checksum matched (`0x00000004`): yes
7. service dispatcher status was `0x24000001`: yes
8. service table was complete (`0x0000000f`): yes
9. phase-service dependency validation still completed: yes
10. descriptor-driven phase dispatcher still completed: yes
11. root step mask was complete (`0x000007ff`): yes
12. validation mask was zero: yes
13. high root returned status `0x24000001`: yes
14. checksum matched through identity and alias views: yes
15. SGI and timer IRQ paths still worked after high root: yes
16. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage25 can introduce a high-root bootstrap registry that cross-checks service descriptors and phase descriptors as one object graph:

- keep identity/recovery mappings and caches disabled,
- keep descriptor-driven service and phase dispatchers,
- record registry counts, descriptor masks, and dependency masks in one summary object,
- validate that every phase dependency is provided by a service descriptor,
- validate that dispatcher execution order covers all registered descriptors,
- keep identity-side verification and recovery,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
