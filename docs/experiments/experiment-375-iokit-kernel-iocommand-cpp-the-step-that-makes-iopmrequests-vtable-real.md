# Experiment 375 — `iokit/Kernel/IOCommand.cpp`: the step that makes `IOPMRequest`'s vtable real, and a stop that is a power connection's constructor

**Step:** link one object, `iokit/Kernel/IOCommand.cpp` (`iokit_Kernel_IOCommand.o`) — the pool definer of
374's stop `_ZN9IOCommandC2EPK11OSMetaClass` and of the `_ZN9IOCommand4initEv` 374's block had predicted a
step early. It is inserted into the entry link between `iokit_Kernel_IOPMPowerStateQueue.o` and
`stages/stage90/xnu_platform/MSM8974PlatformExpert.o`. Nothing else changes.

**Prediction:** *5 resolved (3 function, 2 storage) / 0 added — 775 → **770** undefined, 673 → **670**
function, 102 → **100** storage*; object `.text` **0x8017F4DC** (0x15C); `.data` and `.sysctl_set`
**unmoved**; `.bss` 0x801C5CC0 (**0x390D8**) with its start unmoved; text size **1744064**; image bytes
**1858700**; and the stop at **`_ZN17IOPowerConnectionC1Ev`** at key **`0x80166228`**.

**Result:** all three counts exact, **every layout row exact to the byte including the text size, the
`.bss` size, the image bytes and the headroom**, the boundary fill reading 0x0 so no band was needed, the
stop's **name** exact and its **key off by 0x4** — the block printed the call site (`0x80166228`) where
every other key in the same block is a return address, and the run reports **`0x8016622C`**. No falsifier
fired.

## The object

```
resolved (5: 3 function, 2 storage)
    _ZN9IOCommand10gMetaClassE                    object B, stand-in was data B 0x18
    _ZN9IOCommand4initEv                          object T, stand-in was func T
    _ZN9IOCommand9metaClassE                      object R, stand-in was data R 0x4
    _ZN9IOCommandC2EPK11OSMetaClass               object T, stand-in was func T
    _ZN9IOCommandD2Ev                             object T, stand-in was func T
added (0: 0 function, 0 storage)
of the 30 references, 30 are already satisfied
```

Five names retired and nothing created: **the whole closure of this object is already real**, so the only
thing that can stop this step's run on its own account is one of the five names it retires. It is also the
walk's first step in a long time that retires **storage**. The two storage retirees are where the walk has
been standing since 374: `IOCommand::gMetaClass` is `data B 0x18` and `IOCommand::metaClass` is
`data R 0x4`.

**And the data-record layout rule, which had been an open 0x3C discrepancy, is now measured rather than
hypothesised**: every one of the 102 data records is a **64-aligned slot of `align64(size)`** inside
`realstubs.o`'s `.bss`. All 102 symbol offsets in that object are multiples of 0x40, the packed end is
0x2744, and that is the section's own size exactly. The two retirees sit at +0x80 and +0xC0 with the
records either side of them — `IODTNVRAM::metaClass` at +0x40 and `IOBufferMemoryDescriptor::metaClass` at
+0x100 — 0x40 away in each direction, so the pair is the whole of `[0x80, 0x100)` and everything after it
moves down 0x80. (The earlier 0x3C gap came from summing all 102 records with the wrong stride; the object's
own symbol table settles it in one command.)

Ten sections, and every allocatable one is a kind this walk has placed before:

```
.text                                          0x15C  2**2   fifteen functions
.text._ZN9IOCommand9MetaClassD0Ev              0x004  2**2   a *separate* input of the same object,
                                                            matched by the `.text.*` wildcard — 373's
                                                            miss, derived rather than located ever since
.rodata                                        0x084  2**2   `_ZTV9IOCommand` and the
                                                            `metaClass`/`superClass` pair
.rodata.str1.1                                 0x00A  2**0
.bss                                           0x018  2**2   `IOCommand::gMetaClass`
.init_array                                    0x004  2**2   one static ctor,
                                                            `_GLOBAL__sub_I_IOCommand.cpp` at
                                                            `.text`+0x108
```

