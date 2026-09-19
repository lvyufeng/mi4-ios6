# Experiment 360 — `IOEventSource.cpp`: the first step whose `.text` shrinks, and a stub address arithmetic refutes

**Step:** link one object, `iokit/Kernel/IOEventSource.cpp` (`iokit_Kernel_IOEventSource.o`) — the only pool
definer of 359's stop `_ZN13IOEventSourceC2EPK11OSMetaClass` and of the six methods 359 created. Nothing else
changes.

**Prediction:** *22 resolved (21 function, 1 storage) / 0 added — 825 → **803** undefined, 715 → **694**
function, 110 → **109** storage;* `.text` **shrinks** for the first time, ending 0x8019F440..0x8019F44E; no
`__DATA` row moves; `.init_array` **0x78** ending 0x801B9B48; `.bss` start **0x801B9B80** with size **0x39198**
unmoved; `__bss_end` **0x801F2D18**; image **1809224**; headroom **1102568**; `args`/`topOfKernelData` unmoved;
and **the stop at `vm_shared_region_init`, called from `kernel_bootstrap_thread` at `0x8000E6C8`, key
`0x8000E6CC`**.

**Result:** every count and every row exact, and **the predicted key fired to the byte** —
`stub_hit=vm_shared_region_init` at `xnu_entry_stub_caller_v=0x8000e6cc`, `abort_entries=0`. The built image had
already printed that `bl` at 0x8000E6C8 in a disassembly taken before the device was touched. The pad rule's
**eighth** confirmation is its wrap branch (`*fill* 0x801F03D8 0x28` = `(0x00 − 0x18) mod 64`), and
`realstubs.o` closed on all three of its sections. One field of the prediction — the stub's own address — was
359's, and it is the field no run has ever checked.

## The object

| `iokit_Kernel_IOEventSource.o` | |
|---|---|
| `.text` | **676** (0x2A4), 37 definitions |
| `.text.*` COMDAT | **4** (one piece) |
| `.bss` | **24** (0x18) — `IOEventSource::gMetaClass` |
| `.rodata` | **180** (0xB4) |
| `.rodata.str1.1` | **14** (0xE) |
| `.init_array` | 4 |
| `.data`, `__DATA,__sysctl_set` | **neither** |
| definitions / references | 22 names retired, none created |

**22 resolved / 0 added** — `gMetaClass` (`B 0x18`) out as a storage stand-in and twenty-one functions out, for
825 → **803** undefined in the **694 function / 109 storage** split. This is the first *subtractive* step since
the walk began linking whole classes: everything the object defines was already a stub, because 359's step had
brought the whole `IOEventSource` family in as stubs when the gate's body named it.

## The stop: the enclosing frame's next call, three instructions apart

```
8000e6b0: bl 800046dc <PE_init_iokit>                 real, the frame the walk had been inside since 355
8000e6c4: bl 80017004 <ml_set_interrupts_enabled>     real (this is `spllo()`, inlined)
8000e6c8: bl <vm_shared_region_init>                  <- STUB, and the stop; recorded lr 0x8000E6CC
8000e6cc: bl <vm_commpage_init>                       <- STUB
8000e6d0: bl <vm_commpage_text_init>                  <- STUB
```

The step's own bodies are all real inside, which is why the walk left the frame it had been sitting in: the
constructor is `bl OSObject::OSObject(OSMetaClass const*)` (0x80120DBC) plus a `str` of `_ZTV13IOEventSource + 8`;
`setAction` is `str r1, [r0, #16]; bx lr`; `init` (object 0x100, 0x78 bytes) is `OSObject::init()`, the virtual
`[vptr + 0x4C]` = `setAction` (0x8017970C in the 359 image, a stub this step makes real), then `IOMalloc(4)`.
So `IOCommandGate::commandGate` runs out, `IOWorkLoop::init` runs out, `PE_init_iokit` returns, and the stop is
the **first stub the enclosing frame calls after the frame returns** — the sixth frontier kind in its plainest
form.

