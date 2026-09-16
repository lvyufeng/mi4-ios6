# Experiment 31 — Stage28 Manifest-Driven Launch Contract

Date: 2026-06-05

Goal: add a manifest-driven launch contract for the next XNU-like startup boundary.

Stage28 still does **not** run XNU or iOS. It extends Stage27 by adding a launch contract object above the registry, boot policy, and bootstrap manifest. The launch contract records the high-root output expected by a later XNU-like startup path and validates root-step coverage, root status, manifest status, policy status, MMU state, timebase frequency, and interrupt readiness before final root success.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage28 adds over Stage27

Stage27 proved:

- descriptor-driven service dispatcher,
- descriptor-driven phase dispatcher,
- high-root bootstrap registry,
- registry-gated boot policy,
- descriptor-driven bootstrap manifest,
- manifest order/satisfied masks `0x0000003f`,
- manifest root-step observation `0x00001ff3`,
- manifest checksum `0x00000038`,
- root step mask `0x00003fff`,
- status `0x27000001`.

Stage28 adds a launch contract:

- launch contract version: `1`,
- launch contract size: `0x0000028c`,
- required root-step mask: `0x00003ff3`,
- observed root-step mask: `0x00003ff3`,
- required/observed root status: `0x28000001`,
- required/observed manifest status: `0x28000001`,
- required/observed policy status: `0x28000001`,
- required/observed MMU state: `0x00000001`,
- required/observed timebase frequency: `0x0124f800` (`19.2 MHz`),
- required/observed interrupt-readiness mask: `0x0000000f`,
- launch satisfied mask: `0x0000007f`,
- launch checksum: `0x000002f2`,
- launch status: `0x28000001`,
- full root step mask extends to `0x00007fff`,
- final root status `0x28000001`.

Launch contract satisfied bits:

```text
0x00000001 root steps satisfied
0x00000002 root status satisfied
0x00000004 manifest status satisfied
0x00000008 policy status satisfied
0x00000010 MMU state satisfied
0x00000020 timebase satisfied
0x00000040 interrupt readiness satisfied
```

Complete launch satisfied mask: `0x0000007f`.

Interrupt-readiness bits:

```text
0x00000001 GIC distributor enabled
0x00000002 GIC CPU interface enabled
0x00000004 interrupt service available
0x00000008 19.2 MHz timebase available
```

Complete interrupt-readiness mask: `0x0000000f`.

The launch contract required root-step mask intentionally includes manifest completion but excludes final `RESULT`, `RETURN`, and `LAUNCH_CONTRACT` bits while the contract is being evaluated:

```text
required before launch contract: 0x00003ff3
final root steps:                0x00007fff
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
0x00004000 launch contract complete
```

Complete root-step mask: `0x00007fff`.

## Built image

```bash
./stage28/build.sh
```

Successful local build:

```text
out/stage28/stage28-qcdt.img
sha256=172f938e9add9b63807cab50d04d4f426e0ea5be1b63a125f48da30addfd21b1
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=59120 (0xe6f0)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage28 mi4ios6=stage28 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage28_vectors
0000ab68 t stage28_kernel_root
0000dd4c T mmu_high_bootstrap_selftest
0000fc14 T kernel_entry
0000ff3c T test_kernel_entry
000100d4 T stage28_main
00024000 b stage28_l1_table
0002a000 B __stage28_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage28/stage28-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2522 KB)                       OKAY [  0.080s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.090s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage28-last_kmsg.txt
```

The recovered log was 35091 bytes and contained:

```text
575 MI4IOS6_STAGE28 markers
549 MI4IOS6_STAGE28_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE28_XNU high root service dispatcher ok
MI4IOS6_STAGE28_XNU high root service table ok
MI4IOS6_STAGE28_XNU high root phase-service dependencies ok
MI4IOS6_STAGE28_XNU high root phase dispatcher ok
MI4IOS6_STAGE28_XNU high root bootstrap registry ok
MI4IOS6_STAGE28_XNU high root phase table ok
MI4IOS6_STAGE28_XNU high init sequence complete
MI4IOS6_STAGE28_XNU high root boot policy ok
MI4IOS6_STAGE28_XNU high root bootstrap manifest ok
MI4IOS6_STAGE28_XNU high root launch contract ok
MI4IOS6_STAGE28_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE28_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE28_XNU high_bootstrap_init_status=0x28000001
```

