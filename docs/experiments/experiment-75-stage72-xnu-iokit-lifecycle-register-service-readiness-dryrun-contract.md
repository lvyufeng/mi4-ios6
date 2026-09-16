# Experiment 75 — Stage72 XNU IOKit lifecycle/register-service readiness dry-run contract

## Goal

Stage72 advances the Xiaomi Mi 4 `cancro` XNU/Darwin bring-up from the Stage71 local IOKit attach/start readiness dry-run toward a local lifecycle/register-service proof.

The goal is not to run XNU, not to execute IOKit, not to execute `IORegistryEntry`, `IOService`, `IODeviceTreeSupport`, registry-topology, property-inheritance, registry-entry, attach/start, register-service, notification, provider-plane, catalog, property-plane, or platform-driver runtime code, and not to install live XNU/pmap tables. The goal is a Stage-owned, fail-closed contract proving that the four selected Stage71 attach/start-ready entries can be projected into deterministic local lifecycle, register-service, notification, ordering, and dependency facts for platform, interrupt, timer, and CPU personalities while one explicit rejected personality remains unpublished, unlinked, unattached, unstarted, unregistered, and non-notifying.

## Safety boundary

Stage72 preserves all Stage71 boundaries and extends them to register-service and notification runtime:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No IOKit match, registry/service, provider-plane, catalog, property-plane, property-inheritance, registry-entry, registry-topology, attach/start, register-service, notification, or platform-driver runtime execution.
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
stage72/xnu_object_shims.c
```

Public ARM VM/pmap files and public IOKit files remain reference-only. The public IOKit reference-only set is:

```text
external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp
external/xnu-4570.1.46/iokit/Kernel/IOPlatformExpert.cpp
external/xnu-4570.1.46/iokit/Kernel/IOCPU.cpp
```

They are not compiled, linked, or executed.

## Implementation

Stage72 adds:

```text
stage72/xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.c
struct stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract
```

The new contract imports source facts from:

- the Stage72 IOKit attach/start readiness dry-run contract,
- the Stage72 IOKit registry-plane / IODeviceTree topology dry-run contract,
- the Stage72 IOKit property-inheritance / registry-entry dry-run contract,
- the Stage72 IOKit catalog/property/personality dry-run contract,
- the Stage72 IOKit provider-plane dry-run contract,
- the Stage72 broader IOKit registry/service dry-run contract,
- the Stage72 IOKit/platform scaffold contract,
- the Stage72 single-match IOKit dry-run contract,
- the Stage72 pexpert hook readiness contract,
- the Stage72 public compile graph/object subset/controlled link proof,
- the retained pmap transition dry-run contract,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

Stage72 keeps the Stage71 root-node set and adds no new root children. The Stage-owned Apple flattened device tree root remains at 19 children. The four selected personality nodes are extended from 36 to 53 properties with local lifecycle/register-service readiness properties:

```text
/stage72-platform-personality
/stage72-interrupt-personality
/stage72-timer-personality
/stage72-cpu-personality

lifecycle-regsvc-dryrun=1
lifecycle-state-matched=1
lifecycle-provider-published=1
lifecycle-registry-linked=1
lifecycle-topology-linked=1
register-service-readiness=1
service-registered=1
register-service-runtime-exec=0
notification-ready=1
notification-runtime-exec=0
register-service-order=0..3
notification-order=0..3
register-provider-path="IODeviceTree:/" or "IODeviceTree:/stage72-platform-personality"
register-provider-ordinal=0xffffffff for platform, 0 for interrupt/timer/CPU
register-provider-start-req=0 for platform, 1 for interrupt/timer/CPU
register-dependency-ready=1
lifecycle-provenance="stage72-attach-start-readiness"
```

The rejected catalog personality is extended from 35 to 52 properties with negative lifecycle/register-service facts:

```text
/stage72-rejected-personality

