# Stage57 — XNU pmap/bootstrap allocation contract proof

Stage57 keeps the no-XNU-runtime safety envelope, preserves the bounded public pexpert object/link proof, and adds a Stage-owned XNU pmap/bootstrap allocation contract for Xiaomi Mi 4 cancro / MSM8974 ARMv7.

The public object subset remains intentionally stable:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage57/xnu_object_shims.c
```

The new Stage57 work is **not** a public `arm_vm_init.c` or `pmap.c` compile. Those public VM/pmap sources remain reference-only. Stage57 models selected public ARM VM/pmap constants and arithmetic inside Stage-owned code, layered on top of the already-proven bootstrap mapping tuple and the Stage-owned allocator/workspace snapshot.

## Safety model

Stage57 still does **not** run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute public ARM pexpert/platform runtime code, does not execute public ARM VM/pmap runtime code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed XNU physical load addresses, does not write proposed TTE workspace addresses, does not install live proposed XNU/pmap tables, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

Retained boundaries:

- Non-persistent `fastboot boot` only for hardware validation.
- No partition flash/erase/write.
- No bootloader or partition changes.
- No parsed Mach-O entrypoint execution.
- No generated Mach-O fixture execution.
- No public-XNU object execution on hardware.
- No public ARM pexpert/platform runtime execution.
- No public ARM `arm_vm_init.c` / `pmap.c` execution.
- No real XNU `_start` / `arm_init` handoff.
- No full public `mach_kernel` build attempt.
- No dependency-heavy ARM bring-up source inclusion (`start.s`, `arm_init.c`, `arm_vm_init.c`, `pmap.c`).
- No mutation of `external/xnu-upstream` or `external/xnu-4570.1.46`.
- No writes to proposed XNU physical load addresses.
- No writes to proposed XNU TTE workspace physical addresses.
- No use of the proposed XNU TTE workspace as a live TTBR table.
- No live proposed XNU/pmap table install.
- Controlled TTBR0 round-trip remains Stage-owned and restored.
- Original TTBR0/TTBCR/DACR/SCTLR state remains restored after inherited selftests.
- Caches remain disabled/unchanged.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- Public-XNU graph/object/link outputs stay ignored under `out/stage57/`.
- Public source checkouts stay ignored under `external/`.

The Stage57 completion message is intentionally explicit:

```text
Stage57 XNU execution disabled: bootstrap mapping and pmap/bootstrap allocation contracts proved from stage-owned loader/TTE/highVA/safe-table/TTBR and allocator/workspace snapshot facts; public arm_vm_init/pmap references are arithmetic-only and compile/link/execute counts are zero; pexpert/platform compile graph classified, bounded public pe_gen plus ARM pe_bootargs and ARM consistent_debug objects compiled, and controlled ARM ELF link proof recorded; inert MH_PRELOAD fixture carries metadata only; no full mach_kernel build, no public-XNU execution, no platform runtime execution, no Mach-O execution, no proposed physical/workspace writes, no live pmap table install, no persistent writes, caches unchanged
```

## What Stage57 adds

Stage57 keeps the bounded public object/link proof stable and adds:

- a standalone `stage57/` payload with `0x57000001` success status and `MI4IOS6_STAGE57` logs,
- command-line markers for `xnu-bootstrap-contract`, `xnu-pmap-bootstrap-contract`, and `pmap-bootstrap-reference-only`,
- a Stage-owned `struct stage57_pmap_bootstrap_snapshot` exported from `mmu.c`,
- a Stage-owned `struct stage57_xnu_pmap_bootstrap_contract` ABI in `stage57.h`,
- `stage57/xnu_pmap_bootstrap_contract.c`,
- loader preflight roll-up fields for `xnu_pmap_bootstrap_contract`,
- compile graph reference-only tracking for public ARM VM/pmap files,
- target-side proof that public pmap/VM compile/link/execute counts are zero,
- target-side proof that no live pmap table was installed.

The pmap/bootstrap allocation contract consumes the bootstrap mapping contract and Stage-owned snapshot facts. It models public-XNU-derived arithmetic only:

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

## Selected public XNU baselines

Stage57 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded compile/link proof objects and for blocked/reference-only source classification.

## Public VM/pmap reference-only surface

Stage57 reads/models these later-public ARM files only as public reference material:

```text
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
external/xnu-4570.1.46/osfmk/arm/pmap.h
external/xnu-4570.1.46/osfmk/arm/proc_reg.h
external/xnu-4570.1.46/osfmk/mach/arm/vm_param.h
```

They are not compiled into the public object subset, not linked into the proof ELF, and not executed on target hardware.

Modeled public constants/facts include:

```text
PAGE_SIZE=0x00001000
MEM_SIZE_MAX=0x40000000
VM_MIN_KERNEL_ADDRESS=0x80000000
VM_MAX_KERNEL_ADDRESS=0xfffeffff
L1_ALIGNMENT=0x00004000
SECTION_SIZE=0x00100000
SECTION_DESC_SO=0x00010c02
```

## Compile graph

`stage57/xnu_compile_graph_scan.py` runs after workspace validation and before object compilation. It writes generated, ignored artifacts under `out/stage57/`.

The scanner classifies twenty candidates. It allows only the five bounded public pexpert sources and treats ARM VM/pmap sources as reference-only or blocked runtime:

```text
external/xnu-upstream/pexpert/gen/device_tree.c                 compile/link allowed
external/xnu-upstream/pexpert/gen/bootargs.c                    compile/link allowed
external/xnu-upstream/pexpert/gen/pe_gen.c                      compile/link allowed
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c                compile/link allowed
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c        compile/link allowed
external/xnu-4570.1.46/pexpert/pexpert/arm/boot.h               ABI reference only
external/xnu-4570.1.46/pexpert/pexpert/arm/consistent_debug.h   ABI reference only
external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c        blocked-runtime reference
external/xnu-4570.1.46/pexpert/arm/pe_init.c                    blocked-runtime reference
external/xnu-4570.1.46/pexpert/arm/pe_kprintf.c                 blocked-runtime reference
external/xnu-4570.1.46/pexpert/arm/pe_serial.c                  blocked-runtime reference
external/xnu-upstream/pexpert/i386/pe_serial.c                  wrong-arch reference only
external/xnu-upstream/pexpert/i386/pe_kprintf.c                 wrong-arch reference only
external/xnu-4570.1.46/osfmk/arm/start.s                        excluded high-risk
external/xnu-4570.1.46/osfmk/arm/arm_init.c                     excluded high-risk
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c                  pmap arithmetic reference only; runtime blocked
external/xnu-4570.1.46/osfmk/arm/pmap.c                         pmap arithmetic/reference only; runtime blocked
external/xnu-4570.1.46/osfmk/arm/pmap.h                         pmap ABI reference only
external/xnu-4570.1.46/osfmk/arm/proc_reg.h                     processor-register ABI reference only
external/xnu-4570.1.46/osfmk/mach/arm/vm_param.h                VM parameter reference only
```

Successful graph status from the final local build:

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
stage57_xnu_compile_graph_no_public_xnu_exec=0x00000001
stage57_xnu_compile_graph_no_platform_runtime_exec=0x00000001
stage57_xnu_compile_graph_no_macho_exec=0x00000001
stage57_xnu_compile_graph_no_external_mutation=0x00000001
stage57_xnu_compile_graph_outputs_ignored=0x00000001
stage57_xnu_compile_graph_fail_closed=0x00000001
```

