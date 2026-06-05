# Experiment 42 — Stage39 Kernel Collection Handoff

Date: 2026-06-05

Goal: make the kernel object table drive a minimal kernel collection handoff descriptor.

Stage39 still does **not** run XNU or iOS. It extends Stage38 by adding a versioned kernel collection handoff descriptor after the kernel object table has been populated and accepted. The handoff consumes object-table coverage and high-virtual object slots for boot args, device tree, PE state, VM plan, VM state, bootstrap allocator, and pmap workspace, then validates entry count, object masks, object pointers, workspace/L1 facts, checksum, and status through identity and high-alias views before final root success.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage39 adds over Stage38

Stage38 proved:

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
- object table required/satisfied mask `0x0000003f`,
- object table object coverage mask `0x0000007f`,
- object table checksum/status `0xa61ac796` / `0x38000001`,
- root step mask `0x01ffffff`,
- final root status `0x38000001`.

Stage39 adds a kernel collection handoff descriptor object:

- collection handoff version: `1`,
- collection handoff size: `0x0000005c`,
- handoff required/satisfied mask: `0x0000003f`,
- accepted object table status: `0x39000001`,
- accepted object table checksum: `0xa71ac796`,
- handoff entry count: `7`,
- required/observed object mask: `0x0000007f`,
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
- collection handoff checksum: `0xce451213`,
- collection handoff status: `0x39000001`,
- full root step mask extends to `0x03ffffff`,
- final root status `0x39000001`.

Collection handoff satisfied bits:

```text
0x00000001 accepted object table status/checksum satisfied
0x00000002 handoff entry count and required/observed object masks satisfied
0x00000004 boot args, device tree, and PE state high pointers satisfied
0x00000008 VM plan/state high pointers and statuses satisfied
0x00000010 allocator/pmap workspace high pointers and statuses satisfied
0x00000020 workspace range, section count, L1 table, and allocation tag satisfied
```

Complete collection handoff satisfied mask: `0x0000003f`.

The new root-step bit records that the kernel collection handoff descriptor was accepted:

```text
Stage38 final root steps: 0x01ffffff
Stage39 final root steps: 0x03ffffff
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
```

Complete root-step mask: `0x03ffffff`.

## Built image

```bash
./stage39/build.sh
```

Successful local build:

```text
out/stage39/stage39-qcdt.img
sha256=aad3b3cd0b0f08eed8c04f7ff5770ff0b24b9111b7d10af69ab33acda190f851
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=106208 (0x19ee0)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage39 mi4ios6=stage39 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage39_vectors
0000ab68 t stage39_kernel_root
000106b4 t stage39_startup_entry
00011800 T mmu_high_bootstrap_selftest
000159f0 T kernel_entry
00015d18 T test_kernel_entry
00015eb0 T stage39_main
0002c000 b stage39_vm_plan_block
0002c04c b stage39_vm_state_block
0002c09c b stage39_boot_allocator_block
0002c0e4 b stage39_pmap_workspace_block
0002c12c b stage39_kernel_object_table_block
0002c188 b stage39_kernel_collection_handoff_block
0002c1e4 b stage39_startup_handoff_block
0002c214 b stage39_kernel_context_block
0002c264 b stage39_bootstrap_state_block
00030000 b stage39_l1_table
00036000 B __stage39_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage39/stage39-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2568 KB)                       OKAY [  0.082s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.102s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage39-last_kmsg.txt
```

The recovered log was 72852 bytes and contained:

