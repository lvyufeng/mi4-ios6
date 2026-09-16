# Experiment 56 — Stage53 Public-XNU Compile Graph Migration Proof

Date: 2026-06-07

Goal: advance from Stage52's controlled public-XNU object-subset link proof to a formal compile-migration decision surface for the Xiaomi Mi 4 cancro / MSM8974 ARMv7 target. Stage53 keeps the selected public Darwin 12 / iOS 6-era XNU baseline, classifies a bounded set of public XNU source candidates, gates the first expanded public source (`pexpert/gen/pe_gen.c`) through that graph, extends the host-only ARM ELF link proof, and reports the graph/object/link facts through target-side logs.

Stage53 still does **not** run XNU or iOS. It does not build a full public `mach_kernel`, does not execute public-XNU object code, does not execute the generated Mach-O fixture, does not jump to XNU `_start` / `arm_init`, does not write proposed XNU physical load or TTE workspace addresses, does not use the proposed XNU TTE workspace as a live TTBR table, does not mutate external public checkouts, does not write persistent storage, and does not enable or change caches.

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
- Public-XNU graph/object/link outputs stay ignored under `out/stage53/`.
- Public source checkouts stay ignored under `external/`.

The Stage53 completion message is intentionally explicit:

```text
Stage53 XNU execution disabled: public-XNU compile graph classified, bounded pe_gen object compiled, and controlled ARM ELF link proof recorded; inert MH_PRELOAD fixture carries metadata only; no full mach_kernel build, no public-XNU execution, no Mach-O execution, no proposed physical/workspace writes, no persistent writes, caches unchanged
```

## What Stage53 adds over Stage52

Stage52 linked two dependency-light public XNU sources into a controlled host-only ARM ELF proof. Stage53 keeps that proof style and adds compile-migration structure before expanding the object set:

- new standalone `stage53/` payload copied from Stage52,
- Stage53 status/log prefixes (`0x53000001`, `MI4IOS6_STAGE53`),
- command-line markers for:
  - `public-xnu-compile-graph`,
  - `public-xnu-bounded-pe-gen`,
  - `public-xnu-controlled-link`,
  - `stage53-xnu-link-proof`,
  - inherited `public-xnu-workspace`, `public-xnu-object-subset`, `ttbr0-roundtrip`, `recovery-table`, `cache-bits-preserved`, `no-public-xnu-exec`, `no-macho-exec`, `no-proposed-phys-write`, `no-proposed-tte-write`, `no-cache-policy-change`, `no-persist-write`,
- host-side compile graph scanner `stage53/xnu_compile_graph_scan.py`,
- target-side graph ABI/logs in `stage53/xnu_compile_graph.h` / `.c`,
- Stage53-owned `shims/kern/debug.h` for the bounded `pe_gen.c` compile,
- Stage53-owned no-op support for `Debugger`, `cnputc`, and `vcattach`,
- expanded object-subset compiler including `pexpert/gen/pe_gen.c`,
- expanded controlled ARM ELF link proof including `pe_gen.o`,
- loader roll-up integration for graph status before object/link status.

The compile graph is the main Stage53 boundary. It prevents the next migration step from becoming an uncontrolled source inclusion exercise.

## Selected public XNU baseline

Stage53 continues to validate the selected public iOS 6 / Darwin 12-era baseline read-only:

```text
path: external/xnu-upstream
selected ref: xnu-2050.22.13
commit: cc8a9b0ce917bb7115f5c97a78b38db871557db0
config/MasterVersion: 12.3.0
compact commit32: 0xcc8a9b0c
compact master version: 0x000c0300
```

The ignored checkout was not mutated. `external/` remains ignored by git.

`external/xnu-4570.1.46` remains reference-only for later public ARM implementation details. Stage53 records selected 4570 ARM files in the graph as reference-only or excluded high-risk candidates; it does not compile or link them.

## Compile graph scanner

`stage53/xnu_compile_graph_scan.py` runs after workspace validation and before object compilation. It writes generated, ignored artifacts under `out/stage53/`:

```text
out/stage53/xnu-compile-graph-status.txt
out/stage53/xnu-compile-graph-manifest.tsv
out/stage53/xnu-compile-graph-includes.txt
out/stage53/xnu-compile-graph-symbols.txt
out/stage53/xnu-compile-graph-risk.txt
out/stage53/xnu-compile-graph-manifest.json
out/stage53/xnu_compile_graph_generated.h
```

The scanner classifies ten candidates:

```text
external/xnu-upstream/pexpert/gen/device_tree.c        compile-proven
external/xnu-upstream/pexpert/gen/bootargs.c           compile-proven
external/xnu-upstream/pexpert/gen/pe_gen.c             bounded-new
external/xnu-4570.1.46/pexpert/pexpert/arm/boot.h      reference-only
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c       reference-only
external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c reference-only
external/xnu-4570.1.46/osfmk/arm/start.s               excluded-high-risk
external/xnu-4570.1.46/osfmk/arm/arm_init.c            excluded-high-risk
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c         excluded-high-risk
external/xnu-4570.1.46/osfmk/arm/pmap.c                excluded-high-risk
```

Successful scanner output:

```text
stage53_xnu_compile_graph_status=0x53000001
stage53_xnu_compile_graph_required_mask=0x0003ffff
stage53_xnu_compile_graph_satisfied_mask=0x0003ffff
stage53_xnu_compile_graph_failure_mask=0x00000000
stage53_xnu_compile_graph_candidate_count=0x0000000a
stage53_xnu_compile_graph_allowed_compile_count=0x00000003
stage53_xnu_compile_graph_allowed_link_count=0x00000003
stage53_xnu_compile_graph_forbidden_count=0x00000007
stage53_xnu_compile_graph_shim_required_count=0x00000001
stage53_xnu_compile_graph_max_risk_class=0x00000004
stage53_xnu_compile_graph_pe_gen_allowed=0x00000001
stage53_xnu_compile_graph_4570_reference_only=0x00000001
stage53_xnu_compile_graph_no_full_xnu_build=0x00000001
stage53_xnu_compile_graph_no_public_xnu_exec=0x00000001
stage53_xnu_compile_graph_no_macho_exec=0x00000001
stage53_xnu_compile_graph_no_external_mutation=0x00000001
stage53_xnu_compile_graph_outputs_ignored=0x00000001
stage53_xnu_compile_graph_fail_closed=0x00000001
```

## `pe_gen.c` dependency surface

Stage53's only new public source is:

```text
external/xnu-upstream/pexpert/gen/pe_gen.c
```

Its bounded symbol surface is small enough to classify and close with explicit Stage-owned shims:

```text
defined:   pe_init_debug, PE_enter_debugger, PE_init_printf, PE_putc, gPESerialBaud, appleClut8
undefined: PE_parse_boot_argn, Debugger, cnputc, vcattach
shim:      shims/kern/debug.h
```

Stage53 provides only no-op compile/link support for `Debugger`, `cnputc`, and `vcattach`. These support definitions do not implement a debugger, console driver, scheduler, VM, IOKit service, interrupt controller, platform driver, or real pexpert runtime. The bootable Stage53 payload imports facts about the host-side proof; it does not call the public `pe_gen.c` functions on hardware.

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

Successful object-subset status:

```text
stage53_xnu_object_subset_status=0x53000001
stage53_xnu_object_subset_required_mask=0x0001ffff
stage53_xnu_object_subset_satisfied_mask=0x0001ffff
stage53_xnu_object_subset_failure_mask=0x00000000
stage53_xnu_object_source_mask=0x00000007
stage53_xnu_object_shim_mask=0x0000003f
stage53_xnu_object_count=0x00000004
stage53_xnu_object_device_tree_bytes=0x00000f28
stage53_xnu_object_bootargs_bytes=0x00000bf4
stage53_xnu_object_pe_gen_bytes=0x000008d0
stage53_xnu_object_device_tree_sha32=0xc507eca7
stage53_xnu_object_bootargs_sha32=0x2f9e47cb
stage53_xnu_object_pe_gen_sha32=0x2998568c
```

