# Experiment 43 — Stage40 Kernel Collection Entry Table

Date: 2026-06-05

Goal: make the kernel collection handoff drive a minimal kernel collection entry-table descriptor.

Stage40 still does **not** run XNU or iOS. It extends Stage39 by adding a versioned kernel collection entry-table descriptor after the kernel collection handoff has been populated and accepted. The entry table consumes the seven handoff object slots for boot args, device tree, PE state, VM plan, VM state, bootstrap allocator, and pmap workspace, then validates entry count, object masks, entry ordering, boot/platform/VM/pmap pointer classes, workspace/L1 facts, checksum, and status through identity and high-alias views before final root success.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage40 adds over Stage39

Stage39 proved:

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
- kernel object table descriptor object,
- kernel collection handoff descriptor object,
- collection handoff required/satisfied mask `0x0000003f`,
- handoff entry/object mask `0x00000007` / `0x0000007f`,
- object table checksum/status `0xa71ac796` / `0x39000001`,
- collection handoff checksum/status `0xce451213` / `0x39000001`,
- root step mask `0x03ffffff`,
- final root status `0x39000001`.

Stage40 adds a kernel collection entry-table descriptor object:

- collection entry-table version: `1`,
- collection entry-table size: `0x00000088`,
- entry-table required/satisfied mask: `0x0000007f`,
- accepted handoff status: `0x40000001`,
- accepted handoff checksum: `0xce451213`,
- entry count: `7`,
- required/observed object mask: `0x0000007f`,
- required/observed entry-order mask: `0x0000007f`,
- required/observed class mask: `0x0000000f`,
- entry object sequence: `0x00000001`, `0x00000002`, `0x00000004`, `0x00000008`, `0x00000010`, `0x00000020`, `0x00000040`,
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
- collection entry-table checksum: `0xde1ac77d`,
- collection entry-table status: `0x40000001`,
- full root step mask extends to `0x07ffffff`,
- final root status `0x40000001`.

Collection entry-table satisfied bits:

```text
0x00000001 accepted handoff status/checksum satisfied
0x00000002 entry count and entry ordering satisfied
0x00000004 object and pointer-class coverage satisfied
0x00000008 boot args, device tree, and PE state high pointers satisfied
0x00000010 VM plan/state high pointers and statuses satisfied
0x00000020 allocator/pmap workspace high pointers and statuses satisfied
0x00000040 workspace range, section count, L1 table, and allocation tag satisfied
```

Complete collection entry-table satisfied mask: `0x0000007f`.

The new root-step bit records that the kernel collection entry-table descriptor was accepted:

```text
Stage39 final root steps: 0x03ffffff
Stage40 final root steps: 0x07ffffff
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
0x02000000 kernel collection handoff descriptor complete
0x04000000 kernel collection entry-table descriptor complete
```

Complete root-step mask: `0x07ffffff`.

## Built image

```bash
./stage40/build.sh
```

Successful local build:

```text
out/stage40/stage40-qcdt.img
sha256=72cc90c6119e4926430d5fe578ebfaa7dfef79e758866bd20072a652af968307
```

Build hashes:

```text
90829ec93fbb18d08fb3193c1cde9918c2ca4aecd887431e335d07a95013a484  out/stage40/stage40.elf
c42308992d8b920090857ded5f5e9c722de44b9978dcd3dd3e05670d43bc6cac  out/stage40/stage40.bin
90cfefeebb2b06c63f28628c55ace0ec53898c23ce27e025c1ec99fa11dd9863  out/stage40/stage40.img
72cc90c6119e4926430d5fe578ebfaa7dfef79e758866bd20072a652af968307  out/stage40/stage40-qcdt.img
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=113556 (0x1bb94)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage40 mi4ios6=stage40 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage40_vectors
0000ab68 t stage40_kernel_root
00010ef8 t stage40_startup_entry
00011ff4 T mmu_high_bootstrap_selftest
00016630 T kernel_entry
00016958 T test_kernel_entry
00016af0 T stage40_main
0002c000 b stage40_vm_plan_block
0002c04c b stage40_vm_state_block
0002c09c b stage40_boot_allocator_block
0002c0e4 b stage40_pmap_workspace_block
0002c12c b stage40_kernel_object_table_block
0002c188 b stage40_kernel_collection_handoff_block
0002c1e4 b stage40_kernel_collection_entry_table_block
0002c26c b stage40_startup_handoff_block
0002c29c b stage40_kernel_context_block
0002c2ec b stage40_bootstrap_state_block
00030000 b stage40_l1_table
00036000 B __stage40_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage40/stage40-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2576 KB)                       OKAY [  0.082s]
Booting                                            OKAY [  0.006s]
Finished. Total time: 0.093s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage40-last_kmsg.txt
```

