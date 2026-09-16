# Experiment 33 — Stage30 Startup Routine Handoff

Date: 2026-06-05

Goal: make the startup boundary drive a minimal XNU-like startup routine.

Stage30 still does **not** run XNU or iOS. It extends Stage29 by adding a startup-routine state object after the contract-consuming startup boundary. The routine consumes the startup-boundary output and validates root-step coverage, startup status, launch status, pre-final root status, high virtual boot-args pointer, high virtual Apple-DT pointer, timebase frequency, and interrupt readiness before final root success.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage30 adds over Stage29

Stage29 proved:

- descriptor-driven service dispatcher,
- descriptor-driven phase dispatcher,
- high-root bootstrap registry,
- registry-gated boot policy,
- descriptor-driven bootstrap manifest,
- manifest-driven launch contract,
- contract-consuming startup boundary,
- startup boundary root-step observation `0x00007ff3`,
- startup boundary satisfied mask `0x000000ff`,
- startup boundary status `0x29000001`,
- root step mask `0x0000ffff`,
- status `0x29000001`.

Stage30 adds a startup routine state object:

- startup routine version: `1`,
- startup routine size: `0x00000334`,
- required root-step mask: `0x0000fff3`,
- observed root-step mask: `0x0000fff3`,
- required/observed startup-boundary status: `0x30000001`,
- required/observed launch status: `0x30000001`,
- required/observed pre-final root status: `0x30000001`,
- required/observed boot args pointer: `0xc002c000`,
- required/observed DT pointer: `0xc002c140`,
- required/observed timebase frequency: `0x0124f800` (`19.2 MHz`),
- required/observed interrupt-readiness mask: `0x0000000f`,
- startup routine satisfied mask: `0x000000ff`,
- startup routine checksum: `0x000003ca`,
- startup routine status: `0x30000001`,
- full root step mask extends to `0x0001ffff`,
- final root status `0x30000001`.

Startup routine satisfied bits:

```text
0x00000001 root steps satisfied
0x00000002 startup-boundary status satisfied
0x00000004 launch status satisfied
0x00000008 root status satisfied
0x00000010 boot args pointer satisfied
0x00000020 DT pointer satisfied
0x00000040 timebase satisfied
0x00000080 interrupt readiness satisfied
```

Complete startup routine satisfied mask: `0x000000ff`.

The startup routine required root-step mask intentionally includes startup-boundary completion but excludes final `RESULT`, `RETURN`, and `STARTUP_ROUTINE` bits while the routine is being evaluated:

```text
required before startup routine: 0x0000fff3
final root steps:                0x0001ffff
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
0x00008000 startup boundary complete
0x00010000 startup routine complete
```

Complete root-step mask: `0x0001ffff`.

## Built image

```bash
./stage30/build.sh
```

Successful local build:

```text
out/stage30/stage30-qcdt.img
sha256=669f071c40ee9e008600aa85a5268de0720e38ea16343d01c42cdfd1901f1ed2
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=67100 (0x1061c)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage30 mi4ios6=stage30 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage30_vectors
0000ab68 t stage30_kernel_root
0000e884 T mmu_high_bootstrap_selftest
00010dd0 T kernel_entry
000110f8 T test_kernel_entry
00011290 T stage30_main
00028000 b stage30_l1_table
0002e000 B __stage30_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage30/stage30-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2530 KB)                       OKAY [  0.081s]
Booting                                            OKAY [  0.004s]
Finished. Total time: 0.127s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage30-last_kmsg.txt
```

The recovered log was 40438 bytes and contained:

```text
649 MI4IOS6_STAGE30 markers
623 MI4IOS6_STAGE30_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE30_XNU high root service dispatcher ok
MI4IOS6_STAGE30_XNU high root service table ok
MI4IOS6_STAGE30_XNU high root phase-service dependencies ok
MI4IOS6_STAGE30_XNU high root phase dispatcher ok
MI4IOS6_STAGE30_XNU high root bootstrap registry ok
MI4IOS6_STAGE30_XNU high root phase table ok
MI4IOS6_STAGE30_XNU high init sequence complete
MI4IOS6_STAGE30_XNU high root boot policy ok
MI4IOS6_STAGE30_XNU high root bootstrap manifest ok
MI4IOS6_STAGE30_XNU high root launch contract ok
MI4IOS6_STAGE30_XNU high root startup boundary ok
MI4IOS6_STAGE30_XNU high root startup routine ok
MI4IOS6_STAGE30_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE30_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE30_XNU high_bootstrap_init_status=0x30000001
```

