# Stage63 — MSM8974 XNU pexpert interrupt/timer hook readiness contract

Stage63 keeps the Stage62 no-XNU-runtime safety envelope and adds a Stage-owned **MSM8974/XNU pexpert interrupt/timer hook readiness contract** for Xiaomi Mi 4 `cancro`. It proves that the already-live Stage-owned PE_state, Apple flattened device tree, GIC snapshot, SGI IRQ path, ARM generic timer IRQ path, and 19.2 MHz timebase facts are coherent enough to model the next XNU platform-expert hook boundary while still avoiding any public-XNU/platform runtime execution.

The new Stage63 contract is a readiness proof only. It does not call public ARM pexpert runtime code, does not enter XNU `_start` / `arm_init`, does not install live proposed pmap tables, does not write proposed TTBR/TTBCR/DACR/SCTLR values, does not invalidate TLBs for a proposed pmap install, does not change cache policy, does not execute the inert Mach-O fixture, and does not perform persistent writes.

## Public object subset

The bounded public object subset remains intentionally stable:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage63/xnu_object_shims.c
```

Public ARM VM/pmap sources remain reference-only. Stage63 still does **not** compile, link, or execute public `arm_vm_init.c` or `pmap.c`.

## Safety model

Stage63 still does **not** run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute public ARM pexpert/platform runtime code, does not execute public ARM VM/pmap runtime code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed XNU physical load addresses, does not write proposed TTE/pmap workspace physical addresses, does not install live proposed XNU/pmap tables, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

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
- Public-XNU graph/object/link outputs stay ignored under `out/stage63/`.
- Public source checkouts stay ignored under `external/`.

## What Stage63 adds

Stage63 keeps the Stage62 transition-prerequisite dry-run stable and adds:

- a standalone `stage63/` payload with `0x63000001` success status and `MI4IOS6_STAGE63` logs,
- command-line markers for `xnu-pexpert-hook-readiness-contract`, `pexpert-hook-ready`, and `irq-timer-hook-readiness`,
- a Stage-owned `struct stage63_xnu_pexpert_hook_readiness_contract` ABI in `stage63.h`,
- `stage63/xnu_pexpert_hook_readiness_contract.c`,
- loader preflight roll-up fields for `xnu_pexpert_hook_readiness_contract`,
- a readiness contract that imports public compile graph/object/link roll-ups and the Stage62 pmap transition dry-run roll-up,
- exact Stage-owned PE_state checks for boot args, device-tree pointer/length, memory, CPU count, machine type, vector base, GIC bases, timer base, and timer frequency,
- Apple-DT and `/chosen/boot-args` marker checks for `mi4ios6.stage=63` and `pexpert-hook-ready`,
- GICv2 base/snapshot/capacity checks for `0xf9000000` / `0xf9002000`, at least 288 IRQs, and four CPU interfaces,
- SGI0 hook readiness from the returnable IRQ selftest,
- ARM generic physical timer hook readiness with PPI IDs 18/19 and mask `0x000c0000`,
- timebase hook readiness from both Stage-owned timebase and XNU-adjacent `ml_*` shims at 19.2 MHz,
- platform/interrupt roll-up proof without adding a 33rd top-level loader satisfied bit,
- negative boundary proof that public pexpert/platform runtime, public pmap runtime, generated Mach-O, XNU `_start`, proposed pmap table install/control writes, TLB invalidation, cache changes, and persistent writes remain disabled.

Because the 32-bit loader satisfied mask was already fully consumed by Stage62, Stage63 records the pexpert hook readiness as a contract roll-up that refines the existing platform/interruption readiness gates instead of adding a new top-level loader bit. Final hardware validation still reaches `loader_satisfied_mask=0xffffffff`.

## Selected public XNU baselines

Stage63 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded compile/link proof objects and for blocked/reference-only source classification.

## Public VM/pmap reference-only surface

Stage63 reads/models these later-public ARM files only as public reference material:

```text
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
external/xnu-4570.1.46/osfmk/arm/pmap.h
external/xnu-4570.1.46/osfmk/arm/proc_reg.h
external/xnu-4570.1.46/osfmk/vm/pmap.h
external/xnu-4570.1.46/osfmk/mach/arm/vm_param.h
```

They are not compiled into the public object subset, not linked into the proof ELF, and not executed on target hardware.

## Compile graph, object subset, and link proof

`stage63/xnu_compile_graph_scan.py` classifies twenty candidates and allows only the five bounded public pexpert sources. ARM VM/pmap sources remain reference-only or blocked runtime inputs.

Successful graph/object/link markers from local validation:

```text
stage63_xnu_compile_graph_status=0x63000001
stage63_xnu_compile_graph_required_mask=0x1fffffff
stage63_xnu_compile_graph_satisfied_mask=0x1fffffff
stage63_xnu_compile_graph_failure_mask=0x00000000
stage63_xnu_compile_graph_candidate_count=0x00000014
stage63_xnu_compile_graph_allowed_compile_count=0x00000005
stage63_xnu_compile_graph_allowed_link_count=0x00000005
stage63_xnu_compile_graph_forbidden_count=0x0000000f
stage63_xnu_compile_graph_pmap_reference_mask=0x0000001f
stage63_xnu_compile_graph_pmap_runtime_blocked_mask=0x00000003
stage63_xnu_compile_graph_pmap_reference_count=0x00000005
stage63_xnu_compile_graph_pmap_public_compile_count=0x00000000
stage63_xnu_compile_graph_pmap_public_link_count=0x00000000
stage63_xnu_compile_graph_pmap_reference_only=0x00000001
stage63_xnu_object_subset_status=0x63000001
stage63_xnu_object_count=0x00000006
stage63_xnu_object_public_arm_pexpert_count=0x00000002
stage63_xnu_object_duplicate_symbol_count=0x00000000
stage63_xnu_object_pe_state_abi_shim_ready=0x00000001
stage63_xnu_object_consistent_debug_abi_shim_ready=0x00000001
stage63_xnu_link_status=0x63000001
stage63_xnu_link_object_count=0x00000006
stage63_xnu_link_undefined_symbol_count=0x00000000
```

`arm-none-eabi-nm -u out/stage63/stage63.elf` and `arm-none-eabi-nm -u out/stage63/xnu-link/stage63-xnu-link.elf` both report no undefined symbols.

## Retained pmap transition prerequisite

Stage63 retains all Stage62 XNU mapping/pmap prerequisites as fail-closed source facts:

```text
stage63_xnu_bootstrap_contract_status=0x63000001
stage63_xnu_pmap_bootstrap_contract_status=0x63000001
stage63_xnu_pmap_table_dryrun_contract_status=0x63000001
stage63_xnu_pmap_page_dryrun_contract_status=0x63000001
stage63_xnu_pmap_attr_dryrun_contract_status=0x63000001
stage63_xnu_pmap_multiwindow_dryrun_contract_status=0x63000001
stage63_xnu_pmap_transition_dryrun_contract_status=0x63000001
```

The retained safe live-table transition prerequisite remains local/read-only: it mirrors the inherited local L1 coarse-table descriptor plan into a separate Stage-owned candidate L1 buffer, uses the already-local L2-bank tables for software translations, records proposed control-register values for a later transition, and proves the candidate is distinct from both the recovery L1 and the proposed pmap workspace L1.

Confirmed Stage63 hardware markers for the retained transition prerequisite:

```text
stage63_xnu_pmap_transition_dryrun_contract_status=0x63000001
stage63_xnu_pmap_transition_dryrun_contract_required_mask=0x00ffffff
stage63_xnu_pmap_transition_dryrun_contract_satisfied_mask=0x00ffffff
stage63_xnu_pmap_transition_dryrun_contract_failure_mask=0x00000000
loader_xnu_pmap_transition_dryrun_contract_status_rollup=0x63000001
```

## Pexpert hook readiness contract

`stage63/xnu_pexpert_hook_readiness_contract.c` imports:

- public compile graph/object/link statuses, masks, and checksums,
- the retained pmap transition dry-run contract status/mask/checksum,
- loader safety mask and Apple-DT semantic mask,
- PE_state boot args/device-tree/memory/CPU/GIC/timer/vector facts,
- `/chosen/boot-args` readiness markers,
- target-side GIC snapshot facts,
- SGI0 selftest observations,
- timer PPI and timer IRQ selftest observations,
- Stage-owned and XNU-adjacent timebase frequency facts,
- negative public-runtime/pmap/Mach-O/persistent-write counters.

Confirmed hardware contract markers:

```text
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_readiness_contract_status=0x63000001
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_readiness_contract_required_mask=0x07ffffff
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_readiness_contract_satisfied_mask=0x07ffffff
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_readiness_contract_failure_mask=0x00000000
MI4IOS6_STAGE63_XNU stage63_xnu_pexpert_hook_readiness_contract_checksum=0x6cabcf99
MI4IOS6_STAGE63_XNU loader_xnu_pexpert_hook_readiness_contract_status=0x63000001
MI4IOS6_STAGE63_XNU loader_xnu_pexpert_hook_readiness_contract_status_rollup=0x63000001
```

Confirmed hardware PE/GIC/timer/timebase facts:

```text
stage63_xnu_pexpert_hook_boot_args_stage63_marker=0x00000001
stage63_xnu_pexpert_hook_boot_args_pexpert_marker=0x00000001
stage63_xnu_pexpert_hook_pe_cpu_count=0x00000004
stage63_xnu_pexpert_hook_pe_gic_dist_base=0xf9000000
stage63_xnu_pexpert_hook_pe_gic_cpu_base=0xf9002000
stage63_xnu_pexpert_hook_pe_timer_base=0xf9020000
stage63_xnu_pexpert_hook_pe_timer_frequency=0x0124f800
stage63_xnu_pexpert_hook_gic_irq_count=0x00000120
stage63_xnu_pexpert_hook_gic_cpu_interface_count=0x00000004
stage63_xnu_pexpert_hook_sgi_irq_count=0x00000001
stage63_xnu_pexpert_hook_sgi_sgi0_count=0x00000001
stage63_xnu_pexpert_hook_timer_ppi_mask=0x000c0000
stage63_xnu_pexpert_hook_timer_irq_count=0x00000001
stage63_xnu_pexpert_hook_timer_timer_count=0x00000001
stage63_xnu_pexpert_hook_timer_last_irq_id=0x00000013
stage63_xnu_pexpert_hook_timebase_freq_hz=0x0124f800
stage63_xnu_pexpert_hook_platform_gap_mask=0x0000001f
stage63_xnu_pexpert_hook_interrupt_ready_mask=0x0000000f
```

Confirmed negative safety markers:

```text
stage63_xnu_pexpert_hook_no_platform_runtime_exec=0x00000001
stage63_xnu_pexpert_hook_no_live_pmap_tables_installed=0x00000000
stage63_xnu_pexpert_hook_pmap_ttbr_written=0x00000000
stage63_xnu_pexpert_hook_pmap_tlbs_invalidated=0x00000000
stage63_xnu_pexpert_hook_persistent_write_attempted=0x00000000
```

## Build artifacts

Local build command:

```bash
/mnt/data/mi4-ios6/stage63/build.sh
```

Final size:

```text
text=255824 data=0 bss=398060 dec=653884 hex=9fa3c
```

Hashes:

```text
f5a2acf33446dd1625c007af8596bbf85c1a180bf92c1ee464028868c58ffe4d  out/stage63/stage63_fixture.macho
d991ec3f6aa54fa09653dfe5274d01e09084d1b28007e64ea2dc687393fc0e0f  out/stage63/stage63.elf
9632f2f988f7706fd9f0891ffb2d32e6ed67d42e608264d0b15c8345fae538df  out/stage63/stage63.bin
cf2b9a46fd633bfda7bbe5ac7ba0b63950f4ae4579e239e37d476be3245c4029  out/stage63/stage63.img
5fbeedcae7628aa7c0f72bcf6ef1454eba6b1dd00ad364a5d11c219ff73e996a  out/stage63/stage63-qcdt.img
```

## Boot image parse facts

```text
out/stage63/stage63.img:
  kernel_size=255824
  page_size=2048
  dt_size=0
  kernel_sha256=9632f2f988f7706fd9f0891ffb2d32e6ed67d42e608264d0b15c8345fae538df

