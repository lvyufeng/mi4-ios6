# Experiment 494 — the device read through the kernel's own mapping

Date: 2026-09-21
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured twice
Artifacts: `stages/stage90/{xnu_platform/MSM8974GIC.cpp,xnu_platform/MSM8974Timer.cpp,
xnu_arm_boot/entry_gic.c,xnu_arm_boot/entry_gic.h}`, `tools/check_driver_catalogue.py`

**Result: the two device drivers no longer hold a number - they hold the device.** 493 made the OS's
resolution of each node's `reg` readable as `_phys0`, a physical address read out of the memory
descriptor the OS built; a number out of a memory descriptor is not an access. 494 maps *that very
object* through Apple's own `IOMemoryDescriptor::map( kIOMapAnywhere )` and reads a register of the
device through the address it returns. Both mappings work, both reads come back with the device's own
words, and the step's falsifier fires in the affirmative: **`GICD_TYPER` read through the OS's mapping
equals the value the payload's own probe read through its own 1 MB section at `0xf9000000`**. Run 2:

    /interrupt-controller          /timer
    _resolve    = 1                1                    493's reading survived unchanged
    _phys0      = 0xf9000000       0xf9020000           the OS's range == the node's own `reg[0]`
    _objlen     = 0x1000           0x1000
    _map        = 0xc0505dc8       0xc0505d80           the `IOMemoryMap *` the map call returned
    _mapvaddr   = 0xc203d000       0xc203e000           a kernel VA, one page per node
    _mapvlen    = 0x1000           0x1000
    _mapctlr    = 0x00000001        -                   `GICD_CTLR` read through the mapping
    _maptyper   = 0x00000468        -                   `GICD_TYPER` read through the mapping
    _probetyper = 0x00000468        -                   the payload's own read of that register
    _hwok       = 1                -                    the two agree
    _rd0        = -                 0x00000000          the first word out of the GPT window

