# Experiment 60 — Stage57 XNU pmap/bootstrap allocation contract

Date: 2026-06-07

## Goal

Stage57 advances the cancro/MSM8974 XNU bring-up path by adding a Stage-owned XNU pmap/bootstrap allocation contract on top of the Stage56 bootstrap mapping contract.

Stage56 already proved the proposed XNU bootstrap tuple and mapping envelope from inert Mach-O loader, local staging/materialization, TTE dry-run, high-VA, safe-table, TTBR0-restore, cache-preservation, public-XNU compile graph, object subset, and controlled link-proof facts. Stage57 keeps those facts and adds the next blocker: public ARM VM/pmap bootstrap arithmetic and Stage-owned early pmap allocation/workspace facts, without compiling or executing public `arm_vm_init.c` or `pmap.c`.

## Safety boundary

Stage57 still does not run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute public ARM pexpert/platform runtime code, does not execute public ARM VM/pmap runtime code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed physical load addresses, does not write proposed TTE workspace addresses, does not install live proposed XNU/pmap tables, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

Hardware validation, when performed, must remain non-persistent `fastboot boot` only.

## Implementation

Stage57 adds:

- `stage57/xnu_pmap_bootstrap_contract.c`
- new pmap/bootstrap contract ABI in `stage57/stage57.h`
- `struct stage57_pmap_bootstrap_snapshot`
- `mmu_stage57_pmap_bootstrap_snapshot_result()` in `stage57/mmu.c`
- loader roll-up fields:
  - `xnu_pmap_bootstrap_contract`
  - `xnu_pmap_bootstrap_contract_status`
  - `xnu_pmap_bootstrap_contract_satisfied_mask`
  - `xnu_pmap_bootstrap_contract_failure_mask`
  - `xnu_pmap_bootstrap_contract_checksum`
  - `xnu_pmap_bootstrap_contract_status_rollup`
- a new loader satisfied bit:
  - `STAGE57_LOADER_SAT_XNU_PMAP_BOOTSTRAP_CONTRACT`
- boot-image command-line markers:
  - `xnu-pmap-bootstrap-contract`
  - `pmap-bootstrap-reference-only`

The contract imports only Stage-owned/generated facts:

- Stage57 XNU bootstrap mapping contract status/masks/checksum
- Stage-owned pmap/bootstrap snapshot status/masks/checksum
- TTE dry-run status
- safe-table materialization status
- stage-owned table status
- TTBR0 round-trip/restore status
- cache-preservation status
- public-XNU compile graph status
- public-XNU object-subset status
- controlled host-only ARM ELF link-proof status

## Public VM/pmap reference-only inputs

Stage57 records public reference-only facts from:

```text
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
external/xnu-4570.1.46/osfmk/arm/pmap.h
external/xnu-4570.1.46/osfmk/arm/proc_reg.h
external/xnu-4570.1.46/osfmk/mach/arm/vm_param.h
```

These files are not compiled into the object subset, not linked into the proof ELF, and not executed on target hardware.

Modeled public constants/facts:

```text
PAGE_SIZE=0x00001000
MEM_SIZE_MAX=0x40000000
VM_MIN_KERNEL_ADDRESS=0x80000000
VM_MAX_KERNEL_ADDRESS=0xfffeffff
L1_ALIGNMENT=0x00004000
SECTION_SIZE=0x00100000
SECTION_DESC_SO=0x00010c02
```

Modeled public ARM VM/pmap arithmetic:

```text
gVirtBase = virtBase
gPhysBase = physBase
gPhysSize = memSize
boot_ttep = topOfKernelData
cpu_ttep = boot_ttep + 4 * PAGE_SIZE
initial_avail_start = cpu_ttep + 6 * PAGE_SIZE
avail_end = physBase + memSize
vstart = (virtBase + MEM_SIZE_MAX + 0x3fffff) & 0xffc00000
virtual_space_end = VM_MAX_KERNEL_ADDRESS
```

## Compile graph update

