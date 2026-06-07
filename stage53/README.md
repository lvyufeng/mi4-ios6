# Stage53 — public-XNU compile graph migration proof

Stage53 keeps the previous-stage no-XNU runtime safety envelope and advances the public Apple OSS XNU work from a controlled object-subset link proof to a formal compile-migration graph for the Xiaomi Mi 4 cancro / MSM8974 ARMv7 target.

It classifies a bounded set of public XNU source candidates, gates the first expanded public source (`pexpert/gen/pe_gen.c`) through that graph, compiles the graph-approved public sources as ARMv7 objects, links those objects into a host-only ARM ELF proof artifact with Stage53-owned support objects, records deterministic graph/object/link/layout/hash facts, embeds only metadata about those facts into the inert generated Mach-O fixture, and reports the result through target-side logs.

Stage53 still does **not** run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute the generated Mach-O fixture, and does not jump to XNU `_start` / `arm_init`.

## Tracked scaffold

- `targets/cancro.mk` records the declarative cancro/MSM8974 ARMv7 target facts, object-subset policy, compile-graph policy, and controlled-link/no-execution policy.
- `targets/cancro.stage53.objects` records the public-XNU inputs and boundary.
- `xnu_workspace_validate.sh` validates ignored public checkouts read-only and emits generated workspace status under `out/stage53/`.
- `xnu_compile_graph_scan.py` classifies compile-proven, bounded-new, reference-only, and excluded-high-risk public XNU candidates and emits generated graph facts under `out/stage53/`.
- `xnu_object_subset_compile.sh` compiles the graph-approved public-XNU object subset into ignored `out/stage53/xnu-objects/`.
- `xnu_link_proof.sh` links the object subset plus Stage53-owned support into ignored `out/stage53/xnu-link/stage53-xnu-link.elf` and emits `out/stage53/xnu_link_generated.h`.
- `xnu_link_support.c` provides host-link-only freestanding libc-like support symbols.
- `xnu_link_noentry.c` provides an inert no-entry link anchor.
- `xnu_link.ld` provides deterministic ARM ELF layout for the proof artifact.
- `shims/` contains Stage53-owned compatibility headers needed to compile the selected public sources without mutating `external/`, including `shims/kern/debug.h` for bounded `pe_gen.c` compile support.
- `xnu_object_shims.c` provides object-subset shim support symbols such as `PE_boot_args`, `kalloc`, `kfree`, `IODTGetDefault`, and no-op compile/link-only `Debugger`, `cnputc`, and `vcattach` support.
- `xnu_workspace.*`, `xnu_compile_graph.*`, `xnu_object_subset.*`, and `xnu_link.*` expose target-side status ABIs used by the Stage53 loader preflight.

## Selected public baseline

Stage53 expects the ignored checkout `external/xnu-upstream` to be detached at public Apple OSS tag `xnu-2050.22.13`, commit `cc8a9b0ce917bb7115f5c97a78b38db871557db0`, with `config/MasterVersion` equal to `12.3.0`.

The public 2050 tree is intentionally treated as incomplete for a full iOS ARMv7 XNU build. Stage53 uses Stage53-owned shims to compile only selected public pexpert/device-tree/boot-argument/debug-adjacent files and uses the ignored public `external/xnu-4570.1.46` checkout only as a later ARM implementation reference.

## Compile graph

