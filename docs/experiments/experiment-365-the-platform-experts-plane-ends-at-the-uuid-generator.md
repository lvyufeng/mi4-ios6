# Experiment 365 — `IORangeAllocator.cpp`: the platform expert's plane ends at the kernel's UUID generator

**Step:** link one object, `iokit/Kernel/IORangeAllocator.cpp` (`iokit_Kernel_IORangeAllocator.o`) — the pool
definer of 364's stop `IORangeAllocator::withRange(unsigned long, unsigned long, unsigned long, unsigned long)`.
It is inserted into the entry link between `iokit_Kernel_IOMapper.o` and
`stages/stage90/xnu_platform/MSM8974PlatformExpert.o`. Nothing else changes.

**Prediction:** *2 resolved (1 function, 1 storage) / 0 added — 783 → **781** undefined, 680 → **679** function,
103 → **102** storage;* `.data` **0x801AC000**; `.sysctl_set` **0x801C5A58**; `.init_array` **0x801C5BA8
(0x84)**; `.bss` **0x801C5C40 (0x3909C)**; `__bss_end` **0x801FED18**; image **1846444**; `args` **+2084864**;
and the stop at **`uuid_generate`** at key **`0x8014E540`**.

**Result:** all three counts exact, every row the prediction derived from a **rule** exact, the `.data` claim
**refuted** (one never-performed hex subtraction), and **the stop name and key exactly as predicted** with no
falsifier firing. The run went through everything `IOPlatformExpert::start` had left — `setPlatform`, then all
three dispatches of `PMInstantiatePowerDomains` — and 0x5F0 bytes into `IOPMrootDomain::start`, which is the
point at which the platform expert's plane is behind the walk.

## Why this object, and where the stop actually is

364 stopped at `withRange` from `IOPlatformExpert::start` at key `0x8015D98C`. The effect tool against the 364
image gives the whole step:

```
resolved (2: 1 function, 1 storage)
    _ZN16IORangeAllocator9metaClassE              object R, stand-in was data R 0x4
    _ZN16IORangeAllocator9withRangeEmmmm          object T, stand-in was func T
added (0: 0 function, 0 storage)
of the 37 references, 37 are already satisfied
```

`withRange`'s own body is real from end to end (the object's `.text` 0x9D4, `.rodata` 0xAC,
`.rodata.str1.1` 0x11, `.bss` 0x1C, `.init_array` 4, no `.data` at all), and every direct call
`IOPlatformExpert::start` makes after it is real too. So the stop is **not** in the caller, and the question is
what the caller's *dispatches* reach. Resolving them out of `_ZTV21MSM8974PlatformExpert` (the address-point
table is `_ZTV+8`, so a slot is `+N` from the vptr) names them all:

```
0x8015d8d4 +0x060  this->removeProperty(key)                    slot +0x7c
0x8015d8ec +0x078  this->getProperty(key)                       slot +0x88
0x8015d94c +0x0d8  provider->setProperty(...)                   slot +0x60  IORegistryEntry::setProperty(char const*, OSObject*)
0x8015d95c +0x0e8  _ZNK8OSObject7releaseEv                      slot +0x14
0x8015d9a4 +0x130  this->setProperty(...)                       slot +0x60
0x8015d9c8 +0x154  _ZN16IOPlatformExpert25PMInstantiatePowerDomainsEv      slot +0x354
0x8015d9e0 +0x16c  provider->getProperty(...)                   slot +0x88
0x8015d9fc +0x188  _ZN18IODTPlatformExpert30createSystemSerialNumberStringEP6OSData  slot +0x3c8
0x8015da24 +0x1b0  provider->setProperty(...)                   slot +0x60
0x8015da34 +0x1c0  _ZNK8OSObject7releaseEv                      slot +0x14
0x8015da48 +0x1d4  _ZN18IODTPlatformExpert9configureEP9IOService            slot +0x358
```

`PMInstantiatePowerDomains` (0x8015E004) is the next thing to run, and its last three instructions are the
three dispatches of `root = new IOPMrootDomain`: `init(0)` (slot +0x50), `attach(this)` (slot +0x1c4,
`IOService::attach`), and a tail `bx` to `start(this)` (slot +0x168, which resolves to
`_ZN14IOPMrootDomain5startEP9IOService`). That last one is where the stop is.

