# Experiment 50 — Stage47 TTE Descriptor Verification and VTOP Dry-Run

Date: 2026-06-06

Goal: move one step closer to a real XNU handoff by verifying the local simulated ARMv7 short-descriptor L1 section entries that Stage46 began writing, then performing a software-only virt-to-phys dry-run over those local table bytes. Stage47 still keeps the operation strictly local: table bytes are written and read only from a Stage47-owned BSS arena, not from the proposed XNU physical TTE workspace.

Stage47 still does **not** run XNU or iOS. It does not branch to parsed Mach-O entry metadata, does not jump to XNU `_start` / `arm_init`, does not write the proposed XNU physical load or TTE workspace addresses, does not program TTBR0/TTBR1, does not replace the live Stage47 MMU tables, and does not enable caches. It preserves Stage46's safety gates and adds descriptor-word readback plus software L1 section translation checks only.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- No parsed Mach-O entrypoint execution.
- No real XNU `_start` / `arm_init` handoff.
- No writes to proposed XNU physical load addresses.
- No writes to proposed XNU TTE workspace physical addresses.
- No TTBR0/TTBR1 writes.
- No replacement of the live Stage47 MMU tables.
- Caches remain disabled/unchanged.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- SGI/timer IRQ selftests are rerun before the Stage47 loader preflight.
- Mach-O materialization remains local BSS-only.
- TTE workspace modeling, descriptor readback, and dry-run translation use only `stage47_tte_dryrun_arena[]`, a Stage47-owned local BSS simulation buffer.

## What Stage47 adds over Stage46

Stage46 modeled the early ARM XNU TTE workspace layout and wrote simulated L1 section descriptors into a local arena. Stage47 adds:

- a new standalone `stage47/` payload copied from Stage46,
- Stage47 status/log prefixes (`0x47000001`, `MI4IOS6_STAGE47`),
- command-line markers:
  - `tte-dryrun`,
  - `tte-verify`,
  - `vtop-dryrun`,
- descriptor mask constants for ARMv7 short-descriptor section validation,
- local simulated L1 descriptor readback checks for:
  - low RAM first/last section descriptors,
  - loaded kernel/Mach-O first/last section descriptors,
  - `ram_console` section descriptor,
  - GIC/timer MMIO first/last section descriptors,
- descriptor type/attribute mask verification,
- a software-only L1 section translation checker for identity-style local mappings,
- public-XNU-policy checks for `topOfKernelData`/`boot_ttep`, `avail_start`, and `avail_end`,
- loader safety/readiness bits proving descriptor verification and VTOP translation remain dry-run only.

## Inherited load and workspace facts

Stage47 preserves Stage46's local Mach-O materialization and proposed XNU load/workspace facts:

```text
xnu_proposed_loaded_phys_base=0x80000000
xnu_proposed_loaded_phys_end=0x80009000
xnu_proposed_loaded_file_end=0x000004e0
xnu_proposed_topOfKernelData=0x8000c000
xnu_proposed_ttep_workspace_limit=0x80016000
xnu_proposed_avail_start=0x80016000
```

The modeled TTE workspace remains:

```text
xnu_tte_workspace_base=0x8000c000
xnu_tte_workspace_size=0x0000a000
xnu_tte_workspace_limit=0x80016000
xnu_tte_l1_table_base=0x8000c000
xnu_tte_l1_table_size=0x00004000
xnu_tte_l2_table_base=0x80010000
xnu_tte_l2_table_size=0x00001000
xnu_tte_scratch_base=0x80011000
xnu_tte_scratch_size=0x00005000
```

Stage47 does not write this proposed physical workspace. It writes and verifies table bytes only in `stage47_tte_dryrun_arena[]`, observed at:

```text
xnu_tte_local_arena_base=0x0003c000
```

## Descriptor encoding

Stage47 continues using the same conservative ARMv7 section descriptor value used by the live MMU skeleton and Stage46 dry-run:

```text
STAGE47_XNU_TTE_DESC_SECTION_SO = 0x00010c02
```

The local simulated section word is composed as:

```text
word = (l1_index << 20) | 0x00010c02
```

Stage47 verifies:

```text
type mask      = word & 0x00000003 == 0x00000002
base mask      = word & 0xfff00000 == l1_index << 20
attribute mask = word & 0x000fffff == 0x00010c02
```