Stage57 keeps the allowed public object set stable:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
```

It explicitly tracks the pmap/VM reference-only files and fails closed if public pmap/VM runtime files become compile/link allowed.

Final graph markers:

```text
stage57_xnu_compile_graph_status=0x57000001
stage57_xnu_compile_graph_required_mask=0x1fffffff
stage57_xnu_compile_graph_satisfied_mask=0x1fffffff
stage57_xnu_compile_graph_failure_mask=0x00000000
stage57_xnu_compile_graph_candidate_count=0x00000014
stage57_xnu_compile_graph_allowed_compile_count=0x00000005
stage57_xnu_compile_graph_allowed_link_count=0x00000005
stage57_xnu_compile_graph_forbidden_count=0x0000000f
stage57_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage57_xnu_compile_graph_pmap_runtime_blocked_mask=0x00000003
stage57_xnu_compile_graph_pmap_reference_count=0x00000005
stage57_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage57_xnu_compile_graph_pmap_public_link_count=0x00000000
stage57_xnu_compile_graph_pmap_reference_only=0x00000001
```

## Pmap/bootstrap allocation contract markers

Confirmed target-side contract markers:

```text
stage57_xnu_pmap_bootstrap_contract_status=0x57000001
stage57_xnu_pmap_bootstrap_contract_required_mask=0x7fffffff
stage57_xnu_pmap_bootstrap_contract_satisfied_mask=0x7fffffff
stage57_xnu_pmap_bootstrap_contract_failure_mask=0x00000000
stage57_xnu_pmap_bootstrap_contract_checksum=0xb76cea5d
stage57_xnu_pmap_source_bootstrap_status=0x57000001
stage57_xnu_pmap_source_snapshot_status=0x57000001
stage57_xnu_pmap_source_snapshot_satisfied_mask=0x0000003f
stage57_xnu_pmap_source_snapshot_failure_mask=0x00000000
stage57_xnu_pmap_reference_mask=0x0000001f
stage57_xnu_pmap_runtime_blocked_mask=0x00000007
stage57_xnu_pmap_reference_count=0x00000005
stage57_xnu_pmap_public_compile_count=0x00000000
stage57_xnu_pmap_public_link_count=0x00000000
stage57_xnu_pmap_public_execute_count=0x00000000
```

Modeled arithmetic markers:

```text
stage57_xnu_pmap_gVirtBase=0x80008000
stage57_xnu_pmap_gPhysBase=0x80000000
stage57_xnu_pmap_gPhysSize=0x5e500000
stage57_xnu_pmap_boot_ttep=0x8000c000
stage57_xnu_pmap_cpu_ttep=0x80010000
stage57_xnu_pmap_initial_avail_start=0x80016000
stage57_xnu_pmap_avail_end=0xde500000
stage57_xnu_pmap_vstart=0xc0400000
stage57_xnu_pmap_virtual_space_end=0xfffeffff
```

Stage-owned allocator/workspace markers:

```text
stage57_xnu_pmap_allocator_span_base=0x80000000
stage57_xnu_pmap_allocator_span_size=0x00100000
stage57_xnu_pmap_allocator_span_end=0x80100000
stage57_xnu_pmap_allocator_initial_cursor=0x80000000
stage57_xnu_pmap_allocator_current_cursor=0x80100000
stage57_xnu_pmap_allocator_remaining_bytes=0x00000000
stage57_xnu_pmap_allocator_first_alloc_base=0x80000000
stage57_xnu_pmap_allocator_first_alloc_size=0x00100000
stage57_xnu_pmap_allocator_first_alloc_end=0x80100000
stage57_xnu_pmap_allocator_first_alloc_tag=0x414c4c43
stage57_xnu_pmap_allocator_alignment=0x00001000
stage57_xnu_pmap_workspace_base=0x80000000
stage57_xnu_pmap_workspace_limit=0x80100000
stage57_xnu_pmap_workspace_l1_table_phys=0x00074000
stage57_xnu_pmap_workspace_l1_table_virt=0xc0074000
stage57_xnu_pmap_workspace_l1_section_descriptor=0x00010c02
stage57_xnu_pmap_workspace_l1_section_size=0x00100000
stage57_xnu_pmap_workspace_cache_policy=0x00000000
```

Safety markers:

```text
stage57_xnu_pmap_public_arm_vm_init_executed=0x00000000
stage57_xnu_pmap_live_pmap_tables_installed=0x00000000
stage57_xnu_pmap_xnu_start_executed=0x00000000
stage57_xnu_pmap_generated_macho_executed=0x00000000
stage57_xnu_pmap_proposed_phys_load_written=0x00000000
stage57_xnu_pmap_proposed_tte_workspace_written=0x00000000
stage57_xnu_pmap_persistent_write_attempted=0x00000000
stage57_xnu_pmap_caches_changed=0x00000000
loader_xnu_pmap_bootstrap_contract_status_rollup=0x57000001
loader_status=0x57000001
```

## Local validation

Local build passed:

```bash
/mnt/data/mi4-ios6/stage57/build.sh
```

Final local image hashes:

```text
751cf2aa208d4324faaf17ce7da70adbb78956be8aef1a5777e03f8e91587389  out/stage57/stage57_fixture.macho
4d00bfcb3296254a8663197d7cf42b431ae120ed3d3fef3ab2a56b67b479a92d  out/stage57/stage57.elf
9c9d82dd151b4f538f622ff4385ea88b946c470e1d6a222175dcf42436820869  out/stage57/stage57.bin
adea5a9f885ae3d82b0d48a6376553e2ceb43f4a5f11ed3d14eca06b9b0c1d8a  out/stage57/stage57.img
8746feb4d67b99ba600590fda7a93de0bc215ee0c3ff07e7717c98e532f708fd  out/stage57/stage57-qcdt.img
```

Boot-image parse passed for both images. QCDT image highlights:

```text
magic=ANDROID!
page_size=2048
kernel_size=196900 (0x30124)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage57 mi4ios6=stage57 ... xnu-bootstrap-contract xnu-pmap-bootstrap-contract pmap-bootstrap-reference-only ... no-public-xnu-exec no-platform-runtime-exec no-macho-exec no-proposed-phys-write no-proposed-tte-write no-cache-policy-change no-persist-write no-external-mutation
part=kernel offset=0x800 size=196900 sha256=9c9d82dd151b4f538f622ff4385ea88b946c470e1d6a222175dcf42436820869
part=dt.img offset=0x31000 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Undefined-symbol checks passed:

