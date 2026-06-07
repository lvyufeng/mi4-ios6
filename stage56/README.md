# Stage56 — XNU bootstrap mapping contract proof

Stage56 keeps the no-XNU-runtime safety envelope, preserves the Stage55 bounded public ARM pexpert object subset, and adds a Stage-owned XNU bootstrap mapping contract for Xiaomi Mi 4 cancro / MSM8974 ARMv7.

The Stage56 public object subset remains stable from Stage55:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
```

The new Stage56 implementation is Stage-owned `xnu_bootstrap_contract.c`. It derives page-granular `virtBase` / `physBase` / `topOfKernelData` / TTE workspace / `avail_start` / `avail_end` facts from the existing inert Mach-O loader preflight, staging, TTE dry-run, high-VA, safe-table, TTBR0-restore, cache-preservation, compile-graph, object-subset, and link-proof data. It does **not** execute public-XNU code, does **not** call the public consistent-debug helpers, does **not** run pexpert/platform runtime code, and does **not** write proposed XNU physical load or TTE workspace addresses.

## Safety model

Stage56 still does **not** run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not execute public ARM pexpert/platform runtime code, does not write proposed XNU physical load or TTE workspace addresses, does not use the proposed XNU TTE workspace as a live TTBR table, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

Specific retained boundaries:

- Non-persistent `fastboot boot` only for any hardware validation.
- No partition flash/erase/write.
- No bootloader or partition changes.
- No parsed Mach-O entrypoint execution.
- No generated Mach-O fixture execution.
- No public-XNU object execution on hardware.
- No public ARM pexpert/platform runtime execution.
- No real XNU `_start` / `arm_init` handoff.
- No full public `mach_kernel` build attempt.
- No dependency-heavy ARM bring-up source inclusion (`start.s`, `arm_init.c`, `arm_vm_init.c`, `pmap.c`).
- No mutation of `external/xnu-upstream` or `external/xnu-4570.1.46`.
- No writes to proposed XNU physical load addresses.
- No writes to proposed XNU TTE workspace physical addresses.
- No use of the proposed XNU TTE workspace as a live TTBR table.
- Inherited controlled TTBR0 round-trip remains Stage-owned and restored.
- Original TTBR0/TTBCR/DACR/SCTLR state remains restored after inherited selftests.
- Caches remain disabled/unchanged.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- Public-XNU graph/object/link outputs stay ignored under `out/stage56/`.
- Public source checkouts stay ignored under `external/`.

The Stage56 completion message is intentionally explicit:

```text
Stage56 XNU execution disabled: bootstrap mapping contract proved from stage-owned loader/TTE/highVA/safe-table/TTBR facts; pexpert/platform compile graph classified, bounded public pe_gen plus ARM pe_bootargs and ARM consistent_debug objects compiled, and controlled ARM ELF link proof recorded; inert MH_PRELOAD fixture carries metadata only; no full mach_kernel build, no public-XNU execution, no platform runtime execution, no Mach-O execution, no proposed physical/workspace writes, no persistent writes, caches unchanged
```

## What Stage56 adds over Stage55

Stage55 added the bounded public ARM `pe_consistent_debug.c` host-only proof. Stage56 intentionally does not add another public pexpert runtime object. Instead, it keeps the public object/link set stable and adds a Stage-owned bootstrap mapping contract:

- new standalone `stage56/` payload copied from previous stage,
- Stage56 status/log prefixes (`0x56000001`, `MI4IOS6_STAGE56`),
- command-line markers for:
  - `xnu-bootstrap-contract`,
  - `public-xnu-arm-consistent-debug`,
  - `stage56-xnu-link-proof`,
  - inherited `public-xnu-workspace`, `public-xnu-compile-graph`, `public-xnu-platform-graph`, `public-xnu-object-subset`, `public-xnu-controlled-link`, `public-xnu-bounded-pe-gen`, `public-xnu-arm-pe-bootargs`, `ttbr0-roundtrip`, `recovery-table`, `cache-bits-preserved`, `no-public-xnu-exec`, `no-platform-runtime-exec`, `no-macho-exec`, `no-proposed-phys-write`, `no-proposed-tte-write`, `no-cache-policy-change`, `no-persist-write`,
- expanded host-side compile graph scanner `stage56/xnu_compile_graph_scan.py`,
- expanded target-side graph ABI/logs in `stage56/xnu_compile_graph.h` / `.c`,
- new target-side bootstrap mapping contract ABI in `stage56/stage56.h`,
- new Stage-owned contract implementation in `stage56/xnu_bootstrap_contract.c`,
- new compile-only ABI shims:
  - `stage56/shims/pexpert/arm/consistent_debug.h`,
  - `stage56/shims/libkern/OSAtomic.h`,
  - `stage56/shims/machine/machine_routines.h`,
- Stage-owned host-proof support definitions for `OSCompareAndSwap64()` and `ml_map_high_window()` in `stage56/xnu_object_shims.c`,
- object-subset compiler kept at five public objects plus one Stage-owned support object,
- controlled ARM ELF link proof kept at the Stage55 six-object public/shim set,
- loader safety/status roll-up now includes `loader_xnu_bootstrap_contract_status_rollup`, retaining no-public-XNU-exec and no-platform-runtime-exec boundaries.

The important Stage56 boundary is that the bootstrap mapping contract is Stage-owned and consumes only already-safe preflight facts. The public objects contribute symbols only to the host-only proof, while the booted payload consumes generated facts proving those objects compiled and linked safely.

## Selected public XNU baselines

Stage56 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded compile/link proof objects and for blocked-runtime source classification.

## Public ARM `pe_consistent_debug.c` dependency surface

The new bounded public source defines only:

```text
PE_consistent_debug_inherit
PE_consistent_debug_register
PE_consistent_debug_enabled
```

Its meaningful external dependencies are:

```text
DTLookupEntry
DTGetProperty
OSCompareAndSwap64
ml_map_high_window
```

Stage56 already compiles/links the public 2050 device-tree parser that provides `DTLookupEntry` and `DTGetProperty`. It adds minimal Stage-owned compile/link shims for the remaining two dependencies:

```c
Boolean OSCompareAndSwap64(UInt64 oldValue, UInt64 newValue, volatile UInt64 *address)
{
    if (!address) {
        return FALSE;
    }
    if (*address != oldValue) {
        return FALSE;
    }
    *address = newValue;
    return TRUE;
}