The compile graph scanner records ten candidates:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/pexpert/arm/boot.h
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c
external/xnu-4570.1.46/osfmk/arm/start.s
external/xnu-4570.1.46/osfmk/arm/arm_init.c
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
```

Only the first three public 2050 generic pexpert sources are allowed for Stage53 compile/link. Later ARM sources remain reference-only or excluded-high-risk. `start.s`, `arm_init.c`, `arm_vm_init.c`, and `pmap.c` are explicitly not compiled.

Successful graph status:

```text
stage53_xnu_compile_graph_status=0x53000001
stage53_xnu_compile_graph_satisfied_mask=0x0003ffff
stage53_xnu_compile_graph_failure_mask=0x00000000
stage53_xnu_compile_graph_candidate_count=0x0000000a
stage53_xnu_compile_graph_allowed_compile_count=0x00000003
stage53_xnu_compile_graph_allowed_link_count=0x00000003
stage53_xnu_compile_graph_forbidden_count=0x00000007
stage53_xnu_compile_graph_shim_required_count=0x00000001
stage53_xnu_compile_graph_pe_gen_allowed=0x00000001
stage53_xnu_compile_graph_4570_reference_only=0x00000001
```

## Public-XNU object subset

Compiled public sources:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
```

Stage53-owned object-subset support:

```text
stage53/xnu_object_shims.c
stage53/shims/kern/debug.h
```

Successful compile status:

```text
stage53_xnu_object_subset_status=0x53000001
stage53_xnu_object_subset_satisfied_mask=0x0001ffff
stage53_xnu_object_subset_failure_mask=0x00000000
stage53_xnu_object_source_mask=0x00000007
stage53_xnu_object_shim_mask=0x0000003f
stage53_xnu_object_count=0x00000004
stage53_xnu_object_device_tree_sha32=0xc507eca7
stage53_xnu_object_bootargs_sha32=0x2f9e47cb
stage53_xnu_object_pe_gen_sha32=0x2998568c
```

The object count is three graph-approved public-source objects plus one Stage-owned shim object.

## Controlled link proof

The available toolchain is GNU ARM ELF-oriented (`arm-none-eabi-*`), not Apple `ld64`/Mach-O-oriented. Stage53 therefore does not claim a native bootable XNU Mach-O link. Instead, it creates a closed host-only ARM ELF proof artifact and carries only metadata about that proof in the inert Mach-O fixture.

Link inputs:

```text
out/stage53/xnu-objects/device_tree.o
out/stage53/xnu-objects/bootargs.o
out/stage53/xnu-objects/pe_gen.o
out/stage53/xnu-objects/xnu_object_shims.o
out/stage53/xnu-link/xnu_link_support.o
out/stage53/xnu-link/xnu_link_noentry.o
```

Successful link status:

```text
stage53_xnu_link_status=0x53000001
stage53_xnu_link_satisfied_mask=0x0000ffff
stage53_xnu_link_failure_mask=0x00000000
stage53_xnu_link_object_count=0x00000004
stage53_xnu_link_support_object_count=0x00000002
stage53_xnu_link_undefined_symbol_count=0x00000000
stage53_xnu_link_global_symbol_count=0x00000026
stage53_xnu_link_elf_bytes=0x00009da4
stage53_xnu_link_elf_sha32=0x29c6a3e2
stage53_xnu_link_text_addr=0x80008000
stage53_xnu_link_text_size=0x000010a2
stage53_xnu_link_data_addr=0x800090b0
stage53_xnu_link_data_size=0x00000404
stage53_xnu_link_bss_addr=0x800094c0
stage53_xnu_link_bss_size=0x00001020
```

Expected symbols include `DTInit`, `DTLookupEntry`, `DTGetProperty`, `PE_parse_boot_argn`, `PE_get_default`, `PE_boot_args`, `pe_init_debug`, `PE_enter_debugger`, `PE_init_printf`, `PE_putc`, `gPESerialBaud`, `appleClut8`, `IODTGetDefault`, `kalloc`, `kfree`, `Debugger`, `cnputc`, `vcattach`, `strncmp`, and the inert `stage53_xnu_link_noentry` anchor.

## Loader roll-up

Hardware validation reported:

```text
loader_xnu_workspace_status=0x53000001
loader_xnu_compile_graph_status=0x53000001
loader_xnu_compile_graph_status_rollup=0x53000001
loader_xnu_object_subset_status=0x53000001
loader_xnu_link_status=0x53000001
loader_xnu_link_status_rollup=0x53000001
loader_safety_mask=0x000fffff
loader_satisfied_mask=0x01ffffff
loader_status=0x53000001
```

