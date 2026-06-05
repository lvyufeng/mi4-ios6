# Experiment 28 — Stage25 High-Root Bootstrap Registry

Date: 2026-06-05

Goal: add a high-root bootstrap registry that cross-checks service descriptors and phase descriptors as one object graph.

Stage25 still does **not** run XNU or iOS. It extends Stage24 by adding a registry summary object over the descriptor-driven service and phase dispatchers. The registry records service descriptor coverage, phase descriptor coverage, phase dependency coverage, and combined dispatcher execution coverage. The high root validates that every phase dependency is provided by a registered service descriptor and that both service and phase dispatchers ran all registered descriptors.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage25 adds over Stage24

Stage24 proved:

- descriptor-driven service dispatcher,
- service dispatcher order mask `0x0000000f`,
- service dispatcher handler mask `0x0000000f`,
- service dispatcher checksum `0x00000004`,
- root step mask `0x000007ff`,
- status `0x24000001`.

Stage25 adds a bootstrap registry summary:

- registry version: `1`,
- registry size: `0x00000190`,
- registered service descriptor mask: `0x0000000f`,
- registered phase descriptor mask: `0x0000000f`,
- dependency coverage mask: `0x0000000f`,
- dispatch coverage mask: `0x000f000f`,
- registry checksum: `0x000f0191`,
- registry status: `0x25000001`,
- root step mask extends to `0x00000fff`,
- final root status `0x25000001`.

Registry coverage meaning:

```text
service descriptor mask        0x0000000f  logging/timebase/platform/interrupts registered
phase descriptor mask          0x0000000f  validate/DT/platform/return-ready registered
dependency coverage mask       0x0000000f  all phase service dependencies provided
dispatch coverage mask         0x000f000f  low 16 bits: services dispatched; high 16 bits: phases dispatched
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
0x00000800 bootstrap registry complete
```

Complete root-step mask: `0x00000fff`.

## Built image

```bash
./stage25/build.sh
```

Successful local build:

```text
out/stage25/stage25-qcdt.img
sha256=bc83b7e5d1e4eee00915563d0e948f92d1f6c89704f724369f4824e3f9b2ba60
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=47028 (0xb7b4)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage25 mi4ios6=stage25 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage25_vectors
0000ab68 t stage25_kernel_root
0000cd50 T mmu_high_bootstrap_selftest
0000e238 T kernel_entry
0000e560 T test_kernel_entry
0000e6f8 T stage25_main
00020000 b stage25_l1_table
00026000 B __stage25_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage25/stage25-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2510 KB)                       OKAY [  0.080s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.090s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage25-last_kmsg.txt
```

The recovered log was 26324 bytes and contained:

```text
451 MI4IOS6_STAGE25 markers
425 MI4IOS6_STAGE25_XNU markers
```

## Key recovered high-root registry markers

