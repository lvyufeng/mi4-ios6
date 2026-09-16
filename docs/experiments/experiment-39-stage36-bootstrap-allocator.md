# Experiment 39 — Stage36 Bootstrap Allocator Descriptor

Date: 2026-06-05

Goal: make the VM bootstrap state drive a minimal bootstrap allocator descriptor.

Stage36 still does **not** run XNU or iOS. It extends Stage35 by adding a versioned bootstrap allocator descriptor after the VM bootstrap state has been populated and accepted. The descriptor consumes the VM state's bootstrap allocation span, records the allocator span, initial/current cursor, remaining bytes, first allocation metadata, alignment policy, checksum, and status, then validates those facts through identity and high-alias views before final root success.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage36 adds over Stage35

Stage35 proved:

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
- VM state required/satisfied mask `0x0000003f`,
- VM plan checksum/status `0xea540a8a` / `0x35000001`,
- VM state checksum/status `0x1f4506e6` / `0x35000001`,
- root step mask `0x003fffff`,
- final root status `0x35000001`.

Stage36 adds a bootstrap allocator descriptor object:

- allocator version: `1`,
- allocator size: `0x00000048`,
- allocator required/satisfied mask: `0x0000003f`,
- accepted VM state status: `0x36000001`,
- accepted VM state checksum: `0x1f4506e6`,
- allocator span base/size/end: `0x80000000` / `0x00100000` / `0x80100000`,
- initial cursor: `0x80000000`,
- current cursor: `0x80100000`,
- remaining bytes: `0x00000000`,
- first allocation base/size/end: `0x80000000` / `0x00100000` / `0x80100000`,
- first allocation tag: `0x414c4c43` (`ALLC`),
- alignment policy: `0x00001000`,
- allocator checksum: `0x68195ad2`,
- allocator status: `0x36000001`,
- full root step mask extends to `0x007fffff`,
- final root status `0x36000001`.

Allocator satisfied bits:

```text
0x00000001 accepted VM state status/checksum satisfied
0x00000002 allocator span base/size/end satisfied
0x00000004 initial/current cursor satisfied
0x00000008 remaining-byte accounting satisfied
0x00000010 first allocation metadata satisfied
0x00000020 alignment policy satisfied
```

Complete allocator satisfied mask: `0x0000003f`.

The new root-step bit records that the allocator descriptor was accepted:

```text
Stage35 final root steps: 0x003fffff
Stage36 final root steps: 0x007fffff
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
```

Complete root-step mask: `0x007fffff`.

## Built image

```bash
./stage36/build.sh
```

Successful local build:

```text
out/stage36/stage36-qcdt.img
sha256=7e01e35cab57c16e1ab0bd539d6ed3cab29b6c8c7e8aeccca3f752d6caffa5fa
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=90904 (0x16318)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage36 mi4ios6=stage36 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage36_vectors
0000ab68 t stage36_kernel_root
0000f4f0 t stage36_startup_entry
0001065c T mmu_high_bootstrap_selftest
00013bbc T kernel_entry
00013ee4 T test_kernel_entry
0001407c T stage36_main
00028000 b stage36_vm_plan_block
0002804c b stage36_vm_state_block
0002809c b stage36_boot_allocator_block
000280e4 b stage36_startup_handoff_block
00028114 b stage36_kernel_context_block
00028164 b stage36_bootstrap_state_block
0002c000 b stage36_l1_table
00032000 B __stage36_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage36/stage36-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2554 KB)                       OKAY [  0.081s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.091s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage36-last_kmsg.txt
```

The recovered log was 60308 bytes and contained:

```text
946 MI4IOS6_STAGE36 markers
920 MI4IOS6_STAGE36_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE36_XNU high_vm_plan_virt=0xc0028000
MI4IOS6_STAGE36_XNU high_vm_state_virt=0xc002804c
MI4IOS6_STAGE36_XNU high_allocator_virt=0xc002809c
MI4IOS6_STAGE36_XNU high root vm bootstrap plan begin
MI4IOS6_STAGE36_XNU high root vm bootstrap plan ok
MI4IOS6_STAGE36_XNU high root vm bootstrap state begin
MI4IOS6_STAGE36_XNU high root vm bootstrap state ok
MI4IOS6_STAGE36_XNU high root bootstrap allocator begin
MI4IOS6_STAGE36_XNU high root bootstrap allocator ok
MI4IOS6_STAGE36_XNU high_root_steps=0x007fffff
MI4IOS6_STAGE36_XNU high_root_status=0x36000001
```

VM plan/state markers:

```text
MI4IOS6_STAGE36_XNU high_vm_plan_required_mask=0x0000007f
MI4IOS6_STAGE36_XNU high_vm_plan_satisfied_mask=0x0000007f
MI4IOS6_STAGE36_XNU high_vm_plan_checksum=0xe9540a8a
MI4IOS6_STAGE36_XNU high_vm_plan_status=0x36000001
MI4IOS6_STAGE36_XNU high_vm_state_required_mask=0x0000003f
MI4IOS6_STAGE36_XNU high_vm_state_satisfied_mask=0x0000003f
MI4IOS6_STAGE36_XNU high_vm_state_checksum=0x1f4506e6
MI4IOS6_STAGE36_XNU high_vm_state_status=0x36000001
```

Bootstrap allocator markers:

```text
MI4IOS6_STAGE36_XNU high_allocator_version=0x00000001
MI4IOS6_STAGE36_XNU high_allocator_size=0x00000048
MI4IOS6_STAGE36_XNU high_allocator_required_mask=0x0000003f
MI4IOS6_STAGE36_XNU high_allocator_satisfied_mask=0x0000003f
MI4IOS6_STAGE36_XNU high_allocator_checksum=0x68195ad2
MI4IOS6_STAGE36_XNU high_allocator_status=0x36000001
MI4IOS6_STAGE36_XNU high_allocdesc_vm_state_status=0x36000001
MI4IOS6_STAGE36_XNU high_allocdesc_vm_state_checksum=0x1f4506e6
MI4IOS6_STAGE36_XNU high_allocdesc_span_base=0x80000000
MI4IOS6_STAGE36_XNU high_allocdesc_span_size=0x00100000
MI4IOS6_STAGE36_XNU high_allocdesc_span_end=0x80100000
MI4IOS6_STAGE36_XNU high_allocdesc_initial_cursor=0x80000000
MI4IOS6_STAGE36_XNU high_allocdesc_current_cursor=0x80100000
MI4IOS6_STAGE36_XNU high_allocdesc_remaining_bytes=0x00000000
MI4IOS6_STAGE36_XNU high_allocdesc_first_alloc_base=0x80000000
MI4IOS6_STAGE36_XNU high_allocdesc_first_alloc_size=0x00100000
MI4IOS6_STAGE36_XNU high_allocdesc_first_alloc_end=0x80100000
MI4IOS6_STAGE36_XNU high_allocdesc_first_alloc_tag=0x414c4c43
MI4IOS6_STAGE36_XNU high_allocdesc_alignment=0x00001000
MI4IOS6_STAGE36_XNU high_allocdesc_satisfied_mask=0x0000003f
MI4IOS6_STAGE36_XNU high_allocdesc_checksum=0x68195ad2
MI4IOS6_STAGE36_XNU high_allocdesc_status=0x36000001
MI4IOS6_STAGE36_XNU high_bootstrap_checksum=0x8e029de4
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocator_phys=0x0002809c
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocator_virt=0xc002809c
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocator_version_id=0x00000001
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocator_size_id=0x00000048
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocator_required_mask_id=0x0000003f
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocator_satisfied_mask_id=0x0000003f
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocator_checksum_id=0x68195ad2
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocator_status_id=0x36000001
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_vm_state_status_id=0x36000001
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_vm_state_checksum_id=0x1f4506e6
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_span_base_id=0x80000000
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_span_size_id=0x00100000
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_span_end_id=0x80100000
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_current_cursor_id=0x80100000
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_remaining_bytes_id=0x00000000
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_first_alloc_base_id=0x80000000
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_first_alloc_size_id=0x00100000
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_first_alloc_end_id=0x80100000
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_first_alloc_tag_id=0x414c4c43
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_alignment_id=0x00001000
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_satisfied_mask_id=0x0000003f
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_checksum_id=0x68195ad2
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_status_id=0x36000001
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocator_status_alias=0x36000001
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocator_checksum_alias=0x68195ad2
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_status_alias=0x36000001
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_checksum_alias=0x68195ad2
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_current_cursor_alias=0x80100000
MI4IOS6_STAGE36_XNU mmu_high_bootstrap_allocdesc_remaining_alias=0x00000000
MI4IOS6_STAGE36_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the allocator-gated root path:

```text
MI4IOS6_STAGE36_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the allocator-gated root path:

```text
MI4IOS6_STAGE36_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE36_XNU kernel_entry ok
MI4IOS6_STAGE36 kernel_entry returned success
MI4IOS6_STAGE36 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage36 turns the Stage35 VM bootstrap state into an explicit allocator descriptor. It still keeps the actual MMU policy conservative: identity mappings remain, high aliases are still section mappings, and caches remain disabled. The useful step is that later stages now have a typed allocator contract for span ownership, cursor state, first allocation metadata, remaining-byte accounting, and alignment policy.

This is still an XNU-adjacent kernel skeleton, not a booting XNU kernel. The allocator descriptor is a staging contract for later bootstrap allocator workspace, pmap allocation, and kernel object initialization work.

## Success criteria — met

1. bootloader accepted `stage36-qcdt.img`: yes
2. high virtual root entry ran: yes
3. startup entry was called: yes
4. kernel callout table dispatched: yes
5. kernel context object was populated: yes
6. VM plan object was populated: yes
7. VM state object was populated: yes
8. bootstrap allocator descriptor was populated: yes
9. allocator accepted VM state status/checksum (`0x36000001` / `0x1f4506e6`): yes
10. allocator span matched (`0x80000000` / `0x00100000` / `0x80100000`): yes
11. allocator cursor matched (`0x80000000` -> `0x80100000`): yes
12. allocator remaining bytes matched (`0x00000000`): yes
13. allocator first allocation metadata matched (`0x80000000`, `0x00100000`, `0x80100000`, `0x414c4c43`): yes
14. allocator alignment policy matched (`0x00001000`): yes
15. allocator satisfied mask was complete (`0x0000003f`): yes
16. allocator checksum matched (`0x68195ad2`): yes
17. allocator status was `0x36000001`: yes
18. full root step mask was complete (`0x007fffff`): yes
19. high root returned status `0x36000001`: yes
20. checksum/status/cursor matched through identity and alias views: yes
21. SGI and timer IRQ paths still worked after high root: yes
22. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage37 can make the bootstrap allocator descriptor drive a minimal pmap bootstrap workspace descriptor:

- keep identity/recovery mappings and caches disabled,
- keep existing startup-entry/callout/context/VM-plan/VM-state/allocator checks,
- add a versioned pmap bootstrap workspace object consuming allocator span/cursor facts and L1 table facts,
- validate workspace base/limit, section count, L1 table identity/high aliases, allocation tag, checksum/status, and identity/high-alias views,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
