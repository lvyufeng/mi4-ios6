# Experiment 63 — Stage60 XNU pmap cache/MMU attribute dry-run contract

Date: 2026-06-08

## Goal

Stage60 advances the cancro/MSM8974 XNU bring-up path by adding a Stage-owned XNU pmap cache/MMU attribute dry-run contract on top of the Stage56 bootstrap mapping contract, Stage57 pmap/bootstrap allocation contract, Stage58 pmap section-table dry-run contract, and Stage59 pmap page-granular dry-run contract.

Stage59 proved that a local Stage-owned model can populate and validate ARMv7 short-descriptor L1 coarse/table descriptors plus a bounded 4 MiB L2 small-page window with default PTE attributes `0x00000412`. Stage60 keeps that prerequisite stable and adds the next blocker: prove the exact public-XNU ARMv7 cache, access-protection, WIMG, section-template, and small-page-template attribute arithmetic in Stage-owned code.

The Stage60 contract verifies that the public reference constants and macros can be modeled locally and that their resulting descriptor/PTE attribute words match the inherited Stage58/Stage59 readbacks.

## Safety boundary

Stage60 still does not run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute public ARM pexpert/platform runtime code, does not execute public ARM VM/pmap runtime code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed physical load addresses, does not write proposed TTE/pmap workspace addresses, does not install live proposed XNU/pmap tables, does not invalidate TLBs for a proposed pmap install, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

Hardware validation must remain non-persistent `fastboot boot` only.

## Implementation

Stage60 adds:

- `stage60/xnu_pmap_attr_dryrun_contract.c`
- new pmap cache/MMU attribute dry-run ABI in `stage60/stage60.h`
- a new loader satisfied bit:
  - `STAGE60_LOADER_SAT_XNU_PMAP_ATTR_DRYRUN_CONTRACT`
- new loader roll-up fields:
  - `xnu_pmap_attr_dryrun_contract`
  - `xnu_pmap_attr_dryrun_contract_status`
  - `xnu_pmap_attr_dryrun_contract_satisfied_mask`
  - `xnu_pmap_attr_dryrun_contract_failure_mask`
  - `xnu_pmap_attr_dryrun_contract_checksum`
  - `xnu_pmap_attr_dryrun_contract_status_rollup`
- build source inclusion for `xnu_pmap_attr_dryrun_contract.c`
- boot-image command-line markers:
  - `xnu-pmap-attr-dryrun-contract`
  - `pmap-attr-local-dryrun-only`
- fixed stale scaffold boot-arg markers from Stage59 to Stage60 in:
  - `stage60/boot_args.c`
  - `stage60/stage60_main.c`
  - `stage60/xnu_object_shims.c`

The contract imports only Stage-owned/generated facts:

- Stage60 pmap section-table dry-run contract status/masks/checksum
- Stage60 pmap page-granular dry-run contract status/masks/checksum
- Stage60 XNU bootstrap mapping contract status
- Stage60 XNU pmap/bootstrap allocation contract status
- Stage60 cache-preservation status
- Stage60 public-XNU compile graph status
- Stage60 public-XNU object-subset status
- Stage60 controlled host-only ARM ELF link-proof status
- Stage60 TTBR0 round-trip negative safety counters

## Public VM/pmap reference-only inputs

Stage60 keeps public ARM VM/pmap files reference-only:

```text
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
external/xnu-4570.1.46/osfmk/arm/pmap.h
external/xnu-4570.1.46/osfmk/arm/proc_reg.h
external/xnu-4570.1.46/osfmk/vm/pmap.h
external/xnu-4570.1.46/osfmk/mach/arm/vm_param.h
```

These files are not compiled into the object subset, not linked into the proof ELF, and not executed on target hardware.

Modeled public ARM cache attribute constants from `proc_reg.h`:

```text
CACHE_ATTRINDX_WRITEBACK=0x00000000
CACHE_ATTRINDX_WRITECOMB=0x00000001
CACHE_ATTRINDX_WRITETHRU=0x00000002
CACHE_ATTRINDX_DISABLE=0x00000003
CACHE_ATTRINDX_INNERWRITEBACK=0x00000004
CACHE_ATTRINDX_POSTED=CACHE_ATTRINDX_DISABLE=0x00000003
CACHE_ATTRINDX_DEFAULT=CACHE_ATTRINDX_WRITEBACK=0x00000000
```

Modeled public ARM AP constants from `proc_reg.h`:

```text
AP_RWNA=0x00000000
AP_RWRW=0x00000001
AP_RONA=0x00000002
AP_RORO=0x00000003
```

Modeled public WIMG constants from `osfmk/vm/pmap.h` and `osfmk/arm/pmap.h`:

