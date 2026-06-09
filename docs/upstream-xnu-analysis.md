# Upstream XNU Analysis for Mi4 Cancro Bring-up

Date: 2026-06-08

## Purpose

The project direction is now explicitly toward eventually running XNU, not just adding more synthetic XNU-adjacent descriptors. This note records the public Apple OSS XNU baseline selected for the next work and compares the real XNU boot interfaces against the Stage42 boot skeleton.

Safety remains unchanged:

- No partition flash/erase/write.
- No bootloader or partition changes.
- No hardware boot is required for this analysis task.
- If hardware validation is requested later, use non-persistent `sudo fastboot boot` only.
- Use public Apple OSS only; do not rely on leaked or proprietary Apple code.
- Keep large external source checkouts under ignored `external/`; do not commit the clone.

## Upstream clone metadata

The upstream clone is:

```text
path:    external/xnu-upstream
remote:  https://github.com/apple-oss-distributions/xnu.git
```

The initial clone landed on modern `main` at `xnu-12377.1.9`, but that is not an iOS 6-era target. The checkout has therefore been switched to an iOS 6/Darwin 12-era public tag:

```text
selected ref:        xnu-2050.22.13
checkout mode:       detached HEAD
commit:              cc8a9b0ce917bb7115f5c97a78b38db871557db0
config/MasterVersion: 12.3.0
```

Rationale for this tag:

- The user correctly pointed out that analysis should be on an iOS 6-matching branch/tag.
- Public `distribution-iOS` manifests checked in this repo for iOS 6.1.3 do not list an XNU project/tag, so there is no exact public iOS 6.1.3 ARM XNU source tag to select from those manifests.
- The public XNU 2050 tag family is Darwin 12 / iOS 6-era. `xnu-2050.22.13` reports `MasterVersion` `12.3.0`, which is the closest available public Darwin 12.3/iOS 6.1.x-era baseline among the checked public XNU tags.
- `origin/rel/xnu-2050` currently points at `xnu-2050.48.11` (`MasterVersion` `12.5.0`), which is later in the Darwin 12 line and less directly matched to iOS 6.1.3 timing.

## Existing local XNU references

| Path | Role | Observed ref |
| --- | --- | --- |
| `external/xnu-upstream` | Selected upstream Apple OSS reference for this analysis; now detached at the closest public iOS 6-era tag | `xnu-2050.22.13`, `cc8a9b0c`, `MasterVersion 12.3.0` |
| `external/xnu-2050.18.24` | Earlier Darwin 12 / iOS 6-era public XNU structure reference | `xnu-2050.18.24`, `d4e188f0`, `MasterVersion 12.2.0` |
| `external/apple-xnu-rel-2050` | Public `rel/xnu-2050` branch head reference | `xnu-2050.48.11`, `4abd0e59`, `MasterVersion 12.5.0` |
| `external/xnu-4570.1.46` | Later public ARM/ARMv7 XNU reference for files missing from the public 2050 release | `xnu-4570.1.46`, `76e12aa3` |

Important source availability limit: the public `xnu-2050.*` trees available here do not include a complete iOS ARMv7 source tree. They are still useful for Darwin 12-era kernel organization, Mach-O headers, generic device-tree parsing, and IOKit concepts, but practical ARM boot details still need comparison against the later public `xnu-4570.1.46` ARM sources.

## ARM boot ABI findings

The public ARM `boot_args` shape in `pexpert/pexpert/arm/boot.h` matches the Stage42 structure at the field level for 32-bit ARM:

```text
Revision
Version
virtBase
physBase
memSize
topOfKernelData
Boot_Video
machineType
deviceTreeP
deviceTreeLength
CommandLine[256]
bootFlags
memSizeActual
```

Stage42 already mirrors this layout in `stage42/stage42.h` and populates it in `stage42/boot_args.c` with revision/version 2, a 256-byte command line, a device-tree pointer/length, and `topOfKernelData`.

However, Stage42 currently treats the fields as a safe skeleton handoff, not as the real XNU boot contract:

- `virtBase = 0` is identity-oriented. Real ARM XNU uses `virtBase` as the kernel virtual base for early `physBase`/`virtBase` address translation and pmap setup.
- `physBase = STAGE42_BASE` (`0x00008000`) describes the current boot payload address. Real XNU uses `physBase`/`gPhysBase` in pmap and memory-management math, so Stage43 must decide whether this should become the true RAM/kernel physical base, a bootloader low alias, or a staged loader-specific value.
- `topOfKernelData = __stage42_image_end` is currently a marker for the end of the tiny image. Real ARM XNU uses `topOfKernelData` as early page-table workspace and later as the boundary between kernel/boot data and available physical pages.

## start.s and arm_init entry path

The practical public ARM reference is `external/xnu-4570.1.46/osfmk/arm/start.s`.

Key early-entry facts:

- `_start` expects `r0 = boot_args *`.
- It disables IRQ/FIQ very early.
- It enables L1 I-cache and branch prediction early, before the C path.
- It reads `physBase`, `virtBase`, `memSize`, and `topOfKernelData` from `boot_args`.
- It writes `topOfKernelData` into TTBR0/TTBR1 as the translation-table base.
- It creates early mappings for the current PC and kernel virtual mapping using section/coarse entries.

The practical public ARM C path is `external/xnu-4570.1.46/osfmk/arm/arm_init.c`.

Key sequence from that file:

1. `arm_init(boot_args *args)` is the first C-level boot path.
2. It calls `PE_init_platform(FALSE, args)` early so pexpert can initialize boot args and the device tree.
3. It initializes CPU/thread/bootstrap structures and registers the timebase path.
4. It parses boot arguments such as `diag`, `maxmem`, and debug options.
5. It calls `arm_vm_init(xmaxmem, args)`.
6. It initializes printf/panic/console paths.
7. It later calls `PE_init_platform(TRUE, &BootCpuData)` to initialize interrupts/debug with VM available.
8. It calls `cpu_timebase_init(TRUE)`, FIQ context setup, then `machine_startup(args)`.

Stage42 does not yet implement this. Its `kernel_entry(struct boot_args *)` validates the skeleton ABI and then runs hardware/selftest code directly. A real XNU attempt must either jump to a real `_start` with the expected preconditions or first implement a loader probe that proves those preconditions without jumping.

## PE_init_platform and boot-arg exposure

`external/xnu-4570.1.46/pexpert/arm/pe_init.c` shows the public ARM pexpert contract:

- On first initialization, it stores:
  - `PE_state.bootArgs = boot_args_ptr`
  - `PE_state.deviceTreeHead = boot_args_ptr->deviceTreeP`
  - video fields from `boot_args->Video`
- In the pre-VM call (`vm_initialized == FALSE`) it calls:
  - `DTInit(PE_state.deviceTreeHead)`
  - `pe_identify_machine(boot_args_ptr)`
- In the post-VM call (`vm_initialized == TRUE`) it calls:
  - `pe_arm_init_interrupts(args)`
  - `pe_arm_init_debug(args)`
- IOKit later starts from `PE_state.deviceTreeHead` and `PE_state.bootArgs`.

`pexpert/gen/bootargs.c` parses boot arguments through `PE_boot_args()` and `PE_parse_boot_argn()`. Stage42 already passes a realistic command line (`debug=0x144 serial=0x1 ...`) but does not yet expose the exact full pexpert/IOKit boot-arg environment.

## Apple flattened device tree findings

The public XNU flattened device-tree ABI is described by `pexpert/pexpert/device_tree.h` and implemented in `pexpert/gen/device_tree.c`:

- A `DeviceTreeNode` has `uint32_t nProperties` and `uint32_t nChildren`.
- Each property uses a fixed 32-byte name (`DeviceTreeNodeProperty.name`) and a byte length.
- Property data is padded/aligned to 4 bytes.
- `DTInit()` just records the base pointer and then the parser walks the flattened tree in place.
- Lookup paths include `DTLookupEntry`, `DTFindEntry`, and `DTGetProperty`.

Stage42's `stage42/apple_dt.c` builder follows this flattened format closely enough for its own selftest and for the generic parser shape. The main gap is not the binary encoding; it is the semantic content required by real XNU/pexpert/IOKit.

Notable semantic gaps:

- XNU pexpert looks for an entry with `name = "device-tree"` to read `target-type` and `model`. Stage42 currently uses root `name = "/"` with `target-type` and `model` on the root node.
- Stage42 has a useful `/chosen`, `/memory`, `/cpus`, `/interrupt-controller`, and `/timer`, but real XNU will expect more platform-specific nodes/properties before IOKit can become useful.
- `/chosen` may need pexpert-visible properties such as `debug-enabled` and a boot-args/defaults layout closer to Apple devices.
- MSM8974-specific interrupt and timer nodes need a bridge to XNU's ARM pexpert hooks; simply naming the node `qcom,msm-qgic2` does not create an XNU interrupt controller implementation.

## VM/pmap bootstrap and topOfKernelData

The practical public ARM VM bootstrap reference is `external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c` plus `osfmk/arm/pmap.c`.

Important observed behavior:

- `arm_vm_init()` derives `gVirtBase` from `args->virtBase`.
- It derives `gPhysBase` from `args->physBase`.
- It uses `args->topOfKernelData` as `boot_ttep` and derives early translation-table/page-table workspace from it.
- It sets `avail_start` after the boot page-table workspace and `avail_end = gPhysBase + mem_size`.
- It discovers kernel Mach-O segments from `_mh_execute_header`, including `__TEXT`, `__DATA`, `__LINKEDIT`, `__KLD`, `__LAST`, `__PRELINK_TEXT`, and `__PRELINK_INFO`.
- It calls `pmap_bootstrap(...)`, and `pmap.c` allocates pmap metadata from `avail_start`.