## Object subset and controlled link proof

Compiled public sources:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
```

Stage-owned support object:

```text
stage57/xnu_object_shims.c
```

Successful object-subset status:

```text
stage57_xnu_object_subset_status=0x57000001
stage57_xnu_object_subset_required_mask=0x07ffffff
stage57_xnu_object_subset_satisfied_mask=0x07ffffff
stage57_xnu_object_subset_failure_mask=0x00000000
stage57_xnu_object_count=0x00000006
stage57_xnu_object_public_2050_count=0x00000003
stage57_xnu_object_public_arm_pexpert_count=0x00000002
stage57_xnu_object_stage_owned_shim_count=0x00000001
stage57_xnu_object_duplicate_symbol_count=0x00000000
stage57_xnu_object_no_public_xnu_exec=0x00000001
stage57_xnu_object_no_platform_runtime_exec=0x00000001
stage57_xnu_object_no_external_mutation=0x00000001
```

The controlled host-only ARM ELF link proof records:

```text
stage57_xnu_link_status=0x57000001
stage57_xnu_link_required_mask=0x0001ffff
stage57_xnu_link_satisfied_mask=0x0001ffff
stage57_xnu_link_failure_mask=0x00000000
stage57_xnu_link_object_count=0x00000006
stage57_xnu_link_support_object_count=0x00000002
stage57_xnu_link_undefined_symbol_count=0x00000000
stage57_xnu_link_global_symbol_count=0x0000002c
stage57_xnu_link_elf_bytes=0x0000a25c
stage57_xnu_link_elf_sha32=0x0dfb7571
stage57_xnu_link_text_addr=0x80008000
stage57_xnu_link_text_size=0x000012c6
stage57_xnu_link_data_addr=0x800092d0
stage57_xnu_link_data_size=0x000004b8
stage57_xnu_link_bss_addr=0x80009790
stage57_xnu_link_bss_size=0x00005020
stage57_xnu_link_public_only=0x00000001
stage57_xnu_link_no_full_xnu_build=0x00000001
stage57_xnu_link_no_public_xnu_exec=0x00000001
stage57_xnu_link_no_macho_exec=0x00000001
stage57_xnu_link_no_platform_runtime_exec=0x00000001
stage57_xnu_link_no_external_mutation=0x00000001
stage57_xnu_link_outputs_ignored=0x00000001
stage57_xnu_link_fail_closed=0x00000001
```

`arm-none-eabi-nm -u out/stage57/xnu-link/stage57-xnu-link.elf` reports no undefined symbols.

## Bootstrap mapping contract

`stage57/xnu_bootstrap_contract.c` records the Stage-owned bootstrap mapping contract over inert Mach-O parse/load-plan, local staging/materialization, TTE dry-run, high-VA descriptor facts, safe-table local-only materialization, controlled TTBR0 round-trip/restore, cache preservation, compile graph, object subset, and controlled host-only ARM ELF link facts.

Confirmed target markers:

```text
stage57_xnu_bootstrap_contract_status=0x57000001
stage57_xnu_bootstrap_contract_required_mask=0x7fffffff
stage57_xnu_bootstrap_contract_failure_mask=0x00000000
stage57_xnu_bootstrap_contract_checksum=0x80249539
stage57_xnu_bootstrap_contract_proposed_virtBase=0x80008000
stage57_xnu_bootstrap_contract_proposed_physBase=0x80000000
stage57_xnu_bootstrap_contract_proposed_topOfKernelData=0x8000c000
stage57_xnu_bootstrap_contract_proposed_avail_start=0x80016000
stage57_xnu_bootstrap_contract_proposed_avail_end=0xde500000
loader_xnu_bootstrap_contract_status_rollup=0x57000001
```

## Pmap/bootstrap allocation contract

`stage57/xnu_pmap_bootstrap_contract.c` records a dependent Stage-owned pmap/bootstrap allocation contract. It imports the bootstrap mapping contract, Stage-owned pmap snapshot, TTBR/cache safety facts, graph/object/link facts, and reference-only public pmap classification.

Confirmed contract markers:

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

Modeled public ARM VM/pmap arithmetic markers:

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

Negative safety markers:

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

## Built image

```bash
/mnt/data/mi4-ios6/stage57/build.sh
```

Final local build hashes:

```text
751cf2aa208d4324faaf17ce7da70adbb78956be8aef1a5777e03f8e91587389  out/stage57/stage57_fixture.macho
4d00bfcb3296254a8663197d7cf42b431ae120ed3d3fef3ab2a56b67b479a92d  out/stage57/stage57.elf
9c9d82dd151b4f538f622ff4385ea88b946c470e1d6a222175dcf42436820869  out/stage57/stage57.bin
adea5a9f885ae3d82b0d48a6376553e2ceb43f4a5f11ed3d14eca06b9b0c1d8a  out/stage57/stage57.img
8746feb4d67b99ba600590fda7a93de0bc215ee0c3ff07e7717c98e532f708fd  out/stage57/stage57-qcdt.img
```

Size summary:

```text
text=196900 data=0 bss=251424 dec=448324 hex=6d744
```

Boot image parse highlights:

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

## Validation status

Current local validation completed:

- `stage57/build.sh` succeeds.
- `tools/parse_android_bootimg.py out/stage57/stage57.img` succeeds.
- `tools/parse_android_bootimg.py out/stage57/stage57-qcdt.img` succeeds and confirms the cancro-required QCDT `dt_size=2521088`.
- `arm-none-eabi-nm -u out/stage57/stage57.elf` prints no undefined symbols.
- `arm-none-eabi-nm -u out/stage57/xnu-link/stage57-xnu-link.elf` prints no undefined symbols.
- Static stale-reference scan finds no stale previous-stage code markers under `stage57/`.
- `external/xnu-upstream` and `external/xnu-4570.1.46` remain clean.
- `out/stage57/` and `external/` remain ignored by git.

During validation, an overlong `boot_args.CommandLine` string was found to overflow the fixed 256-byte public ARM boot-args field and corrupt the adjacent Apple-DT buffer, producing an Apple-DT walk mismatch on target. Stage57 now uses a shorter command line plus a bounded copy and explicit final NUL byte for `CommandLine[255]`.

Hardware validation completed with non-persistent boot only:

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

Confirmed hardware markers after non-persistent boot:

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