This is still a conservative identity-style local table model. It is not yet a full XNU high-virtual kernel mapping policy.

## Descriptor readback facts

Recovered hardware log descriptor facts:

```text
xnu_tte_kernel_l1_first_index=0x00000800
xnu_tte_kernel_l1_last_index=0x00000800
xnu_tte_lowmem_l1_first_index=0x00000800
xnu_tte_lowmem_l1_last_index=0x00000de4
xnu_tte_ram_console_l1_index=0x00000de5
xnu_tte_gic_l1_first_index=0x00000f90
xnu_tte_gic_l1_last_index=0x00000f90
xnu_tte_descriptor_verify_mask=0x0000007f
xnu_tte_descriptor_failure_mask=0x00000000
xnu_tte_descriptor_lowmem_first_word=0x80010c02
xnu_tte_descriptor_lowmem_last_word=0xde410c02
xnu_tte_descriptor_kernel_first_word=0x80010c02
xnu_tte_descriptor_kernel_last_word=0x80010c02
xnu_tte_descriptor_ram_console_word=0xde510c02
xnu_tte_descriptor_gic_first_word=0xf9010c02
xnu_tte_descriptor_gic_last_word=0xf9010c02
xnu_tte_descriptor_type_mask_seen=0x00000002
xnu_tte_descriptor_attr_mask_seen=0x00010c02
xnu_tte_descriptor_expected_attr_mask=0x00010c02
```

Interpretation:

- descriptor verify mask `0x7f` means all seven readback points passed,
- descriptor failure mask `0x0` means no local L1 slot mismatch was detected,
- all observed descriptor type bits are ARM section descriptors (`0x2`),
- all observed low 20-bit descriptor attributes match `0x00010c02`,
- first/last kernel descriptor words match because the generated fixture's loaded span fits within one 1 MiB section,
- first/last GIC descriptor words match because the checked GIC/timer MMIO range also sits in section index `0xf90`.

## Dry-run translation facts

Stage47 implements a software-only L1 section translator over the local BSS arena:

```text
index = va >> 20
desc  = local_l1[index]
if ((desc & 0x3) != 0x2) fail
pa = (desc & 0xfff00000) | (va & 0x000fffff)
```

Recovered translation facts:

```text
xnu_tte_translation_check_mask=0x0000001f
xnu_tte_translation_failure_mask=0x00000000
xnu_tte_translation_case_count=0x00000005
xnu_tte_translation_kernel_text_va=0x80000000
xnu_tte_translation_kernel_text_pa=0x80000000
xnu_tte_translation_kernel_last_va=0x80008fff
xnu_tte_translation_kernel_last_pa=0x80008fff
xnu_tte_translation_lowmem_va=0x80000000
xnu_tte_translation_lowmem_pa=0x80000000
xnu_tte_translation_ram_console_va=0xde500000
xnu_tte_translation_ram_console_pa=0xde500000
xnu_tte_translation_gic_va=0xf9000000
xnu_tte_translation_gic_pa=0xf9000000
```

Interpretation:

- translation mask `0x1f` means all five dry-run translation cases passed,
- the local table translates the generated loaded kernel first and last bytes as expected,
- low RAM, `ram_console`, and GIC MMIO translate identity-style through local L1 section descriptors,
- these checks read only local simulated table bytes and do not use TTBRs or hardware translation.

## Public XNU boot policy comparison

Public ARM XNU `_start` expects `r0 = boot_args *`, reads `physBase`, `virtBase`, `memSize`, and `topOfKernelData`, then uses `topOfKernelData` as the early translation-table base. `arm_vm_init()` derives `avail_start` after the boot table workspace and `avail_end = gPhysBase + memSize`.

Stage47 records the matching policy values without executing XNU:

```text
xnu_tte_xnu_boot_ttep_expected=0x8000c000
xnu_tte_xnu_avail_start_expected=0x80016000
xnu_tte_xnu_avail_end_expected=0xde500000
xnu_tte_xnu_policy_mask=0x0000001f
```

Interpretation:

- `boot_ttep`/`topOfKernelData` is modeled as `0x8000c000`,
- `avail_start` follows the 10-page workspace at `0x80016000`,
- `avail_end = proposed_physBase + proposed_memSize = 0xde500000`, matching the RAM range below `ram_console`,
- safe dry-run state is included in the policy mask: no TTBR writes and caches unchanged.