This is the largest blocker between Stage42 and real XNU. Stage42's current MMU path is intentionally conservative:

- identity mappings and high aliases are 1 MiB sections,
- caches remain disabled,
- the high alias is a controlled test path,
- the descriptor chain validates pmap workspace facts but does not implement XNU pmap.

Real XNU expects a much richer early page-table contract, an address-space model based on `virtBase`/`physBase`, and cache/MMU attributes that are not equivalent to the current safe skeleton.

## Timer and interrupt hook gaps

Stage42 has strong MSM8974 hardware proofs:

- GICv2 distributor/CPU interface at `0xf9000000` / `0xf9002000`.
- SGI0 delivery and returnable IRQ path.
- ARM generic physical timer interrupt ID 19.
- 19.2 MHz timebase confirmed.

Public ARM XNU expects those facts to be wired through platform hooks:

- `PE_init_platform(TRUE, ...)` calls the ARM pexpert interrupt/debug initialization path.
- `PE_register_timebase_callback()` and `PE_call_timebase_callback()` expose `gPEClockFrequencyInfo.timebase_frequency_hz` to the kernel timebase code.
- `rtclock_early_init()` registers the timebase callback.
- `rtclock_intr()` and `timer_intr()` are the kernel timer interrupt path.
- `machine_routines.c` exposes interrupt-handler installation and interrupt-state helpers.

The next real-XNU work must provide an MSM8974 implementation for the interrupt controller and timer hooks. Stage42 proves the hardware access and interrupt mechanics, but it does not yet satisfy XNU's platform-controller abstraction.

## Mach-O / kernelcache loader clues

The 2050 public tree provides Darwin 12-era Mach-O headers, while the later public ARM tree shows what ARM VM bootstrap inspects after the kernel is loaded.

Minimum loader-facing facts for Stage43:

- `MH_EXECUTE` is `0x2`.
- `MH_PRELOAD` is `0x5`.
- `LC_SEGMENT` is `0x1` for 32-bit segment commands.
- `LC_SYMTAB` is `0x2`.
- `LC_UNIXTHREAD` is `0x5`.
- `LC_MAIN` exists but is a userland-style replacement and should not be assumed to drive XNU kernel entry.
- ARM VM bootstrap expects a valid in-memory Mach-O header at `_mh_execute_header` and segment names such as `__TEXT`, `__DATA`, `__LINKEDIT`, `__PRELINK_TEXT`, and `__PRELINK_INFO`.

Therefore Stage43 should not be another purely synthetic activation-order descriptor. It should become a Mach-O/XNU loader preflight that parses a real XNU-style Mach-O/kernel artifact enough to validate the load contract, but still avoids jumping into XNU until boot_args, DT, pmap, interrupt, and timer preconditions are understood.

## Comparison with Stage42

Stage42 already has these useful pieces:

- safe non-persistent boot via cancro `fastboot boot`,
- persistent ram_console recovery logging,
- an ARMv7 `boot_args` structure matching the public field layout,
- an Apple flattened-DT encoder/walker close to the public XNU format,
- pexpert-like `PE_state` summary and validation,
- MSM8974 GIC/timer hardware proofs,
- high virtual aliases and high-root call paths,
- a descriptor chain through kernel collection dependency resolution,
- custom abort logging and post-root SGI/timer retests.

Stage42 is still missing the parts that matter for actually running XNU:

- no real Mach-O or kernelcache loader,
- no real `_start` / `arm_init` handoff,
- no XNU-compatible `virtBase`/`physBase`/`topOfKernelData` page-table contract,
- no XNU pmap bootstrap,
- no MSM8974 XNU platform expert implementation,
- no XNU interrupt-controller driver/hook path,
- no XNU timer deadline/pending implementation,
- no IOKit driver stack for cancro hardware,
- no plan to satisfy code-signing/userspace requirements.

## Revised Stage43 recommendation

Stage43 should pivot toward a Mach-O/XNU loader probe.

Recommended Stage43 objective:

```text
Parse or stage a real XNU-style Mach-O/kernel artifact enough to validate load commands, segment layout, entry expectations, boot_args compatibility, topOfKernelData/translation-table requirements, and Apple-DT handoff assumptions. Do not jump into XNU yet.
```

Concrete Stage43 work packages:

1. Keep `external/xnu-upstream` on the selected iOS 6-era public tag `xnu-2050.22.13` for Darwin 12 context.
2. Keep `external/xnu-4570.1.46` as the public ARM implementation reference for `start.s`, `arm_init`, pexpert, VM, pmap, timer, and interrupt paths.
3. Add a Stage43 loader-preflight descriptor that records:
   - selected XNU baseline/tag metadata,
   - accepted Mach-O magic/filetype/cputype,
   - observed load-command coverage,
   - required segment names and address spans,
   - proposed `virtBase`, `physBase`, `memSize`, and `topOfKernelData`,
   - page-table workspace reservation and alignment,
   - Apple-DT semantic readiness,
   - platform hook readiness for timer/interrupts.
4. Build a small Mach-O parser/probe first; do not execute parsed entrypoints.
5. Preserve all Stage42 safety properties: identity/recovery mappings, high-alias validation, caches disabled until intentionally changed, ram_console logging, abort handlers, PS_HOLD reset, and SGI/timer retests.

A later stage can attempt a controlled transition toward an XNU-style `_start` only after the loader preflight shows the boot ABI and pmap workspace are coherent.


## Stage43 implementation note

Stage43 now implements the recommended first loader-probe step with a bounded, inert, embedded 32-bit ARM Mach-O-shaped artifact. The probe validates public Mach-O/XNU-facing facts (`MH_MAGIC`, ARMv7 CPU subtype, `MH_PRELOAD`, `LC_SEGMENT`, `LC_SYMTAB`, `LC_UNIXTHREAD`, and `__TEXT`/`__DATA`/`__LINKEDIT`) and records a proposed future-XNU `virtBase`/`physBase`/`memSize`/`topOfKernelData` tuple plus a 10-page early TTE workspace.

The implementation deliberately does not execute the parsed entry metadata, does not jump to XNU, does not switch TTBRs to the proposed tuple, and does not enable caches. Its hardware run confirms the inherited Stage42 MMU/GIC/timer safety checks still pass and the loader preflight returns status `0x43000001`. The remaining blockers are still real pmap bootstrap, MSM8974 pexpert support, XNU interrupt/timer hooks, IOKit/platform drivers, and later kernelcache/userspace details.

## Stage44 implementation note

Stage44 builds on the Stage43 loader preflight by replacing the hand-written embedded Mach-O byte array with a reproducible, non-proprietary host-generated fixture (`tools/mkmacho_fixture.py` producing `stage44/macho_fixture.c` and an ignored `out/stage44/stage44_fixture.macho`). The Stage44 parser now walks 32-bit `LC_SEGMENT` section records, reports public-XNU-relevant section/segment names including `__TEXT,__text`, `__DATA,__const`, `__PRELINK_TEXT`, `__PRELINK_INFO`, and `__PRELINK_STATE`, and records a bounded proposed physical load plan that maps each segment's VM span to a proposed physical range under `RAM_PHYS_BASE`.

The proposed future-XNU `topOfKernelData` is now derived from the load plan's physical end (`load_phys_end`) rather than the Stage43 fixed-span fallback, and `avail_start` is reported after the 10-page early TTE workspace. Stage44 still does not execute the fixture, does not jump to XNU, does not switch TTBRs, and does not enable caches. Its hardware run returns loader status `0x44000001` with the inherited Stage43 MMU/GIC/timer gates intact. The remaining blockers are unchanged: real pmap bootstrap, MSM8974 pexpert support, XNU interrupt/timer hooks, IOKit/platform drivers, and later kernelcache/userspace details.

## Stage45 implementation note

Stage45 proves the next public loader rule: `LC_SEGMENT` file bytes are copied to their loaded addresses and `vmsize - filesize` tails are zero-filled. The implementation deliberately materializes only into a Stage45-owned local BSS arena, not into the proposed XNU physical load addresses. The proposed physical load plan therefore remains dry-run metadata while the target still executes the real copy+zero-fill loop on the owned device.

To make materialized reparse meaningful, the Stage45 fixture generator emits a Stage45-compatible layout where the first `__TEXT` segment includes the Mach-O header/load commands at `fileoff=0`. Stage45 reparses the arena image, verifies marker prefixes for `ST45-TEXT`, `ST45-DATA`, and `ST45-PRELINK-TEXT`, scans all zero-fill tails, and records a public-XNU-style max loaded address (`0x80011000`). Its hardware run returns `macho_staging_status=0x45000001`, `loader_safety_mask=0x0000007f`, `loader_satisfied_mask=0x000001ff`, and loader status `0x45000001` while still not executing the fixture, not jumping to XNU, not switching TTBRs, not enabling caches, and not writing proposed physical load addresses.

The remaining blockers are still real ARM XNU pmap/bootstrap table construction, MSM8974 pexpert support, XNU interrupt/timer hooks, IOKit/platform drivers, real kernelcache/loading details, and later kernelcache/userspace policy work.

## Stage46 implementation note

Stage46 uses the concrete Stage45 load/materialization facts to model the public ARM XNU early TTE workspace contract. It preserves the proposed physical load range (`0x80000000`-`0x80009000`) and derived `topOfKernelData=0x8000c000`, then records a 10-page workspace ending at `0x80016000` with a 16 KiB L1 table reservation, a page-granular L2/coarse-table reservation, and remaining scratch space. It also records section-index coverage for the loaded kernel, low RAM, ram_console, and GIC/timer MMIO.

