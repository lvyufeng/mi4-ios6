# Experiment 38 — Stage35 VM Bootstrap State

Date: 2026-06-05

Goal: make the accepted VM bootstrap plan drive a minimal VM bootstrap state object.

Stage35 still does **not** run XNU or iOS. It extends Stage34 by adding a versioned VM bootstrap state object after the VM bootstrap plan has been populated and accepted. The state records the accepted plan status/checksum, the early kernel-map span, the first available-memory cursor, a first bootstrap allocation span, pmap-like section mapping policy, the L1 table location, MMU state, and cache policy, then validates those facts through identity and high-alias views before final root success.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage35 adds over Stage34

Stage34 proved:

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
- context required/satisfied mask `0x0000003f`,
- context checksum `0x012506ba`,
- VM bootstrap plan object,
- VM plan required/satisfied mask `0x0000007f`,
- VM plan status `0x34000001`,
- root step mask `0x001fffff`,
- final root status `0x34000001`.

Stage35 adds a VM bootstrap state object:

- VM state version: `1`,
- VM state size: `0x00000050`,
- VM state required/satisfied mask: `0x0000003f`,
- accepted VM plan status: `0x35000001`,
- accepted VM plan checksum: `0xea540a8a`,
- kernel map base/limit: `0xc0000000` / `0xc0100000`,
- available memory base/cursor: `0x80000000` / `0x80100000`,
- first bootstrap allocation span: `0x80000000` + `0x00100000` -> `0x80100000`,
- pmap section size: `0x00100000`,
- pmap section descriptor policy: `0x00010c02`,
- pmap L1 table physical/high-alias address: `0x0002c000` / `0xc002c000`,
- MMU enabled: `0x00000001`,
- cache policy: `0x00000000` (I-cache/D-cache disabled),
- VM state checksum: `0x1f4506e6`,
- VM state status: `0x35000001`,
- full root step mask extends to `0x003fffff`,
- final root status `0x35000001`.

VM state satisfied bits:

```text
0x00000001 accepted VM plan status/checksum satisfied
0x00000002 kernel map base/limit satisfied
0x00000004 available memory cursor satisfied
0x00000008 first bootstrap allocation span satisfied
0x00000010 pmap section/L1-table policy satisfied
0x00000020 MMU/cache policy satisfied
```

Complete VM state satisfied mask: `0x0000003f`.

The new root-step bit records that the VM bootstrap state was accepted:

```text
Stage34 final root steps: 0x001fffff
Stage35 final root steps: 0x003fffff
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
```

Complete root-step mask: `0x003fffff`.

## Built image

```bash
./stage35/build.sh
```

Successful local build:

```text
out/stage35/stage35-qcdt.img
sha256=ee6cbfb2295ebc6c8b8ef50fc5da20eb4f0cf62b631fcdc7f9d76e1a24f7deb9
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=86176 (0x150a0)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage35 mi4ios6=stage35 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage35_vectors
0000ab68 t stage35_kernel_root
0000ef04 t stage35_startup_entry
00010070 T mmu_high_bootstrap_selftest
000131dc T kernel_entry
00013504 T test_kernel_entry
0001369c T stage35_main
00028000 b stage35_vm_plan_block
0002804c b stage35_vm_state_block
0002809c b stage35_startup_handoff_block
000280cc b stage35_kernel_context_block
0002811c b stage35_bootstrap_state_block
0002c000 b stage35_l1_table
00032000 B __stage35_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage35/stage35-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2550 KB)                       OKAY [  0.081s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.091s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage35-last_kmsg.txt
```

The recovered log was 56619 bytes and contained:

```text
891 MI4IOS6_STAGE35 markers
865 MI4IOS6_STAGE35_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE35_XNU high_vm_plan_virt=0xc0028000
MI4IOS6_STAGE35_XNU high_vm_state_virt=0xc002804c
MI4IOS6_STAGE35_XNU high root vm bootstrap plan begin
MI4IOS6_STAGE35_XNU high root vm bootstrap plan ok
MI4IOS6_STAGE35_XNU high root vm bootstrap state begin
MI4IOS6_STAGE35_XNU high root vm bootstrap state ok
MI4IOS6_STAGE35_XNU high_root_steps=0x003fffff
MI4IOS6_STAGE35_XNU high_root_status=0x35000001
```

VM plan state markers:

```text
MI4IOS6_STAGE35_XNU high_vm_plan_version=0x00000001
MI4IOS6_STAGE35_XNU high_vm_plan_size=0x0000004c
MI4IOS6_STAGE35_XNU high_vm_plan_required_mask=0x0000007f
MI4IOS6_STAGE35_XNU high_vm_plan_satisfied_mask=0x0000007f
MI4IOS6_STAGE35_XNU high_vm_plan_checksum=0xea540a8a
MI4IOS6_STAGE35_XNU high_vm_plan_status=0x35000001
```

VM bootstrap state markers:

```text
MI4IOS6_STAGE35_XNU high_vm_state_version=0x00000001
MI4IOS6_STAGE35_XNU high_vm_state_size=0x00000050
MI4IOS6_STAGE35_XNU high_vm_state_required_mask=0x0000003f
MI4IOS6_STAGE35_XNU high_vm_state_satisfied_mask=0x0000003f
MI4IOS6_STAGE35_XNU high_vm_state_checksum=0x1f4506e6
MI4IOS6_STAGE35_XNU high_vm_state_status=0x35000001
MI4IOS6_STAGE35_XNU high_vmstate_plan_status=0x35000001
MI4IOS6_STAGE35_XNU high_vmstate_plan_checksum=0xea540a8a
MI4IOS6_STAGE35_XNU high_vmstate_kernel_map_base=0xc0000000
MI4IOS6_STAGE35_XNU high_vmstate_kernel_map_limit=0xc0100000
MI4IOS6_STAGE35_XNU high_vmstate_available_memory_base=0x80000000
MI4IOS6_STAGE35_XNU high_vmstate_available_memory_cursor=0x80100000
MI4IOS6_STAGE35_XNU high_vmstate_bootstrap_alloc_base=0x80000000
MI4IOS6_STAGE35_XNU high_vmstate_bootstrap_alloc_size=0x00100000
MI4IOS6_STAGE35_XNU high_vmstate_bootstrap_alloc_end=0x80100000
MI4IOS6_STAGE35_XNU high_vmstate_pmap_section_size=0x00100000
MI4IOS6_STAGE35_XNU high_vmstate_pmap_section_descriptor=0x00010c02
MI4IOS6_STAGE35_XNU high_vmstate_pmap_l1_table_phys=0x0002c000
MI4IOS6_STAGE35_XNU high_vmstate_pmap_l1_table_virt=0xc002c000
MI4IOS6_STAGE35_XNU high_vmstate_mmu_enabled=0x00000001
MI4IOS6_STAGE35_XNU high_vmstate_cache_policy=0x00000000
MI4IOS6_STAGE35_XNU high_vmstate_satisfied_mask=0x0000003f
MI4IOS6_STAGE35_XNU high_vmstate_checksum=0x1f4506e6
MI4IOS6_STAGE35_XNU high_vmstate_status=0x35000001
MI4IOS6_STAGE35_XNU high_bootstrap_checksum=0xd35bc465
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vm_state_phys=0x0002804c
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vm_state_virt=0xc002804c
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vm_state_version_id=0x00000001
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vm_state_size_id=0x00000050
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vm_state_required_mask_id=0x0000003f
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vm_state_satisfied_mask_id=0x0000003f
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vm_state_checksum_id=0x1f4506e6
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vm_state_status_id=0x35000001
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_plan_status_id=0x35000001
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_plan_checksum_id=0xea540a8a
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_kernel_map_base_id=0xc0000000
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_kernel_map_limit_id=0xc0100000
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_available_memory_base_id=0x80000000
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_available_memory_cursor_id=0x80100000
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_bootstrap_alloc_base_id=0x80000000
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_bootstrap_alloc_size_id=0x00100000
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_bootstrap_alloc_end_id=0x80100000
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_pmap_l1_table_phys_id=0x0002c000
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_pmap_l1_table_virt_id=0xc002c000
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_mmu_enabled_id=0x00000001
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_cache_policy_id=0x00000000
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_satisfied_mask_id=0x0000003f
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_checksum_id=0x1f4506e6
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_status_id=0x35000001
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vm_state_status_alias=0x35000001
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vm_state_checksum_alias=0x1f4506e6
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_status_alias=0x35000001
MI4IOS6_STAGE35_XNU mmu_high_bootstrap_vmstate_checksum_alias=0x1f4506e6
MI4IOS6_STAGE35_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the VM-state-gated root path:

```text
MI4IOS6_STAGE35_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the VM-state-gated root path:

```text
MI4IOS6_STAGE35_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE35_XNU kernel_entry ok
MI4IOS6_STAGE35 kernel_entry returned success
MI4IOS6_STAGE35 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage35 turns the Stage34 VM bootstrap plan into a concrete early VM bootstrap state. It still keeps the actual MMU policy conservative: identity mappings remain, high aliases are still section mappings, and caches remain disabled. The useful step is that later stages now have a typed state object for the early kernel map, first bootstrap allocation, available-memory cursor, and pmap-like section policy instead of relying on implicit root-path constants.

This is still an XNU-adjacent kernel skeleton, not a booting XNU kernel. The VM state is a staging contract for later bootstrap allocator and pmap initialization work.

## Success criteria — met

1. bootloader accepted `stage35-qcdt.img`: yes
2. high virtual root entry ran: yes
3. startup entry was called: yes
4. kernel callout table dispatched: yes
5. kernel context object was populated: yes
6. VM plan object was populated: yes
7. VM state object was populated: yes
8. VM plan status/checksum matched (`0x35000001` / `0xea540a8a`): yes
9. VM state kernel map matched (`0xc0000000` / `0xc0100000`): yes
10. VM state available-memory cursor matched (`0x80000000` -> `0x80100000`): yes
11. VM state bootstrap allocation span matched (`0x80000000` + `0x00100000`): yes
12. VM state pmap section policy matched (`0x00100000` / `0x00010c02`): yes
13. VM state L1 table addresses matched (`0x0002c000` / `0xc002c000`): yes
14. VM state MMU/cache policy matched (`0x00000001` / `0x00000000`): yes
15. VM state satisfied mask was complete (`0x0000003f`): yes
16. VM state checksum matched (`0x1f4506e6`): yes
17. VM state status was `0x35000001`: yes
18. full root step mask was complete (`0x003fffff`): yes
19. high root returned status `0x35000001`: yes
20. checksum/status matched through identity and alias views: yes
21. SGI and timer IRQ paths still worked after high root: yes
22. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage36 can make the VM bootstrap state drive a minimal bootstrap allocator descriptor:

- keep identity/recovery mappings and caches disabled,
- keep existing startup-entry/callout/context/VM-plan/VM-state checks,
- add a versioned bootstrap allocator object consuming the VM state's allocation base, size, end, and cursor,
- validate allocator current cursor, remaining bytes, first allocation metadata, checksum/status, and identity/high-alias views,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