## Loader-preflight facts

Recovered Stage47 loader facts:

```text
xnu_tte_dryrun_status=0x47000001
xnu_tte_satisfied_mask=0x0001ffff
xnu_tte_failure_mask=0x00000000
xnu_tte_checksum=0x0017c35e
loader_tte_verify_status=0x47000001
loader_vtop_dryrun_status=0x47000001
loader_safety_mask=0x00000fff
loader_satisfied_mask=0x00000fff
loader_checksum=0x105a9d9d
loader_status=0x47000001
```

The Stage47 TTE satisfied mask extends Stage46 with:

- descriptor readback,
- descriptor attribute verification,
- kernel translation,
- low-RAM translation,
- `ram_console` translation,
- device/GIC translation,
- public-XNU boot table/availability policy check.

The Stage47 loader safety mask extends Stage46 with explicit:

- TTE verification is local-only,
- VTOP translation is dry-run-only.

## Built image

```bash
/mnt/data/mi4-ios6/stage47/build.sh
```

Successful local build:

```text
out/stage47/stage47-qcdt.img
sha256=8bc013997daa908e768b9c66b0a2993d79817f46e05a2915326d29653c881c25
```

Build hashes:

```text
4487445da59e434f00ca2fe006e04eeb7d920a6b116b930968920526ac411bf0  out/stage47/stage47_fixture.macho
010dd5d2bc61fa8535eccdb7193165426bff9ef784e639640db372dee48310e4  out/stage47/stage47.elf
355a173b10eeb6c15ff32a7ad841d378a435711da65c5f7a6d409919d27b939a  out/stage47/stage47.bin
bd08c35328a3c3c216e8c41e7ab059de12196aadb4999940bff1a10a6c04df58  out/stage47/stage47.img
8bc013997daa908e768b9c66b0a2993d79817f46e05a2915326d29653c881c25  out/stage47/stage47-qcdt.img
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=155996 (0x2615c)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage47 mi4ios6=stage47 c-runtime apple-dt macho-fixture load-plan materialize tte-dryrun tte-verify vtop-dryrun xnu-preflight
part=kernel offset=0x800 size=155996 sha256=355a173b10eeb6c15ff32a7ad841d378a435711da65c5f7a6d409919d27b939a
part=dt.img offset=0x27000 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Important symbols:

```text
000080a0 T stage47_vectors
0000c0b0 T stage47_loader_preflight_run
0001c904 T kernel_entry
0001cc4c T test_kernel_entry
0001cde4 T stage47_main
0001e68c T stage47_embedded_macho_size
0001e690 T stage47_embedded_macho
00038000 b stage47_loader_preflight_block
0003c000 b stage47_tte_dryrun_arena
00046000 b stage47_macho_staging_arena
00062000 B __stage47_image_end
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage47/stage47-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2618 KB)                       OKAY [  0.083s]
Booting                                            OKAY [  0.006s]
Finished. Total time: 0.094s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage47-last_kmsg.txt
```

The recovered log was 107558 bytes and contained:

```text
1628 MI4IOS6_STAGE47 markers
1602 MI4IOS6_STAGE47_XNU markers
```

## Key recovered markers

Inherited high-root/MMU success remains intact:

```text
MI4IOS6_STAGE47_XNU high_root_status=0x47000001
MI4IOS6_STAGE47_XNU mmu high bootstrap selftest ok
```

Post-root IRQ retests still pass:

```text
MI4IOS6_STAGE47_XNU gic SGI selftest ok
MI4IOS6_STAGE47_XNU gic timer selftest ok
```

Stage47 loader/TTE verification markers:

```text
MI4IOS6_STAGE47_XNU macho_staging_status=0x47000001
MI4IOS6_STAGE47_XNU xnu_tte_dryrun_status=0x47000001
MI4IOS6_STAGE47_XNU xnu_tte_descriptor_verify_mask=0x0000007f
MI4IOS6_STAGE47_XNU xnu_tte_descriptor_failure_mask=0x00000000
MI4IOS6_STAGE47_XNU xnu_tte_translation_check_mask=0x0000001f
MI4IOS6_STAGE47_XNU xnu_tte_translation_failure_mask=0x00000000
MI4IOS6_STAGE47_XNU xnu_tte_satisfied_mask=0x0001ffff
MI4IOS6_STAGE47_XNU xnu_tte_failure_mask=0x00000000
MI4IOS6_STAGE47_XNU loader_tte_verify_status=0x47000001
MI4IOS6_STAGE47_XNU loader_vtop_dryrun_status=0x47000001
MI4IOS6_STAGE47_XNU loader_safety_mask=0x00000fff
MI4IOS6_STAGE47_XNU loader_satisfied_mask=0x00000fff
MI4IOS6_STAGE47_XNU loader_status=0x47000001
MI4IOS6_STAGE47_XNU Stage47 XNU handoff disabled: materialized only into local BSS arena; TTE workspace and descriptor translation modeled only in local BSS; no Mach-O entry executed, no physical writes, no TTBR writes, caches unchanged
MI4IOS6_STAGE47_XNU Stage47 Mach-O/XNU loader preflight ok
```

Final success markers:

```text
MI4IOS6_STAGE47_XNU kernel_entry ok
MI4IOS6_STAGE47 kernel_entry returned success
MI4IOS6_STAGE47 attempting MSM8974 PS_HOLD reset
```

## Interpretation

Stage47 is still not a real XNU boot, but it makes the future handoff contract more concrete than Stage46. Stage46 proved the workspace layout and wrote simulated section entries. Stage47 proves those local entries can be read back as exact ARMv7 section descriptor words, that their descriptor type and attribute bits match the expected conservative section policy, and that a software L1-section walk translates the loaded-kernel, low-RAM, `ram_console`, and GIC addresses exactly as expected.

This removes one class of uncertainty before any controlled TTBR or `_start` work: the dry-run table bytes are no longer just laid out; they are checked as descriptor words and walked by a deterministic VTOP routine. The remaining major blockers are still real XNU high-virtual mapping policy, live table materialization/switching, pmap bootstrap, MSM8974 pexpert support, XNU interrupt/timer hooks, IOKit/platform drivers, real kernelcache/loading details, relocation/linking/prelink details, and later code-signing/userspace policy work.

## Success criteria — met

1. Stage47 directory and build outputs created: yes
2. Stage47 command line includes `tte-verify` and `vtop-dryrun`: yes
3. Stage46 materialization/TTE workspace behavior preserved: yes
4. Descriptor readback fields added and logged: yes
5. All local simulated descriptor readback points passed: yes
6. Descriptor type and attribute masks matched expected section policy: yes
7. Dry-run L1 section VTOP checker added and logged: yes
8. Kernel first/last translation cases passed: yes
9. Low RAM translation case passed: yes
10. `ram_console` translation case passed: yes
11. GIC translation case passed: yes
12. Public-XNU boot_ttep/avail policy mask completed: yes
13. Proposed physical TTE workspace remained dry-run metadata only: yes
14. TTBR0/TTBR1 writes remained zero: yes
15. caches-enabled flag remained zero: yes
16. Bootloader accepted `stage47-qcdt.img`: yes
17. Apple-DT/PE_state/GIC/timebase/MMU checks still passed: yes
18. SGI IRQ retest passed after high-root/MMU path: yes
19. Timer IRQ retest passed after high-root/MMU path: yes
20. TTE dry-run status returned `0x47000001`: yes
21. Loader safety mask included TTE verification/VTOP dry-run-only bits: yes
22. Loader preflight returned status `0x47000001`: yes
23. `kernel_entry` returned success: yes
24. payload reset through PS_HOLD: yes

## Next stage

Completed by Stage48. Stage48 materialized the verified identity/recovery table bytes into a Stage-owned safe table buffer, added a PA-base-aware high-VA section descriptor model, verified high-VA descriptor and software VTOP checks, and proved `xnu_tte_descriptor_verify_mask=0x000001ff`, `xnu_tte_translation_check_mask=0x0000007f`, `xnu_tte_satisfied_mask=0x001fffff`, `loader_safety_mask=0x00007fff`, `loader_satisfied_mask=0x00007fff`, and loader status `0x48000001` while still not executing XNU, writing proposed physical addresses, programming TTBRs, replacing live MMU tables, or changing caches.

Stage49 should attempt a controlled no-XNU TTBR-switch selftest using Stage-owned tables only, preserving recovery mappings, ram_console, PS_HOLD, GIC, timer, abort logging, non-persistent booting, and caches unchanged.
