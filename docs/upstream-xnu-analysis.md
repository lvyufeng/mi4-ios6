# Upstream XNU Analysis for Mi4 Cancro Bring-up

Date: 2026-06-05

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

The remaining blockers are now more focused: real pmap/bootstrap table population, exact XNU mapping/cache policy, MSM8974 pexpert support, XNU interrupt/timer hooks, IOKit/platform drivers, real kernelcache/loading details, relocation/linking/prelink details, and later code-signing/userspace policy work.

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

This was a source-only analysis task. No hardware boot, flash, erase, partition write, or bootloader change was performed. Future hardware validation must continue using non-persistent `sudo fastboot boot` unless the user explicitly authorizes a specific persistent operation.
