# Experiment 76 — Stage73 XNU IOKit provider-notification / interest / delivery-readiness dry-run contract

## Goal

Stage73 advances the Xiaomi Mi 4 `cancro` XNU/Darwin bring-up from the Stage72 local IOKit lifecycle/register-service readiness dry-run toward a structured provider-notification / interest / delivery-readiness proof.

The goal is not to run XNU, not to execute IOKit, not to execute `IORegistryEntry`, `IOService`, `IODeviceTreeSupport`, registry-topology, property-inheritance, registry-entry, attach/start, register-service, notification, interest, delivery, callback, provider-plane, catalog, property-plane, or platform-driver runtime code, and not to install live XNU/pmap tables. The goal is a Stage-owned, fail-closed contract proving that the four selected Stage72 lifecycle/register-service-ready personalities can be projected into deterministic local provider-notification, interest-registration, expected-notification, delivered-notification, dependency, ordering, and provenance facts for platform, interrupt, timer, and CPU personalities while one explicit rejected personality remains unpublished, unlinked, unattached, unstarted, unregistered, non-notifying, no-interest, and undelivered.

## Safety boundary

Stage73 preserves all Stage72 boundaries and extends them to interest/delivery runtime:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No IOKit match, registry/service, provider-plane, catalog, property-plane, property-inheritance, registry-entry, registry-topology, attach/start, register-service, notification, interest, delivery, callback, or platform-driver runtime execution.
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
stage73/xnu_object_shims.c
```

Public ARM VM/pmap files and public IOKit files remain reference-only. The public IOKit reference-only set is:

```text
external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp
external/xnu-4570.1.46/iokit/Kernel/IOPlatformExpert.cpp
external/xnu-4570.1.46/iokit/Kernel/IOCPU.cpp
```

They are not compiled, linked, or executed.

## Implementation

Stage73 adds:

```text
stage73/xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.c
struct stage73_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract
```

The new contract imports source facts from:

- the Stage73 IOKit lifecycle/register-service readiness dry-run contract,
- the Stage73 IOKit attach/start readiness dry-run contract,
- the Stage73 IOKit registry-plane / IODeviceTree topology dry-run contract,
- the Stage73 IOKit property-inheritance / registry-entry dry-run contract,
- the Stage73 IOKit catalog/property/personality dry-run contract,
- the Stage73 IOKit provider-plane dry-run contract,
- the Stage73 broader IOKit registry/service dry-run contract,
- the Stage73 IOKit/platform scaffold contract,
- the Stage73 single-match IOKit dry-run contract,
- the Stage73 pexpert hook readiness contract,
- the Stage73 public compile graph/object subset/controlled link proof,
- the retained pmap transition dry-run contract,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

Stage73 keeps the Stage72 root-node set and adds no new root children. The Stage-owned Apple flattened device tree root remains at 19 children. The four selected personality nodes are extended from 53 to 68 properties with local provider-notification / interest / delivery-readiness properties:

```text
/stage73-platform-personality
/stage73-interrupt-personality
/stage73-timer-personality
/stage73-cpu-personality

prov-notify-dryrun=1
prov-notify-ready=1
interest-ready=1
interest-runtime-exec=0
delivery-ready=1
delivery-runtime-exec=0
interest-provider-path="IODeviceTree:/" or "IODeviceTree:/stage73-platform-personality"
interest-provider-ordinal=0xffffffff for platform, 0 for interrupt/timer/CPU
interest-type-mask=<platform/interrupt/timer/CPU service bit>
expected-notify-mask=<same per-service bit>
delivered-notify-mask=<same per-service bit>
interest-order=0..3
delivery-order=0..3
notify-dependency-ready=1
notify-provenance="stage73-lifecycle-regsvc"
```

The rejected catalog personality is extended from 52 to 67 properties with negative provider-notification / interest / delivery facts:

```text
/stage73-rejected-personality