All TTE bytes are modeled only in `stage46_tte_dryrun_arena`, a local BSS simulation arena. Stage46 does not write the proposed physical TTE workspace, does not write TTBR0/TTBR1, does not replace the live MMU tables, does not enable caches, and does not execute the Mach-O fixture or XNU. Its hardware run returns `xnu_tte_dryrun_status=0x46000001`, `xnu_tte_satisfied_mask=0x000003ff`, `loader_safety_mask=0x000003ff`, `loader_satisfied_mask=0x000003ff`, and loader status `0x46000001`.

## Stage47 implementation note

Stage47 builds on Stage46 by verifying the local simulated ARMv7 L1 section descriptors as actual descriptor words and by walking them through a software-only identity VTOP checker. It still uses the proposed physical load range (`0x80000000`-`0x80009000`), derived `topOfKernelData=0x8000c000`, 10-page TTE workspace ending at `0x80016000`, and local BSS-only TTE arena, but it now checks the local L1 slots for low RAM, the loaded kernel, ram_console, and GIC/timer MMIO.

The Stage47 descriptor word policy remains the conservative ARMv7 short-descriptor section value `0x00010c02`, composed as `(l1_index << 20) | 0x00010c02`. Hardware validation reports `xnu_tte_descriptor_verify_mask=0x0000007f`, `xnu_tte_descriptor_failure_mask=0x00000000`, `xnu_tte_descriptor_type_mask_seen=0x00000002`, and `xnu_tte_descriptor_attr_mask_seen=0x00010c02`. The software dry-run translator reports `xnu_tte_translation_check_mask=0x0000001f` and `xnu_tte_translation_failure_mask=0x00000000` for the loaded-kernel first/last bytes, low RAM, ram_console, and GIC mappings. Stage47 also records public-XNU `boot_ttep`/availability policy facts with `xnu_tte_xnu_policy_mask=0x0000001f`.

Stage47 does not claim full XNU high-virtual mapping support. It verifies the current local identity-section table model only, and still does not write the proposed physical TTE workspace, write TTBR0/TTBR1, replace live MMU tables, enable caches, execute the Mach-O fixture, or jump to XNU. Its hardware run returns `xnu_tte_dryrun_status=0x47000001`, `xnu_tte_satisfied_mask=0x0001ffff`, `loader_safety_mask=0x00000fff`, `loader_satisfied_mask=0x00000fff`, and loader status `0x47000001`.

The remaining blockers are now more focused: real/high-virtual XNU mapping policy, safe live table materialization/switching, real pmap/bootstrap table population, exact XNU cache policy, MSM8974 pexpert support, XNU interrupt/timer hooks, IOKit/platform drivers, real kernelcache/loading details, relocation/linking/prelink details, and later code-signing/userspace policy work.

## Stage48 implementation note

Stage48 addresses the Stage47 high-virtual mapping gap without yet installing or executing any XNU table. It keeps the public ARM XNU boot tuple model (`virtBase`, `physBase`, `memSize`, `topOfKernelData`) and adds a PA-base-aware section descriptor helper so VA slot selection (`va >> 20`) is no longer conflated with the physical descriptor base (`pa & 0xfff00000`). The generated fixture still has a section-envelope mapping around `0x80000000`, so this is not yet the final page-granular XNU bootstrap map, but it proves the implementation path can represent VA slot and PA base independently.

Stage48 also materializes two local L1 tables into `stage48_safe_table_arena[]`: an identity/recovery table and a high-VA table. Hardware validation reports `xnu_tte_descriptor_verify_mask=0x000001ff`, `xnu_tte_translation_check_mask=0x0000007f`, `xnu_tte_satisfied_mask=0x001fffff`, `xnu_safe_table_status=0x48000001`, `loader_safety_mask=0x00007fff`, `loader_satisfied_mask=0x00007fff`, and loader status `0x48000001`. Safety-proof fields confirm no proposed physical load writes, no proposed workspace writes, no TTBR0/TTBR1 writes, no TTBCR/DACR/SCTLR writes, no live MMU table replacement, no TLB invalidation for table install, and no cache changes.

## Stage49 implementation note

Stage49 performs the next accelerated live-MMU proof without yet installing or executing any XNU table. It creates a separate Stage49-owned 16 KiB recovery L1 table at `0x00068000`, verifies recovery mappings for low stage code/data, active stack, VBAR/vector section, the table itself, ram_console, IMEM/restart reason, GIC/timer, and PS_HOLD, then temporarily writes TTBR0 to that Stage-owned table and invalidates TLBs. It immediately restores the original live TTBR0 (`0x0006c000`) and invalidates TLBs again.

Hardware validation reports `stage49_ttbr_roundtrip_status=0x49000001`, `stage49_ttbr_roundtrip_satisfied_mask=0x0000ffff`, `stage49_ttbr_roundtrip_failure_mask=0x00000000`, `ttbr_rt_mapping_present_mask=0x000001ff`, `ttbr_rt_switched_ttbr0=0x00068000`, `ttbr_rt_restored_ttbr0=0x0006c000`, and cache bits before/during/after all `0x00000000`. The normal round-trip path writes only TTBR0 twice and invalidates TLBs twice; `ttbr_rt_ttbcr_write_count`, `ttbr_rt_dacr_write_count`, and `ttbr_rt_sctlr_write_count` all remain zero. Safety-proof fields confirm no XNU entry execution, no generated Mach-O execution, no proposed physical load writes, no proposed workspace writes, no persistent writes, and no cache changes. Loader roll-up reports `loader_ttbr_roundtrip_status=0x49000001`, `loader_stage_owned_ttbr_status=0x49000001`, `loader_ttbr_restored_status=0x49000001`, `loader_cache_preserved_status=0x49000001`, `loader_satisfied_mask=0x0007ffff`, and loader status `0x49000001`.

This is still not a real XNU `_start`/`arm_init` handoff and not a public-XNU pmap table install. It proves that a Stage-owned recovery table can safely carry execution/logging/MMIO/timebase probes through a TTBR0 round-trip and restore original MMU state. The remaining accelerated blockers are now the public XNU build workspace / cancro target scaffold, minimal public-XNU object subset compilation, linkable minimal public-XNU Mach-O construction, sub-section/page-granular XNU mapping policy, real pmap/bootstrap table population, exact XNU cache policy, MSM8974 pexpert support, XNU interrupt/timer hooks, IOKit/platform drivers, real kernelcache/loading details, relocation/linking/prelink details, and later code-signing/userspace policy work.

## Stage50 implementation note

Stage50 completes the next accelerated blocker by creating a tracked public-XNU workspace and cancro/MSM8974 ARMv7 target scaffold without attempting a full public `mach_kernel` build and without executing public-XNU code. The read-only validator confirms `external/xnu-upstream` is still detached at public `xnu-2050.22.13` (`cc8a9b0c`, first-line `config/MasterVersion` `12.3.0`) and records the incomplete public 2050 ARM tree as an explicit known limitation. It also validates later public ARM reference files from `external/xnu-4570.1.46` for boot args, `_start`/`arm_init`, pexpert, VM/pmap, timer, and interrupt guidance.

Stage50 tracks `stage50/targets/cancro.mk` and `stage50/targets/cancro.stage51.objects`, emits ignored validation artifacts under `out/stage50/`, and publishes target-side workspace status through `struct stage50_xnu_workspace`. Hardware validation reports `stage50_xnu_workspace_status=0x50000001`, `stage50_xnu_workspace_satisfied_mask=0x0007ffff`, `stage50_xnu_workspace_failure_mask=0x00000000`, `loader_xnu_workspace_status=0x50000001`, `loader_cancro_target_status=0x50000001`, `loader_stage51_plan_status=0x50000001`, `loader_safety_mask=0x0003ffff`, `loader_satisfied_mask=0x003fffff`, and loader status `0x50000001`.

Stage50 keeps the inherited TTBR0 restore/cache proof intact (`stage50_ttbr_roundtrip_status=0x50000001`, restored TTBR0 `0x0006c000`, cache bits `0x00000000` before/during/after) and confirms no full public-XNU build, no public-XNU object execution, no generated Mach-O execution, no external checkout mutation, no proposed physical/workspace writes, no persistent writes, and no cache changes. The next blocker is now Stage51: a minimal public-only object-subset compile from the Stage50 manifest, still without linking or executing public-XNU code on hardware.

## Stage51 implementation note

Stage51 completes that blocker by compiling the first minimal public-XNU ARMv7 object subset from the selected public 2050 baseline. It consumes `external/xnu-upstream` read-only at `xnu-2050.22.13` and compiles `pexpert/gen/device_tree.c` plus `pexpert/gen/bootargs.c` with Stage51-owned shim headers/support under `stage51/shims/` and `stage51/xnu_object_shims.c`. The resulting ignored objects live under `out/stage51/xnu-objects/` and include `device_tree.o`, `bootargs.o`, and `xnu_object_shims.o`.

Hardware validation reports `stage51_xnu_object_subset_status=0x51000001`, `stage51_xnu_object_subset_satisfied_mask=0x00003fff`, `stage51_xnu_object_subset_failure_mask=0x00000000`, `stage51_xnu_object_count=0x00000003`, `loader_xnu_object_subset_status=0x51000001`, `loader_safety_mask=0x0007ffff`, `loader_satisfied_mask=0x007fffff`, and loader status `0x51000001`. Stage51 still performs no full public `mach_kernel` build, no public-XNU Mach-O link, no public-XNU object execution, no generated Mach-O execution, no external checkout mutation, no proposed physical/workspace writes, no persistent writes, and no cache changes.

## Stage52 implementation note

Stage52 completes the next blocker by turning the Stage51 compile-only object subset into a controlled linkability proof. Because the available toolchain is GNU ARM ELF-oriented rather than Apple `ld64`/Mach-O-oriented, Stage52 does not claim a native public-XNU `mach_kernel` link. It compiles the same public `xnu-2050.22.13` sources (`pexpert/gen/device_tree.c` and `pexpert/gen/bootargs.c`) plus Stage52-owned shims, then links them with Stage52-owned freestanding support objects into a closed host-only ARM ELF artifact under `out/stage52/xnu-link/stage52-xnu-link.elf`.

