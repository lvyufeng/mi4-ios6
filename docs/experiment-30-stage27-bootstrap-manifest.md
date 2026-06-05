# Experiment 30 — Stage27 Descriptor-Driven Bootstrap Manifest

Date: 2026-06-05

Goal: turn the Stage26 boot policy into a descriptor-driven bootstrap manifest.

Stage27 still does **not** run XNU or iOS. It extends Stage26 by adding a manifest object above the high-root bootstrap registry and boot policy. The manifest binds service coverage, phase coverage, dependency coverage, dispatcher coverage, policy satisfaction, and final policy status into ordered records. Each record is dispatched through a descriptor and must match its required value before the high root can return final success.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage27 adds over Stage26

Stage26 proved:

- descriptor-driven service dispatcher,
- descriptor-driven phase dispatcher,
- high-root bootstrap registry,
- registry-gated high-root boot policy,
- required/observed policy root-step mask `0x00000ff3`,
- policy satisfied mask `0x0000003f`,
- policy checksum `0x000001ea`,
- root step mask `0x00001fff`,
- status `0x26000001`.

Stage27 adds a descriptor-driven bootstrap manifest:

- manifest version: `1`,
- manifest record count: `6`,
- required record mask: `0x0000003f`,
- manifest order mask: `0x0000003f`,
- manifest satisfied mask: `0x0000003f`,
- manifest required root steps: `0x00001ff3`,
- manifest observed root steps: `0x00001ff3`,
- manifest checksum: `0x00000038`,
- manifest status: `0x27000001`,
- full root step mask extends to `0x00003fff`,
- final root status `0x27000001`.

Manifest records:

```text
record 0  service coverage      required/observed 0x0000000f
record 1  phase coverage        required/observed 0x0000000f
record 2  dependency coverage   required/observed 0x0000000f
record 3  dispatcher coverage   required/observed 0x000f000f
record 4  policy satisfied mask required/observed 0x0000003f
record 5  boot status           required/observed 0x27000001
```

The manifest required root-step mask intentionally includes policy completion but excludes final `RESULT`, `RETURN`, and `MANIFEST` bits while the manifest is being evaluated:

```text
required before manifest: 0x00001ff3
final root steps:         0x00003fff
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
0x00001000 boot policy complete
0x00002000 bootstrap manifest complete
```

Complete root-step mask: `0x00003fff`.

## Built image

```bash
./stage27/build.sh
```

Successful local build:

```text
out/stage27/stage27-qcdt.img
sha256=46550ba0f8cd4a0b45ea8c5245d02df613bbc9fcdf20ab2f4cecf8f64a190028
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=55472 (0xd8b0)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage27 mi4ios6=stage27 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage27_vectors
0000ab68 t stage27_kernel_root
0000d828 T mmu_high_bootstrap_selftest
0000f44c T kernel_entry
0000f774 T test_kernel_entry
0000f90c T stage27_main
00024000 b stage27_l1_table
0002a000 B __stage27_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage27/stage27-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2520 KB)                       OKAY [  0.080s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.090s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage27-last_kmsg.txt
```

The recovered log was 32404 bytes and contained:

```text
536 MI4IOS6_STAGE27 markers
510 MI4IOS6_STAGE27_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE27_XNU high root service dispatcher ok
MI4IOS6_STAGE27_XNU high root service table ok
MI4IOS6_STAGE27_XNU high root phase-service dependencies ok
MI4IOS6_STAGE27_XNU high root phase dispatcher ok
MI4IOS6_STAGE27_XNU high root bootstrap registry ok
MI4IOS6_STAGE27_XNU high root phase table ok
MI4IOS6_STAGE27_XNU high init sequence complete
MI4IOS6_STAGE27_XNU high root boot policy ok
MI4IOS6_STAGE27_XNU high root bootstrap manifest ok
MI4IOS6_STAGE27_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE27_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE27_XNU high_bootstrap_init_status=0x27000001
```

Registry and policy markers:

```text
MI4IOS6_STAGE27_XNU high_registry_version=0x00000001
MI4IOS6_STAGE27_XNU high_registry_size=0x00000240
MI4IOS6_STAGE27_XNU high_registry_service_descriptor_mask=0x0000000f
MI4IOS6_STAGE27_XNU high_registry_phase_descriptor_mask=0x0000000f
MI4IOS6_STAGE27_XNU high_registry_dependency_coverage_mask=0x0000000f
MI4IOS6_STAGE27_XNU high_registry_dispatch_coverage_mask=0x000f000f
MI4IOS6_STAGE27_XNU high_registry_status_checksum=0x000f0241
MI4IOS6_STAGE27_XNU high_registry_status=0x27000001
MI4IOS6_STAGE27_XNU high_boot_policy_version=0x00000001
MI4IOS6_STAGE27_XNU high_boot_policy_size=0x00000240
MI4IOS6_STAGE27_XNU high_boot_policy_required_root_steps=0x00000ff3
MI4IOS6_STAGE27_XNU high_boot_policy_observed_root_steps=0x00000ff3
MI4IOS6_STAGE27_XNU high_boot_policy_satisfied_mask=0x0000003f
MI4IOS6_STAGE27_XNU high_boot_policy_status_checksum=0x0000027e
MI4IOS6_STAGE27_XNU high_boot_policy_status=0x27000001
```

Manifest markers:

