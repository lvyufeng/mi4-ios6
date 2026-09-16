# Experiment 78 — Stage75 XNU IOKit client-open / provider-claim / close-readiness dry-run contract

Stage75 keeps the hardware-validated Stage74 IOKit provider-callback / client-notification readiness dry-run proof stable and adds a Stage-owned **IOService client-open / provider-claim / close-readiness dry-run contract** for Xiaomi Mi 4 `cancro`.

The goal is still not to run XNU, not to execute IOKit, not to execute `IOService::open`, `IOService::close`, `handleOpen`, `handleClose`, provider-claim/arbitration code, client-close/release code, callbacks, notifications, `IORegistryEntry`, `IODeviceTreeSupport`, attach/start, register-service, provider-plane, catalog, property-plane, property-inheritance, registry-entry, registry-topology, or platform-driver runtime code, and not to execute public ARM pexpert/platform or VM/pmap runtime paths. Stage75 deepens Stage74's local provider-callback / client-notification facts into deterministic local client-open, provider-claim, client-close, dependency, ordering, and provenance facts over Stage-owned Apple-DT data only.

## Safety model

Stage75 preserves the no-runtime safety envelope:

- Non-persistent `fastboot boot` only for hardware validation.
- No flash, erase, partition write, bootloader change, or persistent storage write.
- No full public `mach_kernel` build.
- No public-XNU object execution.
- No public ARM pexpert/platform runtime execution.
- No public ARM VM/pmap runtime execution.
- No public IOKit runtime execution.
- No IOKit match, registry/service, provider-plane, catalog, property-plane, property-inheritance, registry-entry, registry-topology, attach/start, register-service, notification, interest, delivery, callback, client-notification, open, claim, close, or platform-driver runtime execution.
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

The retained Stage-owned TTBR0 round-trip and recovery-table paths are still only earlier controlled local safety tests. Stage75 does not install proposed XNU/pmap tables.

## Implementation

Stage75 retains the Stage64-through-Stage74 IOKit prerequisite contracts after renumbering to Stage75:

```text
stage75/xnu_iokit_platform_scaffold_contract.c
stage75/xnu_iokit_match_dryrun_contract.c
stage75/xnu_iokit_registry_service_dryrun_contract.c
stage75/xnu_iokit_provider_plane_dryrun_contract.c
stage75/xnu_iokit_catalog_property_dryrun_contract.c
stage75/xnu_iokit_property_inheritance_dryrun_contract.c
stage75/xnu_iokit_registry_topology_dryrun_contract.c
stage75/xnu_iokit_attach_start_readiness_dryrun_contract.c
stage75/xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.c
stage75/xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.c
stage75/xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.c
```

It then adds the client-open / provider-claim / close-readiness dry-run contract:

```text
stage75/xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract.c
struct stage75_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract
```

The new contract imports source facts from the Stage75 provider-callback/client-notification contract, all retained IOKit prerequisite contracts, the Stage75 pexpert/pmap/public-XNU proof chain, the loader safety mask, Apple-DT semantic readiness, public IOKit reference-only graph facts, and pmap/no-runtime/no-persistent-write safety counters.

## Apple-DT facts

Stage75 does not add new root nodes. The root child count remains 19.

It extends the four selected personality nodes from 83 to 100 properties with local client-open / provider-claim / client-close facts:

```text
client-open-dryrun=1
client-open-ready=1
provider-claim-ready=1
client-close-ready=1
open-runtime-exec=0
claim-runtime-exec=0
close-runtime-exec=0
open-provider-path="IODeviceTree:/" or "IODeviceTree:/stage75-platform-personality"
open-provider-ordinal=0xffffffff for platform, 0 for interrupt/timer/CPU
open-type-mask=<platform/interrupt/timer/CPU service bit>
expected-open-mask=<same per-service bit>
claimed-open-mask=<same per-service bit>
client-close-mask=<same per-service bit>
open-order=0..3
close-order=0..3
open-dependency-ready=1
open-provenance="stage75-provider-callback"
```