vm_map_address_t ml_map_high_window(vm_offset_t phys_addr, vm_size_t len)
{
    (void)phys_addr;
    /* Stage56 host-proof support only; public consistent-debug code is never executed on hardware. */
    if (len == 0u || len > sizeof(g_stage56_xnu_consistent_debug_window)) {
        return 0u;
    }
    return (vm_map_address_t)(uintptr_t)g_stage56_xnu_consistent_debug_window;
}
```

These shims do not become a fake runtime debugger, high-window mapper, VM path, pmap path, platform runtime, or IOKit path. They only close a controlled host-proof link.

## Compile graph

`stage56/xnu_compile_graph_scan.py` runs after workspace validation and before object compilation. It writes generated, ignored artifacts under `out/stage56/`.

The scanner classifies seventeen candidates and explicitly blocks runtime-heavy public ARM pexpert sources:

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
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c                  excluded high-risk
external/xnu-4570.1.46/osfmk/arm/pmap.c                         excluded high-risk
```

Successful scanner status from the current local build:

```text
stage56_xnu_compile_graph_status=0x56000001
stage56_xnu_compile_graph_required_mask=0x1fffffff
stage56_xnu_compile_graph_satisfied_mask=0x1fffffff
stage56_xnu_compile_graph_failure_mask=0x00000000
stage56_xnu_compile_graph_candidate_count=0x00000011
stage56_xnu_compile_graph_allowed_compile_count=0x00000005
stage56_xnu_compile_graph_allowed_link_count=0x00000005
stage56_xnu_compile_graph_forbidden_count=0x0000000c
stage56_xnu_compile_graph_shim_required_count=0x00000003
stage56_xnu_compile_graph_max_risk_class=0x00000005
stage56_xnu_compile_graph_pe_gen_allowed=0x00000001
stage56_xnu_compile_graph_arm_bootargs_allowed=0x00000001
stage56_xnu_compile_graph_arm_consistent_debug_allowed=0x00000001
stage56_xnu_compile_graph_consistent_debug_layout_recorded=0x00000001
stage56_xnu_compile_graph_arm_pe_kprintf_blocked=0x00000001
stage56_xnu_compile_graph_arm_pe_serial_blocked=0x00000001
stage56_xnu_compile_graph_arm_pe_identify_machine_blocked=0x00000001
stage56_xnu_compile_graph_arm_pe_init_blocked=0x00000001
stage56_xnu_compile_graph_bootstrap_contract_selected=0x00000001
stage56_xnu_compile_graph_platform_reference_count=0x00000006
stage56_xnu_compile_graph_blocked_runtime_count=0x00000004
stage56_xnu_compile_graph_duplicate_symbol_count=0x00000000
stage56_xnu_compile_graph_pe_state_abi_recorded=0x00000001
stage56_xnu_compile_graph_boot_args_arm_layout_recorded=0x00000001
stage56_xnu_compile_graph_4570_bounded_reference_policy=0x00000001
stage56_xnu_compile_graph_no_full_xnu_build=0x00000001
stage56_xnu_compile_graph_no_public_xnu_exec=0x00000001
stage56_xnu_compile_graph_no_macho_exec=0x00000001
stage56_xnu_compile_graph_no_platform_runtime_exec=0x00000001
stage56_xnu_compile_graph_no_external_mutation=0x00000001
stage56_xnu_compile_graph_outputs_ignored=0x00000001
stage56_xnu_compile_graph_fail_closed=0x00000001
```