The recovered log was 79510 bytes and contained:

```text
1216 MI4IOS6_STAGE40 markers
1190 MI4IOS6_STAGE40_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE40_XNU high_pmap_workspace_virt=0xc002c0e4
MI4IOS6_STAGE40_XNU high_object_table_virt=0xc002c12c
MI4IOS6_STAGE40_XNU high_collection_handoff_virt=0xc002c188
MI4IOS6_STAGE40_XNU high_collection_entry_table_virt=0xc002c1e4
MI4IOS6_STAGE40_XNU high root vm bootstrap plan begin
MI4IOS6_STAGE40_XNU high root vm bootstrap plan ok
MI4IOS6_STAGE40_XNU high root vm bootstrap state begin
MI4IOS6_STAGE40_XNU high root vm bootstrap state ok
MI4IOS6_STAGE40_XNU high root bootstrap allocator begin
MI4IOS6_STAGE40_XNU high root bootstrap allocator ok
MI4IOS6_STAGE40_XNU high root pmap workspace begin
MI4IOS6_STAGE40_XNU high root pmap workspace ok
MI4IOS6_STAGE40_XNU high root kernel object table begin
MI4IOS6_STAGE40_XNU high root kernel object table ok
MI4IOS6_STAGE40_XNU high root kernel collection handoff begin
MI4IOS6_STAGE40_XNU high root kernel collection handoff ok
MI4IOS6_STAGE40_XNU high root kernel collection entry table begin
MI4IOS6_STAGE40_XNU high root kernel collection entry table ok
MI4IOS6_STAGE40_XNU high_root_steps=0x07ffffff
MI4IOS6_STAGE40_XNU high_root_status=0x40000001
```

Object table, collection handoff, and entry-table state markers:

```text
MI4IOS6_STAGE40_XNU high_object_table_version=0x00000001
MI4IOS6_STAGE40_XNU high_object_table_size=0x0000005c
MI4IOS6_STAGE40_XNU high_object_table_required_mask=0x0000003f
MI4IOS6_STAGE40_XNU high_object_table_satisfied_mask=0x0000003f
MI4IOS6_STAGE40_XNU high_object_table_checksum=0xde1ac796
MI4IOS6_STAGE40_XNU high_object_table_status=0x40000001
MI4IOS6_STAGE40_XNU high_collection_handoff_version=0x00000001
MI4IOS6_STAGE40_XNU high_collection_handoff_size=0x0000005c
MI4IOS6_STAGE40_XNU high_collection_handoff_required_mask=0x0000003f
MI4IOS6_STAGE40_XNU high_collection_handoff_satisfied_mask=0x0000003f
MI4IOS6_STAGE40_XNU high_collection_handoff_checksum=0xce451213
MI4IOS6_STAGE40_XNU high_collection_handoff_status=0x40000001
MI4IOS6_STAGE40_XNU high_collection_entry_table_version=0x00000001
MI4IOS6_STAGE40_XNU high_collection_entry_table_size=0x00000088
MI4IOS6_STAGE40_XNU high_collection_entry_table_required_mask=0x0000007f
MI4IOS6_STAGE40_XNU high_collection_entry_table_satisfied_mask=0x0000007f
MI4IOS6_STAGE40_XNU high_collection_entry_table_checksum=0xde1ac77d
MI4IOS6_STAGE40_XNU high_collection_entry_table_status=0x40000001
```

