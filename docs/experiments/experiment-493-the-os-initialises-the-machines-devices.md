# Experiment 493 — the OS initialises the machine's devices, and the reader was reading the wrong class

Date: 2026-09-21
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured twice
Artifacts: `stages/stage90/{stage90_main.c,xnu_platform/MSM8974Timer.cpp,xnu_platform/MSM8974GIC.cpp,
xnu_platform/stage90_platform_config_tables.c}`, `stages/stage90/xnu_arm_boot/build_entry.sh`,
`tools/{build_xnu_arm_kernel.sh,check_driver_catalogue.py}`

**Result: two device nodes of this machine's tree now have drivers, and each driver reads the OS's own
resolution of the node's `reg` and finds it agrees with the tree.** 492 gave `/timer` a driver and left
one question open - a single driver cannot tell "the table matches nodes" from "this node happened to
work" - so 493 states the same three parts (the bucket `IOPlatformDevice`, the probe score, the name
bridge) again for `/interrupt-controller`, whose names `MSM8974Timer`'s cannot match, **and** makes the
kernel's own device-memory resolution readable for the first time. Run 2:

    /timer                /interrupt-controller
    _seq        = 1                      1                  one start each
    _match      = 1                      1                  the node's `name` matched, in both
    _have       = 0x3                   0x1                 `reg` (and, for the timer, `frequency`)
    _reg0       = 0xf9020000            0xf9000000          the node's own first address
    _reg1       = 0x1000                0x1000              the node's own first size
    _regwords   = 6                     4                   = 2 x the number of pairs
    _devcount   = 3                     2                   the OS resolved exactly the pairs
    _objkind    = 2                     2                   an `IOMemoryDescriptor`, not an `IODeviceMemory`
    _objlen     = 0x1000                0x1000
    _phys0      = 0xf9020000            0xf9000000          the OS's entry 0 == the node's `reg[0]`
    _len0       = 0x1000                0x1000              the OS's entry 0 length == the node's `reg[1]`
    _resolve    = 1                     1                   the OS's answer and the tree agree