lifecycle-regsvc-dryrun=0
service-registered=0
notification-ready=0
prov-notify-dryrun=0
prov-notify-ready=0
interest-ready=0
interest-runtime-exec=0
delivery-ready=0
delivery-runtime-exec=0
interest-provider-path="IODeviceTree:/unlinked"
interest-provider-ordinal=0xffffffff
interest-type-mask=0
expected-notify-mask=0
delivered-notify-mask=0
interest-order=0xffffffff
delivery-order=0xffffffff
notify-dependency-ready=0
notify-provenance="stage73-rejected-undelivered"
rejected-candidate=1
stage-owned-local-only=1
```

All added Apple-DT property names are deliberately compact enough for the public XNU-style flattened Apple-DT 32-byte property-name field.

The provider-notification / interest / delivery-readiness dry-run proves:

- The Stage72-renumbered lifecycle/register-service readiness dry-run is OK.
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
- The selected platform, interrupt, timer, and CPU personalities have deterministic provider-notification-ready, interest-ready, and delivery-ready markers.
- The selected platform, interrupt, timer, and CPU personalities have interest provider path/ordinal, interest type, expected notification mask, delivered notification mask, order, dependency, and provenance facts.
- Runtime interest registration and delivery/callback execution remain explicitly blocked.
- The selected provider-notification entry mask is complete (`0x0000000f`).
- The rejected personality remains un-notified/no-interest/undelivered with mask `0x00000010`.
- Exactly five personality candidates are considered, four selected provider-notification dry-runs are produced, and one rejected/un-notified candidate is recorded.
- Public pexpert/platform runtime, public IOKit runtime, IOKit match/registry/service/provider-plane/catalog/property-plane/property-inheritance/registry-entry/registry-topology/attach/start/register-service/notification/interest/delivery runtime, Stage-owned platform-driver runtime, public VM/pmap runtime, generated Mach-O execution, XNU entry execution, proposed pmap writes, live pmap table installs, pmap control-register writes, TLB invalidation, cache changes, and persistent writes remain disabled.

Expected selected/rejected masks are:

```text
provider_notification_ready_mask=0x0000000f
interest_ready_mask=0x0000000f
delivery_ready_mask=0x0000000f
interest_provider_path_match_mask=0x0000000f
interest_provider_ordinal_match_mask=0x0000000f
interest_type_match_mask=0x0000000f
expected_notify_mask_match_mask=0x0000000f
delivered_notify_mask_match_mask=0x0000000f
notify_dependency_ready_mask=0x0000000f
interest_runtime_blocked_mask=0x0000000f
delivery_runtime_blocked_mask=0x0000000f
interest_order_match_mask=0x0000000f
delivery_order_match_mask=0x0000000f
notify_provenance_match_mask=0x0000000f
selected_provider_notification_entry_mask=0x0000000f
rejected_provider_notification_mask=0x00000010
provider_notification_matrix=0x0000001f
```

Because the 32-bit loader satisfied mask was already fully consumed by Stage62, Stage73 does not add a 33rd top-level loader bit. Instead, it records the provider-notification / interest / delivery-readiness dry-run as a contract roll-up copied into loader preflight. Final loader success still requires and reaches:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x73000001
```

## Local validation

Build:

```bash
/mnt/data/mi4-ios6/stage73/build.sh
```

Final build size:

```text
text=382092 data=0 bss=447516 dec=829608 hex=ca8a8
```

Hashes:

```text
9f72526cb89f8d073a3b1cbbdc23f17537181431e10e57e3f775b99d3d41ab04  out/stage73/stage73_fixture.macho
34e7a0ae3775f43d979b4c9e70ac9d70b79e3f3c61262921e7279b2343cf13c5  out/stage73/stage73.elf
af7e0dadcf8bc925f91ddb6e86d4d5e780887f1e09f2d29889139d8260701e8e  out/stage73/stage73.bin
0e67aacf1265847d32b52a3a427d043c9368c32c182ee143b858a2237d60f3c8  out/stage73/stage73.img
bcec03b0936a1c58a8d129eeeef8a9545ad56b4bdf73e148c55baa6c7298615b  out/stage73/stage73-qcdt.img
```

Boot image parse checks confirmed:

```text
stage73-qcdt.img: page_size=2048 kernel_size=382092 (0x5d48c) dt_size=2521088 (0x267800)
part=kernel offset=0x800 size=382092 sha256=af7e0dadcf8bc925f91ddb6e86d4d5e780887f1e09f2d29889139d8260701e8e
part=dt.img offset=0x5e000 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Undefined symbol checks were clean for both:

```text
out/stage73/stage73.elf
out/stage73/xnu-link/stage73-xnu-link.elf
```

Static validation facts:

```text
stage73/boot_args.c command_line_len_with_nul=245 fits256=True has_pnotify=True has_lfrs=True
stage73/stage73_main.c command_line_len_with_nul=245 fits256=True has_pnotify=True has_lfrs=True
stage73/xnu_object_shims.c command_line_len_with_nul=245 fits256=True has_pnotify=True has_lfrs=True
out/stage73/stage73.img build_cmdline_len=1508 fits1536=True has_pnotify=True has_no_interest=True has_no_delivery=True
out/stage73/stage73-qcdt.img build_cmdline_len=1508 fits1536=True has_pnotify=True has_no_interest=True has_no_delivery=True
root_child_count_19=True
selected_prop_count_68_nodes=4
rejected_prop_count_67_nodes=1
dt_buffer_32768=True
apple_dt_prop_name_long_violations=0
stage73_source_stale_marker_violations=0
```

The fixed public ARM `CommandLine[256]` strings fit at 245 bytes including NUL, include `iokit-lfrs` and `iokit-pnotify`, and preserve `pexpert-hook-ready`. The Android boot-image command line uses compact provider-notification markers (`xnu-iokit-pnotify`, `pnotify-local`, `no-interest-exec`, and `no-delivery-exec`) and fits the legacy `mkbootimg` limit at 1508 bytes.

## Hardware validation

Hardware validation used non-persistent boot only:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage73/stage73-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage73-last_kmsg.txt
```

Recovered log:

```text
/tmp/cancro-stage73-last_kmsg.txt
size=235817 bytes
stage73_marker_count=3177
stage73_xnu_marker_count=3139
```

Key IOKit provider-notification / interest / delivery-readiness dry-run markers:

```text
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status=0x73000001
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_satisfied_mask=0x7fffffff
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_failure_mask=0x00000000
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_selected_provider_notification_entry_mask=0x0000000f
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_rejected_provider_notification_mask=0x00000010
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_matrix=0x0000001f
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_checksum=0xebebe9ff
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_expected_checksum=0xebebe9ff
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_no_interest_runtime_exec=0x00000001
MI4IOS6_STAGE73_XNU stage73_xnu_iokit_provider_notification_no_delivery_runtime_exec=0x00000001
MI4IOS6_STAGE73_XNU loader_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status_rollup=0x73000001
```

Final loader markers remained successful:

```text
MI4IOS6_STAGE73_XNU loader_satisfied_mask=0xffffffff
MI4IOS6_STAGE73_XNU loader_status=0x73000001
MI4IOS6_STAGE73_XNU kernel_entry ok
MI4IOS6_STAGE73 kernel_entry returned success
```

Retained IOKit lifecycle/register-service, attach/start, topology, property-inheritance, catalog/property, provider-plane, registry/service, match, scaffold, pexpert, and pmap transition source facts also remained OK.

No target-side failure markers were observed:

```text
kernel_entry returned failure: 0
loader preflight failed: 0
```

## Result

Stage73 is complete through local validation and non-persistent hardware validation. It proves a Stage-owned IOKit provider-notification / interest / delivery-readiness dry-run over the retained lifecycle/register-service readiness dry-run, attach/start readiness dry-run, registry-plane / IODeviceTree topology dry-run, property-inheritance / registry-entry dry-run, catalog/property/personality dry-run, provider-plane dry-run, IOKit/platform scaffold, local service/driver match dry-run, broader registry/service dry-run, Stage-owned Apple-DT provider/service/candidate/catalog/personality/topology/attach-start/lifecycle/provider-notification nodes, MSM8974 platform/interrupt/timer/CPU service facts, public IOKit reference-only classification, and retained pexpert/pmap transition safety facts.

The next safe direction is a still-local deeper IOKit provider-callback/client-notification proof, a deeper MSM8974 pexpert/platform implementation proof, broader high-virtual pmap behavior, or another bounded public-XNU proof while continuing to avoid a full public `mach_kernel` build, XNU runtime handoff, public pexpert/platform runtime execution, public IOKit runtime execution, IOKit match/registry/service/provider-plane/catalog/property-plane/property-inheritance/registry-entry/registry-topology/attach/start/register-service/notification/interest/delivery runtime execution, public VM/pmap runtime execution, live proposed table install, TLB invalidation, cache change, and persistent writes.
