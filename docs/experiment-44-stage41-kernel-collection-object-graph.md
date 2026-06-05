# Experiment 44 — Stage41 Kernel Collection Object Graph

Date: 2026-06-05

Goal: make the kernel collection entry table drive a minimal kernel collection object-graph descriptor.

Stage41 still does **not** run XNU or iOS. It extends Stage40 by adding a versioned kernel collection object-graph descriptor after the kernel collection entry table has been populated and accepted. The object graph consumes the seven ordered entry-table nodes for boot args, device tree, PE state, VM plan, VM state, bootstrap allocator, and pmap workspace, then validates node count, node mask, dependency/edge ordering, boot/platform/VM/pmap class coverage, workspace/L1 facts, checksum, and status through identity and high-alias views before final root success.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage41 adds over Stage40

Stage40 proved:

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
- kernel collection entry-table descriptor object,
- entry-table required/satisfied mask `0x0000007f`,
- object/order masks `0x0000007f` / `0x0000007f`,
- class mask `0x0000000f`,
- handoff checksum/status `0xce451213` / `0x40000001`,
- entry-table checksum/status `0xde1ac77d` / `0x40000001`,
- root step mask `0x07ffffff`,
- final root status `0x40000001`.

Stage41 adds a kernel collection object-graph descriptor object:

- object-graph version: `1`,
- object-graph size: `0x000000a4`,
- object-graph required/satisfied mask: `0x0000003f`,
- accepted entry-table status: `0x41000001`,
- accepted entry-table checksum: `0xdf1a877d`,
- node count: `7`,
- required/observed node mask: `0x0000007f`,
- required/observed edge mask: `0x0000007f`,
- required/observed class mask: `0x0000000f`,
- node object sequence: `0x00000001`, `0x00000002`, `0x00000004`, `0x00000008`, `0x00000010`, `0x00000020`, `0x00000040`,
- dependency sequence: `0x00000000`, `0x00000001`, `0x00000003`, `0x00000007`, `0x00000008`, `0x00000010`, `0x00000020`,
- boot args high pointer: `0xc0038000`,
- device tree high pointer: `0xc0038140`,
- PE state high pointer: `0xc002d020`,
- VM plan high pointer: `0xc0030000`,
- VM state high pointer: `0xc003004c`,
- allocator high pointer: `0xc003009c`,
- pmap workspace high pointer: `0xc00300e4`,
- workspace base/limit: `0x80000000` / `0x80100000`,
- section count: `0x000005e5`,
- L1 table physical/high-alias address: `0x00034000` / `0xc0034000`,
- allocation tag: `0x504d4150` (`PMAP`),
- object-graph checksum: `0xce451242`,
- object-graph status: `0x41000001`,
- full root step mask extends to `0x0fffffff`,
- final root status `0x41000001`.

Object-graph satisfied bits:

```text
0x00000001 accepted entry-table status/checksum satisfied
0x00000002 node count and node mask satisfied
0x00000004 dependency/edge mask satisfied
0x00000008 boot/platform/VM/pmap class coverage satisfied
0x00000010 all seven high-virtual object pointers satisfied
0x00000020 workspace range, section count, L1 table, and allocation tag satisfied
```

Complete object-graph satisfied mask: `0x0000003f`.

The new root-step bit records that the kernel collection object-graph descriptor was accepted:

```text
Stage40 final root steps: 0x07ffffff
Stage41 final root steps: 0x0fffffff
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
0x08000000 kernel collection object-graph descriptor complete
```

Complete root-step mask: `0x0fffffff`.

## Built image

```bash
./stage41/build.sh
```

Successful local build:

```text
out/stage41/stage41-qcdt.img
sha256=902623a6f04843f715b7e457ce0acf470719d9af653cd8d8f8c7cc79beb30e50
```

Build hashes:

```text
1b2e8a1d04ef8e8b5bdfe2af4786c081792f8f9eb8ae6e73f3c26dea700b2c9b  out/stage41/stage41.elf
9724c3ac0a619f43ab6a44e595d5e31655b1602cbafb28065b4aab0df79460c0  out/stage41/stage41.bin
6fb99cad945033454ba2365a51d370de4f20c04eb3186aa7d5720398e89292f8  out/stage41/stage41.img
902623a6f04843f715b7e457ce0acf470719d9af653cd8d8f8c7cc79beb30e50  out/stage41/stage41-qcdt.img
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=124244 (0x1e554)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage41 mi4ios6=stage41 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage41_vectors
0000ab68 t stage41_kernel_root
00011c84 t stage41_startup_entry
00012dd0 T mmu_high_bootstrap_selftest
00017c88 T kernel_entry
00017fb0 T test_kernel_entry
00018148 T stage41_main
00030000 b stage41_vm_plan_block
0003004c b stage41_vm_state_block
0003009c b stage41_boot_allocator_block
000300e4 b stage41_pmap_workspace_block
0003012c b stage41_kernel_object_table_block
00030188 b stage41_kernel_collection_handoff_block
000301e4 b stage41_kernel_collection_entry_table_block
0003026c b stage41_kernel_collection_object_graph_block
00030310 b stage41_startup_handoff_block
00030340 b stage41_kernel_context_block
00030390 b stage41_bootstrap_state_block
00034000 b stage41_l1_table
0003a000 B __stage41_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage41/stage41-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2586 KB)                       OKAY [  0.082s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.092s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage41-last_kmsg.txt
```

The recovered log was 87340 bytes and contained:

```text
1318 MI4IOS6_STAGE41 markers
1292 MI4IOS6_STAGE41_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE41_XNU high_pmap_workspace_virt=0xc00300e4
MI4IOS6_STAGE41_XNU high_object_table_virt=0xc003012c
MI4IOS6_STAGE41_XNU high_collection_handoff_virt=0xc0030188
MI4IOS6_STAGE41_XNU high_collection_entry_table_virt=0xc00301e4
MI4IOS6_STAGE41_XNU high_collection_object_graph_virt=0xc003026c
MI4IOS6_STAGE41_XNU high root vm bootstrap plan begin
MI4IOS6_STAGE41_XNU high root vm bootstrap plan ok
MI4IOS6_STAGE41_XNU high root vm bootstrap state begin
MI4IOS6_STAGE41_XNU high root vm bootstrap state ok
MI4IOS6_STAGE41_XNU high root bootstrap allocator begin
MI4IOS6_STAGE41_XNU high root bootstrap allocator ok
MI4IOS6_STAGE41_XNU high root pmap workspace begin
MI4IOS6_STAGE41_XNU high root pmap workspace ok
MI4IOS6_STAGE41_XNU high root kernel object table begin
MI4IOS6_STAGE41_XNU high root kernel object table ok
MI4IOS6_STAGE41_XNU high root kernel collection handoff begin
MI4IOS6_STAGE41_XNU high root kernel collection handoff ok
MI4IOS6_STAGE41_XNU high root kernel collection entry table begin
MI4IOS6_STAGE41_XNU high root kernel collection entry table ok
MI4IOS6_STAGE41_XNU high root kernel collection object graph begin
MI4IOS6_STAGE41_XNU high root kernel collection object graph ok
MI4IOS6_STAGE41_XNU high_root_steps=0x0fffffff
MI4IOS6_STAGE41_XNU high_root_status=0x41000001
```

Object table, collection handoff, entry-table, and object-graph state markers:

```text
MI4IOS6_STAGE41_XNU high_object_table_checksum=0xdf1a8796
MI4IOS6_STAGE41_XNU high_object_table_status=0x41000001
MI4IOS6_STAGE41_XNU high_collection_handoff_checksum=0xce451213
MI4IOS6_STAGE41_XNU high_collection_handoff_status=0x41000001
MI4IOS6_STAGE41_XNU high_collection_entry_table_checksum=0xdf1a877d
MI4IOS6_STAGE41_XNU high_collection_entry_table_status=0x41000001
MI4IOS6_STAGE41_XNU high_collection_object_graph_version=0x00000001
MI4IOS6_STAGE41_XNU high_collection_object_graph_size=0x000000a4
MI4IOS6_STAGE41_XNU high_collection_object_graph_required_mask=0x0000003f
MI4IOS6_STAGE41_XNU high_collection_object_graph_satisfied_mask=0x0000003f
MI4IOS6_STAGE41_XNU high_collection_object_graph_checksum=0xce451242
MI4IOS6_STAGE41_XNU high_collection_object_graph_status=0x41000001
```

