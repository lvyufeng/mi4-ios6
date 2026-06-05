# Experiment 34 — Stage31 High-Virtual Startup Entry

Date: 2026-06-05

Goal: split the startup routine into a separately called high-virtual startup entry.

Stage31 still does **not** run XNU or iOS. It extends Stage30 by adding an explicit startup handoff object and a separate high-virtual `startup_entry` function. The high-root path validates the existing startup routine, prepares a handoff object, calls the startup entry through the high alias, and then verifies the entry status through both identity and high-alias views before final root success.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage31 adds over Stage30

Stage30 proved:

- descriptor-driven service dispatcher,
- descriptor-driven phase dispatcher,
- high-root bootstrap registry,
- registry-gated boot policy,
- descriptor-driven bootstrap manifest,
- manifest-driven launch contract,
- contract-consuming startup boundary,
- startup-boundary-driven startup routine,
- startup routine root-step observation `0x0000fff3`,
- startup routine satisfied mask `0x000000ff`,
- startup routine status `0x30000001`,
- root step mask `0x0001ffff`,
- status `0x30000001`.

Stage31 adds:

- separate high-virtual `startup_entry` function at symbol `0x0000ab68`,
- explicit startup handoff object at identity address `0x00024000`,
- handoff version: `1`,
- handoff size: `0x00000030`,
- handoff root-step input: `0x0001fff3`,
- handoff routine/startup/root statuses: `0x31000001`,
- handoff boot args pointer: `0xc002c000`,
- handoff DT pointer: `0xc002c140`,
- handoff timebase frequency: `0x0124f800` (`19.2 MHz`),
- handoff interrupt-readiness mask: `0x0000000f`,
- handoff checksum: `0x3025068c`,
- handoff status: `0x31000001`,
- startup entry version: `1`,
- startup entry size: `0x00000388`,
- startup entry required root-step mask: `0x0001fff3`,
- startup entry observed root-step mask: `0x0001fff3`,
- required/observed routine status: `0x31000001`,
- required/observed startup status: `0x31000001`,
- required/observed root status: `0x31000001`,
- required/observed boot args pointer: `0xc002c000`,
- required/observed DT pointer: `0xc002c140`,
- required/observed timebase frequency: `0x0124f800`,
- required/observed interrupt-readiness mask: `0x0000000f`,
- startup entry satisfied mask: `0x000000ff`,
- startup entry checksum: `0x00000376`,
- startup entry status: `0x31000001`,
- full root step mask extends to `0x0003ffff`,
- final root status `0x31000001`.

Startup entry satisfied bits:

```text
0x00000001 root steps satisfied
0x00000002 startup routine status satisfied
0x00000004 startup-boundary status satisfied
0x00000008 root status satisfied
0x00000010 boot args pointer satisfied
0x00000020 DT pointer satisfied
0x00000040 timebase satisfied
0x00000080 interrupt readiness satisfied
```

Complete startup entry satisfied mask: `0x000000ff`.

The startup entry required root-step mask intentionally includes startup-routine completion but excludes final `RESULT`, `RETURN`, and `STARTUP_ENTRY` bits while the entry is being evaluated:

```text
required before startup entry: 0x0001fff3
final root steps:             0x0003ffff
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
```

Complete root-step mask: `0x0003ffff`.

## Built image

```bash
./stage31/build.sh
```

Successful local build:

```text
out/stage31/stage31-qcdt.img
sha256=4687ff19376ce6d799f18ea934ae8367753e6629d445aa34524b0b95532d037a
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=75260 (0x125fc)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage31 mi4ios6=stage31 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage31_vectors
0000ab68 t stage31_startup_entry
0000afcc t stage31_kernel_root
0000f3ec T mmu_high_bootstrap_selftest
00012030 T kernel_entry
00012358 T test_kernel_entry
000124f0 T stage31_main
00024000 b stage31_startup_handoff_block
00024048 b stage31_bootstrap_state_block
00028000 b stage31_l1_table
0002e000 B __stage31_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage31/stage31-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2538 KB)                       OKAY [  0.081s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.091s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage31-last_kmsg.txt
```

The recovered log was 45849 bytes and contained:

```text
725 MI4IOS6_STAGE31 markers
699 MI4IOS6_STAGE31_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE31_XNU high root service dispatcher ok
MI4IOS6_STAGE31_XNU high root service table ok
MI4IOS6_STAGE31_XNU high root phase-service dependencies ok
MI4IOS6_STAGE31_XNU high root phase dispatcher ok
MI4IOS6_STAGE31_XNU high root bootstrap registry ok
MI4IOS6_STAGE31_XNU high root phase table ok
MI4IOS6_STAGE31_XNU high init sequence complete
MI4IOS6_STAGE31_XNU high root boot policy ok
MI4IOS6_STAGE31_XNU high root bootstrap manifest ok
MI4IOS6_STAGE31_XNU high root launch contract ok
MI4IOS6_STAGE31_XNU high root startup boundary ok
MI4IOS6_STAGE31_XNU high root startup routine ok
MI4IOS6_STAGE31_XNU high root startup handoff ok
MI4IOS6_STAGE31_XNU high virtual startup_entry ok
MI4IOS6_STAGE31_XNU high root startup entry ok
MI4IOS6_STAGE31_XNU high_bootstrap_validation_mask=0x00000000
MI4IOS6_STAGE31_XNU high_bootstrap_init_steps=0x0000000f
MI4IOS6_STAGE31_XNU high_bootstrap_init_status=0x31000001
```

Registry, policy, manifest, launch, boundary, and routine markers:

```text
MI4IOS6_STAGE31_XNU high_registry_version=0x00000001
MI4IOS6_STAGE31_XNU high_registry_size=0x00000388
MI4IOS6_STAGE31_XNU high_registry_status_checksum=0x000f0389
MI4IOS6_STAGE31_XNU high_registry_status=0x31000001
MI4IOS6_STAGE31_XNU high_boot_policy_version=0x00000001
MI4IOS6_STAGE31_XNU high_boot_policy_size=0x00000388
MI4IOS6_STAGE31_XNU high_boot_policy_required_root_steps=0x00000ff3
MI4IOS6_STAGE31_XNU high_boot_policy_observed_root_steps=0x00000ff3
MI4IOS6_STAGE31_XNU high_boot_policy_satisfied_mask=0x0000003f
MI4IOS6_STAGE31_XNU high_boot_policy_status_checksum=0x000003b6
MI4IOS6_STAGE31_XNU high_boot_policy_status=0x31000001
MI4IOS6_STAGE31_XNU high_manifest_version=0x00000001
MI4IOS6_STAGE31_XNU high_manifest_record_count=0x00000006
MI4IOS6_STAGE31_XNU high_manifest_required_record_mask=0x0000003f
MI4IOS6_STAGE31_XNU high_manifest_order_mask=0x0000003f
MI4IOS6_STAGE31_XNU high_manifest_satisfied_mask=0x0000003f
MI4IOS6_STAGE31_XNU high_manifest_required_root_steps=0x00001ff3
MI4IOS6_STAGE31_XNU high_manifest_observed_root_steps=0x00001ff3
MI4IOS6_STAGE31_XNU high_manifest_status_checksum=0x00000038
MI4IOS6_STAGE31_XNU high_manifest_status=0x31000001
MI4IOS6_STAGE31_XNU high_launch_contract_version=0x00000001
MI4IOS6_STAGE31_XNU high_launch_contract_size=0x00000388
MI4IOS6_STAGE31_XNU high_launch_required_root_steps=0x00003ff3
MI4IOS6_STAGE31_XNU high_launch_observed_root_steps=0x00003ff3
MI4IOS6_STAGE31_XNU high_launch_satisfied_mask=0x0000007f
MI4IOS6_STAGE31_XNU high_launch_status_checksum=0x000003f6
MI4IOS6_STAGE31_XNU high_launch_status=0x31000001
MI4IOS6_STAGE31_XNU high_startup_boundary_version=0x00000001
MI4IOS6_STAGE31_XNU high_startup_boundary_size=0x00000388
MI4IOS6_STAGE31_XNU high_startup_required_root_steps=0x00007ff3
MI4IOS6_STAGE31_XNU high_startup_observed_root_steps=0x00007ff3
MI4IOS6_STAGE31_XNU high_startup_satisfied_mask=0x000000ff
MI4IOS6_STAGE31_XNU high_startup_status_checksum=0x00000376
MI4IOS6_STAGE31_XNU high_startup_status=0x31000001
MI4IOS6_STAGE31_XNU high_startup_routine_version=0x00000001
MI4IOS6_STAGE31_XNU high_startup_routine_size=0x00000388
MI4IOS6_STAGE31_XNU high_startup_routine_required_root_steps=0x0000fff3
MI4IOS6_STAGE31_XNU high_startup_routine_observed_root_steps=0x0000fff3
MI4IOS6_STAGE31_XNU high_startup_routine_satisfied_mask=0x000000ff
MI4IOS6_STAGE31_XNU high_startup_routine_status_checksum=0x00000376
MI4IOS6_STAGE31_XNU high_startup_routine_status=0x31000001
```

