# Experiment 74 — Stage71 XNU IOKit attach/start readiness dry-run contract

## Goal

Stage71 advances the Xiaomi Mi 4 `cancro` XNU/Darwin bring-up from the Stage70 local IOKit registry-plane / IODeviceTree topology dry-run toward a local attach/start readiness proof.

The goal is not to run XNU, not to execute IOKit, not to execute `IORegistryEntry`, `IOService`, `IODeviceTreeSupport`, registry-topology, property-inheritance, registry-entry, attach/start, provider-plane, catalog, property-plane, or platform-driver runtime code, and not to install live XNU/pmap tables. The goal is a Stage-owned, fail-closed contract proving that the four selected Stage70 topology entries can be projected into deterministic local attach/start readiness facts for platform, interrupt, timer, and CPU personalities while one explicit rejected personality remains unpublished, unattached, unstarted, and unlinked.

## Safety boundary

Stage71 preserves all Stage70 boundaries and extends them to attach/start readiness runtime:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No IOKit match, registry/service, provider-plane, catalog, property-plane, property-inheritance, registry-entry, registry-topology, attach/start, or platform-driver runtime execution.
- No Stage-owned MSM8974 platform-driver execution.
- No generated Mach-O execution.
- No XNU `_start` / `arm_init` jump.
- No proposed physical load-address writes.
- No proposed TTE/pmap workspace writes.
- No live proposed XNU/pmap table install.
- No TTBR/TTBCR/DACR/SCTLR writes for proposed pmap install.
- No TLB invalidation for proposed pmap install.
- No cache policy change.
- No mutation of ignored `external/` public XNU checkouts.

The public object subset remains bounded to:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage71/xnu_object_shims.c
```

Public ARM VM/pmap files and public IOKit files remain reference-only. The public IOKit reference-only set is:

```text
external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp
external/xnu-4570.1.46/iokit/Kernel/IOPlatformExpert.cpp
external/xnu-4570.1.46/iokit/Kernel/IOCPU.cpp
```

They are not compiled, linked, or executed.

## Implementation

Stage71 adds:

```text
stage71/xnu_iokit_attach_start_readiness_dryrun_contract.c
struct stage71_xnu_iokit_attach_start_readiness_dryrun_contract
```

The new contract imports source facts from:

- the Stage71 IOKit registry-plane / IODeviceTree topology dry-run contract,
- the Stage71 IOKit property-inheritance / registry-entry dry-run contract,
- the Stage71 IOKit catalog/property/personality dry-run contract,
- the Stage71 IOKit provider-plane dry-run contract,
- the Stage71 broader IOKit registry/service dry-run contract,
- the Stage71 IOKit/platform scaffold contract,
- the Stage71 single-match IOKit dry-run contract,
- the Stage71 pexpert hook readiness contract,
- the Stage71 public compile graph/object subset/controlled link proof,
- the retained pmap transition dry-run contract,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

Stage71 keeps the Stage70 root-node set and adds no new root children. The Stage-owned Apple flattened device tree root remains at 19 children. The four selected catalog personality nodes are extended with local attach/start readiness properties:

```text
/stage71-platform-personality
/stage71-interrupt-personality
/stage71-timer-personality
/stage71-cpu-personality

attach-start-readiness-dryrun=1
attach-readiness=1
start-readiness=1
attach-runtime-exec=0
start-runtime-exec=0
attach-provider-path="IODeviceTree:/" or "IODeviceTree:/stage71-platform-personality"
start-provider-path="IODeviceTree:/" or "IODeviceTree:/stage71-platform-personality"
attach-provider-ordinal=0xffffffff for platform, 0 for interrupt/timer/CPU
start-provider-ordinal=0xffffffff for platform, 0 for interrupt/timer/CPU
attach-order=0..3
start-order=0..3
provider-start-required=0 for platform, 1 for interrupt/timer/CPU
attach-start-provenance="stage71-registry-topology"
```

The rejected catalog personality is extended with negative attach/start facts:

```text
/stage71-rejected-personality