Kernel collection object-graph descriptor markers:

```text
MI4IOS6_STAGE41_XNU high_kc_object_graph_entry_table_status=0x41000001
MI4IOS6_STAGE41_XNU high_kc_object_graph_entry_table_checksum=0xdf1a877d
MI4IOS6_STAGE41_XNU high_kc_object_graph_node_count=0x00000007
MI4IOS6_STAGE41_XNU high_kc_object_graph_required_node_mask=0x0000007f
MI4IOS6_STAGE41_XNU high_kc_object_graph_observed_node_mask=0x0000007f
MI4IOS6_STAGE41_XNU high_kc_object_graph_required_edge_mask=0x0000007f
MI4IOS6_STAGE41_XNU high_kc_object_graph_observed_edge_mask=0x0000007f
MI4IOS6_STAGE41_XNU high_kc_object_graph_required_class_mask=0x0000000f
MI4IOS6_STAGE41_XNU high_kc_object_graph_observed_class_mask=0x0000000f
MI4IOS6_STAGE41_XNU high_kc_object_graph_node0_object=0x00000001
MI4IOS6_STAGE41_XNU high_kc_object_graph_node1_object=0x00000002
MI4IOS6_STAGE41_XNU high_kc_object_graph_node2_object=0x00000004
MI4IOS6_STAGE41_XNU high_kc_object_graph_node3_object=0x00000008
MI4IOS6_STAGE41_XNU high_kc_object_graph_node4_object=0x00000010
MI4IOS6_STAGE41_XNU high_kc_object_graph_node5_object=0x00000020
MI4IOS6_STAGE41_XNU high_kc_object_graph_node6_object=0x00000040
MI4IOS6_STAGE41_XNU high_kc_object_graph_node0_dependencies=0x00000000
MI4IOS6_STAGE41_XNU high_kc_object_graph_node1_dependencies=0x00000001
MI4IOS6_STAGE41_XNU high_kc_object_graph_node2_dependencies=0x00000003
MI4IOS6_STAGE41_XNU high_kc_object_graph_node3_dependencies=0x00000007
MI4IOS6_STAGE41_XNU high_kc_object_graph_node4_dependencies=0x00000008
MI4IOS6_STAGE41_XNU high_kc_object_graph_node5_dependencies=0x00000010
MI4IOS6_STAGE41_XNU high_kc_object_graph_node6_dependencies=0x00000020
MI4IOS6_STAGE41_XNU high_kc_object_graph_boot_args_virt=0xc0038000
MI4IOS6_STAGE41_XNU high_kc_object_graph_device_tree_virt=0xc0038140
MI4IOS6_STAGE41_XNU high_kc_object_graph_pe_state_virt=0xc002d020
MI4IOS6_STAGE41_XNU high_kc_object_graph_vm_plan_virt=0xc0030000
MI4IOS6_STAGE41_XNU high_kc_object_graph_vm_state_virt=0xc003004c
MI4IOS6_STAGE41_XNU high_kc_object_graph_allocator_virt=0xc003009c
MI4IOS6_STAGE41_XNU high_kc_object_graph_pmap_workspace_virt=0xc00300e4
MI4IOS6_STAGE41_XNU high_kc_object_graph_workspace_base=0x80000000
MI4IOS6_STAGE41_XNU high_kc_object_graph_workspace_limit=0x80100000
MI4IOS6_STAGE41_XNU high_kc_object_graph_section_count=0x000005e5
MI4IOS6_STAGE41_XNU high_kc_object_graph_l1_table_phys=0x00034000
MI4IOS6_STAGE41_XNU high_kc_object_graph_l1_table_virt=0xc0034000
MI4IOS6_STAGE41_XNU high_kc_object_graph_allocation_tag=0x504d4150
MI4IOS6_STAGE41_XNU high_kc_object_graph_satisfied_mask=0x0000003f
MI4IOS6_STAGE41_XNU high_kc_object_graph_checksum=0xce451242
MI4IOS6_STAGE41_XNU high_kc_object_graph_status=0x41000001
MI4IOS6_STAGE41_XNU high_bootstrap_checksum=0x79c7f8a1
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_collection_object_graph_phys=0x0003026c
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_collection_object_graph_virt=0xc003026c
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_collection_object_graph_version_id=0x00000001
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_collection_object_graph_size_id=0x000000a4
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_collection_object_graph_required_mask_id=0x0000003f
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_collection_object_graph_satisfied_mask_id=0x0000003f
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_collection_object_graph_checksum_id=0xce451242
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_collection_object_graph_status_id=0x41000001
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_kc_object_graph_node_count_id=0x00000007
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_kc_object_graph_required_node_mask_id=0x0000007f
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_kc_object_graph_observed_node_mask_id=0x0000007f
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_kc_object_graph_required_edge_mask_id=0x0000007f
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_kc_object_graph_observed_edge_mask_id=0x0000007f
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_kc_object_graph_required_class_mask_id=0x0000000f
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_kc_object_graph_observed_class_mask_id=0x0000000f
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_kc_object_graph_status_id=0x41000001
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_collection_object_graph_status_alias=0x41000001
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_collection_object_graph_checksum_alias=0xce451242
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_kc_object_graph_status_alias=0x41000001
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_kc_object_graph_checksum_alias=0xce451242
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_kc_object_graph_node_mask_alias=0x0000007f
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_kc_object_graph_edge_mask_alias=0x0000007f
MI4IOS6_STAGE41_XNU mmu_high_bootstrap_kc_object_graph_class_mask_alias=0x0000000f
MI4IOS6_STAGE41_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the object-graph-gated root path:

```text
MI4IOS6_STAGE41_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the object-graph-gated root path:

```text
MI4IOS6_STAGE41_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE41_XNU kernel_entry ok
MI4IOS6_STAGE41 kernel_entry returned success
MI4IOS6_STAGE41 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage41 turns the Stage40 entry table into an explicit object graph contract. The graph records the seven early kernel objects as deterministic nodes, records simple dependency masks/edges, checks object-node coverage, checks class coverage, and validates high-virtual object pointers plus workspace/L1 facts independently from the entry table that produced it.

This is still an XNU-adjacent kernel skeleton, not a booting XNU kernel. The object graph is a staging contract for later dependency-resolution, activation-order, and higher-level kernel-bootstrap work.

## Success criteria — met

1. bootloader accepted `stage41-qcdt.img`: yes
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
13. kernel collection object-graph descriptor was populated: yes
14. object graph accepted entry-table status/checksum (`0x41000001` / `0xdf1a877d`): yes
15. node count matched (`0x00000007`): yes
16. required/observed node mask matched (`0x0000007f`): yes
17. required/observed edge mask matched (`0x0000007f`): yes
18. required/observed class mask matched (`0x0000000f`): yes
19. all seven ordered nodes matched: yes
20. all seven dependency masks matched: yes
21. all seven high-virtual object pointers matched: yes
22. workspace base/limit matched (`0x80000000` / `0x80100000`): yes
23. section count matched (`0x000005e5`): yes
24. L1 table addresses matched (`0x00034000` / `0xc0034000`): yes
25. allocation tag matched (`0x504d4150`): yes
26. object-graph satisfied mask was complete (`0x0000003f`): yes
27. object-graph checksum matched (`0xce451242`): yes
28. object-graph status was `0x41000001`: yes
29. full root step mask was complete (`0x0fffffff`): yes
30. high root returned status `0x41000001`: yes
31. checksum/status/node/edge/class masks matched through identity and alias views: yes
32. SGI and timer IRQ paths still worked after high root: yes
33. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage42 can make the kernel collection object graph drive a minimal dependency-resolution descriptor:

- keep identity/recovery mappings and caches disabled,
- keep existing startup-entry/callout/context/VM-plan/VM-state/allocator/pmap-workspace/object-table/collection-handoff/entry-table/object-graph checks,
- add a versioned dependency-resolution descriptor consuming graph nodes and edges,
- validate resolved node order, dependency coverage, activation readiness, boot/platform/VM/pmap class coverage, checksum/status, and identity/high-alias views,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