## Object subset

Compiled public sources:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
```

Stage56-owned support object:

```text
stage56/xnu_object_shims.c
```

Successful compile status from the current local build:

```text
stage56_xnu_object_subset_status=0x56000001
stage56_xnu_object_subset_required_mask=0x07ffffff
stage56_xnu_object_subset_satisfied_mask=0x07ffffff
stage56_xnu_object_subset_failure_mask=0x00000000
stage56_xnu_object_source_mask=0x0000001f
stage56_xnu_object_shim_mask=0x000003ff
stage56_xnu_object_count=0x00000006
stage56_xnu_object_public_2050_count=0x00000003
stage56_xnu_object_public_arm_pexpert_count=0x00000002
stage56_xnu_object_stage_owned_shim_count=0x00000001
stage56_xnu_object_duplicate_symbol_count=0x00000000
stage56_xnu_object_pe_state_abi_shim_ready=0x00000001
stage56_xnu_object_consistent_debug_abi_shim_ready=0x00000001
stage56_xnu_object_device_tree_bytes=0x00000f28
stage56_xnu_object_bootargs_bytes=0x00000bf4
stage56_xnu_object_pe_gen_bytes=0x000008d0
stage56_xnu_object_arm_pe_bootargs_bytes=0x00000368
stage56_xnu_object_arm_pe_consistent_debug_bytes=0x0000073c
stage56_xnu_object_device_tree_sha32=0xc507eca7
stage56_xnu_object_bootargs_sha32=0x2f9e47cb
stage56_xnu_object_pe_gen_sha32=0x2998568c
stage56_xnu_object_arm_pe_bootargs_sha32=0xa819bb2b
stage56_xnu_object_arm_pe_consistent_debug_sha32=0xf4a47725
stage56_xnu_object_public_only=0x00000001
stage56_xnu_object_no_full_xnu_build=0x00000001
stage56_xnu_object_no_macho_link=0x00000001
stage56_xnu_object_no_public_xnu_exec=0x00000001
stage56_xnu_object_no_platform_runtime_exec=0x00000001
stage56_xnu_object_no_external_mutation=0x00000001
stage56_xnu_object_outputs_ignored=0x00000001
```

The count is explicit: three public 2050 objects, two later-public ARM pexpert objects, and one Stage-owned shim/support object.

## Controlled link proof

Stage56 still does not claim a native bootable XNU Mach-O link. The available toolchain is GNU ARM ELF-oriented, so Stage56 creates a closed host-only ARM ELF proof artifact and embeds only proof metadata into the inert generated Mach-O fixture.

Link inputs:

```text
out/stage56/xnu-objects/device_tree.o
out/stage56/xnu-objects/bootargs.o
out/stage56/xnu-objects/pe_gen.o
out/stage56/xnu-objects/arm_pe_bootargs.o
out/stage56/xnu-objects/arm_pe_consistent_debug.o
out/stage56/xnu-objects/xnu_object_shims.o
out/stage56/xnu-link/xnu_link_support.o
out/stage56/xnu-link/xnu_link_noentry.o
```

Successful link status from the current local build:

```text
stage56_xnu_link_status=0x56000001
stage56_xnu_link_required_mask=0x0001ffff
stage56_xnu_link_satisfied_mask=0x0001ffff
stage56_xnu_link_failure_mask=0x00000000
stage56_xnu_link_object_count=0x00000006
stage56_xnu_link_support_object_count=0x00000002
stage56_xnu_link_undefined_symbol_count=0x00000000
stage56_xnu_link_global_symbol_count=0x0000002c
stage56_xnu_link_elf_bytes=0x0000a25c
stage56_xnu_link_elf_sha32=0x4816f1c9
stage56_xnu_link_text_addr=0x80008000
stage56_xnu_link_text_size=0x000012c6
stage56_xnu_link_data_addr=0x800092d0
stage56_xnu_link_data_size=0x000004b8
stage56_xnu_link_bss_addr=0x80009790
stage56_xnu_link_bss_size=0x00005020
stage56_xnu_link_public_only=0x00000001
stage56_xnu_link_no_full_xnu_build=0x00000001
stage56_xnu_link_no_public_xnu_exec=0x00000001
stage56_xnu_link_no_macho_exec=0x00000001
stage56_xnu_link_no_platform_runtime_exec=0x00000001
stage56_xnu_link_no_external_mutation=0x00000001
stage56_xnu_link_outputs_ignored=0x00000001
stage56_xnu_link_fail_closed=0x00000001
```

Expected symbols include public `PE_boot_args`, public `PE_consistent_debug_inherit`, public `PE_consistent_debug_register`, public `PE_consistent_debug_enabled`, Stage-owned `PE_state`, Stage-owned `OSCompareAndSwap64`, Stage-owned `ml_map_high_window`, public `DTInit`, `DTLookupEntry`, `DTGetProperty`, `PE_parse_boot_argn`, `PE_get_default`, `pe_init_debug`, `PE_enter_debugger`, `PE_init_printf`, `PE_putc`, plus the inert `stage56_xnu_link_noentry` anchor. `arm-none-eabi-nm -u out/stage56/xnu-link/stage56-xnu-link.elf` reports no undefined symbols.

## Bootstrap mapping contract

`stage56/xnu_bootstrap_contract.c` records a first-class Stage56 contract over the loader-derived XNU bootstrap tuple. It imports source statuses for Mach-O parsing, staging/materialization, TTE dry-run, safe-table materialization, TTBR0 round-trip/restore, cache preservation, compile graph, object subset, and link proof. It records page-size/alignment facts, loaded image/file/zero-fill/workspace page counts, identity/high-VA L1 section coverage, no-overlap checks against Stage image, ram_console, device tree, staging arena, and safe-table arena, plus safety facts for no public-XNU/platform/Mach-O execution, no proposed physical/TTE writes, no persistent writes, no cache-policy change, and fail-closed graph/link behavior.

Expected contract markers:

```text
stage56_xnu_bootstrap_contract_status=0x56000001
stage56_xnu_bootstrap_contract_required_mask=0x7fffffff
stage56_xnu_bootstrap_contract_failure_mask=0x00000000
stage56_xnu_bootstrap_contract_page_size=0x00001000
stage56_xnu_bootstrap_contract_workspace_page_count=0x0000000a
stage56_xnu_bootstrap_contract_no_public_xnu_exec=0x00000001
stage56_xnu_bootstrap_contract_no_platform_runtime_exec=0x00000001
stage56_xnu_bootstrap_contract_no_macho_exec=0x00000001
stage56_xnu_bootstrap_contract_no_proposed_phys_write=0x00000001
stage56_xnu_bootstrap_contract_no_proposed_tte_write=0x00000001
stage56_xnu_bootstrap_contract_no_persist_write=0x00000001
stage56_xnu_bootstrap_contract_no_cache_policy_change=0x00000001
loader_xnu_bootstrap_contract_status_rollup=0x56000001
```

## Loader roll-up

The booted payload imports generated graph/object/link facts and rolls them into the existing loader-preflight status ABI. It still does not call public-XNU objects.

Expected local selftest roll-up fields include:

```text
loader_xnu_workspace_status=0x56000001
loader_xnu_compile_graph_status=0x56000001
loader_xnu_compile_graph_status_rollup=0x56000001
loader_xnu_object_subset_status=0x56000001
loader_xnu_object_subset_status_rollup=0x56000001
loader_xnu_link_status=0x56000001
loader_xnu_link_status_rollup=0x56000001
stage56_xnu_bootstrap_contract_status=0x56000001
loader_xnu_bootstrap_contract_status_rollup=0x56000001
loader_status=0x56000001
```

Inherited TTBR0/cache safety markers remain part of the target-side gate:

```text
stage56_ttbr_roundtrip_status=0x56000001
ttbr_rt_restored_ttbr0=0x0006c000
ttbr_rt_cache_bits_before=0x00000000
ttbr_rt_cache_bits_during=0x00000000
ttbr_rt_cache_bits_after=0x00000000
ttbr_rt_xnu_entry_executed=0x00000000
ttbr_rt_macho_bytes_executed=0x00000000
ttbr_rt_proposed_phys_load_written=0x00000000
ttbr_rt_proposed_tte_workspace_written=0x00000000
ttbr_rt_persistent_write_attempted=0x00000000
ttbr_rt_caches_changed=0x00000000
```

## Built image

```bash
/mnt/data/mi4-ios6/stage56/build.sh
```

Successful current local build:

```text
out/stage56/stage56-qcdt.img
sha256=79322ef80cdcfc79df5e2911b03d5a80b4b397d6af005fe22fa1bdaae4b78319
```

Build hashes:

```text
23f6ad9276e1f9a597f5c5aacd8a12dc5ef0a0b5b6a48d3df4edd6c8dd5a4683  out/stage56/stage56_fixture.macho
0175bf6119bea8505166e589636818f8e4ecbc86749cb0a96f1ddcdbc807e449  out/stage56/stage56.elf
be75a4dbca5fbac4103fad8722a5e84ceedce26b24699b41db83bc191818be3d  out/stage56/stage56.bin
2eb7bb4400997ef6fa51c6ce74dd73b779f99460d869a1ae75afa458a6d78993  out/stage56/stage56.img
79322ef80cdcfc79df5e2911b03d5a80b4b397d6af005fe22fa1bdaae4b78319  out/stage56/stage56-qcdt.img
```

Size summary:

```text
text=184280 data=0 bss=250992 dec=435272 hex=6a448
```

Boot image parse highlights:

```text
magic=ANDROID!
page_size=2048
kernel_size=184280 (0x2cfd8)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage56 mi4ios6=stage56 ... xnu-bootstrap-contract public-xnu-arm-pe-bootargs public-xnu-arm-consistent-debug stage56-xnu-link-proof ... no-public-xnu-exec no-platform-runtime-exec no-macho-exec no-proposed-phys-write no-proposed-tte-write no-cache-policy-change no-persist-write no-external-mutation
part=kernel offset=0x800 size=184280 sha256=be75a4dbca5fbac4103fad8722a5e84ceedce26b24699b41db83bc191818be3d
part=dt.img offset=0x2d800 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

