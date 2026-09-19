# Experiment 376 — `iokit/Kernel/IOPowerConnection.cpp`: the object that ends `addPowerChild`, and the first stop decided by a value

**Step:** link one object, `iokit/Kernel/IOPowerConnection.cpp` (`iokit_Kernel_IOPowerConnection.o`) — the
pool definer of 375's stop `_ZN17IOPowerConnectionC1Ev` and of twelve more `IOPowerConnection` members. It
is inserted into the entry link between `iokit_Kernel_IOCommand.o` and
`stages/stage90/xnu_platform/MSM8974PlatformExpert.o`. Nothing else changes.

**Prediction:** *14 resolved (13 function, 1 storage) / 0 added — 770 → **756** undefined, 670 → **657**
function, 100 → **99** storage*; object `.text` **0x8017F63C** (0x1E0); `.data` and `.sysctl_set`
**unmoved**; `.bss` **0x801C5CC0 (0x390D8)** with its start, size *and* end unmoved; text size **1744544**;
image **1858704**; headroom **2101864**; and the stop at **`__MALLOC`** at key **`0x80106760`**.

**Result:** all three counts exact, **the stop exact in both name and key**, every layout row below
`.text` exact and unmoved, the text size exact **from the position route while the delta route disagreed
for a reason the block had already identified**, **one miss of 0x4 on three `.rodata` addresses** — the
boundary fill, for the sixth time, absorbed downstream by the `__TEXT,__const`/initcode pad — and **one
row whose size came out 0x2 low**, `realstubs.o`'s `.rodata.str1.4`: the packing rule the block had been
stating as exactly `Σ align4(len+1)` over the function names omits a term, and 376 is the first step
where the omission is visible.

## The object

```
resolved (14: 13 function, 1 storage)
    _ZN17IOPowerConnection12setReadyFlagEb
    _ZN17IOPowerConnection14getAwaitingAckEv
    _ZN17IOPowerConnection14setAwaitingAckEb
    _ZN17IOPowerConnection16parentKnowsStateEv
    _ZN17IOPowerConnection19setParentKnowsStateEb
    _ZN17IOPowerConnection21getDesiredDomainStateEv
    _ZN17IOPowerConnection21setDesiredDomainStateEm
    _ZN17IOPowerConnection22childHasRequestedPowerEv
    _ZN17IOPowerConnection23parentCurrentPowerFlagsEv
    _ZN17IOPowerConnection25setChildHasRequestedPowerEv
    _ZN17IOPowerConnection26setParentCurrentPowerFlagsEm
    _ZN17IOPowerConnection9metaClassE        object R, stand-in was data R 0x4
    _ZN17IOPowerConnectionC1Ev
    _ZNK17IOPowerConnection12getReadyFlagEv  (all of these object T, stand-in was func T)
added (0: 0 function, 0 storage)
of the 224 references, 224 are already satisfied
```

Thirteen names retired, one storage, nothing created — the whole closure already real, so nothing inside
this object can stop the run. The step's real subject is the *call it ends*: `IOService::addPowerChild` has
exactly three stub sites and this object defines all three — the constructor at `+0x204`,
`setAwaitingAck` at `+0x4E8` and `setReadyFlag` at `+0x4F4` — so the function the walk has been standing in
since 375 becomes stub-free in one step.

Same section shape as 375's object:

```
.text                                            0x1E0  2**2   thirteen functions
.text._ZN17IOPowerConnection9MetaClassD0Ev       0x004  2**2   a *separate* input of the same
                                                              object, matched by the `.text.*`
                                                              wildcard
.rodata                                          0x38C  2**2   `_ZTV17IOPowerConnection` plus the
                                                              metaClass/superClass pair
.rodata.str1.1                                   0x012  2**0
.bss                                             0x018  2**2   `IOPowerConnection::gMetaClass`
.init_array                                      0x004  2**2   one static ctor
```

**`_ZTV17IOPowerConnection` has no stand-in record in the 375 image at all** — the stub list carried
thirteen `IOPowerConnection` functions and one `metaClass` R 0x4 and no `_ZTV`, because the only thing that
would ever reference the vtable is the constructor this step retires, and a stub body's own closure is
empty. That is 374's lesson from the other side: a name only becomes visible to the linker when a real
object needs it, so the table this step brings appears in the map for the first time and was never evidence
of anything before.

## The stop: five frames past the object, and a branch decided by a stand-in's zero

