# Experiment 41 — Stage38 Kernel Object Table

Date: 2026-06-05

Goal: make the pmap bootstrap workspace drive a minimal kernel object table descriptor.

Stage38 still does **not** run XNU or iOS. It extends Stage37 by adding a versioned kernel object table descriptor after the pmap workspace has been populated and accepted. The table records high-virtual slots for boot args, device tree, PE state, VM plan, VM state, bootstrap allocator, and pmap workspace, then validates object coverage, workspace/L1 facts, allocation tag policy, checksum, and status through identity and high-alias views before final root success.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage38 adds over Stage37

Stage37 proved:

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
- startup-entry-owned descriptor-driven kernel-start callout table,
- startup-entry-populated kernel-start context object,
- VM bootstrap plan object,
- VM bootstrap state object,
- bootstrap allocator descriptor object,
- pmap bootstrap workspace descriptor object,
- pmap workspace required/satisfied mask `0x0000003f`,
- pmap workspace checksum/status `0xce451213` / `0x37000001`,
- root step mask `0x00ffffff`,
- final root status `0x37000001`.

Stage38 adds a kernel object table descriptor object:

- object table version: `1`,
- object table size: `0x0000005c`,
- table required/satisfied mask: `0x0000003f`,
- object count: `7`,
- required/observed object mask: `0x0000007f`,
- accepted pmap workspace status: `0x38000001`,
- accepted pmap workspace checksum: `0xce451213`,
- boot args high pointer: `0xc0034000`,
- device tree high pointer: `0xc0034140`,
- PE state high pointer: `0xc0029020`,
- VM plan high pointer: `0xc002c000`,
- VM state high pointer: `0xc002c04c`,
- allocator high pointer: `0xc002c09c`,
- pmap workspace high pointer: `0xc002c0e4`,
- workspace base/limit: `0x80000000` / `0x80100000`,
- section count: `0x000005e5`,
- L1 table physical/high-alias address: `0x00030000` / `0xc0030000`,
- allocation tag: `0x504d4150` (`PMAP`),
- object table checksum: `0xa61ac796`,
- object table status: `0x38000001`,
- full root step mask extends to `0x01ffffff`,
- final root status `0x38000001`.

Object table satisfied bits:

```text
0x00000001 accepted pmap workspace status/checksum satisfied
0x00000002 required/observed object mask and object count satisfied
0x00000004 high-virtual object pointer slots satisfied
0x00000008 workspace base/limit satisfied
0x00000010 section count and L1 table placement satisfied
0x00000020 allocation tag, MMU/cache policy satisfied
```

Complete object table satisfied mask: `0x0000003f`.

Object mask bits:

```text
0x00000001 boot args
0x00000002 device tree
0x00000004 PE state
0x00000008 VM plan
0x00000010 VM state
0x00000020 bootstrap allocator
0x00000040 pmap workspace
```

Complete object mask: `0x0000007f`.

The new root-step bit records that the kernel object table descriptor was accepted:

```text
Stage37 final root steps: 0x00ffffff
Stage38 final root steps: 0x01ffffff
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
0x00080000 kernel-start context complete
0x00100000 VM bootstrap plan complete
0x00200000 VM bootstrap state complete
0x00400000 bootstrap allocator descriptor complete
0x00800000 pmap bootstrap workspace descriptor complete
0x01000000 kernel object table descriptor complete
```

Complete root-step mask: `0x01ffffff`.

## Built image

```bash
./stage38/build.sh
```

Successful local build:

```text
out/stage38/stage38-qcdt.img
sha256=15fa41eb59797e6008c9896df34e43de74d7a0171b287c3760abc67907fc7406
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=99816 (0x185e8)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage38 mi4ios6=stage38 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage38_vectors
0000ab68 t stage38_kernel_root
0000fefc t stage38_startup_entry
00010ff8 T mmu_high_bootstrap_selftest
00014c08 T kernel_entry
00014f30 T test_kernel_entry
000150c8 T stage38_main
0002c000 b stage38_vm_plan_block
0002c04c b stage38_vm_state_block
0002c09c b stage38_boot_allocator_block
0002c0e4 b stage38_pmap_workspace_block
0002c12c b stage38_kernel_object_table_block
0002c188 b stage38_startup_handoff_block
0002c1b8 b stage38_kernel_context_block
0002c208 b stage38_bootstrap_state_block
00030000 b stage38_l1_table
00036000 B __stage38_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage38/stage38-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2562 KB)                       OKAY [  0.082s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.091s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage38-last_kmsg.txt
```

