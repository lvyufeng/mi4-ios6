# Experiment 357 — `IOServicePM.cpp`: the largest retirement, a fill that became a formula, and the question this instrument cannot ask

**Step:** link one object, `iokit/Kernel/IOServicePM.cpp` (`iokit_Kernel_IOServicePM.o`) — the only pool definer of
356's stop `_ZN9IOService6PMfreeEv`. Nothing else changes.

**Prediction:** *69 resolved (68 function, 1 storage) / 32 added (30 function, 2 storage) — 855 → **818** undefined,
748 → **710** function, 107 → **108** storage;* `.text` a band closing between 0x8019D404 and 0x8019DC92, so one
`ALIGN(0x4000)` carries the whole `__DATA` segment four boundaries up — **the largest `__DATA` move of the walk**:
`.data` `0x80190000` → **`0x801A0000`**, `.sysctl_set` **0x801B9980** (0x150), `.init_array` **0x801B9AD0** (0x6C,
twenty-seven), `.bss` **0x801B9B40**, image **1809212**; and **the stop at `_ZN10IOWorkLoop8workLoopEv` at
`initWithArgs+0x6C`, key `0x8015FF94`** — the stub 355 created, now at the far end of two frames this step's object
has to unwind through.

**Result:** all three counts exact, **every row of the layout exact** (the one banded row — `.bss`'s size — came out
at the top of its band), the image exact — and **the stop's name and key fired to the byte**:
`stub_hit=_ZN10IOWorkLoop8workLoopEv` at `xnu_entry_stub_caller_v=0x8015ff94` =
`IOPlatformExpertDevice::initWithArgs+0x6c`, `abort_entries=0`. Three of the block's own numbers were wrong, all
three in the same way — a stated value contradicting the formula written beside it (a `realstubs.o` string section,
its `.bss`, and `topOfKernelData`) — and none of them moved a step, because every block takes its base from the
build's own line and only its delta from the prose. The `.bss` fill, the row that has been this ledger's weakest
since 346, is now **derived**: one modular subtraction reproduces all five of the walk's `.bss` padding
measurements. And 356's open question — which of the two `release` sites freed the IOService — turns out **not to be
answerable by this instrument at all**, and the source says why.

## The object

