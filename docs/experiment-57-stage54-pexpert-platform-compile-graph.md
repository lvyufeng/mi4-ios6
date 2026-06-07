# Experiment 57 — Stage54 Public-XNU pexpert/platform Compile Graph Proof

Date: 2026-06-07

Goal: advance from Stage53's generic public-XNU compile graph to a bounded public ARM pexpert/platform compile/link proof for Xiaomi Mi 4 cancro / MSM8974 ARMv7. Stage54 adds `external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c` as the first later-public ARM pexpert object while preserving the strict no-XNU-runtime boundary.

Stage54 still does **not** run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not execute the public ARM pexpert runtime, does not write proposed XNU physical load or TTE workspace addresses, does not use the proposed XNU TTE workspace as a live TTBR table, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

## Safety model

- Non-persistent `fastboot boot` only.
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
- Original TTBR0/TTBCR/DACR/SCTLR state remains restored after the inherited selftest.
- Caches remain disabled/unchanged.
- `ram_console`, PS_HOLD, GIC, timer, and abort handlers remain available.
- Public-XNU graph/object/link outputs stay ignored under `out/stage54/`.
- Public source checkouts stay ignored under `external/`.

The Stage54 completion message is intentionally explicit:

```text
Stage54 XNU execution disabled: pexpert/platform compile graph classified, bounded public pe_gen plus ARM pe_bootargs objects compiled, and controlled ARM ELF link proof recorded; inert MH_PRELOAD fixture carries metadata only; no full mach_kernel build, no public-XNU execution, no platform runtime execution, no Mach-O execution, no proposed physical/workspace writes, no persistent writes, caches unchanged
```

## What Stage54 adds over Stage53

Stage53 introduced a compile-migration graph and graph-gated the bounded public 2050 `pexpert/gen/pe_gen.c` source. Stage54 keeps that discipline and adds a later-public ARM pexpert object with explicit ABI and duplicate-symbol controls:

- new standalone `stage54/` payload copied from Stage53,
- Stage54 status/log prefixes (`0x54000001`, `MI4IOS6_STAGE54`),
- command-line markers for:
  - `public-xnu-platform-graph`,
  - `public-xnu-arm-pe-bootargs`,
  - `no-platform-runtime-exec`,
  - inherited `public-xnu-workspace`, `public-xnu-compile-graph`, `public-xnu-object-subset`, `public-xnu-controlled-link`, `public-xnu-bounded-pe-gen`, `ttbr0-roundtrip`, `recovery-table`, `cache-bits-preserved`, `no-public-xnu-exec`, `no-macho-exec`, `no-proposed-phys-write`, `no-proposed-tte-write`, `no-cache-policy-change`, `no-persist-write`,
- expanded host-side compile graph scanner `stage54/xnu_compile_graph_scan.py`,
- expanded target-side graph ABI/logs in `stage54/xnu_compile_graph.h` / `.c`,
- compile-only public ARM ABI shims in `stage54/shims/pexpert/boot.h` and `stage54/shims/pexpert/pexpert.h`,
- Stage-owned `PE_state` ABI backing in `stage54/xnu_object_shims.c`,
- removal of the Stage53 `PE_boot_args()` shim so the symbol now comes from public `arm_pe_bootargs.o`,
- duplicate-symbol counting fixed to inspect actual `nm` output from all object files,
- object-subset compiler expanded to four public objects plus one Stage-owned support object,
- controlled ARM ELF link proof expanded to include `arm_pe_bootargs.o`,
- loader safety bit `STAGE54_LOADER_SAFETY_NO_PLATFORM_RUNTIME_EXEC` and status/log roll-up.

The important Stage54 boundary is that `pe_bootargs.c` contributes a real public ARM pexpert symbol to the host-only proof, while the booted payload only consumes generated facts proving it compiled/linked safely.

## Selected public XNU baselines

Stage54 continues to validate the selected public iOS 6 / Darwin 12-era baseline read-only:

```text
path: external/xnu-upstream
selected ref: xnu-2050.22.13
commit: cc8a9b0ce917bb7115f5c97a78b38db871557db0
config/MasterVersion: 12.3.0
compact commit32: 0xcc8a9b0c
compact master version: 0x000c0300
```

The ignored checkout was not mutated. `external/` remains ignored by git.

