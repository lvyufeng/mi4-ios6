# Experiment 356 — `IODeviceTreeSupport.cpp`: the virtual dispatch the tooling cannot see

**Step:** link one object, `iokit/Kernel/IODeviceTreeSupport.cpp` (`iokit_Kernel_IODeviceTreeSupport.o`) — the
only pool definer of 355's stop `_Z17IODeviceTreeAllocPv`. Nothing else changes.

**Prediction:** *11 resolved / 2 added — 864 → **855** undefined, 753 → **748** function, 111 → **107**
storage;* `.text` a band at +0x27F8..+0x2AFF with **both ends past the `0x8018C000` boundary**, so **`.data`
steps a whole `0x4000` to `0x80190000`** and every row below it moves with it; and **the stop at
`_ZN10IOWorkLoop8workLoopEv` at `initWithArgs+0x6C`, key `0x8015FF94`** — the stub 355 created, at the far end
of the call this step retires. Three named falsifiers: one of the 29 `blx` dispatches inside
`IODeviceTreeAlloc`; the real `panic` at 0x8002DEB4 reached from object +0x4F0 on a failed DT lookup; or
`IOService::init` returning zero.

**Result:** all three counts exact, **every row of the layout exact or inside its band**, the image exact — and
**the stop is not the predicted one**: `stub_hit=_ZN9IOService6PMfreeEv` at `xnu_entry_stub_caller_v=0x8012ad60`
= `IOService::free+0x2c`, `abort_entries=0`. Falsifier (a) fired, in its sharpest form: the walk deviated
through a chain of **virtual** dispatches (`regEntry->init()`, `regEntry->release()`, `release`'s own virtual
`free()`), which is the class of call this walk's direct-call tooling has never been able to see. The IOService
that was freed can only have come from `MakeReferenceTable`, and the release that freed it is one of three sites
in this step's own object — two of them in `IODeviceTreeAlloc` — and **the instrument names one frame and not
the path**, so which one fired is this step's open question, stated as such and answered by 357's run.

## The object

| `iokit_Kernel_IODeviceTreeSupport.o` | |
|---|---|
| `.text` | **10528** (0x2920), 82 definitions |
| `.bss` | **88** (0x58) |
| `.rodata.str1.1` | **775** (0x307) |
| `.rodata`, `.init_array`, COMDAT, `.data` | **none of them** |
| definitions / references | 82 / 57, of which 46 already satisfied |

**11 resolved / 2 added** — seven functions (`IODTFreeLoaderInfo`, `IODTGetDefault`, `IODTGetLoaderInfo`,
`_Z17IODeviceTreeAllocPv`, `_Z18IODTCompareNubNamePK15IORegistryEntryP8OSStringPS3_`,
`_Z21IODTResolveAddressingP15IORegistryEntryPKcP14IODeviceMemory`,
`_Z23IODTFindMatchingEntriesP15IORegistryEntrymPKc`) and four storage stand-ins (`gIODTCompatibleKey`,
`gIODTModelKey`, `gIODTPlane`, `gIODTTargetTypeKey`, all `B 4`) out; **two functions in**
(`IODeviceMemory::withSubRange`, `IODeviceMemory::withRange`) — for 864 → **855** undefined,
**753 → 748 function, 111 → 107 storage**, 748 + 107 = 855.

`IODeviceTreeAlloc`'s body was read from its object, as 348's rule requires: **0x694 bytes, 41 direct calls**,
and the walk of that body against the 355 image puts **79 direct external targets on the path — the function,
its two local callees (`_ZL18MakeReferenceTableP13OpaqueDTEntryb`,
`_ZL24IODTMapInterruptsSharingP15IORegistryEntryP12OSDictionary`) and the two locals those reach — with not one
of them a stub or absent**. The smallest is `IORegistryEntry::getRegistryRootEv` at `T 16` and the largest
`IORegistryEntry::fromPathEPKcPK15IORegistryPlanePcPiPS_` at `T 676`, each read with `nm -S -P`, which prints
the defined name first and its size last.

