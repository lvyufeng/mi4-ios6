# Experiment 363 — the stage's own platform expert: the driver Apple's tree does not have

**Step:** stop linking one more object and start **authoring** one. Two new objects, both built by
`tools/build_xnu_arm_kernel.sh`'s platform block and neither in any manifest:

* `stages/stage90/xnu_platform/MSM8974PlatformExpert.cpp` → `out/xnu_platform_obj/MSM8974PlatformExpert.o`
  — a concrete `IODTPlatformExpert`.
* `stages/stage90/xnu_platform/stage90_platform_config_tables.c` →
  `out/xnu_platform_obj/stage90_platform_config_tables.o` — the kernel's personality table, which
  **replaces** the pool's `iokit_KernelConfigTables.o` one object for one object.

The class object is inserted into the entry link between `osfmk_kern_sched_average.o` and the
last-kernel-constructor sentinel; the table object takes the stock object's place. Nothing else changes.

**Prediction:** *0 resolved / 0 added — 786 → **786** undefined, 682 → 682 function, 104 → 104 storage,* and
`realstubs.o` unchanged in all three sections; `.text` ending **0x801A3D80**; `.data` **0x801A4000** (0x19A58)
unmoved with the table's word back at 0x801BC2A0; `.sysctl_set` **0x801BDA58** (0x150); `.init_array`
**0x801BDBA8** (**0x7C**, thirty-one) ending 0x801BDC24; `.bss` **0x801BDC40** (0x39118); `__bss_end`
**0x801F6D58**; image **1825828**; headroom **1086120**; `args` **+2064384**; `topOfKernelData` **+3145728**;
and the stop at **`_ZN8IOMapper17setMapperRequiredEb`** at key **`0x8015D8FC`**, with
`_ZN16IORangeAllocator9withRangeEmmmm` (key 0x8015D98C) and a repeat of 362's panic as the two named
falsifiers.

**Result:** every count and every section boundary below `.text` exact, the stop **name and key exactly as
predicted** — and, the point of the step, **362's panic did not recur: the machine's own platform expert
matched and started.** `allocClassWithName("MSM8974PlatformExpert")` found the class, `IODTPlatformExpert::probe`
returned it, `startCandidate` called our `start`, our `start` called Apple's `IOPlatformExpert::start`, and
Apple's own code ran until it reached something the image cannot call. Three rows missed, all in the `.rodata`
run or in its arithmetic.

## Why the missing piece was a class, not an object

362's run ended at `IOPanicPlatform::start`, which is Apple's designed fallback: its class comment is "If no
legitimate IOPlatformDevice matches, this one does and panics the kernel with a suitable message." The
personality table it fires from is `iokit/KernelConfigTables.cpp:35` and has exactly one entry. The tree has no
second one, and both candidates for "the real platform expert" are unusable as they stand:

* `IODTPlatformExpert` — Apple's whole device-tree platform expert, every method of it real in this image since
  experiment 355 — is **abstract**: `IOPlatformExpert.cpp:1243` is
  `OSDefineMetaClassAndAbstractStructors(IODTPlatformExpert, IOPlatformExpert)`, and matching instantiates a
  driver through `OSMetaClass::allocClassWithName` (`IOService.cpp:3296-3301`), which refuses an abstract class.
* `ApplePlatformExpert` (`iokit/IOKit/platform/ApplePlatformExpert.h:61`) is abstract *and* has no
  implementation anywhere in the tree.

So the step is a fourteen-line class body: the metaclass, the two pure virtuals `IODTPlatformExpert` leaves for
its subclasses (`deleteList`, `excludeList`), and a `start` that forwards to `super::start`. Everything the
machine then does is Apple's. `start` is written out for a second, structural reason as well: this walk's rule
358 is that an object owning a vtable is not accounted for by its reference list, and a class with no virtual of
its own gets no vtable in its translation unit under the standard key-function rule — a vtable nothing defines
arrives in the link as a *storage stand-in*, and the first dispatch through it is a jump through zero. Defining
the three virtuals gives `_ZTV21MSM8974PlatformExpert` a home.

One requirement this class puts on its personality, and it is not optional: `IODTPlatformExpert::probe`
(`IOPlatformExpert.cpp:1256-1268`) is three lines, and the last is
`if( !provider->compareNames( getProperty( gIONameMatchKey ) )) return( 0 );`. With no such property
`getProperty` returns 0, `IORegistryEntry::compareNames(0)` falls through both casts to a null `string` and
returns **false** (`IORegistryEntry.cpp:903-928`), and the machine goes straight back to the fallback. The table
therefore carries `'IONameMatch' = "qcom,msm8974-xnu-stage90"`, which is the root node's `compatible` as this
machine's device tree writes it (`stage90_main.c:80`); the comparison is `IOPlatformExpertDevice::compareName` →
`IODTCompareNubName` → `CompareKey` over the node's `name`, `compatible`, `device_type` and `model`
(`IODeviceTreeSupport.cpp:799-865`), so an exact match on any of the four is a match. The value is **quoted**
because the old-style plist lexer's unquoted-string rule accepts `[A-Za-z0-9-]` only (`OSUnserialize.y:298-317`)
and this one has a comma.