The count is explicit: three public-source objects plus one Stage-owned shim object.

## Controlled ARM ELF link proof

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
stage53_xnu_link_required_mask=0x0000ffff
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

Expected public/subset symbols are defined and undefined symbols are closed:

```text
DTInit
DTLookupEntry
DTGetProperty
PE_parse_boot_argn
PE_get_default
PE_boot_args
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
stage53_xnu_link_noentry
undefined symbol count: 0
```

The `stage53_xnu_link_noentry` symbol is an inert Stage53-owned anchor. It is not a path to XNU execution.

## Inert Mach-O wrapper metadata

Stage53 embeds a compact metadata string from `out/stage53/xnu-link-macho-metadata.txt` into the deterministic `MH_PRELOAD` fixture's `__PRELINK_INFO,__info` payload. The metadata records the compile graph and controlled link proof, including:

```text
stage53-xnu-compile-graph
stage53-xnu-link-proof
bounded-pe-gen=true
objects=4
undefined=0
no-full-mach-kernel=true
no-public-xnu-exec=true
no-macho-exec=true
```

The fixture remains non-proprietary and inert:

```text
out/stage53/stage53_fixture.macho
sha256=48e1a6403f909bfddddcf97227853ee358dec399eb83ed968c15554012e48eba
```

The bootable payload parses and materializes this fixture only into Stage-owned local BSS, reuses the inherited marker and zero-fill checks, and never executes the fixture.

## Target-side compile graph ABI

The target payload imports generated host facts through `xnu_compile_graph_generated.h` and publishes them through `struct stage53_xnu_compile_graph`.

Hardware-proven graph roll-up:

```text
MI4IOS6_STAGE53_XNU stage53_xnu_compile_graph_status=0x53000001
MI4IOS6_STAGE53_XNU stage53_xnu_compile_graph_satisfied_mask=0x0003ffff
MI4IOS6_STAGE53_XNU stage53_xnu_compile_graph_failure_mask=0x00000000
MI4IOS6_STAGE53_XNU stage53_xnu_compile_graph_pe_gen_allowed=0x00000001
MI4IOS6_STAGE53_XNU stage53_xnu_compile_graph_4570_reference_only=0x00000001
MI4IOS6_STAGE53_XNU loader_xnu_compile_graph_status=0x53000001
MI4IOS6_STAGE53_XNU loader_xnu_compile_graph_status_rollup=0x53000001
```

## Inherited workspace and TTBR/control safety proof

Stage53 preserves the public-XNU workspace validation:

```text
MI4IOS6_STAGE53_XNU stage53_xnu_workspace_status=0x53000001
MI4IOS6_STAGE53_XNU stage53_xnu_workspace_failure_mask=0x00000000
```

Stage53 also preserves the controlled no-XNU TTBR0 round-trip behavior:

```text
MI4IOS6_STAGE53_XNU stage53_ttbr_roundtrip_status=0x53000001
MI4IOS6_STAGE53_XNU stage53_ttbr_roundtrip_satisfied_mask=0x0000ffff
MI4IOS6_STAGE53_XNU stage53_ttbr_roundtrip_failure_mask=0x00000000
MI4IOS6_STAGE53_XNU ttbr_rt_restored_ttbr0=0x0006c000
MI4IOS6_STAGE53_XNU ttbr_rt_restored_ttbcr=0x00000000
MI4IOS6_STAGE53_XNU ttbr_rt_restored_dacr=0x00000003
MI4IOS6_STAGE53_XNU ttbr_rt_restored_sctlr=0x00c5487b
MI4IOS6_STAGE53_XNU ttbr_rt_cache_bits_before=0x00000000
MI4IOS6_STAGE53_XNU ttbr_rt_cache_bits_during=0x00000000
MI4IOS6_STAGE53_XNU ttbr_rt_cache_bits_after=0x00000000
MI4IOS6_STAGE53_XNU ttbr_rt_caches_changed=0x00000000
MI4IOS6_STAGE53_XNU ttbr_rt_xnu_entry_executed=0x00000000
MI4IOS6_STAGE53_XNU ttbr_rt_macho_bytes_executed=0x00000000
MI4IOS6_STAGE53_XNU ttbr_rt_proposed_phys_load_written=0x00000000
MI4IOS6_STAGE53_XNU ttbr_rt_proposed_tte_workspace_written=0x00000000
MI4IOS6_STAGE53_XNU ttbr_rt_persistent_write_attempted=0x00000000
```

