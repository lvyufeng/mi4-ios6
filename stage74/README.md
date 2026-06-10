# Stage74 — XNU IOKit provider-callback / client-notification readiness dry-run contract

Stage74 keeps the hardware-validated Stage73 IOKit provider-notification / interest / delivery-readiness dry-run proof stable and adds a Stage-owned **IOKit provider-callback / client-notification readiness dry-run contract** for Xiaomi Mi 4 `cancro`.

The goal is still not to run XNU, not to execute IOKit, not to execute `IORegistryEntry`, `IOService`, `IODeviceTreeSupport`, attach/start, register-service, provider-notification, interest, delivery, callback, client-notification, provider-plane, catalog, property-plane, property-inheritance, registry-entry, registry-topology, or platform-driver runtime code, and not to execute public ARM pexpert/platform or VM/pmap runtime paths. Stage74 deepens Stage73's local provider-notification / interest / delivery facts into deterministic local provider-callback, client-notification, acknowledgement, dependency, ordering, and provenance facts over Stage-owned Apple-DT data only.

## Safety model

Stage74 preserves the no-runtime safety envelope:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No IOKit match, registry/service, provider-plane, catalog, property-plane, property-inheritance, registry-entry, registry-topology, attach/start, register-service, notification, interest, delivery, callback, client-notification, or platform-driver runtime execution.
- No Stage-owned MSM8974 platform-driver execution.
- No generated Mach-O fixture execution.
- No XNU `_start` / `arm_init` jump.
- No proposed physical load-address writes.
- No proposed TTE/pmap workspace writes.
- No live proposed XNU/pmap table install.
- No TTBR/TTBCR/DACR/SCTLR writes for proposed pmap install.
- No TLB invalidation for proposed pmap install.
- No cache policy change.
- No mutation of ignored `external/` public XNU checkouts.

The retained Stage-owned TTBR0 round-trip and recovery-table paths are still only earlier controlled local safety tests. Stage74 does not install proposed XNU/pmap tables.

## Public object subset