## The stop: the two sites 374 named are retired, and the frontier moves one frame out

374's block predicted `IOCommand::init` and had to correct itself to the *constructor* one call earlier;
375 retires both. `acquirePMRequest` is 90 instructions with six direct calls, and the two stub sites in
it are:

```
acquirePMRequest   0x801658A0
  +0x20  blx r1                              -> `IOPMRequest::MetaClass::alloc`
                                                -> `IOCommand::IOCommand(OSMetaClass const *)`
                                                <- 374's stop, key 0x8017302C
  +0x30  bl 80183528 <_ZN9IOCommand4initEv>  <- key 0x801658D4, 374's prediction
  +0x70  bl 80183528 <_ZN9IOCommand4initEv>  <- key 0x80165914, the second of the pair
```

After this step both `init` sites are real code and the constructor behind the `blx` returns through real
`OSObject::OSObject(OSMetaClass const *)`, so `acquirePMRequest` completes — and so does everything above
it. Measured on the 374 image with `tools/first_stub_call.py`, there is **no stub call anywhere on the
straight line** of `registerPowerDriver` (0x80167290, 7 direct calls), `IORootParent::start`
(0x8015BA14), `IOService::makeUsable` (0x80169B80), `IOService::submitPMRequest` (0x80165A08),
`IOService::changePowerStateToPriv` (0x80169CFC), `IOPMrootDomain::changePowerStateToPriv` (0x80153B54)
or `IOPMrootDomain::start` itself (0x8014DF4C, **88 direct calls, every one real**).

The first stub site the resumed path can reach is therefore one frame further out, in
`IOService::addPowerChild` (0x80166024) — `IOPMrootDomain::start`'s `patriarch->addPowerChild(this)` at
0x8014E76C, the first call after `patriarch->start` returns:

```
+0x1FC  bl OSObject::nw(0x38)                     real  — the connection's storage, 0x38 bytes
+0x204  bl 801833a8 <_ZN17IOPowerConnectionC1Ev>  <- THE STOP
+0x4E8  bl 801832d0 <setAwaitingAckEb>            the second site, 376's if it is reached
+0x4F4  bl 801832a0 <setReadyFlagEb>              the third
```

Its twelve direct calls before the stop are all real (`strncpy`, `getRegistryEntryID`, `safeMetaCast`,
`OSObject::nw`), and the branch to +0x204 is taken because `addPowerChild`'s guard reads **data**, not a
symbol: `if (!initialized || !child->initialized) return IOPMNotYetInitialized;` — and `initialized` is set
on both sides (`PMinit()` inside `IORootParent::start`, and the root domain's own `PMinit()` before 373's
stop) — while `child->getParentIterator(gIOPowerPlane)` yields no child of the patriarch, so `ok` stays
true and `new IOPowerConnection` is the path taken.

`_ZN17IOPowerConnectionC1Ev` is func record **652**, *below* the three records (668, 669, 670) this step
retires, so its body keeps its index and moves only with the section: `0x8017F688 + 0x18 × 652 =
0x801833A8` in 374's image, `0x8017F7E8 + 0x18 × 652 = 0x80183508` in this one. The *call site* does not
move at all: `addPowerChild` and every frame above it sit below 0x8017F4DC, the insertion point.

## What the run measured

