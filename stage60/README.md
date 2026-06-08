# Stage60 — XNU pmap cache/MMU attribute dry-run contract

Stage60 keeps the no-XNU-runtime safety envelope, preserves the bounded public pexpert object/link proof, preserves the Stage56 bootstrap mapping contract, preserves the Stage57 pmap/bootstrap allocation contract, preserves the Stage58 local section-table dry-run, preserves the Stage59 page-granular coarse/L2 dry-run, and adds a Stage-owned **XNU pmap cache/MMU attribute dry-run contract** for Xiaomi Mi 4 cancro / MSM8974 ARMv7.

The new proof advances beyond Stage59 by modeling the exact ARMv7 short-descriptor attribute encodings used by later public ARM XNU references: `CACHE_ATTRINDX_*`, `AP_*`, `ARM_PTE_ATTRINDX()`, `ARM_TTE_BLOCK_ATTRINDX()`, WIMG-to-PTE mapping, page-protection helper templates, section descriptor attributes, and small-page PTE attributes. The contract builds and validates only local Stage-owned arithmetic facts. It does not write the proposed pmap workspace, does not install live XNU/pmap tables, does not write TTBR/TTBCR/DACR/SCTLR for proposed tables, does not invalidate TLBs for proposed tables, does not change caches, and does not execute public VM/pmap runtime code.

## Public object subset