**That claim cost two attempts, and the first was a parser.** The first pass admitted four-field `nm -S`
records (`value size type name`) — what a *sized* symbol prints. A defined symbol the entry shim supplies
without a size prints three, so `strncmp` at `T 0x80006cc0`, really called from `MakeReferenceTable` at object
+0x1EC, came back "not in the image". `nm -u` on the image prints **0** lines, so the reading was impossible,
not merely wrong: **a record that cannot be read has to be counted as refused, not as absent.** This is the
record-shape defect `tools/entry_object_effect.py` was built to not have, three steps later.

## The stop: a chain of dispatches, and a stop that is not one name

```
stub_hit=_ZN9IOService6PMfreeEv
xnu_entry_stub_caller_v=0x8012ad60   (also _a and _e, all three agreeing)
caller-4 = 0x8012AD5C: bl 80167548 <_ZN9IOService6PMfreeEv>   0x18 STUB
abort_entries=0x00000000
```

`tools/host_resolve_entry_addr.sh 0x8012ad60` → **`_ZN9IOService4freeEv+0x2c`**. `IOService::free`'s body was
read from the image: after the stub it calls only real things — `IOFree` ×3 (0x8012adc8, 0x8012adec, 0x8012ae08),
`IOLockFree` (0x8012ade0), one virtual at 0x8012ada0 — and **tail-calls** the real `IORegistryEntry::free` at
0x8012ae1c.

What the object makes certain is that the walk entered `IODeviceTreeAlloc` and built its first nub: `new
IOService` exists **only** in `MakeReferenceTable` (object +0x6B0, one site) and `MakeReferenceTable` is called
**only** from `IODeviceTreeAlloc` (object +0x248 and +0x300). `MakeReferenceTable`'s first call is
`regEntry->init()` as a **virtual** call — `ldr r0, [r0]; mov r1, #0; ldr r2, [r0, #0x50]; blx r2`, and
`vptr + 0x50` is `vtable + 0x58` = `_ZN9IOService4initEP12OSDictionary` in `_ZTV9IOService` at 0x801808dc —
and its failure branch (+0x2B0) is `ldr r0, [r4]; ldr r1, [r0, #0x14]; blx r1; mov r4, #0`, i.e.
`vptr + 0x14` = `vtable + 0x1C` = `_ZNK8OSObject7releaseEv`. A release that reaches zero then enters `free()`
through the same vptr, and `IOService::free` is where the stub is.

**Three sites can free an IOService in this step, and one is eliminated:**

- **(i) `MakeReferenceTable`'s failure branch** (object +0x944, `regEntry->release(); regEntry = 0;`) —
  **eliminated, four ways.** It needs `init()` to return 0, i.e. one of `IORegistryEntry::init`'s four failure
  points, and three of the four are in the image as arithmetic: `OSObject::init()` is `mov r0, #1; bx lr`;
  the two `kalloc` paths cannot fail, because `zalloc_internal` (0x8006f9d0) loads `[zone + 0xa1]` at
  0x8006f9e8, so a NULL zone — the state before `kalloc_init`, which `vm_mem_bootstrap` runs at 0x80040c60
  early on this walk's path — would fault, and `abort_entries=0`; and `IORecursiveLockAlloc`'s
  `if (!IOLockGroup) return NULL` (0x8011ce4c) cannot fire because `IOLibInit` (0x8011b41c) stores the real
  `lck_grp_alloc_init` result into `IOLockGroup` (`movw/movt r7, #0x801df508; str r0, [r7]` at 0x8011b44c and
  0x8011b460) and **its call site 0x8011b260 precedes the `devsw_init` call site 0x8011b268 that 352's run
  stopped at** — the CPU has executed it on every run since 352. The fourth, `OSDictionary::withCapacity(4)`
  → `kalloc_canblock(&0x20, 1, &site)`, is the same `kalloc` argument. And the branch's consequence is ruled
  out independently: a NULL `MakeReferenceTable` result is dereferenced by `child->attachToParent` (whose body
  reads `reserved->fRegistryEntryID`) and by `AddPHandle`, i.e. a virtual call on NULL reading the vptr at
  address 0 — an abort, and `abort_entries=0`.
- **(ii) `IODeviceTreeAlloc`'s per-child `child->release()`** in the device-tree loop
  (`IODeviceTreeSupport.cpp:186`, the line whose comment is *"only registry holds retain"*) — fires when that
  child's `attachToParent(parent, gIODTPlane)` did not retain it.