Startup handoff markers:

```text
MI4IOS6_STAGE31_XNU high_startup_handoff_version=0x00000001
MI4IOS6_STAGE31_XNU high_startup_handoff_size=0x00000030
MI4IOS6_STAGE31_XNU high_startup_handoff_root_steps=0x0001fff3
MI4IOS6_STAGE31_XNU high_startup_handoff_routine_status=0x31000001
MI4IOS6_STAGE31_XNU high_startup_handoff_startup_status=0x31000001
MI4IOS6_STAGE31_XNU high_startup_handoff_root_status=0x31000001
MI4IOS6_STAGE31_XNU high_startup_handoff_boot_args_virt=0xc002c000
MI4IOS6_STAGE31_XNU high_startup_handoff_dt_virt=0xc002c140
MI4IOS6_STAGE31_XNU high_startup_handoff_timebase_freq=0x0124f800
MI4IOS6_STAGE31_XNU high_startup_handoff_interrupt_mask=0x0000000f
MI4IOS6_STAGE31_XNU high_startup_handoff_checksum=0x3025068c
MI4IOS6_STAGE31_XNU high_startup_handoff_status=0x31000001
```

Startup entry markers:

```text
MI4IOS6_STAGE31_XNU high_startup_entry_version=0x00000001
MI4IOS6_STAGE31_XNU high_startup_entry_size=0x00000388
MI4IOS6_STAGE31_XNU high_startup_entry_required_root_steps=0x0001fff3
MI4IOS6_STAGE31_XNU high_startup_entry_observed_root_steps=0x0001fff3
MI4IOS6_STAGE31_XNU high_startup_entry_required_routine_status=0x31000001
MI4IOS6_STAGE31_XNU high_startup_entry_observed_routine_status=0x31000001
MI4IOS6_STAGE31_XNU high_startup_entry_required_startup_status=0x31000001
MI4IOS6_STAGE31_XNU high_startup_entry_observed_startup_status=0x31000001
MI4IOS6_STAGE31_XNU high_startup_entry_required_root_status=0x31000001
MI4IOS6_STAGE31_XNU high_startup_entry_observed_root_status=0x31000001
MI4IOS6_STAGE31_XNU high_startup_entry_required_boot_args_virt=0xc002c000
MI4IOS6_STAGE31_XNU high_startup_entry_observed_boot_args_virt=0xc002c000
MI4IOS6_STAGE31_XNU high_startup_entry_required_dt_virt=0xc002c140
MI4IOS6_STAGE31_XNU high_startup_entry_observed_dt_virt=0xc002c140
MI4IOS6_STAGE31_XNU high_startup_entry_required_timebase_freq=0x0124f800
MI4IOS6_STAGE31_XNU high_startup_entry_observed_timebase_freq=0x0124f800
MI4IOS6_STAGE31_XNU high_startup_entry_required_interrupt_mask=0x0000000f
MI4IOS6_STAGE31_XNU high_startup_entry_observed_interrupt_mask=0x0000000f
MI4IOS6_STAGE31_XNU high_startup_entry_satisfied_mask=0x000000ff
MI4IOS6_STAGE31_XNU high_startup_entry_status_checksum=0x00000376
MI4IOS6_STAGE31_XNU high_startup_entry_status=0x31000001
MI4IOS6_STAGE31_XNU high_root_steps=0x0003ffff
MI4IOS6_STAGE31_XNU high_root_status=0x31000001
MI4IOS6_STAGE31_XNU high_bootstrap_status=0x31000001
MI4IOS6_STAGE31_XNU high_bootstrap_checksum=0x2753cd39
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE31_XNU mmu_high_bootstrap_result=0x31000001
MI4IOS6_STAGE31_XNU mmu_high_bootstrap_expected_checksum=0x2753cd39
MI4IOS6_STAGE31_XNU mmu_high_bootstrap_magic_id=0x31003100
MI4IOS6_STAGE31_XNU mmu_high_bootstrap_root_steps_id=0x0003ffff
MI4IOS6_STAGE31_XNU mmu_high_bootstrap_startup_handoff_checksum_id=0x3025068c
MI4IOS6_STAGE31_XNU mmu_high_bootstrap_startup_handoff_status_id=0x31000001
MI4IOS6_STAGE31_XNU mmu_high_bootstrap_startup_entry_satisfied_mask_id=0x000000ff
MI4IOS6_STAGE31_XNU mmu_high_bootstrap_startup_entry_status_id=0x31000001
MI4IOS6_STAGE31_XNU mmu_high_bootstrap_status_alias=0x31000001
MI4IOS6_STAGE31_XNU mmu_high_bootstrap_checksum_alias=0x2753cd39
MI4IOS6_STAGE31_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the startup-entry-gated root path:

```text
MI4IOS6_STAGE31_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the startup-entry-gated root path:

