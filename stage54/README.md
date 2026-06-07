# Stage54 — public-XNU pexpert/platform compile graph proof

Stage54 keeps the no-XNU runtime safety envelope and advances the public Apple OSS XNU migration from the Stage53 generic pexpert compile graph to a bounded public ARM pexpert/platform object proof for Xiaomi Mi 4 cancro / MSM8974 ARMv7.

The new Stage54 object is:

```text
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
```

It is used as a later-public ARM implementation reference because the selected Darwin 12 / iOS 6-era public `xnu-2050.22.13` tree does not contain a complete ARMv7 pexpert implementation. Stage54 compiles and links this object only in ignored host proof artifacts. The booted Stage54 payload imports generated facts about that host proof; it does **not** execute public-XNU code.

## Tracked scaffold

- `targets/cancro.mk` records cancro/MSM8974 ARMv7 target facts, pexpert/platform graph policy, public ARM `pe_bootargs` object policy, duplicate-symbol closure, and the no-platform-runtime-execution boundary.
- `targets/cancro.stage54.objects` records the compile/link allow-list plus ABI/reference-only, blocked-runtime, and excluded-high-risk public ARM sources.
- `xnu_workspace_validate.sh` validates ignored public checkouts read-only and emits generated workspace status under `out/stage54/`.
- `xnu_compile_graph_scan.py` classifies fourteen public XNU candidates and emits generated graph facts under `out/stage54/`.
- `xnu_object_subset_compile.sh` compiles graph-approved public objects into ignored `out/stage54/xnu-objects/` and verifies duplicate public/stage-owned symbols are closed.
- `xnu_link_proof.sh` links the object subset plus Stage54-owned support into ignored `out/stage54/xnu-link/stage54-xnu-link.elf` and emits `out/stage54/xnu_link_generated.h`.
- `shims/pexpert/boot.h` and `shims/pexpert/pexpert.h` provide a minimal compile-only ARM `boot_args` / `PE_state_t` ABI surface for public `pe_bootargs.c`.
- `xnu_object_shims.c` intentionally does **not** define `PE_boot_args()` in Stage54. The symbol comes from public `arm_pe_bootargs.o`; the Stage-owned shim object only supplies `PE_state` backing and compile/link-only support symbols.
- `xnu_workspace.*`, `xnu_compile_graph.*`, `xnu_object_subset.*`, and `xnu_link.*` expose target-side status ABIs used by the Stage54 loader preflight.

## Selected public baselines

Stage54 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for the bounded `pe_bootargs.c` compile/link proof and for blocked-runtime source classification.

## Compile graph

The Stage54 scanner classifies fourteen candidates:

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

Successful scanner status:

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
stage54_xnu_compile_graph_arm_bootargs_allowed=0x00000001
stage54_xnu_compile_graph_duplicate_symbol_count=0x00000000
stage54_xnu_compile_graph_pe_state_abi_recorded=0x00000001
stage54_xnu_compile_graph_boot_args_arm_layout_recorded=0x00000001
stage54_xnu_compile_graph_no_platform_runtime_exec=0x00000001
```

## Object subset

Compiled public sources:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
```

Stage54-owned support object:

```text
stage54/xnu_object_shims.c
```