The proof artifact records zero undefined symbols, expected public/subset symbols such as `DTInit`, `DTLookupEntry`, `DTGetProperty`, `PE_parse_boot_argn`, `PE_get_default`, `PE_boot_args`, `kalloc`, `kfree`, `IODTGetDefault`, and `strncmp`, and deterministic layout facts (`text=0x1016`, `data=0x100`, `bss=0x1018`, compact SHA32 `0xe3dabf1f`). Stage52 embeds only metadata about this proof into the inert generated `MH_PRELOAD` fixture; the bootable payload imports generated facts through `struct stage52_xnu_link` and never executes public-XNU objects or Mach-O bytes.

Hardware validation reports `stage52_xnu_link_status=0x52000001`, `stage52_xnu_link_satisfied_mask=0x0000ffff`, `stage52_xnu_link_failure_mask=0x00000000`, `stage52_xnu_link_undefined_symbol_count=0x00000000`, `loader_xnu_link_status=0x52000001`, `loader_xnu_link_status_rollup=0x52000001`, `loader_safety_mask=0x0007ffff`, `loader_satisfied_mask=0x00ffffff`, and loader status `0x52000001`. Stage52 still performs no full public `mach_kernel` build, no public-XNU object execution, no generated Mach-O execution, no external checkout mutation, no proposed physical/workspace writes, no persistent writes, and no cache changes.

## Stage53 implementation note

Stage53 completes the formal public-XNU compile migration step from the tiny Stage52 linked subset toward a tracked compile-unit graph. It introduces `stage53/xnu_compile_graph_scan.py`, which classifies ten selected public candidates before object compilation. The graph allows the known public 2050 generic pexpert sources (`pexpert/gen/device_tree.c`, `pexpert/gen/bootargs.c`) plus one bounded-new public source (`pexpert/gen/pe_gen.c`) and keeps later public ARM bring-up files from `external/xnu-4570.1.46` reference-only or excluded-high-risk (`start.s`, `arm_init.c`, `arm_vm_init.c`, `pmap.c`).

The new bounded `pe_gen.c` dependency surface is closed only with explicit Stage53-owned compile/link support (`shims/kern/debug.h` and no-op `Debugger`, `cnputc`, `vcattach` definitions). These shims do not become a fake runtime debugger, console driver, IOKit stack, scheduler, VM, or pmap implementation. The graph-generated facts report `stage53_xnu_compile_graph_status=0x53000001`, `stage53_xnu_compile_graph_satisfied_mask=0x0003ffff`, `stage53_xnu_compile_graph_failure_mask=0x00000000`, and `stage53_xnu_compile_graph_pe_gen_allowed=0x00000001`.

Stage53 then compiles three public objects plus one Stage53-owned shim object and links them with Stage53-owned support into a closed host-only ARM ELF proof artifact under `out/stage53/xnu-link/stage53-xnu-link.elf`. The proof records zero undefined symbols, `stage53_xnu_link_object_count=0x00000004`, `stage53_xnu_link_undefined_symbol_count=0x00000000`, compact hash `0x29c6a3e2`, and layout facts for text/data/bss. Only graph/link metadata is embedded into the inert generated `MH_PRELOAD` fixture.

Hardware validation reports `stage53_xnu_compile_graph_status=0x53000001`, `stage53_xnu_object_subset_status=0x53000001`, `stage53_xnu_link_status=0x53000001`, `loader_xnu_compile_graph_status_rollup=0x53000001`, `loader_xnu_link_status_rollup=0x53000001`, `loader_safety_mask=0x000fffff`, `loader_satisfied_mask=0x01ffffff`, and loader status `0x53000001`. Stage53 still performs no full public `mach_kernel` build, no public-XNU object execution, no generated Mach-O execution, no external checkout mutation, no proposed physical/workspace writes, no persistent writes, and no cache changes.

## Stage54 implementation note

Stage54 completes the first public ARM pexpert/platform object migration proof. It keeps `external/xnu-upstream` at public `xnu-2050.22.13` for Darwin 12/iOS 6-era context and graph-gates one later-public ARM pexpert source from `external/xnu-4570.1.46`: `pexpert/arm/pe_bootargs.c`. This source provides public `PE_boot_args()` for the host-only proof. Stage54 removes the Stage-owned `PE_boot_args()` shim, supplies only Stage-owned `PE_state` ABI backing, and verifies duplicate symbol count remains zero.

The expanded graph classifies fourteen candidates. It allows `pexpert/gen/device_tree.c`, `pexpert/gen/bootargs.c`, `pexpert/gen/pe_gen.c`, and `pexpert/arm/pe_bootargs.c`; records public ARM `boot.h` / `pexpert.h` ABI references; keeps `pe_init.c`, `pe_identify_machine.c`, and `pe_consistent_debug.c` blocked-runtime/reference-only; and still excludes `start.s`, `arm_init.c`, `arm_vm_init.c`, and `pmap.c` as high-risk startup/VM/pmap sources. Host validation reports `stage54_xnu_compile_graph_status=0x54000001`, `stage54_xnu_compile_graph_satisfied_mask=0x00ffffff`, `stage54_xnu_object_count=0x00000005`, `stage54_xnu_object_public_arm_pexpert_count=0x00000001`, `stage54_xnu_object_duplicate_symbol_count=0x00000000`, `stage54_xnu_link_status=0x54000001`, `stage54_xnu_link_satisfied_mask=0x0001ffff`, `stage54_xnu_link_object_count=0x00000005`, and `stage54_xnu_link_undefined_symbol_count=0x00000000`.

Hardware validation reports `stage54_xnu_compile_graph_status=0x54000001`, `stage54_xnu_object_subset_status=0x54000001`, `stage54_xnu_link_status=0x54000001`, `loader_xnu_compile_graph_status_rollup=0x54000001`, `loader_xnu_object_subset_status_rollup=0x54000001`, `loader_xnu_link_status_rollup=0x54000001`, `loader_safety_mask=0x001fffff`, `loader_satisfied_mask=0x01ffffff`, and loader status `0x54000001`. Stage54 still performs no full public `mach_kernel` build, no public-XNU object execution, no public platform runtime execution, no generated Mach-O execution, no external checkout mutation, no proposed physical/workspace writes, no persistent writes, and no cache changes. The next blocker is Stage55: expand only the next small pexpert/platform surface while preserving the no-execution boundary.

## Stage55 implementation note

Stage55 completes the next bounded public ARM pexpert/platform proof by adding `external/xnu-4570.1.46/pexpert/arm/pe_consistent_debug.c` to the graph-approved host-only object subset. The selected Darwin 12/iOS 6-era context remains `external/xnu-upstream` at public `xnu-2050.22.13`; the later `xnu-4570.1.46` checkout remains an architecture reference only.

The Stage55 graph allows five sources: public 2050 `pexpert/gen/device_tree.c`, `pexpert/gen/bootargs.c`, `pexpert/gen/pe_gen.c`, plus later-public ARM `pexpert/arm/pe_bootargs.c` and `pexpert/arm/pe_consistent_debug.c`. It records the public consistent-debug registry layout through an ABI-reference candidate, adds Stage-owned compile-only shims for `pexpert/arm/consistent_debug.h`, `libkern/OSAtomic.h`, and `machine/machine_routines.h`, and closes the new external dependencies with Stage-owned `OSCompareAndSwap64()` and `ml_map_high_window()` support. Those shims are link-proof support only; the booted payload never calls `PE_consistent_debug_*` or any public pexpert/platform runtime path.

Local validation reports `stage55_xnu_compile_graph_status=0x55000001`, `stage55_xnu_compile_graph_satisfied_mask=0x07ffffff`, `stage55_xnu_object_count=0x00000006`, `stage55_xnu_object_public_arm_pexpert_count=0x00000002`, `stage55_xnu_object_duplicate_symbol_count=0x00000000`, `stage55_xnu_link_status=0x55000001`, `stage55_xnu_link_satisfied_mask=0x0001ffff`, `stage55_xnu_link_object_count=0x00000006`, and `stage55_xnu_link_undefined_symbol_count=0x00000000`. Hardware validation through non-persistent `fastboot boot` recovered 124047 bytes from `/proc/last_kmsg`, including `stage55_xnu_compile_graph_arm_consistent_debug_allowed=0x00000001`, `stage55_xnu_object_consistent_debug_abi_shim_ready=0x00000001`, `loader_safety_mask=0x001fffff`, `loader_satisfied_mask=0x01ffffff`, `loader_status=0x55000001`, `kernel_entry ok`, and PS_HOLD reset. Stage55 still performs no full public `mach_kernel` build, no public-XNU object execution, no public platform runtime execution, no generated Mach-O execution, no external checkout mutation, no proposed physical/workspace writes, no persistent writes, and no cache changes.

## Stage56 implementation note

Stage56 keeps the Stage55 public object/link set stable and adds a Stage-owned XNU bootstrap mapping contract instead of moving into another runtime-heavy public pexpert object. The compile graph now classifies seventeen candidates, still allows only the five bounded public sources (`device_tree.c`, `bootargs.c`, `pe_gen.c`, `pe_bootargs.c`, and `pe_consistent_debug.c`), and explicitly blocks `pe_kprintf.c`, `pe_serial.c`, `pe_identify_machine.c`, and `pe_init.c` as public ARM pexpert runtime references.

