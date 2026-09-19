# Experiment 358 — `IOWorkLoop.cpp`: the step that brings a whole vtable, and a pad that narrows to zero

**Step:** link one object, `iokit/Kernel/IOWorkLoop.cpp` (`iokit_Kernel_IOWorkLoop.o`) — the only pool definer of
357's stop `_ZN10IOWorkLoop8workLoopEv`. Nothing else changes.

**Prediction:** *1 resolved / 3 added — 818 → **820** undefined, 710 → **710** function, 108 → **110** storage;* no
`__DATA` row moves (the closure band is at least 0x1291 below `0x801A0000`), `.init_array` becomes **0x70** ending
0x801B9B40, `.bss` stays at **0x801B9B40** with size **0x39198**, image **1809216**, `args` and `topOfKernelData`
unmoved; and **the stop at `_ZN13IOCommandGate11commandGateEP8OSObjectPFiS1_PvS2_S2_S2_E` at
`IOWorkLoop::init+0xC0`, key `0x80174494`**.

**Result:** every count and **every row exact** — `.text` at the top of its band, `.data`, `.sysctl_set`,
`.init_array`, `.bss`, `__bss_end`, image, headroom, `args` and `topOfKernelData` all as predicted — and **the stop
fired to the byte**: `stub_hit=_ZN13IOCommandGate11commandGateEP8OSObjectPFiS1_PvS2_S2_S2_E` at
`xnu_entry_stub_caller_v=0x80174494` = `IOWorkLoop::init+0xc0`, `abort_entries=0`. The built image had already
placed that `bl` at 0x80174490 before the device was touched, which is the first time this walk's prediction could
be checked against a *disassembly of the thing about to run*. The `.bss` pad rule narrowed from 0x30 to **0x18** as
derived, and `realstubs.o`'s three sections closed exactly. One number in the prediction block was a slip of the
same class 357 found three of — the band's distance to the `0x4000` boundary, written 0x1291 where it is 0x12F1.

## The object: `resolved = 1` is a lower bound

| `iokit_Kernel_IOWorkLoop.o` | |
|---|---|
| `.text` | **3912** (0xF48) |
| `.text.*` COMDAT | **4** (one piece: `IOWorkLoop::MetaClass::~MetaClass`) |
| `.bss` | **24** (0x18) |
| `.rodata` | **212** (0xD4) — the `metaClass`/`superClass` words and **both vtables** |
| `.rodata.str1.1` | **75** (0x4B) |
| `.init_array` | 4 |
| `.data`, `__DATA,__sysctl_set` | **neither** |
| definitions / references | 48 / 68 — 1 name retired, 3 created |

**1 resolved / 3 added** — `workLoop` out (`T 0x6C` in the pool, a `func T` stub in the image); **one function in**
(`IOCommandGate::commandGate`, pool `T 0x7C`) **and two storage stand-ins** (`IOCommandPool::metaClassE`,
`IOInterruptEventSource::metaClassE`, both `R 4`) — for 818 → **820** undefined, **710 → 710 function, 108 → 110
storage**.

**And that count says almost nothing about the step.** The object also defines

- the class's **vtable**, `_ZTV10IOWorkLoop` at `.rodata + 8`, 0x90 bytes, whose 26 function slots name its
  destructors, `init`, `free`, `threadMain`, `addEventSource`, `runEventSources`, `_maintRequest`, `sleepGate`,
  `wakeupGate`, `runAction` and a dozen more;
- `IOWorkLoop::init` (0x128) and `free` (0x1E0) — the two the walk was about to call through that vtable;
- the constructor `IOWorkLoop::IOWorkLoop(OSMetaClass const*)`, three destructors, `workLoopWithOptions`, and
  twenty more bodies;

and **not one of them is a stub in the 357 image, because nothing has ever referenced them directly.** A stub list
is the closure of the walk's *direct* references; these definitions are reachable only through a vtable pointer the
step itself is about to make real. `resolved = 1` counts the one name the walk had already asked for by name, and
this is the first step of the walk where the gap between "what the accounting sees" and "what the step does" is the
step's subject rather than a footnote.

## The stop: the `bl` was in the map before the run

```
stub_hit=_ZN13IOCommandGate11commandGateEP8OSObjectPFiS1_PvS2_S2_S2_E
xnu_entry_stub_caller_v=0x80174494   (also _a and _e, all three agreeing)
caller-4 = 0x80174490  _ZN10IOWorkLoop4initEv+0xbc   <- the `bl`
abort_entries=0x00000000
```

The chain was read from the object and the image together:

```
initWithArgs+0x68   bl IOWorkLoop::workLoop                        (real since this build)
workLoop   +0x08    new IOWorkLoop          -> OSObject::nw         real, kalloc + __bzero
           +0x1C    IOWorkLoop::IOWorkLoop(OSMetaClass const*)       real, this object
           +0x34    OSMetaClass::instanceConstructed                 real
           +0x44    [vptr + 0x30] = vtable slot 12 = IOWorkLoop::init real, this object
           +0x50    (failure branch) [vptr + 0x14] = slot 5 = OSObject::release()
IOWorkLoop::init  +0x08  OSObject::init()                            real, `mov r0, #1; bx lr`
                  +0x28  IOMalloc(0x20)                              real -> kalloc_canblock
                  +0x3C  bzero                                       real
                  +0x4C  IORecursiveLockAlloc                        real -> IOMalloc, lck_mtx_init
                  +0x68  IOSimpleLockAlloc / IOSimpleLockInit        real, call-free
                  +0xA4  thread_set_tag                              real, call-free
                  +0xBC  IOCommandGate::commandGate                  <- STUB, and the stop
```

The vtable slots are the part of this step the tooling cannot see, so they were read off the object's `.rodata`
relocations: the first *function* slot is at `_ZTV + 8`, which makes `.rodata + 0x40` = `IOWorkLoop::init` (slot 12
from the vptr) and `.rodata + 0x44` = `free` (slot 13) — so `workLoop`'s `ldr r1, [r0, #0x30]` is `init` and its
failure branch's `[r0, #0x14]` is `OSObject::release()`, exactly the shape of `IOService`'s vtable at 355
(`vptr + 0x50` = `init`), read the same way. Inside `init`, the four "already have it" tests all fall through
because `OSObject::nw` zeroes the object — it calls `kalloc_canblock` and then `__bzero(r0, size)` at 0x80120D38 —
so the path reaches the gate constructor, which is the only one of `init`'s targets that is not real in the 357
image.

## The layout: every row exact, and most of them had something to be wrong about

| | 357 measured | 358 predicted | 358 measured |
|---|---|---|---|
| `.text` | 0x8019DC80 | end 0x8019ECC4..0x8019ED0F | **0x8019ED00** (the band's high end) |
| `.data` | 0x801A0000 (0x19980) | **0x801A0000** (unmoved) | **0x801A0000** (0x19980) |
| `.sysctl_set` | 0x801B9980 (0x150) | **0x801B9980** (unmoved) | **0x801B9980** (0x150) |
| `.init_array` | 0x801B9AD0 (0x6C, 27) | **0x801B9AD0** (0x70) | **0x801B9AD0** (0x70, 28) |
| its end | 0x801B9B3C | **0x801B9B40** | **0x801B9B40** |
| `.bss` | 0x801B9B40 | **0x801B9B40** | **0x801B9B40** |
| `.bss` size | 0x39118 | **0x39198** | **0x39198** (233880) |
| `__bss_end` | 0x801F2C58 | **0x801F2CD8** | **0x801F2CD8** |
| image | 1809212 | **1809216** | **1809216** |
| headroom | 1102760 | **1102632** | **1102632** |
| `args` | +2048000 | **+2048000** (unmoved) | **+2048000** |
| `topOfKernelData` | +3145728 | **+3145728** (unmoved) | **+3145728** |

Each of those rows had a way to be wrong and was not: `.init_array` grew into a 64-byte alignment without moving
its start (0x6C → 0x70 ends at 0x801B9B40, and `align64` of a 64-aligned address is itself), so the `.bss` start
did not move; the `.bss` size took the pad rule's *narrowing* branch (0x30 → 0x18) and came out unchanged; the
image moved by exactly the four `.init_array` bytes, 1809212 → 1809216; and `args`/`topOfKernelData` stayed put
because `align_up(0x1F2C58, 4096)` and `align_up(0x1F2CD8, 4096)` are the same page — the image grew by 4 bytes
inside a page that was already reserved.

The run's own lines agree: `820 symbol(s) undefined` / `710 function(s), 110 storage`, `__bss_start 0x801b9b40 ...
(233880 bytes to 0x801f2cd8)`, **`the copied image ends at 0x801b9b40, 0 bytes below __bss_start`** — the first step
of the walk where that line reads zero, because `.init_array` now ends exactly on the 64-byte boundary — `image
bytes 1809216`, `headroom 1102632`.

**`.text` closes `+0x1080` against a placed `+0x108F`, so fill −0xF:**

| term | bytes |
|---|---|
| this object's `.text` (0x801742EC, ending 0x80175234) | **+0xF48** |
| its one 4-byte COMDAT piece at 0x80175234 | **+0x004** |
| its `.rodata` (0x8019A274) | **+0xD4** |
| its string bytes, **placed 0x4B of 0x4B** (at 0x8019A348 — nothing merged this time) | **+0x4B** |
| one retired stub body, one created | **+0** |
| retired name slot 0x1C against created 0x40 | **+0x24** |

`realstubs.o` closes on all three of its sections, and two of them are the step's only *independent* measurements of
the name-slot term: `.text` stays at **0x4290** = 710 × 0x18; `.rodata.str1.4` (at 0x8019A4E0) is **0x3E64** =
0x3E40 + 0x24, which is also `sum(align4(len + 1))` over the 710 names of the new stub list; `.bss` is **0x2944** =
0x28C4 + 0x80, the two new stand-ins and no retirement.

## The pad rule's sixth confirmation: 0x30 narrows to 0x18

The object's `.bss` lands at **0x801F0350** — the exact address where 357's map printed `*fill* 0x801F0350 0x30` —
its 0x18 ends at 0x801F0368, and this map prints **`*fill* 0x801F0368 0x18`**. That is
`(old_pad − size) mod 64 = (0x30 − 0x18) mod 64`, the rule 357 derived, now confirmed on a step where the object's
`.bss` is exactly the size of the pad it lands in. The row then closes with no pad term at all:
`0x39118 + 0x18 (the object) + 0x80 (two stand-ins in, one out) + (0x18 − 0x30) = 0x39198`.

## The block's own slip

The prediction said the closure band ends "at least **0x1291** below `0x801A0000`". The band's high end 0x8019ED0F
is **0x12F1** below it — a transposition, and the third time in two steps that a number in a prediction block was
wrong while the step it predicted was exact. The reason is structural and worth restating: **no block ever reads
its own prose.** Every block takes its *base* from the build's own line and only its *delta* from the prediction,
so a false equality or a mistyped distance sits in the ledger indefinitely, and the cheap cure is to evaluate the
arithmetic before writing it down.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000008d   xnu_entry_kv_in_dram=0x000000b1   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x80179d08          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x80174494   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN13IOCommandGate11commandGateEP8OSObjectPFiS1_PvS2_S2_S2_E
```

`tools/host_resolve_entry_addr.sh 0x80174494` → `_ZN10IOWorkLoop4initEv+0xc0`, `caller − 4 = 0x80174490` =
`+0xbc`, the `bl` — and the built image's own disassembly had already placed that instruction at 0x80174490, with
`IOWorkLoop::init` at 0x801743D4 and the object's `.text` at 0x801742EC, exactly where the placement argument said
they would be. The stub the walk stopped on is `T 0x80178f68 18`: an 0x18 body, which is what a created function
stub is.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301672 bytes, and the device back on Android on its own
(`MI 4LTE`, release 10).

## Frontier: 359 — `iokit/Kernel/IOCommandGate.cpp`

`iokit_Kernel_IOCommandGate.o` is the only pool definer of this step's stop `IOCommandGate::commandGate`
(`T 0x7C`): **1 resolved / 6 added** — `commandGate` out, **six functions in**, all `IOEventSource` methods the
gate's own body and vtable name (`wakeupGate`, `checkForWork`, `openGate`, `closeGate`, `sleepGate(Pv,m)`,
`sleepGate(Pv,y,m)`) — for 820 → **825** undefined, **710 → 715 function, 110 → 110 storage**.

The object is `.text` **0x698** with one 4-byte COMDAT piece, `.bss` 0x18, `.rodata` **0xD4**, `.rodata.str1.1`
0x3D, an `.init_array` entry, and again no `.data` and no `__sysctl_set`. Its retired name slot is 0x40 and its six
created slots total 0xD0, so the closure is `+0x878 .. +0x8B5` and `.text` ends at 0x8019F578..0x8019F5B5 — at
least 0xA4B below `0x801A0000`, so **no `__DATA` row moves for the third step running**. `.init_array` becomes
**0x74** (twenty-nine) ending **0x801B9B44**, and this time the `.bss` start *does* move: `align64(0x801B9B44)` =
**0x801B9B80**, +0x40. `.bss`'s size stays **0x39198** (the object's 0x18 replaces the 0x18 pad exactly),
`__bss_end` becomes **0x801F2D18**, image **1809220**, headroom **1102568**, and `args`/`topOfKernelData` stay at
+2048000 and +3145728.

