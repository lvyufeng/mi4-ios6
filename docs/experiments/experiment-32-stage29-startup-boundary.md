# Experiment 32 — Stage29 Contract-Consuming Startup Boundary

Date: 2026-06-05

Goal: add a contract-consuming startup boundary after the Stage28 launch contract.

Stage29 still does **not** run XNU or iOS. It extends Stage28 by adding a high-root-to-startup handoff object that consumes the launch contract as input. The startup boundary validates the launch status, pre-startup root status, manifest status, high virtual boot-args pointer, high virtual Apple-DT pointer, timebase frequency, and interrupt readiness before final root success.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage29 adds over Stage28

Stage28 proved:

- descriptor-driven service dispatcher,
- descriptor-driven phase dispatcher,
- high-root bootstrap registry,
- registry-gated boot policy,
- descriptor-driven bootstrap manifest,
- manifest-driven launch contract,
- launch root-step observation `0x00003ff3`,
- launch satisfied mask `0x0000007f`,
- launch status `0x28000001`,
- root step mask `0x00007fff`,
- status `0x28000001`.

Stage29 adds a startup boundary:

- startup boundary version: `1`,
- startup boundary size: `0x000002e0`,
- required root-step mask: `0x00007ff3`,
- observed root-step mask: `0x00007ff3`,
- required/observed launch status: `0x29000001`,
- required/observed pre-startup root status: `0x29000001`,
- required/observed manifest status: `0x29000001`,
- required/observed boot args pointer: `0xc0028000`,
- required/observed DT pointer: `0xc0028140`,
- required/observed timebase frequency: `0x0124f800` (`19.2 MHz`),
- required/observed interrupt-readiness mask: `0x0000000f`,
- startup satisfied mask: `0x000000ff`,
- startup checksum: `0x0000021e`,
- startup status: `0x29000001`,
- full root step mask extends to `0x0000ffff`,
- final root status `0x29000001`.

Startup boundary satisfied bits:

```text
0x00000001 root steps satisfied
0x00000002 launch status satisfied
0x00000004 root status satisfied
0x00000008 manifest status satisfied
0x00000010 boot args pointer satisfied
0x00000020 DT pointer satisfied
0x00000040 timebase satisfied
0x00000080 interrupt readiness satisfied
```

Complete startup satisfied mask: `0x000000ff`.

The startup boundary required root-step mask intentionally includes launch-contract completion but excludes final `RESULT`, `RETURN`, and `STARTUP_BOUNDARY` bits while the boundary is being evaluated:

```text
required before startup boundary: 0x00007ff3
final root steps:                 0x0000ffff
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
```

Complete root-step mask: `0x0000ffff`.

## Built image

```bash
./stage29/build.sh
```

Successful local build:

```text
out/stage29/stage29-qcdt.img
sha256=be8c68490dc262382733b7e33393e52b684d25634870c4562066101207091a7c
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=63184 (0xf6d0)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage29 mi4ios6=stage29 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage29_vectors
0000ab68 t stage29_kernel_root
0000e388 T mmu_high_bootstrap_selftest
000105bc T kernel_entry
000108e4 T test_kernel_entry
00010a7c T stage29_main
00024000 b stage29_l1_table
0002a000 B __stage29_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage29/stage29-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2526 KB)                       OKAY [  0.080s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.092s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage29-last_kmsg.txt
```

The recovered log was 37637 bytes and contained:

```text
612 MI4IOS6_STAGE29 markers
586 MI4IOS6_STAGE29_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE29_XNU high root service dispatcher ok
MI4IOS6_STAGE29_XNU high root service table ok
MI4IOS6_STAGE29_XNU high root phase-service dependencies ok
MI4IOS6_STAGE29_XNU high root phase dispatcher ok
MI4IOS6_STAGE29_XNU high root bootstrap registry ok
MI4IOS6_STAGE29_XNU high root phase table ok
MI4IOS6_STAGE29_XNU high init sequence complete
MI4IOS6_STAGE29_XNU high root boot policy ok
MI4IOS6_STAGE29_XNU high root bootstrap manifest ok
MI4IOS6_STAGE29_XNU high root launch contract ok
MI4IOS6_STAGE29_XNU high root startup boundary ok
MI4IOS6_STAGE29_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE29_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE29_XNU high_bootstrap_init_status=0x29000001
```