The public object subset remains intentionally stable:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage60/xnu_object_shims.c
```

Public ARM VM/pmap sources remain reference-only. Stage60 still does **not** compile, link, or execute public `arm_vm_init.c` or `pmap.c`.

## Safety model

Stage60 still does **not** run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute public ARM pexpert/platform runtime code, does not execute public ARM VM/pmap runtime code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed XNU physical load addresses, does not write proposed TTE/pmap workspace addresses, does not install live proposed XNU/pmap tables, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

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
- No writes to proposed XNU TTE/pmap workspace physical addresses.
- No use of the proposed XNU TTE/pmap workspace as a live TTBR table.
- No live proposed XNU/pmap table install.
- No TTBR/TTBCR/DACR/SCTLR writes for proposed pmap install.
- No TLB invalidation for proposed pmap install.
- Controlled TTBR0 round-trip remains Stage-owned and restored.
- Original TTBR0/TTBCR/DACR/SCTLR state remains restored after inherited selftests.
- Caches remain disabled/unchanged.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- Public-XNU graph/object/link outputs stay ignored under `out/stage60/`.
- Public source checkouts stay ignored under `external/`.

The Stage60 completion message is intentionally explicit:

```text
Stage60 XNU execution disabled: bootstrap mapping, pmap/bootstrap allocation, local pmap section-table dry-run, local page-granular coarse/L2 dry-run, and exact pmap cache/MMU attribute dry-run contracts proved from stage-owned loader/TTE/highVA/safe-table/TTBR and allocator/workspace snapshot facts; public arm_vm_init/pmap references are arithmetic-only and compile/link/execute counts are zero; pexpert/platform compile graph classified, bounded public pe_gen plus ARM pe_bootargs and ARM consistent_debug objects compiled, and controlled ARM ELF link proof recorded; inert MH_PRELOAD fixture carries metadata only; no full mach_kernel build, no public-XNU execution, no platform runtime execution, no Mach-O execution, no proposed physical/workspace writes, no live pmap table install, no TLB invalidate for pmap install, no cache change, no persistent writes
```

## What Stage60 adds

Stage60 keeps the Stage59 page-granular dry-run stable, and adds:

- a standalone `stage60/` payload with `0x60000001` success status and `MI4IOS6_STAGE60` logs,
- command-line markers for `xnu-pmap-attr-dryrun-contract` and `pmap-attr-local-dryrun-only`,
- a Stage-owned `struct stage60_xnu_pmap_attr_dryrun_contract` ABI in `stage60.h`,
- `stage60/xnu_pmap_attr_dryrun_contract.c`,
- a loader satisfied bit `STAGE60_LOADER_SAT_XNU_PMAP_ATTR_DRYRUN_CONTRACT`,
- loader preflight roll-up fields for `xnu_pmap_attr_dryrun_contract`,
- public XNU `CACHE_ATTRINDX_*` modeling:
  - writeback `0`, write-combine `1`, write-through `2`, disabled/posted `3`, inner-writeback `4`, default `0`,
- public XNU `AP_*` modeling:
  - `AP_RWNA=0`, `AP_RWRW=1`, `AP_RONA=2`, `AP_RORO=3`,
- public ARMv7 short-descriptor attribute macro modeling:
  - PTE attr index bits `CB[3:2]` and `TEX0[6]`,
  - section/TTE attr index bits `CB[3:2]` and `TEX0[12]`,
  - PTE AP bits `AP[5]`/`APX[9]`,
  - section AP bits `AP[11]`/`APX[15]`,
- public `wimg_to_pte()`-style WIMG mapping for default/copyback, IO/posted, write-combine, write-through, and inner-writeback,
- public `arm_vm_page_granular_*` template modeling for RWX/RWNX/ROX/RONX page templates,
- readback cross-checks against the inherited Stage58/Stage59 table/page dry-run attribute words,
- target-side proof that public pmap compile/link/execute counts remain zero,
- target-side proof that no proposed workspace was written and no live pmap table was installed,
- target-side proof that TTBR/TTBCR/DACR/SCTLR write counts, TLB invalidation counts, persistent-write counts, and cache-change counts remain zero.

## Selected public XNU baselines

Stage60 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded compile/link proof objects and for blocked/reference-only source classification.

## Public VM/pmap reference-only surface

Stage60 reads/models these later-public ARM files only as public reference material:

```text
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
external/xnu-4570.1.46/osfmk/arm/pmap.h
external/xnu-4570.1.46/osfmk/arm/proc_reg.h
external/xnu-4570.1.46/osfmk/vm/pmap.h
external/xnu-4570.1.46/osfmk/mach/arm/vm_param.h
```

They are not compiled into the public object subset, not linked into the proof ELF, and not executed on target hardware.

Modeled Stage58 section-table constants/facts remain:

```text
PAGE_SIZE=0x00001000
MEM_SIZE_MAX=0x40000000
VM_MIN_KERNEL_ADDRESS=0x80000000
VM_MAX_KERNEL_ADDRESS=0xfffeffff
L1_ALIGNMENT=0x00004000
L1_ENTRY_COUNT=4096
L1_BYTES=0x00004000
SECTION_SIZE=0x00100000
SECTION_DESC_SO=0x00010c02
DESC_TYPE_SECTION=0x00000002
DESC_BASE_MASK=0xfff00000
```

Modeled Stage59 ARMv7 short-descriptor coarse-table/small-page constants remain:

```text
ARM_PGSHIFT=12
ARM_PGBYTES=0x00001000
ARM_PGMASK=0x00000fff
ARM_TT_L1_SIZE=0x00100000
ARM_TT_L1_PT_SIZE=0x00400000
ARM_TT_L2_SIZE=0x00001000
ARM_TT_L2_OFFMASK=0x00000fff
ARM_TT_L2_SHIFT=12
ARM_TT_L2_INDEX_MASK=0x000ff000
ARM_TTE_TYPE_TABLE=0x00000001
ARM_TTE_TYPE_MASK=0x00000003
ARM_TTE_TABLE_MASK=0xfffffc00
ARM_PTE_TYPE=0x00000002
ARM_PTE_TYPE_MASK=0x00000002
ARM_PTE_PAGE_MASK=0xfffff000
ARM_SMALL_PAGE_SIZE=0x00001000
PTE_ATTR_DEFAULT=0x00000412
```

The inherited Stage59 PTE attribute value corresponds to the public early small-page template shape:

```text
ARM_PTE_TYPE | ARM_PTE_AF | ARM_PTE_SH |
ARM_PTE_ATTRINDX(CACHE_ATTRINDX_DEFAULT) |
ARM_PTE_AP(AP_RWNA)
```

## Compile graph, object subset, and link proof

`stage60/xnu_compile_graph_scan.py` still classifies twenty candidates and allows only the five bounded public pexpert sources. ARM VM/pmap sources remain reference-only or blocked runtime inputs.

Successful graph/object/link markers from local and target validation:

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
stage60_xnu_object_subset_status=0x60000001
stage60_xnu_object_count=0x00000006
stage60_xnu_object_duplicate_symbol_count=0x00000000
stage60_xnu_link_status=0x60000001
stage60_xnu_link_object_count=0x00000006
stage60_xnu_link_undefined_symbol_count=0x00000000
```

