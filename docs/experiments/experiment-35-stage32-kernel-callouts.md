# Experiment 35 — Stage32 Startup-Entry Kernel Callouts

Date: 2026-06-05

Goal: make the separate startup entry dispatch a first minimal kernel-start callout table.

Stage32 still does **not** run XNU or iOS. It extends Stage31 by adding a descriptor-driven callout table inside the high-virtual `startup_entry` path. The startup entry consumes the startup handoff, validates the entry contract, dispatches four early kernel-start callouts (bootstrap, platform, timebase, interrupts), records their order/handler masks and service coverage, and returns the callout-table status to the high-root path before final root success.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage32 adds over Stage31

Stage31 proved:

- descriptor-driven service dispatcher,
- descriptor-driven phase dispatcher,
- high-root bootstrap registry,
- registry-gated boot policy,
- descriptor-driven bootstrap manifest,
- manifest-driven launch contract,
- contract-consuming startup boundary,
- startup-boundary-driven startup routine,
- separate high-virtual startup entry,
- explicit startup handoff object,
- startup entry root-step observation `0x0001fff3`,
- startup entry satisfied mask `0x000000ff`,
- startup entry status `0x31000001`,
- root step mask `0x0003ffff`,
- status `0x31000001`.

Stage32 adds a minimal kernel-start callout table:

- callout table version: `1`,
- callout count: `4`,
- callout required mask: `0x0000000f`,
- callout order mask: `0x0000000f`,
- callout handler mask: `0x0000000f`,
- callout required service mask: `0x0000000f`,
- callout observed service mask: `0x0000000f`,
- callout status checksum: `0x00000005`,
- callout statuses: all `0x32000001`,
- callout table status: `0x32000001`,
- startup entry still validates required/observed root steps `0x0001fff3`,
- startup entry satisfied mask remains `0x000000ff`,
- full root step mask extends to `0x0007ffff`,
- final root status `0x32000001`.

Kernel callout IDs:

```text
0 bootstrap callout
1 platform callout
2 timebase callout
3 interrupt-readiness callout
```

Complete callout order/handler mask: `0x0000000f`.

The startup entry still validates the startup handoff before dispatching callouts. The new root-step bit records that the callout table returned a complete status:

```text
startup-entry input root steps: 0x0001fff3
final root steps:              0x0007ffff
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
0x00020000 startup entry complete
0x00040000 kernel callout table complete
```

Complete root-step mask: `0x0007ffff`.

## Built image

```bash
./stage32/build.sh
```

Successful local build:

```text
out/stage32/stage32-qcdt.img
sha256=0352ea289277996dc595fd0c86db2954b93f98fdc70e29251ac6298267147c4c
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=74432 (0x122c0)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage32 mi4ios6=stage32 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage32_vectors
0000ab68 t stage32_kernel_root
0000e214 t stage32_startup_entry
0000f114 T mmu_high_bootstrap_selftest
00011874 T kernel_entry
00011b9c T test_kernel_entry
00011d34 T stage32_main
00024000 b stage32_startup_handoff_block
00024048 b stage32_bootstrap_state_block
00028000 b stage32_l1_table
0002e000 B __stage32_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage32/stage32-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2538 KB)                       OKAY [  0.081s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.091s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage32-last_kmsg.txt
```

The recovered log was 47632 bytes and contained:

```text
751 MI4IOS6_STAGE32 markers
725 MI4IOS6_STAGE32_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE32_XNU high root service dispatcher ok
MI4IOS6_STAGE32_XNU high root service table ok
MI4IOS6_STAGE32_XNU high root phase-service dependencies ok
MI4IOS6_STAGE32_XNU high root phase dispatcher ok
MI4IOS6_STAGE32_XNU high root bootstrap registry ok
MI4IOS6_STAGE32_XNU high root phase table ok
MI4IOS6_STAGE32_XNU high init sequence complete
MI4IOS6_STAGE32_XNU high root boot policy ok
MI4IOS6_STAGE32_XNU high root bootstrap manifest ok
MI4IOS6_STAGE32_XNU high root launch contract ok
MI4IOS6_STAGE32_XNU high root startup boundary ok
MI4IOS6_STAGE32_XNU high root startup routine ok
MI4IOS6_STAGE32_XNU high root startup handoff ok
MI4IOS6_STAGE32_XNU high virtual startup_entry ok
MI4IOS6_STAGE32_XNU high startup_entry kernel callout table ok
MI4IOS6_STAGE32_XNU high root startup entry ok
MI4IOS6_STAGE32_XNU high root kernel callout table ok
MI4IOS6_STAGE32_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE32_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE32_XNU high_bootstrap_init_status=0x32000001
```

Registry, policy, manifest, launch, boundary, routine, and entry markers:

```text
MI4IOS6_STAGE32_XNU high_registry_version=0x00000001
MI4IOS6_STAGE32_XNU high_registry_size=0x000003dc
MI4IOS6_STAGE32_XNU high_registry_status_checksum=0x000f03dd
MI4IOS6_STAGE32_XNU high_registry_status=0x32000001
MI4IOS6_STAGE32_XNU high_boot_policy_version=0x00000001
MI4IOS6_STAGE32_XNU high_boot_policy_size=0x000003dc
MI4IOS6_STAGE32_XNU high_boot_policy_required_root_steps=0x00000ff3
MI4IOS6_STAGE32_XNU high_boot_policy_observed_root_steps=0x00000ff3
MI4IOS6_STAGE32_XNU high_boot_policy_satisfied_mask=0x0000003f
MI4IOS6_STAGE32_XNU high_boot_policy_status_checksum=0x000003e2
MI4IOS6_STAGE32_XNU high_boot_policy_status=0x32000001
MI4IOS6_STAGE32_XNU high_manifest_version=0x00000001
MI4IOS6_STAGE32_XNU high_manifest_record_count=0x00000006
MI4IOS6_STAGE32_XNU high_manifest_required_record_mask=0x0000003f
MI4IOS6_STAGE32_XNU high_manifest_order_mask=0x0000003f
MI4IOS6_STAGE32_XNU high_manifest_satisfied_mask=0x0000003f
MI4IOS6_STAGE32_XNU high_manifest_required_root_steps=0x00001ff3
MI4IOS6_STAGE32_XNU high_manifest_observed_root_steps=0x00001ff3
MI4IOS6_STAGE32_XNU high_manifest_status_checksum=0x00000038
MI4IOS6_STAGE32_XNU high_manifest_status=0x32000001
MI4IOS6_STAGE32_XNU high_launch_contract_version=0x00000001
MI4IOS6_STAGE32_XNU high_launch_contract_size=0x000003dc
MI4IOS6_STAGE32_XNU high_launch_required_root_steps=0x00003ff3
MI4IOS6_STAGE32_XNU high_launch_observed_root_steps=0x00003ff3
MI4IOS6_STAGE32_XNU high_launch_satisfied_mask=0x0000007f
MI4IOS6_STAGE32_XNU high_launch_status_checksum=0x000003a2
MI4IOS6_STAGE32_XNU high_launch_status=0x32000001
MI4IOS6_STAGE32_XNU high_startup_boundary_version=0x00000001
MI4IOS6_STAGE32_XNU high_startup_boundary_size=0x000003dc
MI4IOS6_STAGE32_XNU high_startup_required_root_steps=0x00007ff3
MI4IOS6_STAGE32_XNU high_startup_observed_root_steps=0x00007ff3
MI4IOS6_STAGE32_XNU high_startup_satisfied_mask=0x000000ff
MI4IOS6_STAGE32_XNU high_startup_status_checksum=0x00000322
MI4IOS6_STAGE32_XNU high_startup_status=0x32000001
MI4IOS6_STAGE32_XNU high_startup_routine_version=0x00000001
MI4IOS6_STAGE32_XNU high_startup_routine_size=0x000003dc
MI4IOS6_STAGE32_XNU high_startup_routine_required_root_steps=0x0000fff3
MI4IOS6_STAGE32_XNU high_startup_routine_observed_root_steps=0x0000fff3
MI4IOS6_STAGE32_XNU high_startup_routine_satisfied_mask=0x000000ff
MI4IOS6_STAGE32_XNU high_startup_routine_status_checksum=0x00000322
MI4IOS6_STAGE32_XNU high_startup_routine_status=0x32000001
MI4IOS6_STAGE32_XNU high_startup_handoff_version=0x00000001
MI4IOS6_STAGE32_XNU high_startup_handoff_size=0x00000030
MI4IOS6_STAGE32_XNU high_startup_handoff_root_steps=0x0001fff3
MI4IOS6_STAGE32_XNU high_startup_handoff_checksum=0x3325068c
MI4IOS6_STAGE32_XNU high_startup_handoff_status=0x32000001
MI4IOS6_STAGE32_XNU high_startup_entry_version=0x00000001
MI4IOS6_STAGE32_XNU high_startup_entry_size=0x000003dc
MI4IOS6_STAGE32_XNU high_startup_entry_required_root_steps=0x0001fff3
MI4IOS6_STAGE32_XNU high_startup_entry_observed_root_steps=0x0001fff3
MI4IOS6_STAGE32_XNU high_startup_entry_satisfied_mask=0x000000ff
MI4IOS6_STAGE32_XNU high_startup_entry_status_checksum=0x00000322
MI4IOS6_STAGE32_XNU high_startup_entry_status=0x32000001
```

Kernel callout markers:

```text
MI4IOS6_STAGE32_XNU high_kernel_callout_version=0x00000001
MI4IOS6_STAGE32_XNU high_kernel_callout_count=0x00000004
MI4IOS6_STAGE32_XNU high_kernel_callout_required_mask=0x0000000f
MI4IOS6_STAGE32_XNU high_kernel_callout_order_mask=0x0000000f
MI4IOS6_STAGE32_XNU high_kernel_callout_handler_mask=0x0000000f
MI4IOS6_STAGE32_XNU high_kernel_callout_required_service_mask=0x0000000f
MI4IOS6_STAGE32_XNU high_kernel_callout_observed_service_mask=0x0000000f
MI4IOS6_STAGE32_XNU high_kernel_callout_status_checksum=0x00000005
MI4IOS6_STAGE32_XNU high_kernel_callout0_status=0x32000001
MI4IOS6_STAGE32_XNU high_kernel_callout1_status=0x32000001
MI4IOS6_STAGE32_XNU high_kernel_callout2_status=0x32000001
MI4IOS6_STAGE32_XNU high_kernel_callout3_status=0x32000001
MI4IOS6_STAGE32_XNU high_kernel_callout_status=0x32000001
MI4IOS6_STAGE32_XNU high_root_steps=0x0007ffff
MI4IOS6_STAGE32_XNU high_root_status=0x32000001
MI4IOS6_STAGE32_XNU high_bootstrap_status=0x32000001
MI4IOS6_STAGE32_XNU high_bootstrap_checksum=0x1557ce6f
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE32_XNU mmu_high_bootstrap_result=0x32000001
MI4IOS6_STAGE32_XNU mmu_high_bootstrap_expected_checksum=0x1557ce6f
MI4IOS6_STAGE32_XNU mmu_high_bootstrap_magic_id=0x32003200
MI4IOS6_STAGE32_XNU mmu_high_bootstrap_root_steps_id=0x0007ffff
MI4IOS6_STAGE32_XNU mmu_high_bootstrap_startup_entry_satisfied_mask_id=0x000000ff
MI4IOS6_STAGE32_XNU mmu_high_bootstrap_startup_entry_status_id=0x32000001
MI4IOS6_STAGE32_XNU mmu_high_bootstrap_kernel_callout_order_mask_id=0x0000000f
MI4IOS6_STAGE32_XNU mmu_high_bootstrap_kernel_callout_handler_mask_id=0x0000000f
MI4IOS6_STAGE32_XNU mmu_high_bootstrap_kernel_callout_observed_service_mask_id=0x0000000f
MI4IOS6_STAGE32_XNU mmu_high_bootstrap_kernel_callout_checksum_id=0x00000005
MI4IOS6_STAGE32_XNU mmu_high_bootstrap_kernel_callout_status_id=0x32000001
MI4IOS6_STAGE32_XNU mmu_high_bootstrap_status_alias=0x32000001
MI4IOS6_STAGE32_XNU mmu_high_bootstrap_checksum_alias=0x1557ce6f
MI4IOS6_STAGE32_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the startup-entry/callout-gated root path:

```text
MI4IOS6_STAGE32_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the startup-entry/callout-gated root path:

```text
MI4IOS6_STAGE32_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE32_XNU kernel_entry ok
MI4IOS6_STAGE32 kernel_entry returned success
MI4IOS6_STAGE32 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage32 makes the startup entry do more than validate its handoff: it now owns a small descriptor-driven early kernel-start callout table. The callouts are still simple validation handlers, but they establish the next layer of structure:

1. high-root validates launch/startup/routine state,
2. high-root builds a startup handoff,
3. startup entry validates that handoff,
4. startup entry dispatches kernel-start callouts,
5. high-root gates final success on the callout-table status.

This is still an XNU-adjacent kernel skeleton, not a booting XNU kernel. The next useful step is to make these callouts populate a real context object that later stages can pass to scheduler/VM/Mach-like initialization stubs.

## Success criteria — met

1. bootloader accepted `stage32-qcdt.img`: yes
2. high virtual root entry ran: yes
3. service dispatcher still completed: yes
4. phase dispatcher still completed: yes
5. bootstrap registry still completed: yes
6. boot policy validation still completed: yes
7. bootstrap manifest still completed: yes
8. launch contract validation still completed: yes
9. startup boundary validation still completed: yes
10. startup routine validation still completed: yes
11. startup handoff validation still completed: yes
12. separate high-virtual startup entry was called: yes
13. startup entry contract validation still completed: yes
14. kernel callout table dispatched: yes
15. callout order mask matched (`0x0000000f`): yes
16. callout handler mask matched (`0x0000000f`): yes
17. callout service mask matched (`0x0000000f`): yes
18. all callout statuses were `0x32000001`: yes
19. callout checksum matched (`0x00000005`): yes
20. callout table status was `0x32000001`: yes
21. full root step mask was complete (`0x0007ffff`): yes
22. high root returned status `0x32000001`: yes
23. checksum matched through identity and alias views: yes
24. SGI and timer IRQ paths still worked after high root: yes
25. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage33 can make the startup-entry callouts populate a minimal kernel-start context object:

- keep identity/recovery mappings and caches disabled,
- keep descriptor-driven service/phase dispatchers plus registry, policy, manifest, launch-contract, startup-boundary, startup-routine, startup-entry, and callout-table checks,
- add a versioned context object containing validated boot args, DT, platform, timebase, interrupt, and callout status facts,
- validate context checksum/status through identity and high-alias views,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