and 491's third census records **29 of 29 rows** with `kids_of = 1` on both rows - `seq = 0x19`
(`interrup…`) and `seq = 0x1a` (`timer`), each carrying the pointer its driver's `_prov` publishes.
(**Corrected at 494, defect 289**: this doc first printed the rows' *positions* in the printed dump,
which are one higher than the keys because the census writes an unnumbered header row first. The
keys themselves read `xnu_live_pex_seq = 0x19` for `interrup…` and `0x1a` for `timer` - so a reader
who greps the earlier number for `interrup…` lands on the timer's row.) The
catalogue's answers move the way a *layer* moves and a coincidence does not: the same 30 calls as 492,
but **26 of them now answered with two candidates** instead of 27 with one, because both personalities
sit in the `IOPlatformDevice` bucket and `probeCandidates` walks the whole bucket for every nub - the
candidate test is what selects, not the bucket's size. The boot's own console frontier did **not** move
(`load_init_program: attempting to load /sbin/launchd`), and nothing here claims otherwise.

Two device runs, both through the gate, both exit 0 with the device back on Android on its own. **Run 1
is kept because it found the step's real defect, in the driver's own reader**: `_devcount` came back
right (3 and 2 - the nodes' own pair counts, so the OS *had* resolved both nodes) beside `_phys0 = 0`,
`_len0 = 0` and `_resolve = 2`. The entry was cast to `IODeviceMemory`, the type the array is
documented to hold, and `IODeviceMemory::withRange` is a blind cast of
`IOMemoryDescriptor::withAddressRange` - which builds an `IOGeneralMemoryDescriptor`, a **sibling** of
`IODeviceMemory`. The cast answered 0 for every entry, the read was skipped, and the record said the OS
had resolved nothing. That is 492's `_freqkind` a second time, in the same file, one step later: **the
reader's type is a reading too.**

## The declaration that reaches every node, and why it is on the root

`IOService::doServiceMatch` on the **nub** runs `kIOReturnSuccess == getResources()`
(`IOService.cpp:3724`) before `probeCandidates( matches )` (`:3726`), and a nub's `getResources` is
`IOPlatformDevice::getResources` -> `IODTPlatformExpert::getNubResources` ->
`IODTResolveAddressing( nub, "reg", 0 )` (`IOPlatformExpert.cpp:1358-1366`, `:1700-1703`). That
function decides how wide one `reg` entry is from the **parent's** counts:

    IODeviceTreeSupport.cpp:1227  parentEntry = regEntry->copyParentEntry( gIODTPlane );
    :1229-1230                    IODTGetCellCounts( parentEntry, &sizeCells, &addressCells );
                                  if( 0 == sizeCells) break;
    :1232                         cells = sizeCells + addressCells;
    :1233                         num = addressProperty->getLength() / (4 * cells);

and `IODTGetCellCounts` (`:1034-1041`) answers `1`/`2` for a node that declares neither. This tree
writes `{address, size}` pairs and declared nothing, so `cells = 3` and the timer's six-word `reg`
resolved to `24 / (4 * 3) = 2` entries instead of 3, and the GIC's four-word `reg` to 1 instead of 2 -
`IODTResolveAddressing` filed an array of the wrong **count** on every nub of the machine, and nothing
in the boot could see it. A nub's parent is the root, so one declaration on the root reaches every node
at once (`stage90_main.c`'s root node, `#address-cells` 1 and `#size-cells` 1); with `cells = 2`,
`num = length / 8`, which is exactly the number of pairs the node writes.

`num` is not the whole story, and run 2's `_phys0`/`_len0` are: the OS's entry 0 is the node's own
first pair, so the resolution is not merely the right *number* of ranges, it is the right ranges. Both
sides of that comparison are read: the node's `reg` out of the registry by the driver, and the OS's
answer out of `getDeviceMemory()` - the array `IODTResolveAddressing` filed under `gIODeviceMemoryKey`
(`:1251`) and `IOService::getDeviceMemory()` returns (`IOService.cpp:6046-6049`).

Two things about the mechanism are worth stating because they are what the step rests on, and both are
read out of Apple's file by the check rather than assumed here:

  * **`getNubResources` cannot fail.** It is
    `if( nub->getDeviceMemory()) return( kIOReturnSuccess ); IODTResolveAddressing( nub, "reg", 0);
    return( kIOReturnSuccess );` - success on the path that resolved nothing. So
    `doServiceMatch`'s `kIOReturnSuccess == getResources()` guard cannot be the thing that reports an
    empty resolution, and an empty `IODeviceMemory` array is invisible to the whole boot unless a
    driver reads it. That is why this step's reading is a driver's and not a status code's.
  * **The address resolution is the identity here.** `IODTResolveAddressCell`'s arm for an entry with no
    `ranges` is `*phys = CellsValue( childAddressCells, cell ); *phys += offset;` with `offset` still at
    its initialiser `0` (`:1090-1094`, `:1065`), so this tree's absolute addresses resolve as absolute
    with no `ranges` between `/` and `/timer` - the tree needs none, and adding one would change what
    every `phys0` means.

## Run 1: the OS resolved it and the driver read nothing

    xnu_live_timerdrv_devcount=0x00000003   the OS resolved three entries for a 6-word `reg`
    xnu_live_timerdrv_devmem=0xc05a4580     the array is there
    xnu_live_timerdrv_phys0=0x00000000      ...and its entry 0 reads as nothing
    xnu_live_timerdrv_len0=0x00000000
    xnu_live_timerdrv_resolve=0x00000002    answered, and the comparison disagrees
    xnu_live_gicdrv_devcount=0x00000002     the same on the second node
    xnu_live_gicdrv_phys0=0x00000000
    xnu_live_gicdrv_resolve=0x00000002

The **count** is the reading that localises this: 3 and 2 are the nodes' own pair counts, which no
other mechanism in this step produces, so both the declaration and the resolution had worked and the
failure was in the driver's read of entry 0. The cause is Apple's blind cast, and it is in two files:

    IODeviceMemory.cpp:34-39
        IODeviceMemory * IODeviceMemory::withRange( start, length )
        { return( (IODeviceMemory *) IOMemoryDescriptor::withAddressRange( start, length, ... )); }

    IOMemoryDescriptor.cpp:1162-1181
        IOMemoryDescriptor::withAddressRanges(...) { ...; IOGeneralMemoryDescriptor * that =
            new IOGeneralMemoryDescriptor; ... }

    IOMemoryDescriptor.h:986   class IOGeneralMemoryDescriptor : public IOMemoryDescriptor
    IODeviceMemory.h:45        class IODeviceMemory        : public IOMemoryDescriptor

so the object in the array is an `IOGeneralMemoryDescriptor`, and `OSDynamicCast( IODeviceMemory, ... )`
- a metaclass check, not a C cast - answers 0 for a sibling class. The driver's `if( range != 0 )` then
skipped the read and the record published two zeros that read exactly like "the OS resolved nothing".

The fix is to read the entry as what it is (`IOMemoryDescriptor`, the class all of these derive from)
and to **publish the kind** so that this particular mistake cannot be invisible again: `_objkind` (1 =
the entry really is an `IODeviceMemory`, 2 = it is an `IOMemoryDescriptor` and not one, 0 = neither) and
`_objlen` (the entry's own `getLength()`, which does not go through the segment walk). Run 2 reads
`_objkind = 2` on both nodes: Apple's blind cast is now a number in the log rather than a paragraph in
a header.

`_reg1` joined the record in the same spirit and for the same reason: the comparison's right-hand sides
are `_reg0` and `_reg1` and its left-hand sides `_phys0` and `_len0`, and a `_resolve` of 2 with only
three of the four published is a number whose cause is not in the log. That is exactly what run 1 was.

## Run 2: both nodes resolve, and the two records agree

    _resolve = 1 on both     _devcount = 3 and 2 = the pairs     _phys0/_len0 = the node's own pair
    _objkind = 2 on both     _objlen = 0x1000 = the node's size  _match = 1 on both

The `devcount == regwords / 2` conjunct is the declaration arriving, the `phys0 == reg0` and
`len0 == reg1` conjuncts are the ranges being the tree's own, and `_resolve = 1` is all three. The
timers' `_agree = 1` and `_freqkind = 2` from 492 are unchanged, so the run also says the earlier
reading survived the change.

**The drivers still do not touch the device.** `_phys0` is a number: `getPhysicalSegment` reads the
range out of the memory descriptor rather than out of the hardware. `/timer`'s `0xf9020000` and
`/interrupt-controller`'s `0xf9000000` are inside the `io_ranges` window the payload maps
(`stage90_main.c`'s `io_ranges = {0, 0xf9000000, 0x07000000}`, and the payload's GIC is at `0xf9000000`
since 482), but "the payload mapped it" is not "the kernel's page tables map it", and 492's rule stands:
the first register read belongs to the step that proves the mapping first. `/interrupt-controller` is
the node whose addresses this project already holds an independent reading of, which is why it is the
second one.

## A second driver is what makes the bucket a layer

The step's first question - is the `IOPlatformDevice` bucket a property of the catalogue or a property
of `/timer`? - has an answer in the catalogue wrapper's own tallies, taken over the same 30 calls as
492:

| what | 492 | 493 |
| --- | --- | --- |
| calls answered with two candidates | 1 (the root nub) | **26 (every nub)** |
| calls answered with one | 27 | 2 |
| calls answered with none | 2 | 2 |
| `_some` / `_none` / `_max` | 27 / 2 / 2 | 28 / 2 / 2 |

Every nub is now offered **both** personalities and starts the one whose names its node carries, which
is the difference between "a personality matched a node" and "the bucket is searched and the candidate
test decides". The falsification the step was built to be able to see - a GIC driver starting on the
`/timer` nub, or `_match = 3` - did not happen: `_match = 1` on both rows, on the node whose `name` is
the one that matched.

The two personalities share a bucket and an `IOProbeScore`, which is correct rather than sloppy: a score
ranks **siblings in one provider-class array** (`IOServiceOrdering` over `gIOProbeScoreKey`) and is
consulted only when more than one entry could claim the same provider, which two entries naming
disjoint node names cannot.

## The census, and the two rows that now have a child

    pex_seen = pex_shown = pex_named = 0x1d (29 of 29)   pex_registered = pex_matchpass = 0x1c
    pex_inactive = 0    pex_kids = pex_deep_l1 = 0x2

Row `0x19` (`interrup…`, state0 `0x1e`) and row `0x1a` (`timer`, state0 `0x1e`) each read
`kids_of = 1`, and each row's child pointer is the `_prov` its driver published
(`0xc0485708` and `0xc0485760` in run 2) - so the census and the driver are two readings of the same
attachment rather than two claims. `MSM8974R…` (`MSM8974RootResource`) still reads `state0 = 0` at
census time, owed since 491. Both runs produce the same 29 rows with the same `kids_of` pattern, byte
for byte in structure; the two rows' ordinals are the same in both (the row numbering is the walk's, and
this step did not change the tree).

## The check, extended: eleven claims, 36 mutations

`tools/check_driver_catalogue.py` grew from 8 claims/14 mutations to **11 claims/36 mutations**, all
mutations refused. Seven claims were rewritten to iterate `facts["drivers"]` - one record per
personality whose `IOProviderClass` is the class Apple's `createNub` builds, each with the node its
names point at - so a third device personality is checked by adding it to the table and nothing else.
The four new claims are all about the two readings of one device's address:

  * **the root declares the counts its own `reg` arrays are written for**: Apple's defaults are read out
    of `IODTGetCellCounts` (and refused if they are no longer 1/2, because the whole defect is derived
    from them), the declaration is read out of the tree, and for each driver's node the claim does the
    OS's own arithmetic - `len(words) / cells` - and requires it to equal the number of `{address, size}`
    pairs that node writes, and requires the declared width **not** to be the width the defaults would
    have produced, or the declaration is a no-op. It also refuses a `#size-cells = 0`, which
    `IODTResolveAddressing` `break`s on before reading any length at all.
  * **the driver's resolution record is the OS's reading**: `getDeviceMemory()`,
    `getDeviceMemoryCount()` and `getPhysicalSegment(` are all called; the count the OS's answer is
    compared against is derived from the node's own `regwords` **with the divisor the root's declaration
    implies** (so the tree's declaration and the drivers' arithmetic cannot move apart); the property
    compared is the property `getNubResources` resolves, read out of Apple's file; and every number the
    comparison names is published under the driver's own key prefix, so a `_resolve` of 2 is explainable
    from the record. This is the claim that made `_reg1` necessary rather than nice.
  * **Apple's file still does what the headers derive**: `getNubResources` returns success on every path
    (a failure return there would make an empty resolution a *different* failure, before any driver
    runs), `IODTResolveAddressing` is still what files `gIODeviceMemoryKey`, and the no-`ranges` arm is
    still the identity with `offset` at 0.
  * **the class the array holds is the class the driver reads it as**: the cast in `withRange` and the
    class its factory builds are read out of Apple's two files, the class chains are read out of the two
    headers, and the class the driver reads the entry as must be the built class or one of its bases.
    Stated as a derivation on purpose: the claim would still pass on the day Apple's `withRange` built a
    real `IODeviceMemory`. The driver must also publish `_objkind` and `_objlen`.

The mutation that matters most is **`the_driver_reads_the_entry_as_an_io_device_memory`**, which
re-introduces the defect run 1 actually measured - a check that can see the thing that happened rather
than a hypothesis about it. Four others are the defects this check had in itself while being written,
all found by running it and none by reading it:

  * `method_body`'s first form anchored on the qualified name **at the start of a line**, which no
    definition in this tree satisfies (`IODeviceMemory * IODeviceMemory::withRange(` has its return type
    in front); the second form then matched a **call** and followed it into the callee's brace, because
    `.*?\n{` crosses a `);`. The working form skips occurrences preceded by `(`, `::`, `.` or `->` **and**
    forbids a `;` between the name and the brace.
  * `cell_count_defaults` was fine; `c_int_array` was not - it parsed `0xf9000000u` with `int(t, 0)` and
    raised `ValueError` the first time a claim asked for a `reg` array.
  * `resolve_condition` matched from the first `else if(` in the file, so the "condition" it returned was
    most of the driver; a condition cannot contain a `;`, `{` or `}`.
  * the identifier scan over that condition counted the `u` of `2u` as a number the comparison names and
    demanded a `xnu_live_timerdrv_u` key for it.

## What is owed

  * **`/interrupt-controller`'s registers**, i.e. the same first read 492 owes for `/timer`: the kernel's
    page tables have to be shown to map `0xf9000000`/`0xf9020000` before a driver touches either.
  * **The other device nodes** (`/arm-io`, `/cpus`, …) have the same shape as `/timer` and no driver.
  * **The two services the catalogue answers with nothing**: the record publishes their pointer and not
    their class (492's, unchanged).
  * **`MSM8974RootResource`'s `state0 = 0`** at census time, owed since 491.
  * **`IOPMrootDomain`'s start** through `startCandidate`'s vtable slot rather than inferred from an
    attachment (491's).
  * **A name/class reader wider than eight characters**: `IOPlatfo…` cannot separate
    `IOPlatformDevice` from `IOPlatformExpert` - and both of this step's new rows print `interrup…` and
    `timer` under the same cap.
  * The release as a reading, `vm_fault` as a caller-side record, 488's deferred flag-list derivation,
    490's distinct-`(pc, lr)` frames band, and `xnu_live_dec_same` - unchanged by this step.

## The build

Canonical four steps, all green: the pool (626 C tried, 625 compile, 83/83 C++, platform block 4 C++ and
1 C), `gen_assym.sh` (266 defines), `assemble_arm_layer.sh` (17 ok, 0 failed), `build_entry.sh`
(`xnu_arm_entry.bin` 5487228 bytes, `.text` 5262656, `.bss` 362632, 27 undefined symbols, 59 wraps),
`stages/stage90/build.sh` (`.bin` 5982532, `.img` 5986304, `.qcdt.img` 8507392, sha256 `24cc8dac…` and
`c2e9e021…`). Against 492 the image grew 16388 bytes - four pages - and the OS's own console block is
byte-identical to 492's **except one address**: `Added memory device md0/rmd0` moves from
`0000000080505000` to `0000000080509000`, which is the payload's own layout following the image's
`+0x4000`. Nothing the OS decides moved with it.

## Safety

Two runs, each through `preflight_boot_check.sh --allow-xnu-entry` then
`run_and_capture.sh --allow-xnu-entry`, non-persistent `fastboot boot` only. Both exit 0, both captured
(`/tmp/stage90-493-run1.log` 551306 bytes, `/tmp/stage90-493-run2.log` 551655 bytes), both ending
`No errors detected`, both returning to Android on their own - the hardware watchdog and the software
dead-man armed for the run and neither spent (`sleh_seen = 0x20`, `sleh_redirected = 0x1a` in both, as
in 492). No persistent write of any kind: the image is only ever `fastboot boot`ed.