## A measurement defect worth carrying: "no stub on the straight-line path" is not "the closure is real"

`tools/xnu_entry_callwalk.py` reports exactly that for `IOPlatformExpert::start`, for
`PMInstantiatePowerDomains` **and** for `IOPMrootDomain::start` — and the first version of this step's
prediction block read it as a measurement that all three closures are clean. It even wrote that
`IOPMrootDomain::start` (0x9D0 bytes) has "not one non-real direct call in it". **It has three.** The tool's
own docstring says why: it cannot follow an indirect call, so a function's instructions *after its first
`blx`* are words it never reads, and no report of its can distinguish "real" from "unread".
`IOPMrootDomain::start` begins with `super::start` and then a `setProperty` dispatch at +0x10, so the tool
gives up inside its first few words and the `bl` at +0x5F0 is invisible to it.

What settles the question is the scan the other way round: **every `bl`/`b` in the image whose target lands
inside `realstubs.o`'s `.text`** (0x8017B778..0x8017F720 in this frame), grouped by caller. `IOPMrootDomain::start`
makes exactly three such calls, and the first is at +0x5F0:

```
8014e53c: bl 8017e718 <uuid_generate>
8014e54c: bl 8017e760 <uuid_unparse_upper>
8014e6cc: bl 8017f528 <_ZN19IOPMPowerStateQueue17PMPowerStateQueueEP8OSObjectPFvS1_zE>
```

The same scan says only **two vtables in the whole image** have a slot pointing into that range —
`_ZTV11IOPMRequest +0x30 -> _ZN9IOCommand4initEv`, and eight slots of `_ZTV24IOCPUInterruptController ->
_ZN21IOInterruptController*` — and `IOPMrootDomain::start` touches neither before +0x5F0, so no indirect
dispatch can stop the run earlier. That is the check that turns "the tools found nothing" into "there is
nothing there", and it is worth keeping as the instrument for every step where the walk crosses a dispatch.

## What the stop means

```
 xnu_entry_why=0x8017ff08          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x8014e540         xnu_entry_kv_dropped=0x00000000
 xnu_entry_abort_entries=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=uuid_generate
```

`why` is the message string again (`why_byte` 0x61 is its `a`), so this is the stub-hit path and not a fault;
`abort_entries` is zero and there is no panic. The key is the `bl` at 0x8014E53C, i.e.
`IOPMrootDomain::start + 0x5F0`. In source it is `IOPMrootDomain.cpp:1200`'s `initializeBootSessionUUID();`,
**inlined into `start`** — the out-of-line copy is still emitted at 0x8014EBEC and is an independent second
caller of the same stub — whose body is

```c
    uuid_generate(new_uuid);
    uuid_unparse_upper(new_uuid, new_uuid_string);          // IOPMrootDomain.cpp:3247-3248
```

The `if (_bootSessionUUID == 0)` guard around it does not survive into the code the linker produced: the store
immediately before the call is `queuedSleepWakeUUIDString = NULL` (line 1199), not the flag, and the call is
straight-line. Everything between the two stops is now real and returning, in source order — 0x5F0 bytes of
`IOPMrootDomain::start`: `super::start(provider)`, the capability properties, `PMinit`'s dictionary work,
`IOLockAlloc`, `OSArray`/`OSSet`/`OSDictionary` construction, thirty-one `OSSymbol::withCString` calls,
`thread_call_allocate`, `sysctl_register_oid`, `PE_parse_boot_argn`.

**The frontier is therefore the kernel's UUID generator**, and the step after this one needs no discovery:
`libkern/uuid/uuid.c:117` defines `uuid_generate`, its pool object `libkern_uuid_uuid.o` is already built by
`tools/build_xnu_arm_kernel.sh`, and it is experiment 366's object — named in the prediction block before the
run that confirmed it.

## What the layout did