All three of those calls are 0x18-byte stubs in `realstubs.o`'s `.text`, called unconditionally and in that
order, so the recorded return address is the first of them. Reaching it required the whole IOKit start-up to run
real: `StartIOKit` → `IOPlatformExpertDevice::initWithArgs` → `IOWorkLoop::workLoop` → `IOWorkLoop::init`
(`setWorkLoop`, `addEventSource`, `kernel_thread_start` and its whole thread-creation subtree) → back through
`PE_init_iokit` → `rootNub->attach(0)` (which returns on its first instruction, `cmp r1, #0; beq`) →
`record_startup_extensions_function` (a `B 4` nothing ever sets) → `registerService` → `startMatching` →
`_IOServiceJob::startJob`. Every direct call on that path was checked against the image's stub list, and the
virtuals it takes (`getParentEntry`, `platformAdjustService`, `lockForArbitration`, `unlockForArbitration`,
`getProvider`, `startMatching`) were resolved by hand.

The three falsifiers did not fire, and the first is the one worth remembering: **(a)** the *asynchronous* job
`_IOServiceJob::startJob` enqueues with `thread_call_enter` did not reach a stub first — the walk left the IOKit
start-up entirely. (b) No stub hid behind the matching loop's virtuals. (c) No stop came earlier, inside the
step's own newly-real code. 361 and 362 are the two steps that show the first of those was only *early*.

## The layout: every row exact, and `.text` moves down for the first time

| | 359 measured | 360 predicted | 360 measured |
|---|---|---|---|
| `.text` | 0x8019F5C0 | end 0x8019F440..0x8019F44E | **0x8019F460** (last placed input 0x8019F450, fill 0x10) |
| `.data` | 0x801A0000 (0x19980) | 0x801A0000 (unmoved) | **0x801A0000** (0x19980) |
| `.sysctl_set` | 0x801B9980 (0x150) | 0x801B9980 (unmoved) | **0x801B9980** (0x150) |
| `.init_array` | 0x801B9AD0 (0x74, 29) | **0x78**, thirty | **0x801B9AD0** (0x78, thirty) |
| its end | 0x801B9B44 | **0x801B9B48** | **0x801B9B48** |
| `.bss` | 0x801B9B80 | 0x801B9B80 (unmoved) | **0x801B9B80** |
| `.bss` size | 0x39198 | **0x39198** | **0x39198** (233880) |
| `__bss_end` | 0x801F2D18 | 0x801F2D18 (unmoved) | **0x801F2D18** |
| image | 1809220 | **1809224** | **1809224** |
| headroom | 1102568 | 1102568 (unmoved) | **1102568** |
| `args` | +2048000 | +2048000 (unmoved) | **+2048000** |
| `topOfKernelData` | +3145728 | +3145728 (unmoved) | **+3145728** |

The run's own build lines agree in both directions: `803 symbol(s) undefined` with the stub list holding 803
records in the same 694 function / 109 storage split, `__bss_start 0x801b9b80 ... (233880 bytes to 0x801f2d18)`,
`the copied image ends at 0x801b9b48, 56 bytes below __bss_start`, `image bytes 1809224`, `headroom 1102568`.

**`.text` closes `−0x170` against a placed `−0x160`, and the 0x10 that separates them is the section's own
`ALIGN(0x20)`:**

| term | bytes |
|---|---|
| this object's `.text` (0x801758D4, ending 0x80175B78) | **+0x2A4** |
| its one 4-byte `.text.*` COMDAT piece at 0x80175B78 | **+0x004** |
| its `.rodata` (0x8019AC70) | **+0xB4** |
| its string bytes, **placed 0xE of 0xE** (at 0x8019AD24 — nothing merged) | **+0x00E** |
| the align4 pad its string run closes with (0x8019AD32 → 0x8019AD34) — **new this step** | **+0x002** |
| the 21 retired stub bodies | **−0x1F8** |
| the 21 retired name slots | **−0x2E4** |

