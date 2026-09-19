# Experiment 359 — `IOCommandGate.cpp`: the first stop whose object supplies the metaclass it hands to its own base constructor

**Step:** link one object, `iokit/Kernel/IOCommandGate.cpp` (`iokit_Kernel_IOCommandGate.o`) — the only pool
definer of 358's stop `_ZN13IOCommandGate11commandGateEP8OSObjectPFiS1_PvS2_S2_S2_E`. Nothing else changes.

**Prediction:** *1 resolved / 6 added — 820 → **825** undefined, 710 → **715** function, 110 → **110** storage;*
no `__DATA` row moves (the closure band ends at least 0xA4B below `0x801A0000`), `.init_array` becomes **0x74**
(twenty-nine) ending 0x801B9B44, `.bss` **starts** one 0x40 slot higher at **0x801B9B80** with its size **unchanged**
at 0x39198, image **1809220**, `args` and `topOfKernelData` unmoved; and **the stop at
`_ZN13IOEventSourceC2EPK11OSMetaClass` at `IOCommandGate::commandGate+0x28`, key `0x8017534C`**.

**Result:** every count and **every row exact**, and the key fired to the byte —
`stub_hit=_ZN13IOEventSourceC2EPK11OSMetaClass` at `xnu_entry_stub_caller_v=0x8017534c`, `abort_entries=0`. The
built image had already disassembled that `bl` at **0x80175348** before the device was touched, the second step in
a row where the map printed the key first. The pad rule's **seventh** confirmation is its **first genuine zero** —
the object's 0x18 `.bss` lands exactly in the 0x18 `*fill*` in front of `realstubs.o` and the map prints no fill
there at all — and `realstubs.o` closes on all three of its sections again (`.text` 0x4308, `.rodata.str1.4`
0x3EF4 = 0x3E64 − 0x40 + 0xD0, `.bss` 0x2944). One phrase in 358's own block was corrected in place: it said the
pad rule "predicts a pad of zero" where 358's measurement was 0x18.

## The object

| `iokit_Kernel_IOCommandGate.o` | |
|---|---|
| `.text` | **1688** (0x698) |
| `.text.*` COMDAT | **4** (one piece: `IOCommandGate::MetaClass::~MetaClass`) |
| `.bss` | **24** (0x18) — `IOCommandGate::gMetaClass` |
| `.rodata` | **212** (0xD4) — the `metaClass`/`superClass` words and **both vtables** |
| `.rodata.str1.1` | **61** (0x3D) |
| `.init_array` | 4 |
| `.data`, `__DATA,__sysctl_set` | **neither** |
| definitions / references | 33 / 49 — 1 name retired, 6 created |

**1 resolved / 6 added** — `commandGate` out (pool `T 0x7C`, a `func T 0x18` stub in the 358 image); **six functions
in**, all `IOEventSource` methods the gate's own body and vtable name — `wakeupGate(bool)`, `checkForWork`,
`openGate`, `closeGate`, `sleepGate(void*, unsigned long)` and `sleepGate(void*, unsigned long long, unsigned
long)` — for 820 → **825** undefined, **710 → 715 function, 110 → 110 storage**. Two of the step's three
structural facts are visible in that list: the object adds **no storage stand-in**, so `.bss`'s size is decided by
the pad alone; and all six new names are `IOEventSource` methods, i.e. the step links the class whose base
constructor it is about to call.

## The stop: the map printed the key, and this time the whole chain with it

```
stub_hit=_ZN13IOEventSourceC2EPK11OSMetaClass
xnu_entry_stub_caller_v=0x8017534c   (also _a and _e, all three agreeing)
caller-4 = 0x80175348  _ZN13IOCommandGate11commandGateEP8OSObjectPFiS1_PvS2_S2_S2_E+0x24   <- the `bl`
abort_entries=0x00000000
```

The prediction was written from the object's body at offset 0xEC and from the placement the previous step's map
gave, and the built image states both:

```
80175324 <IOCommandGate::commandGate(OSObject*, int (*)(OSObject*, void*, void*, void*, void*))>:
80175328: mov r6, r0
8017532c: mov r0, #40                      ; sizeof(IOCommandGate)
80175334: bl  80120d0c <OSObject::nw(unsigned long)>            real, `nw` at object +0x10
80175338: movw r7, #936                    ; r7 = 0x801F03A8
80175340: movt r7, #32799                  ;   = IOCommandGate::gMetaClass, this object's own `.bss`
80175348: bl  80179754 <IOEventSource::IOEventSource(OSMetaClass const*)>   <- STUB, the stop
8017534c: movw r0, #43700  / movt r0, #32793 / add r0, r0, #8  ; 0x8019AAB4 + 8 = the vtable's slot 0
80175358: str r0, [r4]
```

The object's `.text` is at **0x80175238** and `commandGate` at **0x80175324** — both exactly where the placement
argument put them, which is what makes the recorded return address 0x8017534C rather than 0x8017534B or 0x8017534D.
`nw` (0x28 bytes, real, `kalloc_canblock` then `__bzero`) is the first call and the base constructor is the second,
so the first stub the walk meets is the second call — the same shape 358's prediction used, now read off a
disassembly of the image that was about to run rather than off the previous image.

**The stop's own body is what the step makes real.** `r7` is loaded with `IOCommandGate::gMetaClass` at
0x801F03A8 — a storage stand-in until this step, and an 0x18 definition in this map — and that pointer is what `r1`
holds when `IOEventSource::IOEventSource(OSMetaClass const*)` is called; four instructions later the code stores
`vtable for IOCommandGate`'s first function slot (0x8019AAB4 + 8) into the new object. The object therefore
supplies both the metaclass and the vtable that its own base constructor call and its own vptr store need, and
both were stand-ins in the 358 image. The failure branch after them (`[vptr + 0x14]` = `OSObject::release()`) was
never reached: `init` is `T 4` in the pool — the header's inline body — and it is called through the vtable only
after the constructor returns.

The three falsifiers did not fire. (a) `IOEventSource::init`'s own stub was not reached: the stop is the
constructor the prediction named, which is the call *before* it. (b) The virtual `init` slot could not have come
first, and the recorded key is `+0x24` rather than `+0x54`. (c) `OSObject::nw`'s kalloc path ran to completion,
since the stop is the *next* call and `abort_entries=0`.

## The layout: every row exact, and the `.bss` start moves without its size moving

| | 358 measured | 359 predicted | 359 measured |
|---|---|---|---|
| `.text` | 0x8019ED00 | end 0x8019F578..0x8019F5B5 | **0x8019F5C0** (band + fill 0xB) |
| `.data` | 0x801A0000 (0x19980) | **0x801A0000** (unmoved) | **0x801A0000** (0x19980) |
| `.sysctl_set` | 0x801B9980 (0x150) | **0x801B9980** (unmoved) | **0x801B9980** (0x150) |
| `.init_array` | 0x801B9AD0 (0x70, 28) | **0x801B9AD0** (**0x74**, 29) | **0x801B9AD0** (0x74, twenty-nine) |
| its end | 0x801B9B40 | **0x801B9B44** | **0x801B9B44** |
| `.bss` | 0x801B9B40 | **0x801B9B80** (`align64(0x801B9B44)`, +0x40) | **0x801B9B80** |
| `.bss` size | 0x39198 | **0x39198** | **0x39198** (233880) |
| `__bss_end` | 0x801F2CD8 | **0x801F2D18** | **0x801F2D18** |
| image | 1809216 | **1809220** | **1809220** |
| headroom | 1102632 | **1102568** | **1102568** |
| `args` | +2048000 | **+2048000** (unmoved) | **+2048000** |
| `topOfKernelData` | +3145728 | **+3145728** (unmoved) | **+3145728** |

Both halves of the interesting row are structural rather than lucky. `.init_array` gained its twenty-ninth
entry, so it ends at 0x801B9B44 and `align64(0x801B9B44)` = 0x801B9B80 moves the whole `.bss` up one 0x40 slot;
`.bss`'s *size* then came out unchanged because the object's 0x18 replaced the 0x18 pad exactly. And
`args`/`topOfKernelData` stayed put because `align_up(0x1F2CD8, 4096)` and `align_up(0x1F2D18, 4096)` are the same
0x801F3000 page — the image grew by 4 bytes *and* the section start moved by 0x40, both inside a page already
reserved.