| | 364 measured | 365 predicted | 365 measured |
|---|---|---|---|
| counts | 783 / 680 / 103 | **781 / 679 / 102** | **781 / 679 / 102** |
| `.text` end | 0x801A4CE0 | **0x801A5xxx ±0x40** | **0x801A5720** |
| `.data` | 0x801A8000 (0x19A58) | **0x801AC000** (+0x4000) | **0x801A8000** (0x19A58), **unmoved** |
| `.sysctl_set` | 0x801C1A58 (0x150) | 0x801C5A58 (0x150) | **0x801C1A58** (0x150) |
| `.init_array` | 0x801C1BA8 (0x80) | 0x801C5BA8 (0x84, 33) | **0x801C1BA8** (0x84, thirty-three) |
| its end | 0x801C1C28 | 0x801C5C2C | **0x801C1C2C** |
| `.bss` | 0x801C1C40 (0x390D8) | 0x801C5C40 (0x3909C) | **0x801C1C40** (0x390D8) |
| `__bss_end` | 0x801FAD18 | 0x801FED18 | **0x801FAD18** |
| image | 1842216 | 1846444 | **1842220** |
| headroom | 1069800 | 1064360 | **1069800** |
| `args` | +2080768 | +2084864 | **+2080768** |
| `topOfKernelData` | +3145728 | +3145728 | **+3145728** |

And the rows that carry the rules, all measured:

| | measured |
|---|---|
| object `.text` | **0x8017ABF4** (0x9D4), its 0x4 COMDAT at **0x8017B5C8** |
| object `.rodata` / `.rodata.str1.1` | **0x801A0C94** (0xAC) / **0x801A0D40** (0x11) |
| object `.init_array` | **0x801C1C20** (0x4), thirty-third entry, sentinel still last at 0x801C1C28 |
| object `.bss` | **0x801F855C** (0x1C) |
| `realstubs.o` `.text` | **0x8017B778** (0x3FA8 = 679 × 0x18) |
| `realstubs.o` `.rodata.str1.4` | **0x801A12F8** (0x3A74, was 0x3A9C — one fewer name slot) |
| `realstubs.o` `.bss` | **0x801F85C0** (0x2744, was 0x27C4) |
| the `.bss` pad | **`*fill* 0x801f8590 0x30`** |

Two of those are rules rather than arithmetic:

* **The `.bss` pad went 0xC → 0x30** — the pad rule's **thirteenth** confirmation, `(0 − 0x10) mod 64`, and the
  first one that grows back after shrinking (359 seventh, 360 eighth, 361 ninth, 362 tenth, 363 eleventh,
  364 twelfth).
* **`.bss`'s *size* did not change at all** — 0x390D8 before and after — because +0x1C of new object, −0x40 of
  retired storage stand-in and +0x24 of wider pad cancel exactly. That is why `__bss_end`, the image's `args`
  term and `topOfKernelData` are all 364's rows unchanged.

## The one miss is a hex subtraction that was never performed

The prediction claimed 364's text end left "only 0x320 of room under `.data`", so ~0xA40 of new inputs **must**
carry `.data` into the next bucket at 0x801AC000. The true remainder is
`0x801A8000 − 0x801A4CE0 = **0x3320**`, and the step's inputs came to exactly 0xA40 — comfortably inside it, so
`.data` could not move. The text end landed inside its predicted band, so the *model* was fine; what failed was
a hand arithmetic step asserted in the register of a measurement, on the single number the whole `.data`
consequence hung on. It is the same class as 358's "no block reads its own prose" and 363's two-regime shift
lift: **a derived number in a prediction has to be shown, not asserted.**

## How this step was read

Three instruments, and the one that answered was the least clever:

* the effect tool answered *which* object, splitting 2/0 and naming the storage stand-in;
* `_ZTV`-resolution answered *where the dispatch goes*, which the call tools cannot;
* the whole-image scan for `bl`/`b` into the stub range answered *what is there*, and it is the only one of the
  three that could say anything about a function reached entirely through indirect calls.

The vtable-slot scan is worth keeping for a different reason as well: it is the check that would catch a stub
reached with **no `bl` anywhere on the line** — `_ZTV11IOPMRequest +0x30` and the eight
`_ZTV24IOCPUInterruptController` slots are exactly that shape, and one of them will eventually be the stop.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img`; nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading of
either. `xnu_entry_checks=5` / `xnu_entry_failures=0`, `abort_entries=0`, no `exception:` line. Log 301625
bytes, last line `No errors detected`. The device came back to Android on its own (`MI 4LTE`, release 10).
Nothing in the step is new hardware access: the object it adds allocates from the kernel heap and stores no
device state.
