# Experiment 55 — Stage52 Public-XNU Controlled Link Proof

Date: 2026-06-06

Goal: advance from Stage51's compile-only public-XNU object subset to the first controlled linkability proof for the Xiaomi Mi 4 cancro / MSM8974 ARMv7 target. Stage52 keeps the selected public Darwin 12 / iOS 6-era baseline, compiles the same minimal public-XNU object subset, links that subset into a host-only ARM ELF proof artifact with Stage52-owned support code, embeds only deterministic link metadata into an inert Mach-O fixture, and reports the link facts through target-side logs.

Stage52 still does **not** run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed XNU physical load or TTE workspace addresses, does not use the proposed XNU TTE workspace as a live TTBR table, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- No bootloader or partition changes.
- No parsed Mach-O entrypoint execution.
- No generated Mach-O fixture execution.
- No public-XNU object execution on hardware.
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
- Public-XNU object and link outputs stay ignored under `out/stage52/`.
- Public source checkouts stay ignored under `external/`.

The Stage52 completion message is intentionally explicit:

```text
Stage52 XNU execution disabled: minimal public-XNU object subset compiled and controlled ARM ELF link proof recorded; inert MH_PRELOAD fixture carries link metadata only; no full mach_kernel build, no public-XNU execution, no Mach-O execution, no proposed physical/workspace writes, no persistent writes, caches unchanged
```

## What Stage52 adds over Stage51

Stage51 compiled public-XNU objects but deliberately did not link them. Stage52 keeps the Stage51 object subset and adds a controlled link proof:

- new standalone `stage52/` payload copied from Stage51,
- Stage52 status/log prefixes (`0x52000001`, `MI4IOS6_STAGE52`),
- command-line markers for:
  - `public-xnu-controlled-link`,
  - `stage52-xnu-link-proof`,
  - `inert-macho-link-fixture`,
  - inherited `public-xnu-workspace`, `public-xnu-object-subset`, `ttbr0-roundtrip`, `recovery-table`, `cache-bits-preserved`, `no-public-xnu-exec`, `no-macho-exec`, `no-proposed-phys-write`, `no-proposed-tte-write`, `no-persist-write`,
- host-side link proof script `stage52/xnu_link_proof.sh`,
- Stage52-owned freestanding link support `stage52/xnu_link_support.c`,
- inert no-entry anchor `stage52/xnu_link_noentry.c`,
- deterministic link layout script `stage52/xnu_link.ld`,
- target-side link ABI/logs in `stage52/xnu_link.h` / `.c`,
- loader roll-up integration for the controlled link proof,
- optional link metadata embedding support in `tools/mkmacho_fixture.py` via `--metadata-file`.

Because the available toolchain is GNU ARM ELF-oriented (`arm-none-eabi-*`) rather than an Apple/Mach-O linker, Stage52 is deliberately honest: it creates a closed ARM ELF link proof and embeds only metadata about that proof into an inert generated Mach-O fixture. It does not claim to produce a bootable XNU `mach_kernel` Mach-O.

## Selected public XNU baseline

Stage52 continues to validate the selected public iOS 6 / Darwin 12-era baseline read-only:

```text
path: external/xnu-upstream
selected ref: xnu-2050.22.13
commit: cc8a9b0ce917bb7115f5c97a78b38db871557db0
config/MasterVersion: 12.3.0
compact commit32: 0xcc8a9b0c
compact master version: 0x000c0300
```

The ignored checkout was not mutated. `external/` remains ignored by git.

## Public-XNU object subset

Stage52 compiles the same dependency-light public 2050-era source files as Stage51:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
```

Stage52-owned compile/link support:

```text
stage52/xnu_object_shims.c
stage52/xnu_link_support.c
stage52/xnu_link_noentry.c
```

Successful object-subset status:

```text
stage52_xnu_object_subset_status=0x52000001
stage52_xnu_object_subset_required_mask=0x00003fff
stage52_xnu_object_subset_satisfied_mask=0x00003fff
stage52_xnu_object_subset_failure_mask=0x00000000
stage52_xnu_object_source_mask=0x00000003
stage52_xnu_object_shim_mask=0x0000001f
stage52_xnu_object_count=0x00000003
stage52_xnu_object_device_tree_bytes=0x00000f28
stage52_xnu_object_bootargs_bytes=0x00000bf4
stage52_xnu_object_device_tree_sha32=0xc507eca7
stage52_xnu_object_bootargs_sha32=0x2f9e47cb
```

## Controlled ARM ELF link proof

`stage52/xnu_link_proof.sh` runs after the object-subset compiler and consumes:

```text
out/stage52/xnu-objects/device_tree.o
out/stage52/xnu-objects/bootargs.o
out/stage52/xnu-objects/xnu_object_shims.o
```

It compiles Stage52-owned support objects:

```text
out/stage52/xnu-link/xnu_link_support.o
out/stage52/xnu-link/xnu_link_noentry.o
```

and links a closed host-only proof artifact:

```text
out/stage52/xnu-link/stage52-xnu-link.elf
```

Successful link status:

```text
stage52_xnu_link_status=0x52000001
stage52_xnu_link_required_mask=0x0000ffff
stage52_xnu_link_satisfied_mask=0x0000ffff
stage52_xnu_link_failure_mask=0x00000000
stage52_xnu_link_object_count=0x00000003
stage52_xnu_link_support_object_count=0x00000002
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
stage52_xnu_link_no_full_xnu_build=0x00000001
stage52_xnu_link_no_public_xnu_exec=0x00000001
stage52_xnu_link_no_macho_exec=0x00000001
stage52_xnu_link_no_external_mutation=0x00000001
```

Link proof hash:

```text
e3dabf1fc9a854784c308da7c02b88a3f1c3ac3616c8abdf8753fdb00f081dfd  out/stage52/xnu-link/stage52-xnu-link.elf
```

Link proof size:

```text
   text   data    bss    dec    hex filename
   4118    256   4120   8494   212e out/stage52/xnu-link/stage52-xnu-link.elf