The run's own build lines agree in both directions: `825 symbol(s) undefined` (and the stub list holds 825
records in the same 715 + 110 split), `__bss_start 0x801b9b80 ... (233880 bytes to 0x801f2d18)`, `the copied image
ends at 0x801b9b44, 60 bytes below __bss_start`, `image bytes 1809220`, `headroom 1102568`.

**`.text` closes `+0x8B5` against a placed `+0x8C0`, so fill +0xB:**

| term | bytes |
|---|---|
| this object's `.text` (0x80175238, ending 0x801758D0) | **+0x698** |
| its one 4-byte `.text.*` COMDAT piece at 0x801758D0 | **+0x004** |
| its `.rodata` (0x8019AAAC) — both vtables and the two 4-byte `metaClass`/`superClass` symbols | **+0xD4** |
| its string bytes, **placed 0x3D of 0x3D** (read from this map at 0x8019AB80 — nothing merged) | **+0x3D** |
| the one retired stub body, the six created | **+0x078** |
| the retired name slot 0x40 against the six created 0xD0 | **+0x090** |

`realstubs.o` is exact on all three of its closed forms, and two of them check the rows above independently:
`.text` **0x4308** = 715 × 0x18 (at 0x8017592C), `.rodata.str1.4` **0x3EF4** = 0x3E64 − 0x40 + 0xD0 (at
0x8019AD0C), and `.bss` **0x2944** at 0x801F03C0 — unchanged, because this step retires no storage stand-in and
creates none.

## The pad rule's seventh confirmation, and this time the zero is real

The object's `.bss` lands at **0x801F03A8**, and in the 358 image that address was the middle of the
`*fill* 0x18` in front of `realstubs.o`'s `.bss` (0x801F0368, shifted +0x40 by the `.init_array` growth). Its own
0x18 ends at 0x801F03C0, exactly where `realstubs.o`'s `.bss` begins, and **the map prints no `*fill*` between
them at all**: `new_pad = (0x18 − 0x18) mod 64 = 0`. The row then closes with the size unmoved:
`0x39198 + 0x18 (the object) + 0x00 (stand-ins) + (0x00 − 0x18) = 0x39198`. The map's `.bss` run reads as one
unbroken chain — `IOWorkLoop.o` 0x801F0390 (0x18), `IOCommandGate.o` 0x801F03A8 (0x18), `realstubs.o` 0x801F03C0
(0x2944) — three inputs and not a byte of fill.

That is the case 358's block named one step early: it said the rule "predicts a pad of zero" where the
measurement was 0x18. The phrase is corrected in place there, and the zero it describes is this step's.

## `why` is a string address, and it is worth writing down once