The Stage-owned recovery L1 table remains separate from proposed XNU tables, original live MMU state is restored, and caches remain unchanged.

## Loader roll-up

Stage53 adds compile-graph status to the loader preflight. Hardware-proven loader values:

```text
MI4IOS6_STAGE53_XNU loader_xnu_workspace_status=0x53000001
MI4IOS6_STAGE53_XNU loader_xnu_compile_graph_status=0x53000001
MI4IOS6_STAGE53_XNU loader_xnu_compile_graph_status_rollup=0x53000001
MI4IOS6_STAGE53_XNU loader_xnu_object_subset_status=0x53000001
MI4IOS6_STAGE53_XNU loader_xnu_link_status=0x53000001
MI4IOS6_STAGE53_XNU loader_xnu_link_status_rollup=0x53000001
MI4IOS6_STAGE53_XNU loader_safety_mask=0x000fffff
MI4IOS6_STAGE53_XNU loader_satisfied_mask=0x01ffffff
MI4IOS6_STAGE53_XNU loader_status=0x53000001
```

The inherited Mach-O materialization checks also pass with Stage53 markers:

```text
MI4IOS6_STAGE53_XNU macho_staging_marker_mask=0x00000007
MI4IOS6_STAGE53_XNU macho_staging_failure_mask=0x00000000
MI4IOS6_STAGE53_XNU macho_staging_status=0x53000001
```

## Built image

```bash
/mnt/data/mi4-ios6/stage53/build.sh
```

Successful local build:

```text
out/stage53/stage53-qcdt.img
sha256=9fc8a82c672d161d7becf9bcb5928e70ed032a4230e725e5b92b9df296cb2b06
```

Build hashes:

```text
48e1a6403f909bfddddcf97227853ee358dec399eb83ed968c15554012e48eba  out/stage53/stage53_fixture.macho
5a159084bbb8bf4ae609db0cabb061a36b1682856a64d2624f4482728be70acb  out/stage53/stage53.elf
8bede160655c29f3c6b83f84f8fc17be66860a2a64ffeab64c17be401cf04486  out/stage53/stage53.bin
79356c09a144ec5d82e2c2654e5b28efabe79b664aebda4dd90c90060f440ca1  out/stage53/stage53.img
9fc8a82c672d161d7becf9bcb5928e70ed032a4230e725e5b92b9df296cb2b06  out/stage53/stage53-qcdt.img
```

Size:

```text
text=174096 data=0 bss=250616 dec=424712 hex=67b08
```

Boot image parse highlights:

```text
magic=ANDROID!
page_size=2048
kernel_size=174096 (0x2a810)
kernel_addr=32768 (0x8000)
ramdisk_size=0 (0x0)
ramdisk_addr=33554432 (0x2000000)
second_size=0 (0x0)
second_addr=15728640 (0xf00000)
tags_addr=31457280 (0x1e00000)
dt_size=2521088 (0x267800)
cmdline=stage53 mi4ios6=stage53 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved public-xnu-workspace public-xnu-compile-graph public-xnu-object-subset public-xnu-controlled-link public-xnu-bounded-pe-gen stage53-xnu-link-proof inert-macho-link-fixture cancro-target-scaffold no-full-xnu-build no-xnu-jump no-public-xnu-exec no-macho-exec no-proposed-phys-write no-proposed-tte-write no-cache-policy-change no-persist-write no-external-mutation
part=kernel offset=0x800 size=174096 sha256=8bede160655c29f3c6b83f84f8fc17be66860a2a64ffeab64c17be401cf04486
part=dt.img offset=0x2b800 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Generated output and external checkout policy was verified:

```text
external/ is ignored
out/ is ignored
out/stage53/xnu-link/stage53-xnu-link.elf is ignored
out/stage53/stage53-qcdt.img is ignored
external/xnu-upstream status --short: clean
external/xnu-4570.1.46 status --short: clean
```

## Hardware run result

Hardware run succeeded using the required non-persistent path.

Command:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage53/stage53-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2636 KB)                       OKAY [  0.084s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.093s
```

