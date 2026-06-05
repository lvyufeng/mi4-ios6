# Experiment 29 — Stage26 Registry-Gated Boot Policy

Date: 2026-06-05

Goal: add a registry-gated high-root boot policy object before final root success.

Stage26 still does **not** run XNU or iOS. It extends Stage25 by adding a boot policy object on top of the high-root bootstrap registry. The policy records the masks and statuses that are required for the root path to be considered successful, compares them with observed registry/service/phase/dependency/dispatcher state, and gates final high-root success on that policy result.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage26 adds over Stage25

Stage25 proved:

- descriptor-driven service dispatcher,
- descriptor-driven phase dispatcher,
- high-root bootstrap registry,
- service descriptor mask `0x0000000f`,
- phase descriptor mask `0x0000000f`,
- dependency coverage mask `0x0000000f`,
- dispatch coverage mask `0x000f000f`,
- root step mask `0x00000fff`,
- status `0x25000001`.

Stage26 adds a boot policy summary:

- policy version: `1`,
- policy size: `0x000001d4`,
- required root-step mask: `0x00000ff3`,
- observed root-step mask: `0x00000ff3`,
- required/observed service mask: `0x0000000f`,
- required/observed phase mask: `0x0000000f`,
- required/observed dependency coverage: `0x0000000f`,
- required/observed dispatch coverage: `0x000f000f`,
- required/observed registry status: `0x26000001`,
- policy satisfied mask: `0x0000003f`,
- policy checksum: `0x000001ea`,
- policy status: `0x26000001`,
- full root step mask extends to `0x00001fff`,
- final root status `0x26000001`.

The policy required root-step mask intentionally excludes the final `RESULT`, `RETURN`, and `BOOT_POLICY` bits while the policy is being evaluated:

```text
required before policy: 0x00000ff3
final root steps:       0x00001fff
```

Policy satisfied bits:

```text
0x00000001 root steps satisfied
0x00000002 service mask satisfied
0x00000004 phase mask satisfied
0x00000008 dependency mask satisfied
0x00000010 dispatcher coverage satisfied
0x00000020 registry status satisfied
```