The table keeps Apple's entry behind ours, unchanged, so that a machine with no class of this name still behaves
exactly as 362 measured it.

## What the stop means

The run reported

```
 xnu_entry_why=0x8017e98c          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x8015d8fc
 xnu_entry_abort_entries=0x00000000     xnu_entry_kv_dropped=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN8IOMapper17setMapperRequiredEb
```

`why` is a string address and not a return address: it is the message `a symbol this image does not provide was
called` (`xnu_arm_entry_stubs.o`'s `.rodata.str1.4`, at 0x8017E450 + 0x53C in this image), and 0x61 is its first
letter. So this is the stub-hit path, not the exception path — and the absence of 362's panic is the step's
whole content.

`xnu_entry_stub_caller_v=0x8015D8FC` is `caller-4` = `0x8015D8F8`, the `bl` in `IOPlatformExpert::start`:

```
8015d8f0:  cmp    r0, #0
8015d8f4:  movwne r0, #1
8015d8f8:  bl     8017dfa8 <_ZN8IOMapper17setMapperRequiredEb>
8015d8fc:  mov    r0, #1
8015d900:  bl     8011f8c8 <_ZN12OSDictionary12withCapacityEj>
```

The address is the same one it had in the 362 image, because both new objects are linked after it. The two
instructions at 0x8015D8EC and 0x8015D8E4 are the virtual `getProperty(kIOPlatformMapperPresentKey)` and the
`removeProperty` before it — which is why `tools/xnu_entry_callwalk.py` reports "no stub on the straight-line
path" for this root and lists the site in its *conditional-branch* table instead:

```
_ZN16IOPlatformExpert5startEP9IOService+0x84 -> _ZN8IOMapper17setMapperRequiredEb   STUB
```

So the frontier has moved from *this machine has no platform driver* to *this machine's platform driver wants
`IOMapper`*. `IOMapper::setMapperRequired(bool)` is `iokit/Kernel/IOMapper.cpp:110` — a one-line setter whose
object is not in the link. The next step is the object that owns `IOMapper`, and what the run after *that*
reaches will be decided by the source order the disassembly above continues in: `OSDictionary::withCapacity`,
`IOLockAlloc`, `OSData::withBytesNoCopy`, `OSSymbol::withCStringNoCopy`, `IORangeAllocator::withRange`,
`PMInstantiatePowerDomains`.

## What the layout did

| | 362 measured | 363 predicted | 363 measured |
|---|---|---|---|
| `.text` | end 0x801A3720 | end **0x801A3D80** | **0x801A3D80** (text 1719680) |
| `.data` | 0x801A4000 (0x19A58) | 0x801A4000 (0x19A58) | **0x801A4000** (0x19A58) |
| `.sysctl_set` | 0x801BDA58 (0x150) | **0x801BDA58** (0x150) | **0x801BDA58** (0x150) |
| `.init_array` | 0x801BDBA8 (0x78, 30) | **0x801BDBA8** (**0x7C**, 31) | **0x801BDBA8** (0x7C, thirty-one) |
| its end | 0x801BDC20 | **0x801BDC24** | **0x801BDC24** |
| `.bss` | 0x801BDC40 (0x390D8) | **0x801BDC40** (**0x39118**) | **0x801BDC40** (0x39118) |
| `__bss_end` | 0x801F6D18 | **0x801F6D58** | **0x801F6D58** |
| image | 1825824 | **1825828** | **1825828** |
| headroom | 1086184 | **1086120** | **1086120** |
| `args` | +2064384 | **+2064384** | **+2064384** |
| `topOfKernelData` | +3145728 | **+3145728** | **+3145728** |

Where the new code and data landed, all inside the sections above:

| | predicted | measured |
|---|---|---|
| class `.text` | 0x8017A008 (0x150) | **0x8017A008** (0x150) |
| class `.text.<MetaClass>D0Ev` | 0x8017A158 (0x4) | **0x8017A158** (0x4) |
| `realstubs.o` `.text` | 0x8017A1B4 (0x3FF0) | **0x8017A1B4** (0x3FF0) |
| table `.rodata.str1.1` | +0xBD where the stock one was | **0x8019984B** (0x13F = 0x82 + 0xBD) |
| class `.rodata` | 0x8019F338 (0x440) | **0x8019F330** (0x440) |
| class `.rodata.str1.1` | 0x8019F778 (0x16) | **0x8019F770** (0x16) |
| `realstubs.o` `.rodata.str1.4` | 0x8019F8DC (0x3AE4) | **0x8019F8D4** (0x3AE4) |
| table `.data` word | 0x801BC2A0 (0x4) | **0x801BC2A0** (0x4) |
| class `.init_array` entry | 0x801BDC1C | **0x801BDC1C** (sentinel to 0x801BDC20) |
| class `.bss` | 0x801F4540 (0x18) | **0x801F4540** (0x18) |
| `realstubs.o` `.bss` | 0x801F4580 | **0x801F4580** (0x27C4) |

Three of these rows are worth reading as rules rather than as arithmetic:

* **`__bss_end` moved by 0x40 and not by the object's 0x18** — the pad rule's eighth confirmation, and the first
  one where the pad *grows*. `realstubs.o`'s `.bss` is 64-byte-aligned; `(0 - 0x18) mod 64` = 0x28 of fill, so
  it moves from 0x801F4540 to 0x801F4580, and the end is 0x801F6D04 + `ALIGN(8)` + the 0x10
  `__entry_reset_handler_data` slot = 0x801F6D58.
* **`.init_array` grows by one entry and 332's contract holds for the thirty-first time.** `gMetaClass` is a
  static with a constructor, so the class emits a `.init_array` word; because the object is linked before the
  sentinel, that word is the table's second-to-last and the sentinel's moves to 0x801BDC20 — still last.
  `__entry_init_array_size` is 0x7C.
* **`.data`, `.sysctl_set` and the two offsets do not move at all.** The replacement table keeps a 4-byte `.data`
  word in the same 64-byte slot, and its string lives in the `.text` output section, not `.data`.

## The three misses

**1. The `.rodata`-run rows are 8 bytes low.** Predicted 0x8019F338 / 0x8019F778 / 0x8019F8DC, measured
0x8019F330 / 0x8019F770 / 0x8019F8D4. One cause covers all three: the shift model replays every input's
alignment across the run and comes out 0x8 above the link at that point. Every `.text`-run row is exact, so this
is the fill term again, now measured on the `.rodata` side, and now measured as **0x8** — where 361's reverse
replay was 0x20.

**2. The raw `.text` end moved +0x660 where the model said +0x670 — and the prediction's explanation of the
0x660 was wrong even though its value was right.** `.text` closes with `ALIGN(32)`: 0x801A3710 needed 0x10 of it
and 0x801A3D70 needs 0x10 too, so `__entry_text_end` gains exactly what the raw end gains. The prediction's
"0x801A3D80 does not need the 0x10" was not how it works. The honest statement of the fill term for this step:
the same model is **0x10 high at the raw end of `.text` and 0x8 low inside the `.rodata` run, in one link**.

**3. The `trap_r9_fmt` falsifier's expected value was wrong, and the reason is the useful part.** The panic
string is at **0x8019E02B**, not the predicted 0x8019E486. The `.rodata` run has **two shift regimes** here, and
the boundary between them is the class's own `.rodata` insertion point near the end of the run: inputs before it
move by the shift in force where they sit — `IOPlatformExpert.o`'s `.rodata.str1.1` by **+0x215**, which is the
+0x158 that the table's string position sees (the class's 0x154 plus 4 of alignment) followed by the table's own
0xBD — and inputs after it move by **+0x670**. Lifting the end-of-section shift onto an input in the middle of the
run is wrong by exactly 0x670 − 0x215 = **0x45B**, which is precisely the error above. The falsifier itself never
fired, so what the miss costs is one value for later use: an image that ever traps in `IOPanicPlatform` reports
whichever address *its* map gives, and for this image that is 0x8019E02B.

None of the three falsifiers fired: the stop is not `IORangeAllocator::withRange` (the two virtual calls before
`setMapperRequired` returned what the source says), it is not a name the prediction never mentioned, and it is not
a second panic — `abort_entries=0x00000000` and the log's own last line is the kernel's `No errors detected`.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img`; nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading of either.
`xnu_entry_checks=5` / `xnu_entry_failures=0`. Log 301645 bytes, last line `No errors detected`. The device came
back to Android on its own (`MI 4LTE`, release 10). Nothing in the step is new hardware access: the new code is a
class hierarchy, a plist string and one `.init_array` word, and the code it enables is Apple's, already linked,
and does no I/O.

## What this changes about the walk

Every step from 302 to 362 had the same shape: name the stub the previous run stopped at, find the pool object
that defines it, link it, predict what that object's own body reaches next. 363 is the first step where the
missing thing is not in Apple's tree at all — and the walk's own answer to "which object defines this" was
therefore *none of them; author it*. The frontier that results is still a stub with a name and a caller key, so
the walk's machinery continues to apply; what changed is that the frontier is now inside a driver this project
wrote, and what it needs next comes from the address table of the run that stopped there rather than from any
object's symbol list.