`arm-none-eabi-nm -u out/stage60/stage60.elf` and `arm-none-eabi-nm -u out/stage60/xnu-link/stage60-xnu-link.elf` both report no undefined symbols.

## Bootstrap and pmap/bootstrap prerequisite contracts

Stage60 retains the Stage56/Stage57 contracts as prerequisites:

```text
stage60_xnu_bootstrap_contract_status=0x60000001
stage60_xnu_bootstrap_contract_required_mask=0x7fffffff
stage60_xnu_bootstrap_contract_failure_mask=0x00000000
loader_xnu_bootstrap_contract_status_rollup=0x60000001

stage60_xnu_pmap_bootstrap_contract_status=0x60000001
stage60_xnu_pmap_bootstrap_contract_required_mask=0x7fffffff
stage60_xnu_pmap_bootstrap_contract_satisfied_mask=0x7fffffff
stage60_xnu_pmap_bootstrap_contract_failure_mask=0x00000000
stage60_xnu_pmap_public_compile_count=0x00000000
stage60_xnu_pmap_public_link_count=0x00000000
stage60_xnu_pmap_public_execute_count=0x00000000
loader_xnu_pmap_bootstrap_contract_status_rollup=0x60000001
```

Modeled pmap/bootstrap arithmetic remains:

```text
stage60_xnu_pmap_gVirtBase=0x80008000
stage60_xnu_pmap_gPhysBase=0x80000000
stage60_xnu_pmap_gPhysSize=0x5e500000
stage60_xnu_pmap_boot_ttep=0x8000c000
stage60_xnu_pmap_cpu_ttep=0x80010000
stage60_xnu_pmap_initial_avail_start=0x80016000
stage60_xnu_pmap_avail_end=0xde500000
stage60_xnu_pmap_vstart=0xc0400000
stage60_xnu_pmap_virtual_space_end=0xfffeffff
stage60_xnu_pmap_workspace_base=0x80000000
stage60_xnu_pmap_workspace_limit=0x80100000
stage60_xnu_pmap_workspace_l1_table_phys=0x00074000
stage60_xnu_pmap_workspace_l1_table_virt=0xc0074000
stage60_xnu_pmap_workspace_l1_section_descriptor=0x00010c02
stage60_xnu_pmap_workspace_cache_policy=0x00000000
```

## Section-table dry-run prerequisite

`stage60/xnu_pmap_table_dryrun_contract.c` remains the Stage58 section-table dry-run, now renumbered and retained as a prerequisite. It imports the pmap/bootstrap allocation contract and snapshot facts, zeroes a Stage-owned local L1 buffer, simulates section descriptor population into that buffer, reads descriptors back, performs local software translations, and logs all fields through `xnu_log_kv32`.

Confirmed target-side prerequisite markers:

```text
stage60_xnu_pmap_table_dryrun_contract_status=0x60000001
stage60_xnu_pmap_table_dryrun_contract_required_mask=0x01ffffff
stage60_xnu_pmap_table_dryrun_contract_satisfied_mask=0x01ffffff
stage60_xnu_pmap_table_dryrun_contract_failure_mask=0x00000000
stage60_xnu_pmap_table_dryrun_contract_checksum=0x04263dca
stage60_xnu_pmap_table_dryrun_section_descriptor=0x00010c02
stage60_xnu_pmap_table_dryrun_descriptor_attr_mask_seen=0x00010c02
stage60_xnu_pmap_table_dryrun_translation_case_count=0x00000004
loader_xnu_pmap_table_dryrun_contract_status_rollup=0x60000001
```

## Page-granular pmap dry-run prerequisite