The bounded public object subset remains stable and is still the only public code compiled/linked into the host-only proof:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage74/xnu_object_shims.c
```

Public ARM VM/pmap files remain reference-only:

```text
external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c
external/xnu-4570.1.46/osfmk/arm/pmap.c
external/xnu-4570.1.46/osfmk/arm/pmap.h
external/xnu-4570.1.46/osfmk/arm/proc_reg.h
external/xnu-4570.1.46/osfmk/vm/pmap.h
external/xnu-4570.1.46/osfmk/mach/arm/vm_param.h
```

Public IOKit files remain reference-only:

```text
external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp
external/xnu-4570.1.46/iokit/Kernel/IOPlatformExpert.cpp
external/xnu-4570.1.46/iokit/Kernel/IOCPU.cpp
```

They are not compiled, not linked, and not executed.

## What Stage74 adds

Stage74 retains the Stage64-through-Stage73 IOKit prerequisite contracts after renumbering to Stage74:

```text
stage74/xnu_iokit_platform_scaffold_contract.c
stage74/xnu_iokit_match_dryrun_contract.c
stage74/xnu_iokit_registry_service_dryrun_contract.c
stage74/xnu_iokit_provider_plane_dryrun_contract.c
stage74/xnu_iokit_catalog_property_dryrun_contract.c
stage74/xnu_iokit_property_inheritance_dryrun_contract.c
stage74/xnu_iokit_registry_topology_dryrun_contract.c
stage74/xnu_iokit_attach_start_readiness_dryrun_contract.c
stage74/xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.c
stage74/xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.c
```

It then adds the provider-callback / client-notification readiness dry-run contract:

```text
stage74/xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.c
struct stage74_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract
```

The new contract imports source facts from:

- the Stage74 provider-notification / interest / delivery-readiness dry-run contract,
- the Stage74 lifecycle/register-service readiness dry-run contract,
- the Stage74 attach/start readiness dry-run contract,
- the Stage74 registry-plane / IODeviceTree topology dry-run contract,
- the Stage74 property-inheritance / registry-entry dry-run contract,
- the Stage74 catalog/property/personality dry-run contract,
- the Stage74 provider-plane dry-run contract,
- the Stage74 broader IOKit registry/service dry-run contract,
- the Stage74 IOKit/platform scaffold contract,
- the Stage74 local IOKit match dry-run contract,
- the Stage74-renumbered pexpert hook readiness contract,
- the retained Stage74 pmap transition dry-run contract,
- the public compile graph/object subset/controlled link proof,
- the loader safety mask,
- the Apple-DT semantic readiness mask,
- public IOKit reference-only graph facts,
- pmap/no-runtime/no-persistent-write safety counters.

The Apple flattened device tree keeps the Stage66/Stage67 provider/service/candidate nodes and the Stage68/Stage69/Stage70/Stage71/Stage72/Stage73 catalog/personality/registry-entry/topology/attach-start/lifecycle/provider-notification nodes:

```text
/iokit-platform-scaffold
/msm8974-platform-driver
/msm8974-interrupt-service
/msm8974-timer-service
/msm8974-cpu-service
/msm8974-rejected-driver
/iokit-catalog-property-dryrun
/stage74-platform-personality
/stage74-interrupt-personality
/stage74-timer-personality
/stage74-cpu-personality
/stage74-rejected-personality
```

Stage74 does not add new root nodes. The root child count remains 19. It extends the four selected personality nodes from 68 to 83 properties with local provider-callback / client-notification facts:

```text
prov-callback-dryrun=1
callback-ready=1
client-notify-ready=1
callback-runtime-exec=0
client-notify-runtime-exec=0
callback-provider-path="IODeviceTree:/" or "IODeviceTree:/stage74-platform-personality"
callback-provider-ordinal=0xffffffff for platform, 0 for interrupt/timer/CPU
callback-type-mask=<platform/interrupt/timer/CPU service bit>
expected-callback-mask=<same per-service bit>
delivered-callback-mask=<same per-service bit>
client-ack-mask=<same per-service bit>
callback-order=0..3
client-notify-order=0..3
callback-dependency-ready=1
callback-provenance="stage74-provider-notify"
```

The rejected personality remains unpublished, unlinked, unattached, unstarted, unregistered, non-notifying, no-interest, undelivered, no-callback, and no-client-notification with 82 properties:

```text
prov-callback-dryrun=0
callback-ready=0
client-notify-ready=0
callback-runtime-exec=0
client-notify-runtime-exec=0
callback-provider-path="IODeviceTree:/unlinked"
callback-provider-ordinal=0xffffffff
callback-type-mask=0
expected-callback-mask=0
delivered-callback-mask=0
client-ack-mask=0
callback-order=0xffffffff
client-notify-order=0xffffffff
callback-dependency-ready=0
callback-provenance="stage74-rejected-nocallback"
```

All added Apple-DT property names are 31 visible characters or shorter. The public XNU-style flattened Apple-DT property-name field is 32 bytes and the local builder stores at most 31 visible characters plus NUL.

The provider-callback / client-notification readiness contract validates the selected matrix for platform, interrupt, timer, and CPU entries:

```text
provider_callback_ready_mask=0x0000000f
client_notification_ready_mask=0x0000000f
callback_provider_path_match_mask=0x0000000f
callback_provider_ordinal_match_mask=0x0000000f
callback_type_match_mask=0x0000000f
expected_callback_mask_match_mask=0x0000000f
delivered_callback_mask_match_mask=0x0000000f
client_ack_mask_match_mask=0x0000000f
callback_dependency_ready_mask=0x0000000f
callback_runtime_blocked_mask=0x0000000f
client_notification_runtime_blocked_mask=0x0000000f
callback_order_match_mask=0x0000000f
client_notification_order_match_mask=0x0000000f
callback_provenance_match_mask=0x0000000f
selected_provider_callback_entry_mask=0x0000000f
rejected_provider_callback_mask=0x00000010
provider_callback_client_notification_matrix=0x0000001f
provider_callback_checksum=0xbba8abbd
expected_provider_callback_checksum=0xbba8abbd
```

Because the 32-bit loader satisfied mask was already fully consumed, Stage74 does not add a new top-level loader satisfied bit. Instead, the provider-callback / client-notification readiness dry-run contract is copied into loader preflight and logged as a roll-up. Final success still requires:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x74000001
```

## Selected public XNU baselines

Stage74 expects:

```text
external/xnu-upstream      xnu-2050.22.13  cc8a9b0c  MasterVersion 12.3.0
external/xnu-4570.1.46     xnu-4570.1.46   76e12aa   later public ARM/IOKit reference only
```

The public 2050 tree remains the Darwin 12 / iOS 6-era context. The 4570 tree is used only as a public ARM implementation reference for bounded pexpert proof objects, public VM/pmap reference-only classification, and public IOKit reference-only classification.

## Compile graph, object subset, and link proof

`stage74/xnu_compile_graph_scan.py` classifies twenty-three public candidates. It allows only the five bounded pexpert sources in the public object subset, records three public IOKit files as reference-only, and blocks public IOKit runtime execution.