Registry, policy, and manifest markers:

```text
MI4IOS6_STAGE28_XNU high_registry_version=0x00000001
MI4IOS6_STAGE28_XNU high_registry_size=0x0000028c
MI4IOS6_STAGE28_XNU high_registry_service_descriptor_mask=0x0000000f
MI4IOS6_STAGE28_XNU high_registry_phase_descriptor_mask=0x0000000f
MI4IOS6_STAGE28_XNU high_registry_dependency_coverage_mask=0x0000000f
MI4IOS6_STAGE28_XNU high_registry_dispatch_coverage_mask=0x000f000f
MI4IOS6_STAGE28_XNU high_registry_status_checksum=0x000f028d
MI4IOS6_STAGE28_XNU high_registry_status=0x28000001
MI4IOS6_STAGE28_XNU high_boot_policy_version=0x00000001
MI4IOS6_STAGE28_XNU high_boot_policy_size=0x0000028c
MI4IOS6_STAGE28_XNU high_boot_policy_required_root_steps=0x00000ff3
MI4IOS6_STAGE28_XNU high_boot_policy_observed_root_steps=0x00000ff3
MI4IOS6_STAGE28_XNU high_boot_policy_satisfied_mask=0x0000003f
MI4IOS6_STAGE28_XNU high_boot_policy_status_checksum=0x000002b2
MI4IOS6_STAGE28_XNU high_boot_policy_status=0x28000001
MI4IOS6_STAGE28_XNU high_manifest_version=0x00000001
MI4IOS6_STAGE28_XNU high_manifest_record_count=0x00000006
MI4IOS6_STAGE28_XNU high_manifest_required_record_mask=0x0000003f
MI4IOS6_STAGE28_XNU high_manifest_order_mask=0x0000003f
MI4IOS6_STAGE28_XNU high_manifest_satisfied_mask=0x0000003f
MI4IOS6_STAGE28_XNU high_manifest_required_root_steps=0x00001ff3
MI4IOS6_STAGE28_XNU high_manifest_observed_root_steps=0x00001ff3
MI4IOS6_STAGE28_XNU high_manifest_status_checksum=0x00000038
MI4IOS6_STAGE28_XNU high_manifest_status=0x28000001
```

Launch contract markers:

```text
MI4IOS6_STAGE28_XNU high_launch_contract_version=0x00000001
MI4IOS6_STAGE28_XNU high_launch_contract_size=0x0000028c
MI4IOS6_STAGE28_XNU high_launch_required_root_steps=0x00003ff3
MI4IOS6_STAGE28_XNU high_launch_observed_root_steps=0x00003ff3
MI4IOS6_STAGE28_XNU high_launch_required_root_status=0x28000001
MI4IOS6_STAGE28_XNU high_launch_observed_root_status=0x28000001
MI4IOS6_STAGE28_XNU high_launch_required_manifest_status=0x28000001
MI4IOS6_STAGE28_XNU high_launch_observed_manifest_status=0x28000001
MI4IOS6_STAGE28_XNU high_launch_required_policy_status=0x28000001
MI4IOS6_STAGE28_XNU high_launch_observed_policy_status=0x28000001
MI4IOS6_STAGE28_XNU high_launch_required_mmu_state=0x00000001
MI4IOS6_STAGE28_XNU high_launch_observed_mmu_state=0x00000001
MI4IOS6_STAGE28_XNU high_launch_required_timebase_freq=0x0124f800
MI4IOS6_STAGE28_XNU high_launch_observed_timebase_freq=0x0124f800
MI4IOS6_STAGE28_XNU high_launch_required_interrupt_mask=0x0000000f
MI4IOS6_STAGE28_XNU high_launch_observed_interrupt_mask=0x0000000f
MI4IOS6_STAGE28_XNU high_launch_satisfied_mask=0x0000007f
MI4IOS6_STAGE28_XNU high_launch_status_checksum=0x000002f2
MI4IOS6_STAGE28_XNU high_launch_status=0x28000001
MI4IOS6_STAGE28_XNU high_root_steps=0x00007fff
MI4IOS6_STAGE28_XNU high_root_status=0x28000001
MI4IOS6_STAGE28_XNU high_bootstrap_status=0x28000001
MI4IOS6_STAGE28_XNU high_bootstrap_checksum=0x0f50553c
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE28_XNU mmu_high_bootstrap_result=0x28000001
MI4IOS6_STAGE28_XNU mmu_high_bootstrap_expected_checksum=0x0f50553c
MI4IOS6_STAGE28_XNU mmu_high_bootstrap_magic_id=0x28002800
MI4IOS6_STAGE28_XNU mmu_high_bootstrap_root_steps_id=0x00007fff
MI4IOS6_STAGE28_XNU mmu_high_bootstrap_launch_status_id=0x28000001
MI4IOS6_STAGE28_XNU mmu_high_bootstrap_launch_satisfied_alias=0x0000007f
MI4IOS6_STAGE28_XNU mmu_high_bootstrap_status_alias=0x28000001
MI4IOS6_STAGE28_XNU mmu_high_bootstrap_checksum_alias=0x0f50553c
MI4IOS6_STAGE28_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the launch-contract-gated root path:

```text
MI4IOS6_STAGE28_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the launch-contract-gated root path:

```text
MI4IOS6_STAGE28_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE28_XNU kernel_entry ok
MI4IOS6_STAGE28 kernel_entry returned success
MI4IOS6_STAGE28 attempting MSM8974 PS_HOLD reset
```

## Implementation note

An initial Stage28 hardware attempt exposed a self-dependency bug in the launch contract: the contract's observed root status was computed while also requiring its own status to already be `OK`. That made the launch record fail with `high_launch_satisfied_mask=0x0000007d`. The final implementation fixed the contract to observe the pre-launch root condition from the already-completed policy and manifest states, then gate the final root success on the launch contract status.

## Interpretation

Stage28 turns the manifest result into a handoff contract. The high-root path now reports an explicit launch boundary with:

1. required root-step coverage,
2. required root/manifest/policy status,
3. MMU-on state,
4. timebase frequency,
5. interrupt-readiness mask,
6. launch checksum/status,
7. identity/high-alias verification.

This is still a small XNU-adjacent kernel skeleton, but the high-root output now has a defined contract suitable for the next stage to consume as a startup boundary rather than just a loose set of logs and masks.

## Success criteria — met

1. bootloader accepted `stage28-qcdt.img`: yes
2. high virtual root entry ran: yes
3. service dispatcher still completed: yes
4. phase dispatcher still completed: yes
5. bootstrap registry still completed: yes
6. boot policy validation still completed: yes
7. bootstrap manifest still completed: yes
8. launch contract validation ran: yes
9. launch root-step requirement matched observation (`0x00003ff3`): yes
10. launch root/manifest/policy statuses matched (`0x28000001`): yes
11. launch MMU state matched (`0x00000001`): yes
12. launch timebase matched (`0x0124f800`): yes
13. launch interrupt mask matched (`0x0000000f`): yes
14. launch satisfied mask was complete (`0x0000007f`): yes
15. launch checksum matched (`0x000002f2`): yes
16. launch status was `0x28000001`: yes
17. full root step mask was complete (`0x00007fff`): yes
18. high root returned status `0x28000001`: yes
19. checksum matched through identity and alias views: yes
20. SGI and timer IRQ paths still worked after high root: yes
21. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage29 can add a contract-consuming startup boundary:

- keep identity/recovery mappings and caches disabled,
- keep descriptor-driven service/phase dispatchers plus registry, policy, manifest, and launch-contract checks,
- add a high-root-to-startup handoff object that consumes the launch contract,
- validate startup boundary version, launch status, root status, manifest status, boot args pointer, DT pointer, timebase, and interrupt readiness,
- preserve identity-side verification and recovery,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