Kernel collection entry-table descriptor markers:

```text
MI4IOS6_STAGE40_XNU high_kc_entry_table_handoff_status=0x40000001
MI4IOS6_STAGE40_XNU high_kc_entry_table_handoff_checksum=0xce451213
MI4IOS6_STAGE40_XNU high_kc_entry_table_entry_count=0x00000007
MI4IOS6_STAGE40_XNU high_kc_entry_table_required_object_mask=0x0000007f
MI4IOS6_STAGE40_XNU high_kc_entry_table_observed_object_mask=0x0000007f
MI4IOS6_STAGE40_XNU high_kc_entry_table_required_entry_order_mask=0x0000007f
MI4IOS6_STAGE40_XNU high_kc_entry_table_observed_entry_order_mask=0x0000007f
MI4IOS6_STAGE40_XNU high_kc_entry_table_required_class_mask=0x0000000f
MI4IOS6_STAGE40_XNU high_kc_entry_table_observed_class_mask=0x0000000f
MI4IOS6_STAGE40_XNU high_kc_entry_table_entry0_object=0x00000001
MI4IOS6_STAGE40_XNU high_kc_entry_table_entry1_object=0x00000002
MI4IOS6_STAGE40_XNU high_kc_entry_table_entry2_object=0x00000004
MI4IOS6_STAGE40_XNU high_kc_entry_table_entry3_object=0x00000008
MI4IOS6_STAGE40_XNU high_kc_entry_table_entry4_object=0x00000010
MI4IOS6_STAGE40_XNU high_kc_entry_table_entry5_object=0x00000020
MI4IOS6_STAGE40_XNU high_kc_entry_table_entry6_object=0x00000040
MI4IOS6_STAGE40_XNU high_kc_entry_table_boot_args_virt=0xc0034000
MI4IOS6_STAGE40_XNU high_kc_entry_table_device_tree_virt=0xc0034140
MI4IOS6_STAGE40_XNU high_kc_entry_table_pe_state_virt=0xc0029020
MI4IOS6_STAGE40_XNU high_kc_entry_table_vm_plan_virt=0xc002c000
MI4IOS6_STAGE40_XNU high_kc_entry_table_vm_state_virt=0xc002c04c
MI4IOS6_STAGE40_XNU high_kc_entry_table_allocator_virt=0xc002c09c
MI4IOS6_STAGE40_XNU high_kc_entry_table_pmap_workspace_virt=0xc002c0e4
MI4IOS6_STAGE40_XNU high_kc_entry_table_workspace_base=0x80000000
MI4IOS6_STAGE40_XNU high_kc_entry_table_workspace_limit=0x80100000
MI4IOS6_STAGE40_XNU high_kc_entry_table_section_count=0x000005e5
MI4IOS6_STAGE40_XNU high_kc_entry_table_l1_table_phys=0x00030000
MI4IOS6_STAGE40_XNU high_kc_entry_table_l1_table_virt=0xc0030000
MI4IOS6_STAGE40_XNU high_kc_entry_table_allocation_tag=0x504d4150
MI4IOS6_STAGE40_XNU high_kc_entry_table_satisfied_mask=0x0000007f
MI4IOS6_STAGE40_XNU high_kc_entry_table_checksum=0xde1ac77d
MI4IOS6_STAGE40_XNU high_kc_entry_table_status=0x40000001
MI4IOS6_STAGE40_XNU high_bootstrap_checksum=0xff82eb6c
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_collection_entry_table_phys=0x0002c1e4
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_collection_entry_table_virt=0xc002c1e4
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_collection_entry_table_version_id=0x00000001
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_collection_entry_table_size_id=0x00000088
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_collection_entry_table_required_mask_id=0x0000007f
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_collection_entry_table_satisfied_mask_id=0x0000007f
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_collection_entry_table_checksum_id=0xde1ac77d
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_collection_entry_table_status_id=0x40000001
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_kc_entry_table_handoff_status_id=0x40000001
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_kc_entry_table_handoff_checksum_id=0xce451213
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_kc_entry_table_entry_count_id=0x00000007
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_kc_entry_table_required_object_mask_id=0x0000007f
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_kc_entry_table_observed_object_mask_id=0x0000007f
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_kc_entry_table_required_order_mask_id=0x0000007f
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_kc_entry_table_observed_order_mask_id=0x0000007f
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_kc_entry_table_required_class_mask_id=0x0000000f
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_kc_entry_table_observed_class_mask_id=0x0000000f
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_kc_entry_table_status_id=0x40000001
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_collection_entry_table_status_alias=0x40000001
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_collection_entry_table_checksum_alias=0xde1ac77d
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_kc_entry_table_status_alias=0x40000001
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_kc_entry_table_checksum_alias=0xde1ac77d
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_kc_entry_table_observed_mask_alias=0x0000007f
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_kc_entry_table_order_mask_alias=0x0000007f
MI4IOS6_STAGE40_XNU mmu_high_bootstrap_kc_entry_table_class_mask_alias=0x0000000f
MI4IOS6_STAGE40_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the entry-table-gated root path:

```text
MI4IOS6_STAGE40_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the entry-table-gated root path:

```text
MI4IOS6_STAGE40_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE40_XNU kernel_entry ok
MI4IOS6_STAGE40 kernel_entry returned success
MI4IOS6_STAGE40 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage40 turns the Stage39 handoff into an explicit entry-table contract. The table records all seven early kernel objects in deterministic order, checks object-mask coverage, groups the objects into boot/platform/VM/pmap classes, and validates the high-virtual pointers plus workspace/L1 facts independently from the handoff that produced them.

This is still an XNU-adjacent kernel skeleton, not a booting XNU kernel. The entry table is a staging contract for later kernel collection object-graph validation, bootstrap dependency wiring, and higher-level kernel bootstrap work.

## Success criteria — met

1. bootloader accepted `stage40-qcdt.img`: yes
2. high virtual root entry ran: yes
3. startup entry was called: yes
4. kernel callout table dispatched: yes
5. kernel context object was populated: yes
6. VM plan object was populated: yes
7. VM state object was populated: yes
8. bootstrap allocator descriptor was populated: yes
9. pmap workspace descriptor was populated: yes
10. kernel object table descriptor was populated: yes
11. kernel collection handoff descriptor was populated: yes
12. kernel collection entry-table descriptor was populated: yes
13. entry-table accepted handoff status/checksum (`0x40000001` / `0xce451213`): yes
14. entry count matched (`0x00000007`): yes
15. required/observed object mask matched (`0x0000007f`): yes
16. required/observed entry-order mask matched (`0x0000007f`): yes
17. required/observed class mask matched (`0x0000000f`): yes
18. all seven ordered object slots matched: yes
19. all seven high-virtual object pointers matched: yes
20. workspace base/limit matched (`0x80000000` / `0x80100000`): yes
21. section count matched (`0x000005e5`): yes
22. L1 table addresses matched (`0x00030000` / `0xc0030000`): yes
23. allocation tag matched (`0x504d4150`): yes
24. collection entry-table satisfied mask was complete (`0x0000007f`): yes
25. collection entry-table checksum matched (`0xde1ac77d`): yes
26. collection entry-table status was `0x40000001`: yes
27. full root step mask was complete (`0x07ffffff`): yes
28. high root returned status `0x40000001`: yes
29. checksum/status/object/order/class masks matched through identity and alias views: yes
30. SGI and timer IRQ paths still worked after high root: yes
31. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage41 can make the kernel collection entry table drive a minimal kernel collection object graph:

- keep identity/recovery mappings and caches disabled,
- keep existing startup-entry/callout/context/VM-plan/VM-state/allocator/pmap-workspace/object-table/collection-handoff/entry-table checks,
- add a versioned kernel collection object-graph descriptor consuming ordered entry-table nodes,
- validate node count, node mask, dependency/edge ordering, boot/platform/VM/pmap class coverage, checksum/status, and identity/high-alias views,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