provider-plane-published=0
registry-entry-dryrun=0
property-inheritance-dryrun=1
registry-entry-ordinal=0xffffffff
registry-entry-attached=0
registry-topology-dryrun=0
registry-parent-path="IODeviceTree:/unlinked"
registry-entry-path="IODeviceTree:/stage71-rejected-personality"
registry-parent-ordinal=0xffffffff
registry-sibling-order=0xffffffff
registry-topology-provenance="stage71-rejected-unlinked"
registry-entry-linked=0
attach-start-readiness-dryrun=0
attach-readiness=0
start-readiness=0
attach-runtime-exec=0
start-runtime-exec=0
attach-provider-path="IODeviceTree:/unlinked"
start-provider-path="IODeviceTree:/unlinked"
attach-provider-ordinal=0xffffffff
start-provider-ordinal=0xffffffff
attach-order=0xffffffff
start-order=0xffffffff
provider-start-required=0
attach-start-provenance="stage71-rejected-unattached"
rejected-candidate=1
stage-owned-local-only=1
```

The attach/start readiness dry-run proves:

- The Stage70-renumbered registry-plane / IODeviceTree topology dry-run is OK.
- The Stage69-renumbered property-inheritance / registry-entry dry-run is OK.
- The Stage68-renumbered catalog/property dry-run is OK.
- The Stage67-renumbered provider-plane dry-run is OK.
- The Stage66-renumbered IOKit broader registry/service dry-run is OK.
- The Stage65-renumbered IOKit single-match dry-run is OK.
- The Stage64-renumbered IOKit/platform scaffold readiness is OK.
- The Stage63-renumbered pexpert hook readiness is OK.
- Public compile graph/object subset/link facts are OK.
- Public IOKit reference mask is complete (`0x00000007`).
- Public IOKit runtime-blocked mask is complete (`0x00000007`).
- Public IOKit compile/link counts are zero.
- The selected platform, interrupt, timer, and CPU personalities have attach/start readiness markers.
- The selected platform, interrupt, timer, and CPU personalities have deterministic provider paths, provider ordinals, attach/start order, and provenance strings.
- Runtime attach/start execution remains explicitly blocked.
- Provider-start dependency facts are complete for interrupt, timer, and CPU, while the platform root requires no provider start.
- The selected attach/start entry mask is complete (`0x0000000f`).
- The rejected personality remains unattached/unstarted/unlinked with mask `0x00000010`.
- Exactly five personality candidates are considered, four selected attach/start readiness dry-runs are produced, and one rejected/unattached candidate is recorded.
- Public pexpert/platform runtime, public IOKit runtime, IOKit match/registry/service/provider-plane/catalog/property-plane/property-inheritance/registry-entry/registry-topology/attach/start runtime, Stage-owned platform-driver runtime, public VM/pmap runtime, generated Mach-O execution, XNU entry execution, proposed pmap writes, live pmap table installs, pmap control-register writes, TLB invalidation, cache changes, and persistent writes remain disabled.

Expected selected/rejected masks are:

```text
attach_readiness_mask=0x0000000f
start_readiness_mask=0x0000000f
attach_provider_path_match_mask=0x0000000f
start_provider_path_match_mask=0x0000000f
attach_provider_ordinal_match_mask=0x0000000f
start_provider_ordinal_match_mask=0x0000000f
attach_order_match_mask=0x0000000f
start_order_match_mask=0x0000000f
provider_start_required_mask=0x0000000e
provider_start_satisfied_mask=0x0000000e
attach_runtime_blocked_mask=0x0000000f
start_runtime_blocked_mask=0x0000000f
attach_start_provenance_match_mask=0x0000000f
selected_attach_start_entry_mask=0x0000000f
rejected_attach_start_mask=0x00000010
attach_start_matrix=0x0000001f
```

Because the 32-bit loader satisfied mask was already fully consumed by Stage62, Stage71 does not add a 33rd top-level loader bit. Instead, it records the attach/start readiness dry-run as a contract roll-up copied into loader preflight. Final loader success still requires and reaches:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x71000001
```

## Local validation

Build:

```bash
/mnt/data/mi4-ios6/stage71/build.sh
```

Final build size:

```text
text=347004 data=0 bss=430028 dec=777032 hex=bdb48
```

Hashes:

```text
4c884c03d4b1b304d0b663bc841333881ce1f5879dbef9c928d3330f7065722f  out/stage71/stage71_fixture.macho
e14a822f66e9e047ca4a133790ce22ccdfc93d24bec4d9bae0f2d4ad26c2ecc8  out/stage71/stage71.elf
6230da48a315018cebc1c573c742e92526fe58547563639c0c5662d2f793f0ec  out/stage71/stage71.bin
5a8b9357f2041cece915d51cbca429a9739a22f2c935e90d85051facbeeb5e29  out/stage71/stage71.img
1365ddbd1f285d7771ab0cc6e232b5e677b196c3857d0871c904b15658c8f3af  out/stage71/stage71-qcdt.img
```

Boot image parse checks confirmed:

```text
stage71.img:      page_size=2048 kernel_size=347004 (0x54b7c) dt_size=0
stage71-qcdt.img: page_size=2048 kernel_size=347004 (0x54b7c) dt_size=2521088 (0x267800)
```

Undefined symbol checks were clean for both:

```text
out/stage71/stage71.elf
out/stage71/xnu-link/stage71-xnu-link.elf
```

Static validation facts:

```text
stage71/boot_args.c command_line_len_with_nul=220 fits256=True has_attachstart=True has_pexpert_hook_ready=True
stage71/stage71_main.c command_line_len_with_nul=220 fits256=True has_attachstart=True has_pexpert_hook_ready=True
stage71/xnu_object_shims.c command_line_len_with_nul=220 fits256=True has_attachstart=True has_pexpert_hook_ready=True
build_cmdline_len=1349 fits1536=True has_attachstart=True has_no_attachstart_exec=True
root_child_count_19=True
selected_prop_count_36_nodes=4
rejected_prop_count_35_nodes=1
stage71_attach_start_expected_checksum_marker=True
stage71_source_stale_marker_violations=0
```

The fixed public ARM `CommandLine[256]` strings fit at 220 bytes including NUL, include `iokit-attachstart`, and preserve `pexpert-hook-ready`. The Android boot-image command line uses shortened attach/start markers (`xnu-iokit-attachstart`, `iokit-attachstart-local`, and `no-attachstart-exec`) and fits the legacy `mkbootimg` limit at 1349 bytes.

## Hardware validation

Hardware validation used non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage71/stage71-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage71-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage71-last_kmsg.txt
size=222337 bytes
stage71_marker_count=3044
stage71_xnu_marker_count=3006
```

Key IOKit attach/start readiness dry-run markers:

```text
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_registry_topology_dryrun_contract_status=0x71000001
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_registry_topology_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_registry_topology_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_registry_topology_dryrun_contract_checksum=0xf80001d6

MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_dryrun_contract_status=0x71000001
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_dryrun_contract_required_mask=0x7fffffff
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_dryrun_contract_checksum=0x011f8bea
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_selected_attach_start_entry_mask=0x0000000f
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_rejected_attach_start_mask=0x00000010
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_attach_start_matrix=0x0000001f
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_checksum=0xf7e3efe7
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_expected_checksum=0xf7e3efe7
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_no_attach_runtime_exec=0x00000001
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_no_start_runtime_exec=0x00000001
MI4IOS6_STAGE71_XNU loader_xnu_iokit_attach_start_readiness_dryrun_contract_status=0x71000001
MI4IOS6_STAGE71_XNU loader_xnu_iokit_attach_start_readiness_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE71_XNU loader_xnu_iokit_attach_start_readiness_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE71_XNU loader_xnu_iokit_attach_start_readiness_dryrun_contract_checksum=0x011f8bea
MI4IOS6_STAGE71_XNU loader_xnu_iokit_attach_start_readiness_dryrun_contract_status_rollup=0x71000001
```

Retained IOKit topology, property-inheritance, catalog/property, provider-plane, registry/service, match, scaffold, pexpert, and pmap transition source facts also remained OK.

Negative safety markers remained clear:

```text
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_no_attach_runtime_exec=0x00000001
MI4IOS6_STAGE71_XNU stage71_xnu_iokit_attach_start_readiness_no_start_runtime_exec=0x00000001
MI4IOS6_STAGE71_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE71_XNU loader_status=0x71000001
MI4IOS6_STAGE71_XNU kernel_entry ok
MI4IOS6_STAGE71 kernel_entry returned success
```

No target-side failure markers were observed for `kernel_entry returned failure`, `loader preflight failed`, `mmu high bootstrap selftest failed`, or `apple_dt walk length mismatch` in the successful run.

## Result

Stage71 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned IOKit attach/start readiness dry-run over the retained registry-plane / IODeviceTree topology dry-run, retained property-inheritance / registry-entry dry-run, retained catalog/property/personality dry-run, retained provider-plane dry-run, retained IOKit/platform scaffold, retained local service/driver match dry-run, retained broader registry/service dry-run, Stage-owned Apple-DT provider/service/candidate/catalog/personality/topology nodes, MSM8974 platform/interrupt/timer/CPU service facts, public IOKit reference-only classification, and retained pexpert/pmap transition safety facts.

The next safe direction is a still-local deeper IOKit lifecycle proof, a deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, IOKit match/registry/service/provider-plane/catalog/property-plane/property-inheritance/registry-entry/registry-topology/attach/start runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
