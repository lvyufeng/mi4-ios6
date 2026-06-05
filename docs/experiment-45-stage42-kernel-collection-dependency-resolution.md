# Experiment 45 — Stage42 Kernel Collection Dependency Resolution

Date: 2026-06-05

Goal: make the accepted kernel collection object graph drive a minimal dependency-resolution descriptor.

Stage42 still does **not** run XNU or iOS. It extends Stage41 by adding a versioned kernel collection dependency-resolution descriptor after the kernel collection object graph has been populated and accepted. The dependency-resolution descriptor consumes the seven graph nodes and dependency edges for boot args, device tree, PE state, VM plan, VM state, bootstrap allocator, and pmap workspace, then validates resolved node order, dependency coverage, activation readiness, boot/platform/VM/pmap class coverage, workspace/L1 facts, checksum, status, and identity/high-alias views before final root success.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage42 adds over Stage41

Stage41 proved:

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
- kernel collection object-graph descriptor object,
- object-graph required/satisfied mask `0x0000003f`,
- node/edge masks `0x0000007f` / `0x0000007f`,
- class mask `0x0000000f`,
- entry-table checksum/status `0xdf1a877d` / `0x41000001`,
- object-graph checksum/status `0xce451242` / `0x41000001`,
- root step mask `0x0fffffff`,
- final root status `0x41000001`.

Stage42 adds a kernel collection dependency-resolution descriptor object:

- dependency-resolution version: `1`,
- dependency-resolution size: `0x000000c8`,
- dependency-resolution required/satisfied mask: `0x0000007f`,
- accepted object-graph status: `0x42000001`,
- accepted object-graph checksum: `0xce451242`,
- node count: `7`,
- required/observed resolved-order mask: `0x0000007f`,
- required/observed dependency mask: `0x0000007f`,
- required/observed activation mask: `0x0000007f`,
- required/observed class mask: `0x0000000f`,
- resolved object sequence: `0x00000001`, `0x00000002`, `0x00000004`, `0x00000008`, `0x00000010`, `0x00000020`, `0x00000040`,
- dependency sequence: `0x00000000`, `0x00000001`, `0x00000003`, `0x00000007`, `0x00000008`, `0x00000010`, `0x00000020`,
- activation-ready sequence: `0x00000001`, `0x00000002`, `0x00000004`, `0x00000008`, `0x00000010`, `0x00000020`, `0x00000040`,
- boot args high pointer: `0xc003c000`,
- device tree high pointer: `0xc003c140`,
- PE state high pointer: `0xc0031020`,
- VM plan high pointer: `0xc0034000`,
- VM state high pointer: `0xc003404c`,
- allocator high pointer: `0xc003409c`,
- pmap workspace high pointer: `0xc00340e4`,
- workspace base/limit: `0x80000000` / `0x80100000`,
- section count: `0x000005e5`,
- L1 table physical/high-alias address: `0x00038000` / `0xc0038000`,
- allocation tag: `0x504d4150` (`PMAP`),
- dependency-resolution physical/high-alias address: `0x00034310` / `0xc0034310`,
- dependency-resolution checksum: `0xdc1b472e`,
- dependency-resolution status: `0x42000001`,
- bootstrap checksum: `0xf7dcbc3f`,
- full root step mask extends to `0x1fffffff`,
- final root status `0x42000001`.

Dependency-resolution satisfied bits:

```text
0x00000001 accepted object-graph status/checksum satisfied
0x00000002 resolved node order satisfied
0x00000004 dependency coverage satisfied
0x00000008 activation readiness satisfied
0x00000010 boot/platform/VM/pmap class coverage satisfied
0x00000020 all seven high-virtual object pointers satisfied
0x00000040 workspace range, section count, L1 table, and allocation tag satisfied
```

Complete dependency-resolution satisfied mask: `0x0000007f`.

The new root-step bit records that the kernel collection dependency-resolution descriptor was accepted:

```text
Stage41 final root steps: 0x0fffffff
Stage42 final root steps: 0x1fffffff
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
0x10000000 kernel collection dependency-resolution descriptor complete
```

Complete root-step mask: `0x1fffffff`.

## Built image

```bash
./stage42/build.sh
```

Successful local build:

```text
out/stage42/stage42-qcdt.img
sha256=5537e7e4584cfb00337ecf52755eba038743f3791814583f46b9d4eb650250bb
```

Build hashes:

```text
d42053a1bb4e5d4bcdda32ba9ecdb99f0ccceeb677714c5c058304dfada917dd  out/stage42/stage42.elf
64e40efaa145ccd78a1c8c4625ce535bd5ad4261ef08f9f2fb0ff9e8be05e61a  out/stage42/stage42.bin
50db49366da51198515278b1bfe4b5238343da468c60cd5b1f828d824de5218a  out/stage42/stage42.img
5537e7e4584cfb00337ecf52755eba038743f3791814583f46b9d4eb650250bb  out/stage42/stage42-qcdt.img
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=134692 (0x20e24)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage42 mi4ios6=stage42 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage42_vectors
0000ab68 t stage42_kernel_root
00012a14 t stage42_startup_entry
00013b64 T mmu_high_bootstrap_selftest
00019024 T kernel_entry
0001934c T test_kernel_entry
000194e4 T stage42_main
00034000 b stage42_vm_plan_block
0003404c b stage42_vm_state_block
0003409c b stage42_boot_allocator_block
000340e4 b stage42_pmap_workspace_block
0003412c b stage42_kernel_object_table_block
00034188 b stage42_kernel_collection_handoff_block
000341e4 b stage42_kernel_collection_entry_table_block
0003426c b stage42_kernel_collection_object_graph_block
00034310 b stage42_kernel_collection_dependency_resolution_block
00034458 b stage42_bootstrap_state_block
00038000 b stage42_l1_table
0003e000 B __stage42_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage42/stage42-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2596 KB)                       OKAY [  0.083s]
Booting                                            OKAY [  0.003s]
Finished. Total time: 0.900s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage42-last_kmsg.txt
```

The recovered log was 95357 bytes and contained:

```text
1412 MI4IOS6_STAGE42 markers
1386 MI4IOS6_STAGE42_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE42_XNU high_collection_dependency_resolution_virt=0xc0034310
MI4IOS6_STAGE42_XNU high root kernel collection dependency resolution begin
MI4IOS6_STAGE42_XNU high root kernel collection dependency resolution ok
MI4IOS6_STAGE42_XNU high_root_steps=0x1fffffff
MI4IOS6_STAGE42_XNU high_root_status=0x42000001
```

Dependency-resolution state markers:

```text
MI4IOS6_STAGE42_XNU high_collection_dependency_resolution_version=0x00000001
MI4IOS6_STAGE42_XNU high_collection_dependency_resolution_size=0x000000c8
MI4IOS6_STAGE42_XNU high_collection_dependency_resolution_required_mask=0x0000007f
MI4IOS6_STAGE42_XNU high_collection_dependency_resolution_satisfied_mask=0x0000007f
MI4IOS6_STAGE42_XNU high_collection_dependency_resolution_checksum=0xdc1b472e
MI4IOS6_STAGE42_XNU high_collection_dependency_resolution_status=0x42000001
```

Kernel collection dependency-resolution descriptor markers:

```text
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_object_graph_status=0x42000001
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_object_graph_checksum=0xce451242
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_node_count=0x00000007
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_required_order_mask=0x0000007f
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_observed_order_mask=0x0000007f
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_required_dependency_mask=0x0000007f
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_observed_dependency_mask=0x0000007f
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_required_activation_mask=0x0000007f
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_observed_activation_mask=0x0000007f
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_required_class_mask=0x0000000f
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_observed_class_mask=0x0000000f
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_resolved0_object=0x00000001
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_resolved1_object=0x00000002
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_resolved2_object=0x00000004
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_resolved3_object=0x00000008
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_resolved4_object=0x00000010
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_resolved5_object=0x00000020
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_resolved6_object=0x00000040
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_resolved0_dependencies=0x00000000
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_resolved1_dependencies=0x00000001
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_resolved2_dependencies=0x00000003
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_resolved3_dependencies=0x00000007
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_resolved4_dependencies=0x00000008
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_resolved5_dependencies=0x00000010
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_resolved6_dependencies=0x00000020
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_activation0_ready=0x00000001
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_activation1_ready=0x00000002
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_activation2_ready=0x00000004
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_activation3_ready=0x00000008
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_activation4_ready=0x00000010
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_activation5_ready=0x00000020
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_activation6_ready=0x00000040
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_boot_args_virt=0xc003c000
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_device_tree_virt=0xc003c140
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_pe_state_virt=0xc0031020
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_vm_plan_virt=0xc0034000
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_vm_state_virt=0xc003404c
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_allocator_virt=0xc003409c
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_pmap_workspace_virt=0xc00340e4
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_workspace_base=0x80000000
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_workspace_limit=0x80100000
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_section_count=0x000005e5
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_l1_table_phys=0x00038000
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_l1_table_virt=0xc0038000
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_allocation_tag=0x504d4150
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_satisfied_mask=0x0000007f
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_checksum=0xdc1b472e
MI4IOS6_STAGE42_XNU high_kc_dependency_resolution_status=0x42000001
MI4IOS6_STAGE42_XNU high_bootstrap_checksum=0xf7dcbc3f
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE42_XNU mmu_high_bootstrap_collection_dependency_resolution_phys=0x00034310
MI4IOS6_STAGE42_XNU mmu_high_bootstrap_collection_dependency_resolution_virt=0xc0034310
MI4IOS6_STAGE42_XNU mmu_high_bootstrap_collection_dependency_resolution_version_id=0x00000001
MI4IOS6_STAGE42_XNU mmu_high_bootstrap_collection_dependency_resolution_size_id=0x000000c8
MI4IOS6_STAGE42_XNU mmu_high_bootstrap_collection_dependency_resolution_required_mask_id=0x0000007f
MI4IOS6_STAGE42_XNU mmu_high_bootstrap_collection_dependency_resolution_satisfied_mask_id=0x0000007f
MI4IOS6_STAGE42_XNU mmu_high_bootstrap_collection_dependency_resolution_checksum_id=0xdc1b472e
MI4IOS6_STAGE42_XNU mmu_high_bootstrap_collection_dependency_resolution_status_id=0x42000001
MI4IOS6_STAGE42_XNU mmu_high_bootstrap_collection_dependency_resolution_status_alias=0x42000001
MI4IOS6_STAGE42_XNU mmu_high_bootstrap_collection_dependency_resolution_checksum_alias=0xdc1b472e
MI4IOS6_STAGE42_XNU mmu_high_bootstrap_kc_dependency_resolution_status_alias=0x42000001
MI4IOS6_STAGE42_XNU mmu_high_bootstrap_kc_dependency_resolution_checksum_alias=0xdc1b472e
MI4IOS6_STAGE42_XNU mmu_high_bootstrap_kc_dependency_resolution_order_mask_alias=0x0000007f
MI4IOS6_STAGE42_XNU mmu_high_bootstrap_kc_dependency_resolution_dependency_mask_alias=0x0000007f
MI4IOS6_STAGE42_XNU mmu_high_bootstrap_kc_dependency_resolution_activation_mask_alias=0x0000007f
MI4IOS6_STAGE42_XNU mmu_high_bootstrap_kc_dependency_resolution_class_mask_alias=0x0000000f
MI4IOS6_STAGE42_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the dependency-resolution-gated root path:

```text
MI4IOS6_STAGE42_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the dependency-resolution-gated root path:

```text
MI4IOS6_STAGE42_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE42_XNU kernel_entry ok
MI4IOS6_STAGE42 kernel_entry returned success
MI4IOS6_STAGE42 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage42 turns the Stage41 object graph into an explicit dependency-resolution contract. The descriptor records the seven early kernel objects in resolved order, proves each dependency is covered by earlier resolved nodes, proves activation readiness for every object, and checks boot/platform/VM/pmap class coverage. It also keeps the existing pointer, workspace, L1 table, allocation-tag, checksum, status, identity-view, and high-alias-view checks.

This is still an XNU-adjacent kernel skeleton, not a booting XNU kernel. The dependency-resolution object is a staging contract for later activation-order, linkage, and higher-level kernel-bootstrap work.

## Success criteria — met

1. bootloader accepted `stage42-qcdt.img`: yes
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
14. kernel collection dependency-resolution descriptor was populated: yes
15. dependency resolution accepted object-graph status/checksum (`0x42000001` / `0xce451242`): yes
16. node count matched (`0x00000007`): yes
17. required/observed resolved-order mask matched (`0x0000007f`): yes
18. required/observed dependency mask matched (`0x0000007f`): yes
19. required/observed activation mask matched (`0x0000007f`): yes
20. required/observed class mask matched (`0x0000000f`): yes
21. all seven resolved objects matched: yes
22. all seven dependency masks matched and were covered by prior resolved nodes: yes
23. all seven activation-ready slots matched: yes
24. all seven high-virtual object pointers matched: yes
25. workspace base/limit matched (`0x80000000` / `0x80100000`): yes
26. section count matched (`0x000005e5`): yes
27. L1 table addresses matched (`0x00038000` / `0xc0038000`): yes
28. allocation tag matched (`0x504d4150`): yes
29. dependency-resolution satisfied mask was complete (`0x0000007f`): yes
30. dependency-resolution checksum matched (`0xdc1b472e`): yes
31. dependency-resolution status was `0x42000001`: yes
32. full root step mask was complete (`0x1fffffff`): yes
33. high root returned status `0x42000001`: yes
34. checksum/status/order/dependency/activation/class masks matched through identity and alias views: yes
35. SGI and timer IRQ paths still worked after high root: yes
36. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage43 can make the dependency-resolution descriptor drive a minimal activation-order descriptor:

- keep identity/recovery mappings and caches disabled,
- keep existing startup-entry/callout/context/VM-plan/VM-state/allocator/pmap-workspace/object-table/collection-handoff/entry-table/object-graph/dependency-resolution checks,
- add a versioned activation-order descriptor consuming resolved nodes and activation-ready facts,
- validate activation sequence, prerequisite coverage, class readiness, checksum/status, and identity/high-alias views,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