```
 xnu_entry_checks=0x00000005              xnu_entry_failures=0x00000000
 xnu_entry_stub_caller_v=0x8016622c       xnu_entry_abort_entries=0x00000000
 xnu_entry_stub_caller_digits=0x0000003e
 xnu_entry_stub_caller_w0=0x36313038 ("8016")   w1=0x63323236 ("622c")
 xnu_entry_abort_first_dfar=0x00000000    xnu_entry_abort_first_pc=0x00000000
 xnu_entry_image_bytes=0x001c5c8c         xnu_entry_bss_start=0x801c5cc0
 xnu_entry_bss_end=0x801fed98             xnu_entry_args_pa=0x80200000
 xnu_entry_top_of_kernel_data=0x80400000  xnu_entry_why_byte=0x00000061
 xnu_entry_checksum=0x907fed91
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN17IOPowerConnectionC1Ev
No errors detected
```

**The name is exact and the key is off by 0x4.** The run reports `0x8016622C`; the `bl` is at
`0x80166228`. Every other key in this block is a return address — `0x801658D4` is `0x801658A0 + 0x30 + 4`,
and falsifier (b) wrote `0x80166510` for the site at `0x8016650C` — and the block's own sentence for this
one wrote the formula correctly, `0x80166024 + 0x204 + 4`, and then printed the middle term as its value.
**Tell: the four-step arithmetic and the number it produces are two claims, and writing them on one line is
not the same as deriving the second from the first.** The rows that would have caught it are three lines
below the sentence: the record index, the call site, and the two return addresses in (b), all of which came
out exactly as written.

What the machine did, in order: `IOPMrootDomain::start` resumed at +0x784 where 373 stopped at +0x780, ran
`IOWorkLoop::addEventSource` (374), built `IORootParent`, `init`ed, `attach`ed and `start`ed it, and inside
`IORootParent::start`'s `registerPowerDriver` reached `acquirePMRequest`; there `IOPMRequest::MetaClass::alloc()`
called the real `IOCommand` constructor, and then both `bl IOCommand::init` sites ran — over an
`IOPMRequest` whose `_ZTV11IOPMRequest` slot 0x38 is now real code, so **the PM request object is functional
for the first time**; `registerPowerDriver` returned, `makeUsable()` ran, `IORootParent::start` returned to
`IOPMrootDomain::start` at +0x80C, and `patriarch->addPowerChild(this)` entered a function this walk had
**never been inside**, ran its whole guard from cold — twelve direct calls and nine `blx` dispatches — and
reached the power-connection layer.

The falsifiers, checked one by one: **(a) yes** — `_ZN17IOPowerConnectionC1Ev`, the predicted name.
**(b) neither** — no `setAwaitingAck` at 0x80166510 and no `setReadyFlag` at 0x8016651C, so the link took
one object and not two. **(c) neither** — no `_ZN9IOCommand4initEv` at 0x801658D4 or 0x80165914, now a
two-way fact: 374 predicted that site and never reached it, 375 retired it, and this run passed through
both. **(d) no stop at any `blx`** — the nine dispatches in `addPowerChild` all landed on real code, and
`abort_entries=0` with `abort_first_pc=0` and `abort_first_dfar=0` says no vtable slot on the path held a
zeroed stand-in. **(e)** `checks=5` / `failures=0`, no `panic` line, no `exception:` line, and the log ends
in the kernel's own `No errors detected`.

## The layout