Recovered persistent log:

```bash
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage53-last_kmsg.txt
```

The recovered log was 122104 bytes and contained:

```text
1861 MI4IOS6_STAGE53 markers
1835 MI4IOS6_STAGE53_XNU markers
```

## Key recovered markers

Compile graph gates pass:

```text
MI4IOS6_STAGE53_XNU stage53_xnu_compile_graph_status=0x53000001
MI4IOS6_STAGE53_XNU stage53_xnu_compile_graph_satisfied_mask=0x0003ffff
MI4IOS6_STAGE53_XNU stage53_xnu_compile_graph_failure_mask=0x00000000
MI4IOS6_STAGE53_XNU stage53_xnu_compile_graph_pe_gen_allowed=0x00000001
MI4IOS6_STAGE53_XNU loader_xnu_compile_graph_status=0x53000001
MI4IOS6_STAGE53_XNU loader_xnu_compile_graph_status_rollup=0x53000001
```

Object/link proof gates pass:

```text
MI4IOS6_STAGE53_XNU stage53_xnu_object_subset_status=0x53000001
MI4IOS6_STAGE53_XNU stage53_xnu_object_subset_failure_mask=0x00000000
MI4IOS6_STAGE53_XNU stage53_xnu_object_count=0x00000004
MI4IOS6_STAGE53_XNU stage53_xnu_object_pe_gen_bytes=0x000008d0
MI4IOS6_STAGE53_XNU stage53_xnu_link_status=0x53000001
MI4IOS6_STAGE53_XNU stage53_xnu_link_failure_mask=0x00000000
MI4IOS6_STAGE53_XNU stage53_xnu_link_object_count=0x00000004
MI4IOS6_STAGE53_XNU stage53_xnu_link_undefined_symbol_count=0x00000000
```

Loader/final success markers:

```text
MI4IOS6_STAGE53_XNU loader_safety_mask=0x000fffff
MI4IOS6_STAGE53_XNU loader_satisfied_mask=0x01ffffff
MI4IOS6_STAGE53_XNU loader_status=0x53000001
MI4IOS6_STAGE53_XNU Stage53 Mach-O/XNU loader preflight ok
MI4IOS6_STAGE53_XNU kernel_entry ok
MI4IOS6_STAGE53 kernel_entry returned success
MI4IOS6_STAGE53 attempting MSM8974 PS_HOLD reset
```

Negative checks passed: no data abort, no prefetch abort, no undefined-instruction abort, no watchdog-style hang marker, no public-XNU function execution, no generated Mach-O execution, no full `mach_kernel` build marker, no persistent-write marker, no proposed physical load write, no proposed TTE workspace write, and no cache-bit changes. The recovered log lines containing the phrase `no public-XNU execution` and `no proposed physical/workspace writes` are explicit negative safety messages.

## Interpretation

Stage53 is still not a real XNU boot, but it removes the next migration blocker. The project now has a repeatable public-only source graph for the first tiny XNU migration set, a graph-gated expansion to `pexpert/gen/pe_gen.c`, a closed host-only ARM ELF link proof with zero undefined symbols, and target-side hardware logs proving those host-generated facts were imported and checked without executing public-XNU code.