out/stage63/stage63-qcdt.img:
  kernel_size=255824
  page_size=2048
  dt_size=2521088 (0x267800)
  kernel_sha256=9632f2f988f7706fd9f0891ffb2d32e6ed67d42e608264d0b15c8345fae538df
  dt_sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

The QCDT image preserves the cancro-required legacy Android v0 `dt_size=2521088` field.

## Local validation

Local validation performed:

```text
/mnt/data/mi4-ios6/stage63/build.sh
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py out/stage63/stage63.img
/mnt/data/mi4-ios6/tools/parse_android_bootimg.py out/stage63/stage63-qcdt.img
arm-none-eabi-nm -u out/stage63/stage63.elf
arm-none-eabi-nm -u out/stage63/xnu-link/stage63-xnu-link.elf
```

Validation facts:

```text
stage63/boot_args.c command_line_len_with_nul=182 fits256=True
stage63/stage63_main.c command_line_len_with_nul=182 fits256=True
stage63/xnu_object_shims.c command_line_len_with_nul=159 fits256=True
stale_marker_violations=0
public_vm_pmap_command_violations=0
out/stage63 ignored by .gitignore
external/xnu-upstream clean
external/xnu-4570.1.46 clean
```

## Hardware validation

Hardware validation used non-persistent `fastboot boot` only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage63/stage63-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage63-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage63-last_kmsg.txt
size=178383 bytes
stage63_marker_count=2549
stage63_xnu_marker_count=2523
```

Final success markers:

```text
MI4IOS6_STAGE63_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE63_XNU loader_status=0x63000001
MI4IOS6_STAGE63_XNU kernel_entry ok
MI4IOS6_STAGE63 kernel_entry returned success
```

## Result

Stage63 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned MSM8974 pexpert interrupt/timer hook readiness contract while preserving the bounded public-XNU object/link proof and all no-execution/no-live-pmap-install/no-persistent-write boundaries.

The next safe direction is still-local IOKit/platform-driver scaffolding, a deeper MSM8974 pexpert implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