`external/xnu-4570.1.46` remains a later public ARM implementation reference. Stage54 compiles only the bounded `pexpert/arm/pe_bootargs.c` source from it. Other public ARM pexpert/startup/VM sources are classified as ABI/reference-only, blocked-runtime references, or excluded high-risk.

## Compile graph scanner

`stage54/xnu_compile_graph_scan.py` runs after workspace validation and before object compilation. It writes generated, ignored artifacts under `out/stage54/`:

```text
out/stage54/xnu-compile-graph-status.txt
out/stage54/xnu-compile-graph-manifest.tsv
out/stage54/xnu-compile-graph-includes.txt
out/stage54/xnu-compile-graph-symbols.txt
out/stage54/xnu-compile-graph-risk.txt
out/stage54/xnu-compile-graph-manifest.json
out/stage54/xnu_compile_graph_generated.h
```

The scanner classifies fourteen candidates:

```text
external/xnu-upstream/pexpert/gen/device_tree.c                 compile/link allowed
external/xnu-upstream/pexpert/gen/bootargs.c                    compile/link allowed
external/xnu-upstream/pexpert/gen/pe_gen.c                      compile/link allowed
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c                compile/link allowed
external/xnu-4570.1.46/pexpert/pexpert/arm/boot.h               ABI reference only
external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c        blocked-runtime reference
external/xnu-4570.1.46/pexpert/arm/pe_init.c                    blocked-runtime reference
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c        blocked-runtime reference
external/xnu-upstream/pexpert/i386/pe_serial.c                  wrong-arch reference only
external/xnu-upstream/pexpert/i386/pe_kprintf.c                 wrong-arch reference only
external/xnu-4570.1.46/osfmk/arm/start.s                        excluded high-risk
external/xnu-4570.1.46/osfmk/arm/arm_init.c                     excluded high-risk
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c                  excluded high-risk
external/xnu-4570.1.46/osfmk/arm/pmap.c                         excluded high-risk
```

Successful scanner output:

```text
stage54_xnu_compile_graph_status=0x54000001
stage54_xnu_compile_graph_required_mask=0x00ffffff
stage54_xnu_compile_graph_satisfied_mask=0x00ffffff
stage54_xnu_compile_graph_failure_mask=0x00000000
stage54_xnu_compile_graph_candidate_count=0x0000000e
stage54_xnu_compile_graph_allowed_compile_count=0x00000004
stage54_xnu_compile_graph_allowed_link_count=0x00000004
stage54_xnu_compile_graph_forbidden_count=0x0000000a
stage54_xnu_compile_graph_shim_required_count=0x00000002
stage54_xnu_compile_graph_max_risk_class=0x00000005
stage54_xnu_compile_graph_pe_gen_allowed=0x00000001
stage54_xnu_compile_graph_arm_bootargs_allowed=0x00000001
stage54_xnu_compile_graph_platform_reference_count=0x00000004
stage54_xnu_compile_graph_blocked_runtime_count=0x00000003
stage54_xnu_compile_graph_duplicate_symbol_count=0x00000000
stage54_xnu_compile_graph_pe_state_abi_recorded=0x00000001
stage54_xnu_compile_graph_boot_args_arm_layout_recorded=0x00000001
stage54_xnu_compile_graph_4570_bounded_reference_policy=0x00000001
stage54_xnu_compile_graph_no_platform_runtime_exec=0x00000001
```

## Public ARM `pe_bootargs.c` dependency surface

The new bounded public source is:

```text
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
```

The relevant public implementation is intentionally tiny:

```c
char *
PE_boot_args(void)
{
    return (char *)((boot_args *)PE_state.bootArgs)->CommandLine;
}
```

Stage54 models only the ABI needed for that function:

- public ARM `boot_args` shape from `pexpert/pexpert/arm/boot.h`,
- minimal `PE_state_t` shape from public pexpert headers,
- Stage-owned `PE_state` backing object whose `bootArgs` points at a compile/link-only static ARM `boot_args` instance.

Stage54 does **not** compile or execute public `pe_init.c`. It does not run `PE_init_platform()`, `pe_identify_machine()`, interrupt/debug init, IOKit startup, pmap, scheduler, VM, or full pexpert runtime.

## Duplicate-symbol closure