lifecycle-regsvc-dryrun=0
lifecycle-state-matched=0
lifecycle-provider-published=0
lifecycle-registry-linked=0
lifecycle-topology-linked=0
register-service-readiness=0
service-registered=0
register-service-runtime-exec=0
notification-ready=0
notification-runtime-exec=0
register-provider-path="IODeviceTree:/unlinked"
register-provider-ordinal=0xffffffff
register-provider-start-req=0
register-service-order=0xffffffff
notification-order=0xffffffff
register-dependency-ready=0
lifecycle-provenance="stage72-rejected-unregistered"
rejected-candidate=1
stage-owned-local-only=1
```

The property names `lifecycle-regsvc-dryrun` and `register-provider-start-req` are deliberately compact. The public XNU-style flattened Apple-DT property-name field is 32 bytes and Stage72 copies at most 31 visible characters plus NUL. The originally planned longer names (`lifecycle-register-service-dryrun` and `register-provider-start-required`) would be truncated and fail exact property lookup.

The lifecycle/register-service readiness dry-run proves:

- The Stage71-renumbered attach/start readiness dry-run is OK.
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
- The selected platform, interrupt, timer, and CPU personalities have deterministic matched/provider-published/registry-linked/topology-linked lifecycle markers.
- The selected platform, interrupt, timer, and CPU personalities have register-service readiness, service-registered, notification-ready, order, provider-dependency, and provenance facts.
- Runtime register-service and notification execution remain explicitly blocked.
- Provider-start dependency facts are complete for interrupt, timer, and CPU, while the platform root requires no provider start.
- The selected lifecycle entry mask is complete (`0x0000000f`).
- The rejected personality remains unregistered/non-notifying with mask `0x00000010`.
- Exactly five personality candidates are considered, four selected lifecycle/register-service dry-runs are produced, and one rejected/unregistered candidate is recorded.
- Public pexpert/platform runtime, public IOKit runtime, IOKit match/registry/service/provider-plane/catalog/property-plane/property-inheritance/registry-entry/registry-topology/attach/start/register-service/notification runtime, Stage-owned platform-driver runtime, public VM/pmap runtime, generated Mach-O execution, XNU entry execution, proposed pmap writes, live pmap table installs, pmap control-register writes, TLB invalidation, cache changes, and persistent writes remain disabled.

Expected selected/rejected masks are:

```text
matched_mask=0x0000000f
provider_published_mask=0x0000000f
registry_linked_mask=0x0000000f
topology_linked_mask=0x0000000f
attach_readiness_mask=0x0000000f
start_readiness_mask=0x0000000f
register_service_readiness_mask=0x0000000f
service_registered_mask=0x0000000f
notification_ready_mask=0x0000000f
dependency_ready_mask=0x0000000f
lifecycle_provenance_match_mask=0x0000000f
register_runtime_blocked_mask=0x0000000f
notification_runtime_blocked_mask=0x0000000f
register_order_match_mask=0x0000000f
notification_order_match_mask=0x0000000f
provider_dependency_match_mask=0x0000000f
register_provider_start_required_mask=0x0000000e
register_provider_start_satisfied_mask=0x0000000e
selected_lifecycle_entry_mask=0x0000000f
rejected_lifecycle_mask=0x00000010
lifecycle_matrix=0x0000001f
```

Because the 32-bit loader satisfied mask was already fully consumed by Stage62, Stage72 does not add a 33rd top-level loader bit. Instead, it records the lifecycle/register-service readiness dry-run as a contract roll-up copied into loader preflight. Final loader success still requires and reaches:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x72000001
```

## Local validation

Build:

```bash
/mnt/data/mi4-ios6/stage72/build.sh
```

Final build size:

```text
text=365056 data=0 bss=446936 dec=811992 hex=c63d8
```

Hashes:

```text
ffeacea62fd9df7e5ea1e9b9604efe71c6876021515f8306f10b7856adf6298d  out/stage72/stage72_fixture.macho
49c4693e8b7e118b8cb392b81c95975cadd40691839a3ec96585f9f14d2df613  out/stage72/stage72.elf
a4e8ef684c7515578b067f6575ed591626a854f7e6d1d99761c7e9d1724431b0  out/stage72/stage72.bin
2981f434e870f6d008802c63bb1a5c78896c44a269774ed2b0e6be646a91ffb1  out/stage72/stage72.img
c9e72d0156cd37e22e5e7ebd7fb13e39309a930266f56903d4930480ab58c770  out/stage72/stage72-qcdt.img
```

Boot image parse checks confirmed:

```text
stage72-qcdt.img: page_size=2048 kernel_size=365056 (0x59200) dt_size=2521088 (0x267800)
part=kernel offset=0x800 size=365056 sha256=a4e8ef684c7515578b067f6575ed591626a854f7e6d1d99761c7e9d1724431b0
part=dt.img offset=0x5a000 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Undefined symbol checks were clean for both:

```text
out/stage72/stage72.elf
out/stage72/xnu-link/stage72-xnu-link.elf
```

Static validation facts:

```text
stage72/boot_args.c command_line_len_with_nul=231 fits256=True has_lfrs=True has_pexpert_hook_ready=True
stage72/stage72_main.c command_line_len_with_nul=231 fits256=True has_lfrs=True has_pexpert_hook_ready=True
stage72/xnu_object_shims.c command_line_len_with_nul=231 fits256=True has_lfrs=True has_pexpert_hook_ready=True
build_cmdline_1_len=1442 fits1536=True has_lifecycle=True has_no_register=True has_no_notify=True
build_cmdline_2_len=1442 fits1536=True has_lifecycle=True has_no_register=True has_no_notify=True
root_child_count_19=True
selected_prop_count_53_nodes=4
rejected_prop_count_52_nodes=1
dt_buffer_32768=True
apple_dt_prop_name_long_violations=0
stage72_source_stale_marker_violations=0
```

The fixed public ARM `CommandLine[256]` strings fit at 231 bytes including NUL, include `iokit-lfrs`, and preserve `pexpert-hook-ready`. The Android boot-image command line uses compact lifecycle markers (`xnu-iokit-lifecycle`, `lifecycle-regsvc-local`, `no-lifecycle-exec`, `no-register-exec`, and `no-notify-exec`) and fits the legacy `mkbootimg` limit at 1442 bytes.

## Hardware validation

Hardware validation used non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage72/stage72-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage72-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage72-last_kmsg.txt
size=229447 bytes
stage72_marker_count=3112
stage72_xnu_marker_count=3074
```

Key IOKit lifecycle/register-service readiness dry-run markers:

```text
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_attach_start_readiness_dryrun_contract_status=0x72000001
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_attach_start_readiness_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_attach_start_readiness_dryrun_contract_failure_mask=0x00000000

MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status=0x72000001
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_selected_lifecycle_entry_mask=0x0000000f
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_rejected_lifecycle_mask=0x00000010
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_lifecycle_matrix=0x0000001f
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_checksum=0xbba5bdb5
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_expected_checksum=0xbba5bdb5
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_no_register_service_runtime_exec=0x00000001
MI4IOS6_STAGE72_XNU stage72_xnu_iokit_lifecycle_register_service_no_notification_runtime_exec=0x00000001
MI4IOS6_STAGE72_XNU loader_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status_rollup=0x72000001
```

Final loader markers remained successful:

```text
MI4IOS6_STAGE72_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE72_XNU loader_status=0x72000001
MI4IOS6_STAGE72_XNU kernel_entry ok
MI4IOS6_STAGE72 kernel_entry returned success
```

Retained IOKit attach/start, topology, property-inheritance, catalog/property, provider-plane, registry/service, match, scaffold, pexpert, and pmap transition source facts also remained OK.

No target-side failure markers were observed for `kernel_entry returned failure`, `loader preflight failed`, `mmu high bootstrap selftest failed`, or `apple_dt walk length mismatch` in the successful run.

## Result

Stage72 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned IOKit lifecycle/register-service readiness dry-run over the retained attach/start readiness dry-run, registry-plane / IODeviceTree topology dry-run, property-inheritance / registry-entry dry-run, catalog/property/personality dry-run, provider-plane dry-run, IOKit/platform scaffold, local service/driver match dry-run, broader registry/service dry-run, Stage-owned Apple-DT provider/service/candidate/catalog/personality/topology/attach-start nodes, MSM8974 platform/interrupt/timer/CPU service facts, public IOKit reference-only classification, and retained pexpert/pmap transition safety facts.

The next safe direction is a still-local deeper IOKit lifecycle/provider-notification proof, a deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, IOKit match/registry/service/provider-plane/catalog/property-plane/property-inheritance/registry-entry/registry-topology/attach/start/register-service/notification runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
