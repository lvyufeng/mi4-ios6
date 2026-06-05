# Experiment 40 — Stage37 Pmap Bootstrap Workspace

Date: 2026-06-05

Goal: make the bootstrap allocator descriptor drive a minimal pmap bootstrap workspace descriptor.

Stage37 still does **not** run XNU or iOS. It extends Stage36 by adding a versioned pmap bootstrap workspace descriptor after the bootstrap allocator descriptor has been populated and accepted. The workspace consumes allocator span/cursor facts plus VM/L1 table facts, records workspace base/limit/size, memory section count, L1 table physical/high-alias addresses, section mapping policy, allocation tag, MMU/cache policy, checksum, and status, then validates those facts through identity and high-alias views before final root success.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage37 adds over Stage36

Stage36 proved:

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
- allocator required/satisfied mask `0x0000003f`,
- allocator checksum/status `0x68195ad2` / `0x36000001`,
- root step mask `0x007fffff`,
- final root status `0x36000001`.

Stage37 adds a pmap bootstrap workspace descriptor object:

- workspace version: `1`,
- workspace size: `0x00000048`,
- workspace required/satisfied mask: `0x0000003f`,
- accepted allocator status: `0x37000001`,
- accepted allocator checksum: `0x69195ad2`,
- workspace base/limit/size: `0x80000000` / `0x80100000` / `0x00100000`,
- memory section count: `0x000005e5`,
- L1 table physical/high-alias address: `0x0002c000` / `0xc002c000`,
- section descriptor/size: `0x00010c02` / `0x00100000`,
- allocation tag: `0x504d4150` (`PMAP`),
- MMU/cache policy: `0x00000001` / `0x00000000`,
- pmap workspace checksum: `0xce451213`,
- pmap workspace status: `0x37000001`,
- full root step mask extends to `0x00ffffff`,
- final root status `0x37000001`.

Pmap workspace satisfied bits:

```text
0x00000001 accepted allocator status/checksum satisfied
0x00000002 workspace base/limit/size range satisfied
0x00000004 L1 table physical/high-alias placement satisfied
0x00000008 memory section count and section policy satisfied
0x00000010 pmap allocation tag satisfied
0x00000020 MMU/cache/alignment policy satisfied
```

Complete pmap workspace satisfied mask: `0x0000003f`.

The new root-step bit records that the pmap workspace descriptor was accepted:

```text
Stage36 final root steps: 0x007fffff
Stage37 final root steps: 0x00ffffff
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
```

Complete root-step mask: `0x00ffffff`.

## Built image

```bash
./stage37/build.sh
```

Successful local build:

```text
out/stage37/stage37-qcdt.img
sha256=2e444b68139a70afe3c24cca8f1d57e3cb64823edd8663e1a3f7f7a2d10aedc3
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=95312 (0x17450)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage37 mi4ios6=stage37 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage37_vectors
0000ab68 t stage37_kernel_root
0000fa08 t stage37_startup_entry
00010b74 T mmu_high_bootstrap_selftest
00014494 T kernel_entry
000147bc T test_kernel_entry
00014954 T stage37_main
00028000 b stage37_vm_plan_block
0002804c b stage37_vm_state_block
0002809c b stage37_boot_allocator_block
000280e4 b stage37_pmap_workspace_block
0002812c b stage37_startup_handoff_block
0002815c b stage37_kernel_context_block
000281ac b stage37_bootstrap_state_block
0002c000 b stage37_l1_table
00032000 B __stage37_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage37/stage37-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2558 KB)                       OKAY [  0.081s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.091s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage37-last_kmsg.txt
```

The recovered log was 63924 bytes and contained:

```text
1000 MI4IOS6_STAGE37 markers
974 MI4IOS6_STAGE37_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE37_XNU high_vm_state_virt=0xc002804c
MI4IOS6_STAGE37_XNU high_allocator_virt=0xc002809c
MI4IOS6_STAGE37_XNU high_pmap_workspace_virt=0xc00280e4
MI4IOS6_STAGE37_XNU high root vm bootstrap plan begin
MI4IOS6_STAGE37_XNU high root vm bootstrap plan ok
MI4IOS6_STAGE37_XNU high root vm bootstrap state begin
MI4IOS6_STAGE37_XNU high root vm bootstrap state ok
MI4IOS6_STAGE37_XNU high root bootstrap allocator begin
MI4IOS6_STAGE37_XNU high root bootstrap allocator ok
MI4IOS6_STAGE37_XNU high root pmap workspace begin
MI4IOS6_STAGE37_XNU high root pmap workspace ok
MI4IOS6_STAGE37_XNU high_root_steps=0x00ffffff
MI4IOS6_STAGE37_XNU high_root_status=0x37000001
```

Allocator and pmap workspace state markers:

```text
MI4IOS6_STAGE37_XNU high_allocator_required_mask=0x0000003f
MI4IOS6_STAGE37_XNU high_allocator_satisfied_mask=0x0000003f
MI4IOS6_STAGE37_XNU high_allocator_checksum=0x69195ad2
MI4IOS6_STAGE37_XNU high_allocator_status=0x37000001
MI4IOS6_STAGE37_XNU high_pmap_workspace_version=0x00000001
MI4IOS6_STAGE37_XNU high_pmap_workspace_size=0x00000048
MI4IOS6_STAGE37_XNU high_pmap_workspace_required_mask=0x0000003f
MI4IOS6_STAGE37_XNU high_pmap_workspace_satisfied_mask=0x0000003f
MI4IOS6_STAGE37_XNU high_pmap_workspace_checksum=0xce451213
MI4IOS6_STAGE37_XNU high_pmap_workspace_status=0x37000001
```

Pmap workspace descriptor markers:

```text
MI4IOS6_STAGE37_XNU high_pmapws_allocator_status=0x37000001
MI4IOS6_STAGE37_XNU high_pmapws_allocator_checksum=0x69195ad2
MI4IOS6_STAGE37_XNU high_pmapws_workspace_base=0x80000000
MI4IOS6_STAGE37_XNU high_pmapws_workspace_limit=0x80100000
MI4IOS6_STAGE37_XNU high_pmapws_workspace_size=0x00100000
MI4IOS6_STAGE37_XNU high_pmapws_section_count=0x000005e5
MI4IOS6_STAGE37_XNU high_pmapws_l1_table_phys=0x0002c000
MI4IOS6_STAGE37_XNU high_pmapws_l1_table_virt=0xc002c000
MI4IOS6_STAGE37_XNU high_pmapws_l1_section_descriptor=0x00010c02
MI4IOS6_STAGE37_XNU high_pmapws_l1_section_size=0x00100000
MI4IOS6_STAGE37_XNU high_pmapws_allocation_tag=0x504d4150
MI4IOS6_STAGE37_XNU high_pmapws_mmu_enabled=0x00000001
MI4IOS6_STAGE37_XNU high_pmapws_cache_policy=0x00000000
MI4IOS6_STAGE37_XNU high_pmapws_satisfied_mask=0x0000003f
MI4IOS6_STAGE37_XNU high_pmapws_checksum=0xce451213
MI4IOS6_STAGE37_XNU high_pmapws_status=0x37000001
MI4IOS6_STAGE37_XNU high_bootstrap_checksum=0x76c78ed7
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmap_workspace_phys=0x000280e4
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmap_workspace_virt=0xc00280e4
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmap_workspace_version_id=0x00000001
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmap_workspace_size_id=0x00000048
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmap_workspace_required_mask_id=0x0000003f
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmap_workspace_satisfied_mask_id=0x0000003f
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmap_workspace_checksum_id=0xce451213
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmap_workspace_status_id=0x37000001
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_allocator_status_id=0x37000001
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_allocator_checksum_id=0x69195ad2
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_workspace_base_id=0x80000000
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_workspace_limit_id=0x80100000
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_workspace_size_id=0x00100000
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_section_count_id=0x000005e5
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_l1_table_phys_id=0x0002c000
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_l1_table_virt_id=0xc002c000
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_l1_section_descriptor_id=0x00010c02
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_l1_section_size_id=0x00100000
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_allocation_tag_id=0x504d4150
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_mmu_enabled_id=0x00000001
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_cache_policy_id=0x00000000
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_satisfied_mask_id=0x0000003f
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_checksum_id=0xce451213
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_status_id=0x37000001
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmap_workspace_status_alias=0x37000001
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmap_workspace_checksum_alias=0xce451213
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_status_alias=0x37000001
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_checksum_alias=0xce451213
MI4IOS6_STAGE37_XNU mmu_high_bootstrap_pmapws_l1_table_virt_alias=0xc002c000
MI4IOS6_STAGE37_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the pmap-workspace-gated root path:

```text
MI4IOS6_STAGE37_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the pmap-workspace-gated root path:

```text
MI4IOS6_STAGE37_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE37_XNU kernel_entry ok
MI4IOS6_STAGE37 kernel_entry returned success
MI4IOS6_STAGE37 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage37 turns the Stage36 allocator descriptor into an explicit pmap bootstrap workspace descriptor. It still keeps the actual MMU policy conservative: identity mappings remain, high aliases are still section mappings, and caches remain disabled. The useful step is that later stages now have a typed pmap workspace contract for early memory span ownership, section count, L1 table physical/high-alias placement, section descriptor policy, allocation tag, and MMU/cache policy.

This is still an XNU-adjacent kernel skeleton, not a booting XNU kernel. The pmap workspace descriptor is a staging contract for later pmap bootstrap accounting, page table workspace allocation, and higher-level kernel object initialization work.

## Success criteria — met

1. bootloader accepted `stage37-qcdt.img`: yes
2. high virtual root entry ran: yes
3. startup entry was called: yes
4. kernel callout table dispatched: yes
5. kernel context object was populated: yes
6. VM plan object was populated: yes
7. VM state object was populated: yes
8. bootstrap allocator descriptor was populated: yes
9. pmap workspace descriptor was populated: yes
10. pmap workspace accepted allocator status/checksum (`0x37000001` / `0x69195ad2`): yes
11. workspace range matched (`0x80000000` / `0x80100000` / `0x00100000`): yes
12. memory section count matched (`0x000005e5`): yes
13. L1 table addresses matched (`0x0002c000` / `0xc002c000`): yes
14. section policy matched (`0x00010c02` / `0x00100000`): yes
15. allocation tag matched (`0x504d4150`): yes
16. MMU/cache policy matched (`0x00000001` / `0x00000000`): yes
17. pmap workspace satisfied mask was complete (`0x0000003f`): yes
18. pmap workspace checksum matched (`0xce451213`): yes
19. pmap workspace status was `0x37000001`: yes
20. full root step mask was complete (`0x00ffffff`): yes
21. high root returned status `0x37000001`: yes
22. checksum/status/L1 high alias matched through identity and alias views: yes
23. SGI and timer IRQ paths still worked after high root: yes
24. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage38 can make the pmap workspace descriptor drive a minimal kernel object table descriptor:

- keep identity/recovery mappings and caches disabled,
- keep existing startup-entry/callout/context/VM-plan/VM-state/allocator/pmap-workspace checks,
- add a versioned kernel object table object consuming workspace span, L1 table, section count, and allocator tag facts,
- validate object slots for boot args, device tree, PE state, VM plan, VM state, allocator, and pmap workspace,
- validate checksum/status and identity/high-alias views,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