**Predicted stop: `_ZN13IOEventSourceC2EPK11OSMetaClass` at `IOCommandGate::commandGate+0x28`, key
`0x8017534C`.** `commandGate`'s body, read from the object at 0xEC, is
`new IOCommandGate; if (gate && !gate->init(owner, action)) { gate->release(); gate = 0; } return gate;` —
`OSObject::nw` (0x28 bytes) at +0x10, then the **base-class constructor** at +0x24, then the vtable store,
`instanceConstructed`, the virtual at `[vptr + 0x68]` = `_ZTV13IOCommandGate + 0x70` = **slot 26** =
`IOCommandGate::init`, and the failure branch's slot 5 = `OSObject::release()`. The base constructor is a **stub in
the 358 image** (`T 0x18`, because 357's step brought the `IOEventSource` names in as stubs) and `OSObject::nw`
before it is real, so the first stub the walk meets is the second call. With the object's `.text` at **0x80175238**
(where `xnu_arm_entry_last_kernel_constructor.o`'s `.text` sits now), `commandGate` is at 0x80175324 and the
recorded return address is **0x8017534C**. Three named falsifiers: (a) `IOEventSource::init`'s own stub, reached
only if the base constructor is real, which would mean the image and the map disagree; (b) the virtual `init` slot
being reached before the constructor, impossible by offset (+0x54 against +0x24); (c) `OSObject::nw`'s kalloc path,
walked as real since 356 and used by every object the walk has created.
