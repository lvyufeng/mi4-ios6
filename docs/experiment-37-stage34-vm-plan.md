# Experiment 37 — Stage34 VM Bootstrap Plan

Date: 2026-06-05

Goal: make the kernel-start context produce a minimal VM bootstrap plan object.

Stage34 still does **not** run XNU or iOS. It extends Stage33 by adding a versioned VM bootstrap plan object after the startup-entry callouts have populated the kernel-start context. The plan records the current identity/high-alias VM shape, L1 table location, memory range, section mapping policy, MMU state, and cache policy, then validates those facts through identity and high-alias views before final root success.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- Identity mappings remain for recovery/debug paths.
- High aliases remain conservative 1 MiB sections.
- Caches remain disabled.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun after the high-virtual root path.

## What Stage34 adds over Stage33

Stage33 proved:

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
- context status `0x33000001`,
- root step mask `0x000fffff`,
- final root status `0x33000001`.

Stage34 adds a VM bootstrap plan object:

- VM plan version: `1`,
- VM plan size: `0x0000004c`,
- VM plan required/satisfied mask: `0x0000007f`,
- context status: `0x34000001`,
- context checksum: `0x012506ba`,
- low identity base: `0x00000000`,
- high alias base: `0xc0000000`,
- ram_console alias base: `0xc0100000`,
- GIC/timer alias base: `0xc0200000`,
- L1 table physical address: `0x00028000`,
- L1 table high alias: `0xc0028000`,
- memory base: `0x80000000`,
- memory size: `0x5e500000`,
- section size: `0x00100000`,
- section descriptor policy: `0x00010c02`,
- MMU enabled: `0x00000001`,
- cache policy: `0x00000000` (I-cache/D-cache disabled),
- VM plan checksum: `0xeb540a8a`,
- VM plan status: `0x34000001`,
- full root step mask extends to `0x001fffff`,
- final root status `0x34000001`.

VM plan satisfied bits:

```text
0x00000001 context status/checksum satisfied
0x00000002 memory range satisfied
0x00000004 L1 table physical/high-alias placement satisfied
0x00000008 identity/high/MMIO alias bases satisfied
0x00000010 MMU enabled satisfied
0x00000020 caches-disabled policy satisfied
0x00000040 section mapping policy satisfied
```

Complete VM plan satisfied mask: `0x0000007f`.

The new root-step bit records that the VM plan was accepted:

```text
Stage33 final root steps: 0x000fffff
Stage34 final root steps: 0x001fffff
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
```

Complete root-step mask: `0x001fffff`.

## Built image

```bash
./stage34/build.sh
```

Successful local build:

```text
out/stage34/stage34-qcdt.img
sha256=c7fa1676431cf73cda85a7c4e96cbdd8b309588430dcbe0df0473720fd5fa8ac
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=80848 (0x13bd0)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage34 mi4ios6=stage34 c-runtime apple-dt
```

Important symbols:

```text
000080a0 T stage34_vectors
0000ab68 t stage34_kernel_root
0000e8bc t stage34_startup_entry
0000f9d8 T mmu_high_bootstrap_selftest
000125f0 T kernel_entry
00012918 T test_kernel_entry
00012ab0 T stage34_main
00024000 b stage34_vm_plan_block
0002404c b stage34_startup_handoff_block
0002407c b stage34_kernel_context_block
000240cc b stage34_bootstrap_state_block
00028000 b stage34_l1_table
0002e000 B __stage34_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage34/stage34-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2544 KB)                       OKAY [  0.081s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.091s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage34-last_kmsg.txt
```

The recovered log was 52790 bytes and contained:

```text
834 MI4IOS6_STAGE34 markers
808 MI4IOS6_STAGE34_XNU markers
```

## Key recovered high-root markers

```text
MI4IOS6_STAGE34_XNU high startup_entry kernel callout table ok
MI4IOS6_STAGE34_XNU high startup_entry kernel context ok
MI4IOS6_STAGE34_XNU high root startup entry ok
MI4IOS6_STAGE34_XNU high root kernel callout table ok
MI4IOS6_STAGE34_XNU high root kernel context ok
MI4IOS6_STAGE34_XNU high root vm bootstrap plan begin
MI4IOS6_STAGE34_XNU high root vm bootstrap plan ok
MI4IOS6_STAGE34_XNU high_root_steps=0x001fffff
MI4IOS6_STAGE34_XNU high_root_status=0x34000001
```

VM plan state markers:

```text
MI4IOS6_STAGE34_XNU high_vm_plan_version=0x00000001
MI4IOS6_STAGE34_XNU high_vm_plan_size=0x0000004c
MI4IOS6_STAGE34_XNU high_vm_plan_required_mask=0x0000007f
MI4IOS6_STAGE34_XNU high_vm_plan_satisfied_mask=0x0000007f
MI4IOS6_STAGE34_XNU high_vm_plan_checksum=0xeb540a8a
MI4IOS6_STAGE34_XNU high_vm_plan_status=0x34000001
MI4IOS6_STAGE34_XNU high_plan_context_status=0x34000001
MI4IOS6_STAGE34_XNU high_plan_context_checksum=0x012506ba
MI4IOS6_STAGE34_XNU high_plan_low_identity_base=0x00000000
MI4IOS6_STAGE34_XNU high_plan_high_alias_base=0xc0000000
MI4IOS6_STAGE34_XNU high_plan_ram_console_alias_base=0xc0100000
MI4IOS6_STAGE34_XNU high_plan_gic_alias_base=0xc0200000
MI4IOS6_STAGE34_XNU high_plan_l1_table_phys=0x00028000
MI4IOS6_STAGE34_XNU high_plan_l1_table_virt=0xc0028000
MI4IOS6_STAGE34_XNU high_plan_memory_base=0x80000000
MI4IOS6_STAGE34_XNU high_plan_memory_size=0x5e500000
MI4IOS6_STAGE34_XNU high_plan_section_size=0x00100000
MI4IOS6_STAGE34_XNU high_plan_section_descriptor=0x00010c02
MI4IOS6_STAGE34_XNU high_plan_mmu_enabled=0x00000001
MI4IOS6_STAGE34_XNU high_plan_cache_policy=0x00000000
MI4IOS6_STAGE34_XNU high_plan_satisfied_mask=0x0000007f
MI4IOS6_STAGE34_XNU high_plan_checksum=0xeb540a8a
MI4IOS6_STAGE34_XNU high_plan_status=0x34000001
MI4IOS6_STAGE34_XNU high_bootstrap_checksum=0xf93ec3fb
```

Identity/alias verification after returning from high virtual execution:

```text
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_vm_plan_phys=0x00024000
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_vm_plan_virt=0xc0024000
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_vm_plan_version_id=0x00000001
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_vm_plan_size_id=0x0000004c
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_vm_plan_required_mask_id=0x0000007f
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_vm_plan_satisfied_mask_id=0x0000007f
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_vm_plan_checksum_id=0xeb540a8a
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_vm_plan_status_id=0x34000001
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_plan_context_status_id=0x34000001
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_plan_context_checksum_id=0x012506ba
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_plan_l1_table_phys_id=0x00028000
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_plan_l1_table_virt_id=0xc0028000
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_plan_memory_base_id=0x80000000
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_plan_memory_size_id=0x5e500000
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_plan_mmu_enabled_id=0x00000001
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_plan_cache_policy_id=0x00000000
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_plan_satisfied_mask_id=0x0000007f
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_plan_checksum_id=0xeb540a8a
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_plan_status_id=0x34000001
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_vm_plan_status_alias=0x34000001
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_vm_plan_checksum_alias=0xeb540a8a
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_plan_status_alias=0x34000001
MI4IOS6_STAGE34_XNU mmu_high_bootstrap_plan_checksum_alias=0xeb540a8a
MI4IOS6_STAGE34_XNU mmu high bootstrap selftest ok
```

## Post-root IRQ retests

SGI0 still delivered after the VM-plan-gated root path:

```text
MI4IOS6_STAGE34_XNU gic SGI selftest ok
```

The ARM generic timer PPI still delivered after the VM-plan-gated root path:

```text
MI4IOS6_STAGE34_XNU gic timer selftest ok
```

Final success markers:

```text
MI4IOS6_STAGE34_XNU kernel_entry ok
MI4IOS6_STAGE34 kernel_entry returned success
MI4IOS6_STAGE34 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage34 turns the kernel-start context into an explicit VM bootstrap plan. It still keeps the actual MMU policy conservative: identity mappings remain, the high aliases are still section mappings, and caches remain disabled. The useful step is that later stages now have a typed plan object for the current memory-translation environment instead of relying on implicit constants scattered through the root path.

This is still an XNU-adjacent kernel skeleton, not a booting XNU kernel. The plan is a staging contract for later VM/pmap/bootstrap allocator work.

## Success criteria — met

1. bootloader accepted `stage34-qcdt.img`: yes
2. high virtual root entry ran: yes
3. startup entry was called: yes
4. kernel callout table dispatched: yes
5. kernel context object was populated: yes
6. VM plan object was populated: yes
7. VM plan context status/checksum matched (`0x34000001` / `0x012506ba`): yes
8. VM plan alias bases matched (`0x00000000`, `0xc0000000`, `0xc0100000`, `0xc0200000`): yes
9. VM plan L1 table addresses matched (`0x00028000` / `0xc0028000`): yes
10. VM plan memory range matched (`0x80000000` / `0x5e500000`): yes
11. VM plan section policy matched (`0x00100000` / `0x00010c02`): yes
12. VM plan MMU/cache policy matched (`0x00000001` / `0x00000000`): yes
13. VM plan satisfied mask was complete (`0x0000007f`): yes
14. VM plan checksum matched (`0xeb540a8a`): yes
15. VM plan status was `0x34000001`: yes
16. full root step mask was complete (`0x001fffff`): yes
17. high root returned status `0x34000001`: yes
18. checksum matched through identity and alias views: yes
19. SGI and timer IRQ paths still worked after high root: yes
20. payload returned through `kernel_entry` successfully and reset through PS_HOLD: yes

## Next stage

Stage35 can make the VM plan drive a first minimal VM bootstrap state object:

- keep identity/recovery mappings and caches disabled,
- keep existing startup-entry/callout/context/VM-plan checks,
- add a versioned VM bootstrap state object containing the accepted VM plan status, kernel map base/limit, available memory cursor, first bootstrap allocation span, and pmap-like policy facts,
- validate VM bootstrap state checksum/status through identity and high-alias views,
- preserve post-run SGI/timer IRQ retests,
- avoid persistent writes.
