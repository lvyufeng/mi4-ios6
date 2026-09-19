# Experiment 347 — `IOUserClient.cpp`: the step that creates twenty-one names and moves the stop to none of them

**Step:** link one object, `iokit/Kernel/IOUserClient.cpp` (`iokit_Kernel_IOUserClient.o`, 88856 bytes) — the
only object defining 346's stop, and the largest step since 342. Nothing else changes.

**Prediction:** *7 resolved / 21 added — 823 → **837** undefined, 723 → **736** function, 100 → **101**
storage; `.text` a band, `.data` stepping 0x8000 to `0x8016C000`;* and **the stop at
`_ZN18IOMemoryDescriptor10initializeEv` on `iokit_post_constructor_init+0x24`, key `0x8011b110`.**

**Result:** all three counts exact, every derived address exact, the stop landed **name and key** with
`abort_entries=0` — and `realstubs.o` moved on all three of its sections by exactly the predicted amount, with
no tail artifact, for the first time in the walk. The step creates twenty-one stub names and the frontier
moved to **none** of them.

## The object

| `iokit_Kernel_IOUserClient.o` | |
|---|---|
| `.text` | **35036** (0x88DC), 321 definitions |
| `.group` / COMDAT | 0x48 / **0x18** |
| `.bss` | **160** (0xA0) |
| `.rodata` | **1720** (0x6B8) |
| `.rodata.str1.1` | **1515** (0x5EB) — the largest string input of the walk |
| `.init_array` | 4 (`_GLOBAL__sub_I_IOUserClient.cpp`) |
| definitions / references | **321 / 355**, of which 334 already satisfied |

**7 resolved / 21 added** — six `IOUserClient` methods, `iokit_task_terminate` and the `R 0x4` stand-in
`_ZN12IOUserClient9metaClassE` out; **nineteen functions and two storage stand-ins in** — for
823 + 21 − 7 = **837**, **723 → 736 function, 100 → 101 storage**, 736 + 101 = 837. The two created storage
stand-ins cost 2 × 0x40 of `.bss` rather than their own sizes, which is why this is the first step since 344
with a **positive** `.bss` placed term.

## The check, and why the stop did not move