- **(iii) `IODeviceTreeAlloc`'s root `parent->release()`** after
  `parent->attachToParent(IORegistryEntry::getRegistryRoot(), gIODTPlane)` (line 205) — fires when that attach
  did not retain. `getRegistryRoot` is a real 0x10-byte body and the root exists: `IORegistryEntry::initialize`
  is called from `iokit_post_constructor_init` at 0x8011b0f4, which this walk ran through at 351/352.

(ii) and (iii) both reach `IOService::free` through `OSObject::release`'s virtual `free()`, so both produce
exactly the record above. **The instrument names the innermost frame, and `stub_hit` + `caller` are a one-name
fact only when the frames above them are direct calls.** 356 is the first stop of the walk where they are not —
348's `IOGetLastPageNumber` and 351's `IOKitDiagnostics::diagnosticsEv` were both `bl`-reached — and the honest
form of the result is to say so and let 357's run name the frame, rather than to pick the more likely site and
write it as measured.

## The layout: every row exact, and the `__DATA` step carried by the boundary

| | 355 | 356 measured | 356 predicted |
|---|---|---|---|
| `.text` | 0x80189BA0 | **0x8018C600** | 0x8018C398..0x8018C69F ✓ |
| `.data` | 0x8018C000 (0x198B8) | **0x80190000** (0x198B8, **unmoved**) | **0x80190000** ✓ |
| `.sysctl_set` | 0x801A58B8 (0x140) | **0x801A98B8** (0x140) | **0x801A98B8** ✓ |
| `.init_array` | 0x801A59F8 (0x68, 26) | **0x801A99F8** (0x68, 26) | **0x801A99F8** ✓ |
| its end | 0x801A5A60 | **0x801A9A60** | **0x801A9A60** ✓ |
| `.bss` | 0x801A5A80 | **0x801A9A80** | **0x801A9A80** ✓ |
| `.bss` size | 0x39018 | **0x38F58** | 0x38F18..0x38F70 ✓ |
| `__bss_end` | 0x801DEA98 | **0x801E29D8** | 0x801E2998..0x801E29F0 ✓ |
| image | 1727072 | **1743456** | **1743456** ✓ |
| headroom | 1185128 | **1168936** | 1169000..1168912 ✓ |

**The step was predicted from the boundary and not from the fill, and that is why the row that usually misses
did not.** `.text` ended at 0x80189BA0 with `0x2460` of room below the `0x8018C000` boundary, the object's
closure is at least `+0x27F8`, so no fill could have kept `.data` where it was, and both ends of the band fell
on the same side of `0x80190000`. The step's own map then shows every part: the object's `.text` at
**0x80160610** (0x2920), its `.rodata.str1.1` placed at **0x801873F0 — 0x272 of the object's 0x307**, 0x95
merged — `.text` closing at 0x8018C600 with the `.v4_bx`/`.iplt` rows at that address, and `.data` at
0x80190000.

The closure, to the byte: `0x2920 + 0x272 − 0xA8 − 0xFC + 0x30 + 0x4C = 0x2A6A` against a measured `+0x2A60`,
so **fill −0xA**. The seven retired name slots (0x14, 0x10, 0x14, 0x18, 0x38, 0x40, 0x34 = **0xFC**) and the
two created ones (0x28, 0x24 = **0x4C**) are the same `align4(len + 1)` sum `realstubs.o` prints, and
`realstubs.o` is exact on all three closed forms again: `.text` **0x4620** = 748 × 0x18, `.rodata.str1.4`
**0x448C** = 0x453C − 0xFC + 0x4C, `.bss` **0x2884** = 0x2984 − 4 × 0x40.