The recovered log was 68271 bytes and contained:

```text
1064 MI4IOS6_STAGE38 markers
1038 MI4IOS6_STAGE38_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE38_XNU high_pmap_workspace_virt=0xc002c0e4
MI4IOS6_STAGE38_XNU high_object_table_virt=0xc002c12c
MI4IOS6_STAGE38_XNU high root vm bootstrap plan begin
MI4IOS6_STAGE38_XNU high root vm bootstrap plan ok
MI4IOS6_STAGE38_XNU high root vm bootstrap state begin
MI4IOS6_STAGE38_XNU high root vm bootstrap state ok
MI4IOS6_STAGE38_XNU high root bootstrap allocator begin
MI4IOS6_STAGE38_XNU high root bootstrap allocator ok
MI4IOS6_STAGE38_XNU high root pmap workspace begin
MI4IOS6_STAGE38_XNU high root pmap workspace ok
MI4IOS6_STAGE38_XNU high root kernel object table begin
MI4IOS6_STAGE38_XNU high root kernel object table ok
MI4IOS6_STAGE38_XNU high_root_steps=0x01ffffff
MI4IOS6_STAGE38_XNU high_root_status=0x38000001
```

Object table state markers:

```text
MI4IOS6_STAGE38_XNU high_object_table_version=0x00000001
MI4IOS6_STAGE38_XNU high_object_table_size=0x0000005c
MI4IOS6_STAGE38_XNU high_object_table_required_mask=0x0000003f
MI4IOS6_STAGE38_XNU high_object_table_satisfied_mask=0x0000003f
MI4IOS6_STAGE38_XNU high_object_table_checksum=0xa61ac796
MI4IOS6_STAGE38_XNU high_object_table_status=0x38000001
```

Kernel object table descriptor markers:

```text
MI4IOS6_STAGE38_XNU high_objtable_pmap_status=0x38000001
MI4IOS6_STAGE38_XNU high_objtable_pmap_checksum=0xce451213
MI4IOS6_STAGE38_XNU high_objtable_object_count=0x00000007
MI4IOS6_STAGE38_XNU high_objtable_required_object_mask=0x0000007f
MI4IOS6_STAGE38_XNU high_objtable_observed_object_mask=0x0000007f
MI4IOS6_STAGE38_XNU high_objtable_boot_args_virt=0xc0034000
MI4IOS6_STAGE38_XNU high_objtable_device_tree_virt=0xc0034140
MI4IOS6_STAGE38_XNU high_objtable_pe_state_virt=0xc0029020
MI4IOS6_STAGE38_XNU high_objtable_vm_plan_virt=0xc002c000
MI4IOS6_STAGE38_XNU high_objtable_vm_state_virt=0xc002c04c
MI4IOS6_STAGE38_XNU high_objtable_allocator_virt=0xc002c09c
MI4IOS6_STAGE38_XNU high_objtable_pmap_workspace_virt=0xc002c0e4
MI4IOS6_STAGE38_XNU high_objtable_workspace_base=0x80000000
MI4IOS6_STAGE38_XNU high_objtable_workspace_limit=0x80100000
MI4IOS6_STAGE38_XNU high_objtable_section_count=0x000005e5
MI4IOS6_STAGE38_XNU high_objtable_l1_table_phys=0x00030000
MI4IOS6_STAGE38_XNU high_objtable_l1_table_virt=0xc0030000
MI4IOS6_STAGE38_XNU high_objtable_allocation_tag=0x504d4150
MI4IOS6_STAGE38_XNU high_objtable_satisfied_mask=0x0000003f
MI4IOS6_STAGE38_XNU high_objtable_checksum=0xa61ac796
MI4IOS6_STAGE38_XNU high_objtable_status=0x38000001
MI4IOS6_STAGE38_XNU high_bootstrap_checksum=0xe9dd4625
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_object_table_phys=0x0002c12c
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_object_table_virt=0xc002c12c
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_object_table_version_id=0x00000001
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_object_table_size_id=0x0000005c
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_object_table_required_mask_id=0x0000003f
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_object_table_satisfied_mask_id=0x0000003f
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_object_table_checksum_id=0xa61ac796
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_object_table_status_id=0x38000001
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_pmap_status_id=0x38000001
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_pmap_checksum_id=0xce451213
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_object_count_id=0x00000007
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_required_object_mask_id=0x0000007f
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_observed_object_mask_id=0x0000007f
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_boot_args_virt_id=0xc0034000
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_device_tree_virt_id=0xc0034140
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_pe_state_virt_id=0xc0029020
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_vm_plan_virt_id=0xc002c000
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_vm_state_virt_id=0xc002c04c
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_allocator_virt_id=0xc002c09c
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_pmap_workspace_virt_id=0xc002c0e4
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_workspace_base_id=0x80000000
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_workspace_limit_id=0x80100000
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_section_count_id=0x000005e5
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_l1_table_phys_id=0x00030000
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_l1_table_virt_id=0xc0030000
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_allocation_tag_id=0x504d4150
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_satisfied_mask_id=0x0000003f
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_checksum_id=0xa61ac796
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_status_id=0x38000001
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_object_table_status_alias=0x38000001
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_object_table_checksum_alias=0xa61ac796
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_status_alias=0x38000001
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_checksum_alias=0xa61ac796
MI4IOS6_STAGE38_XNU mmu_high_bootstrap_objtable_observed_mask_alias=0x0000007f
MI4IOS6_STAGE38_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the object-table-gated root path:

```text
MI4IOS6_STAGE38_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the object-table-gated root path:

```text
MI4IOS6_STAGE38_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE38_XNU kernel_entry ok
MI4IOS6_STAGE38 kernel_entry returned success
MI4IOS6_STAGE38 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage38 turns the Stage37 pmap workspace into an explicit kernel object table descriptor. It still keeps the actual MMU policy conservative: identity mappings remain, high aliases are still section mappings, and caches remain disabled. The useful step is that later stages now have a typed table of early kernel objects and their high-virtual addresses, plus coverage checks tying boot args, device tree, PE state, VM plan, VM state, allocator, and pmap workspace together.

This is still an XNU-adjacent kernel skeleton, not a booting XNU kernel. The object table descriptor is a staging contract for later kernel collection handoff, early object graph validation, and higher-level kernel bootstrap work.

## Success criteria — met

1. bootloader accepted `stage38-qcdt.img`: yes
2. high virtual root entry ran: yes
3. startup entry was called: yes
4. kernel callout table dispatched: yes
5. kernel context object was populated: yes
6. VM plan object was populated: yes
7. VM state object was populated: yes
8. bootstrap allocator descriptor was populated: yes
9. pmap workspace descriptor was populated: yes
10. kernel object table descriptor was populated: yes
11. object table accepted pmap workspace status/checksum (`0x38000001` / `0xce451213`): yes
12. required/observed object mask matched (`0x0000007f`): yes
13. all seven high-virtual object slots matched: yes
14. workspace base/limit matched (`0x80000000` / `0x80100000`): yes
15. section count matched (`0x000005e5`): yes
16. L1 table addresses matched (`0x00030000` / `0xc0030000`): yes
17. allocation tag matched (`0x504d4150`): yes
18. object table satisfied mask was complete (`0x0000003f`): yes
19. object table checksum matched (`0xa61ac796`): yes
20. object table status was `0x38000001`: yes
21. full root step mask was complete (`0x01ffffff`): yes
22. high root returned status `0x38000001`: yes
23. checksum/status/object mask matched through identity and alias views: yes
24. SGI and timer IRQ paths still worked after high root: yes
25. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage39 can make the kernel object table drive a minimal kernel collection handoff descriptor:

- keep identity/recovery mappings and caches disabled,
- keep existing startup-entry/callout/context/VM-plan/VM-state/allocator/pmap-workspace/object-table checks,
- add a versioned kernel collection handoff object consuming object table coverage and object pointers,
- validate handoff entry count, object mask, boot args/device tree/PE state pointers, VM/pmap object pointers, checksum/status, and identity/high-alias views,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