Registry, policy, manifest, launch, and boundary markers:

```text
MI4IOS6_STAGE30_XNU high_registry_version=0x00000001
MI4IOS6_STAGE30_XNU high_registry_size=0x00000334
MI4IOS6_STAGE30_XNU high_registry_status_checksum=0x000f0335
MI4IOS6_STAGE30_XNU high_registry_status=0x30000001
MI4IOS6_STAGE30_XNU high_boot_policy_version=0x00000001
MI4IOS6_STAGE30_XNU high_boot_policy_size=0x00000334
MI4IOS6_STAGE30_XNU high_boot_policy_required_root_steps=0x00000ff3
MI4IOS6_STAGE30_XNU high_boot_policy_observed_root_steps=0x00000ff3
MI4IOS6_STAGE30_XNU high_boot_policy_satisfied_mask=0x0000003f
MI4IOS6_STAGE30_XNU high_boot_policy_status_checksum=0x0000030a
MI4IOS6_STAGE30_XNU high_boot_policy_status=0x30000001
MI4IOS6_STAGE30_XNU high_manifest_version=0x00000001
MI4IOS6_STAGE30_XNU high_manifest_record_count=0x00000006
MI4IOS6_STAGE30_XNU high_manifest_required_record_mask=0x0000003f
MI4IOS6_STAGE30_XNU high_manifest_order_mask=0x0000003f
MI4IOS6_STAGE30_XNU high_manifest_satisfied_mask=0x0000003f
MI4IOS6_STAGE30_XNU high_manifest_required_root_steps=0x00001ff3
MI4IOS6_STAGE30_XNU high_manifest_observed_root_steps=0x00001ff3
MI4IOS6_STAGE30_XNU high_manifest_status_checksum=0x00000038
MI4IOS6_STAGE30_XNU high_manifest_status=0x30000001
MI4IOS6_STAGE30_XNU high_launch_contract_version=0x00000001
MI4IOS6_STAGE30_XNU high_launch_contract_size=0x00000334
MI4IOS6_STAGE30_XNU high_launch_required_root_steps=0x00003ff3
MI4IOS6_STAGE30_XNU high_launch_observed_root_steps=0x00003ff3
MI4IOS6_STAGE30_XNU high_launch_satisfied_mask=0x0000007f
MI4IOS6_STAGE30_XNU high_launch_status_checksum=0x0000034a
MI4IOS6_STAGE30_XNU high_launch_status=0x30000001
MI4IOS6_STAGE30_XNU high_startup_boundary_version=0x00000001
MI4IOS6_STAGE30_XNU high_startup_boundary_size=0x00000334
MI4IOS6_STAGE30_XNU high_startup_required_root_steps=0x00007ff3
MI4IOS6_STAGE30_XNU high_startup_observed_root_steps=0x00007ff3
MI4IOS6_STAGE30_XNU high_startup_satisfied_mask=0x000000ff
MI4IOS6_STAGE30_XNU high_startup_status_checksum=0x000003ca
MI4IOS6_STAGE30_XNU high_startup_status=0x30000001
```

Startup routine markers:

```text
MI4IOS6_STAGE30_XNU high_startup_routine_version=0x00000001
MI4IOS6_STAGE30_XNU high_startup_routine_size=0x00000334
MI4IOS6_STAGE30_XNU high_startup_routine_required_root_steps=0x0000fff3
MI4IOS6_STAGE30_XNU high_startup_routine_observed_root_steps=0x0000fff3
MI4IOS6_STAGE30_XNU high_startup_routine_required_startup_status=0x30000001
MI4IOS6_STAGE30_XNU high_startup_routine_observed_startup_status=0x30000001
MI4IOS6_STAGE30_XNU high_startup_routine_required_launch_status=0x30000001
MI4IOS6_STAGE30_XNU high_startup_routine_observed_launch_status=0x30000001
MI4IOS6_STAGE30_XNU high_startup_routine_required_root_status=0x30000001
MI4IOS6_STAGE30_XNU high_startup_routine_observed_root_status=0x30000001
MI4IOS6_STAGE30_XNU high_startup_routine_required_boot_args_virt=0xc002c000
MI4IOS6_STAGE30_XNU high_startup_routine_observed_boot_args_virt=0xc002c000
MI4IOS6_STAGE30_XNU high_startup_routine_required_dt_virt=0xc002c140
MI4IOS6_STAGE30_XNU high_startup_routine_observed_dt_virt=0xc002c140
MI4IOS6_STAGE30_XNU high_startup_routine_required_timebase_freq=0x0124f800
MI4IOS6_STAGE30_XNU high_startup_routine_observed_timebase_freq=0x0124f800
MI4IOS6_STAGE30_XNU high_startup_routine_required_interrupt_mask=0x0000000f
MI4IOS6_STAGE30_XNU high_startup_routine_observed_interrupt_mask=0x0000000f
MI4IOS6_STAGE30_XNU high_startup_routine_satisfied_mask=0x000000ff
MI4IOS6_STAGE30_XNU high_startup_routine_status_checksum=0x000003ca
MI4IOS6_STAGE30_XNU high_startup_routine_status=0x30000001
MI4IOS6_STAGE30_XNU high_root_steps=0x0001ffff
MI4IOS6_STAGE30_XNU high_root_status=0x30000001
MI4IOS6_STAGE30_XNU high_bootstrap_status=0x30000001
MI4IOS6_STAGE30_XNU high_bootstrap_checksum=0x1751cc84
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE30_XNU mmu_high_bootstrap_result=0x30000001
MI4IOS6_STAGE30_XNU mmu_high_bootstrap_expected_checksum=0x1751cc84
MI4IOS6_STAGE30_XNU mmu_high_bootstrap_magic_id=0x30003000
MI4IOS6_STAGE30_XNU mmu_high_bootstrap_root_steps_id=0x0001ffff
MI4IOS6_STAGE30_XNU mmu_high_bootstrap_startup_routine_satisfied_mask_id=0x000000ff
MI4IOS6_STAGE30_XNU mmu_high_bootstrap_startup_routine_status_id=0x30000001
MI4IOS6_STAGE30_XNU mmu_high_bootstrap_status_alias=0x30000001
MI4IOS6_STAGE30_XNU mmu_high_bootstrap_checksum_alias=0x1751cc84
MI4IOS6_STAGE30_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the startup-routine-gated root path:

```text
MI4IOS6_STAGE30_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the startup-routine-gated root path:

```text
MI4IOS6_STAGE30_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE30_XNU kernel_entry ok
MI4IOS6_STAGE30 kernel_entry returned success
MI4IOS6_STAGE30 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage30 adds an explicit startup routine state after the boundary object. The high-root path now validates a startup layer that consumes the earlier boundary and records:

1. startup-boundary status,
2. launch status,
3. pre-final root status,
4. high virtual boot-args pointer,
5. high virtual Apple-DT pointer,
6. 19.2 MHz timebase,
7. interrupt readiness,
8. startup routine checksum/status,
9. identity/high-alias verification.

This is still a small XNU-adjacent kernel skeleton, but the next stage can split this validated routine into a separately called startup entry and begin shaping it toward a real kernel startup boundary.

## Success criteria — met

1. bootloader accepted `stage30-qcdt.img`: yes
2. high virtual root entry ran: yes
3. service dispatcher still completed: yes
4. phase dispatcher still completed: yes
5. bootstrap registry still completed: yes
6. boot policy validation still completed: yes
7. bootstrap manifest still completed: yes
8. launch contract validation still completed: yes
9. startup boundary validation still completed: yes
10. startup routine validation ran: yes
11. startup routine root-step requirement matched observation (`0x0000fff3`): yes
12. startup/startup-boundary/launch/root statuses matched (`0x30000001`): yes
13. startup routine boot-args pointer matched (`0xc002c000`): yes
14. startup routine DT pointer matched (`0xc002c140`): yes
15. startup routine timebase matched (`0x0124f800`): yes
16. startup routine interrupt mask matched (`0x0000000f`): yes
17. startup routine satisfied mask was complete (`0x000000ff`): yes
18. startup routine checksum matched (`0x000003ca`): yes
19. startup routine status was `0x30000001`: yes
20. full root step mask was complete (`0x0001ffff`): yes
21. high root returned status `0x30000001`: yes
22. checksum matched through identity and alias views: yes
23. SGI and timer IRQ paths still worked after high root: yes
24. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage31 can split the startup routine into a separately called high-virtual startup entry:

- keep identity/recovery mappings and caches disabled,
- keep descriptor-driven service/phase dispatchers plus registry, policy, manifest, launch-contract, startup-boundary, and startup-routine checks,
- introduce a separate startup entry function called through the high alias,
- pass an explicit startup handoff object into that entry,
- validate entry arguments, startup state version, high-root status, boot args, DT, timebase, and interrupt readiness,
- preserve identity-side verification and recovery,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