| | 374 measured | 375 predicted | 375 measured |
|---|---|---|---|
| counts | 775 / 673 / 102 | **770 / 670 / 100** | **770 / 670 / 100** |
| queue `.text` | 0x8017F1C0 (0x318) | unmoved | **0x8017F1C0 (0x318)** |
| its `MetaClassD0Ev` input | 0x8017F4D8 (0x4) | unmoved | **0x8017F4D8 (0x4)** |
| object `.text` | — | 0x8017F4DC (0x15C) | **0x8017F4DC (0x15C)** |
| its `MetaClassD0Ev` input | — | 0x8017F638 (0x4) | **0x8017F638 (0x4)** |
| platform expert `.text` | 0x8017F4DC (0x150) | 0x8017F63C (0x150) | **0x8017F63C (0x150)** |
| its `MetaClassD0Ev` input | 0x8017F62C (0x4) | 0x8017F78C (0x4) | **0x8017F78C (0x4)** |
| last kernel ctor `.text.startup` | 0x8017F630 (0x4) | 0x8017F790 (0x4) | **0x8017F790 (0x4)** |
| rtabi `.text.eabi` | 0x8017F634 (0x54) | 0x8017F794 (0x54) | **0x8017F794 (0x54)** |
| `realstubs.o` `.text` | 0x8017F688 (0x3F18) | 0x8017F7E8 (0x3ED0 = 670 × 0x18) | **0x8017F7E8 (0x3ED0)** |
| its end | 0x801835A0 | 0x801836B8 | **0x801836B8** |
| `.text` run end (= `.rodata` run start) | 0x8018364C | 0x80183764 | **0x80183764** |
| queue `.rodata` / `.rodata.str1.1` | 0x801A5170 / 0x801A5224 | 0x801A5288 (0xB4) / 0x801A533C (0x14) | **both exact** |
| object `.rodata` / `.rodata.str1.1` | — | 0x801A5350 (0x84) / 0x801A53D4 (0xA) | **both exact** |
| platform expert `.rodata` / `.str1.1` | 0x801A5238 / 0x801A5678 | 0x801A53E0 (0x440) / 0x801A5820 (0x16) | **both exact** |
| `.rodata.macho` | 0x801A5690 (0x14C) | 0x801A5838 (0x14C) | **0x801A5838 (0x14C)** |
| `realstubs.o` `.rodata.str1.4` | 0x801A57DC (0x39B8) | 0x801A5984 (0x396C) | **0x801A5984 (0x396C)** |
| its end | 0x801A9194 | 0x801A92F0 | **0x801A92F0** |
| `.text` end | 0x801A9B60 | 0x801A9CC0 | **0x801A9CC0** |
| text size | 1743712 | 1744064 | **1744064** |
| `.data` | 0x801AC000 (0x19AB0) | unmoved | **0x801AC000 (0x19AB0)** |
| `.sysctl_set` | 0x801C5AB0 (0x150) | unmoved | **0x801C5AB0 (0x150)** |
| `.init_array` | 0x801C5C00 (0x88) | 0x801C5C00 (0x8C) | **0x801C5C00 (0x8C)** |
| its end | 0x801C5C88 | 0x801C5C8C | **0x801C5C8C** |
| `.bss` | 0x801C5CC0 (0x39158) | 0x801C5CC0 (0x390D8) | **0x801C5CC0 (0x390D8)** |
| object `.bss` | — | 0x801FC67C (0x18) | **0x801FC67C (0x18)** |
| `realstubs.o` `.bss` | 0x801FC6C0 (0x2744) | 0x801FC6C0 (0x26C4) | **0x801FC6C0 (0x26C4)** |
| `__bss_end` | 0x801FEE18 | 0x801FED98 | **0x801FED98** |
| image | 1858696 | 1858700 (= 0x1C5C8C) | **1858700** |
| headroom | 2101736 | 2101864 (= 0x201268) | **2101864** |
| args / topOfKernelData / tree / window | +2097152 / +4194304 / +6291456 / 8388608 | all unmoved | **all unmoved** |

**The three terms, built from one integer each.** The `.text` run's delta is `+0x15C + 0x4 − 3 × 0x18` =
**+0x118** (one object, two inputs, three stub bodies retired, none created); the `.rodata` run's is
`+0x84 + 0xA − 0x4C` = **+0x42**, where 0x4C is `align4(20+1) + align4(31+1) + align4(17+1)` for
`_ZN9IOCommand4initEv`, `_ZN9IOCommandC2EPK11OSMetaClass` and `_ZN9IOCommandD2Ev` — the *same three* names,
one name slot each, from the same integer the effect tool printed first (371's rule: one name **and** one
body per resolved function). The `.bss` delta is `+0x18 (the object) − 0x18 (the pad gives it up) − 0x80
(the two retired slots)` = **−0x80**.