```

Expected public/subset symbols are defined and undefined symbols are closed:

```text
ELF32, little endian, Machine: ARM, entry point 0x80008000
DTInit
DTLookupEntry
DTGetProperty
PE_parse_boot_argn
PE_get_default
PE_boot_args
kalloc
kfree
IODTGetDefault
strncmp
stage52_xnu_link_noentry
undefined symbol count: 0
```

The `stage52_xnu_link_noentry` symbol is an inert Stage52-owned anchor. It is not a path to XNU execution.

## Inert Mach-O wrapper metadata

`tools/mkmacho_fixture.py` now accepts `--metadata-file`. Stage52 uses this to embed a compact metadata string from `out/stage52/xnu-link-macho-metadata.txt` into the existing deterministic `MH_PRELOAD` fixture's `__PRELINK_INFO,__info` payload.

The fixture remains non-proprietary and inert:

```text
out/stage52/stage52_fixture.macho
sha256=b2d9e25902e1c841045009cf436eda58d06ef5047a2d0c3076dda95b6200e8b9
```

The bootable payload parses and materializes this fixture only into Stage-owned local BSS, reuses the inherited marker and zero-fill checks, and never executes the fixture.

## Target-side link ABI

The target payload imports generated host facts through `xnu_link_generated.h` and publishes them through `struct stage52_xnu_link`.

Hardware-proven link roll-up:

```text
MI4IOS6_STAGE52_XNU stage52_xnu_link_status=0x52000001
MI4IOS6_STAGE52_XNU stage52_xnu_link_satisfied_mask=0x0000ffff
MI4IOS6_STAGE52_XNU stage52_xnu_link_failure_mask=0x00000000
MI4IOS6_STAGE52_XNU stage52_xnu_link_checksum=0xfd5c3fd4
MI4IOS6_STAGE52_XNU stage52_xnu_link_undefined_symbol_count=0x00000000
MI4IOS6_STAGE52_XNU stage52_xnu_link_global_symbol_count=0x0000001d
MI4IOS6_STAGE52_XNU stage52_xnu_link_elf_bytes=0x000098a4
MI4IOS6_STAGE52_XNU stage52_xnu_link_elf_sha32=0xe3dabf1f
MI4IOS6_STAGE52_XNU stage52_xnu_link_text_addr=0x80008000
MI4IOS6_STAGE52_XNU stage52_xnu_link_text_size=0x00001016
MI4IOS6_STAGE52_XNU stage52_xnu_link_data_addr=0x80009020
MI4IOS6_STAGE52_XNU stage52_xnu_link_data_size=0x00000100
MI4IOS6_STAGE52_XNU stage52_xnu_link_bss_addr=0x80009120
MI4IOS6_STAGE52_XNU stage52_xnu_link_bss_size=0x00001018
```

## Inherited workspace and TTBR/control safety proof

Stage52 preserves the public-XNU workspace validation:

```text
MI4IOS6_STAGE52_XNU stage52_xnu_workspace_status=0x52000001
MI4IOS6_STAGE52_XNU stage52_xnu_workspace_satisfied_mask=0x0007ffff
MI4IOS6_STAGE52_XNU stage52_xnu_workspace_failure_mask=0x00000000
MI4IOS6_STAGE52_XNU stage52_xnu_workspace_checksum=0xdbf6992b
```

Stage52 also preserves the controlled no-XNU TTBR0 round-trip behavior:

```text
MI4IOS6_STAGE52_XNU stage52_ttbr_roundtrip_status=0x52000001
MI4IOS6_STAGE52_XNU ttbr_rt_cache_bits_before=0x00000000
MI4IOS6_STAGE52_XNU ttbr_rt_cache_bits_during=0x00000000
MI4IOS6_STAGE52_XNU ttbr_rt_cache_bits_after=0x00000000
MI4IOS6_STAGE52_XNU ttbr_rt_caches_changed=0x00000000
```

The Stage-owned recovery L1 table remains separate from proposed XNU tables, original live MMU state is restored, and caches remain unchanged.

## Loader roll-up

Stage52 replaces Stage51's no-link safety assertion with a controlled-link/no-execution safety bit while keeping the safety mask value stable.

Hardware-proven loader values:

```text
MI4IOS6_STAGE52_XNU loader_xnu_workspace_status=0x52000001
MI4IOS6_STAGE52_XNU loader_xnu_object_subset_status=0x52000001
MI4IOS6_STAGE52_XNU loader_xnu_link_status=0x52000001
MI4IOS6_STAGE52_XNU loader_xnu_link_status_rollup=0x52000001
MI4IOS6_STAGE52_XNU loader_safety_mask=0x0007ffff
MI4IOS6_STAGE52_XNU loader_satisfied_mask=0x00ffffff
MI4IOS6_STAGE52_XNU loader_checksum=0x2f26c102
MI4IOS6_STAGE52_XNU loader_status=0x52000001
```

The inherited Mach-O materialization checks also pass with Stage52 markers:

```text
MI4IOS6_STAGE52_XNU macho_staging_marker_mask=0x00000007
MI4IOS6_STAGE52_XNU macho_staging_failure_mask=0x00000000
MI4IOS6_STAGE52_XNU macho_staging_status=0x52000001
```

## Built image

```bash
/mnt/data/mi4-ios6/stage52/build.sh
```

Successful local build:

```text
out/stage52/stage52-qcdt.img
sha256=cedad6c60be5536bc59f24a24b4d5df96dbf6fa0ae8c5bf64b7155fe85ce9033
```

Build hashes:

```text
b2d9e25902e1c841045009cf436eda58d06ef5047a2d0c3076dda95b6200e8b9  out/stage52/stage52_fixture.macho
7707f786524bdd4078956cd92797ad77d90ca612476b2a2da68202ba194b3a26  out/stage52/stage52.elf
b32277882cdee165a74ec0dc35dbb81ccbe7e4264a474d72c1e5d67b86e8d65e  out/stage52/stage52.bin
c4d085f905b9259d715321702a2347f92f465ccf1342505faf419e765d8eb976  out/stage52/stage52.img
cedad6c60be5536bc59f24a24b4d5df96dbf6fa0ae8c5bf64b7155fe85ce9033  out/stage52/stage52-qcdt.img
```

Size:

```text
text=171252 data=0 bss=250480 dec=421732 hex=66f64
```

Boot image parse highlights:

```text
magic=ANDROID!
page_size=2048
kernel_size=171252 (0x29cf4)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage52 mi4ios6=stage52 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved public-xnu-workspace public-xnu-object-subset public-xnu-controlled-link stage52-xnu-link-proof inert-macho-link-fixture cancro-target-scaffold no-full-xnu-build no-xnu-jump no-public-xnu-exec no-macho-exec no-proposed-phys-write no-proposed-tte-write no-persist-write no-external-mutation
part=kernel offset=0x800 size=171252 sha256=b32277882cdee165a74ec0dc35dbb81ccbe7e4264a474d72c1e5d67b86e8d65e
part=dt.img offset=0x2a800 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Generated output and external checkout policy was verified:

```text
external/ is ignored
out/ is ignored
out/stage52/xnu-link/stage52-xnu-link.elf is ignored
out/stage52/stage52-qcdt.img is ignored
external/xnu-upstream status --short: clean
external/xnu-4570.1.46 status --short: clean
```

## Hardware run result

Hardware run succeeded using the required non-persistent path.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage52/stage52-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2632 KB)                       OKAY [  0.084s]
Booting                                            OKAY [  0.006s]
Finished. Total time: 0.095s
```

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage52-last_kmsg.txt
```

The recovered log was 119349 bytes and contained:

```text
1824 MI4IOS6_STAGE52 markers
1798 MI4IOS6_STAGE52_XNU markers
```

## Key recovered markers

Link proof gates pass:

```text
MI4IOS6_STAGE52_XNU stage52_xnu_link_status=0x52000001
MI4IOS6_STAGE52_XNU stage52_xnu_link_failure_mask=0x00000000
MI4IOS6_STAGE52_XNU stage52_xnu_link_undefined_symbol_count=0x00000000
MI4IOS6_STAGE52_XNU stage52_xnu_link_checksum=0xfd5c3fd4
```

Loader/final success markers:

```text
MI4IOS6_STAGE52_XNU loader_xnu_link_status=0x52000001
MI4IOS6_STAGE52_XNU loader_xnu_link_status_rollup=0x52000001
MI4IOS6_STAGE52_XNU loader_safety_mask=0x0007ffff
MI4IOS6_STAGE52_XNU loader_satisfied_mask=0x00ffffff
MI4IOS6_STAGE52_XNU loader_status=0x52000001
MI4IOS6_STAGE52_XNU Stage52 Mach-O/XNU loader preflight ok
MI4IOS6_STAGE52_XNU kernel_entry ok
MI4IOS6_STAGE52 kernel_entry returned success
MI4IOS6_STAGE52 attempting MSM8974 PS_HOLD reset
```