Local graph/object/link validation reported:

```text
stage74_xnu_compile_graph_status=0x74000001
stage74_xnu_compile_graph_required_mask=0x7fffffff
stage74_xnu_compile_graph_satisfied_mask=0x7fffffff
stage74_xnu_compile_graph_failure_mask=0x00000000
stage74_xnu_compile_graph_candidate_count=0x00000017
stage74_xnu_compile_graph_allowed_compile_count=0x00000005
stage74_xnu_compile_graph_allowed_link_count=0x00000005
stage74_xnu_compile_graph_iokit_reference_mask=0x00000007
stage74_xnu_compile_graph_iokit_runtime_blocked_mask=0x00000007
stage74_xnu_compile_graph_iokit_public_compile_count=0x00000000
stage74_xnu_compile_graph_iokit_public_link_count=0x00000000
stage74_xnu_object_subset_status=0x74000001
stage74_xnu_object_count=0x00000006
stage74_xnu_link_status=0x74000001
stage74_xnu_link_undefined_symbol_count=0x00000000
```

## Local validation

Build:

```bash
/mnt/data/mi4-ios6/stage74/build.sh
```

Final build size:

```text
text=397032 data=0 bss=448136 dec=845168 hex=ce570
```

Hashes:

```text
ca081714135cfdeea65abb4ffa3fd60cab6444192451b5de9dcbac95623ed3ae  out/stage74/stage74_fixture.macho
43a4c50a83a8dd214f0da77e2f2efe0f1cda4f2555f1397634615439cef13cc5  out/stage74/stage74.elf
0da8550cea9dab6d9a197c47774ba150e31ffbc52818f339776e3c82f875a9ba  out/stage74/stage74.bin
0b1271c8e515c03d20aaf95b543ad93ba9a9b3746aeed33b98b323372552ab07  out/stage74/stage74.img
b23a3548f6a8384b8f3407200d8fa64afad4ba94cbceaa7108b8bfc43458d4a9  out/stage74/stage74-qcdt.img
```

Boot image parse checks confirmed:

```text
stage74.img: page_size=2048 kernel_size=397032 (0x60ee8) dt_size=0
stage74-qcdt.img: page_size=2048 kernel_size=397032 (0x60ee8) dt_size=2521088 (0x267800)
part=kernel offset=0x800 size=397032 sha256=0da8550cea9dab6d9a197c47774ba150e31ffbc52818f339776e3c82f875a9ba
part=dt.img offset=0x61800 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Undefined symbol checks were clean for both:

```text
out/stage74/stage74.elf: undefined_symbol_count=0
out/stage74/xnu-link/stage74-xnu-link.elf: undefined_symbol_count=0
```

Static checks confirmed:

```text
CommandLine[256] bytes including NUL: 252
Android boot-image cmdline bytes including NUL: 1514
selected personality declarations: 4 nodes at 83 properties
rejected personality declarations: 1 node at 82 properties
long_property_names=[]
stale_stage73_runtime_markers=[]
old 68/67 exact gates=[]
external/xnu-upstream HEAD=cc8a9b0ce
external/xnu-4570.1.46 HEAD=76e12aa
```

## Hardware validation

Hardware validation used only non-persistent boot of the QCDT image; no flash, erase, partition write, or persistent storage write was performed:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage74/stage74-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage74-last_kmsg.txt
```

The recovered `/proc/last_kmsg` was 242219 bytes and confirmed the Stage74 provider-callback / client-notification contract and loader roll-up:

```text
stage74_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_status=0x74000001
stage74_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_satisfied_mask=0x7fffffff
stage74_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_failure_mask=0x00000000
stage74_xnu_iokit_provider_callback_selected_provider_callback_entry_mask=0x0000000f
stage74_xnu_iokit_provider_callback_rejected_provider_callback_mask=0x00000010
stage74_xnu_iokit_provider_callback_matrix=0x0000001f
stage74_xnu_iokit_provider_callback_checksum=0xbba8abbd
stage74_xnu_iokit_provider_callback_expected_checksum=0xbba8abbd
stage74_xnu_iokit_provider_callback_no_callback_runtime_exec=0x00000001
stage74_xnu_iokit_provider_callback_no_client_notification_runtime_exec=0x00000001
loader_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_status_rollup=0x74000001
loader_satisfied_mask=0xffffffff
loader_status=0x74000001
kernel_entry returned success
```

The device returned to Android after the Stage74 PS_HOLD reset path.