The new contract imports only Stage-owned/generated loader facts: inert Mach-O parse/load-plan, local staging/materialization, TTE dry-run, high-VA section coverage, safe-table local-only materialization, controlled TTBR0 round-trip/restore, cache-preservation, compile graph, object-subset, and controlled host-only ARM ELF link proof. It records page-granular `virtBase`, `physBase`, `topOfKernelData`, TTE workspace, `avail_start`, and `avail_end` facts, plus no-overlap and safety gates for no public-XNU execution, no platform runtime execution, no generated Mach-O execution, no proposed physical/TTE workspace writes, no persistent writes, and no cache-policy changes.

Local Stage56 validation reports `stage56_xnu_compile_graph_status=0x56000001`, `stage56_xnu_compile_graph_satisfied_mask=0x1fffffff`, `stage56_xnu_compile_graph_bootstrap_contract_selected=0x00000001`, `stage56_xnu_object_count=0x00000006`, `stage56_xnu_link_undefined_symbol_count=0x00000000`, and rebuilt `out/stage56/stage56-qcdt.img` hash `79322ef80cdcfc79df5e2911b03d5a80b4b397d6af005fe22fa1bdaae4b78319`. Hardware validation through non-persistent `fastboot boot` recovered 130283 bytes from `/proc/last_kmsg`, confirmed `stage56_xnu_bootstrap_contract_status=0x56000001`, `loader_xnu_bootstrap_contract_status_rollup=0x56000001`, `loader_status=0x56000001`, `kernel_entry ok`, all negative execution/write/cache safety markers at zero, and returned to Android.


## Stage57 implementation note

Stage57 keeps the Stage56 bootstrap mapping contract and adds a Stage-owned XNU pmap/bootstrap allocation contract instead of compiling or executing public `arm_vm_init.c` or `pmap.c`. The compile graph now classifies twenty candidates, still allows only the five bounded public pexpert sources (`device_tree.c`, `bootargs.c`, `pe_gen.c`, `pe_bootargs.c`, and `pe_consistent_debug.c`), and records public ARM `arm_vm_init.c`, `pmap.c`, `pmap.h`, `proc_reg.h`, and `vm_param.h` as pmap/VM reference-only inputs.

The new contract imports only Stage-owned/generated facts: the bootstrap mapping contract, allocator/workspace snapshot, TTE dry-run, safe-table materialization, stage-owned table proof, TTBR0 round-trip/restore, cache preservation, compile graph, object subset, and controlled host-only ARM ELF link proof. It models public ARM VM/pmap arithmetic (`gVirtBase`, `gPhysBase`, `gPhysSize`, `boot_ttep`, `cpu_ttep`, `initial_avail_start`, `avail_end`, `vstart`, and `virtual_space_end`) using Stage-owned code. Public pmap compile/link/execute counts remain zero.

Local Stage57 validation reports `stage57_xnu_compile_graph_status=0x57000001`, `stage57_xnu_compile_graph_pmap_reference_mask=0x0000001f`, `stage57_xnu_compile_graph_pmap_public_compile_count=0x00000000`, `stage57_xnu_compile_graph_pmap_public_link_count=0x00000000`, `stage57_xnu_object_count=0x00000006`, `stage57_xnu_link_undefined_symbol_count=0x00000000`, and final `out/stage57/stage57-qcdt.img` hash `8746feb4d67b99ba600590fda7a93de0bc215ee0c3ff07e7717c98e532f708fd`. Hardware validation through non-persistent `fastboot boot` recovered 138383 bytes from `/proc/last_kmsg`, confirmed `stage57_xnu_pmap_bootstrap_contract_status=0x57000001`, `stage57_xnu_pmap_bootstrap_contract_satisfied_mask=0x7fffffff`, `loader_xnu_pmap_bootstrap_contract_status_rollup=0x57000001`, `loader_status=0x57000001`, `kernel_entry ok`, all public pmap execution/write/cache safety markers at zero, and returned to Android.

During Stage57 validation, an overlong `boot_args.CommandLine` string was found to overflow the fixed 256-byte public ARM boot-args field and corrupt the adjacent Apple-DT buffer, producing an Apple-DT walk mismatch on target. Stage57 now uses a shorter command line plus a bounded copy and explicit final NUL byte for `CommandLine[255]`.

Stage57 still performs no full public `mach_kernel` build, no public-XNU object execution, no public platform runtime execution, no public VM/pmap runtime execution, no generated Mach-O execution, no XNU `_start` / `arm_init` jump, no proposed physical/workspace writes, no live pmap table install, no external checkout mutation, no persistent writes, and no cache changes.

## Stage58 implementation note

Stage58 keeps the Stage57 bootstrap mapping and pmap/bootstrap allocation contracts stable, then adds a Stage-owned XNU pmap table population dry-run contract. Instead of compiling or executing public `arm_vm_init.c` or `pmap.c`, it imports the Stage57/Stage58 pmap tuple and Stage-owned pmap/bootstrap snapshot, zeroes a local Stage-owned 16 KiB L1 simulation buffer, populates ARMv7 short-descriptor section entries with the modeled `0x00010c02` attributes, reads descriptors back, runs local software translations, and checksums the populated table.

The new table dry-run contract records `stage58_xnu_pmap_table_dryrun_contract_status=0x58000001`, required/satisfied mask `0x01ffffff`, failure mask `0x00000000`, local L1 buffer `0x00080000`-`0x00084000`, proposed pmap workspace L1 `0x00074000`/`0xc0074000`, descriptor type/attribute masks `0x00000002`/`0x00010c02`, low-memory section count `0x000005e5`, ram_console section count `0x00000002`, and four software translation checks. The local dry-run buffer is explicitly distinct from the proposed pmap workspace and the contract keeps proposed workspace write count, live pmap table install count, TTBR/TTBCR/DACR/SCTLR writes, TLB invalidations, public VM/pmap execution, generated Mach-O execution, persistent writes, and cache changes at zero.

Local Stage58 validation reports `stage58_xnu_compile_graph_status=0x58000001`, public pmap compile/link counts both zero, `stage58_xnu_object_subset_status=0x58000001`, `stage58_xnu_link_status=0x58000001`, no undefined symbols in both the boot payload and host-only XNU link proof, and final `out/stage58/stage58-qcdt.img` hash `3d35a5af065c779cac4d5cd43dd74806e95145531523c582966a07df574ea7f5`. Hardware validation through non-persistent `fastboot boot` recovered 145908 bytes from `/proc/last_kmsg`, confirmed `stage58_xnu_pmap_table_dryrun_contract_status=0x58000001`, `loader_xnu_pmap_table_dryrun_contract_status_rollup=0x58000001`, `loader_status=0x58000001`, `kernel_entry ok`, and returned to Android.

Stage58 still performs no full public `mach_kernel` build, no public-XNU object execution, no public platform runtime execution, no public VM/pmap runtime execution, no generated Mach-O execution, no XNU `_start` / `arm_init` jump, no proposed physical/TTE/pmap workspace writes, no live pmap table install, no TLB invalidation for pmap install, no external checkout mutation, no persistent writes, and no cache changes.

## Stage59 implementation note

Stage59 keeps the Stage58 section-table dry-run contract stable, then adds a Stage-owned XNU pmap page-granular dry-run contract. Instead of compiling or executing public `arm_vm_init.c` or `pmap.c`, it imports the Stage59 bootstrap/pmap prerequisites and Stage-owned pmap/bootstrap snapshot, zeroes local Stage-owned L1 and L2 buffers, creates four ARMv7 short-descriptor L1 coarse/table descriptors pointing into one local 4 KiB L2 page, populates 1024 small-page PTEs with the modeled public early PTE attribute value `0x00000412`, reads descriptors and PTEs back, runs local software translations, and checksums the populated buffers and contract.

The new page dry-run contract records `stage59_xnu_pmap_page_dryrun_contract_status=0x59000001`, required/satisfied mask `0x00ffffff`, failure mask `0x00000000`, local L1 buffer `0x00088000`-`0x0008c000`, local L2 buffer `0x00085000`-`0x00086000`, four L1 table descriptors from index `0x800` through `0x803`, 1024 L2 PTE writes over the 4 MiB window `0x80000000`-`0x80400000`, descriptor words `0x00085001` and `0x00085c01`, first/kernel/workspace/last PTE words `0x80000412`, `0x80008412`, `0x80000412`, and `0x803ff412`, and four software translation checks including `0x80008000 -> 0x80008000` and `0x803fffff -> 0x803fffff`. The local dry-run buffers are explicitly distinct from the proposed pmap workspace and the contract keeps proposed workspace write count, live pmap table install count, TTBR/TTBCR/DACR/SCTLR writes, TLB invalidations, public VM/pmap execution, generated Mach-O execution, persistent writes, and cache changes at zero.

Local Stage59 validation reports `stage59_xnu_compile_graph_status=0x59000001`, public pmap compile/link counts both zero, `stage59_xnu_object_subset_status=0x59000001`, `stage59_xnu_link_status=0x59000001`, no undefined symbols in both the boot payload and host-only XNU link proof, and final `out/stage59/stage59-qcdt.img` hash `6a79d281de5d242c3851bb06120951690ecfdff4931d7c88ffcabff6a034bc21`. Hardware validation through non-persistent `fastboot boot` recovered 153475 bytes from `/proc/last_kmsg`, confirmed `stage59_xnu_pmap_page_dryrun_contract_status=0x59000001`, `loader_xnu_pmap_page_dryrun_contract_status_rollup=0x59000001`, `loader_status=0x59000001`, `kernel_entry ok`, and returned to Android.

Stage59 still performs no full public `mach_kernel` build, no public-XNU object execution, no public platform runtime execution, no public VM/pmap runtime execution, no generated Mach-O execution, no XNU `_start` / `arm_init` jump, no proposed physical/TTE/pmap workspace writes, no live pmap table install, no TLB invalidation for pmap install, no external checkout mutation, no persistent writes, and no cache changes.

