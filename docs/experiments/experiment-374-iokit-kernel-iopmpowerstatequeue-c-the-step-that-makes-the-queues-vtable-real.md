# Experiment 374 — `iokit/Kernel/IOPMPowerStateQueue.cpp`: the step that makes the queue's vtable real, and misnames the last call in the right frame

**Step:** link one object, `iokit/Kernel/IOPMPowerStateQueue.cpp`
(`iokit_Kernel_IOPMPowerStateQueue.o`) — the object the walk has been *stopped inside* since 365, and the
pool definer of 373's stop `IOPMPowerStateQueue::PMPowerStateQueue(OSObject *, void (*)(OSObject *, ...))`.
It is inserted into the entry link between `osfmk_prng_fips_sha1.o` and
`stages/stage90/xnu_platform/MSM8974PlatformExpert.o` — at the *end* of `LINK_OBJS`, so every `.text`
address the resumed code runs is below the insertion point and does not move. Nothing else changes.

**Prediction:** *2 resolved (2 function) / 0 added — 777 → **775** undefined, 675 → **673** function,
102 → **102** storage*; object `.text` **0x8017F1C0** (0x318); every row of the `.text` run derived rather
than located (373's miss); `.data` and every address below `.text` **unmoved**; `.init_array` 0x84 → 0x88;
`.bss` *size* 0x39118 → 0x39158 with its start unmoved; and the stop at **`_ZN9IOCommand4initEv`** at key
**`0x801658D4`**.

**Result:** all three counts exact, every `.text` row exact including the one 373 missed, every row below
`.text` exact, the `.bss` pad rule exact, and the stop **the right frame one call early** — the run reports
`_ZN9IOCommandC2EPK11OSMetaClass` at key **`0x8017302C`**, the *constructor*, whose name was not in the
prediction at all. **One miss, of 0x4, on the object's `.rodata` row** — the boundary fill, for the fifth
time.

## The object

```
resolved (2: 2 function, 0 storage)
    _ZN19IOPMPowerStateQueue16submitPowerEventEjPvy               object T, stand-in was func T
    _ZN19IOPMPowerStateQueue17PMPowerStateQueueEP8OSObjectPFvS1_zE object T, stand-in was func T
added (0: 0 function, 0 storage)
of the 47 references, 47 are already satisfied
```

Two names retired and nothing created: **the whole closure of this object is already real.** All 47
references — `OSObject::nw` (at 56 and at 112 bytes), `IOEventSource::IOEventSource(OSMetaClass const *)`,
`OSMetaClass::instanceConstructed`, `IOMalloc` (0x8011B5DC), `IOLockAlloc` (0x8011CD74, defined by
`iokit_Kernel_IOLocks.o`), `lck_mtx_lock`/`lck_mtx_unlock`, `IOEventSource::signalWorkAvailable` and
`OSObject::release` — are definitions in this image and not stand-ins, so **nothing inside this object can
stop the run**. It is also the pool definer of the *second* stub its own header names, `submitPowerEvent`,
which `IOPMrootDomain::registerInterest` reaches later in the same frame, so one step retires both.

Six allocatable inputs, and the alignments are the ones the layout arithmetic needs:

```
.text                                          0x318  2**2
.text._ZN19IOPMPowerStateQueue9MetaClassD0Ev   0x004  2**2   a *separate* input of the same object
                                                            matched by the `.text.*` wildcard
.rodata                                        0x0B4  2**2   two vtables, `metaClass`/`superClass`
.rodata.str1.1                                 0x014  2**0
.bss                                           0x018  2**2   `IOPMPowerStateQueue::gMetaClass`
.init_array                                    0x004  2**2   one static ctor,
                                                            `_GLOBAL__sub_I_IOPMPowerStateQueue.cpp`
                                                            at `.text`+0x2C4
```

The `.text` holds fifteen real functions including `init(OSObject *, Action)` (`+0x164`, which is
`IOEventSource::init(owner, action)` then `IOLockAlloc`) and `checkForWork()` — and **the empty `.text` of
an object with a static constructor is what makes `.init_array` grow for the first time since 364**.

## The stop: this step's real subject is a vtable, not a symbol

The constructor is entered at `IOPMrootDomain::start+0x780` and its body never leaves real code:

```
+0xE8  push {r4,r5,r6,r7,fp,lr}
+0xF8  bl OSObject::nw(0x38)                              real
+0x10C bl IOEventSource::IOEventSource(OSMetaClass const*) real
+0x124 bl OSMetaClass::instanceConstructed               real
+0x13C blx r3 = vtable+0x38 = the queue's own init(...)  real, in this object
+0x154 blx r1 = vtable+0x14 = OSObject::release          real, only if init returned 0
```

The **indirect** pair is what this step buys: before it, the queue's vtable is not in the image, so those
two `blx` go through a *storage stand-in of that size* — zeros — and `blx r3` on a zero is a jump to 0.
That is 365's kind 9. So the constructor returns the queue, `IOPMrootDomain::start` resumes at **`+0x784`**,
and the first stub the resumed code can reach is nine frames away:

```
+0x788  blx [[gIOPMWorkLoop]]+0x4C = IOWorkLoop::addEventSource(queue)  (0x801747E4)
            -> IOCommandGate::runCommand -> IOCommandGate::runAction
            -> IOWorkLoop::closeGate / onThread (false: boot thread) / sleepGate
            -> the PM work loop thread runs IOWorkLoop::_maintRequest (179 instructions, **zero direct
               calls** - every IOEventSource access inlined) whose 17 blx are all on the *queue's* vtable
+0x79C  new IORootParent (0x58), IOService(OSMetaClass const*), vptr, instanceConstructed    real
+0x7E0  patriarch->init(0)  = IOService::init(OSDictionary*)                                 real
+0x7F4  patriarch->attach(this) = IOService::attach(IOService*)   nine indirect sites, all real
+0x808  patriarch->start(this) = IORootParent::start(IOService*) (0x8015BA14)
            IOService::start(nub) = `mov r0,#1; bx lr`
            attachToParent(getRegistryRoot(), gIOPowerPlane)                                 real
            PMinit()                       sets `initialized = 1`
            registerPowerDriver(this, patriarchPowerStates, 2)    0x80167290
                +0x30C  IOService::acquirePMRequest(this, kIOPMRequestTypeRegisterPowerDriver)
                        0x801658A0, and `IOPMRequest::create()` is *inlined* into it:
                        +0x20  blx  IOPMRequest::gMetaClass.alloc()     "real"
                        +0x30  bl   IOCommand::init                     <- THE STOP (predicted)
```

**Predicted: `stub_hit=_ZN9IOCommand4initEv` at caller key `0x801658D4`** — the return address of that
`bl`, `0x801658A0 + 0x30 + 4`; `acquirePMRequest`, `registerPowerDriver`, `IORootParent::start` and
`IOPMrootDomain::start` all sit below the insertion point and do not move.

The three conditions between the constructor and that `bl` are all *data*, and all three hold:
`initialized` set by `PMinit()`; `patriarchPowerStates` two `{1,0,ON_POWER,…}` entries in `.data` at
0x801C4588; and `powerStatesVersion` ≤ 2. Each was a named alternative in the block's falsifier list, and
falsifier (a) below is the one that reads them.

## What the run measured

```
 xnu_entry_checks=0x00000005              xnu_entry_failures=0x00000000
 xnu_entry_stub_caller_v=0x8017302c       xnu_entry_abort_entries=0x00000000
 xnu_entry_stub_caller_digits=0x00000043
 xnu_entry_stub_caller_w0=0x37313038 ("8017")   w1=0x63323033 ("302c")
 xnu_entry_abort_first_dfar=0x00000000    xnu_entry_abort_first_pc=0x00000000
 xnu_entry_image_bytes=0x001c5c88         xnu_entry_bss_start=0x801c5cc0
 xnu_entry_bss_end=0x801fee18             xnu_entry_args_pa=0x80200000
 xnu_entry_top_of_kernel_data=0x80400000  xnu_entry_why_byte=0x00000061
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN9IOCommandC2EPK11OSMetaClass
No errors detected
```

**The frame is exactly the one the block derived, and the call inside it is not.** The stop is
`IOCommand`'s own constructor at caller key **0x8017302C**, which is
`IOPMRequest::IOPMRequest(OSMetaClass const *)`+0x90 (0x80172F9C + 0x90) — the base-constructor call
inside `IOPMRequest`'s constructor, which is itself what `IOPMRequest::MetaClass::alloc()` runs. Everything
above it happened as written: the queue was built (both its `blx` found a real vtable this time),
`IOWorkLoop::addEventSource` ran — so the command gate's `closeGate`/`onThread`/`sleepGate` sequence and
the PM work loop thread's `_maintRequest` both completed — the new `IORootParent` was constructed,
`init`ed, `attach`ed and `start`ed, `PMinit()` ran, and `registerPowerDriver` reached
`IOService::acquirePMRequest` with all three data conditions satisfied; then
`IOPMRequest::create()` — inlined — got as far as its `blx gMetaClass.alloc()`. **The block's ordering
argument was right and its last hop was one call early.**

The falsifiers, checked one by one. **(a)** no `_ZN17IOPowerConnectionC1Ev` at 0x8016622C — the named
alternative did not fire, which is the reading that says `registerPowerDriver` passed all three checks and
reached `acquirePMRequest`, one call earlier as predicted. **(b)** no
`_ZN19IOPMPowerStateQueue16submitPowerEventEjPvy` at 0x80157928, so the object took and the walk never
reached `registerInterest`. **(c)** no stop at any `blx` on the resumed path — `addEventSource`,
`IOCommandGate::runAction` and `IOWorkLoop::_maintRequest` all ran to completion, which **certifies this
step's real contribution**: before it the queue's vtable was zeros and any `blx` through it was a jump to
0. **(d) the scan's set is exact and its conclusion is not** — `_ZTV11IOPMRequest+0x38` (0x8018323C =
`IOCommand::init`) and eight `_ZTV24IOCPUInterruptController` slots really are the only *function
pointers* into the stub region; the error was inferring from that "no vtable on this path matters", when
the stop needed no function pointer at all — `_ZTV11IOPMRequest`'s **slot 0x30** is the constructor, and
it is reached by the native `MetaClass::alloc()` straight line. The scan answered the question it asked,
and the question was the wrong one. **(e)** `abort_entries=0` with `abort_first_pc=0` and
`abort_first_dfar=0`, and no `panic` line: the resumed `start` did not fault on `gIOPMWorkLoop` — the one
thing the block could only argue from the source — `checks=5` / `failures=0`, and the log ends in the
kernel's own `No errors detected`.

**What the machine did that no previous step had done**: it built an `IOWorkLoop` event source, entered
`IOCommandGate::runAction` and took its *not on the work loop thread* branch — the boot thread called
`IOWorkLoop::sleepGate` and the PM work loop thread ran `IOWorkLoop::_maintRequest` over the new queue's
vtable. **The walk came back from a thread switch for the first time**, and `IOPMrootDomain::start`
resumed at +0x784 after 373's stop at +0x780 and reached +0x838 (the `acquirePMRequest` frame) before the
frontier moved.

## The layout

| | 373 measured | 374 predicted | 374 measured |
|---|---|---|---|
| counts | 777 / 675 / 102 | **775 / 673 / 102** | **775 / 673 / 102** |
| object `.text` | — | 0x8017F1C0 (0x318, no fill) | **0x8017F1C0 (0x318)** |
| its `MetaClassD0Ev` input | — | 0x8017F4D8 (0x4) | **0x8017F4D8 (0x4)** |
| platform expert `.text` | 0x8017F1C0 (0x150) | 0x8017F4DC (0x150) | **0x8017F4DC (0x150)** |
| `realstubs.o` `.text` | 0x8017F36C (0x3F48) | 0x8017F688 (0x3F18 = 673 × 0x18) | **0x8017F688 (0x3F18)** |
| its end | 0x801832B4 | 0x801835A0 | **0x801835A0** |
| `.text` run end (= `.rodata` run start) | 0x80183560 | 0x8018364C | **0x8018364C** |
| object `.rodata` | — | 0x801A5174 (0xB4) | **0x801A5170 (0xB4)** |
| object `.rodata.str1.1` | — | 0x801A5228 (0x14) | **0x801A5224 (0x14)** |
| platform expert `.rodata` | 0x801A4E88 (0x440) | — | **0x801A5238 (0x440)** |
| `realstubs.o` `.rodata.str1.4` | 0x801A542C (0x3A28) | 0x801A57E0 (0x39B8) | **0x801A57DC (0x39B8)** |
| its end | 0x801A8E54 | 0x801A9198 | **0x801A9194** |
| `.text` end | 0x801A9820 | 0x801A9B60 | **0x801A9B60** |
| text size | 1742880 | 1743712 | **1743712 (= 0x1A9B60)** |
| `.data` | 0x801AC000 (0x19AB0) | unmoved | **0x801AC000 (0x19AB0)** |
| `.sysctl_set` | 0x801C5AB0 (0x150) | unmoved | **0x801C5AB0 (0x150)** |
| `.init_array` | 0x801C5C00 (0x84) | 0x801C5C00 (0x88) | **0x801C5C00 (0x88)** |
| its end | 0x801C5C84 | 0x801C5C88 | **0x801C5C88** |
| `.bss` | 0x801C5CC0 (0x39118) | 0x801C5CC0 (0x39158) | **0x801C5CC0 (0x39158)** |
| object `.bss` | — | 0x801FC664 (0x18) | **0x801FC664 (0x18)** |
| `realstubs.o` `.bss` | 0x801FC680 (0x2744) | 0x801FC6C0 (0x2744) | **0x801FC6C0 (0x2744)** |
| `__bss_end` | 0x801FEDD8 | 0x801FEE18 | **0x801FEE18** |
| image | 1858692 | 1858696 (= 0x1C5C88) | **1858696** |
| headroom | 2101800 | 2101736 (= 0x2011E8) | **2101736** |
| args / topOfKernelData / tree / window | +2097152 / +4194304 / +6291456 / 8388608 | all unmoved | **all unmoved** |

**373's miss is the row this block derived rather than located**: the object's `.text._ZN…MetaClassD0Ev`
input (0x4) is a *second* input of the same object, matched by the `.text.*` wildcard, and it is what the
platform expert's `.text` follows by, so `realstubs.o`'s row is `0x8017F1C0 + 0x318 + 0x4 + 0x150 + 0x4
+ 0x54` = 0x8017F688, not the platform expert's end. All five of those constants came out exact.

**`.data` could not move**: `align_up(0x801A9B60, 0x4000)` is still 0x801AC000 (0x2480 of room), and the
object has no `.data` at all, so `.data`, `.sysctl_set` and every row derived from their end are exactly
373's.

**The `.bss` pad rule came out exact, and this is the first step since 364 that reads it.** The object's
0x18 lands at 0x801FC664 — immediately before the platform expert's `gMetaClass`, which moves
0x801FC664 → 0x801FC67C — and the 4-aligned slot in front of `realstubs.o`'s `.bss` goes 0x4 → 0x2C, so
`realstubs.o`'s row moves 0x801FC680 → 0x801FC6C0 with its size unmoved and `__bss_end` follows at
0x801FEE18. That is the `new_pad = (old_pad − object_size) mod 64` rule read *forward* for the first time;
367 read it backward as a retiree. `.bss` grows 0x40 = the object's 0x18 + the 0x28 the pad gives up.

## The one miss: 0x4 on the object's `.rodata`, and the `.text` end is unmoved by it

The object's `.rodata` is at **0x801A5170**, not 0x801A5174: the `.text` run and the `.rodata` run do not
meet exactly, and what the join absorbs is 0x4 on this step. The measured fill is not a property of this
step — 368 read 0x4, 371 0x0, 372 0x4, 373 0x0, **374 0x4**. Everything below it moved with it exactly:
the platform expert's `.rodata` to 0x801A5238, `realstubs.o`'s row to 0x801A57DC, all three 0x4 low
against the block's numbers and all three internally consistent
(0x801A5170 + 0xB4 = 0x801A5224; 0x801A5224 + 0x14 + 0x440 + 0x16 + fill 0x2 + 0x14C = 0x801A57DC).

**The `.text` end is unmoved by it**, and that is the part worth keeping: the raw end goes
0x801A9194 + 0x4 + fill 0x4 + 0x64C + 0x251 + 0x3 + 0x108 + 0x8 = 0x801A9B4C, and `ALIGN(0x20)` closes
the section at **0x801A9B60** — the same 32-byte line the block predicted from a raw end 0x4 higher.
**A 4-byte boundary term that shifts a whole run can still leave a section end exact, because the section
end is an `ALIGN`, and the `ALIGN` erases any shift smaller than its own granularity minus the fill.**

The block's own two routes to the text size disagreed before the build and it recorded that rather than
hiding it: the position route predicted 0x801A9B60, the delta route 0x801A9B5C, and the map says
0x801A9B60. Route 2's `0x340 − 0x4` is a *raw-end* number and the raw end is not what `size` reports.
**Tell, and it is 372's tell moved down one row: when a prediction has two routes and they disagree, the
one that predicts the quantity the tool will print is the prediction, and the other is arithmetic to be
recorded rather than defended.**

## The stop's miss: a real callee is not a call that cannot stop

The block resolved the `blx` at `acquirePMRequest+0x20` to `IOPMRequest::MetaClass::alloc` and wrote
"real" — true of the *symbol* — and then predicted the next thing in the caller's own disassembly,
`bl IOCommand::init`. But `alloc` is not a leaf:

```
8017300C  push {r4, lr}
80173010+ bl OSObject::nw(0x38)                            real
80173028  bl IOCommand::IOCommand(OSMetaClass const *)     <- THE STOP, key 0x8017302C
```

and the constructor it calls is a stub, because `iokit_Kernel_IOCommand.o` is not in the link. **The check
that would have caught it is one command**, and the tool answers it with the measured key to the byte:

```
$ tools/first_stub_call.py _ZNK11IOPMRequest9MetaClass5allocEv --image out/stage90/xnu_arm_entry.elf
_ZNK11IOPMRequest9MetaClass5allocEv: 16 instructions, 0x8017300C..0x80173048
3 direct calls
  +0x8     _ZN8OSObjectnwEm                      real
first stub call: +0x1C  _ZN9IOCommandC2EPK11OSMetaClass
  the run should stop with stub_hit=_ZN9IOCommandC2EPK11OSMetaClass and the caller key at +0x20
```

`0x8017300C + 0x20 = 0x8017302C`. The block ran this tool on nine functions on the path and not on this
one, because it had already decided what this one was. **Tell: for every call a prediction describes —
direct or indirect — the question is not "is the target real?" but "does the target's own straight line
reach a stub?", and that question has a one-command answer that must be run for each target, including the
ones that look like leaves.** It is also the first time this walk has stopped inside a constructor chain
reached through a vtable — and it is falsifier (d)'s lesson in the same step: *a real slot is not a
guarantee either.* Nothing derived from the wrong premise moved: all three counts, every `.text`
placement, `.data`, `.bss`, the derived numbers and the safety readings are exact.

## The next object, named before its run

The frontier is `IOCommand::init` **and** `IOCommand::IOCommand(OSMetaClass const *)`, both defined by
**`iokit_Kernel_IOCommand.o`** — the only object in the 695-object pool that defines either, and the only
one that defines `~IOCommand()` too. Measured against this image: 20 definitions, 30 references (all
satisfied), **5 resolved (3 function, 2 storage) / 0 added** — in 374's image **775 → 770 undefined,
673 → 670 function, 102 → 100 storage**, the walk's first step in a long time that retires *storage*:
`IOCommand::gMetaClass` (0x18) and `IOCommand::metaClass` (0x4) are stand-ins. Its `.text` is **0x9AC**
holding fifteen real functions; every section kind arrives at once. `IOCommand::init` is also
`_ZTV11IOPMRequest`'s slot 0x30, so **375 is the step that makes `IOPMRequest`'s vtable fully real** —
after which the PM request machinery `registerPowerDriver` was reaching for can actually be built.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img` (4878336 bytes, sha256
`d6e4386101b66c8b556d41d25810188f67f8b7596c87f1fbc8d506ec1fce606c`); nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading of
either. `xnu_entry_checks=0x00000005` / `xnu_entry_failures=0x00000000`, `checksum=0x907fee15`,
`abort_entries=0` with `abort_first_pc=0` and `abort_first_dfar=0`, no `exception:` line. Log 301643
bytes, 3975 lines, last line `No errors detected`. The device came back to Android on its own
(`MI 4LTE`, release 10).

The step's own code is real C++ that ran: an `IOWorkLoop` event source was built, a command gate was
entered and slept on, the PM work loop thread ran `_maintRequest`, and an `IORootParent` was constructed,
attached and started. It reads no device and stops at a reporting stub before `acquirePMRequest` gives the
PM engine a real request — but it is the first step in this walk where the kernel's own *threading*
machinery ran, and the recovery nets (`sleepGate` returns under the hardware watchdog; the software
dead-man fires on a silent boot) are the ones that would have caught it if it had not.