`xnu_entry_why=0x8017a41c` resolves to nothing useful — `__clzsi2 + 0x584`, past the end of a 0x48-byte symbol —
because `g_why` is a `const char *` holding the address of the message
`a symbol this image does not provide was called`, which lives in `xnu_arm_entry_stubs.o`'s `.rodata.str1.4`
(0x80179EE0 + 0x9AB). `why_byte=0x61` is that string's first letter, presented in every run of this walk as
`0x00000061 'a'`. It moves from step to step for the same reason the stub names do: the string run moves. Nothing
in the ledger depends on it, and reading it as a return address would be a measurement defect of the same family
as the ones this walk keeps finding — **a field whose value is a pointer to a message is not a call site.**

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000075   xnu_entry_kv_in_dram=0x00000099   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x8017a41c          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x8017534c   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN13IOEventSourceC2EPK11OSMetaClass
```

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301648 bytes whose last line is the kernel's own
`No errors detected`, and the device back on Android on its own (`MI 4LTE`, release 10).

## Frontier: 360 — `iokit/Kernel/IOEventSource.cpp`, the definer of the base constructor

`iokit_Kernel_IOEventSource.o` is the only pool definer of this step's stop `_ZN13IOEventSourceC2EPK11OSMetaClass`
and of the six methods this step created, so its effect on the accounting is *subtractive*: **22 resolved (21
function, 1 storage) / 0 added** — the `gMetaClass` storage stand-in (`B 0x18`) and twenty-one functions — for
825 → **803** undefined, 715 → **694 function, 110 → 109 storage**. The object: `.text` **0x2A4** (676), one
4-byte COMDAT piece, `.bss` 0x18, `.rodata` **0xB4**, `.rodata.str1.1` 0xE, an `.init_array` entry, and no
`.data` and no `__sysctl_set`.

**This is the first step of the walk whose `.text` shrinks.** The closure is
`0x2A4 + 0x004 + 0xB4 + (0x000..0x00E) − 21 retired stub bodies (0x1F8) − 21 retired name slots (0x2E4)` =
**−0x180..−0x172**, so `.text` ends at 0x8019F440..0x8019F44E before the fill, against 0x8019F5C0 at 359. Nothing
below it moves: `.data` sits at the next `ALIGN(0x4000)` boundary and `.bss`'s start is `align64(.init_array
end)`. `.init_array` becomes **0x78** (thirty) ending **0x801B9B48**, so `align64(0x801B9B48)` = 0x801B9B80
leaves the `.bss` start where it is; the `.bss` size stays **0x39198** because the 0x40 the retired stand-in frees
pays for the object's 0x18 *and* the `(0x00 − 0x18) mod 64 = 0x28` of new fill exactly; `__bss_end`, headroom,
`args` and `topOfKernelData` are all unmoved, and the image grows by the four `.init_array` bytes to **1809224**.
`realstubs.o` should close at `.text` **0x4110** = 694 × 0x18, `.rodata.str1.4` **0x3C10** = 0x3EF4 − 0x2E4, `.bss`
**0x2904** = 0x2944 − 0x40.

**Predicted stop: `vm_shared_region_init`, called from `kernel_bootstrap_thread` at `0x8000E6C8`, key
`0x8000E6CC`.** The step's own newly-real bodies are all real inside: the constructor is
`bl OSObject::OSObject(OSMetaClass const*)` (0x80120DBC) plus a `str` of `_ZTV13IOEventSource + 8`;
`IOEventSource::init` (object 0x100, 0x78 bytes) is `OSObject::init()`, the virtual `[vptr + 0x4C]` =
`IOEventSource::setAction` (`T 0x8017970C` in the 359 image, a stub this step makes real), then `IOMalloc(4)`
(0x8011B5DC, real); `setAction` and `setWorkLoop`'s non-NULL branch are call-free. So the family runs out clean,
and the walk leaves `IOWorkLoop::init` through `kernel_thread_start`'s whole thread-creation subtree — every
direct call in it real — and returns up through `workLoop` → `initWithArgs` → `StartIOKit`. `StartIOKit` then
takes `rootNub->attach(0)` (which returns on its first instruction, `cmp r1, #0; beq` — no calls), skips
`record_startup_extensions_function` (a `B 4` nothing ever sets), and runs `registerService` → `startMatching` →
`_IOServiceJob::startJob`, whose direct calls and hand-resolved virtuals (`getParentEntry`,
`platformAdjustService`, `lockForArbitration`, `unlockForArbitration`, `getProvider`, `startMatching`) are all
real. `PE_init_iokit` — the frame the walk has been inside since 355 — then returns to
`kernel_bootstrap_thread`, and the three calls after it in `osfmk/kern/startup.c:544..556` are stubs:

```
8000e6b0: bl 800046dc <PE_init_iokit>                 real, the frame the walk is in
8000e6c4: bl 80017004 <ml_set_interrupts_enabled>     real (this is `spllo()`, inlined)
8000e6c8: bl 801790dc <vm_shared_region_init>         <- STUB, and the stop
8000e6cc: bl 80178d4c <vm_commpage_init>              <- STUB
8000e6d0: bl 80178d64 <vm_commpage_text_init>         <- STUB
```

Three named falsifiers: (a) the **asynchronous** job `_IOServiceJob::startJob` enqueues with `thread_call_enter`
runs before `PE_init_iokit` returns, and `IOService::doServiceMatch`'s virtual dispatches reach a stub — its 45
direct calls are all real, so this would need a virtual, which is the class of call this walk's direct-call
tooling cannot see (356); (b) a stub behind one of the synchronous path's virtual dispatches, in particular in
the matching loop (`matchPassive`, `findDrivers`, `invokeNotifier` — all real in their direct calls); (c) a stop
*earlier* than the IOKit start-up finishing, inside this step's own newly-real code, which would mean the image
and the object disagree name by name.