## Validation status

Current local validation completed:

- `stage56/build.sh` succeeds.
- `tools/parse_android_bootimg.py out/stage56/stage56.img` succeeds.
- `tools/parse_android_bootimg.py out/stage56/stage56-qcdt.img` succeeds and confirms the cancro-required QCDT `dt_size=2521088`.
- `arm-none-eabi-nm -u out/stage56/xnu-link/stage56-xnu-link.elf` prints no undefined symbols.
- Static stale-reference scan finds no stale previous-stage identifiers under `stage56/`.
- `external/xnu-upstream` and `external/xnu-4570.1.46` remain clean.
- `out/stage56/` and `external/` remain ignored by git.

Hardware validation completed with non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage56/stage56-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage56-last_kmsg.txt
```

Recovered log facts:

```text
stage56_last_kmsg_bytes=0x0001fceb
stage56_marker_count=0x000007a8
stage56_xnu_marker_count=0x0000078e
stage56_hardware_validation=ok
```

Confirmed hardware markers after non-persistent boot:

```text
MI4IOS6_STAGE56_XNU stage56_xnu_compile_graph_status=0x56000001
MI4IOS6_STAGE56_XNU stage56_xnu_compile_graph_bootstrap_contract_selected=0x00000001
MI4IOS6_STAGE56_XNU stage56_xnu_object_subset_status=0x56000001
MI4IOS6_STAGE56_XNU stage56_xnu_link_status=0x56000001
MI4IOS6_STAGE56_XNU stage56_xnu_bootstrap_contract_status=0x56000001
MI4IOS6_STAGE56_XNU loader_xnu_bootstrap_contract_status_rollup=0x56000001
MI4IOS6_STAGE56_XNU loader_status=0x56000001
MI4IOS6_STAGE56_XNU kernel_entry ok
MI4IOS6_STAGE56 kernel_entry returned success
```

Expected negative safety markers remain:

```text
MI4IOS6_STAGE56_XNU ttbr_rt_xnu_entry_executed=0x00000000
MI4IOS6_STAGE56_XNU ttbr_rt_macho_bytes_executed=0x00000000
MI4IOS6_STAGE56_XNU ttbr_rt_proposed_phys_load_written=0x00000000
MI4IOS6_STAGE56_XNU ttbr_rt_proposed_tte_workspace_written=0x00000000
MI4IOS6_STAGE56_XNU ttbr_rt_persistent_write_attempted=0x00000000
MI4IOS6_STAGE56_XNU ttbr_rt_caches_changed=0x00000000
```