| `iokit_Kernel_IOServicePM.o` | |
|---|---|
| `.text` | **70564** (0x113A4) |
| `.text.*` COMDAT | **6 × 4** (the five `IOPM*` MetaClass destructors and `IOServicePM`'s) |
| `.bss` | **372** (0x174) |
| `.rodata` | **1084** (0x43C) |
| `.rodata.str1.1` | **2190** (0x88E) |
| `.data` | **200** (0xC8) |
| `__DATA,__sysctl_set` | **16** (0x10) |
| `.init_array` | 4 |
| definitions / references | 422 / 166 — of which 69 names retired and 32 created |

**69 resolved (68 function, 1 storage) / 32 added (30 function, 2 storage)** — the whole IOService power-management
API out (`PMfree`, `PMinit`, `PMstop`, `PMstart`, `PMregisterDevice`, `PMinitCompletion`, the `IOPMinformee*`
helpers, `IOPMallocMemory`, the `changePowerStateTo*` family, the tell/ack chain, …) plus the storage stand-in
`gCanSleepTimeout` (`B 4`); **thirty functions in** (the `IOEventSource` and `IOPowerConnection` APIs, `IOCommand`'s
init and destructor) **and two storage stand-ins** (`IOEventSource::gMetaClass`, `IOCommand::gMetaClass`, both
`B 0x18`) — for 855 → **818** undefined, **748 → 710 function, 107 → 108 storage**. **It is the largest
single-object retirement of the walk**, 69 names against 349's 15 and 355's 8, on the largest `.text` the walk has
linked.

The two new stand-ins are 0x18 bytes each, so 354's correction is exercised again in this step: they cost
`align64(0x18)` = 0x40 each, not their own sizes, and the retired `gCanSleepTimeout` (4 bytes) freed one 0x40 slot.
Net **+0x40** of `.bss` from the stand-ins, and `realstubs.o`'s `.bss` measures exactly `0x2884 + 0x40 = 0x28C4`.

## The stop: a key predicted to the byte, two frames away

```
stub_hit=_ZN10IOWorkLoop8workLoopEv
xnu_entry_stub_caller_v=0x8015ff94   (also _a and _e, all three agreeing)
caller-4 = 0x8015ff90  _ZN22IOPlatformExpertDevice12initWithArgsEPvS0_S0_S0_+0x68   <- the `bl`
abort_entries=0x00000000
```

`tools/host_resolve_entry_addr.sh 0x8015ff94` → **`_ZN22IOPlatformExpertDevice12initWithArgsEPvS0_S0_S0_+0x6c`**.
The prediction was written before the build, from 355's `initWithArgs` (`bl _ZN10IOWorkLoop8workLoopEv` at object
+0x68, both of `initWithArgs`'s branches converging on it), and it named both the symbol and the byte-exact return
address. What makes it a *frame* prediction rather than a name prediction is the distance: this step retires
`IOService::PMfree`, so `IOService::free` runs to completion (every remaining call in it is real: `IOFree` ×3,
`IOLockFree`, one virtual, then a tail-call to `IORegistryEntry::free`), `release` returns to a site inside
`IODeviceTreeAlloc`, that function finishes, `IOService::init(entry, gIODTPlane)` succeeds, and only then does
`initWithArgs` reach `+0x68`. The walk unwound two frames of code this step itself made real and stopped on the
stub the previous step had predicted it would *not* reach.

## The layout: every row exact, three of the block's own numbers not

| | 356 measured | 357 predicted | 357 measured |
|---|---|---|---|
| `.text` | 0x8018C600 | end ..0x8019DC92 | **0x8019DC80** |
| `.data` | 0x80190000 (0x198B8) | **0x801A0000** (0x19980) | **0x801A0000** (0x19980) |
| `.sysctl_set` | 0x801A98B8 (0x140) | **0x801B9980** (0x150) | **0x801B9980** (0x150) |
| `.init_array` | 0x801A99F8 (0x68, 26) | **0x801B9AD0** (0x6C, 27) | **0x801B9AD0** (0x6C, 27) |
| its end | 0x801A9A60 | 0x801B9B3C | **0x801B9B3C** |
| `.bss` | 0x801A9A80 | **0x801B9B40** | **0x801B9B40** |
| `.bss` size | 0x38F58 | 0x3910C + fill | **0x39118** (+0xC) |
| `__bss_end` | 0x801E29D8 | 0x801F2C4C..(+fill) | **0x801F2C58** |
| image | 1743456 | **1809212** | **1809212** |
| headroom | 1168936 | — | **1102760** |
| `args` | 0x801E4000 | **0x801F4000** | **0x801F4000** (+2048000) |
| `topOfKernelData` | 0x80300000 | 0x80400000 | **0x80300000** (+3145728) |

**The step was predicted from the `0x4000` boundary and not from the fill**, and this is why the largest `__DATA`
move of the walk came out exact. `.text` ended at 0x8018C600 with 0x3A00 of room below `0x80190000`, the object's
closure is `+0x10E04` at least, so no fill could have kept `.data` where it was; both ends of the closure band fell
below `0x801A0000`, so one `ALIGN(0x4000)` sent all four `__DATA` rows up by exactly `0x10000`. The step's own map
then shows the object's `.text` at **0x80162F30**, its six COMDAT pieces at **0x801742D4..0x801742EC**,
`xnu_arm_entry_last_kernel_constructor.o` at 0x801742EC, `xnu_arm_entry_rtabi.o`'s 0x54 at 0x801742F0,
`realstubs.o` at 0x80174344 — the chain closes byte for byte — and its `.rodata` at **0x80198674** with its strings
placed at **0x80198AB0**.

**`.text` closes `+0x11680` against a placed `+0x11698`, so fill −0x18:**

| term | bytes |
|---|---|
| this object's `.text` (0x80162F30, ending 0x801742D4) | **+0x113A4** |
| its six 4-byte `.text.*` COMDAT pieces | **+0x018** |
| its `.rodata` (0x80198674) | **+0x43C** |
| its string bytes, **placed 0x87C of 0x88E** (read from this map at 0x80198AB0) | **+0x87C** |
| `realstubs.o`'s `.text`: 748 × 0x18 → 710 × 0x18 (68 retired, 30 created) | **−0x390** |
| `realstubs.o`'s `.rodata.str1.4`: 0x448C → 0x3E40 (68 name slots out, 30 in) | **−0x64C** |

The last two rows are *measured*, not modelled: `realstubs.o`'s `.text` is exactly the function count times `0x18`
in both images, and its string run is exactly `sum(align4(len + 1))` over the current stub list — reading the 710
function records of `out/stage90/xnu_arm_entry_stubnames.txt` gives **0x3E40** on the nose. That is what makes the
block's `.rodata.str1.4` prediction of 0x43F0 a slip rather than a modelling error: **the closed form printed
beside it evaluates to the measured 0x3E40**. Same for `.bss`: the block wrote `0x2884 = 0x2884 − 0x40 + 0x80`,
whose right-hand side is 0x28C4, which is what the build printed. And `topOfKernelData` is 0x80300000, not
0x80400000: with `args` at 0x801F4000, `align_up(0x801F4000 + 0x1000 + 0x100000, 0x100000)` = 0x80300000.

None of the three moved a step, which is exactly why a false equality can sit in a block indefinitely: nothing reads
it, because every block takes its *base* from the build's own line and only its *delta* from the prose. The tell is
cheap and it is new to the ledger: **a closed form written out with both sides is self-checking — evaluate it.**

## The `.bss` row: absorption is a modular subtraction

The object's `.bss` lands at **0x801F01DC** and ends at 0x801F0350; `realstubs.o`'s `.bss` follows at **0x801F0380**,
and the map prints `*fill* 0x801F0350 0x30` where 356's same fill was 0x24. One line reproduces every `.bss`
padding measurement this walk has made:

```
new_pad = (old_pad - object_size) mod 64
```

| step | object `.bss` | old pad | predicted | measured |
|---|---|---|---|---|
| 353 | 0x20 | 0x30 | 0x10 | 0x10 |
| 354 | 0x20 | 0x30 | 0x10 | 0x10 |
| 355 | 0x94 | 0x10 | 0x3C | 0x3C |
| 356 | 0x58 | 0x3C | 0x24 | 0x24 |
| 357 | 0x174 | 0x24 | 0x30 | 0x30 |

The pad is `align64(end) − end`, so inserting `s` bytes at the end moves it by `−s mod 64`: it shrinks while `s` is
smaller than the old pad and wraps past zero when it is not. That is the whole of what this ledger had been writing
as separate cases — "absorbed 0x20 of a 0x30 gap" (353, 354), "absorbed nothing and the gap *grew*" (355, where
0x10 − 0x94 wraps to 0x3C), "absorbed 0x18 of a 0x3C gap" (356). The row then closes to the byte:
`0x38F58 + 0x174 (the object) + 0x40 (two stand-ins in, one out) + 0xC (the pad, 0x24 → 0x30) = 0x39118`.

## 356's question: not open, but unanswerable by this instrument

The source of the loop decides it:

```c
child = MakeReferenceTable( dtChild, freeDT );
child->attachToParent( parent, gIODTPlane );
AddPHandle( child );
if( kSuccess == DTEnterEntry( &iter, dtChild)) { stack->setObject( parent); parent = child; }
child->release();                                 // only registry holds retain
...
parent->attachToParent( IORegistryEntry::getRegistryRoot(), gIODTPlane);
parent->release();
```

Both releases free **only when the matching `attachToParent` did not take a retain** — the retain is the parent's
child-set `OSArray::setObject`, and `attachToParent` returns `makeLink(...) & parent->attachToChild(...)`. So 356's
stop says "an attach did not retain" and nothing finer. 357's record cannot separate site (ii) from site (iii) *even
in principle*: everything else on that path is real, so a per-child release mid-loop leaves the loop running, the
next child's release reaches the **same** now-real `IOService::free`, and the tail release reaches it too — the two
continuations reconverge long before the next stub, which is `initWithArgs+0x68`'s real `IOWorkLoop::workLoop`, the
stop this step recorded. **A stop is named by the first stub the walk meets, so two sites whose continuations
reconverge before the next stub are indistinguishable to it.**

That also retires 357's own falsifier (a) as written — "the release site was (ii), so the walk resumes in the middle
of the tree and meets the next stub in the loop" — because the loop's remainder holds no stub: the falsifier assumed
what 356's own 79-target walk had already refuted. Of the other two: not `initWithArgs+0x7C`, and not a stub inside
`IODTMapInterruptsSharing`; the walk left `IODeviceTreeAlloc` through its tail.

What the run does add is that the free *happened* and the walk survived it. Both survivors use the released object
afterwards — `AddPHandle(child)` for (ii), `parent->setProperty(...)`/`return parent` for (iii) — and
`abort_entries=0` says the freed memory stayed readable, which is consistent with `IOFree` returning the buffer to a
zone without poisoning it. The record is a record of two sites, and it is written that way.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000006b   xnu_entry_kv_in_dram=0x0000008f   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x80178dbc          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x8015ff94   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN10IOWorkLoop8workLoopEv
```

The run's own build lines agree with the table in both directions: `818 symbol(s) undefined` /
`710 function(s), 108 storage`, `__bss_start 0x801b9b40 ... (233752 bytes to 0x801f2c58)`, `the copied image ends
at 0x801b9b3c, 4 bytes below __bss_start`, `image bytes 1809212`, `headroom 1102760 bytes below topOfKernelData`.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301638 bytes, and the device back on Android on its own
(`MI 4LTE`, release 10).

## Frontier: 358 — `iokit/Kernel/IOWorkLoop.cpp`, and `resolved = 1` as a lower bound

`iokit_Kernel_IOWorkLoop.o` is the only pool definer of this step's stop `_ZN10IOWorkLoop8workLoopEv`:
**1 resolved / 3 added** — `workLoop` out (`T 0x6C` in the pool, a `func T` stub in the image); **one function in**
(`_ZN13IOCommandGate11commandGateEP8OSObjectPFiS1_PvS2_S2_S2_E`, pool `T 0x7C`) **and two storage stand-ins**
(`_ZN13IOCommandPool9metaClassE`, `_ZN22IOInterruptEventSource9metaClassE`, both `R 4`) — for 818 → **820**
undefined, **710 → 710 function, 108 → 110 storage**.

**The stub accounting under-reports this step, and that is the step's point.** The object also defines the class's
vtable (`_ZTV10IOWorkLoop` at `.rodata + 8`, 0x90), the constructor and three destructors, `IOWorkLoop::init`
(0x128), `free` (0x1E0), `workLoopWithOptions`, `threadMain`, `addEventSource`, `runEventSources`, `_maintRequest`
and twenty more — **and not one of them is a stub in the image, because nothing has ever referenced them
directly.** They are reached only through the vtable this step is about to make real. A stub list is the closure of
the walk's *direct* references, so an object that owns a class's vtable drags in a whole family of definitions the
accounting cannot see; `resolved = 1` counts the one name the walk had already asked for by name.

The object's `.text` is 0xF48 with one 4-byte COMDAT piece, `.bss` 0x18, `.rodata` 0xD4 (the two 4-byte
`metaClass`/`superClass` symbols and both vtables), `.rodata.str1.1` 0x4B, an `.init_array` entry, and no `.data`
and no `__sysctl_set`. Its one retired name slot is 0x1C and its one created slot 0x40, so the closure is
`+0x1044 .. +0x108F`, `.text` ends at 0x8019ECC4..0x8019ED0F — at least 0x1291 below `0x801A0000`, so **no `__DATA`
row moves** — `.init_array` grows to 0x70 (twenty-eight) ending 0x801B9B40, `.bss` starts where it already does,
`.bss` size becomes `0x39118 + 0x18 + 0x80 − 0x18 = 0x39198` (the same pad rule, narrowing 0x30 → 0x18),
`__bss_end` 0x801F2CD8, image 1809216, `args` and `topOfKernelData` unmoved.

**Predicted stop: `_ZN13IOCommandGate11commandGateEP8OSObjectPFiS1_PvS2_S2_S2_E` at `IOWorkLoop::init+0xC0`, key
`0x80174494`.** `initWithArgs+0x68` calls the now-real `workLoop`, which is
`new IOWorkLoop; if (wl && !wl->init()) { wl->release(); wl = 0; }`: `OSObject::nw` at object +0x218 (which calls
`kalloc_canblock` and then `__bzero(r0, size)` at 0x80120D38, so the new object's ivars are zero), the constructor
at +0x22C, `instanceConstructed` at +0x244, then the virtual at `[vptr + 0x30]` = `_ZTV10IOWorkLoop + 0x38` =
**slot 12** = `_ZN10IOWorkLoop4initEv` (the first virtual slot is at `+8`, so `.rodata + 0x40` is `init` and
`+0x44` is `free`), and the failure branch's `[vptr + 0x14]` = `_ZTV + 0x1C` = slot 5 =
`_ZNK8OSObject7releaseEv`. Inside `init` the four "already have it" tests all fall through and it reaches
`bl IOCommandGate::commandGate` at object +0x1A4 — **the only target of `init` that is not real in the 357 image**
(`IOMalloc` → `kalloc_canblock` + `OSAddAtomic`; `IORecursiveLockAlloc` → `IOMalloc` + `lck_mtx_init`;
`IOSimpleLockAlloc`, `IOSimpleLockInit` and `thread_set_tag` are call-free; `kernel_thread_start` is `T 0xBC` and
comes after). With the object's `.text` at 0x801742EC, `init` is at 0x801743D4 and the recorded return address is
**0x80174494** = `init + 0xC0`. Three named falsifiers: (a) the `blx` pair *after* the gate is created (+0x1B4 and
+0x1C4 — the new command gate's own virtual slot and `IOWorkLoop`'s `addEventSource`), which cannot be reached
before the stop, so a stop there would mean the stop was *missed* rather than moved; (b) `init`'s 0x16C branch
taken because the object was not zeroed, which carries the walk to `kernel_thread_start` with no stub in between —
refuted by the `__bzero`, but it is the branch that would do it; (c) a stub inside the kalloc path
(`kalloc_canblock` → `zalloc_canblock_tag`), which 356 walked as real.