The block's band said −0x180..−0x172 and it called that band the section end, so it was 2 bytes short of the
closure (it did not know about the align4 pad that closes a mergeable string run) and 0x10 short of the section
end (it did not carry the trailing `ALIGN(0x20)`, which is a real term whenever the placed end is not already
32-aligned — as it is not here and was at 359). Both sub-terms are now measured.

`realstubs.o` is exact on all three of its closed forms: `.text` **0x4110** = 694 × 0x18 (at 0x80175BD4),
`.rodata.str1.4` **0x3C10** = 0x3EF4 − 0x2E4 (at 0x8019AE80), `.bss` **0x2904** = 0x2944 − 0x40 (at 0x801F0400).

## The pad rule's eighth confirmation, entered from a pad of zero

The retired stand-in `IOEventSource::gMetaClass` occupied a whole 0x40 slot at 0x801F03C0 — exactly where 359's
map had `realstubs.o`'s `.bss` starting — and the object's own definition lands there instead: 0x18 bytes at
0x801F03C0, ending 0x801F03D8. `realstubs.o`'s `.bss` moves up one slot to 0x801F0400 and the map prints
**`*fill* 0x801F03D8 0x28`** = `new_pad = (0x00 − 0x18) mod 64`. The row closes to the byte:
`0x39198 − 0x40 (the retired stand-in) + 0x18 (the object) + 0x28 (the fill) = 0x39198`, which is why the size
and `__bss_end` do not move at all:

```
IOWorkLoop.o 0x801F0390 (0x18) / IOCommandGate.o 0x801F03A8 (0x18) / IOEventSource.o 0x801F03C0 (0x18)
  / *fill* 0x28 / realstubs.o 0x801F0400 (0x2904)
```

## One field of the prediction was 359's, and arithmetic refutes it

The block wrote the three stops' addresses as 0x801790DC, 0x80178D4C and 0x80178D64 and called them "0x18-byte
stubs in this image's realstubs region". This image's `realstubs.o` is based at **0x80175BD4**, so a stub body's
address is `base + 0x18k`:

```
0x801790DC − 0x80175BD4 = 0x3508 = 565 × 0x18 + 0x10     off the grid
0x80178D64 − 0x80175BD4 = 0x3190 = 528 × 0x18 + 0x10     off the grid
0x80178D4C − 0x80175BD4 = 0x3168 = 527 × 0x18            on it (by luck or by reading)
```

The first number is 359's: against *that* image's base 0x8017592C, `0x801790DC − 0x8017592C = 0x37B0` = **594 ×
0x18** exactly. The block had carried the 359 map's 594th stub body forward while the base moved 0x2A8 under it.
The cheap check that catches this class — with no run and no map — is `(address − base) mod 0x18 == 0`, and it is
worth writing down because **a predicted stub address is the one field of a prediction this walk has never
verified**: the run reports the *key*, i.e. the caller's return address, and nothing else.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000066   xnu_entry_kv_in_dram=0x0000008a   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x8017a4cc          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x8000e6cc   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=vm_shared_region_init
```

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301633 bytes whose last line is the kernel's own
`No errors detected`, and the device back on Android on its own (`MI 4LTE`, release 10).

## Frontier: 361 — `osfmk/vm/vm_shared_region.c`

`osfmk_vm_vm_shared_region.o` is the pool definer of this step's stop: **13 resolved / 0 added** — 803 → **790**
undefined, 694 → **682 function, 109 → 108 storage** — with `.text` 0x3FFC, `.data` 0x18 plus a `__DATA,__data`
0x18, `.bss` 0x70 and `.rodata.str1.1` 0x85. Because the object's `.text` is 0x3FFC and the section ends at
0x8019F460, `.text` crosses `0x801A0000` and **`.data` steps a whole `ALIGN(0x4000)` to 0x801A4000** — the walk's
second `__DATA` step. The predicted stop was the next stub the boot thread would call after `vm_shared_region_init`
ran real; the run answered with a stop on a different thread entirely (experiment 361).