Successful compile status:

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
stage54_xnu_object_arm_pe_bootargs_bytes=0x00000368
stage54_xnu_object_arm_pe_bootargs_sha32=0xa819bb2b
stage54_xnu_object_no_platform_runtime_exec=0x00000001
```

The count is explicit: three public 2050 objects, one later-public ARM pexpert object, and one Stage-owned shim/support object.

## Controlled link proof

Stage54 still does not claim a native bootable XNU Mach-O link. The available toolchain is GNU ARM ELF-oriented, so Stage54 creates a closed host-only ARM ELF proof artifact and embeds only proof metadata into the inert generated Mach-O fixture.

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

Expected symbols include `PE_boot_args` from public `arm_pe_bootargs.o`, `PE_state` from the Stage-owned ABI shim, `DTInit`, `DTLookupEntry`, `DTGetProperty`, `PE_parse_boot_argn`, `PE_get_default`, `pe_init_debug`, `PE_enter_debugger`, `PE_init_printf`, `PE_putc`, `gPESerialBaud`, `appleClut8`, `Debugger`, `cnputc`, `vcattach`, `kalloc`, `kfree`, `IODTGetDefault`, `strncmp`, and the inert `stage54_xnu_link_noentry` anchor.

## Loader roll-up

Hardware validation reported:

```text
loader_xnu_workspace_status=0x54000001
loader_xnu_compile_graph_status=0x54000001
loader_xnu_compile_graph_status_rollup=0x54000001
loader_xnu_object_subset_status=0x54000001
loader_xnu_object_subset_status_rollup=0x54000001
loader_xnu_link_status=0x54000001
loader_xnu_link_status_rollup=0x54000001
loader_safety_mask=0x001fffff
loader_satisfied_mask=0x01ffffff
loader_status=0x54000001
```

The completion line states the Stage54 boundary:

```text
Stage54 XNU execution disabled: pexpert/platform compile graph classified, bounded public pe_gen plus ARM pe_bootargs objects compiled, and controlled ARM ELF link proof recorded; inert MH_PRELOAD fixture carries metadata only; no full mach_kernel build, no public-XNU execution, no platform runtime execution, no Mach-O execution, no proposed physical/workspace writes, no persistent writes, caches unchanged
```

The inherited TTBR0/cache safety markers still pass:

```text
stage54_ttbr_roundtrip_status=0x54000001
ttbr_rt_restored_ttbr0=0x0006c000
ttbr_rt_restored_sctlr=0x00c5487b
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
cmdline=stage54 mi4ios6=stage54 ... public-xnu-platform-graph public-xnu-arm-pe-bootargs ... no-platform-runtime-exec ... no-persist-write no-external-mutation
part=kernel offset=0x800 size=174088 sha256=dfe43683ed1408b8006c61c69a78bbaac93d017c44e6935e8d7ff55c5934c2e6
part=dt.img offset=0x2b800 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

## Hardware result

Stage54 was validated on cancro with non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage54/stage54-qcdt.img
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage54-last_kmsg.txt
```

Fastboot accepted the image:

```text
Sending 'boot.img' (2636 KB)                       OKAY [  0.084s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.094s
```

The recovered log was 123494 bytes and contained 1878 `MI4IOS6_STAGE54` markers and 1852 `MI4IOS6_STAGE54_XNU` markers.

Key success markers:

```text
MI4IOS6_STAGE54_XNU stage54_xnu_compile_graph_status=0x54000001
MI4IOS6_STAGE54_XNU stage54_xnu_compile_graph_arm_bootargs_allowed=0x00000001
MI4IOS6_STAGE54_XNU stage54_xnu_compile_graph_duplicate_symbol_count=0x00000000
MI4IOS6_STAGE54_XNU stage54_xnu_object_subset_status=0x54000001
MI4IOS6_STAGE54_XNU stage54_xnu_object_count=0x00000005
MI4IOS6_STAGE54_XNU stage54_xnu_object_public_arm_pexpert_count=0x00000001
MI4IOS6_STAGE54_XNU stage54_xnu_object_duplicate_symbol_count=0x00000000
MI4IOS6_STAGE54_XNU stage54_xnu_link_status=0x54000001
MI4IOS6_STAGE54_XNU stage54_xnu_link_object_count=0x00000005
MI4IOS6_STAGE54_XNU stage54_xnu_link_undefined_symbol_count=0x00000000
MI4IOS6_STAGE54_XNU loader_safety_mask=0x001fffff
MI4IOS6_STAGE54_XNU loader_satisfied_mask=0x01ffffff
MI4IOS6_STAGE54_XNU loader_status=0x54000001
MI4IOS6_STAGE54_XNU Stage54 Mach-O/XNU loader preflight ok
MI4IOS6_STAGE54_XNU kernel_entry ok
MI4IOS6_STAGE54 kernel_entry returned success
MI4IOS6_STAGE54 attempting MSM8974 PS_HOLD reset
```

Negative checks passed: no data abort, no prefetch abort, no undefined-instruction abort, no watchdog-style hang marker, no public-XNU execution, no platform runtime execution, no generated Mach-O execution, no full `mach_kernel` execution marker, no proposed physical/TTE workspace writes, no persistent-write marker, and no cache-bit changes.