**The `.bss` row is inside its band for the first time in ten steps, and the mechanism is the absorption
measured from the other side.** The object's 0x58 lands at **0x801E00C4** and `realstubs.o`'s `.bss` begins at
**0x801E0140**, so the pre-existing `*fill*` of 0x3C **shrank to 0x24**: the section grew by
`0x58 − 0x18 = 0x40` exactly, where the 0x18 is the part of the object's bytes the alignment gap swallowed.
353 absorbed 0x20 of a 0x30 gap, 354 absorbed 0x10 of a 0x30 gap, 355 absorbed nothing and the gap grew, 356
absorbs 0x18 of a 0x3C gap. **The placed term and the fill are two predictions, and only the first is
derivable** — the second is read off the map, every time.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000067   xnu_entry_kv_in_dram=0x0000008b   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x80167d90          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x8012ad60   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN9IOService6PMfreeEv
```

The run's own build lines agree with the table in both directions: `855 symbol(s) undefined` /
`748 function(s), 107 storage`, `__bss_start 0x801a9a80 ... (233304 bytes to 0x801e29d8)`, `the copied image
ends at 0x801a9a60, 32 bytes below __bss_start`, `image bytes 1743456`, `headroom 1168936 bytes below
topOfKernelData`.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of
either, `xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301634 bytes, and the device back on Android on its
own (`MI 4LTE`, release 10).

## Frontier: 357 — `iokit/Kernel/IOServicePM.cpp`, the definer of `PMfree`

`iokit_Kernel_IOServicePM.o` is the only pool definer of this step's stop `_ZN9IOService6PMfreeEv`:
**69 resolved (68 function, 1 storage) / 32 added (30 function, 2 storage)** — the whole IOService
power-management API out, plus the `gCanSleepTimeout` stand-in; thirty functions in (the `IOEventSource` and
`IOPowerConnection` APIs, `IOCommand`'s init and destructor) and two `gMetaClass` stand-ins — for
855 → **818** undefined, **748 → 710 function, 107 → 108 storage**. **It is the largest single-object
retirement of the walk**, 69 names against 349's 15 and 355's 8, on the largest `.text` the walk has linked:
**0x113A4** (70564 bytes), with `.bss` 0x174, `.rodata` 0x43C, `.rodata.str1.1` 0x88E, `.data` 0xC8,
`__DATA,__sysctl_set` 0x10 and an `.init_array` entry. Its 68 retired name slots are 0xAD8 and its 30 created
ones 0x48C, so `realstubs.o` should print `.text` **0x4290** = 710 × 0x18, `.rodata.str1.4` **0x43F0** =
0x448C − 0xAD8 + 0x48C, `.bss` **0x2884** = 0x2884 − 0x40 + 0x80.

**Predicted layout — and this is the largest `__DATA` move of the walk: `.data` steps `0x10000` to
`0x801A0000`.** The closure is `+0x10E04` (nothing of the 0x88E string bytes placed) to `+0x11692` (all
placed), so `.text` ends between 0x8019D404 and 0x8019DC92 — both above `0x80190000` and both below
`0x801A0000` — and one `ALIGN(0x4000)` carries the whole `__DATA` segment four boundaries up: `.data`
**0x801A0000** size **0x19980**, `.sysctl_set` **0x801B9980** (**0x150**), `.init_array` **0x801B9AD0**
(**0x6C**, twenty-seven) ending **0x801B9B3C**, `.bss` start **0x801B9B40**, `.bss` size **0x3910C** + fill,
`__bss_end` 0x801F2C4C..(+fill), image **1809212**. The *boot_args* move with it by construction —
`ENTRY_ARGS_OFFSET` is `align_up(bss_end − ENTRY_BASE, 4096) + 0x1000`, *"the first page above the image, so it
cannot overlap it however it grows"* — so `args` goes 0x801E4000 → **0x801F4000** and `topOfKernelData` from
0x80300000 to **0x80400000**, still far below the tree buffer at `ENTRY_BASE + 5242880`.

**Predicted stop: `_ZN10IOWorkLoop8workLoopEv` at `initWithArgs+0x6C`, key `0x8015FF94`** — the stub 355
created, now at the far end of two frames the walk has to unwind through, with `PMfree` real: `IOService::free`
runs out (every remaining call in it is real), `release` returns, and the release site in `IODeviceTreeAlloc`
continues the device-tree walk. Three named falsifiers: (a) the site was (ii), so the walk resumes in the
middle of the tree and meets the next stub in the loop rather than at the end — and 357's record then names
frame (ii), closing 356's open question; (b) `IOService::init`'s failure path reached again from `initWithArgs`
at `+0x7C`, which sends `StartIOKit` to its `pop` rather than on toward `workLoop`; (c) a stub inside
`IODTMapInterruptsSharing` (0x9a0 bytes, 50 direct calls) — walked against the 356 image with every target
real, so the walk would have to arrive through one of its own `blx` sites.
