# Stage52 — public-XNU controlled link proof

Stage52 keeps the previous-stage no-XNU runtime safety envelope and advances the public Apple OSS XNU work from a compile-only object subset to a controlled linkability proof for the Xiaomi Mi 4 cancro / MSM8974 ARMv7 target.

It compiles the same tiny dependency-light public Darwin 12 / iOS 6-era object subset from `xnu-2050.22.13`, links that subset into a host-only ARM ELF proof artifact with Stage52-owned support objects, records deterministic link/layout/hash facts, embeds only metadata about those facts into the inert generated Mach-O fixture, and reports the result through target-side logs.

Stage52 still does **not** run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute the generated Mach-O fixture, and does not jump to XNU `_start` / `arm_init`.

## Tracked scaffold

- `targets/cancro.mk` records the declarative cancro/MSM8974 ARMv7 target facts, object-subset policy, and controlled-link/no-execution policy.
- `targets/cancro.stage52.objects` records the public-XNU inputs and boundary.
- `xnu_workspace_validate.sh` validates ignored public checkouts read-only and emits generated workspace status under `out/stage52/`.
- `xnu_object_subset_compile.sh` compiles the minimal public-XNU object subset into ignored `out/stage52/xnu-objects/`.
- `xnu_link_proof.sh` links the object subset plus Stage52-owned support into ignored `out/stage52/xnu-link/stage52-xnu-link.elf` and emits `out/stage52/xnu_link_generated.h`.
- `xnu_link_support.c` provides host-link-only freestanding libc-like support symbols.
- `xnu_link_noentry.c` provides an inert no-entry link anchor.
- `xnu_link.ld` provides deterministic ARM ELF layout for the proof artifact.
- `shims/` contains Stage52-owned compatibility headers needed to compile the selected public sources without mutating `external/`.
- `xnu_object_shims.c` provides object-subset shim support symbols such as `PE_boot_args`, `kalloc`, `kfree`, and `IODTGetDefault`.
- `xnu_workspace.*`, `xnu_object_subset.*`, and `xnu_link.*` expose target-side status ABIs used by the Stage52 loader preflight.

## Selected public baseline

Stage52 expects the ignored checkout `external/xnu-upstream` to be detached at public Apple OSS tag `xnu-2050.22.13`, commit `cc8a9b0ce917bb7115f5c97a78b38db871557db0`, with `config/MasterVersion` equal to `12.3.0`.

The public 2050 tree is intentionally treated as incomplete for a full iOS ARMv7 XNU build. Stage52 uses Stage52-owned shims to compile only selected public pexpert/device-tree/boot-argument-adjacent files and uses the ignored public `external/xnu-4570.1.46` checkout only as a later ARM implementation reference.

## Public-XNU object subset

Compiled public sources:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
```

Stage52-owned object-subset support:

```text
stage52/xnu_object_shims.c
```

Successful compile status:

```text
stage52_xnu_object_subset_status=0x52000001
stage52_xnu_object_subset_satisfied_mask=0x00003fff
stage52_xnu_object_subset_failure_mask=0x00000000
stage52_xnu_object_count=0x00000003
stage52_xnu_object_device_tree_sha32=0xc507eca7
stage52_xnu_object_bootargs_sha32=0x2f9e47cb
```

## Controlled link proof

The available toolchain is GNU ARM ELF-oriented (`arm-none-eabi-*`), not Apple `ld64`/Mach-O-oriented. Stage52 therefore does not claim a native bootable XNU Mach-O link. Instead, it creates a closed host-only ARM ELF proof artifact and carries only metadata about that proof in the inert Mach-O fixture.

Link inputs:

```text
out/stage52/xnu-objects/device_tree.o
out/stage52/xnu-objects/bootargs.o
out/stage52/xnu-objects/xnu_object_shims.o
out/stage52/xnu-link/xnu_link_support.o
out/stage52/xnu-link/xnu_link_noentry.o
```

Successful link status:

```text
stage52_xnu_link_status=0x52000001
stage52_xnu_link_satisfied_mask=0x0000ffff
stage52_xnu_link_failure_mask=0x00000000
stage52_xnu_link_undefined_symbol_count=0x00000000
stage52_xnu_link_global_symbol_count=0x0000001d
stage52_xnu_link_elf_bytes=0x000098a4
stage52_xnu_link_elf_sha32=0xe3dabf1f
stage52_xnu_link_text_addr=0x80008000
stage52_xnu_link_text_size=0x00001016
stage52_xnu_link_data_addr=0x80009020
stage52_xnu_link_data_size=0x00000100
stage52_xnu_link_bss_addr=0x80009120
stage52_xnu_link_bss_size=0x00001018
```

Expected symbols include `DTInit`, `DTLookupEntry`, `DTGetProperty`, `PE_parse_boot_argn`, `PE_get_default`, `PE_boot_args`, `IODTGetDefault`, `kalloc`, `kfree`, `strncmp`, and the inert `stage52_xnu_link_noentry` anchor.

## Loader roll-up

Hardware validation reported:

```text
loader_xnu_workspace_status=0x52000001
loader_xnu_object_subset_status=0x52000001
loader_xnu_link_status=0x52000001
loader_xnu_link_status_rollup=0x52000001
loader_safety_mask=0x0007ffff
loader_satisfied_mask=0x00ffffff
loader_status=0x52000001
```

The inherited TTBR0 round-trip/cache preservation still passes:

```text
stage52_ttbr_roundtrip_status=0x52000001
ttbr_rt_cache_bits_before=0x00000000
ttbr_rt_cache_bits_during=0x00000000
ttbr_rt_cache_bits_after=0x00000000
ttbr_rt_caches_changed=0x00000000
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
- Generated validation/build/link outputs stay ignored under `out/stage52/`.

## Hardware result

Stage52 was validated on Mi4 with non-persistent boot only:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage52/stage52-qcdt.img
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage52-last_kmsg.txt
```

Key success markers:

```text
MI4IOS6_STAGE52_XNU stage52_xnu_link_status=0x52000001
MI4IOS6_STAGE52_XNU stage52_xnu_link_failure_mask=0x00000000
MI4IOS6_STAGE52_XNU stage52_xnu_link_undefined_symbol_count=0x00000000
MI4IOS6_STAGE52_XNU loader_safety_mask=0x0007ffff
MI4IOS6_STAGE52_XNU loader_satisfied_mask=0x00ffffff
MI4IOS6_STAGE52_XNU loader_status=0x52000001
MI4IOS6_STAGE52_XNU Stage52 Mach-O/XNU loader preflight ok
MI4IOS6_STAGE52_XNU kernel_entry ok
MI4IOS6_STAGE52 kernel_entry returned success
MI4IOS6_STAGE52 attempting MSM8974 PS_HOLD reset
```

Negative checks passed: no data abort, no undefined abort, no watchdog-style hang marker, no public-XNU execution, no generated Mach-O execution, no proposed physical/TTE workspace writes, no persistent-write marker, and no cache-bit changes.