```text
VM_MEM_GUARDED=0x00000001
VM_MEM_COHERENT=0x00000002
VM_MEM_NOT_CACHEABLE=0x00000004
VM_MEM_WRITE_THROUGH=0x00000008
VM_MEM_INNER=0x00000010
VM_MEM_EARLY_ACK=0x00000020
VM_WIMG_DEFAULT=0x00000002
VM_WIMG_COPYBACK=0x00000002
VM_WIMG_INNERWBACK=0x00000012
VM_WIMG_IO=0x00000007
VM_WIMG_POSTED=0x00000027
VM_WIMG_WTHRU=0x0000000b
VM_WIMG_WCOMB=0x00000006
```

Modeled ARMv7 short-descriptor attribute bit placement from `proc_reg.h`:

```text
ARM_PTE_ATTRINDX(indx)=((indx & 0x3) << 2) | (((indx >> 2) & 0x1) << 6)
ARM_TTE_BLOCK_ATTRINDX(indx)=((indx & 0x3) << 2) | (((indx >> 2) & 0x1) << 12)
ARM_PTE_AP(ap)=((ap & 0x1) << 5) | (((ap >> 1) & 0x1) << 9)
ARM_TTE_BLOCK_AP(ap)=((ap & 0x1) << 11) | (((ap >> 1) & 0x1) << 15)
```

## Compile graph stability