Stage52/Stage53 had a Stage-owned `PE_boot_args()` support implementation so `bootargs.c` could compile and link. Stage54 deliberately removes that shim definition because public `pe_bootargs.c` now owns `PE_boot_args()`.

The object-subset compiler now writes combined defined-symbol output before counting duplicates:

```text
out/stage54/xnu-objects/xnu_object_subset.defined
```

The observed object proof records:

```text
stage54_xnu_compile_graph_duplicate_symbol_count=0x00000000
stage54_xnu_object_duplicate_symbol_count=0x00000000
```

Manual symbol check confirms there is a single `PE_boot_args` provider and it is the public `arm_pe_bootargs.o` path in the object subset.

## Public-XNU object subset

Compiled public sources:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
```

Stage54-owned object-subset support:

```text
stage54/xnu_object_shims.c
stage54/shims/pexpert/boot.h
stage54/shims/pexpert/pexpert.h
stage54/shims/kern/debug.h
```

Successful object-subset status:

```text
stage54_xnu_object_subset_status=0x54000001
stage54_xnu_object_subset_required_mask=0x007fffff
stage54_xnu_object_subset_satisfied_mask=0x007fffff
stage54_xnu_object_subset_failure_mask=0x00000000
stage54_xnu_object_source_mask=0x0000000f
stage54_xnu_object_shim_mask=0x0000007f
stage54_xnu_object_count=0x00000005
stage54_xnu_object_public_2050_count=0x00000003
stage54_xnu_object_public_arm_pexpert_count=0x00000001
stage54_xnu_object_stage_owned_shim_count=0x00000001
stage54_xnu_object_duplicate_symbol_count=0x00000000
stage54_xnu_object_pe_state_abi_shim_ready=0x00000001
stage54_xnu_object_device_tree_bytes=0x00000f28
stage54_xnu_object_bootargs_bytes=0x00000bf4
stage54_xnu_object_pe_gen_bytes=0x000008d0
stage54_xnu_object_arm_pe_bootargs_bytes=0x00000368
stage54_xnu_object_device_tree_sha32=0xc507eca7
stage54_xnu_object_bootargs_sha32=0x2f9e47cb
stage54_xnu_object_pe_gen_sha32=0x2998568c
stage54_xnu_object_arm_pe_bootargs_sha32=0xa819bb2b
stage54_xnu_object_no_platform_runtime_exec=0x00000001
```

The count is explicit: three public 2050 objects, one later-public ARM pexpert object, and one Stage-owned shim object.

## Controlled ARM ELF link proof

The available toolchain is GNU ARM ELF-oriented (`arm-none-eabi-*`), not Apple `ld64`/Mach-O-oriented. Stage54 therefore does not claim a native bootable XNU Mach-O link. Instead, it creates a closed host-only ARM ELF proof artifact and carries only metadata about that proof in the inert Mach-O fixture.

Link inputs:

```text
out/stage54/xnu-objects/device_tree.o
out/stage54/xnu-objects/bootargs.o
out/stage54/xnu-objects/pe_gen.o
out/stage54/xnu-objects/arm_pe_bootargs.o
out/stage54/xnu-objects/xnu_object_shims.o
out/stage54/xnu-link/xnu_link_support.o
out/stage54/xnu-link/xnu_link_noentry.o
```

Successful link status:

```text
stage54_xnu_link_status=0x54000001
stage54_xnu_link_required_mask=0x0001ffff
stage54_xnu_link_satisfied_mask=0x0001ffff
stage54_xnu_link_failure_mask=0x00000000
stage54_xnu_link_object_count=0x00000005
stage54_xnu_link_support_object_count=0x00000002
stage54_xnu_link_undefined_symbol_count=0x00000000
stage54_xnu_link_global_symbol_count=0x00000027
stage54_xnu_link_elf_bytes=0x00009e9c
stage54_xnu_link_elf_sha32=0x6a199bde
stage54_xnu_link_text_addr=0x80008000
stage54_xnu_link_text_size=0x000010aa
stage54_xnu_link_data_addr=0x800090b0
stage54_xnu_link_data_size=0x000004b8
stage54_xnu_link_bss_addr=0x80009570
stage54_xnu_link_bss_size=0x00001018
stage54_xnu_link_no_platform_runtime_exec=0x00000001
```

Expected public/subset symbols are defined and undefined symbols are closed:

```text
DTInit
DTLookupEntry
DTGetProperty
PE_parse_boot_argn
PE_get_default
PE_boot_args
PE_state
pe_init_debug
PE_enter_debugger
PE_init_printf
PE_putc
gPESerialBaud
appleClut8
Debugger
cnputc
vcattach
kalloc
kfree
IODTGetDefault
strncmp
stage54_xnu_link_noentry
undefined symbol count: 0
```

The `stage54_xnu_link_noentry` symbol is an inert Stage54-owned anchor. It is not a path to XNU execution.

## Inert Mach-O wrapper metadata

Stage54 embeds compact metadata from `out/stage54/xnu-link-macho-metadata.txt` into the deterministic `MH_PRELOAD` fixture's `__PRELINK_INFO,__info` payload. The metadata records the platform graph and controlled link proof, including:

```text
stage54-xnu-platform-graph
stage54-xnu-link-proof
bounded-pe-gen=true
bounded-arm-pe-bootargs=true
no-platform-runtime-exec=true
objects=5
undefined=0
no-full-mach-kernel=true
no-public-xnu-exec=true
no-macho-exec=true
```

The fixture remains non-proprietary and inert:

```text
out/stage54/stage54_fixture.macho
sha256=f1bc5bfa37136127e168495e9672b49cc1fa151dfaeaff4027c151586d577431
```

The bootable payload parses and materializes this fixture only into Stage-owned local BSS, verifies Stage54 fixture markers, and never executes the fixture.

## Target-side graph/object/link ABIs

The target payload imports generated host facts through:

```text
xnu_compile_graph_generated.h
xnu_object_subset_generated.h
xnu_link_generated.h
```

Hardware-proven roll-up:

```text
MI4IOS6_STAGE54_XNU stage54_xnu_compile_graph_status=0x54000001
MI4IOS6_STAGE54_XNU stage54_xnu_compile_graph_failure_mask=0x00000000
MI4IOS6_STAGE54_XNU stage54_xnu_compile_graph_arm_bootargs_allowed=0x00000001
MI4IOS6_STAGE54_XNU stage54_xnu_compile_graph_duplicate_symbol_count=0x00000000
MI4IOS6_STAGE54_XNU stage54_xnu_compile_graph_no_platform_runtime_exec=0x00000001
MI4IOS6_STAGE54_XNU stage54_xnu_object_subset_status=0x54000001
MI4IOS6_STAGE54_XNU stage54_xnu_object_count=0x00000005
MI4IOS6_STAGE54_XNU stage54_xnu_object_public_arm_pexpert_count=0x00000001
MI4IOS6_STAGE54_XNU stage54_xnu_object_duplicate_symbol_count=0x00000000
MI4IOS6_STAGE54_XNU stage54_xnu_link_status=0x54000001
MI4IOS6_STAGE54_XNU stage54_xnu_link_object_count=0x00000005
MI4IOS6_STAGE54_XNU stage54_xnu_link_undefined_symbol_count=0x00000000
MI4IOS6_STAGE54_XNU stage54_xnu_link_no_platform_runtime_exec=0x00000001
```

## Inherited workspace and TTBR/control safety proof

Stage54 preserves the public-XNU workspace validation:

```text
MI4IOS6_STAGE54_XNU stage54_xnu_workspace_status=0x54000001
MI4IOS6_STAGE54_XNU stage54_xnu_workspace_failure_mask=0x00000000
```

Stage54 also preserves the controlled no-XNU TTBR0 round-trip behavior:

```text
MI4IOS6_STAGE54_XNU stage54_ttbr_roundtrip_status=0x54000001
MI4IOS6_STAGE54_XNU stage54_ttbr_roundtrip_failure_mask=0x00000000
MI4IOS6_STAGE54_XNU ttbr_rt_restored_ttbr0=0x0006c000
MI4IOS6_STAGE54_XNU ttbr_rt_restored_ttbcr=0x00000000
MI4IOS6_STAGE54_XNU ttbr_rt_restored_dacr=0x00000003
MI4IOS6_STAGE54_XNU ttbr_rt_restored_sctlr=0x00c5487b
MI4IOS6_STAGE54_XNU ttbr_rt_cache_bits_before=0x00000000
MI4IOS6_STAGE54_XNU ttbr_rt_cache_bits_during=0x00000000
MI4IOS6_STAGE54_XNU ttbr_rt_cache_bits_after=0x00000000
MI4IOS6_STAGE54_XNU ttbr_rt_caches_changed=0x00000000
MI4IOS6_STAGE54_XNU ttbr_rt_xnu_entry_executed=0x00000000
MI4IOS6_STAGE54_XNU ttbr_rt_macho_bytes_executed=0x00000000
MI4IOS6_STAGE54_XNU ttbr_rt_proposed_phys_load_written=0x00000000
MI4IOS6_STAGE54_XNU ttbr_rt_proposed_tte_workspace_written=0x00000000
MI4IOS6_STAGE54_XNU ttbr_rt_persistent_write_attempted=0x00000000
```

The Stage-owned recovery L1 table remains separate from proposed XNU tables, original live MMU state is restored, and caches remain unchanged.

## Loader roll-up

Stage54 adds the explicit no-platform-runtime-execution safety bit. Hardware-proven loader values:

```text
MI4IOS6_STAGE54_XNU loader_xnu_workspace_status=0x54000001
MI4IOS6_STAGE54_XNU loader_xnu_compile_graph_status=0x54000001
MI4IOS6_STAGE54_XNU loader_xnu_compile_graph_status_rollup=0x54000001
MI4IOS6_STAGE54_XNU loader_xnu_object_subset_status=0x54000001
MI4IOS6_STAGE54_XNU loader_xnu_object_subset_status_rollup=0x54000001
MI4IOS6_STAGE54_XNU loader_xnu_link_status=0x54000001
MI4IOS6_STAGE54_XNU loader_xnu_link_status_rollup=0x54000001
MI4IOS6_STAGE54_XNU loader_safety_mask=0x001fffff
MI4IOS6_STAGE54_XNU loader_satisfied_mask=0x01ffffff
MI4IOS6_STAGE54_XNU loader_status=0x54000001
```

The inherited Mach-O materialization checks also pass with Stage54 markers:

```text
ST54-TEXT-NOEXEC
ST54-DATA-CONST
ST54-PRELINK-TEXT-NOEXEC
```

## Built image

```bash
/mnt/data/mi4-ios6/stage54/build.sh
```

Successful local build:

```text
out/stage54/stage54-qcdt.img
sha256=ff71c0cf7b8a8c63c544524f0d30c26ab64f6e878ae5e613028e379e8e579ffe
```

Build hashes:

```text
f1bc5bfa37136127e168495e9672b49cc1fa151dfaeaff4027c151586d577431  out/stage54/stage54_fixture.macho
2cdac7269f0dcabfce6cad301caf65a0a86bbd55b2b32d001543ef625fbdf9fe  out/stage54/stage54.elf
dfe43683ed1408b8006c61c69a78bbaac93d017c44e6935e8d7ff55c5934c2e6  out/stage54/stage54.bin
482e2f67932554a1d875688c0944097f52c25530ac9409d8111f61c183b1ca36  out/stage54/stage54.img
ff71c0cf7b8a8c63c544524f0d30c26ab64f6e878ae5e613028e379e8e579ffe  out/stage54/stage54-qcdt.img
```

Size:

```text
text=174088 data=0 bss=250684 dec=424772 hex=67b44
```

Boot image parse highlights:

```text
magic=ANDROID!
page_size=2048
kernel_size=174088 (0x2a808)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage54 mi4ios6=stage54 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved public-xnu-workspace public-xnu-compile-graph public-xnu-platform-graph public-xnu-object-subset public-xnu-controlled-link public-xnu-bounded-pe-gen public-xnu-arm-pe-bootargs stage54-xnu-link-proof inert-macho-link-fixture cancro-target-scaffold no-full-xnu-build no-xnu-jump no-public-xnu-exec no-platform-runtime-exec no-macho-exec no-proposed-phys-write no-proposed-tte-write no-cache-policy-change no-persist-write no-external-mutation
part=kernel offset=0x800 size=174088 sha256=dfe43683ed1408b8006c61c69a78bbaac93d017c44e6935e8d7ff55c5934c2e6
part=dt.img offset=0x2b800 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Generated output and external checkout policy was verified:

```text
out/stage54/xnu-link/stage54-xnu-link.elf is ignored
out/stage54/stage54-qcdt.img is ignored
external/xnu-upstream status --short: clean
external/xnu-4570.1.46 status --short: clean
```

Static safety checks also passed:

```text
stale Stage53 refs in stage54/: none
stale ST53 fixture markers in stage54/: none
undefined link symbols: none
forbidden full-kernel symbols (_start/arm_init/arm_vm_init/pmap_bootstrap/kernel_bootstrap): none
```

## Hardware run result

Hardware run succeeded using the required non-persistent path.

Command:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage54/stage54-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2636 KB)                       OKAY [  0.084s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.094s
```

Recovered persistent log:

```bash
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage54-last_kmsg.txt
```

The recovered log was 123494 bytes and contained:

```text
1878 MI4IOS6_STAGE54 markers
1852 MI4IOS6_STAGE54_XNU markers
```

## Key recovered markers

Compile graph gates pass:

```text
MI4IOS6_STAGE54_XNU stage54_xnu_compile_graph_status=0x54000001
MI4IOS6_STAGE54_XNU stage54_xnu_compile_graph_failure_mask=0x00000000
MI4IOS6_STAGE54_XNU stage54_xnu_compile_graph_arm_bootargs_allowed=0x00000001
MI4IOS6_STAGE54_XNU stage54_xnu_compile_graph_duplicate_symbol_count=0x00000000
MI4IOS6_STAGE54_XNU stage54_xnu_compile_graph_no_platform_runtime_exec=0x00000001
MI4IOS6_STAGE54_XNU loader_xnu_compile_graph_status=0x54000001
MI4IOS6_STAGE54_XNU loader_xnu_compile_graph_status_rollup=0x54000001
```

Object/link proof gates pass:

```text
MI4IOS6_STAGE54_XNU stage54_xnu_object_subset_status=0x54000001
MI4IOS6_STAGE54_XNU stage54_xnu_object_count=0x00000005
MI4IOS6_STAGE54_XNU stage54_xnu_object_public_arm_pexpert_count=0x00000001
MI4IOS6_STAGE54_XNU stage54_xnu_object_duplicate_symbol_count=0x00000000
MI4IOS6_STAGE54_XNU stage54_xnu_object_no_platform_runtime_exec=0x00000001
MI4IOS6_STAGE54_XNU stage54_xnu_link_status=0x54000001
MI4IOS6_STAGE54_XNU stage54_xnu_link_failure_mask=0x00000000
MI4IOS6_STAGE54_XNU stage54_xnu_link_object_count=0x00000005
MI4IOS6_STAGE54_XNU stage54_xnu_link_undefined_symbol_count=0x00000000
MI4IOS6_STAGE54_XNU stage54_xnu_link_no_platform_runtime_exec=0x00000001
```

Loader/final success markers:

```text
MI4IOS6_STAGE54_XNU loader_safety_mask=0x001fffff
MI4IOS6_STAGE54_XNU loader_satisfied_mask=0x01ffffff
MI4IOS6_STAGE54_XNU loader_status=0x54000001
MI4IOS6_STAGE54_XNU Stage54 Mach-O/XNU loader preflight ok
MI4IOS6_STAGE54_XNU kernel_entry ok
MI4IOS6_STAGE54 kernel_entry returned success
MI4IOS6_STAGE54 attempting MSM8974 PS_HOLD reset
```

Negative checks passed: no data abort, no prefetch abort, no undefined-instruction abort, no watchdog-style hang marker, no public-XNU function execution, no platform runtime execution, no generated Mach-O execution, no full `mach_kernel` execution marker, no persistent-write marker, no proposed physical load write, no proposed TTE workspace write, and no cache-bit changes. The recovered log lines containing `no public-XNU execution`, `no platform runtime execution`, and `no proposed physical/workspace writes` are explicit negative safety messages.

## Interpretation

Stage54 is still not a real XNU boot, but it moves the migration boundary closer to the public ARM pexpert path. The project now has:

- a graph-gated public ARM `PE_boot_args()` object from later public XNU,
- Stage-owned PE_state/boot_args ABI backing without duplicating the public symbol,
- a duplicate-symbol closure proof,
- a controlled host-only ARM ELF link proof with zero undefined symbols,
- target-side hardware logs proving those host-generated facts were imported and checked without executing public-XNU/platform runtime code.

The important boundary is that Stage54 migrates compile/link facts, not runtime control. The bootable Stage54 payload imports generated graph/object/link facts and embeds inert metadata in the generated Mach-O fixture; it does not call public-XNU functions or branch to any XNU entry. The inherited no-XNU TTBR0 round-trip, Mach-O parser/materialization, local TTE/high-VA/safe-table preflight, ram_console logging, and PS_HOLD reset path all remain intact.

## Success criteria — met

1. Stage54 directory and build outputs created: yes
2. Stage54 command line includes pexpert/platform graph, ARM `pe_bootargs`, and no-platform-runtime-exec markers: yes
3. Stale Stage53 runtime refs/markers removed from Stage54: yes (`ST54-*` marker checks pass)
4. Public-XNU workspace validator preserved: yes
5. Compile graph scanner expanded and fail-closed: yes
6. Graph candidate manifest records compile/link allowed, ABI/reference-only, blocked-runtime, wrong-arch, and excluded-high-risk candidates: yes
7. `start.s`, `arm_init.c`, `arm_vm_init.c`, and `pmap.c` remain excluded/reference-only: yes
8. Public `device_tree.c` compiled as ARMv7 object: yes
9. Public `bootargs.c` compiled as ARMv7 object: yes
10. Public `pe_gen.c` compiled as ARMv7 object: yes
11. Public ARM `pe_bootargs.c` compiled as ARMv7 object only after graph approval: yes
12. Stage54-owned shim support object compiled: yes
13. Stage54 shim no longer defines `PE_boot_args()`: yes
14. `PE_boot_args()` comes from public `arm_pe_bootargs.o`: yes
15. Stage-owned `PE_state` ABI backing object added: yes
16. Duplicate symbol count is zero: yes
17. Controlled ARM ELF link proof updated to five objects: yes
18. Link proof undefined symbol count is zero: yes
19. Link proof status returned `0x54000001`: yes
20. Link proof satisfied mask reached `0x0001ffff`: yes
21. Link proof failure mask stayed zero: yes
22. Compile graph status returned `0x54000001`: yes
23. Compile graph satisfied mask reached `0x00ffffff`: yes
24. Compile graph failure mask stayed zero: yes
25. Link metadata embedded into inert Mach-O fixture: yes
26. No full public `mach_kernel` build attempted: yes
27. No public-XNU object execution occurred: yes
28. No public platform runtime execution occurred: yes
29. No Mach-O fixture execution occurred: yes
30. External checkouts remain ignored and clean: yes
31. Target-side graph/object/link ABIs updated: yes
32. Loader graph/object/link rollups returned `0x54000001`: yes
33. Loader safety mask reached `0x001fffff`: yes
34. Loader satisfied mask reached `0x01ffffff`: yes
35. Loader preflight returned status `0x54000001`: yes
36. Inherited TTBR round-trip still returned `0x54000001`: yes
37. Cache bits before/during/after stayed `0x00000000`: yes
38. Proposed physical load writes remained zero: yes
39. Proposed physical workspace writes remained zero: yes
40. Persistent write attempt remained zero: yes
41. Bootloader accepted `stage54-qcdt.img`: yes
42. `kernel_entry` returned success: yes
43. Payload reset through PS_HOLD: yes

## Next stage

Stage54 completes the first public ARM pexpert/platform object migration proof. Stage55 should stay focused on the path toward real XNU execution while preserving the no-execution safety boundary:

- expand the graph from `PE_boot_args()` toward the next smallest public pexpert/platform surface, likely boot-argument/default parsing or pexpert default/property access with explicit ABI shims,
- keep public `xnu-2050.22.13` as the Darwin 12/iOS 6-era baseline,
- use later public ARM sources only as references for missing ARMv7 boot implementation details,
- start defining cancro/MSM8974 pexpert shim packages for timebase, interrupt, debug, and platform identification hooks,
- continue refusing `start.s`, `arm_init.c`, `arm_vm_init.c`, `pmap.c`, scheduler, VM, and IOKit sources until their preconditions are explicitly modeled,
- keep build/link outputs ignored under `out/stage55/`,
- do not jump into XNU or execute public-XNU code on hardware until boot args, page tables, pmap, interrupts, timer, and cache policy are proven,
- continue non-persistent `sudo fastboot boot` validation only.
