# Stage55 — public-XNU pexpert consistent-debug compile/link proof

Stage55 keeps the no-XNU-runtime safety envelope and advances the public Apple OSS XNU migration from the previous stage public ARM `PE_boot_args()` proof to the next bounded public ARM pexpert surface for Xiaomi Mi 4 cancro / MSM8974 ARMv7: consistent-debug registry helpers.

The new Stage55 public object is:

```text
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
```

It is used as a later-public ARM implementation reference because the selected Darwin 12 / iOS 6-era public `xnu-2050.22.13` tree does not contain a complete ARMv7 pexpert implementation. Stage55 compiles and links this object only into ignored host proof artifacts. The booted Stage55 payload imports generated facts about that host proof; it does **not** execute public-XNU code, does **not** call the public consistent-debug helpers, and does **not** run pexpert/platform runtime code.

## Safety model

Stage55 still does **not** run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not execute public ARM pexpert/platform runtime code, does not write proposed XNU physical load or TTE workspace addresses, does not use the proposed XNU TTE workspace as a live TTBR table, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

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
- Public-XNU graph/object/link outputs stay ignored under `out/stage55/`.
- Public source checkouts stay ignored under `external/`.

The Stage55 completion message is intentionally explicit:

```text
Stage55 XNU execution disabled: pexpert/platform compile graph classified, bounded public pe_gen plus ARM pe_bootargs and ARM consistent_debug objects compiled, and controlled ARM ELF link proof recorded; inert MH_PRELOAD fixture carries metadata only; no full mach_kernel build, no public-XNU execution, no platform runtime execution, no Mach-O execution, no proposed physical/workspace writes, no persistent writes, caches unchanged
```

## What Stage55 adds over the previous stage

The previous stage introduced the first later-public ARM pexpert object proof by sourcing `PE_boot_args()` from public `pexpert/arm/pe_bootargs.c`. Stage55 keeps that discipline and adds the next small pexpert/platform surface:

- new standalone `stage55/` payload copied from previous stage,
- Stage55 status/log prefixes (`0x55000001`, `MI4IOS6_STAGE55`),
- command-line markers for:
  - `public-xnu-arm-consistent-debug`,
  - `stage55-xnu-link-proof`,
  - inherited `public-xnu-workspace`, `public-xnu-compile-graph`, `public-xnu-platform-graph`, `public-xnu-object-subset`, `public-xnu-controlled-link`, `public-xnu-bounded-pe-gen`, `public-xnu-arm-pe-bootargs`, `ttbr0-roundtrip`, `recovery-table`, `cache-bits-preserved`, `no-public-xnu-exec`, `no-platform-runtime-exec`, `no-macho-exec`, `no-proposed-phys-write`, `no-proposed-tte-write`, `no-cache-policy-change`, `no-persist-write`,
- expanded host-side compile graph scanner `stage55/xnu_compile_graph_scan.py`,
- expanded target-side graph ABI/logs in `stage55/xnu_compile_graph.h` / `.c`,
- new compile-only ABI shims:
  - `stage55/shims/pexpert/arm/consistent_debug.h`,
  - `stage55/shims/libkern/OSAtomic.h`,
  - `stage55/shims/machine/machine_routines.h`,
- Stage-owned host-proof support definitions for `OSCompareAndSwap64()` and `ml_map_high_window()` in `stage55/xnu_object_shims.c`,
- object-subset compiler expanded to five public objects plus one Stage-owned support object,
- controlled ARM ELF link proof expanded to include `arm_pe_consistent_debug.o`,
- loader safety/status roll-up retaining no-public-XNU-exec and no-platform-runtime-exec boundaries.

The important Stage55 boundary is that `pe_consistent_debug.c` contributes real public ARM pexpert symbols to a host-only proof, while the booted payload only consumes generated facts proving those objects compiled and linked safely.

## Selected public XNU baselines

Stage55 expects:

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

Stage55 already compiles/links the public 2050 device-tree parser that provides `DTLookupEntry` and `DTGetProperty`. It adds minimal Stage-owned compile/link shims for the remaining two dependencies:

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
    /* Stage55 host-proof support only; public consistent-debug code is never executed on hardware. */
    if (len == 0u || len > sizeof(g_stage55_xnu_consistent_debug_window)) {
        return 0u;
    }
    return (vm_map_address_t)(uintptr_t)g_stage55_xnu_consistent_debug_window;
}
```

These shims do not become a fake runtime debugger, high-window mapper, VM path, pmap path, platform runtime, or IOKit path. They only close a controlled host-proof link.

## Compile graph

`stage55/xnu_compile_graph_scan.py` runs after workspace validation and before object compilation. It writes generated, ignored artifacts under `out/stage55/`.

The scanner classifies fifteen candidates:

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
external/xnu-upstream/pexpert/i386/pe_serial.c                  wrong-arch reference only
external/xnu-upstream/pexpert/i386/pe_kprintf.c                 wrong-arch reference only
external/xnu-4570.1.46/osfmk/arm/start.s                        excluded high-risk
external/xnu-4570.1.46/osfmk/arm/arm_init.c                     excluded high-risk
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c                  excluded high-risk
external/xnu-4570.1.46/osfmk/arm/pmap.c                         excluded high-risk
```