Registry, policy, manifest, and launch markers:

```text
MI4IOS6_STAGE29_XNU high_registry_version=0x00000001
MI4IOS6_STAGE29_XNU high_registry_size=0x000002e0
MI4IOS6_STAGE29_XNU high_registry_status_checksum=0x000f02e1
MI4IOS6_STAGE29_XNU high_registry_status=0x29000001
MI4IOS6_STAGE29_XNU high_boot_policy_version=0x00000001
MI4IOS6_STAGE29_XNU high_boot_policy_size=0x000002e0
MI4IOS6_STAGE29_XNU high_boot_policy_required_root_steps=0x00000ff3
MI4IOS6_STAGE29_XNU high_boot_policy_observed_root_steps=0x00000ff3
MI4IOS6_STAGE29_XNU high_boot_policy_satisfied_mask=0x0000003f
MI4IOS6_STAGE29_XNU high_boot_policy_status_checksum=0x000002de
MI4IOS6_STAGE29_XNU high_boot_policy_status=0x29000001
MI4IOS6_STAGE29_XNU high_manifest_version=0x00000001
MI4IOS6_STAGE29_XNU high_manifest_record_count=0x00000006
MI4IOS6_STAGE29_XNU high_manifest_required_record_mask=0x0000003f
MI4IOS6_STAGE29_XNU high_manifest_order_mask=0x0000003f
MI4IOS6_STAGE29_XNU high_manifest_satisfied_mask=0x0000003f
MI4IOS6_STAGE29_XNU high_manifest_required_root_steps=0x00001ff3
MI4IOS6_STAGE29_XNU high_manifest_observed_root_steps=0x00001ff3
MI4IOS6_STAGE29_XNU high_manifest_status_checksum=0x00000038
MI4IOS6_STAGE29_XNU high_manifest_status=0x29000001
MI4IOS6_STAGE29_XNU high_launch_contract_version=0x00000001
MI4IOS6_STAGE29_XNU high_launch_contract_size=0x000002e0
MI4IOS6_STAGE29_XNU high_launch_required_root_steps=0x00003ff3
MI4IOS6_STAGE29_XNU high_launch_observed_root_steps=0x00003ff3
MI4IOS6_STAGE29_XNU high_launch_satisfied_mask=0x0000007f
MI4IOS6_STAGE29_XNU high_launch_status_checksum=0x0000029e
MI4IOS6_STAGE29_XNU high_launch_status=0x29000001
```

Startup boundary markers:

```text
MI4IOS6_STAGE29_XNU high_startup_boundary_version=0x00000001
MI4IOS6_STAGE29_XNU high_startup_boundary_size=0x000002e0
MI4IOS6_STAGE29_XNU high_startup_required_root_steps=0x00007ff3
MI4IOS6_STAGE29_XNU high_startup_observed_root_steps=0x00007ff3
MI4IOS6_STAGE29_XNU high_startup_required_launch_status=0x29000001
MI4IOS6_STAGE29_XNU high_startup_observed_launch_status=0x29000001
MI4IOS6_STAGE29_XNU high_startup_required_root_status=0x29000001
MI4IOS6_STAGE29_XNU high_startup_observed_root_status=0x29000001
MI4IOS6_STAGE29_XNU high_startup_required_manifest_status=0x29000001
MI4IOS6_STAGE29_XNU high_startup_observed_manifest_status=0x29000001
MI4IOS6_STAGE29_XNU high_startup_required_boot_args_virt=0xc0028000
MI4IOS6_STAGE29_XNU high_startup_observed_boot_args_virt=0xc0028000
MI4IOS6_STAGE29_XNU high_startup_required_dt_virt=0xc0028140
MI4IOS6_STAGE29_XNU high_startup_observed_dt_virt=0xc0028140
MI4IOS6_STAGE29_XNU high_startup_required_timebase_freq=0x0124f800
MI4IOS6_STAGE29_XNU high_startup_observed_timebase_freq=0x0124f800
MI4IOS6_STAGE29_XNU high_startup_required_interrupt_mask=0x0000000f
MI4IOS6_STAGE29_XNU high_startup_observed_interrupt_mask=0x0000000f
MI4IOS6_STAGE29_XNU high_startup_satisfied_mask=0x000000ff
MI4IOS6_STAGE29_XNU high_startup_status_checksum=0x0000021e
MI4IOS6_STAGE29_XNU high_startup_status=0x29000001
MI4IOS6_STAGE29_XNU high_root_steps=0x0000ffff
MI4IOS6_STAGE29_XNU high_root_status=0x29000001
MI4IOS6_STAGE29_XNU high_bootstrap_status=0x29000001
MI4IOS6_STAGE29_XNU high_bootstrap_checksum=0x2750d455
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE29_XNU mmu_high_bootstrap_result=0x29000001
MI4IOS6_STAGE29_XNU mmu_high_bootstrap_expected_checksum=0x2750d455
MI4IOS6_STAGE29_XNU mmu_high_bootstrap_magic_id=0x29002900
MI4IOS6_STAGE29_XNU mmu_high_bootstrap_root_steps_id=0x0000ffff
MI4IOS6_STAGE29_XNU mmu_high_bootstrap_startup_satisfied_mask_id=0x000000ff
MI4IOS6_STAGE29_XNU mmu_high_bootstrap_startup_status_id=0x29000001
MI4IOS6_STAGE29_XNU mmu_high_bootstrap_status_alias=0x29000001
MI4IOS6_STAGE29_XNU mmu_high_bootstrap_checksum_alias=0x2750d455
MI4IOS6_STAGE29_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the startup-boundary-gated root path:

```text
MI4IOS6_STAGE29_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the startup-boundary-gated root path:

```text
MI4IOS6_STAGE29_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE29_XNU kernel_entry ok
MI4IOS6_STAGE29 kernel_entry returned success
MI4IOS6_STAGE29 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage29 makes the launch contract executable as an explicit startup handoff boundary. The high-root path now has a declared object that consumes the prior launch result and records the facts needed by the next startup layer:

1. launch-contract status,
2. pre-startup root/manifest status,
3. high virtual boot-args pointer,
4. high virtual Apple-DT pointer,
5. 19.2 MHz timebase,
6. interrupt readiness,
7. startup checksum/status,
8. identity/high-alias verification.

This is still a small XNU-adjacent kernel skeleton, but the next stage can now drive a distinct startup routine from a validated handoff object rather than from loose globals.

## Success criteria — met

1. bootloader accepted `stage29-qcdt.img`: yes
2. high virtual root entry ran: yes
3. service dispatcher still completed: yes
4. phase dispatcher still completed: yes
5. bootstrap registry still completed: yes
6. boot policy validation still completed: yes
7. bootstrap manifest still completed: yes
8. launch contract validation still completed: yes
9. startup boundary validation ran: yes
10. startup root-step requirement matched observation (`0x00007ff3`): yes
11. startup launch/root/manifest statuses matched (`0x29000001`): yes
12. startup boot-args pointer matched (`0xc0028000`): yes
13. startup DT pointer matched (`0xc0028140`): yes
14. startup timebase matched (`0x0124f800`): yes
15. startup interrupt mask matched (`0x0000000f`): yes
16. startup satisfied mask was complete (`0x000000ff`): yes
17. startup checksum matched (`0x0000021e`): yes
18. startup status was `0x29000001`: yes
19. full root step mask was complete (`0x0000ffff`): yes
20. high root returned status `0x29000001`: yes
21. checksum matched through identity and alias views: yes
22. SGI and timer IRQ paths still worked after high root: yes
23. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage30 can make the startup boundary drive a minimal startup routine:

- keep identity/recovery mappings and caches disabled,
- keep descriptor-driven service/phase dispatchers plus registry, policy, manifest, launch-contract, and startup-boundary checks,
- introduce a separate XNU-like startup entry that consumes the startup boundary,
- validate startup entry arguments, startup state version, high-root status, boot args, DT, timebase, and interrupt readiness,
- preserve identity-side verification and recovery,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