The payload's own live keys in the same run agree on both registers it read:
`xnu_live_gic_dist_typer = 0x00000468` and `xnu_live_gic_dist_ctlr = 0x00000001` - so the register the
driver compares (`TYPER`) and the one it merely reads (`CTLR`, the enable bit 482's probe found set)
are each independently accounted for. The two kernel VAs are exactly one page apart in **both** runs
(`0xc2009000`/`0xc200a000` in run 1, `0xc203d000`/`0xc203e000` in run 2) and each `_mapvlen` is
`0x1000`: two 4 KB device pages handed out by the OS's own mapping machinery, not the payload's
identity-mapped `0xf9000000`/`0xf9020000`.

The boot's console frontier did **not** move: the OS console block is byte-identical to 493's (1288
characters, sha256 `a0593af0…`, ending `load_init_program: attempting to load /sbin/launchd`), and the
fault counters are the same 32 aborts with `_armed = 0x1c`, `_redirected = 0x1a`, `_storm = 9` as
493's run - in which no driver touched a device at all. Nothing here claims the boot advanced.

## Why the number had to become an access

492's rule, spent here: *the first register read belongs to the step that proves the mapping first.*
`_phys0` is what the OS **decided** about `reg`; the payload's own identity map of
`0xf9000000`/`0xf9020000` (482, `io_ranges` in `stage90_main.c`) is what the *payload* did; neither is
"the kernel's page tables map this device for a kext". 494 is the step that reads through the kernel's
own answer, once, and publishes what came back - and it reads only. The first **write** to a device
register from the OS side belongs to the step that has something to program, and this one has nothing.

## What `map()` does on this object, read out of Apple's files rather than assumed

All of it is in `IOMemoryDescriptor.cpp`, and every symbol named is defined in *this* image:

  * `IODeviceMemory::withRange` is `IOMemoryDescriptor::withAddressRange( start, length,
    kIODirectionNone | kIOMemoryMapperNone, NULL )` (`IODeviceMemory.cpp:34-39`) - **the task is
    NULL**, and `withAddressRanges` turns that into `kIOMemoryTypePhysical64`
    (`IOMemoryDescriptor.cpp:1176-1179`) while building `new IOGeneralMemoryDescriptor` (`:1162-1181`).
    So the range really is a *physical* range and not a virtual one in some task - which is what makes
    the object `getPhysicalSegment` reads (`_phys0`) and the object this step maps the same range.
  * `IOMemoryDescriptor::map( options )` is `createMappingInTask( kernel_task, 0,
    options|kIOMapAnywhere, 0, getLength() )` (`:4375-4381`), and `createMappingInTask` adds
    `kIOMap64Bit` (`:4418`) - the flag `doMap` and `makeMapping` both panic without (`:3596`, `:4515`).
  * With `_task == NULL`, `IOGeneralMemoryDescriptor::doMap`'s "mapping source == destination"
    shortcut **cannot** fire: it is guarded on `_task` (`:3623-3635`), so the work reaches
    `memoryReferenceCreate`, whose `_task == NULL` arm is the **device pager** arm -
    `device_pager_setup( NULL, reserved, size, pagerFlags )` with `DEVICE_PAGER_CONTIGUOUS` for a
    single range and `mach_memory_object_memory_entry_64` over it (`:641-678`).
  * That pager's `data_request` maps each page from `getPhysicalSegment`, which for a `Physical64`
    descriptor **is** the physical address - the page the OS resolved from the tree's `reg`.

  `device_pager_setup` `0x800a4634`, `device_pager_data_request` `0x800a43f8`,
  `IOMemoryDescriptor::getKernelReserved()` `0x8015a0e0`, `IOMemoryDescriptor::map(unsigned long)`
  `0x801606d4`, `createMappingInTask` `0x801605ac`, `IOMemoryMap::getVirtualAddress()` `0x80160204`
  and `IOGeneralMemoryDescriptor::doMap` `0x8015f0e8` are all in the linked image, so this is a path
  through code the run executed and not a description of code that might be there.

What the run shows is the *consequence* of that path and is stated as such: a kernel VA came back,
whose page holds the device's registers, from a descriptor whose range the OS resolved. The equality
with the payload's own read is what makes it a reading of the device rather than a reading of a page
of RAM that happened to be mapped - see the next section for why that equality can be checked here at
all, and why it cannot be checked on `/timer`.

## The one value in this step that has two definitions

The comparison is a *value* comparison, and it is only possible because the register chosen is
read-only and constant for the life of the part. `GICD_TYPER` is the distributor's identification
register, so:

    entry_gic.c:399   dist_typer = gicd_read( STAGE90_GICD_TYPER );      the payload's own read, through
                                                                          its 1 MB section at 0xf9000000
    entry_gic.c:415   g_stage90_gic_dist_typer = dist_typer;             published by name, next to the
                                                                          live key that carries it (409)

    MSM8974GIC.cpp    maptyper = *(volatile uint32_t *)( mapvaddr + MSM8974_GICD_TYPER_OFF );
                      probetyper = g_stage90_gic_dist_typer;
                      hwok = ( maptyper == probetyper ) ? 1u : 0u;

Two pieces of code, two planes, two different mappings, one register, and `_hwok = 1`. **The store at
`:415` happens before this probe writes any register at all** - the first `gicc_write` is the
CPU-interface guard below it (`:425`) - so the value the driver compares against is the register as
the probe *found* it and not one the probe caused. And because the payload's own live key
`xnu_live_gic_dist_typer` carries the same number to the log, the chain has three records of one
value: the payload's live key, the payload's named symbol, and the driver's mapped read.

The value is worth one line of decoding, because it is not a magic constant: `0x468` is
`ITLinesNumber = 8` (9 x 32 = **288 interrupt lines**), `CPUNumber = 3` (4 CPU interfaces - the four
cores of this part) and `SecurityExtn = 1`. A register whose fields agree with the machine is a
harder thing to read by accident than an arbitrary word, and it is the *same* word in every run so
far.

**`/timer` gets no such comparison, and the step says so rather than inventing one.** The GPT at
`0xf9020000` has **no second definition on this machine**: nothing in this project has ever read a
word out of that block, and the payload's own timer handlers use the architectural counter (`CNTV`),
not this device. `_rd0` is therefore published raw - a first reading, at the offset the payload's
header would name if it named one (`MSM8974_TIMER_REG0_OFF = 0x000`, which exists because the
driver's read needs a *named* offset for the check to resolve, not because anything else in the tree
claims that register is there). What `_rd0 = 0x00000000` establishes is that the mapping is usable:
a word came back from the device's own window, and - see Safety - no abort was recorded anywhere in
the run. What the word *means* is owed to the step that finds a second way to read that block.

## The census, and the ordinals that were the rows' positions

491's third census is unchanged by this step - 29 of 29 rows, `_registered = _matchpass = 0x1c`,
`_inactive = 0`, `_kids = _deep_l1 = 0x2`, and `_kids_of = 1` on rows `0x01` (`IOPMroot…`), `0x19`
(`interrup…`), `0x1a` (`timer`) and `0x1b` (`IOResour…`). Each of the two device rows' `_child` is the
`_prov` its driver published in the same run: `0xc04ea8c0` for `interrup…` and `0xc04e9738` for
`timer` in run 2, in run 1 `0xc04bc868` and `0xc04bb6e0` - the census and the driver are two readings
of one attachment.

**And this step corrects those two ordinals, in four records.** 493's doc, its memory entries and
this project's defect table all named the rows `seq = 0x1a` (`interrup…`) and `seq = 0x1b` (`timer`).
The rows' own `xnu_live_pex_seq` keys read **0x19** and **0x1a**; `0x1a` is the *timer*, so a reader
who greps the doc's number for `interrup…` lands on the other row. The numbers were the rows'
**positions in the printed dump**, which are one higher than the keys because the census writes an
unnumbered header row first. The keys, read out of the log in all four runs (the addresses here are 494 run 2's):

    seq=0x19 depth=2 child=0xc04ea8c0 name='interrup'        <- /interrupt-controller, seq 0x19
    seq=0x1a depth=2 child=0xc04e9738 name='timer'           <- /timer, seq 0x1a
    seq=0x1b depth=1 child=0xc04ea080 name='IOResour'

This is defect 287's class a second time - a number taken from where a row sits rather than from what
it says - and the correction is recorded rather than quietly made, because the *previous* correction
of the same reading had already recorded the position as the value.

## The check, extended: thirteen claims, 46 mutations

`tools/check_driver_catalogue.py` grew from 11 claims / 36 mutations to **13 claims / 46 mutations**,
all mutations refused (`--selftest`, run by `build_entry.sh:28181-28182` on every build, both modes
green in this step's log at lines 921-922). The two new claims are the access, and the ten new
mutations are its ways of going wrong:

  * **`claim_device_mapping`** - the map is called **on the object the OS's entry was cast into**
    (`range->map( kIOMapAnywhere )`, and `range` is the `OSDynamicCast` result, so a driver that built
    its own descriptor, or read the node's `reg` directly, is refused); the address the read goes
    through comes from *that* map's `getVirtualAddress()`; the map's length is published; **every read
    offset is a named definition that resolves** (so `MSM8974_GICD_CTLR_OFF` and
    `MSM8974_GICD_TYPER_OFF` cannot be inline literals); and both guards - `if( <map> != 0 )` and
    `if( <vaddr> != 0u )` - are present **and precede** the read, because a dereference of zero on this
    machine is a data abort that would name nothing.
  * **`claim_device_value`** - the payload's chain is read out of the payload: `gicd_read(
    STAGE90_GICD_TYPER )` -> the variable it lands in -> a **named non-`static` global**, whose
    declaration must be in `entry_gic.h`; the driver must declare that same symbol `extern "C"`; the
    two sides of the comparison are derived from *whichever* name is in the driver's mapped read, so
    the verdict cannot be about two names that no read produced; the payload's side must be assigned
    from the payload symbol; the driver's offset and the payload's offset must be the same number; and
    `_maptyper`, `_probetyper` and the verdict must all be published. The driver with **no** payload
    symbol - `/timer`, correctly - is *noted* rather than failed, and the note is what says out loud
    that its read has one definition.

The ten mutations, and the distinct reason each is refused (the reasons are the check's own words):

    the_driver_maps_a_descriptor_it_built_itself     the mapped object is not the one the OS's array holds
    the_driver_reads_the_node_s_own_address          neither operand is assigned from a word read through the mapping
    the_driver_drops_the_guard_on_the_address        one of the two guards is missing
    the_driver_publishes_no_mapping                  `_map` is not published: a guard that fails silently
    the_driver_stops_publishing_the_mapped_value     `_maptyper` is not published: a verdict whose cause is not in the record
    the_driver_reads_at_another_offset               the two definitions of which register this is disagree (0x008 vs 0x004)
    the_comparison_is_against_a_typed_constant       the verdict's other side is a name the payload's read does not reach
    the_header_moves_the_offset                      the header's offset and the payload's disagree
    the_payload_publishes_a_constant                 the symbol is not the probe's own read
    the_payload_stops_publishing_the_value           the header does not declare the symbol the driver declares `extern`

Three defects in the check itself were found by running it, not by reading it, and each is recorded
with the step's own numbers (290-292): a cast reader that recognised only the `T * v = OSDynamicCast(
… )` spelling and answered `None` once the driver hoisted the variable (so the claim refused a correct
file - the safe direction, and 286's class); a selftest mutation whose needle was that same spelling,
so after the hoist the mutation matched nothing and the selftest **raised** instead of refusing
(275's class, one step after 275); and a verdict that could be *skipped* - when the mutation removed
the mapped read, the comparison's mapped side had no assignment, the check compared a non-mapped
variable against the payload's, and the mutation was accepted. A claim that can pass because the
program it describes no longer exists is not a claim, and the fix is the "neither side is a device
read" failure above.

## What is owed

  * **`/timer`'s second definition**: a second, independent way to read the GPT at `0xf9020000`, so
    that `_rd0` stops being a value with one definition - and with it, what offset 0 of that block
    holds. Named in every build by this step's own check, whose note for `/timer` says out loud that its
    read has one definition on this machine.
  * **The other device nodes** (`/arm-io`, `/cpus`, …) have the same shape as `/timer` and no driver.
  * **A device *write*, and the use a mapping is for**: no driver installs an interrupt handler or
    programs its distributor yet; the GIC driver reads `GICD_CTLR` and does not set a bit in it.
  * **The two services the catalogue answers with nothing**: the record publishes their pointer and
    not their class (492's, unchanged).
  * **`MSM8974RootResource`'s `state0 = 0`** at census time, owed since 491.
  * **`IOPMrootDomain`'s start** through `startCandidate`'s vtable slot rather than inferred from an
    attachment (491's).
  * **A name/class reader wider than eight characters**: `IOPlatfo…` cannot separate
    `IOPlatformDevice` from `IOPlatformExpert`, and every device row prints under the same cap.
  * The release as a reading, `vm_fault` as a caller-side record, 488's deferred flag-list
    derivation, 490's distinct-`(pc, lr)` frames band, and `xnu_live_dec_same` - unchanged by this
    step.

## The build

Canonical four steps, all green, and the numbers barely move - because they cannot:
`build_xnu_arm_kernel.sh` (626 C tried, 625 compile, 1 fail, 7 absent, 83/83 C++, platform expert 4
C++ and 1 C, tables 3 C), `gen_assym.sh` (266 defines from 571 lines), `assemble_arm_layer.sh` (17
ok / 0 failed, 4 files translated, 26 symbols de-underscored), `build_entry.sh` (entry base
`0x80000000`, entry point `0x80000074`, `.text` **5262656 -> 5263456**, image **5487228** unchanged,
`.bss` `0x8053ba80..0x80594308` 362632 **unchanged**, headroom 1490168, 27 undefined symbols, 59
wraps: 53 reached, 1 same-object, 1 never called, 4 by address), `stages/stage90/build.sh` (`.bin`
5982532, `.img` 5986304, `.qcdt.img` 8507392 - **the same three sizes as 493** - with sha256
`a91188fa…`, `fabb004c…`, `2adbe1a2…`).

The sizes are 493's and the hashes are not, and that is the linker's slots rather than a coincidence:
`.text` grew 800 bytes and `.bss` 4 (`g_stage90_gic_dist_typer`), both inside the slack before the
16 KB-aligned `.data`, so image, `.bin`, `.img` and `.qcdt.img` all stay the size they were while
every byte of content that moved is a different byte. The one number that had to move is `.text`, and
it moved by the code this step added: the two drivers' map-and-read blocks, their seven and four new
live writes, and the payload's one store. (The chain of `.text` through this stretch of the walk is
monotone and each step's delta is accounted for: 490 `5254728`, 491 `5261096`, 493 `5262656`, 494
`5263456`.)

## Safety

Two runs, each through `preflight_boot_check.sh --allow-xnu-entry` then
`run_and_capture.sh --allow-xnu-entry`, non-persistent `fastboot boot` only. Both exit 0, both
captured (`/tmp/stage90-494-run1.log` and `/tmp/stage90-494-run2.log`, 551915 bytes each, sha256
`88bfc7db…` and `b7ab33f8…`), both ending `No errors detected`, both returning to Android on their
own - the hardware watchdog and the software dead-man armed for the run and neither spent.

**And the run is evidence about the reads themselves, which is why the fault counters are in this
section rather than in a footnote.** `xnu_live_sleh_seen = 0x20` (32 data aborts), `_armed = 0x1c`
(28), `_redirected = 0x1a` (26) and `_storm = 9` are *identical* to 493's run - in which no driver
read a device register at all - and there is no `exception:` line, no panic and no `stub_hit=`. A
read that faulted would enter the same data-abort path that counts those 32: six device reads (two GIC
registers twice, one GPT word twice) went through two mappings the OS made and added nothing to any
counter. That is the same argument 493's doc makes for "the drivers do not touch the device", turned
around: **they do touch it now, and the machine did not notice.** No persistent write of any kind: the
image is only ever `fastboot boot`ed.