Successful scanner status from the current local build:

```text
stage55_xnu_compile_graph_status=0x55000001
stage55_xnu_compile_graph_required_mask=0x07ffffff
stage55_xnu_compile_graph_satisfied_mask=0x07ffffff
stage55_xnu_compile_graph_failure_mask=0x00000000
stage55_xnu_compile_graph_candidate_count=0x0000000f
stage55_xnu_compile_graph_allowed_compile_count=0x00000005
stage55_xnu_compile_graph_allowed_link_count=0x00000005
stage55_xnu_compile_graph_forbidden_count=0x0000000a
stage55_xnu_compile_graph_shim_required_count=0x00000003
stage55_xnu_compile_graph_max_risk_class=0x00000005
stage55_xnu_compile_graph_pe_gen_allowed=0x00000001
stage55_xnu_compile_graph_arm_bootargs_allowed=0x00000001
stage55_xnu_compile_graph_arm_consistent_debug_allowed=0x00000001
stage55_xnu_compile_graph_consistent_debug_layout_recorded=0x00000001
stage55_xnu_compile_graph_platform_reference_count=0x00000004
stage55_xnu_compile_graph_blocked_runtime_count=0x00000002
stage55_xnu_compile_graph_duplicate_symbol_count=0x00000000
stage55_xnu_compile_graph_pe_state_abi_recorded=0x00000001
stage55_xnu_compile_graph_boot_args_arm_layout_recorded=0x00000001
stage55_xnu_compile_graph_4570_bounded_reference_policy=0x00000001
stage55_xnu_compile_graph_no_full_xnu_build=0x00000001
stage55_xnu_compile_graph_no_public_xnu_exec=0x00000001
stage55_xnu_compile_graph_no_macho_exec=0x00000001
stage55_xnu_compile_graph_no_platform_runtime_exec=0x00000001
stage55_xnu_compile_graph_no_external_mutation=0x00000001
stage55_xnu_compile_graph_outputs_ignored=0x00000001
stage55_xnu_compile_graph_fail_closed=0x00000001
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

Stage55-owned support object:

```text
stage55/xnu_object_shims.c
```

Successful compile status from the current local build:

```text
stage55_xnu_object_subset_status=0x55000001
stage55_xnu_object_subset_required_mask=0x07ffffff
stage55_xnu_object_subset_satisfied_mask=0x07ffffff
stage55_xnu_object_subset_failure_mask=0x00000000
stage55_xnu_object_source_mask=0x0000001f
stage55_xnu_object_shim_mask=0x000003ff
stage55_xnu_object_count=0x00000006
stage55_xnu_object_public_2050_count=0x00000003
stage55_xnu_object_public_arm_pexpert_count=0x00000002
stage55_xnu_object_stage_owned_shim_count=0x00000001
stage55_xnu_object_duplicate_symbol_count=0x00000000
stage55_xnu_object_pe_state_abi_shim_ready=0x00000001
stage55_xnu_object_consistent_debug_abi_shim_ready=0x00000001
stage55_xnu_object_device_tree_bytes=0x00000f28
stage55_xnu_object_bootargs_bytes=0x00000bf4
stage55_xnu_object_pe_gen_bytes=0x000008d0
stage55_xnu_object_arm_pe_bootargs_bytes=0x00000368
stage55_xnu_object_arm_pe_consistent_debug_bytes=0x0000073c
stage55_xnu_object_device_tree_sha32=0xc507eca7
stage55_xnu_object_bootargs_sha32=0x2f9e47cb
stage55_xnu_object_pe_gen_sha32=0x2998568c
stage55_xnu_object_arm_pe_bootargs_sha32=0xa819bb2b
stage55_xnu_object_arm_pe_consistent_debug_sha32=0xf4a47725
stage55_xnu_object_public_only=0x00000001
stage55_xnu_object_no_full_xnu_build=0x00000001
stage55_xnu_object_no_macho_link=0x00000001
stage55_xnu_object_no_public_xnu_exec=0x00000001
stage55_xnu_object_no_platform_runtime_exec=0x00000001
stage55_xnu_object_no_external_mutation=0x00000001
stage55_xnu_object_outputs_ignored=0x00000001
```

The count is explicit: three public 2050 objects, two later-public ARM pexpert objects, and one Stage-owned shim/support object.

## Controlled link proof

Stage55 still does not claim a native bootable XNU Mach-O link. The available toolchain is GNU ARM ELF-oriented, so Stage55 creates a closed host-only ARM ELF proof artifact and embeds only proof metadata into the inert generated Mach-O fixture.

Link inputs:

```text
out/stage55/xnu-objects/device_tree.o
out/stage55/xnu-objects/bootargs.o
out/stage55/xnu-objects/pe_gen.o
out/stage55/xnu-objects/arm_pe_bootargs.o
out/stage55/xnu-objects/arm_pe_consistent_debug.o
out/stage55/xnu-objects/xnu_object_shims.o
out/stage55/xnu-link/xnu_link_support.o
out/stage55/xnu-link/xnu_link_noentry.o
```

Successful link status from the current local build:

```text
stage55_xnu_link_status=0x55000001
stage55_xnu_link_required_mask=0x0001ffff
stage55_xnu_link_satisfied_mask=0x0001ffff
stage55_xnu_link_failure_mask=0x00000000
stage55_xnu_link_object_count=0x00000006
stage55_xnu_link_support_object_count=0x00000002
stage55_xnu_link_undefined_symbol_count=0x00000000
stage55_xnu_link_global_symbol_count=0x0000002c
stage55_xnu_link_elf_bytes=0x0000a25c
stage55_xnu_link_elf_sha32=0x10f82176
stage55_xnu_link_text_addr=0x80008000
stage55_xnu_link_text_size=0x000012c6
stage55_xnu_link_data_addr=0x800092d0
stage55_xnu_link_data_size=0x000004b8
stage55_xnu_link_bss_addr=0x80009790
stage55_xnu_link_bss_size=0x00005020
stage55_xnu_link_public_only=0x00000001
stage55_xnu_link_no_full_xnu_build=0x00000001
stage55_xnu_link_no_public_xnu_exec=0x00000001
stage55_xnu_link_no_macho_exec=0x00000001
stage55_xnu_link_no_platform_runtime_exec=0x00000001
stage55_xnu_link_no_external_mutation=0x00000001
stage55_xnu_link_outputs_ignored=0x00000001
stage55_xnu_link_fail_closed=0x00000001
```

Expected symbols include public `PE_boot_args`, public `PE_consistent_debug_inherit`, public `PE_consistent_debug_register`, public `PE_consistent_debug_enabled`, Stage-owned `PE_state`, Stage-owned `OSCompareAndSwap64`, Stage-owned `ml_map_high_window`, public `DTInit`, `DTLookupEntry`, `DTGetProperty`, `PE_parse_boot_argn`, `PE_get_default`, `pe_init_debug`, `PE_enter_debugger`, `PE_init_printf`, `PE_putc`, plus the inert `stage55_xnu_link_noentry` anchor. `arm-none-eabi-nm -u out/stage55/xnu-link/stage55-xnu-link.elf` reports no undefined symbols.

## Loader roll-up

The booted payload imports generated graph/object/link facts and rolls them into the existing loader-preflight status ABI. It still does not call public-XNU objects.

Expected local selftest roll-up fields include:

```text
loader_xnu_workspace_status=0x55000001
loader_xnu_compile_graph_status=0x55000001
loader_xnu_compile_graph_status_rollup=0x55000001
loader_xnu_object_subset_status=0x55000001
loader_xnu_object_subset_status_rollup=0x55000001
loader_xnu_link_status=0x55000001
loader_xnu_link_status_rollup=0x55000001
loader_status=0x55000001
```

Inherited TTBR0/cache safety markers remain part of the target-side gate:

```text
stage55_ttbr_roundtrip_status=0x55000001
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
/mnt/data/mi4-ios6/stage55/build.sh
```

Successful current local build:

```text
out/stage55/stage55-qcdt.img
sha256=ca5c19b7950003b940e3d22916db9d9470c41f80e87c6f444932b30e982fc8dc
```

Build hashes:

```text
fdc7468dd42014256ad70a3c4c2eb0f24b6daa34374de7077ceaef39bea0ec53  out/stage55/stage55_fixture.macho
b9a0e8a0517df4fd0bd7eccf3d0b43639ddf4ffb50bf337f11e82228e4892b34  out/stage55/stage55.elf
bb5319546b98958fe54cffbaf94450d95e8e5ed000673ae187cda7ca6757a838  out/stage55/stage55.bin
c47214e7c778d5dae40cd4a1768ac7abb67432b5f5595f28f18fe5d5b045ef37  out/stage55/stage55.img
ca5c19b7950003b940e3d22916db9d9470c41f80e87c6f444932b30e982fc8dc  out/stage55/stage55-qcdt.img
```

Size summary:

```text
text=176220 data=0 bss=250708 dec=426928 hex=683b0
```

Boot image parse highlights:

```text
magic=ANDROID!
page_size=2048
kernel_size=176220 (0x2b05c)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage55 mi4ios6=stage55 ... public-xnu-arm-pe-bootargs public-xnu-arm-consistent-debug stage55-xnu-link-proof ... no-public-xnu-exec no-platform-runtime-exec no-macho-exec no-proposed-phys-write no-proposed-tte-write no-cache-policy-change no-persist-write no-external-mutation
part=kernel offset=0x800 size=176220 sha256=bb5319546b98958fe54cffbaf94450d95e8e5ed000673ae187cda7ca6757a838
part=dt.img offset=0x2c000 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