```text
1128 MI4IOS6_STAGE39 markers
1102 MI4IOS6_STAGE39_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE39_XNU high_pmap_workspace_virt=0xc002c0e4
MI4IOS6_STAGE39_XNU high_object_table_virt=0xc002c12c
MI4IOS6_STAGE39_XNU high_collection_handoff_virt=0xc002c188
MI4IOS6_STAGE39_XNU high root vm bootstrap plan begin
MI4IOS6_STAGE39_XNU high root vm bootstrap plan ok
MI4IOS6_STAGE39_XNU high root vm bootstrap state begin
MI4IOS6_STAGE39_XNU high root vm bootstrap state ok
MI4IOS6_STAGE39_XNU high root bootstrap allocator begin
MI4IOS6_STAGE39_XNU high root bootstrap allocator ok
MI4IOS6_STAGE39_XNU high root pmap workspace begin
MI4IOS6_STAGE39_XNU high root pmap workspace ok
MI4IOS6_STAGE39_XNU high root kernel object table begin
MI4IOS6_STAGE39_XNU high root kernel object table ok
MI4IOS6_STAGE39_XNU high root kernel collection handoff begin
MI4IOS6_STAGE39_XNU high root kernel collection handoff ok
MI4IOS6_STAGE39_XNU high_root_steps=0x03ffffff
MI4IOS6_STAGE39_XNU high_root_status=0x39000001
```

Object table and collection handoff state markers:

```text
MI4IOS6_STAGE39_XNU high_object_table_version=0x00000001
MI4IOS6_STAGE39_XNU high_object_table_size=0x0000005c
MI4IOS6_STAGE39_XNU high_object_table_required_mask=0x0000003f
MI4IOS6_STAGE39_XNU high_object_table_satisfied_mask=0x0000003f
MI4IOS6_STAGE39_XNU high_object_table_checksum=0xa71ac796
MI4IOS6_STAGE39_XNU high_object_table_status=0x39000001
MI4IOS6_STAGE39_XNU high_collection_handoff_version=0x00000001
MI4IOS6_STAGE39_XNU high_collection_handoff_size=0x0000005c
MI4IOS6_STAGE39_XNU high_collection_handoff_required_mask=0x0000003f
MI4IOS6_STAGE39_XNU high_collection_handoff_satisfied_mask=0x0000003f
MI4IOS6_STAGE39_XNU high_collection_handoff_checksum=0xce451213
MI4IOS6_STAGE39_XNU high_collection_handoff_status=0x39000001
```

Kernel collection handoff descriptor markers:

```text
MI4IOS6_STAGE39_XNU high_kc_handoff_object_table_status=0x39000001
MI4IOS6_STAGE39_XNU high_kc_handoff_object_table_checksum=0xa71ac796
MI4IOS6_STAGE39_XNU high_kc_handoff_entry_count=0x00000007
MI4IOS6_STAGE39_XNU high_kc_handoff_required_object_mask=0x0000007f
MI4IOS6_STAGE39_XNU high_kc_handoff_observed_object_mask=0x0000007f
MI4IOS6_STAGE39_XNU high_kc_handoff_boot_args_virt=0xc0034000
MI4IOS6_STAGE39_XNU high_kc_handoff_device_tree_virt=0xc0034140
MI4IOS6_STAGE39_XNU high_kc_handoff_pe_state_virt=0xc0029020
MI4IOS6_STAGE39_XNU high_kc_handoff_vm_plan_virt=0xc002c000
MI4IOS6_STAGE39_XNU high_kc_handoff_vm_state_virt=0xc002c04c
MI4IOS6_STAGE39_XNU high_kc_handoff_allocator_virt=0xc002c09c
MI4IOS6_STAGE39_XNU high_kc_handoff_pmap_workspace_virt=0xc002c0e4
MI4IOS6_STAGE39_XNU high_kc_handoff_workspace_base=0x80000000
MI4IOS6_STAGE39_XNU high_kc_handoff_workspace_limit=0x80100000
MI4IOS6_STAGE39_XNU high_kc_handoff_section_count=0x000005e5
MI4IOS6_STAGE39_XNU high_kc_handoff_l1_table_phys=0x00030000
MI4IOS6_STAGE39_XNU high_kc_handoff_l1_table_virt=0xc0030000
MI4IOS6_STAGE39_XNU high_kc_handoff_allocation_tag=0x504d4150
MI4IOS6_STAGE39_XNU high_kc_handoff_satisfied_mask=0x0000003f
MI4IOS6_STAGE39_XNU high_kc_handoff_checksum=0xce451213
MI4IOS6_STAGE39_XNU high_kc_handoff_status=0x39000001
MI4IOS6_STAGE39_XNU high_bootstrap_checksum=0xbfb63ae2
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_collection_handoff_phys=0x0002c188
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_collection_handoff_virt=0xc002c188
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_collection_handoff_version_id=0x00000001
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_collection_handoff_size_id=0x0000005c
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_collection_handoff_required_mask_id=0x0000003f
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_collection_handoff_satisfied_mask_id=0x0000003f
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_collection_handoff_checksum_id=0xce451213
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_collection_handoff_status_id=0x39000001
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_object_table_status_id=0x39000001
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_object_table_checksum_id=0xa71ac796
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_entry_count_id=0x00000007
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_required_object_mask_id=0x0000007f
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_observed_object_mask_id=0x0000007f
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_boot_args_virt_id=0xc0034000
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_device_tree_virt_id=0xc0034140
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_pe_state_virt_id=0xc0029020
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_vm_plan_virt_id=0xc002c000
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_vm_state_virt_id=0xc002c04c
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_allocator_virt_id=0xc002c09c
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_pmap_workspace_virt_id=0xc002c0e4
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_workspace_base_id=0x80000000
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_workspace_limit_id=0x80100000
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_section_count_id=0x000005e5
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_l1_table_phys_id=0x00030000
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_l1_table_virt_id=0xc0030000
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_allocation_tag_id=0x504d4150
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_satisfied_mask_id=0x0000003f
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_checksum_id=0xce451213
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_status_id=0x39000001
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_collection_handoff_status_alias=0x39000001
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_collection_handoff_checksum_alias=0xce451213
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_status_alias=0x39000001
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_checksum_alias=0xce451213
MI4IOS6_STAGE39_XNU mmu_high_bootstrap_kc_handoff_observed_mask_alias=0x0000007f
MI4IOS6_STAGE39_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the collection-handoff-gated root path:

```text
MI4IOS6_STAGE39_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the collection-handoff-gated root path:

```text
MI4IOS6_STAGE39_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE39_XNU kernel_entry ok
MI4IOS6_STAGE39 kernel_entry returned success
MI4IOS6_STAGE39 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage39 turns the Stage38 kernel object table into an explicit kernel collection handoff descriptor. It still keeps the actual MMU policy conservative: identity mappings remain, high aliases are still section mappings, and caches remain disabled. The useful step is that later stages now have a typed handoff object that can be consumed as an early kernel collection contract, with high-virtual object slots and workspace/L1 facts validated independently from the object table that produced them.

This is still an XNU-adjacent kernel skeleton, not a booting XNU kernel. The handoff descriptor is a staging contract for later kernel collection entry-table validation, bootstrap object graph construction, and higher-level kernel bootstrap work.

## Success criteria — met

1. bootloader accepted `stage39-qcdt.img`: yes
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
12. object table accepted pmap workspace status/checksum (`0x39000001` / `0xce451213`): yes
13. handoff accepted object table status/checksum (`0x39000001` / `0xa71ac796`): yes
14. handoff entry count matched (`0x00000007`): yes
15. required/observed object mask matched (`0x0000007f`): yes
16. all seven high-virtual object slots matched: yes
17. workspace base/limit matched (`0x80000000` / `0x80100000`): yes
18. section count matched (`0x000005e5`): yes
19. L1 table addresses matched (`0x00030000` / `0xc0030000`): yes
20. allocation tag matched (`0x504d4150`): yes
21. collection handoff satisfied mask was complete (`0x0000003f`): yes
22. collection handoff checksum matched (`0xce451213`): yes
23. collection handoff status was `0x39000001`: yes
24. full root step mask was complete (`0x03ffffff`): yes
25. high root returned status `0x39000001`: yes
26. checksum/status/object mask matched through identity and alias views: yes
27. SGI and timer IRQ paths still worked after high root: yes
28. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage40 can make the kernel collection handoff drive a minimal kernel collection entry table:

- keep identity/recovery mappings and caches disabled,
- keep existing startup-entry/callout/context/VM-plan/VM-state/allocator/pmap-workspace/object-table/collection-handoff checks,
- add a versioned kernel collection entry-table object consuming the handoff object slots,
- validate entry count, object mask, entry ordering, boot/platform/VM/pmap pointer classes, checksum/status, and identity/high-alias views,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