Twenty-one created names is more than any earlier step of the walk (the previous maximum was 343's one).
342's defect was a pre-run check that asked only "is this target a stub *today*" and therefore could not see
the name the link was about to create; 343's fix was to classify every call site against the stub list **plus
the `added` column**. Here that classification runs over both populations at once and answers:

```
bl sites naming a stub-or-added name: 71      of which in IOUserClient::initialize: 0
```

and the function being retired is not a large one being waved through — `nm -S` says
`_ZN12IOUserClient10initializeEv` is **0x28 bytes** and its whole body makes **two** calls, both `IOLockAlloc`
(real since 328). So the constructor advances **one** call, to a stub that has been next on its line since
341:

```
8011b108: bl <_ZN12IOUserClient10initializeEv>          <- 346's stop, retired by 347
8011b10c: bl <_ZN18IOMemoryDescriptor10initializeEv>    STUB  <- 347's stop, key 0x8011b110
8011b110: bl <_ZN12IORootParent10initializeEv>          STUB
8011b114: bl <_ZN16IOPMinformeeList22getSharedRecursiveLockEv>  STUB
```

`iokit_post_constructor_init` is at 0x8011B0EC, well below the insertion point 0x8013C6A0, so the step moved
neither the function nor the key — which is why the prediction could be an address rather than an offset from
a re-derived base.

## `.text`: eight terms, and the widest band of the walk

| term | bytes |
|---|---|
| this object's `.text` | **+0x88DC** |
| its COMDAT | **+0x018** |
| its `.rodata`, placed whole | **+0x6B8** |
| its string bytes, as placed | **+0x000 .. +0x5EB** |
| the six retired stub bodies | **−0x090** |
| the nineteen created stub bodies | **+0x1C8** |
| the six retired name slots | **−0x104** |
| the nineteen created name slots | **+0x2A4** |

Sum **+0x9284** with no string bytes placed, **+0x986F** with all 0x5EB. The longest created name is 97
characters (`binaryWithCapacity`), whose slot is `align4(98)` = **0x64**, the largest single name slot this
walk has created or retired.

**The end address is a band and `.data` is not.** `.text` closes with `. = ALIGN(0x20)` and the last input in
the 346 image is `_udivmoddi4.o`'s 8-byte `.ARM.exidx` ending at 0x80160FB8 + 8 = **0x80160FC0**, already
32-aligned. So the end is `align32(0x80160FC0 + Q)`, and over the whole band that is 0x8016A260 to
0x8016A840 — every value of which lies between 0x8016A000 and 0x8016C000. So **`.data` steps 0x8000 to
0x8016C000`** for any fill delta up to +0x1780, and the whole fill today is 0xD2A.

| | 346 | 347 measured | 347 predicted |
|---|---|---|---|
| `.text` | 0x80160FC0 (0x160FC0) | **0x8016A820** (0x16A820) | 0x8016A260..0x8016A840 ✓ (near the top) |
| `.data` | 0x80164000 (0x19498) | **0x8016C000** (0x19498, unmoved) | **0x8016C000** ✓ |
| `.sysctl_set` | 0x8017D498 (0x10C) | **0x80185498** (0x10C) | **0x80185498** ✓ |
| `.init_array` | 0x8017D5A4 (0x50, 20) | **0x801855A4** (**0x54**, twenty-one entries) | **0x801855A4** (0x54, 21) ✓ |
| `.bss` | 0x8017D600 (size 0x37BD8, placed +0x000, fill 0x144) | **0x80185600** (size **0x37C98**, placed **+0x0E0**, fill **0x124**) | 0x80185600, placed +0x0E0 ✓ |
| `__bss_end` | 0x801B51D8 | **0x801BD298** | ~0x801BD2B8 (0x20 high) |
| image | 1562100 | **1594872** | **1594872** ✓ |
| headroom | 1355304 | **1322344** | ~1322312 (0x20 low) |

`.text` closed **+0xA860** (0x160FC0 → 0x16A820) with fill 0xD2A → **0xD2E** (+4) across **65** rows both
times, so the placed term is **+0x985C** — inside the predicted band, and it implies **0x5D8 of the object's
0x5EB string bytes were placed**, i.e. 0x13 bytes were merged away. That is the third mechanism 346 found
(`relaxed` against the input's own duplicates) appearing again — and this time inside a band wide enough to
contain it, which is the whole point of a 0x5EB-wide band against 346's 0x20-wide one. `.bss`'s placed term
is exact at **+0xE0** = −0x40 (one retired slot) + 0x80 (two created) + 0xA0 (this object's own `.bss`), and
its start is unmoved relative to `.init_array` for the eighth step running:
`align64(0x801855A4 + 0x54)` = `align64(0x801855F8)` = **0x80185600**.

## `realstubs.o`: all three sections exact for the first time

This is the cleanest measurement of the step, because the step's own arithmetic predicts it with no string
term and no fill term:

| | 346 | 347 measured | predicted |
|---|---|---|---|
| `.text` | 0x43C8 | **0x4500** | **+0x138** = +13 bodies (19 created, 6 retired) ✓ |
| `.rodata.str1.4` | 0x40B3 | **0x4253** | **+0x1A0** = 0x2A4 created slots − 0x104 retired slots ✓ |
| `.bss` | 0x19C4 | **0x1A04** | **+0x40** = 2 × 0x40 created − 1 × 0x40 retired ✓ |

All three exact, and **no tail artifact in any of them** — which is 346's withdrawal holding one step later.
`last_kernel_constructor` went 0x8013C6A0 → **0x80144F94** (+0x88F4 = this object's 0x88DC plus its 0x18
COMDAT).

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000076   xnu_entry_kv_in_dram=0x0000009a   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x80149cd4          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x8011b110   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN18IOMemoryDescriptor10initializeEv
```

`tools/host_resolve_entry_addr.sh 0x8011b110` → **`iokit_post_constructor_init+0x24`**, `caller-4` =
`0x8011b10c: bl 80148e8c <_ZN18IOMemoryDescriptor10initializeEv>` — the name and the key both as written, one
call past 346's. `digits=0x49` with `w0=0x31313038` (`"8011"`) and `w1=0x30313162` (`"b110"`) renders the key
as its own ASCII digits again.

**`abort_entries=0` is what makes the object's entry a measurement rather than an assumption.**
`IOUserClient::initialize` ran both `IOLockAlloc` calls and returned, the constructor advanced one call, and
nothing in the 35036 bytes of new code reachable from that function faulted on a zero.

**The one row that missed is the fill again, and by less than usual**: `.bss`'s fill went 0x144 → **0x124**,
so `__bss_end` and the headroom are 0x20 out. The placed term was exact; only the fill moved. That is the
third step running in which the fill is the only thing between this ledger and an exact row, and each of the
three recorded a different way for it to move — 344 carried it forward, 345 modelled it, 346 assumed it
constant, and here it was **assumed to stay**, the mildest of the four and still wrong.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301649 bytes, and the device back on Android on its own
(`MI 4LTE`, release 10).

## Frontier: 348 — `iokit/Kernel/IOMemoryDescriptor.cpp`

`iokit_Kernel_IOMemoryDescriptor.o` (**59796 bytes**) is the only object defining this step's stop:
**8 resolved / 17 added** — seven functions (`_ZN18IOMemoryDescriptor10initializeEv`,
`_ZN18IOMemoryDescriptor16withAddressRangeEyymP4task`, `_ZN18IOMemoryDescriptor18getPhysicalAddressEv`,
`_ZN18IOMemoryDescriptor19createMappingInTaskEP4taskymyy`, `_ZN11IOMemoryMap15userClientUnmapEv`,
`device_close`, `device_data_action`) plus the `R 0x4` stand-in `_ZN11IOMemoryMap9metaClassE` out; fourteen
functions and three storage stand-ins in — for 837 + 17 − 8 = **846** undefined, **736 → 743 function,
101 → 103 storage** (743 + 103 = 846). It brings `.text` 0x7E6C, a `.group`/COMDAT pair, `.bss` 0x58,
`.rodata` 0x2D8, `.rodata.str1.1` 0x314 and **8 bytes of `.data`** — the first step in a long time that
brings `.data` of its own — and an `.init_array` of its own.

`_ZN18IOMemoryDescriptor10initializeEv` in the 347 image is **six instructions with zero direct calls**, so
it will run to completion and the constructor advances one call to `_ZN12IORootParent10initializeEv`
(`iokit_Kernel_IOPMrootDomain.o`) at `+0x28`, key **0x8011b114**.

One name in its `added` column is worth flagging before it is linked: `upl_get_internal_vectorupl` is
**defined nowhere in the 695-object pool** — it is declared in `osfmk/mach/memory_object_types.h` and
mentioned in `bsd/vfs/vfs_cluster.c` and `osfmk/vm/vm_pageout.c`, but no compiled object defines it. That is
the shape of 329's `__cxa_atexit` and 330's `__dso_handle`: a name a link cannot resolve from the pool, whose
body is supplied by the generator and whose *behaviour* is therefore zero rather than wrong. It is not this
step's stop — the constructor does not call it — but it is the kind of thing that decides a later one.