## Validation status

Current local validation completed:

- `stage55/build.sh` succeeds.
- `tools/parse_android_bootimg.py out/stage55/stage55.img` succeeds.
- `tools/parse_android_bootimg.py out/stage55/stage55-qcdt.img` succeeds and confirms the cancro-required QCDT `dt_size=2521088`.
- `arm-none-eabi-nm -u out/stage55/xnu-link/stage55-xnu-link.elf` prints no undefined symbols.
- Static stale-reference scan finds no stale previous-stage identifiers under `stage55/`.
- `external/xnu-upstream` and `external/xnu-4570.1.46` remain clean.
- `out/stage55/` and `external/` remain ignored by git.

Hardware validation passed on cancro with non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage55/stage55-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage55-last_kmsg.txt
```

Fastboot accepted the image:

```text
Sending 'boot.img' (2638 KB)                       OKAY [  0.084s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.092s
```

The recovered log was 124047 bytes and contained 1884 `MI4IOS6_STAGE55` markers and 1858 `MI4IOS6_STAGE55_XNU` markers.

Key success markers:

```text
MI4IOS6_STAGE55_XNU stage55_xnu_compile_graph_status=0x55000001
MI4IOS6_STAGE55_XNU stage55_xnu_compile_graph_arm_consistent_debug_allowed=0x00000001
MI4IOS6_STAGE55_XNU stage55_xnu_compile_graph_consistent_debug_layout_recorded=0x00000001
MI4IOS6_STAGE55_XNU stage55_xnu_object_subset_status=0x55000001
MI4IOS6_STAGE55_XNU stage55_xnu_object_count=0x00000006
MI4IOS6_STAGE55_XNU stage55_xnu_object_public_arm_pexpert_count=0x00000002
MI4IOS6_STAGE55_XNU stage55_xnu_object_consistent_debug_abi_shim_ready=0x00000001
MI4IOS6_STAGE55_XNU stage55_xnu_link_status=0x55000001
MI4IOS6_STAGE55_XNU stage55_xnu_link_object_count=0x00000006
MI4IOS6_STAGE55_XNU stage55_xnu_link_undefined_symbol_count=0x00000000
MI4IOS6_STAGE55_XNU loader_xnu_compile_graph_status_rollup=0x55000001
MI4IOS6_STAGE55_XNU loader_xnu_object_subset_status_rollup=0x55000001
MI4IOS6_STAGE55_XNU loader_xnu_link_status_rollup=0x55000001
MI4IOS6_STAGE55_XNU loader_safety_mask=0x001fffff
MI4IOS6_STAGE55_XNU loader_satisfied_mask=0x01ffffff
MI4IOS6_STAGE55_XNU loader_status=0x55000001
MI4IOS6_STAGE55_XNU Stage55 Mach-O/XNU loader preflight ok
MI4IOS6_STAGE55_XNU kernel_entry ok
MI4IOS6_STAGE55 kernel_entry returned success
MI4IOS6_STAGE55 attempting MSM8974 PS_HOLD reset
```

Negative safety markers passed:

```text
MI4IOS6_STAGE55_XNU ttbr_rt_xnu_entry_executed=0x00000000
MI4IOS6_STAGE55_XNU ttbr_rt_macho_bytes_executed=0x00000000
MI4IOS6_STAGE55_XNU ttbr_rt_proposed_phys_load_written=0x00000000
MI4IOS6_STAGE55_XNU ttbr_rt_proposed_tte_workspace_written=0x00000000
MI4IOS6_STAGE55_XNU ttbr_rt_persistent_write_attempted=0x00000000
MI4IOS6_STAGE55_XNU ttbr_rt_caches_changed=0x00000000
```

No data abort, prefetch abort, undefined-instruction abort, watchdog-style hang marker, public-XNU execution marker, platform runtime execution marker, generated Mach-O execution marker, proposed physical/TTE workspace write marker, persistent-write marker, or cache-change marker was observed.