## Stage60 implementation note

Stage60 keeps the Stage59 page-granular dry-run contract stable, then adds a Stage-owned XNU pmap cache/MMU attribute dry-run contract. Instead of compiling or executing public `arm_vm_init.c` or `pmap.c`, it imports the Stage58/Stage59 section/PTE readbacks plus the Stage60 bootstrap/pmap prerequisites and models the exact public ARM XNU cache attribute indices, AP encodings, VM WIMG values, ARMv7 short-descriptor PTE/TTE attr bit placements, `wimg_to_pte()` cases, and public `arm_vm_page_granular_*` templates using Stage-owned arithmetic only.

The attr dry-run contract records `stage60_xnu_pmap_attr_dryrun_contract_status=0x60000001`, required/satisfied mask `0x00ffffff`, failure mask `0x00000000`, checksum `0xfd77fdcd`, cache indices `WRITEBACK=0`, `WRITECOMB=1`, `WRITETHRU=2`, `DISABLE=3`, `INNERWRITEBACK=4`, `POSTED=3`, `DEFAULT=0`, AP encodings `RWNA=0`, `RWRW=1`, `RONA=2`, `RORO=3`, WIMG values `DEFAULT/COPYBACK=0x02`, `INNERWBACK=0x12`, `IO=0x07`, `POSTED=0x27`, `WTHRU=0x0b`, and `WCOMB=0x06`, WIMG-derived PTE bits `0x00000400`, `0x0000000d`, `0x00000005`, `0x00000408`, and `0x00000440`, page template words `0x00000412`, `0x00000413`, `0x00000612`, and `0x00000613`, and inherited readback words `0x00010c02` and `0x00000412`. The contract keeps public pmap compile/link/execute counts, public `arm_vm_init` execution, proposed workspace writes, live pmap table installs, TTBR/TTBCR/DACR/SCTLR writes, TLB invalidations, generated Mach-O execution, persistent writes, and cache changes at zero.

Local Stage60 validation reports `stage60_xnu_compile_graph_status=0x60000001`, public pmap compile/link counts both zero, `stage60_xnu_object_subset_status=0x60000001`, `stage60_xnu_link_status=0x60000001`, no undefined symbols in both the boot payload and host-only XNU link proof, final `out/stage60/stage60-qcdt.img` hash `74cd1f3ec18d974a088cb03767200c37d0f7224dbb654a730a12f7a862e3c680`, and size `text=224396 data=0 bss=316156`. Hardware validation through non-persistent `fastboot boot` recovered 159522 bytes from `/proc/last_kmsg`, confirmed `stage60_xnu_pmap_attr_dryrun_contract_status=0x60000001`, `loader_xnu_pmap_attr_dryrun_contract_status_rollup=0x60000001`, `loader_status=0x60000001`, `kernel_entry ok`, and returned to Android.

Stage60 still performs no full public `mach_kernel` build, no public-XNU object execution, no public platform runtime execution, no public VM/pmap runtime execution, no generated Mach-O execution, no XNU `_start` / `arm_init` jump, no proposed physical/TTE/pmap workspace writes, no live pmap table install, no TLB invalidation for pmap install, no external checkout mutation, no persistent writes, and no cache changes.

## Stage61 implementation note

Stage61 keeps the Stage60 pmap cache/MMU attribute dry-run contract stable, then adds a Stage-owned XNU pmap multi-window page-granular dry-run contract. Instead of compiling or executing public `arm_vm_init.c` or `pmap.c`, it imports the Stage58/Stage59 table/page prerequisites, the exact Stage60 PTE templates, and the Stage61 bootstrap/pmap snapshot, then builds three independent local 4 MiB ARMv7 coarse/L2 windows in Stage-owned scratch buffers only.

The multi-window dry-run contract records `stage61_xnu_pmap_multiwindow_dryrun_contract_status=0x61000001`, required/satisfied mask `0x1fffffff`, failure mask `0x00000000`, checksum `0x252caa78`, a local L1 buffer `0x00098000`-`0x0009c000`, a local L2-bank buffer `0x00095000`-`0x00098000`, 12 local L1 descriptor writes, 3072 local L2 PTE writes, imported PTE templates `0x00000412`, `0x00000413`, and device-style `0x0000001f`, kernel/workspace window `0x80000000` -> `0x80000000`, RAM-console window `0xde400000` -> `0xde400000`, and device/GIC-style window `0xc0000000` -> `0xf9000000`. Software translations prove `0x80008000 -> 0x80008000`, `0x80000000 -> 0x80000000`, `0xde500000 -> 0xde500000`, and `0xc0000000 -> 0xf9000000`. The contract keeps public pmap compile/link/execute counts, public `arm_vm_init` execution, public pmap runtime execution, proposed workspace writes, live pmap table installs, TTBR/TTBCR/DACR/SCTLR writes, TLB invalidations, generated Mach-O execution, persistent writes, and cache changes at zero.

Local Stage61 validation reports `stage61_xnu_compile_graph_status=0x61000001`, public pmap compile/link counts both zero, `stage61_xnu_object_subset_status=0x61000001`, `stage61_xnu_link_status=0x61000001`, no undefined symbols in both the boot payload and host-only XNU link proof, final `out/stage61/stage61-qcdt.img` hash `304b0f9fb60c37841f77bf6a69f553f21bb22ac3f5a74dc6b62ccd9936326839`, and size `text=239368 data=0 bss=364864`. Hardware validation through non-persistent `fastboot boot` recovered 159522 bytes from `/proc/last_kmsg`, confirmed `stage61_xnu_pmap_multiwindow_dryrun_contract_status=0x61000001`, `loader_xnu_pmap_multiwindow_dryrun_contract_status_rollup=0x61000001`, `loader_status=0x61000001`, `kernel_entry ok`, and returned to Android.

Stage61 still performs no full public `mach_kernel` build, no public-XNU object execution, no public platform runtime execution, no public VM/pmap runtime execution, no generated Mach-O execution, no XNU `_start` / `arm_init` jump, no proposed physical/TTE/pmap workspace writes, no live pmap table install, no TLB invalidation for pmap install, no external checkout mutation, no persistent writes, and no cache changes.

## Stage62 implementation note

Stage62 keeps the Stage61-style pmap multi-window page-granular dry-run contract stable, then adds a Stage-owned XNU pmap safe live-table transition prerequisite dry-run contract. Instead of compiling or executing public `arm_vm_init.c` or `pmap.c`, it imports the Stage62 multi-window dry-run output, the Stage62 pmap/bootstrap contract, and the Stage-owned TTBR roundtrip/restored control-state facts. It mirrors only the already-proved local L1 coarse-table descriptor plan into a separate Stage-owned candidate L1 buffer, while still using the inherited local L2-bank tables for software translation checks.

The transition dry-run contract records `stage62_xnu_pmap_transition_dryrun_contract_status=0x62000001`, required/satisfied mask `0x00ffffff`, failure mask `0x00000000`, checksum `0x9baa0bd1`, source multi-window checksum `0x263daa78`, local L2-bank `0x00099000`-`0x0009c000`, candidate L1 `0x000a4000`-`0x000a8000`, candidate L1 write count `0x0000000c`, descriptor words `0x00099001`, `0x0009a001`, and `0x0009b001`, and software translations `0x80008000 -> 0x80008000`, `0x80000000 -> 0x80000000`, `0xde500000 -> 0xde500000`, and `0xc0000000 -> 0xf9000000`. It also records a read-only proposed control-register plan (`TTBR0=0x000a4000`, `TTBCR=0x00000000`, `DACR=0x00000003`, `SCTLR=0x00c5487b`), restored control state, recovery L1 `0x00078000`-`0x0007c000`, proposed pmap workspace L1 `0x0007c000`, and distinctness checks for recovery/workspace/candidate tables.

Local Stage62 validation reports `stage62_xnu_compile_graph_status=0x62000001`, public pmap compile/link counts both zero, `stage62_xnu_object_subset_status=0x62000001`, `stage62_xnu_link_status=0x62000001`, no undefined symbols in both the boot payload and host-only XNU link proof, final `out/stage62/stage62-qcdt.img` hash `6625f35e2f2740d9f5525ee26be8ba3d3989cb0e2a36408debf2cd4e02126327`, and size `text=249616 data=0 bss=397632`. Hardware validation through non-persistent `fastboot boot` recovered 175285 bytes from `/proc/last_kmsg`, confirmed `stage62_xnu_pmap_transition_dryrun_contract_status=0x62000001`, `loader_xnu_pmap_transition_dryrun_contract_status_rollup=0x62000001`, `loader_satisfied_mask=0xffffffff`, `loader_status=0x62000001`, `kernel_entry ok`, and returned to Android.

Stage62 still performs no full public `mach_kernel` build, no public-XNU object execution, no public platform runtime execution, no public VM/pmap runtime execution, no generated Mach-O execution, no XNU `_start` / `arm_init` jump, no proposed physical/TTE/pmap workspace writes, no live pmap table install, no control-register writes for proposed pmap install, no TLB invalidation for proposed pmap install, no external checkout mutation, no persistent writes, and no cache changes.

## Stage63 implementation note

Stage63 keeps the Stage62 pmap safe live-table transition prerequisite dry-run contract stable, then adds a Stage-owned MSM8974 XNU pexpert interrupt/timer hook readiness contract. It does not compile or execute public runtime-heavy ARM pexpert sources (`pe_kprintf.c`, `pe_serial.c`, `pe_identify_machine.c`, or `pe_init.c`), does not execute public ARM pexpert/platform runtime code, and does not enter XNU `_start` / `arm_init`. Instead, it imports Stage-owned PE_state, Apple-DT, GIC snapshot, SGI0 IRQ selftest, generic timer IRQ selftest, 19.2 MHz timebase, bounded public compile graph/object/link proof, and the retained pmap transition dry-run contract into a fail-closed readiness roll-up.

