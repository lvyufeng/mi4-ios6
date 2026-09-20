# Experiment 486 — the tree root's block changes hands

Date: 2026-09-20
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured
Artifacts: `stages/stage90/xnu_arm_boot/entry_trace.c`, `entry_stubs.c`, `build_entry.sh`,
`tools/check_registry_adoption.py` (new)

**Result: 485's "one registry slot, two root objects" is answered, and the answer is not two trees.**
The registry's IODT child is the **`IOPlatformExpertDevice`** `StartIOKit` makes, and it adopts the tree
root's **21 children** — the same child-set array, `0xc05d13a0`, that the tree root answered with before
the handover. The object 461 recorded as "the tree's root nub" is not a second tree either: **its block
was released and reused**, and by the time the census reads it the address `0xc05c9cb8` holds an
**`IORootParent`** — the object `IOPMrootDomain` makes at `IOPMrootDomain.cpp:1247` — with zero children
and no child set, because it is a different object that never registered in the IODT plane.

The three classes are read off the objects' own vtables and named, in one run, from the same image:

| moment (the instrument's site) | object | class |
| --- | --- | --- |
| the `IODeviceTreeAlloc` wrapper, `walk_seq = 1` | `0xc05c9cb8` | **`IOService`** (`_ZTV9IOService + 8`) |
| every `fromPath` after it, `walk_seq ≥ 2` | `0xc05943b0` | **`IOPlatformExpertDevice`** (`_ZTV22IOPlatformExpertDevice + 8`) |
| the census, `dtrec_*` (461's recorded pointer) | `0xc05c9cb8` | **`IORootParent`** (`_ZTV12IORootParent + 8`) |

**And the step's prediction was falsified by its own run.** The `entry_note_dtrec` comment inherited 461's
reading and said the returned entry would be an `IOService` with zero children and no child set *because
`IORegistryEntry::init` removed the child-set key from its own table* — which is true of the tree root and
is what the numbers look like. What the run answers is `IORootParent`: the zero children and the missing
set belong to a **different object that inherited the address**. The comment now records both, and the
instrument that would turn the argument into a reading is named for 487.

The boot is unperturbed and the device came back on its own, so the never-brick half holds: 25 ×
`persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`, entry checks 5 / failures 0,
`checksum = 0x9071e9b9`, 32 `sleh` entries all handled, the console text identical, and the OS still
resolving `/chosen` (`0xc05ca448`) and `/chosen/memory-map` (`0xc05c9898`) for its own calls.

## What 485 left, and why the class name is the question

485 ended with a measurement and a gap. The measurement: `xnu_live_dtk_same = 0` — the object 461 recorded
out of `IODeviceTreeAlloc` and the registry root's own IODT child are different objects at every moment
after `bsd_init`'s first `fromPath`, with the same 21 children and **the same child-set pointer**. The gap:
*which* objects they are. Two pointers that both answer "21 children" are two trees, one tree and its
adopter, or one tree and a stale pointer — and pointer arithmetic cannot tell those apart.

A class name can. 485's own note said where to look: two entries, two classes, and Apple's sources name
both. The tree's nodes are `new IOService` (`IODeviceTreeSupport.cpp:359`, `MakeReferenceTable`), so the
root is an `IOService`; and the entry that ends up owning its children is
`new IOPlatformExpertDevice` (`IOStartIOKit.cpp:156-157`) performing
`ok = super::init( dt, gIODTPlane )` with `dt = IODeviceTreeAlloc( dtTop )`
(`IOPlatformExpert.cpp:1558-1566`). `IOPlatformExpertDevice` is an `IOService` subclass, and
`IORegistryEntry::init(old, plane)` (`IORegistryEntry.cpp:333-385`) is the function that moves the links.

## The instrument, and the three runs it took to get right

`entry_class_words` reads an object's class, and the road is the object's **own vtable**. That is the whole
of this step's instrument, and three of its four runs were decided by getting it wrong in three different
ways — each one a defect now recorded, because each one produced a *plausible* log.

**Run A: a virtual called by its mangled name is the base implementation.** The first version declared and
called `OSObject::getMetaClass` by name — the road 461 took to `IOService::getProperty`, which is not
overridden away from its base. `OSObject::getMetaClass` is `{ return &gMetaClass; }`
(`libkern/c++/OSObject.cpp:57-58`), so run A printed `OSObject` for **all nine** class records — the seven
walks, the census's root and the census's recorded pointer. The tell is inside the same log: its other
instrument, the metaclass walk of an earlier step, printed **`IOResource`** and **`IOPlatformExpertDevice`**
for the two metaclass objects at the same moment (`xnu_live_wcls_name0/1 = 0x65524f49/0x72756f73` and
`0x6c504f49/0x6f667461`). One log, two instruments, two different class names for the same tree — a
correct answer (`OSObject` *is* the base's metaclass) to a question nobody asked.

**Run B: the slot is the ABI's, and the object's table is not the vtable.** The second version indexed the
object's first word with the vtable's own index (9) and called what it found. `getMetaClass` sits at index
9 of the **vtable**, but an object's vptr points *past* the vtable's two-word preamble
(`[offset-to-top][typeinfo]`), so index 9 of the **object's** table is
`OSObject::taggedRetain(const void *)` (`0x80133d38`) — a `void` function whose leftover `r0` is whatever
it was, which the log says was **1**. `getClassName`'s first instruction, `ldr r0, [r0, #12]`, then took a
data abort with `DFSR = 0x5` and `DFAR = 0x0d`, and the abort could not be serviced: the handler was
entered 64 times (`sleh_seen = 0x40`) and `xnu_live_sleh_at_back` was written **zero** times, against run
A's twelve. Run B's log stops there — no `walk_*`, no `dtc_*`, no `cls_*` records at all, 443 681 bytes
against run A's 504 460 — which is why the run is described here by what is *missing* from it.

**Run C: the guard read the preamble at the vptr.** The third version gated the call on the two words
before the object's first word being zero — but read them *at* the vptr, so the two words it got were that
vtable's **first two function slots**: the class's own destructors. Every object was refused
(`cls_took = 0` for all nine calls) and `dtc_class0/1` and `dtrec_class0/1` are `0x00000000` in run C's log.
**And the refusal still named both classes**, because `pre0`/`pre1` are code addresses `nm` can resolve:
`0x8013cfb0`/`0x8013cfb4` are `IOService::~IOService()` and its deleting form, `0x801754a8`/`0x801754ac`
are `IOPlatformExpertDevice::~IOPlatformExpertDevice()` and its deleting form; `0x804999ac` is
`_ZTV9IOService + 8` and `0x8049f990` is `_ZTV22IOPlatformExpertDevice + 8`. The two classes 485 needed
were in run C's log, printed as two pairs of addresses, with no name attached.

**Run D: the guard reads the words the ABI defines, and the classes are named.** The preamble constant is
a second `#define` (`STAGE90_VTABLE_PREAMBLE = 2`), the load is
`STAGE90_VTABLE_META_CLASS - STAGE90_VTABLE_PREAMBLE` = index 7 of the object's table, and the two words
tested are `vptr[-2]` and `vptr[-1]`, which are `0/0` for a primary vtable with no RTTI pointer — read from
the image, not assumed. The call is made only when the preamble is zero **and** the slot holds an address
inside this image (`[0x80000000, &__bss_start)`), and `entry_note_class` publishes the object, its vptr,
both preamble words, the slot's word, the metaclass and whether the call was made — *before* the call and
after it, so an object that is refused is a record rather than a hole. That precondition is the one
change run B's fault bought: a number the instrument cannot use is published, not dereferenced.

## The measurement, run D

Nine class records, in three groups by site. `STAGE90_CLS_WALK = 1` is the walk inside the
`IODeviceTreeAlloc` wrapper and every later `fromPath`; `STAGE90_CLS_ROOT = 2` is the census's own root;
`STAGE90_CLS_RECORDED = 3` is 461's recorded pointer.

| call | site | object | vptr | slot word | metaclass | took | class |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 1 | `0xc05c9cb8` | `0x804999ac` | `0x8013cfc8` | `0x80574570` | 1 | **`IOService`** |
| 2–7 | 1 | `0xc05943b0` | `0x8049f990` | `0x801754c0` | `0x80574d3c` | 1 | **`IOPlatformExpertDevice`** |
| 8 | 2 | `0xc05943b0` | `0x8049f990` | `0x801754c0` | `0x80574d3c` | 1 | **`IOPlatformExpertDevice`** |
| 9 | 3 | `0xc05c9cb8` | `0x8049db40` | `0x80170f74` | `0x80574c38` | 1 | **`IORootParent`** |

Every `pre0`/`pre1` is `0/0`, every `took` is 1. The vptrs are the image's own vtable labels plus the
preamble: `_ZTV9IOService = 0x804999a4`, `_ZTV22IOPlatformExpertDevice = 0x8049f988`,
`_ZTV12IORootParent = 0x8049db38`, each **+8** — which is the arithmetic run B got wrong, visible in the
same log as the labels it was wrong about.

The census, run D — `dtk_calls = 1`, so it ran (unlike 485, whose report group was never written):
`plane = 0xc0592960`, `recorded = 0xc05c9cb8` (**`IORootParent`**, `kids = 0`, `set = 0`),
`root = 0xc05943b0` (**`IOPlatformExpertDevice`**, `kids = 0x15`, `set = 0xc05d13a0`), `same = 0`,
`count = 0x15`, then the same 21 children 485 named, each at `__state[0] = 0x1e` and `__state[1] = 0`.
The two child counts agree again (`getChildCount` = 21 and `OSArray::getCount` on the set the accessor is
written in terms of = 21) and `getChildSetReference` hands back the same `0xc05d13a0` the walk saw at
every seq; the census's own class read is site 2 with site 3 beside it, so the two subjects of the two
counts are published as values rather than asserted in prose — 237's fix, still holding.

The walk brackets the handover, and the child set is the cross-check:

| `walk_seq` | `walk_root` | `walk_count` | `walk_first` | class | `walk_set` | `walk_kids` | `walk_control` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `0xc05c8768` | 1 | `0xc05c9cb8` | `IOServic`… | `0xc05d13a0` | `0x15` | `0x0` |
| 2 | `0xc05c8768` | 1 | `0xc05943b0` | `IOPlatfo`… | `0xc05d13a0` | `0x15` | `0xc05ca448` |
| 3–7 | `0xc05c8768` | 1 | `0xc05943b0` | `IOPlatfo`… | `0xc05d13a0` | `0x15` | `0xc05c9898` |

**The child-set pointer does not move across the handover.** It is `0xc05d13a0` before, after, and at the
census — and it is the same array `getChildSetReference` hands the platform expert device. So the children
were not re-parented into a new array; **the same array changed owners**, which is what the shallow copy
and the registry-table swap in `IORegistryEntry::init` predict and what a "second tree" reading cannot
produce. `walk_control` is the OS's own two lookups: `0` inside the `IODeviceTreeAlloc` wrapper (the OS has
not called `fromPath` yet), then `/chosen` and `/chosen/memory-map` — the same two pointers 461 measured,
still resolving.

## The mechanism, as far as a log can carry it

`IOPlatformExpertDevice::initWithArgs` (`IOPlatformExpert.cpp:1558-1566`) is `dt = IODeviceTreeAlloc(dtTop)`
then `ok = super::init( dt, gIODTPlane )`. So in `IORegistryEntry::init(old, plane)`
(`IORegistryEntry.cpp:333-385`) `old` is the tree root and `this` is the platform expert device, and the
file's `IOREGSPLITTABLES` (`:121`, defined unconditionally, so `registryTable()` is `fRegistryTable`)
decides the rest:

  - `fPropertyTable = old->dictionaryWithProperties();` — a **shallow** copy: the entries are copied and
    each value is `taggedRetain`ed, so the two tables share the same `OSArray` objects;
  - `fRegistryTable = old->fRegistryTable;` and `old->fRegistryTable = fRegistryTable->copyCollection();`
    — the **registry table object changes hands**: `this` owns the table that maps plane keys to the
    child/parent arrays, and `old` gets a deep copy of it;
  - `old->registryTable()->removeObject( plane->keys[ kParentSetIndex ] )` and `…kChildSetIndex` —
    removed from `old`'s *copy*, which is why the tree root's own table no longer has a child-set key;
  - the parents' loop: `all = getParentSetReference( plane )` is now *this*'s table, i.e. `old`'s original,
    holding the **meta root's child array**, whose only member is `old`. For `next = gRegistryRoot`:
    `next->makeLink( this, kChildSetIndex, plane )` then `next->breakLink( old, kChildSetIndex, plane )`.
  - the children's loop: `all = getChildSetReference( plane )` is the tree's own child array — the
    `0xc05d13a0` of every record above — and for each of the 21 children
    `next->makeLink( this, kParentSetIndex, plane )` then `next->breakLink( old, kParentSetIndex, plane )`.

`breakLink` is `links->removeObject( index )` (`:1389-1410`), so the parents' loop's second call drops the
reference the meta root's child array held on the tree root. The count was **1**: `attachToParent` added
the array's reference and `IODeviceTreeAlloc`'s own `parent->release()` left exactly that one. So that
`breakLink` is the release that frees the tree root — and the children's loop then runs *after* it,
rewriting the links of an object whose block has already been released. That did not crash here (the freed
block is still mapped), and the census's `dtrec_*` record shows what the block holds afterwards.

**`IOPMrootDomain`'s `patriarch = new IORootParent` (`IOPMrootDomain.cpp:1247`) then gets that block back**,
which is why the address `0xc05c9cb8` — the tree root at `walk_seq = 1`, 461's `g_dtplane_root`, the
census's `recorded` — reads as `IORootParent` with zero children and no set at the end of the boot, and why
485's reading of "zero children and a NULL child set" was a reading of a *different object* than its prose
named.

**What is argument rather than reading, stated as such:** the release is inferred from the retain count and
from the allocator handing the same address to a later `new`. Nothing in this run watches the release or
the allocation. 487's instrument is therefore specific: read the class of this pointer at **every** walk so
the log brackets the moment the block changes hands, and wrap the allocator path that receives it
(`OSObject::release` releasing at zero, or the `IONew`/`kalloc` under it) so the second half is a record
too. Until then, "the tree root was freed and its block reused" is the best explanation of three measured
class names, not a fourth measurement.

## Defects this step's own session found

Seven, in `mi4-measurement-defects.md` (240–246) — this step is unusual in that **three of its four runs
were wrong**, and each wrong run produced a log that read like an answer.

  - **240 — one call declared twice.** The first version needed `OSMetaClass::getClassName` and declared
    it, without noticing that `entry_trace.c` already declared it for the metaclass walk of an earlier
    step under `entry_xnu_metaclass_name`. Two declarations of one call are this project's oldest defect
    class, and the selftest consequence is the sharp end: a mutation that edits one of them leaves the
    other standing and reports a blind spot that is not there. The claim now requires exactly **one**
    occurrence of the mangled name in the file.
  - **241 — a virtual called by its mangled name is the base implementation.** Run A's nine records all
    say `OSObject`, because `OSObject::getMetaClass` returns `&gMetaClass` and nothing in the log
    distinguishes "the object's class is `OSObject`" from "the base implementation answered". The tell is
    the same log's other instrument printing the real class names. A name that resolves is not the
    function the program would have called; for a virtual, only the object's own table answers.
  - **242 — the source name is not the linked name.** `entry_class_words` is `static` with two callers, so
    GCC emitted `entry_class_words.part.0.constprop.0` and the callers `bl` to *that*; the check's claim
    about "the census's text transfers to `entry_class_words`" compared against the bare name and **failed
    the build on an image that was right**. The mirror image sat in the selftest: a mutation addressing the
    facts dict by bare name raised `KeyError` instead of mutating — a mutation that cannot run is a
    mutation that cannot refuse anything. Both now go through a resolver that accepts the `.part.`-suffixed
    key and refuses loudly when neither name exists.
  - **243 — one index, two tables, and nothing comparing them.** The deepest defect of the step, and the
    one that put a fault on the device. `claim_slot` read `_ZTV8OSObject` and `_ZTV9IOService` out of the
    image, found each class's own `getMetaClass` at **index 9 of the label**, and required the code's
    `#define` to equal 9 — while the code indexed the **object's** table, where 9 is
    `OSObject::taggedRetain`. The check and the code agreed on a number and disagreed about the thing it
    indexed, so the check *passed* a program that called the wrong virtual, and run B's abort at
    `getClassName+0` with `DFAR = 0x0d` is what found it. The fix is the two constants and the subtraction,
    with the preamble length read out of the image as the run of leading zero words before the first code
    address and required to equal `STAGE90_VTABLE_PREAMBLE`.
  - **244 — a constant defined and not used as a displacement.** Run C defined the preamble length and then
    read the words *at* the vptr instead of *before* it, so the guard tested the vtable's first two
    function slots. Every object was refused and `cls_took = 0` nine times; the run looks like "the guard
    works and these objects are not objects". **The tell: a guard that refuses everything and a guard that
    reads the wrong words are the same log.** What separated them was `nm` on the two words it published —
    the class's own destructors — which is why the fix keeps publishing the refused object's numbers.
  - **245 — a pointer treated as one subject across a lifetime.** The `entry_note_dtrec` comment predicted
    the class of the object 461 recorded, on the assumption that the pointer still named the tree root.
    The run answers `IORootParent` at that address: the block had been released and reused, so the prose
    and the number were about different objects, **and every reading of that pointer between the two
    moments inherited the same assumption** — 461's `g_dtplane_root`, 485's zero children, and this step's
    first walk without a class name. The tell is the general one this file counts in five earlier forms: a
    pointer is a *name*, and a name is not a subject across a lifetime; an allocation that is freed and
    reused does not announce it, and only a property of the object *at that moment* can.
  - **246 — a count written in prose, twice wrong, compared with nothing.** The new check's `--image`
    requirement said "three of the seven claims are about the linked image". The file has **eight** claims
    and **two** of them read the image. Nothing compared either number, which is the "one value, two
    definitions" defect this repository counts twenty-four cases of, in its smallest form: a sentence.
    `image_claims()` now *measures* the set — empty the image's four fact groups and ask each claim — and
    `compare` fails if the measurement is not `IMAGE_CLAIMS`, so the message cannot drift again.

## Build, layout, and artifacts

| | 485 B | 486 D |
| --- | --- | --- |
| `.text` | 5246784 | **5248352** |
| image bytes | 5470836 | **5470836** (unchanged) |
| `.bss` | `..0x8058f8c8` (360008) | **`0x80537a80 .. 0x8058f908`** (360072) |
| headroom | 1509176 | 1509112 |
| undefined / wraps | 26 / 59 | 26 / 59 |

`.text` grew by 1568 bytes — the class instrument and its guard — and the image did not move, the `.data`
16 KB alignment absorbing it (284's lesson, unchanged in 486). Entry image `xnu_arm_entry.bin` sha256
`7b84318c9d7a69b381923e7483b20c7195c08c393bc206c1dba7b074cec3a6e0` (`.elf`
`ebd00a54fdda42f6503004e313b6b949f67e9f39c70e03107d3140cdc47941f3`, text 5 253 128, data 206 796,
bss 360 072). Payload `stage90-qcdt.img` 8 491 008 bytes sha256
`c20d8eba56afab30241e11634604ae34354f67f8f1c5a1dce6a6141198cdb597`, `kernel_size = 5966100`,
`dt_size = 2521088`. Build lines, in order, all green: `check_undef_handler.py --split` (61 mutations
refused), `check_timebase_registration.py` (12), `check_gic_routing.py` (43), `check_irq_routing.py` (36),
`check_timer_sources.py` (42), `check_boot_completion.py` (52), `check_registry_adoption.py` (**66**).

Four device runs, each through `preflight_boot_check.sh --allow-xnu-entry` and
`run_and_capture.sh --allow-xnu-entry`, exit 0, device back on Android on its own every time: A (504 460
bytes, the base implementation), B (443 681 bytes, the fault — the shortest run of the step and the only
one with `sleh_seen = 0x40` and no `at_back`), C (506 808 bytes, the refusal), D (507 024 bytes, 6376
lines, the measurement). B's payload is the only one whose image no longer exists: D's build wrote over
`xnu_arm_entry.bin`, so B is recorded by its log and by the fault's numbers, not by a hash.

## What 487 has to do

**The release, as a reading.** The step's mechanism has one inferential step in it — that the tree root's
block was freed and handed to `new IORootParent` — and it is worth exactly the two instruments named above:
the class of `g_dtplane_root` (and of `walk_first`) at **every** walk, so the log brackets the change, and
a wrapper on the release/allocation path so the block's ownership is a record rather than an argument from
the allocator's reuse. 462's "the delete list was every entry" is the other half of the same question and
is still open.

**And, unchanged, 485's second half: the copies that build process 1's arguments.** `exec_save_path+0x38`,
`exec_copyout_strings+0x128`, `load_init_program_at_path+0x30` → `mmu_kvtop_wpreflight+0xc`, the
`copyin/copyout` word loops and `copypv+0x84` — the 32 abort records whose centre of gravity moved in 485 —
are still undecided between "a demand fault the kernel's own handler services" (which would be this
image's first serviced demand fault) and "a retry of the same translation fault". 486 did not take this
up: it wraps `vm_fault`/`arm_fast_fault` (owed since 480) and publishes the VA's translation before and
after the handler returns.

Still owed, and unchanged: which of `arm_fast_fault`/`vm_fault` serviced 480's write fault; why the
`VM_FLAGS_FIXED` stack allocation at `0x26E00000` is refused; 474's `thread->map = 0` moment; 448's `_bad`
slots as a named pair; the pthread table's other ~34 slots; `osfmk/kperf/kperfbsd.c`; the untraced build's
`entry_stubs.c` compile errors; `thread_bootstrap_return`; and the fixture's timer node
(`interrupts = <1 2 0 1 3 0>` → INTID 18/19), falsified for `CNTV` by 482.