```text
MI4IOS6_STAGE31_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE31_XNU kernel_entry ok
MI4IOS6_STAGE31 kernel_entry returned success
MI4IOS6_STAGE31 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage31 moves one step closer to a real kernel startup boundary by separating the startup routine from the function that consumes the final startup handoff. The high-root path now has three distinct startup-adjacent layers:

1. startup boundary consumes the launch contract,
2. startup routine validates the boundary state,
3. startup entry consumes a compact handoff object through an independent high-virtual call.

The startup entry still only validates declared state and returns a structured status. It does not create tasks, run a scheduler, load Mach-O images, initialize IOKit, or boot Apple userspace. The important proof is that a separate high-virtual startup function can be called safely with explicit arguments, can validate boot args/DT/timebase/interrupt readiness, and can be checked through identity/high-alias views afterward.

## Success criteria — met

1. bootloader accepted `stage31-qcdt.img`: yes
2. high virtual root entry ran: yes
3. service dispatcher still completed: yes
4. phase dispatcher still completed: yes
5. bootstrap registry still completed: yes
6. boot policy validation still completed: yes
7. bootstrap manifest still completed: yes
8. launch contract validation still completed: yes
9. startup boundary validation still completed: yes
10. startup routine validation still completed: yes
11. startup handoff object was built: yes
12. startup handoff checksum matched (`0x3025068c`): yes
13. separate high-virtual startup entry was called: yes
14. startup entry root-step requirement matched observation (`0x0001fff3`): yes
15. startup entry routine/startup/root statuses matched (`0x31000001`): yes
16. startup entry boot-args pointer matched (`0xc002c000`): yes
17. startup entry DT pointer matched (`0xc002c140`): yes
18. startup entry timebase matched (`0x0124f800`): yes
19. startup entry interrupt mask matched (`0x0000000f`): yes
20. startup entry satisfied mask was complete (`0x000000ff`): yes
21. startup entry checksum matched (`0x00000376`): yes
22. startup entry status was `0x31000001`: yes
23. full root step mask was complete (`0x0003ffff`): yes
24. high root returned status `0x31000001`: yes
25. checksum matched through identity and alias views: yes
26. SGI and timer IRQ paths still worked after high root: yes
27. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage32 can make the separate startup entry dispatch a first minimal kernel-start callout table:

- keep identity/recovery mappings and caches disabled,
- keep descriptor-driven service/phase dispatchers plus registry, policy, manifest, launch-contract, startup-boundary, startup-routine, and startup-entry checks,
- introduce a small descriptor table inside/after startup entry for early kernel callouts,
- validate callout order, required service mask, handler return values, and entry status,
- preserve identity-side verification and recovery,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