The Stage63 pexpert readiness contract records `stage63_xnu_pexpert_hook_readiness_contract_status=0x63000001`, required/satisfied mask `0x07ffffff`, failure mask `0x00000000`, checksum `0x6cabcf99`, boot-args markers for `mi4ios6.stage=63` and `pexpert-hook-ready`, PE_state CPU count `0x00000004`, GIC bases `0xf9000000`/`0xf9002000`, timer base `0xf9020000`, timer frequency `0x0124f800`, GIC IRQ count `0x00000120`, GIC CPU interface count `0x00000004`, SGI IRQ count `0x00000001`, SGI0 count `0x00000001`, timer PPI mask `0x000c0000`, timer IRQ count `0x00000001`, timer last IRQ ID `0x00000013`, and timebase frequency `0x0124f800`. It also records platform/interrupt readiness masks `0x0000001f` and `0x0000000f` while keeping public pexpert/platform runtime execution, public pmap runtime execution, proposed pmap workspace writes, live pmap table installs, pmap control-register writes, pmap TLB invalidation, cache changes, generated Mach-O execution, XNU entry execution, and persistent writes disabled.

Local Stage63 validation reports `stage63_xnu_compile_graph_status=0x63000001`, public pmap compile/link counts both zero, `stage63_xnu_object_subset_status=0x63000001`, `stage63_xnu_link_status=0x63000001`, no undefined symbols in both the boot payload and host-only XNU link proof, final `out/stage63/stage63-qcdt.img` hash `5fbeedcae7628aa7c0f72bcf6ef1454eba6b1dd00ad364a5d11c219ff73e996a`, and size `text=255824 data=0 bss=398060`. Hardware validation through non-persistent `fastboot boot` recovered 178383 bytes from `/proc/last_kmsg`, confirmed `stage63_xnu_pexpert_hook_readiness_contract_status=0x63000001`, `loader_xnu_pexpert_hook_readiness_contract_status_rollup=0x63000001`, `loader_satisfied_mask=0xffffffff`, `loader_status=0x63000001`, `kernel_entry ok`, and returned to Android.

Stage63 still performs no full public `mach_kernel` build, no public-XNU object execution, no public platform runtime execution, no public VM/pmap runtime execution, no generated Mach-O execution, no XNU `_start` / `arm_init` jump, no proposed physical/TTE/pmap workspace writes, no live pmap table install, no control-register writes for proposed pmap install, no TLB invalidation for proposed pmap install, no external checkout mutation, no persistent writes, and no cache changes.

## Stage64 implementation note

Stage64 keeps the Stage63 MSM8974 pexpert interrupt/timer hook readiness contract stable, then adds a Stage-owned XNU IOKit/platform-driver scaffold readiness contract. It does not compile or execute public IOKit runtime sources, does not execute a Stage-owned platform driver, and does not enter XNU `_start` / `arm_init`. Instead, it extends the Stage-owned Apple flattened device tree with local `/iokit-platform-scaffold` and `/msm8974-platform-driver` nodes, imports public IOKit reference-only classification for `IODeviceTreeSupport.cpp`, `IOPlatformExpert.cpp`, and `IOCPU.cpp`, and records MSM8974 match facts for GIC distributor base `0xf9000000`, timer base `0xf9020000`, 19.2 MHz timebase, and four CPUs.

The Stage64 IOKit/platform scaffold contract records `stage64_xnu_iokit_platform_scaffold_contract_status=0x64000001`, required/satisfied mask `0x00ffffff`, failure mask `0x00000000`, checksum `0x89af157f`, IOKit reference mask `0x00000007`, IOKit runtime-blocked mask `0x00000007`, public IOKit compile/link counts both zero, and no-I/O-Kit/no-platform-driver runtime execution markers set. It also imports the retained pmap transition dry-run contract and the Stage64 pexpert hook readiness roll-up, then refines the existing platform gap mask by clearing only the IOKit-stack gap rather than adding a 33rd top-level loader satisfied bit.

Local Stage64 validation reports `stage64_xnu_compile_graph_status=0x64000001`, candidate count `0x00000017`, public pmap compile/link counts both zero, public IOKit compile/link counts both zero, `stage64_xnu_object_subset_status=0x64000001`, `stage64_xnu_link_status=0x64000001`, no undefined symbols in both the boot payload and host-only XNU link proof, final `out/stage64/stage64-qcdt.img` hash `b6dc97d83cb2d1d17cbcbc36f8997cae97b74e7a4bbe2a8623b4be20c24f6e51`, and size `text=262336 data=0 bss=398396`. Hardware validation through non-persistent `fastboot boot` recovered 182310 bytes from `/proc/last_kmsg`, confirmed `stage64_xnu_iokit_platform_scaffold_contract_status=0x64000001`, `loader_xnu_iokit_platform_scaffold_contract_status_rollup=0x64000001`, `loader_satisfied_mask=0xffffffff`, `loader_status=0x64000001`, `kernel_entry ok`, and returned to Android.

Stage64 still performs no full public `mach_kernel` build, no public-XNU object execution, no public platform runtime execution, no public IOKit runtime execution, no public VM/pmap runtime execution, no generated Mach-O execution, no XNU `_start` / `arm_init` jump, no proposed physical/TTE/pmap workspace writes, no live pmap table install, no control-register writes for proposed pmap install, no TLB invalidation for proposed pmap install, no external checkout mutation, no persistent writes, and no cache changes.

## Stage65 implementation note

Stage65 keeps the Stage64 XNU IOKit/platform-driver scaffold readiness contract stable, then adds a Stage-owned local IOKit service/driver match dry-run contract. It does not compile or execute public IOKit runtime sources, does not execute IOKit matching runtime code, does not execute a Stage-owned platform driver, and does not enter XNU `_start` / `arm_init`. Instead, it uses the Stage-owned Apple flattened device tree provider/driver nodes, adds a local `IOProbeScore=0x00000650`, imports public IOKit reference-only classification for `IODeviceTreeSupport.cpp`, `IOPlatformExpert.cpp`, and `IOCPU.cpp`, and records a local one-provider/one-driver dry-run selection with attach/start deferred.

The Stage65 IOKit match dry-run contract records `stage65_xnu_iokit_match_dryrun_contract_status=0x65000001`, required/satisfied mask `0x03ffffff`, failure mask `0x00000000`, checksum `0xf9646bb0`, provider class/path/category/compatible match markers all set, driver probe score `0x00000650`, dry-run match count `0x00000001`, attach/start deferred markers set, IOKit reference mask `0x00000007`, IOKit runtime-blocked mask `0x00000007`, public IOKit compile/link counts both zero, and no-I/O-Kit/no-platform-driver/no-live-pmap/no-persistent-write markers preserved. It also imports the retained Stage65 IOKit scaffold contract, pmap transition dry-run contract, and pexpert hook readiness roll-up, then records the match dry-run as a loader preflight roll-up rather than adding a 33rd top-level loader satisfied bit.

Local Stage65 validation reports `stage65_xnu_compile_graph_status=0x65000001`, candidate count `0x00000017`, public pmap compile/link counts both zero, public IOKit compile/link counts both zero, `stage65_xnu_object_subset_status=0x65000001`, `stage65_xnu_link_status=0x65000001`, no undefined symbols in both the boot payload and host-only XNU link proof, final `out/stage65/stage65-qcdt.img` hash `6e063c5738c66002075ea4b2451a6413b6d6e8d63de20b72fb1d5febff3ac740`, and size `text=270228 data=0 bss=398740`. Hardware validation through non-persistent `fastboot boot` recovered 184957 bytes from `/proc/last_kmsg`, confirmed `stage65_xnu_iokit_match_dryrun_contract_status=0x65000001`, `loader_xnu_iokit_match_dryrun_contract_status_rollup=0x65000001`, `loader_satisfied_mask=0xffffffff`, `loader_status=0x65000001`, `kernel_entry ok`, and returned to Android.

Stage65 still performs no full public `mach_kernel` build, no public-XNU object execution, no public platform runtime execution, no public IOKit runtime execution, no IOKit match runtime execution, no public VM/pmap runtime execution, no generated Mach-O execution, no XNU `_start` / `arm_init` jump, no proposed physical/TTE/pmap workspace writes, no live pmap table install, no control-register writes for proposed pmap install, no TLB invalidation for proposed pmap install, no external checkout mutation, no persistent writes, and no cache changes.

## Stage66 implementation note

Stage66 keeps the Stage65 local IOKit service/driver match dry-run contract stable, then adds a Stage-owned broader IOKit registry/service dry-run contract. It does not compile or execute public IOKit runtime sources, does not execute IOKit matching or registry/service runtime code, does not execute a Stage-owned platform driver, and does not enter XNU `_start` / `arm_init`. Instead, it expands the Stage-owned Apple flattened device tree with local MSM8974 platform, interrupt, timer, CPU service nodes plus one rejected negative driver candidate, imports public IOKit reference-only classification for `IODeviceTreeSupport.cpp`, `IOPlatformExpert.cpp`, and `IOCPU.cpp`, and records a local one-provider/five-driver dry-run matrix with four selected service matches, one rejected candidate, and attach/start deferred.