Complete policy satisfied mask: `0x0000003f`.

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
0x00001000 boot policy complete
```

Complete root-step mask: `0x00001fff`.

## Built image

```bash
./stage26/build.sh
```

Successful local build:

```text
out/stage26/stage26-qcdt.img
sha256=a9aa7f2dafb7ff75c2a842aae3aa031720ab46560557edc5f109f2373fa721d2
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=50816 (0xc680)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage26 mi4ios6=stage26 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage26_vectors
0000ab68 t stage26_kernel_root
0000d160 T mmu_high_bootstrap_selftest
0000e99c T kernel_entry
0000ecc4 T test_kernel_entry
0000ee5c T stage26_main
00024000 b stage26_l1_table
0002a000 B __stage26_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage26/stage26-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2514 KB)                       OKAY [  0.080s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.092s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage26-last_kmsg.txt
```

The recovered log was 29243 bytes and contained:

```text
490 MI4IOS6_STAGE26 markers
464 MI4IOS6_STAGE26_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE26_XNU high root service dispatcher ok
MI4IOS6_STAGE26_XNU high root service table ok
MI4IOS6_STAGE26_XNU high root phase-service dependencies ok
MI4IOS6_STAGE26_XNU high root phase dispatcher ok
MI4IOS6_STAGE26_XNU high root bootstrap registry ok
MI4IOS6_STAGE26_XNU high root phase table ok
MI4IOS6_STAGE26_XNU high init sequence complete
MI4IOS6_STAGE26_XNU high root boot policy ok
MI4IOS6_STAGE26_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE26_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE26_XNU high_bootstrap_init_status=0x26000001
MI4IOS6_STAGE26_XNU high_phase_completed_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_phase_status_checksum=0x0000000b
MI4IOS6_STAGE26_XNU high_phase_service_dependency_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_phase_service_satisfied_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_phase_service_status_checksum=0x00000008
MI4IOS6_STAGE26_XNU high_phase_service_dependency_status=0x26000001
MI4IOS6_STAGE26_XNU high_phase_dispatcher_count=0x00000004
MI4IOS6_STAGE26_XNU high_phase_dispatcher_order_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_phase_dispatcher_handler_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_phase_dispatcher_status_checksum=0x00000004
MI4IOS6_STAGE26_XNU high_phase_dispatcher_status=0x26000001
MI4IOS6_STAGE26_XNU high_service_count=0x00000004
MI4IOS6_STAGE26_XNU high_service_available_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_service_status_checksum=0x0000000b
MI4IOS6_STAGE26_XNU high_service_dispatcher_count=0x00000004
MI4IOS6_STAGE26_XNU high_service_dispatcher_order_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_service_dispatcher_handler_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_service_dispatcher_status_checksum=0x00000004
MI4IOS6_STAGE26_XNU high_service_dispatcher_status=0x26000001
MI4IOS6_STAGE26_XNU high_registry_version=0x00000001
MI4IOS6_STAGE26_XNU high_registry_size=0x000001d4
MI4IOS6_STAGE26_XNU high_registry_service_descriptor_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_registry_phase_descriptor_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_registry_dependency_coverage_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_registry_dispatch_coverage_mask=0x000f000f
MI4IOS6_STAGE26_XNU high_registry_status_checksum=0x000f01d5
MI4IOS6_STAGE26_XNU high_registry_status=0x26000001
```

Policy markers:

```text
MI4IOS6_STAGE26_XNU high_boot_policy_version=0x00000001
MI4IOS6_STAGE26_XNU high_boot_policy_size=0x000001d4
MI4IOS6_STAGE26_XNU high_boot_policy_required_root_steps=0x00000ff3
MI4IOS6_STAGE26_XNU high_boot_policy_required_service_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_boot_policy_required_phase_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_boot_policy_required_dependency_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_boot_policy_required_dispatch_coverage_mask=0x000f000f
MI4IOS6_STAGE26_XNU high_boot_policy_required_registry_status=0x26000001
MI4IOS6_STAGE26_XNU high_boot_policy_observed_root_steps=0x00000ff3
MI4IOS6_STAGE26_XNU high_boot_policy_observed_service_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_boot_policy_observed_phase_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_boot_policy_observed_dependency_mask=0x0000000f
MI4IOS6_STAGE26_XNU high_boot_policy_observed_dispatch_coverage_mask=0x000f000f
MI4IOS6_STAGE26_XNU high_boot_policy_observed_registry_status=0x26000001
MI4IOS6_STAGE26_XNU high_boot_policy_satisfied_mask=0x0000003f
MI4IOS6_STAGE26_XNU high_boot_policy_status_checksum=0x000001ea
MI4IOS6_STAGE26_XNU high_boot_policy_status=0x26000001
MI4IOS6_STAGE26_XNU high_root_steps=0x00001fff
MI4IOS6_STAGE26_XNU high_root_status=0x26000001
MI4IOS6_STAGE26_XNU high_bootstrap_status=0x26000001
MI4IOS6_STAGE26_XNU high_bootstrap_checksum=0x01503878
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_result=0x26000001
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_expected_checksum=0x01503878
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_magic_id=0x26002600
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_root_steps_id=0x00001fff
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_root_status_id=0x26000001
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_boot_policy_version_id=0x00000001
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_boot_policy_size_id=0x000001d4
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_boot_policy_required_root_steps_id=0x00000ff3
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_boot_policy_observed_root_steps_id=0x00000ff3
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_boot_policy_satisfied_mask_id=0x0000003f
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_boot_policy_checksum_id=0x000001ea
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_boot_policy_status_id=0x26000001
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_boot_policy_satisfied_alias=0x0000003f
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_boot_policy_status_alias=0x26000001
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_boot_policy_observed_root_steps_alias=0x00000ff3
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_status_alias=0x26000001
MI4IOS6_STAGE26_XNU mmu_high_bootstrap_checksum_alias=0x01503878
MI4IOS6_STAGE26_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the policy-gated root path:

```text
MI4IOS6_STAGE26_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the policy-gated root path:

```text
MI4IOS6_STAGE26_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE26_XNU kernel_entry ok
MI4IOS6_STAGE26 kernel_entry returned success
MI4IOS6_STAGE26 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage26 makes the high-root success condition more explicit. Earlier stages accumulated status through direct checks and root-step masks. Stage26 introduces a policy object that says what the root requires, records what the registry and dispatchers observed, and only then allows final success.

This moves the skeleton closer to an early-kernel bootstrap contract:

1. platform facts are discovered,
2. services and phases are described,
3. dispatchers execute descriptors,
4. the bootstrap registry summarizes descriptor/dependency coverage,
5. boot policy compares requirements with observed state,
6. final root success depends on the policy status.

## Success criteria — met

1. bootloader accepted `stage26-qcdt.img`: yes
2. high virtual root entry ran: yes
3. service dispatcher still completed: yes
4. phase dispatcher still completed: yes
5. bootstrap registry still completed: yes
6. boot policy validation ran: yes
7. policy required and observed root-step masks matched (`0x00000ff3`): yes
8. policy required and observed service masks matched (`0x0000000f`): yes
9. policy required and observed phase masks matched (`0x0000000f`): yes
10. policy required and observed dependency masks matched (`0x0000000f`): yes
11. policy required and observed dispatch coverage matched (`0x000f000f`): yes
12. policy satisfied mask was complete (`0x0000003f`): yes
13. policy checksum matched (`0x000001ea`): yes
14. policy status was `0x26000001`: yes
15. full root step mask was complete (`0x00001fff`): yes
16. high root returned status `0x26000001`: yes
17. checksum matched through identity and alias views: yes
18. SGI and timer IRQ paths still worked after high root: yes
19. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage27 can turn the boot policy into a descriptor-driven bootstrap manifest:

- keep identity/recovery mappings and caches disabled,
- keep descriptor-driven service and phase dispatchers,
- keep the Stage25 registry and Stage26 boot policy,
- add manifest records that bind services, phases, registry coverage, and policy requirements,
- validate manifest order and checksums before final root success,
- preserve identity-side verification and recovery,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