The important boundary is that Stage53 migrates compile/link facts, not runtime control. The bootable Stage53 payload imports generated graph/object/link facts and embeds inert metadata in the generated Mach-O fixture; it does not call public-XNU functions or branch to any XNU entry. The inherited no-XNU TTBR0 round-trip, Mach-O parser/materialization, local TTE/high-VA/safe-table preflight, SGI/timer IRQ retests, ram_console logging, and PS_HOLD reset path all remain intact.

## Success criteria — met

1. Stage53 directory and build outputs created: yes
2. Stage53 command line includes compile-graph and bounded-`pe_gen` markers: yes
3. Stale Stage52 marker checks removed from Stage53 runtime code: yes (`ST53-*` marker checks pass)
4. Public-XNU workspace validator preserved: yes
5. Compile graph scanner added and fail-closed: yes
6. Graph candidate manifest records compile-proven, bounded-new, reference-only, and excluded-high-risk candidates: yes
7. `start.s`, `arm_init.c`, `arm_vm_init.c`, and `pmap.c` remain excluded/reference-only: yes
8. Public `device_tree.c` compiled as ARMv7 object: yes
9. Public `bootargs.c` compiled as ARMv7 object: yes
10. Public `pe_gen.c` compiled as ARMv7 object only after graph approval: yes
11. Stage53-owned shim support object compiled: yes
12. `shims/kern/debug.h` added for the bounded `pe_gen.c` dependency surface: yes
13. Controlled ARM ELF link proof updated: yes
14. Link proof undefined symbol count is zero: yes
15. Link proof status returned `0x53000001`: yes
16. Link proof satisfied mask reached `0x0000ffff`: yes
17. Link proof failure mask stayed zero: yes
18. Compile graph status returned `0x53000001`: yes
19. Compile graph satisfied mask reached `0x0003ffff`: yes
20. Compile graph failure mask stayed zero: yes
21. Link metadata embedded into inert Mach-O fixture: yes
22. No full public `mach_kernel` build attempted: yes
23. No public-XNU object execution occurred: yes
24. No Mach-O fixture execution occurred: yes
25. External checkouts remain ignored and clean: yes
26. Target-side graph ABI added: yes
27. Loader graph status returned `0x53000001`: yes
28. Loader safety mask reached `0x000fffff`: yes
29. Loader satisfied mask reached `0x01ffffff`: yes
30. Loader preflight returned status `0x53000001`: yes
31. Inherited TTBR round-trip still returned `0x53000001`: yes
32. Cache bits before/during/after stayed `0x00000000`: yes
33. Proposed physical load writes remained zero: yes
34. Proposed physical workspace writes remained zero: yes
35. Persistent write attempt remained zero: yes
36. Bootloader accepted `stage53-qcdt.img`: yes
37. `kernel_entry` returned success: yes
38. Payload reset through PS_HOLD: yes

## Next stage

Stage53 completed the formal compile graph proof for the first expanded public-XNU subset. Stage54 has now used that graph discipline to migrate one bounded public ARM pexpert/platform object (`external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c`) into the host-only proof while preserving the proven no-execution boundary, closing duplicate symbols, and validating no-platform-runtime-execution status on hardware.

Stage55 should continue from the Stage54 pexpert/platform graph:

- expand only the next smallest tracked public-XNU pexpert/platform surface,
- keep public `xnu-2050.22.13` as the Darwin 12/iOS 6-era baseline,
- use later public ARM sources only as references for missing ARMv7 boot implementation details,
- continue defining cancro/MSM8974 pexpert shim packages for timebase, interrupt, debug, and platform identification hooks,
- continue refusing `start.s`, `arm_init.c`, `arm_vm_init.c`, `pmap.c`, scheduler, VM, and IOKit sources until their preconditions are explicitly modeled,
- keep build/link outputs ignored under `out/stage55/`,
- do not jump into XNU or execute public-XNU/platform-runtime code on hardware until boot args, page tables, pmap, interrupts, timer, and cache policy are proven,
- continue non-persistent `sudo fastboot boot` validation only.