The Stage66 IOKit registry/service dry-run contract records `stage66_xnu_iokit_registry_service_dryrun_contract_status=0x66000001`, required/satisfied mask `0x07ffffff`, failure mask `0x00000000`, checksum `0x9dd7207f`, provider/category/compatible/probe-score match masks all `0x0000000f`, selected service mask `0x0000000f`, rejected service mask `0x00000010`, selected driver count `0x00000004`, rejected driver count `0x00000001`, dry-run match count `0x00000004`, IOKit reference mask `0x00000007`, IOKit runtime-blocked mask `0x00000007`, public IOKit compile/link counts both zero, and no-I/O-Kit/no-platform-driver/no-live-pmap/no-persistent-write markers preserved. It also imports the retained Stage66 IOKit scaffold contract, Stage66 IOKit single-match dry-run contract, pmap transition dry-run contract, and pexpert hook readiness roll-up, then records the registry/service dry-run as a loader preflight roll-up rather than adding a 33rd top-level loader satisfied bit.

Local Stage66 validation reports `stage66_xnu_compile_graph_status=0x66000001`, candidate count `0x00000017`, public pmap compile/link counts both zero, public IOKit compile/link counts both zero, `stage66_xnu_object_subset_status=0x66000001`, `stage66_xnu_link_status=0x66000001`, no undefined symbols in both the boot payload and host-only XNU link proof, final `out/stage66/stage66-qcdt.img` hash `7c082e8eb986deaa0115c3cc6b23627f1864a69b0391070995b9c79d5dff9f90`, and size `text=281804 data=0 bss=403248`. Hardware validation through non-persistent `fastboot boot` recovered 190399 bytes from `/proc/last_kmsg`, confirmed `stage66_xnu_iokit_registry_service_dryrun_contract_status=0x66000001`, `loader_xnu_iokit_registry_service_dryrun_contract_status_rollup=0x66000001`, `loader_satisfied_mask=0xffffffff`, `loader_status=0x66000001`, `kernel_entry ok`, and returned to Android.

Stage66 still performs no full public `mach_kernel` build, no public-XNU object execution, no public platform runtime execution, no public IOKit runtime execution, no IOKit match or registry/service runtime execution, no public VM/pmap runtime execution, no generated Mach-O execution, no XNU `_start` / `arm_init` jump, no proposed physical/TTE/pmap workspace writes, no live pmap table install, no control-register writes for proposed pmap install, no TLB invalidation for proposed pmap install, no external checkout mutation, no persistent writes, and no cache changes.

## Stage67 implementation note

Stage67 keeps the Stage66 broader IOKit registry/service dry-run contract stable, then adds a Stage-owned IOKit provider-plane publish/order dry-run contract. It does not compile or execute public IOKit runtime sources, does not execute IOKit matching, registry/service, or provider-plane runtime code, does not execute a Stage-owned platform driver, and does not enter XNU `_start` / `arm_init`. Instead, it imports the Stage66 selected local platform/interrupt/timer/CPU service candidates and rejected negative candidate, records provider-parent/dependency/publish-order facts, and models deterministic provider-plane publication order with root/platform/interrupt/timer/CPU ordinals while keeping the rejected candidate unpublished.

The Stage67 provider-plane dry-run contract records `stage67_xnu_iokit_provider_plane_dryrun_contract_status=0x67000001`, required/satisfied mask `0x07ffffff`, failure mask `0x00000000`, checksum `0xfc28394a`, service fact, parent, dependency, and publish-order masks all `0x0000000f`, published service mask `0x0000000f`, unpublished service mask `0x00000010`, one provider candidate, five service candidates, four dry-run publications, one unpublished candidate, deferred attach/start counts of four, IOKit reference mask `0x00000007`, IOKit runtime-blocked mask `0x00000007`, public IOKit compile/link counts both zero, and no-I/O-Kit/no-provider-plane/no-platform-driver/no-live-pmap/no-persistent-write markers preserved. It also imports the retained Stage67 IOKit scaffold contract, Stage67 IOKit single-match dry-run contract, Stage67 registry/service dry-run contract, pmap transition dry-run contract, and pexpert hook readiness roll-up, then records the provider-plane dry-run as a loader preflight roll-up rather than adding a 33rd top-level loader satisfied bit.

Local Stage67 validation reports `stage67_xnu_compile_graph_status=0x67000001`, candidate count `0x00000017`, public pmap compile/link counts both zero, public IOKit compile/link counts both zero, `stage67_xnu_object_subset_status=0x67000001`, `stage67_xnu_link_status=0x67000001`, no undefined symbols in both the boot payload and host-only XNU link proof, final `out/stage67/stage67-qcdt.img` hash `9fc9c3e0eaee02aaa22e21e199c8194a8bb39850dd0cd7c7e174f29a8171a64a`, and size `text=289376 data=0 bss=403640`. Hardware validation through non-persistent `fastboot boot` recovered 195330 bytes from `/proc/last_kmsg`, confirmed `stage67_xnu_iokit_provider_plane_dryrun_contract_status=0x67000001`, `loader_xnu_iokit_provider_plane_dryrun_contract_status_rollup=0x67000001`, `loader_satisfied_mask=0xffffffff`, `loader_status=0x67000001`, `kernel_entry ok`, and returned to Android.

Stage67 still performs no full public `mach_kernel` build, no public-XNU object execution, no public platform runtime execution, no public IOKit runtime execution, no IOKit match, registry/service, or provider-plane runtime execution, no public VM/pmap runtime execution, no generated Mach-O execution, no XNU `_start` / `arm_init` jump, no proposed physical/TTE/pmap workspace writes, no live pmap table install, no control-register writes for proposed pmap install, no TLB invalidation for proposed pmap install, no external checkout mutation, no persistent writes, and no cache changes.

## Stage68 implementation note

Stage68 keeps the Stage67 IOKit provider-plane publish/order dry-run contract stable, then adds a Stage-owned IOKit catalog/property/personality dry-run contract. It does not compile or execute public IOKit runtime sources, does not execute IOKit matching, registry/service, provider-plane, catalog, or property-plane runtime code, does not execute a Stage-owned platform driver, and does not enter XNU `_start` / `arm_init`. Instead, it extends the Stage-owned Apple flattened device tree with a local catalog node plus platform, interrupt, timer, CPU, and rejected personalities, imports the Stage67 selected local provider-plane publications and rejected negative candidate, and records deterministic local catalog/personality property facts with attach/start deferred.

The Stage68 catalog/property dry-run contract records `stage68_xnu_iokit_catalog_property_dryrun_contract_status=0x68000001`, required/satisfied mask `0x7fffffff`, failure mask `0x00000000`, checksum `0x6fffd084`, Apple-DT semantic mask `0x0001ffff`, catalog version `0x00000001`, source selected mask `0x0000000f`, source rejected mask `0x00000010`, source fact/parent/dependency/publish-order masks all `0x0000000f`, name/bundle/catalog-marker/provider-class/category/compatible/probe-score/local-only masks all `0x0000000f`, selected personality mask `0x0000000f`, rejected personality mask `0x00000010`, catalog property matrix `0x0000001f`, order checksum `0xffffffff`, property checksum `0xfffffffb`, expected checksum `0xfffffffb`, one catalog, five personality candidates, four selected personalities, one rejected personality, four dry-run personality selections, and attach/start counts of four. It also imports the retained Stage68 IOKit provider-plane contract, registry/service dry-run contract, IOKit single-match dry-run contract, IOKit scaffold contract, pmap transition dry-run contract, and pexpert hook readiness roll-up, then records the catalog/property dry-run as a loader preflight roll-up rather than adding a 33rd top-level loader satisfied bit.

Local Stage68 validation reports `stage68_xnu_compile_graph_status=0x68000001`, candidate count `0x00000017`, public pmap compile/link counts both zero, public IOKit compile/link counts both zero, `stage68_xnu_object_subset_status=0x68000001`, `stage68_xnu_link_status=0x68000001`, no undefined symbols in both the boot payload and host-only XNU link proof, final `out/stage68/stage68-qcdt.img` hash `21d46a040917364e20573a9905ab6fd38a5981e8106a5c421bda688fc8915339`, and size `text=300672 data=0 bss=428636`. Hardware validation through non-persistent `fastboot boot` recovered 201848 bytes from `/proc/last_kmsg`, confirmed `stage68_xnu_iokit_catalog_property_dryrun_contract_status=0x68000001`, `loader_xnu_iokit_catalog_property_dryrun_contract_status_rollup=0x68000001`, `loader_satisfied_mask=0xffffffff`, `loader_status=0x68000001`, `kernel_entry ok`, and returned to Android.

Stage68 still performs no full public `mach_kernel` build, no public-XNU object execution, no public platform runtime execution, no public IOKit runtime execution, no IOKit match, registry/service, provider-plane, catalog, or property-plane runtime execution, no public VM/pmap runtime execution, no generated Mach-O execution, no XNU `_start` / `arm_init` jump, no proposed physical/TTE/pmap workspace writes, no live pmap table install, no control-register writes for proposed pmap install, no TLB invalidation for proposed pmap install, no external checkout mutation, no persistent writes, and no cache changes.

## Verification commands used

Representative commands used during this pass:

```bash
git -C external/xnu-upstream fetch --tags origin
git -C external/xnu-upstream tag -l 'xnu-2050*'
git -C external/xnu-upstream show xnu-2050.22.13:config/MasterVersion
git -C external/xnu-upstream reset --hard xnu-2050.22.13
git -C external/xnu-upstream switch --detach xnu-2050.22.13
git -C external/xnu-upstream status --short --branch
git -C external/xnu-upstream rev-parse HEAD
git -C external/xnu-upstream describe --tags --always
git -C . check-ignore -v external/xnu-upstream
```

The clone remains under ignored `external/` and must not be committed.

## Safety notes

The initial upstream analysis was source-only. Later Stage43 through Stage68 hardware validations used non-persistent `sudo fastboot boot` only. No flash, erase, partition write, persistent hardware configuration, or bootloader change was performed. Future hardware validation must continue using non-persistent `sudo fastboot boot` unless the user explicitly authorizes a specific persistent operation.