```text
MI4IOS6_STAGE25_XNU high root service dispatcher begin
MI4IOS6_STAGE25_XNU high root service dispatcher ok
MI4IOS6_STAGE25_XNU high root service table begin
MI4IOS6_STAGE25_XNU high root service table ok
MI4IOS6_STAGE25_XNU high root phase-service dependencies begin
MI4IOS6_STAGE25_XNU high root phase-service dependencies ok
MI4IOS6_STAGE25_XNU high root phase dispatcher begin
MI4IOS6_STAGE25_XNU high root phase dispatcher ok
MI4IOS6_STAGE25_XNU high root bootstrap registry begin
MI4IOS6_STAGE25_XNU high root bootstrap registry ok
MI4IOS6_STAGE25_XNU high root phase table begin
MI4IOS6_STAGE25_XNU high root phase table ok
MI4IOS6_STAGE25_XNU high init sequence complete
MI4IOS6_STAGE25_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE25_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE25_XNU high_bootstrap_init_status=0x25000001
MI4IOS6_STAGE25_XNU high_phase_completed_mask=0x0000000f
MI4IOS6_STAGE25_XNU high_phase_status_checksum=0x0000000b
MI4IOS6_STAGE25_XNU high_phase_service_dependency_mask=0x0000000f
MI4IOS6_STAGE25_XNU high_phase_service_satisfied_mask=0x0000000f
MI4IOS6_STAGE25_XNU high_phase_service_status_checksum=0x00000008
MI4IOS6_STAGE25_XNU high_phase_service_dependency_status=0x25000001
MI4IOS6_STAGE25_XNU high_phase_dispatcher_count=0x00000004
MI4IOS6_STAGE25_XNU high_phase_dispatcher_order_mask=0x0000000f
MI4IOS6_STAGE25_XNU high_phase_dispatcher_handler_mask=0x0000000f
MI4IOS6_STAGE25_XNU high_phase_dispatcher_status_checksum=0x00000004
MI4IOS6_STAGE25_XNU high_phase_dispatcher_status=0x25000001
MI4IOS6_STAGE25_XNU high_service_count=0x00000004
MI4IOS6_STAGE25_XNU high_service_available_mask=0x0000000f
MI4IOS6_STAGE25_XNU high_service_status_checksum=0x0000000b
MI4IOS6_STAGE25_XNU high_service_dispatcher_count=0x00000004
MI4IOS6_STAGE25_XNU high_service_dispatcher_order_mask=0x0000000f
MI4IOS6_STAGE25_XNU high_service_dispatcher_handler_mask=0x0000000f
MI4IOS6_STAGE25_XNU high_service_dispatcher_status_checksum=0x00000004
MI4IOS6_STAGE25_XNU high_service_dispatcher_status=0x25000001
MI4IOS6_STAGE25_XNU high_registry_version=0x00000001
MI4IOS6_STAGE25_XNU high_registry_size=0x00000190
MI4IOS6_STAGE25_XNU high_registry_service_descriptor_mask=0x0000000f
MI4IOS6_STAGE25_XNU high_registry_phase_descriptor_mask=0x0000000f
MI4IOS6_STAGE25_XNU high_registry_dependency_coverage_mask=0x0000000f
MI4IOS6_STAGE25_XNU high_registry_dispatch_coverage_mask=0x000f000f
MI4IOS6_STAGE25_XNU high_registry_status_checksum=0x000f0191
MI4IOS6_STAGE25_XNU high_registry_status=0x25000001
MI4IOS6_STAGE25_XNU high_bootstrap_status=0x25000001
MI4IOS6_STAGE25_XNU high_bootstrap_checksum=0x27502b25
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_result=0x25000001
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_expected_checksum=0x27502b25
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_magic_id=0x25002500
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_root_steps_id=0x00000fff
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_root_status_id=0x25000001
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_init_steps_id=0x0000000f
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_init_status_id=0x25000001
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_registry_version_id=0x00000001
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_registry_size_id=0x00000190
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_registry_service_descriptor_mask_id=0x0000000f
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_registry_phase_descriptor_mask_id=0x0000000f
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_registry_dependency_coverage_mask_id=0x0000000f
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_registry_dispatch_coverage_mask_id=0x000f000f
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_registry_checksum_id=0x000f0191
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_registry_status_id=0x25000001
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_status_id=0x25000001
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_checksum_id=0x27502b25
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_magic_alias=0x25002500
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_root_steps_alias=0x00000fff
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_registry_status_alias=0x25000001
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_registry_dispatch_coverage_alias=0x000f000f
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_status_alias=0x25000001
MI4IOS6_STAGE25_XNU mmu_high_bootstrap_checksum_alias=0x27502b25
MI4IOS6_STAGE25_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the registry root path:

```text
MI4IOS6_STAGE25_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the registry root path:

```text
MI4IOS6_STAGE25_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE25_XNU kernel_entry ok
MI4IOS6_STAGE25 kernel_entry returned success
MI4IOS6_STAGE25 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage25 ties the descriptor-driven bootstrap pieces together. The high root now validates services, phases, and dependency coverage as one registry-like object graph:

1. service descriptors publish service coverage,
2. phase descriptors publish phase coverage,
3. phase dependency masks are checked against registered services,
4. dispatcher order masks prove all registered descriptors ran,
5. the registry result is carried through identity and high-alias verification.

This is still a small XNU-adjacent kernel skeleton, but it is moving toward a structured early-kernel bootstrap model where platform services and init phases are declared, dispatched, checked, and summarized before the root returns.

## Success criteria — met

1. bootloader accepted `stage25-qcdt.img`: yes
2. high virtual root entry ran: yes
3. service dispatcher still completed: yes
4. phase dispatcher still completed: yes
5. bootstrap registry validation ran: yes
6. service descriptor mask was complete (`0x0000000f`): yes
7. phase descriptor mask was complete (`0x0000000f`): yes
8. dependency coverage mask was complete (`0x0000000f`): yes
9. dispatch coverage mask was complete (`0x000f000f`): yes
10. registry checksum matched (`0x000f0191`): yes
11. registry status was `0x25000001`: yes
12. root step mask was complete (`0x00000fff`): yes
13. validation mask was zero: yes
14. high root returned status `0x25000001`: yes
15. checksum matched through identity and alias views: yes
16. SGI and timer IRQ paths still worked after high root: yes
17. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage26 can add registry-gated boot policy checks before root success:

- keep identity/recovery mappings and caches disabled,
- keep descriptor-driven service and phase dispatchers,
- keep the Stage25 bootstrap registry,
- add a boot policy object that records required registry/service/phase masks,
- gate the final root success on the policy object rather than only raw step masks,
- keep identity-side verification and recovery,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