`stage60/xnu_pmap_page_dryrun_contract.c` imports the bootstrap/pmap prerequisites, zeroes Stage-owned local L1 and L2 buffers, creates four ARMv7 short-descriptor L1 coarse/table descriptors, populates 1024 small-page PTEs, verifies descriptor/PTE readback, performs local software translations, checksums the local buffers and contract, and logs all fields through `xnu_log_kv32`.

Confirmed target-side prerequisite markers:

```text
stage60_xnu_pmap_page_dryrun_contract_status=0x60000001
stage60_xnu_pmap_page_dryrun_contract_required_mask=0x00ffffff
stage60_xnu_pmap_page_dryrun_contract_satisfied_mask=0x00ffffff
stage60_xnu_pmap_page_dryrun_contract_failure_mask=0x00000000
stage60_xnu_pmap_page_dryrun_contract_checksum=0x9950cddf
loader_xnu_pmap_page_dryrun_contract_status=0x60000001
loader_xnu_pmap_page_dryrun_contract_satisfied_mask=0x00ffffff
loader_xnu_pmap_page_dryrun_contract_failure_mask=0x00000000
loader_xnu_pmap_page_dryrun_contract_checksum=0x9950cddf
loader_xnu_pmap_page_dryrun_contract_status_rollup=0x60000001
```

Public page-table constants recorded by the prerequisite:

```text
stage60_xnu_pmap_page_dryrun_l1_table_type=0x00000001
stage60_xnu_pmap_page_dryrun_l1_table_mask=0xfffffc00
stage60_xnu_pmap_page_dryrun_l2_page_bytes=0x00001000
stage60_xnu_pmap_page_dryrun_l2_coarse_table_bytes=0x00000400
stage60_xnu_pmap_page_dryrun_l2_pte_count=0x00000400
stage60_xnu_pmap_page_dryrun_pte_attr_default=0x00000412
```

Local-only L1/L2 buffer markers:

```text
stage60_xnu_pmap_page_dryrun_local_l1_base=0x00088000
stage60_xnu_pmap_page_dryrun_local_l1_limit=0x0008c000
stage60_xnu_pmap_page_dryrun_local_l2_base=0x00085000
stage60_xnu_pmap_page_dryrun_local_l2_limit=0x00086000
stage60_xnu_pmap_page_dryrun_local_l1_write_count=0x00000004
stage60_xnu_pmap_page_dryrun_local_l2_pte_write_count=0x00000400
```

Page-granular 4 MiB window markers:

```text
stage60_xnu_pmap_page_dryrun_window_virt_base=0x80000000
stage60_xnu_pmap_page_dryrun_window_virt_limit=0x80400000
stage60_xnu_pmap_page_dryrun_window_phys_base=0x80000000
stage60_xnu_pmap_page_dryrun_window_phys_limit=0x80400000
stage60_xnu_pmap_page_dryrun_l1_first_index=0x00000800
stage60_xnu_pmap_page_dryrun_l1_last_index=0x00000803
stage60_xnu_pmap_page_dryrun_l1_count=0x00000004
stage60_xnu_pmap_page_dryrun_l2_pte_first_index=0x00000000
stage60_xnu_pmap_page_dryrun_l2_pte_last_index=0x000003ff
stage60_xnu_pmap_page_dryrun_l2_pte_count=0x00000400
```

L1 descriptor and PTE readback markers:

```text
stage60_xnu_pmap_page_dryrun_l1_descriptor_first_word=0x00085001
stage60_xnu_pmap_page_dryrun_l1_descriptor_last_word=0x00085c01
stage60_xnu_pmap_page_dryrun_l1_descriptor_type_mask_seen=0x00000001
stage60_xnu_pmap_page_dryrun_l1_descriptor_attr_mask_seen=0x00000001
stage60_xnu_pmap_page_dryrun_pte_first_word=0x80000412
stage60_xnu_pmap_page_dryrun_pte_kernel_word=0x80008412
stage60_xnu_pmap_page_dryrun_pte_workspace_word=0x80000412
stage60_xnu_pmap_page_dryrun_pte_last_word=0x803ff412
stage60_xnu_pmap_page_dryrun_pte_type_mask_seen=0x00000002
stage60_xnu_pmap_page_dryrun_pte_attr_mask_seen=0x00000412
```

Software translation markers:

```text
stage60_xnu_pmap_page_dryrun_translation_kernel_va=0x80008000
stage60_xnu_pmap_page_dryrun_translation_kernel_pa=0x80008000
stage60_xnu_pmap_page_dryrun_translation_workspace_va=0x80000000
stage60_xnu_pmap_page_dryrun_translation_workspace_pa=0x80000000
stage60_xnu_pmap_page_dryrun_translation_window_first_va=0x80000000
stage60_xnu_pmap_page_dryrun_translation_window_first_pa=0x80000000
stage60_xnu_pmap_page_dryrun_translation_window_last_va=0x803fffff
stage60_xnu_pmap_page_dryrun_translation_window_last_pa=0x803fffff
stage60_xnu_pmap_page_dryrun_translation_case_count=0x00000004
```

## Pmap cache/MMU attribute dry-run contract

`stage60/xnu_pmap_attr_dryrun_contract.c` imports the table and page dry-run prerequisites, models public `proc_reg.h`, `pmap.h`, `pmap.c`, and `arm_vm_init.c` attribute arithmetic, verifies exact ARMv7 short-descriptor bit placements, checks the inherited section/PTE readbacks, and logs all fields through `xnu_log_kv32`.

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

Public cache/AP/WIMG constants recorded by the contract:

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

Prior-readback cross-check markers:

```text
stage60_xnu_pmap_attr_dryrun_prior_table_section_word=0x00010c02
stage60_xnu_pmap_attr_dryrun_prior_table_attr_seen=0x00010c02
stage60_xnu_pmap_attr_dryrun_prior_page_pte_word=0x00000412
stage60_xnu_pmap_attr_dryrun_prior_page_attr_seen=0x00000412
```

Negative safety markers:

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

## Built image

```bash
/mnt/data/mi4-ios6/stage60/build.sh
```

Final local build hashes:

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

Boot-image parse highlights:

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

## Validation status

Current local validation completed:

- `stage60/build.sh` succeeds.
- `tools/parse_android_bootimg.py out/stage60/stage60.img` succeeds.
- `tools/parse_android_bootimg.py out/stage60/stage60-qcdt.img` succeeds and confirms the cancro-required QCDT `dt_size=2521088`.
- `arm-none-eabi-nm -u out/stage60/stage60.elf` prints no undefined symbols.
- `arm-none-eabi-nm -u out/stage60/xnu-link/stage60-xnu-link.elf` prints no undefined symbols.
- Static stale-reference scan finds no stale Stage59 code markers under `stage60/` outside generated or historical context.
- `external/xnu-upstream` and `external/xnu-4570.1.46` remain ignored/untouched.
- `out/stage60/` and `external/` remain ignored by git.
- Fixed public ARM boot-args command lines remain under the 256-byte `CommandLine` field and keep the bounded-copy/final-NUL rule:
  - `stage60/boot_args.c`: `226/256` including NUL.
  - `stage60/stage60_main.c`: `240/256` including NUL.
  - `stage60/xnu_object_shims.c`: `196/256` including NUL.

Hardware validation completed with non-persistent boot only:

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
MI4IOS6_STAGE60_XNU loader_satisfied_mask=0x3fffffff
MI4IOS6_STAGE60_XNU loader_status=0x60000001
MI4IOS6_STAGE60_XNU kernel_entry ok
MI4IOS6_STAGE60 kernel_entry returned success
```

The device returned to Android after the PS_HOLD reset path. No persistent flash/erase/write was performed.

## Result

Stage60 completes the exact pmap cache/MMU attribute dry-run blocker. The new contract proves that the Stage-owned model can represent public ARM XNU cache attribute indices, AP encodings, ARMv7 short-descriptor PTE/TTE attribute bit placements, WIMG-derived PTE bits, page-protection helper templates, and inherited section/PTE readback words while preserving all no-execution, no-proposed-write, no-live-table-install, no-control-register-write, no-TLB-invalidate, no-cache-change, and no-persistent-write boundaries.

The next safe direction is broader page-granular pmap windows, MSM8974 pexpert/timer/interrupt hooks, IOKit/platform-driver scaffolding, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build or any XNU runtime handoff.