```bash
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage57/stage57.elf
arm-none-eabi-nm -u /mnt/data/mi4-ios6/out/stage57/xnu-link/stage57-xnu-link.elf
```

Both commands produced no undefined symbols.

Static stale-reference scan under `stage57/` is clean for stale Stage56 code markers. `external/xnu-upstream` and `external/xnu-4570.1.46` remain clean, and `out/stage57/` remains ignored.

During validation, an overlong `boot_args.CommandLine` string was found to overflow the fixed 256-byte public ARM boot-args field and corrupt the adjacent Apple-DT buffer, producing an Apple-DT walk mismatch on target. Stage57 now uses a shorter command line plus a bounded copy and explicit final NUL byte for `CommandLine[255]`.

## Hardware validation

Stage57 hardware validation completed with non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage57/stage57-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage57-last_kmsg.txt
```

Recovered log facts:

```text
stage57_last_kmsg_bytes=0x00021c8f
stage57_marker_count=0x00000817
stage57_xnu_marker_count=0x000007fd
stage57_hardware_validation=ok
```

Confirmed key markers:

```text
MI4IOS6_STAGE57_XNU stage57_xnu_compile_graph_status=0x57000001
MI4IOS6_STAGE57_XNU stage57_xnu_compile_graph_pmap_public_compile_count=0x00000000
MI4IOS6_STAGE57_XNU stage57_xnu_compile_graph_pmap_public_link_count=0x00000000
MI4IOS6_STAGE57_XNU stage57_xnu_object_subset_status=0x57000001
MI4IOS6_STAGE57_XNU stage57_xnu_link_status=0x57000001
MI4IOS6_STAGE57_XNU stage57_xnu_bootstrap_contract_status=0x57000001
MI4IOS6_STAGE57_XNU stage57_xnu_pmap_bootstrap_contract_status=0x57000001
MI4IOS6_STAGE57_XNU stage57_xnu_pmap_bootstrap_contract_failure_mask=0x00000000
MI4IOS6_STAGE57_XNU loader_xnu_pmap_bootstrap_contract_status_rollup=0x57000001
MI4IOS6_STAGE57_XNU loader_status=0x57000001
MI4IOS6_STAGE57_XNU kernel_entry ok
MI4IOS6_STAGE57 kernel_entry returned success
```

Confirmed negative safety markers:

```text
MI4IOS6_STAGE57_XNU stage57_xnu_pmap_public_compile_count=0x00000000
MI4IOS6_STAGE57_XNU stage57_xnu_pmap_public_link_count=0x00000000
MI4IOS6_STAGE57_XNU stage57_xnu_pmap_public_execute_count=0x00000000
MI4IOS6_STAGE57_XNU stage57_xnu_pmap_public_arm_vm_init_executed=0x00000000
MI4IOS6_STAGE57_XNU stage57_xnu_pmap_live_pmap_tables_installed=0x00000000
MI4IOS6_STAGE57_XNU stage57_xnu_pmap_xnu_start_executed=0x00000000
MI4IOS6_STAGE57_XNU stage57_xnu_pmap_generated_macho_executed=0x00000000
MI4IOS6_STAGE57_XNU stage57_xnu_pmap_proposed_phys_load_written=0x00000000
MI4IOS6_STAGE57_XNU stage57_xnu_pmap_proposed_tte_workspace_written=0x00000000
MI4IOS6_STAGE57_XNU stage57_xnu_pmap_persistent_write_attempted=0x00000000
MI4IOS6_STAGE57_XNU stage57_xnu_pmap_caches_changed=0x00000000
```

The device returned to Android after the PS_HOLD reset path. No persistent flash/erase/write was performed.