Negative checks passed: no data abort, no undefined abort, no watchdog-style hang marker, no public-XNU function execution, no generated Mach-O execution, no persistent-write marker, and no cache-bit changes. The only recovered log line containing the phrase `public-XNU execution` is the explicit negative completion message saying `no public-XNU execution`.

## Interpretation

Stage52 is still not a real XNU boot, but it removes the next compile/link blocker. The project now has a reproducible public-only ARMv7 object-subset compile and a closed host-only ARM ELF link proof for selected public Darwin 12/iOS 6-era XNU code, reported through target-side logs and validated on Mi4 hardware.

The important boundary is that Stage52 links the public-XNU subset only as a controlled proof artifact. The bootable Stage52 payload imports generated link facts and embeds inert metadata in the generated Mach-O fixture; it does not contain or call a live XNU handoff path. The inherited no-XNU TTBR0 round-trip, Mach-O parser/materialization, local TTE/high-VA/safe-table preflight, SGI/timer IRQ retests, ram_console logging, and PS_HOLD reset path all remain intact.

## Success criteria — met

1. Stage52 directory and build outputs created: yes
2. Stage52 command line includes controlled-link markers: yes
3. Stale Stage51 marker checks removed from Stage52 runtime code: yes (`ST52-*` marker checks pass)
4. Host object-subset compiler preserved: yes
5. Public `device_tree.c` compiled as ARMv7 object: yes
6. Public `bootargs.c` compiled as ARMv7 object: yes
7. Stage52-owned shim support object compiled: yes
8. Controlled ARM ELF link proof added: yes
9. Link proof undefined symbol count is zero: yes
10. Link proof status returned `0x52000001`: yes
11. Link proof satisfied mask reached `0x0000ffff`: yes
12. Link proof failure mask stayed zero: yes
13. Link metadata embedded into inert Mach-O fixture: yes
14. No full public `mach_kernel` build attempted: yes
15. No public-XNU object execution occurred: yes
16. No Mach-O fixture execution occurred: yes
17. External checkouts remain ignored and clean: yes
18. Target-side link ABI added: yes
19. Loader link status returned `0x52000001`: yes
20. Loader safety mask reached `0x0007ffff`: yes
21. Loader satisfied mask reached `0x00ffffff`: yes
22. Loader preflight returned status `0x52000001`: yes
23. Inherited TTBR round-trip still returned `0x52000001`: yes
24. Cache bits before/during/after stayed `0x00000000`: yes
25. Proposed physical load writes remained zero: yes
26. Proposed physical workspace writes remained zero: yes
27. Persistent write attempt remained zero: yes
28. Bootloader accepted `stage52-qcdt.img`: yes
29. `kernel_entry` returned success: yes
30. Payload reset through PS_HOLD: yes

## Next stage

Stage52 completed the accelerated five-stage route to begin formal XNU compile migration. Stage53 has now completed that next migration step while preserving the proven safety boundary: it added a fail-closed public-XNU compile graph, graph-gated the bounded `pexpert/gen/pe_gen.c` expansion, kept later public ARM bring-up sources reference-only/excluded, linked the graph-approved subset into a controlled host-only ARM ELF proof with zero undefined symbols, and validated `loader_safety_mask=0x000fffff`, `loader_satisfied_mask=0x01ffffff`, and loader status `0x53000001` on hardware without public-XNU or Mach-O execution.

Stage54 should continue from the Stage53 graph discipline:

- expand from the tiny pexpert/device-tree/debug subset toward additional tracked public-XNU pexpert/platform compile units,
- keep public `xnu-2050.22.13` as the Darwin 12/iOS 6-era baseline,
- use later public ARM sources only as references for missing ARMv7 boot implementation details,
- create cancro/MSM8974 platform shim packages for pexpert/timebase/interrupt hooks,
- keep build/link outputs ignored under `out/stage54/`,
- do not jump into XNU or execute public-XNU code on hardware until boot args, page tables, pmap, interrupts, timer, and cache policy are proven,
- continue non-persistent `sudo fastboot boot` validation only.