Stage60 keeps the allowed public object set stable:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
```

It explicitly keeps the pmap/VM reference-only files out of compile/link allow-lists and fails closed if public pmap/VM runtime files become compile/link allowed.

Final graph markers:

```text
stage60_xnu_compile_graph_status=0x60000001
stage60_xnu_compile_graph_required_mask=0x1fffffff
stage60_xnu_compile_graph_satisfied_mask=0x1fffffff
stage60_xnu_compile_graph_failure_mask=0x00000000
stage60_xnu_compile_graph_candidate_count=0x00000014
stage60_xnu_compile_graph_allowed_compile_count=0x00000005
stage60_xnu_compile_graph_allowed_link_count=0x00000005
stage60_xnu_compile_graph_forbidden_count=0x0000000f
stage60_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage60_xnu_compile_graph_pmap_runtime_blocked_mask=0x00000003
stage60_xnu_compile_graph_pmap_reference_count=0x00000005
stage60_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage60_xnu_compile_graph_pmap_public_link_count=0x00000000
stage60_xnu_compile_graph_pmap_reference_only=0x00000001
```

## Pmap cache/MMU attribute dry-run contract markers

Confirmed target-side contract markers:

```text
stage60_xnu_pmap_attr_dryrun_contract_status=0x60000001
stage60_xnu_pmap_attr_dryrun_contract_required_mask=0x00ffffff
stage60_xnu_pmap_attr_dryrun_contract_satisfied_mask=0x00ffffff
stage60_xnu_pmap_attr_dryrun_contract_failure_mask=0x00000000
stage60_xnu_pmap_attr_dryrun_contract_checksum=0xfd77fdcd
loader_xnu_pmap_attr_dryrun_contract_status=0x60000001
loader_xnu_pmap_attr_dryrun_contract_satisfied_mask=0x00ffffff
loader_xnu_pmap_attr_dryrun_contract_failure_mask=0x00000000
loader_xnu_pmap_attr_dryrun_contract_checksum=0xfd77fdcd
loader_xnu_pmap_attr_dryrun_contract_status_rollup=0x60000001
```

Modeled cache/AP/WIMG constants:

```text
stage60_xnu_pmap_attr_dryrun_cache_writeback=0x00000000
stage60_xnu_pmap_attr_dryrun_cache_writecomb=0x00000001
stage60_xnu_pmap_attr_dryrun_cache_writethru=0x00000002
stage60_xnu_pmap_attr_dryrun_cache_disable=0x00000003
stage60_xnu_pmap_attr_dryrun_cache_innerwriteback=0x00000004
stage60_xnu_pmap_attr_dryrun_cache_posted=0x00000003
stage60_xnu_pmap_attr_dryrun_cache_default=0x00000000
stage60_xnu_pmap_attr_dryrun_ap_rwna=0x00000000
stage60_xnu_pmap_attr_dryrun_ap_rwrw=0x00000001
stage60_xnu_pmap_attr_dryrun_ap_rona=0x00000002
stage60_xnu_pmap_attr_dryrun_ap_roro=0x00000003
stage60_xnu_pmap_attr_dryrun_vm_wimg_default=0x00000002
stage60_xnu_pmap_attr_dryrun_vm_wimg_copyback=0x00000002
stage60_xnu_pmap_attr_dryrun_vm_wimg_innerwback=0x00000012
stage60_xnu_pmap_attr_dryrun_vm_wimg_io=0x00000007
stage60_xnu_pmap_attr_dryrun_vm_wimg_posted=0x00000027
stage60_xnu_pmap_attr_dryrun_vm_wimg_wthru=0x0000000b
stage60_xnu_pmap_attr_dryrun_vm_wimg_wcomb=0x00000006
```

Attribute macro and template markers:

```text
stage60_xnu_pmap_attr_dryrun_pte_attr_writeback=0x00000000
stage60_xnu_pmap_attr_dryrun_pte_attr_writecomb=0x00000004
stage60_xnu_pmap_attr_dryrun_pte_attr_writethru=0x00000008
stage60_xnu_pmap_attr_dryrun_pte_attr_disable=0x0000000c
stage60_xnu_pmap_attr_dryrun_pte_attr_innerwriteback=0x00000040
stage60_xnu_pmap_attr_dryrun_pte_attr_roundtrip_mask=0x0000001f
stage60_xnu_pmap_attr_dryrun_tte_attr_writeback=0x00000000
stage60_xnu_pmap_attr_dryrun_tte_attr_writecomb=0x00000004
stage60_xnu_pmap_attr_dryrun_tte_attr_writethru=0x00000008
stage60_xnu_pmap_attr_dryrun_tte_attr_disable=0x0000000c
stage60_xnu_pmap_attr_dryrun_tte_attr_innerwriteback=0x00001000
stage60_xnu_pmap_attr_dryrun_tte_attr_roundtrip_mask=0x0000001f
stage60_xnu_pmap_attr_dryrun_pte_template_rwx_word=0x00000412
stage60_xnu_pmap_attr_dryrun_pte_template_rwnx_word=0x00000413
stage60_xnu_pmap_attr_dryrun_pte_template_rox_word=0x00000612
stage60_xnu_pmap_attr_dryrun_pte_template_ronx_word=0x00000613
stage60_xnu_pmap_attr_dryrun_section_template_word=0x00010c02
```

WIMG mapping markers:

```text
stage60_xnu_pmap_attr_dryrun_wimg_default_pte_bits=0x00000400
stage60_xnu_pmap_attr_dryrun_wimg_io_pte_bits=0x0000000d
stage60_xnu_pmap_attr_dryrun_wimg_posted_pte_bits=0x0000000d
stage60_xnu_pmap_attr_dryrun_wimg_wcomb_pte_bits=0x00000005
stage60_xnu_pmap_attr_dryrun_wimg_wthru_pte_bits=0x00000408
stage60_xnu_pmap_attr_dryrun_wimg_innerwback_pte_bits=0x00000440
```

Prior Stage58/Stage59 readback cross-check markers:

```text
stage60_xnu_pmap_attr_dryrun_prior_table_section_word=0x00010c02
stage60_xnu_pmap_attr_dryrun_prior_table_attr_seen=0x00010c02
stage60_xnu_pmap_attr_dryrun_prior_page_pte_word=0x00000412
stage60_xnu_pmap_attr_dryrun_prior_page_attr_seen=0x00000412
```

Safety markers:

```text
stage60_xnu_pmap_attr_dryrun_public_pmap_compile_count=0x00000000
stage60_xnu_pmap_attr_dryrun_public_pmap_link_count=0x00000000
stage60_xnu_pmap_attr_dryrun_public_pmap_execute_count=0x00000000
stage60_xnu_pmap_attr_dryrun_public_arm_vm_init_executed=0x00000000
stage60_xnu_pmap_attr_dryrun_proposed_workspace_written=0x00000000
stage60_xnu_pmap_attr_dryrun_live_pmap_tables_installed=0x00000000
stage60_xnu_pmap_attr_dryrun_ttbr_written=0x00000000
stage60_xnu_pmap_attr_dryrun_ttbcr_written=0x00000000
stage60_xnu_pmap_attr_dryrun_dacr_written=0x00000000
stage60_xnu_pmap_attr_dryrun_sctlr_written=0x00000000
stage60_xnu_pmap_attr_dryrun_tlbs_invalidated=0x00000000
stage60_xnu_pmap_attr_dryrun_caches_changed=0x00000000
stage60_xnu_pmap_attr_dryrun_persistent_write_attempted=0x00000000
stage60_xnu_pmap_attr_dryrun_xnu_start_executed=0x00000000
stage60_xnu_pmap_attr_dryrun_generated_macho_executed=0x00000000
stage60_xnu_pmap_attr_dryrun_local_only=0x00000001
stage60_xnu_pmap_attr_dryrun_fail_closed=0x00000001
loader_status=0x60000001
```

## Local validation

Local build passed:

```bash
/mnt/data/mi4-ios6/stage60/build.sh
```

Final local image hashes:

```text
dfb68a5b34fb584297f5a184cf975d7f4e72b659316714f429b8ccb0ccd963d0  out/stage60/stage60_fixture.macho
bc4f82ded21c9f3fce0f327888092b3662fce985907b299061ae2ff116e8a675  out/stage60/stage60.elf
02b784f4b512cb63ae1c724d6e690ac426457093ce7e8379c92a23b853d09354  out/stage60/stage60.bin
68e8a1efc099e0bdaac448b6eba03294f8d71f337cab3b6028bcde3edc401454  out/stage60/stage60.img
74cd1f3ec18d974a088cb03767200c37d0f7224dbb654a730a12f7a862e3c680  out/stage60/stage60-qcdt.img
```

Size summary:

```text
text=224396 data=0 bss=316156 dec=540552 hex=83f88
```

Boot-image parse passed for both images. QCDT image highlights:

```text
magic=ANDROID!
page_size=2048
kernel_size=224396 (0x36c8c)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage60 mi4ios6=stage60 ... xnu-pmap-page-dryrun-contract xnu-pmap-attr-dryrun-contract pmap-table-local-dryrun-only pmap-page-local-l2-dryrun-only pmap-attr-local-dryrun-only ... no-proposed-pmap-write no-live-pmap-install no-tlb-invalidate no-cache-policy-change no-persist-write no-external-mutation
part=kernel offset=0x800 size=224396 sha256=02b784f4b512cb63ae1c724d6e690ac426457093ce7e8379c92a23b853d09354
part=dt.img offset=0x37800 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Undefined-symbol checks passed:

```bash
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage60/stage60.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage60/xnu-link/stage60-xnu-link.elf
```

Both commands produced no undefined symbols.

Static stale-reference scan under `stage60/` is clean for stale Stage59 code markers outside generated or historical context. `external/xnu-upstream` and `external/xnu-4570.1.46` remain ignored/untouched, and `out/stage60/` remains ignored.

Fixed public ARM boot-args command lines remain under the 256-byte `CommandLine` field:

```text
stage60/boot_args.c 226/256 including NUL
stage60/stage60_main.c 240/256 including NUL
stage60/xnu_object_shims.c 196/256 including NUL
```

## Hardware validation

Stage60 hardware validation completed with non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage60/stage60-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage60-last_kmsg.txt
```

Recovered log facts:

```text
stage60_last_kmsg_bytes=159522 (0x00026f22)
stage60_marker_count=2333 (0x0000091d)
stage60_xnu_marker_count=2307 (0x00000903)
stage60_hardware_validation=ok
```

Confirmed hardware markers after non-persistent boot:

```text
MI4IOS6_STAGE60_XNU stage60_xnu_compile_graph_status=0x60000001
MI4IOS6_STAGE60_XNU stage60_xnu_compile_graph_pmap_public_compile_count=0x00000000
MI4IOS6_STAGE60_XNU stage60_xnu_compile_graph_pmap_public_link_count=0x00000000
MI4IOS6_STAGE60_XNU stage60_xnu_object_subset_status=0x60000001
MI4IOS6_STAGE60_XNU stage60_xnu_link_status=0x60000001
MI4IOS6_STAGE60_XNU stage60_xnu_bootstrap_contract_status=0x60000001
MI4IOS6_STAGE60_XNU stage60_xnu_pmap_bootstrap_contract_status=0x60000001
MI4IOS6_STAGE60_XNU stage60_xnu_pmap_table_dryrun_contract_status=0x60000001
MI4IOS6_STAGE60_XNU stage60_xnu_pmap_page_dryrun_contract_status=0x60000001
MI4IOS6_STAGE60_XNU stage60_xnu_pmap_attr_dryrun_contract_status=0x60000001
MI4IOS6_STAGE60_XNU stage60_xnu_pmap_attr_dryrun_contract_required_mask=0x00ffffff
MI4IOS6_STAGE60_XNU stage60_xnu_pmap_attr_dryrun_contract_satisfied_mask=0x00ffffff
MI4IOS6_STAGE60_XNU stage60_xnu_pmap_attr_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE60_XNU loader_xnu_pmap_attr_dryrun_contract_status_rollup=0x60000001
MI4IOS6_STAGE60_XNU loader_status=0x60000001
MI4IOS6_STAGE60_XNU kernel_entry ok
MI4IOS6_STAGE60 kernel_entry returned success
```

The device returned to Android after the PS_HOLD reset path. No persistent flash/erase/write was performed.

## Result

Stage60 completes the exact pmap cache/MMU attribute dry-run blocker. The new contract proves that the Stage-owned model can represent public ARM XNU cache attribute indices, AP encodings, ARMv7 short-descriptor PTE/TTE attribute bit placements, WIMG-derived PTE bits, page-protection helper templates, and inherited section/PTE readback words while preserving all no-execution, no-proposed-write, no-live-table-install, no-control-register-write, no-TLB-invalidate, no-cache-change, and no-persistent-write boundaries.

The next safe direction is broader page-granular pmap windows, MSM8974 pexpert/timer/interrupt hooks, IOKit/platform-driver scaffolding, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build or any XNU runtime handoff.