The inherited TTBR0 round-trip/cache preservation still passes:

```text
stage53_ttbr_roundtrip_status=0x53000001
ttbr_rt_restored_ttbr0=0x0006c000
ttbr_rt_restored_ttbcr=0x00000000
ttbr_rt_restored_dacr=0x00000003
ttbr_rt_restored_sctlr=0x00c5487b
ttbr_rt_cache_bits_before=0x00000000
ttbr_rt_cache_bits_during=0x00000000
ttbr_rt_cache_bits_after=0x00000000
ttbr_rt_caches_changed=0x00000000
ttbr_rt_xnu_entry_executed=0x00000000
ttbr_rt_macho_bytes_executed=0x00000000
ttbr_rt_proposed_phys_load_written=0x00000000
ttbr_rt_proposed_tte_workspace_written=0x00000000
ttbr_rt_persistent_write_attempted=0x00000000
```

## Safety contract

- Non-persistent `fastboot boot` only for hardware validation.
- No partition flash/erase/write and no bootloader changes.
- No full public `mach_kernel` build attempt.
- No public-XNU object execution.
- No Mach-O fixture execution and no XNU `_start` / `arm_init` jump.
- No use of the controlled ARM ELF link artifact as a boot target.
- No writes to proposed XNU physical load addresses.
- No writes to proposed XNU TTE workspace physical addresses.
- No use of the proposed XNU TTE workspace as a live TTBR table.
- No mutation of `external/xnu-upstream` or `external/xnu-4570.1.46`.
- Inherited controlled TTBR0 round-trip remains Stage-owned, restored, and cache-preserving.
- Generated validation/build/link outputs stay ignored under `out/stage53/`.

## Hardware result

Stage53 was validated on Mi4 with non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage53/stage53-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage53-last_kmsg.txt
```

Fastboot accepted the image:

```text
Sending 'boot.img' (2636 KB)                       OKAY [  0.084s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.093s
```

The recovered log was 122104 bytes and contained 1861 `MI4IOS6_STAGE53` markers and 1835 `MI4IOS6_STAGE53_XNU` markers.

Key success markers:

```text
MI4IOS6_STAGE53_XNU stage53_xnu_compile_graph_status=0x53000001
MI4IOS6_STAGE53_XNU stage53_xnu_compile_graph_failure_mask=0x00000000
MI4IOS6_STAGE53_XNU stage53_xnu_compile_graph_pe_gen_allowed=0x00000001
MI4IOS6_STAGE53_XNU stage53_xnu_object_subset_status=0x53000001
MI4IOS6_STAGE53_XNU stage53_xnu_object_subset_failure_mask=0x00000000
MI4IOS6_STAGE53_XNU stage53_xnu_object_count=0x00000004
MI4IOS6_STAGE53_XNU stage53_xnu_link_status=0x53000001
MI4IOS6_STAGE53_XNU stage53_xnu_link_failure_mask=0x00000000
MI4IOS6_STAGE53_XNU stage53_xnu_link_undefined_symbol_count=0x00000000
MI4IOS6_STAGE53_XNU loader_safety_mask=0x000fffff
MI4IOS6_STAGE53_XNU loader_satisfied_mask=0x01ffffff
MI4IOS6_STAGE53_XNU loader_status=0x53000001
MI4IOS6_STAGE53_XNU Stage53 Mach-O/XNU loader preflight ok
MI4IOS6_STAGE53_XNU kernel_entry ok
MI4IOS6_STAGE53 kernel_entry returned success
MI4IOS6_STAGE53 attempting MSM8974 PS_HOLD reset
```

Negative checks passed: no data abort, no prefetch abort, no undefined-instruction abort, no watchdog-style hang marker, no public-XNU execution, no generated Mach-O execution, no full `mach_kernel` execution marker, no proposed physical/TTE workspace writes, no persistent-write marker, and no cache-bit changes.