With all three of `addPowerChild`'s sites retired, the walk resumes at 0x8016622C, runs the three
`acquirePMRequest` calls (`+0x214`, `+0x22C`, `+0x244`, real since 375), `setAwaitingAck`, `setReadyFlag`
and `submitPMRequests`, and returns to `IOPMrootDomain::start` at **0x8014E76C** (`+0x820`). From there,
every frame's own straight line was measured **in execution order** with `tools/first_stub_call.py`:

```
+0x89C  OSSymbol::withCStringNoCopy      real, and its transitive walk reaches no stub
+0x8E0  IOService::serviceMatching       real, ditto
+0x8E8  IOService::getMatchingServices   real, ditto
+0x93C  IOPMrootDomain::publishFeature   real, ditto
+0x9B0  blx [vptr+0x14]                  = OSObject::release
+0x958..+0x9AC  sysctl_register_oid x8   all eight are the *same* function
+0x9C0  blx [vptr+0x160]                 = IOService::registerService
```

`IOPMrootDomain::start` has **88 direct calls and not one stub site on its straight line**; so does
`IOService::registerService` (0x8012B80C, 122 instructions, nine direct calls, every target real and every
target's own line clean). The only stub site on the path is inside `sysctl_register_oid`:

```
0x80106730 <sysctl_register_oid>:              (73 instructions, 3 direct calls)
+0x08  ldrb  r0, [r0, #14]                     the *third byte* of `oid_kind` - bit 0x40
+0x0C  ldr   r6, [r5]                          oid_parent
+0x10  tst   r0, #0x40
+0x14  bne   0x8010677C                        CTLFLAG_OID2 set -> skip the allocation
+0x2C  bl    0x80181228 <__MALLOC>             <- THE STOP, key 0x80106760
+0x70  ldr   r0, [r6]                          reads through oid_parent = 0 on the malloc path
```

**And which of the eight calls reaches it is decided by data.** The eight oids are `.data` addresses read
out of the instruction stream at +0x958..+0x9AC, and the byte the branch tests is the oid's own storage:

| | address | symbol | `oid_kind` | byte 14 | outcome |
|---|---|---|---|---|---|
| #1 | 0x801C42B0 | `_ZL22sysctl__kern_sleeptime` | 0x83C00005 | 0xC0 | skip |
| #2 | 0x801C42E0 | `_ZL21sysctl__kern_waketime` | 0x83C00005 | 0xC0 | skip |
| #3 | 0x801C4370 | `_ZL25sysctl__kern_willshutdown` | 0xC3C00002 | 0xC0 | skip |
| **#4** | **0x801FCE80** | **`sysctl__kern_iokittest`** | **a storage stand-in — all zeros** | **0x00** | **the `bl __MALLOC`** |
| #5–8 | `.data` | debug_iokit, hw_targettype, consoleoptions, progressoptions | | | |

`sysctl__kern_iokittest` is `data sysctl__kern_iokittest D 0x30` in the stub list (record 523) and it lands
in `realstubs.o`'s `.bss` at 0x801FCE80 — every data record does, `R` and `D` as well as `B`, that object's
`.data` being size 0 — so its 0x30 bytes are **zeros** at run time and its `oid_kind` is 0. The three calls
before it skip the allocation because their oids are real `.data` with `CTLFLAG_OID2`; the fourth takes it
because the stand-in's zero clears the bit.

**This is the walk's first stop whose site was selected by a value rather than by a symbol.** The three
skipped calls still do real work: the `+0x4C` path reads `oid_refcnt` at `+0x28` (1 in each of the three,
read out of the image), falls through, takes `lck_rw_lock_exclusive` — real — and links the oid into the
tree, with no `panic` behind it.

## What the run measured

```
 xnu_entry_checks=0x00000005              xnu_entry_failures=0x00000000
 xnu_entry_stub_caller_v=0x80106760       xnu_entry_abort_entries=0x00000000
 xnu_entry_stub_caller_digits=0x0000002c
 xnu_entry_stub_caller_w0=0x30313038 ("8010")   w1=0x30363736 ("6760")
 xnu_entry_abort_first_dfar=0x00000000    xnu_entry_abort_first_pc=0x00000000
 xnu_entry_image_bytes=0x001c5c90         xnu_entry_bss_start=0x801c5cc0
 xnu_entry_bss_end=0x801fed98             xnu_entry_args_pa=0x80200000
 xnu_entry_top_of_kernel_data=0x80400000  xnu_entry_why_byte=0x00000061
 xnu_entry_checksum=0x907fed8d
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=__MALLOC
No errors detected
```

**Name and key exact, `0x80106730 + 0x2C + 4`, and the digits spell `8010`/`6760`.** The run therefore did
what the block said: the object took, `addPowerChild`'s three sites ran, the walk went back to
`IOPMrootDomain::start` at +0x820, past `withCStringNoCopy`, `serviceMatching`, `getMatchingServices`,
`publishFeature` and `OSObject::release`, and then **three of the eight `sysctl_register_oid` calls
completed on their real oids** before the fourth — the stand-in — took the allocation branch.

The falsifiers, checked one by one: **(a) yes.** **(b) neither** — no `setAwaitingAck` at 0x80166510 and no
`setReadyFlag` at 0x8016651C, so the constructor ran and `addPowerChild` became stub-free exactly as
derived. **(c) resolved as #4** — the site fired and the three calls before it did not, which is the reading
that says the first three oids' `CTLFLAG_OID2` really did send them down the `+0x4C` path. **(d) no stop at
any `blx`** — `abort_entries=0` with both abort addresses zero, so no vtable slot on the path held a zeroed
stand-in; the whole-image scan of function pointers into the stub region finds 28 in this image (eight
`_ZTV24IOCPUInterruptController` slots, twelve in `nocdev`, six in `nobdev`, one in `event_usrreqs`, one in
`sched_average`), none of them on the `IOService`/`IOPMrootDomain` path, and `_ZTV11IOPMRequest+0x38` — the
last one that was — went away in 375. **(e)** five checks, `why_byte` 0x61, no `panic`, `No errors
detected`.

**What the machine did that no previous step had done:** it ran `IOService::addPowerChild` to its *end* —
three `acquirePMRequest`s, the connection's `setAwaitingAck`/`setReadyFlag` and `submitPMRequests`, all real
code over the `IOPowerConnection` this step built — and then it left the power domain altogether and
executed **`libkern`'s `sysctl_register_oid` three times on real oids**, registering three `sysctl`s into
the kernel's own tree. The walk has been inside `IOPMrootDomain`'s machinery since 365; this is the first
step where it ran a system-wide kernel service that is not part of the power domain.

## The layout

| | 375 measured | 376 predicted | 376 measured |
|---|---|---|---|
| counts | 770 / 670 / 100 | **756 / 657 / 99** | **756 / 657 / 99** |
| IOCommand `.text` / its `D0Ev` | 0x8017F4DC (0x15C) / 0x8017F638 (0x4) | unmoved | **unmoved** |
| object `.text` | — | 0x8017F63C (0x1E0) | **0x8017F63C (0x1E0)** |
| its `MetaClassD0Ev` input | — | 0x8017F81C (0x4) | **0x8017F81C (0x4)** |
| platform expert `.text` | 0x8017F63C (0x150) | 0x8017F820 (0x150) | **0x8017F820 (0x150)** |
| its `MetaClassD0Ev` input | 0x8017F78C (0x4) | 0x8017F970 (0x4) | **0x8017F970 (0x4)** |
| last kernel ctor `.text.startup` | 0x8017F790 (0x4) | 0x8017F974 (0x4) | **0x8017F974 (0x4)** |
| rtabi `.text.eabi` | 0x8017F794 (0x54) | 0x8017F978 (0x54) | **0x8017F978 (0x54)** |
| `realstubs.o` `.text` | 0x8017F7E8 (0x3ED0) | 0x8017F9CC (0x3D98 = 657 × 0x18) | **0x8017F9CC (0x3D98)** |
| IOCommand `.rodata` | 0x801A5350 (0x84) | 0x801A53FC (0x84) | **0x801A5400 (0x84)** |
| object `.rodata` | — | 0x801A548C (0x38C) | **0x801A5490 (0x38C)** |
| platform expert `.rodata` | 0x801A53E0 (0x440) | 0x801A582C (0x440) | **0x801A5830 (0x440)** |
| `realstubs.o` `.rodata.str1.4` | 0x801A5984 (0x396C) | 0x801A5DD0 (0x3718) | **0x801A5DD4 (0x3716)** |
| `.text` end | 0x801A9CC0 | 0x801A9EA0 | **0x801A9EA0** |
| text size | 1744064 | 1744544 (position route) | **1744544** |
| `.data` / `.sysctl_set` | 0x801AC000 (0x19AB0) / 0x801C5AB0 (0x150) | unmoved | **unmoved** |
| `.init_array` | 0x801C5C00 (0x8C) | 0x801C5C00 (0x90) | **0x801C5C00 (0x90)** |
| its end | 0x801C5C8C | 0x801C5C90 | **0x801C5C90** |
| `.bss` | 0x801C5CC0 (0x390D8) | 0x801C5CC0 (0x390D8) | **0x801C5CC0 (0x390D8)** |
| object `.bss` | — | 0x801FC694 (0x18) | **0x801FC694 (0x18)** |
| `realstubs.o` `.bss` | 0x801FC6C0 (0x26C4) | 0x801FC700 (0x26C4) | **0x801FC700 (0x26C4)** |
| `__bss_end` | 0x801FED98 | 0x801FED98 | **0x801FED98** |
| image | 1858700 | 1858704 (= 0x1C5C90) | **1858704** |
| headroom | 2101864 | 2101864 (= 0x201268) | **2101864** |
| args / topOfKernelData / tree / window | +2097152 / +4194304 / +6291456 / 8388608 | all unmoved | **all unmoved** |

**The three terms, one integer each.** `.text` run content = `+0x1E0 + 0x4 − 13 × 0x18` = **+0xAC**;
`.rodata`-run content = `+0x38C + 0x12 − 0x254` = **+0x14A**, where 0x254 is `Σ align4(len+1)` over the
*thirteen* retired **function** names — the same integer the effect tool printed first, one name slot *and*
one body per resolved function (371's rule). And the name-string rule, which previous steps had read as
`Σ align4(len+1)` over the function records exactly, is **not quite that**, and this step is the one that
shows it: the strings are packed end to end, each **4-aligned before the next**, so the row is
`Σ align4(len+1)` over all but the *last* name plus the last name's raw `len+1` — **no pad follows the
final string, because the section simply ends**. Only the *function* records have name strings; the 99
data records contribute none, which is why the retired `metaClass` storage slot costs 0x40 of `.bss` and
nothing of `.rodata`. At 375 the last name happened to need no pad, so 0x396C was the whole sum and the
rule looked exact; here the last is `_ZN9IODTNVRAMC1Ev` (`len+1` = 0x12, `align4` = 0x14) and the row is
**0x3716 = 0x3718 − 0x2**, confirmed byte for byte by `readelf -x .rodata.str1.4`, whose last byte
(0x3715) is that string's NUL.

**And `.bss` came out identical in all three of start, size and end.** The object's own 0x18 and the pad's
give-back (0x14 → 0x3C, +0x28) offset each other, and the 0x40 the retired `metaClass` R 0x4 slot frees is
the only net term left... except it is *not* left: `+0x18 + 0x28 − 0x40 = 0`. So the row reads the same as
375 and `__bss_end` — and therefore the image, the args page and the headroom — cannot move. This is the
second step in a row where the pad rule has to be read *forward*, and the first where reading it forward
changes nothing outside `realstubs.o`.

**The two routes to the text size disagreed, and the block named the reason before the run — and half of
that reason was wrong in a way that cancelled.** Position, walked term by term over the map's own rows:
`realstubs.o`'s `.rodata.str1.4` sits at 0x801A5DD4 with the measured 0x3716, so it ends at **0x801A94EA**;
then `*fill* 0x2` — the align4 that `__TEXT,__const` demands — then `__TEXT,__const` 0x4, ending at
0x801A94F0; then `__TEXT, initcode` 0x64C with **pad 0x0** (0x801A94F0 is already 2\*\*4-aligned); then the
three `__TEXT,__os_log` rows (+0x251 +0x3 +0x108) and `.ARM.exidx` (+0x8) — a raw end of **0x801A9EA0**,
already 32-aligned, so `ALIGN(0x20)` closes at the same number. Delta: 375's 0x801A9CC0 + 0xAC + 0x14A =
0x801A9EB6 → `ALIGN(0x20)` → 0x801A9EC0, one 32-byte line too high. **Of the 0x6 of raw-end disagreement,
0x2 is known exactly** — the `.rodata` run term `+0x38C + 0x12 − 0x254` assumes the string row is 0x3718
and the row is 0x3716, so the run's measured content is **0x148** — and the other 0x4 is alignment padding
inside the tail, which a sum of section *contents* cannot see by construction. This is 374's rule ("the
route that predicts the quantity the tool will print is the prediction") with the disagreement *explained*
rather than banded.

**But the block's own walk of the position route had two wrong terms in it, and they cancelled**: it wrote
the row as ending at 0x801A94E8 with `+0x4 (__TEXT,__const)` and a `pad 0x4` to initcode, where the map
says 0x801A94EA, a 0x2 fill, `__TEXT,__const` 0x4 and a **0x0** pad. It used the *predicted* position
(0x801A94E8 is the predicted row 0x801A5DD0 + predicted 0x3718) in a route whose whole point is to predict
from the position the object fixes, and −0x2 on the start with +0x4 of pad ate each other to 0x801A94F0
either way. **The total was right and it was not evidence.**

## The one miss: 0x4 on three `.rodata` addresses, one size 0x2 low, and the `.text` end unmoved by either

The object's `.rodata` is at **0x801A5490**, not 0x801A548C, and the two rows either side moved with it
(IOCommand `.rodata` 0x801A5400 against 0x801A53FC, platform expert 0x801A5830 against 0x801A582C). The
fill is now **sixth in the series**: 368 0x4, 371 0x0, 372 0x4, 373 0x0, 374 0x4,
375 0x0, **376 0x4**. And `realstubs.o`'s `.rodata.str1.4` carries the step's one *size* miss as
well — 0x3716 against the predicted 0x3718, the unpadded final name (see the terms above).

**And the `.text` end is unmoved**, which is 374's finding in a new place: with every `.rodata` address
0x4 high, `realstubs.o`'s `.str1.4` starts at 0x801A5DD4 instead of 0x801A5DD0, and with its size 0x2 low
it ends at 0x801A94EA instead of the predicted 0x801A94E8; the fill before `__TEXT,__const` is then 0x2
where 0x4 was predicted, so `__TEXT,__const` still starts at 0x801A94EC and ends at 0x801A94F0, and the
`align16` before `initcode` costs **0x0** rather than 0x4 — the raw end is 0x801A9EA0 either way. **A
4-byte shift upstream of an `ALIGN` is invisible in the number the tool prints, and so is a 2-byte one
when the pad that would have shown it absorbs exactly that.**

## The next object, named before its run — and it is two objects, because the stop is coupled

`__MALLOC`'s pool definer is **`bsd_kern_kern_malloc.o`** — the only object in the 695-object pool that
defines it, and the definer of `_FREE`, `_FREE_ZONE`, `__MALLOC_ZONE` and `kmeminit` besides. Measured
against this image: 5 resolved (5 function) / 0 added (all 16 references already satisfied) → **756 → 751
undefined, 657 → 652 function, 99 → 99 storage**.

**But linking it alone would not advance the walk: it would turn the stub stop into a data abort.** The
malloc path reads `ldr r6, [r5]` (the oid's `oid_parent`) at `+0x0C`, and at `+0x70` it does `ldr r0, [r6]`
*before* any guard that could bail — so with a zeroed stand-in, making `__MALLOC` real converts the stop at
0x80106760 into an abort at **0x801067A0** with `abort_first_dfar=0`. That is 342's shape exactly: a step
that fixes a symbol and exposes a value. So

**377 links `bsd_kern_kern_malloc.o` *and* `iokit_Tests_Tests.o`** — the second is the pool definer of the
`sysctl__kern_iokittest` storage stand-in (`iokit/Tests/Tests.cpp`, 3 definitions, 2 references, **1
resolved (0 function, 1 storage) / 0 added**), and its oid is a real `.data` 0x30 with `oid_kind =
0xC3C00002`, so `CTLFLAG_OID2` is set and the branch that reaches `__MALLOC` from call #4 is not taken at
all. One step, two objects, one for the symbol and one for the value. `iokit_Tests_Tests.o` brings `.data
0x30`, `.text 0x38` and a `__DATA,__sysctl_set` of 0x4, so 377 is the first step in a long while that moves
`.data` — and `.data` is the bucket `align_up(text_end, 0x4000)`.

**And the frame after the allocation is already named**: the frontier on the other side of
`sysctl_register_oid` is `IOService::registerService(0)` via the `blx` at `IOPMrootDomain::start + 0x9C0` —
`// let clients find us` in the source, the last statement of the function, and the door into IOKit's
service matching. Its straight line is clean; beyond it the frames are virtual dispatches, which is where
the next block will have to look.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img` (4878336 bytes, sha256
`1b02f89a22ed324ad1af42cc6e2be0e9ea24501ea6f8e0b7a43abf4d76200899`); nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading of
either. `xnu_entry_checks=0x00000005` / `xnu_entry_failures=0x00000000`, `checksum=0x907fed8d`,
`abort_entries=0` with `abort_first_pc=0` and `abort_first_dfar=0`, no `exception:` line. Log 301620 bytes,
3975 lines, last line `No errors detected`. The device came back to Android on its own (`MI 4LTE`, release
10).

The step's own code is a constructor and twelve one-line accessors over memory the kernel had already
allocated; `sysctl_register_oid` is real `libkern` code that ran three times over three real `.data` oids,
and the stop is a reporting stub that returns to its caller. What it *unlocks* is the far side of
`IOPMrootDomain::start` — the point where the root domain stops configuring itself and calls
`registerService()` to let the rest of the kernel find it.