The rejected personality remains unpublished, unlinked, unattached, unstarted, unregistered, non-notifying, no-interest, undelivered, no-callback, no-client-notification, no-open, no-claim, and no-close with 99 properties:

```text
client-open-dryrun=0
client-open-ready=0
provider-claim-ready=0
client-close-ready=0
open-runtime-exec=0
claim-runtime-exec=0
close-runtime-exec=0
open-provider-path="IODeviceTree:/unlinked"
open-provider-ordinal=0xffffffff
open-type-mask=0
expected-open-mask=0
claimed-open-mask=0
client-close-mask=0
open-order=0xffffffff
close-order=0xffffffff
open-dependency-ready=0
open-provenance="stage75-rejected-noopen"
```

All added Apple-DT property names are 31 visible characters or shorter.

## Contract facts

The client-open / provider-claim / close-readiness contract validates the selected matrix for platform, interrupt, timer, and CPU entries:

```text
client_open_ready_mask=0x0000000f
provider_claim_ready_mask=0x0000000f
client_close_ready_mask=0x0000000f
open_provider_path_match_mask=0x0000000f
open_provider_ordinal_match_mask=0x0000000f
open_type_match_mask=0x0000000f
expected_open_mask_match_mask=0x0000000f
claimed_open_mask_match_mask=0x0000000f
client_close_mask_match_mask=0x0000000f
open_dependency_ready_mask=0x0000000f
open_runtime_blocked_mask=0x0000000f
provider_claim_runtime_blocked_mask=0x0000000f
client_close_runtime_blocked_mask=0x0000000f
open_order_match_mask=0x0000000f
close_order_match_mask=0x0000000f
open_provenance_match_mask=0x0000000f
selected_client_open_entry_mask=0x0000000f
rejected_client_open_mask=0x00000010
client_open_provider_claim_close_matrix=0x0000001f
client_open_checksum=0xf4f8e8f5
expected_client_open_checksum=0xf4f8e8f5
```

Because the 32-bit loader satisfied mask was already fully consumed, Stage75 does not add a new top-level loader satisfied bit. Instead, the new contract is copied into loader preflight and logged as a roll-up. Final success still requires:

```text
loader_satisfied_mask=0xffffffff
loader_status=0x75000001
```

## Public XNU boundary

The bounded public object subset remains stable and is still the only public code compiled/linked into the host-only proof:

```text
external/xnu-upstream/pexpert/gen/device_tree.c
external/xnu-upstream/pexpert/gen/bootargs.c
external/xnu-upstream/pexpert/gen/pe_gen.c
external/xnu-4570.1.46/pexpert/arm/pe_bootargs.c
external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c
stage75/xnu_object_shims.c
```

Public ARM VM/pmap files remain reference-only, and public IOKit files remain reference-only:

```text
external/xnu-4570.1.46/iokit/Kernel/IODeviceTreeSupport.cpp
external/xnu-4570.1.46/iokit/Kernel/IOPlatformExpert.cpp
external/xnu-4570.1.46/iokit/Kernel/IOCPU.cpp
```

Local graph/object/link validation reported:

```text
stage75_xnu_compile_graph_status=0x75000001
stage75_xnu_compile_graph_satisfied_mask=0x7fffffff
stage75_xnu_compile_graph_failure_mask=0x00000000
stage75_xnu_compile_graph_candidate_count=0x00000017
stage75_xnu_compile_graph_allowed_compile_count=0x00000005
stage75_xnu_compile_graph_allowed_link_count=0x00000005
stage75_xnu_compile_graph_iokit_public_compile_count=0x00000000
stage75_xnu_compile_graph_iokit_public_link_count=0x00000000
stage75_xnu_object_subset_status=0x75000001
stage75_xnu_object_count=0x00000006
stage75_xnu_link_status=0x75000001
stage75_xnu_link_undefined_symbol_count=0x00000000
```

## Local validation

Build:

```bash
/mnt/data/mi4-ios6/stage75/build.sh
```

Final build size:

```text
text=417064 data=0 bss=448768 dec=865832 hex=d3628
```

Hashes:

```text
25afafc6b936bb543d04c880692dd7814fd20320064c0a26487253e9c437fa4e  out/stage75/stage75_fixture.macho
6808811ab25bd79afea6ae167f6232b1bddb0b80ecaf0fce445315be6c5d1877  out/stage75/stage75.elf
f3d983d3d060fb041d1a99342e4f462b99d5665b29f5f6b45b01a58840c237c8  out/stage75/stage75.bin
d981ad498767d22dc466eb8f2248c14da4c94bcd2e32b4529fea887807a71eb8  out/stage75/stage75.img
dcbc6fb89fd6277946a4c4e635bf0f7527a8d3dbbd7216fb580a2eda84434014  out/stage75/stage75-qcdt.img
```

Boot image parse checks confirmed:

```text
stage75.img: page_size=2048 kernel_size=417064 (0x65d28) dt_size=0
stage75-qcdt.img: page_size=2048 kernel_size=417064 (0x65d28) dt_size=2521088 (0x267800)
part=kernel offset=0x800 size=417064 sha256=f3d983d3d060fb041d1a99342e4f462b99d5665b29f5f6b45b01a58840c237c8
part=dt.img offset=0x66800 size=2521088 sha256=c8faf487909b668fcebddfb2dcbd793bfb94d0411286c1f5c588bc0015120efe
```

Undefined symbol checks were clean for both:

```text
out/stage75/stage75.elf: undefined_symbol_count=0
out/stage75/xnu-link/stage75-xnu-link.elf: undefined_symbol_count=0
```

Static checks confirmed:

```text
CommandLine[256] bytes including NUL: 222
Android boot-image cmdline bytes including NUL: 1496
selected personality declarations: 4 nodes at 100 properties
rejected personality declarations: 1 node at 99 properties
Apple-DT root child count: 19
long_property_names=[]
stale_stage74_runtime_markers=[]
old 83/82 exact gates=[]
external/xnu-upstream HEAD=cc8a9b0ce
external/xnu-4570.1.46 HEAD=76e12aa
out/stage75 artifacts ignored
git diff --check clean
```

One local/hardware fix was required before final validation: the new client-open contract originally treated the inherited `no_live_pmap_tables_installed` marker as `1`, while the validated Stage74-style pmap boundary records safe/no-live-install as `0`. The final Stage75 contract aligns with the retained provider-callback boundary (`no_live_pmap_tables_installed == 0`, no proposed pmap writes, no TLB/cache change, and `source_pmap_transition_status=0x75000001`).

## Hardware validation

Hardware validation used only non-persistent boot of the QCDT image; no flash, erase, partition write, or persistent storage write was performed:

```bash
sudo adb -s 4a2fe00b reboot bootloader
sudo fastboot -s 4a2fe00b boot /mnt/data/mi4-ios6/out/stage75/stage75-qcdt.img
sudo adb -s 4a2fe00b wait-for-device
sudo adb -s 4a2fe00b exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage75-last_kmsg.txt
```

The recovered `/proc/last_kmsg` was 248164 bytes and confirmed the Stage75 client-open / provider-claim / close-readiness contract and loader roll-up:

```text
stage75_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_status=0x75000001
stage75_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_satisfied_mask=0x7fffffff
stage75_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_failure_mask=0x00000000
stage75_xnu_iokit_client_open_selected_client_open_entry_mask=0x0000000f
stage75_xnu_iokit_client_open_rejected_client_open_mask=0x00000010
stage75_xnu_iokit_client_open_matrix=0x0000001f
stage75_xnu_iokit_client_open_checksum=0xf4f8e8f5
stage75_xnu_iokit_client_open_expected_checksum=0xf4f8e8f5
stage75_xnu_iokit_client_open_no_open_runtime_exec=0x00000001
stage75_xnu_iokit_client_open_no_claim_runtime_exec=0x00000001
stage75_xnu_iokit_client_open_no_close_runtime_exec=0x00000001
loader_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_status_rollup=0x75000001
loader_satisfied_mask=0xffffffff
loader_status=0x75000001
kernel_entry returned success
```

The device returned to Android after the Stage75 PS_HOLD reset path.