**The text size came out by both routes and they agree**, which is the row 374's block had to arbitrate.
Route 1, position: the `.rodata` run starts at 0x80183764, the queue's `.rodata.str1.1` ends at 0x801A5350,
and `+0x84 +0xA +fill 0x2 +0x440 +0x16 +fill 0x2 +0x14C` reaches `realstubs.o`'s row at 0x801A5984; its
0x396C ends at 0x801A92F0; the tail is a constant of this image — `+0x4` (`__TEXT,__const`), `fill 0xC` to
the 2\*\*4-aligned `__TEXT, initcode`, `+0x64C`, the three `__TEXT,__os_log` rows `+0x251 +0x3 +0x108`,
`+0x8` (`.ARM.exidx`) — so the raw end is 0x801A9CB0 and `ALIGN(0x20)` closes it at **0x801A9CC0**. Route 2,
delta: 374's 0x801A9B60 + 0x118 + 0x42 = 0x801A9CBA, the same 32-byte line.

**And the boundary fill read 0x0 this time.** That term — the one the block wrote on the section end as a
band, 368 0x4 / 371 0x0 / 372 0x4 / 373 0x0 / 374 0x4 — is the *only* thing in this table that is not
derived from the object, and on this step it absorbed nothing, so the prediction needed no band at all. The
`.text` run and the `.rodata` run met exactly at 0x80183764.

## The next object, named before its run

The frontier is `_ZN17IOPowerConnectionC1Ev`, defined by **`iokit_Kernel_IOPowerConnection.o`** — the only
object in the 695-object pool that defines it, and the definer of twelve more `IOPowerConnection` names
(`setAwaitingAck`, `setReadyFlag`, `getAwaitingAck`, `parentKnowsState`/`setParentKnowsState`,
`getDesiredDomainState`/`setDesiredDomainState`, `childHasRequestedPower`/`setChildHasRequestedPower`,
`parentCurrentPowerFlags`/`setParentCurrentPowerFlags`, `getReadyFlag`) and of the storage stand-in
`IOPowerConnection::metaClass` (R 0x4). Measured against this image: 35 definitions, 224 references (all
satisfied), **14 resolved (13 function, 1 storage) / 0 added** — in 375's image **770 → 756 undefined,
670 → 657 function, 100 → 99 storage**. Same section shape as this step: `.text` **0x1E0** (thirteen
functions), a separate `.text._ZN17IOPowerConnection9MetaClassD0Ev` of 0x4, `.rodata` **0x38C** (the
vtable), `.rodata.str1.1` **0x12**, `.bss` 0x18, `.init_array` 0x4.

**And 376 is the step where a vtable nothing in this image can see arrives with the object that needed
it**: `_ZTV17IOPowerConnection` has no stand-in record at all, because the only thing that would reference
it is the constructor this step retires. That is 374's lesson from the other side — a stub body's own
closure is empty, so a name only becomes *visible* to the linker when a real object needs it.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img` (4878336 bytes, sha256
`ec3fe346f46d1065fe4e9f44883028bbddc5cfb055c15b6528fecfb6ff93d056`); nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading of
either. `xnu_entry_checks=0x00000005` / `xnu_entry_failures=0x00000000`, `checksum=0x907fed91`,
`abort_entries=0` with `abort_first_pc=0` and `abort_first_dfar=0`, no `exception:` line. Log 301638 bytes,
3975 lines, last line `No errors detected`. The device came back to Android on its own (`MI 4LTE`, release
10).

The step's own code is real C++ reached by a constructor chain and two `init` calls over memory the kernel
had already allocated: it reads no device, allocates nothing new, writes no persistent state, and the stop
is a reporting stub that returns to its caller. What it *unlocks* is the first functional `IOPMRequest` —
called `create()`d through a `MetaClass::alloc` whose vtable slot is no longer a zeroed stand-in — and with
it the whole PM request machinery `registerPowerDriver` had been reaching for since 374.