```text
MI4IOS6_STAGE27_XNU high_manifest_version=0x00000001
MI4IOS6_STAGE27_XNU high_manifest_record_count=0x00000006
MI4IOS6_STAGE27_XNU high_manifest_required_record_mask=0x0000003f
MI4IOS6_STAGE27_XNU high_manifest_order_mask=0x0000003f
MI4IOS6_STAGE27_XNU high_manifest_satisfied_mask=0x0000003f
MI4IOS6_STAGE27_XNU high_manifest_required_root_steps=0x00001ff3
MI4IOS6_STAGE27_XNU high_manifest_observed_root_steps=0x00001ff3
MI4IOS6_STAGE27_XNU high_manifest_service_required_mask=0x0000000f
MI4IOS6_STAGE27_XNU high_manifest_service_observed_mask=0x0000000f
MI4IOS6_STAGE27_XNU high_manifest_service_status=0x27000001
MI4IOS6_STAGE27_XNU high_manifest_phase_required_mask=0x0000000f
MI4IOS6_STAGE27_XNU high_manifest_phase_observed_mask=0x0000000f
MI4IOS6_STAGE27_XNU high_manifest_phase_status=0x27000001
MI4IOS6_STAGE27_XNU high_manifest_dependency_required_mask=0x0000000f
MI4IOS6_STAGE27_XNU high_manifest_dependency_observed_mask=0x0000000f
MI4IOS6_STAGE27_XNU high_manifest_dependency_status=0x27000001
MI4IOS6_STAGE27_XNU high_manifest_dispatch_required_mask=0x000f000f
MI4IOS6_STAGE27_XNU high_manifest_dispatch_observed_mask=0x000f000f
MI4IOS6_STAGE27_XNU high_manifest_dispatch_status=0x27000001
MI4IOS6_STAGE27_XNU high_manifest_policy_required_mask=0x0000003f
MI4IOS6_STAGE27_XNU high_manifest_policy_observed_mask=0x0000003f
MI4IOS6_STAGE27_XNU high_manifest_policy_status=0x27000001
MI4IOS6_STAGE27_XNU high_manifest_boot_required_status=0x27000001
MI4IOS6_STAGE27_XNU high_manifest_boot_observed_status=0x27000001
MI4IOS6_STAGE27_XNU high_manifest_boot_status_record_status=0x27000001
MI4IOS6_STAGE27_XNU high_manifest_status_checksum=0x00000038
MI4IOS6_STAGE27_XNU high_manifest_status=0x27000001
MI4IOS6_STAGE27_XNU high_root_steps=0x00003fff
MI4IOS6_STAGE27_XNU high_root_status=0x27000001
MI4IOS6_STAGE27_XNU high_bootstrap_status=0x27000001
MI4IOS6_STAGE27_XNU high_bootstrap_checksum=0x27501af5
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE27_XNU mmu_high_bootstrap_result=0x27000001
MI4IOS6_STAGE27_XNU mmu_high_bootstrap_expected_checksum=0x27501af5
MI4IOS6_STAGE27_XNU mmu_high_bootstrap_magic_id=0x27002700
MI4IOS6_STAGE27_XNU mmu_high_bootstrap_root_steps_id=0x00003fff
MI4IOS6_STAGE27_XNU mmu_high_bootstrap_root_status_id=0x27000001
MI4IOS6_STAGE27_XNU mmu_high_bootstrap_manifest_status_id=0x27000001
MI4IOS6_STAGE27_XNU mmu_high_bootstrap_manifest_satisfied_alias=0x0000003f
MI4IOS6_STAGE27_XNU mmu_high_bootstrap_status_alias=0x27000001
MI4IOS6_STAGE27_XNU mmu_high_bootstrap_checksum_alias=0x27501af5
MI4IOS6_STAGE27_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the manifest-gated root path:

```text
MI4IOS6_STAGE27_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the manifest-gated root path:

```text
MI4IOS6_STAGE27_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE27_XNU kernel_entry ok
MI4IOS6_STAGE27 kernel_entry returned success
MI4IOS6_STAGE27 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage27 makes the high-root bootstrap contract descriptor-driven at another level. The root path now has:

1. platform facts,
2. service descriptors,
3. phase descriptors,
4. service/phase dependency validation,
5. registry coverage,
6. boot policy requirements,
7. manifest records binding the registry and policy into an ordered object graph,
8. final root success gated on manifest status.

This is still a small XNU-adjacent kernel skeleton, but the high-root path now resembles a declarative early-bootstrap contract: facts are discovered, descriptors run, coverage is summarized, policy is checked, and a manifest records which parts of the bootstrap graph must be present before success.

## Success criteria — met

1. bootloader accepted `stage27-qcdt.img`: yes
2. high virtual root entry ran: yes
3. service dispatcher still completed: yes
4. phase dispatcher still completed: yes
5. bootstrap registry still completed: yes
6. boot policy validation still completed: yes
7. manifest descriptor dispatch ran: yes
8. manifest record mask/order/satisfied masks were complete (`0x0000003f`): yes
9. manifest root-step requirement matched observation (`0x00001ff3`): yes
10. manifest service/phase/dependency records matched (`0x0000000f`): yes
11. manifest dispatch record matched (`0x000f000f`): yes
12. manifest policy record matched (`0x0000003f`): yes
13. manifest boot status record matched (`0x27000001`): yes
14. manifest checksum matched (`0x00000038`): yes
15. manifest status was `0x27000001`: yes
16. full root step mask was complete (`0x00003fff`): yes
17. high root returned status `0x27000001`: yes
18. checksum matched through identity and alias views: yes
19. SGI and timer IRQ paths still worked after high root: yes
20. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage28 can add a manifest-driven launch contract for the next kernel bootstrap boundary:

- keep identity/recovery mappings and caches disabled,
- keep descriptor-driven service and phase dispatchers,
- keep registry, policy, and manifest checks,
- add a launch contract object that records the high-root output expected by a later XNU-like startup path,
- validate launch contract version, root status, manifest status, MMU state, timer frequency, and interrupt readiness before returning,
- preserve identity-side verification and recovery,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
