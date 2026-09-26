/*
 * The machine's timer node gets a driver of its own (experiment 492) - the first driver this project
 * links in that is matched to a **device node** rather than to the platform expert or the resource
 * root.
 *
 * Why a device node is the missing piece
 * --------------------------------------
 * 491's census settled what the driver layer looks like on this machine: `MSM8974P` - the fixture's
 * `IODTPlatformExpert` subclass - has 26 service-plane children, the nubs `createNubs` built. Every
 * one of them was `Registered|Matched|FirstPublish|FirstMatch` and none of them had a service-plane
 * child, and the only two services in the whole registry with a driver attached were the platform
 * expert itself and `MSM8974RootResource` under `IOResources`. The reason is not in the matching
 * code, which 490 and 491 both read to the bottom: it is in the **catalogue**. `doServiceMatch`
 * decides everything on
 *
 *     matches = gIOCatalogue->findDrivers( this, &catalogGeneration );   // IOService.cpp:3688
 *     if (keepGuessing && matches->getCount() && (kIOReturnSuccess == getResources()))  // :3724
 *
 * and `findDrivers(IOService *, SInt32 *)` (`IOCatalogue.cpp:199-231`) looks the personalities up by
 * **the service's own class chain**:
 *
 *     meta = service->getMetaClass();
 *     while (meta) {
 *         array = personalities->getObject(meta->getClassNameSymbol());
 *         ... if (meta == &IOService::gMetaClass) break; meta = meta->getSuperClass();
 *     }
 *
 * because `addPersonality` (`:124-132`) files each personality under its `IOProviderClass` value.
 * The kernel's table is three entries before this step - `MSM8974PlatformExpert` and `IOPanicPlatform`
 * under `IOPlatformExpertDevice`, `MSM8974RootResource` under `IOResources` - and the nubs this
 * machine's device tree produces are `IOPlatformDevice`: `IODTPlatformExpert::createNub` is
 * `nub = new IOPlatformDevice` (`IOPlatformExpert.cpp:1283`). So no personality in the catalogue
 * could be filed under the class any nub has, `findDrivers` answered every one of them with an empty
 * set, `probeCandidates` was never called for them, and no driver instance was ever built.
 *
 * What makes this class match
 * ---------------------------
 * The personality in `stage90_platform_config_tables.c` is the whole of the effect:
 *
 *     'IOClass' = MSM8974Timer; 'IOProviderClass' = IOPlatformDevice;
 *     'IONameMatch' = (timer, "qcom,msm-timer"); 'IOProbeScore' = 1616:32;
 *
 * `IOProviderClass` is the bucket, and `IOPlatformDevice` is read out of Apple's own `createNub` and
 * not guessed: the class chain of a nub is `IOPlatformDevice` then `IOService`, so a personality under
 * either name is reachable and one under `IOPlatformExpertDevice` - the class of the *root* nub, the
 * one the platform expert's own entry names - is not. `IONameMatch` is what the candidate test asks
 * for later: an `IOPlatformDevice`'s `compareName` is `((IOPlatformExpert *)getProvider())->
 * compareNubName(this, ...)` (`IOPlatformExpert.cpp:1688-1693`), i.e. `IODTCompareNubName` over the
 * provider nub's `name`, `compatible`, `device_type` and `model` (`IODeviceTreeSupport.cpp:799-824`),
 * and the two names listed are the `/timer` node's `name` and its `compatible`. No `CFBundleIdentifier`
 * is present on purpose: `probeCandidates` stalls on `gIOCatalogue->isModuleLoaded(match)`
 * (`IOService.cpp:3253`) and that function answers "true" for exactly the personalities that carry no
 * bundle id - "assumed to be an in-kernel driver" (`IOCatalogue.cpp:475-496`), which this is.
 *
 * What it does, and what it does not
 * ----------------------------------
 * It is a driver and not a stub: `start` reads its **provider's** description of the device - the
 * `frequency` and `reg` properties of the node the personality matched - and reads the machine's own
 * answer to the same question out of the CPU (`CNTFRQ`, `mrc p15, 0, r, c14, c0, 0`, the register the
 * payload's `timebase.c` reads at boot), then records both and whether they agree. That comparison is
 * the point: the device tree's 19,200,000 Hz and the hardware's CNTFRQ are two definitions of one
 * value on this machine, and until this step nothing compared them at run time - the tree's is read by
 * the tree's own selftests host-side and the register's by the payload, in two different programs.
 *
 * **Up to 493 it did not touch the device, and 494 makes it.** The node's `reg[0]` was recorded and not
 * dereferenced: on this machine `0xf9020000` is inside the `io_ranges` window the payload maps
 * (`stage90_main.c`'s `io_ranges = {0, 0xf9000000, 0x07000000}`), but "the window is in the payload's
 * page tables" is not "the kernel's page tables map it", and a driver that faults on its own device's
 * registers would be the instrument's fault and not the driver's. So the first register read waited for
 * the step that proves the mapping first, and 494 is that step: the driver takes the object the OS's
 * own resolution filed, calls `IOMemoryDescriptor::map( kIOMapAnywhere )` on it - the kext route to a
 * device - and reads a word through the address that call returns. **The address is the OS's, not the
 * tree's and not the payload's**: `map()` on a `kIOMemoryTypePhysical64` descriptor reaches
 * `IOGeneralMemoryDescriptor::doMap`'s `_task == NULL` arm, which is `device_pager_setup` +
 * `mach_memory_object_memory_entry_64` (`IOMemoryDescriptor.cpp:641-678`), and the page behind the
 * returned VA is the physical range `getPhysicalSegment` named. So "the kernel's page tables map the
 * device" stops being an argument about `mmu.c` and becomes a number in the log.
 *
 * **What this node's read is not, and the step is explicit about it**: `/timer`'s `_rd0` has **no
 * second definition**. `MSM8974GIC.cpp` compares its own read of `GICD_TYPER` - read-only and
 * constant - against the payload's own read of the same register, and this node has no counterpart:
 * nothing in this project has ever read a word out of the GPT at `0xf9020000`. The tree declares it,
 * `PE_state_stage90.timerBase` names it, the payload's handlers use the architectural counter rather
 * than this block. So `_rd0` is published raw as a first reading, with the mapping beside it, and the
 * second definition is owed to the step that finds one - which is better than inventing an expected
 * value and calling the comparison a measurement.
 *
 * **493: and it asks the OS where its device is, instead of decoding `reg` itself.** The two words
 * above are *this driver's* reading of the node. The kernel has its own reading, made before this
 * `start` is ever called: `IOService::doServiceMatch` on the **nub** runs
 * `kIOReturnSuccess == getResources()` (`IOService.cpp:3724`) before `probeCandidates( matches )`
 * (`:3726`), and a nub's `getResources` is `IOPlatformDevice::getResources`
 * (`IOPlatformExpert.cpp:1700-1703`) -> `getNubResources( this )` -> `IODTResolveAddressing( nub,
 * "reg", 0 )` (`:1358-1366`). That function turns the node's `reg` into `IODeviceMemory` objects and
 * files them under `IODeviceMemoryKey` on the nub (`IODeviceTreeSupport.cpp:1251`), which is exactly
 * what `IOService::getDeviceMemory()` returns (`IOService.cpp:6046-6049`). Two definitions of one
 * value - the driver's and the OS's - and this step is the first time they are compared on the
 * machine.
 *
 * It matters because they can differ, and the shape of the difference is the previous step's defect
 * one layer up: `IODTResolveAddressing` reads the **parent's** `#address-cells`/`#size-cells`
 * (`:1227`) to decide how wide one `reg` entry is, and `IODTGetCellCounts` (`:1034-1041`) answers
 * `1`/`2` for a node that declares neither - Apple's default, not this tree's. 493 declares them on
 * the root (see `stage90_main.c`); `_devcount`/`_phys0`/`_len0` are the reading that says whether the
 * declaration reached the resolution, and `_resolve` is the comparison itself.
 *
 * **And 493's first run found the second half of that defect inside this driver.** `_devcount` came
 * back right - three entries for this node's three `{address, size}` pairs, so the declaration had
 * arrived and the OS had resolved the node - while `_phys0` and `_len0` were both 0 and `_resolve`
 * was 2. The array is the OS's; the *reader* was wrong: the entry was cast to `IODeviceMemory`, the
 * type the array is documented to hold, and `IODeviceMemory::withRange` is a blind cast of
 * `IOMemoryDescriptor::withAddressRange` (`IODeviceMemory.cpp:34-39`), which builds an
 * `IOGeneralMemoryDescriptor` - a sibling of `IODeviceMemory`, not a subclass. So the cast answered 0
 * for every entry, the read was skipped, and the record said the OS had resolved nothing. The entry is
 * read as an `IOMemoryDescriptor` now, and `_objkind`/`_objlen` publish which class it was and its own
 * length, so this particular wrong-kind read cannot be invisible again. It is 492's `_freqkind`
 * lesson a second time, in the same file, one step later: **the reader's type is a reading too.**
 *
 * The record it leaves
 * --------------------
 * Thirty-nine live keys, written in the order that makes the record readable if the driver stops: `_seq`
 * (the ordinal), `_prov` (the provider pointer - the row this driver belongs to in 491's third census,
 * whose `kids_of` is 0 before this step and 1 after it), `_match` (which of the two names matched: 1 =
 * the node's `name`, 2 = its `compatible`, 3 = neither, so a match made on a name this driver does not
 * know is visible as 3 rather than silently counted), `_have` (bit 1 = the tree had `frequency`, bit 2
 * = it had `reg`, so a missing property cannot read as a zero), `_freqkind` (0 = absent, 1 =
 * `OSNumber`, 2 = a 4-byte `OSData`, 3 = an `OSData` of another length, 5 = an `OSData` whose bytes
 * could not be read - the kind is a reading, because 492's first run measured a `_freq` of 0 that was
 * the reader's cast and not the tree's value), `_freq`, `_reg0`, `_reg1` and `_regwords` (the node's
 * `reg` first address, first size and total length in 32-bit words, so "how many entries it says it
 * has" is beside "how many the OS found" and both sides of 493's comparison are in the record),
 * `_cntfrq` and `_agree`; then 493's resolution record: `_devmem` (the array the OS filed under
 * `IODeviceMemoryKey`, 0 = it resolved nothing), `_devcount` (its count), `_objkind` (1 = the object
 * in the array really is an `IODeviceMemory`, 2 = it is an `IOMemoryDescriptor` and not one - Apple's
 * blind cast in `IODeviceMemory::withRange`, which is what this machine produces - 0 = neither, so a
 * driver's own cast can never again be the invisible cause of a zero), `_objlen` (that object's
 * `getLength()`, which does not go through the segment walk), `_phys0` and `_len0` (entry 0's physical
 * address and length), and `_resolve` (1 = the OS answered and its entry 0 is this node's first `reg`
 * pair and its count is the number of pairs the node has; 2 = answered and one of those two disagrees;
 * 3 = answered with no entries; 0 = not answered at all - four outcomes because "no answer" and "an
 * answer that is wrong" are different findings and the `_resolve` key is the one a reader looks at
 * first). **`_reg1`, `_objkind` and `_objlen` are 493's additions to 492's fifteen**, and they are
 * there so the record explains its own `_resolve`: the comparison's right-hand sides are `_reg0` and
 * `_reg1` and its left-hand sides `_phys0` and `_len0`, so a `_resolve` of 2 has all four of its
 * numbers in the log, and `_objkind`/`_objlen` say whether the zero was the OS's or the reader's.
 *
 * **And 494's four, which are the access rather than the description**: `_map` (the `IOMemoryMap *`
 * `map( kIOMapAnywhere )` returned, 0 = the OS could not map the range it had just resolved - a
 * finding about the *mapping machinery*, which is why it is published rather than used as a branch
 * with nothing to show for it), `_mapvaddr` and `_mapvlen` (that map's own `getVirtualAddress()` and
 * `getLength()`), and `_rd0` (the word read at `_mapvaddr`, the node's `reg[0]` register). The two
 * guards are in the record and not only in the code: a `_map` of 0 leaves `_mapvaddr` 0, and a
 * `_mapvaddr` of 0 leaves `_rd0` 0 - so the reader can tell "the OS mapped nothing" from "the OS
 * mapped something and the driver did not read through it", which is the difference between a
 * finding about the OS and a finding about this file.
 *
 * **And 495's seventeen, which are the OS's own timeout machinery rather than a device**: the record of
 * the arming - `_ms` (the interval asked for, out of the one `#define` both `setTimeout` calls use),
 * `_wl` (the work loop, which is a kernel thread this driver caused to be created), `_ts` (the timer
 * event source), `_add` (the `addEventSource` result), `_en` (1 = the source reported itself enabled
 * after `enable()`), `_to` (the `setTimeout` result), `_arm_lo`/`_arm_hi` (`mach_absolute_time` at the
 * arming) and `_due_lo`/`_due_hi` (the deadline the **OS's own** `clock_interval_to_deadline` computes
 * for that interval) - and the record the callback writes: `_fires` (how many times the kernel called
 * this driver back, published as 0 before the arming so "never called" is a value and not an absence),
 * `_fire_lat_ns` (the OS's own `absolutetime_to_nanoseconds` of the difference between the fire and
 * **the arming that fire was for** - the driver's own for the first and the callback's re-arm for the
 * later ones, so the key is one measurement repeated three times rather than a sum that grows),
 * `_fire_lo`/`_fire_hi` (the clock at the last fire), `_gap_ns` (the same conversion between the last
 * two fires, so the interval the OS actually delivered is in the record beside the interval `_ms` says
 * it was asked for) and
 * `_rearms`/`_rearm_rc` (how many times the callback re-armed itself from inside the callback, and
 * what that call returned - which is also the reading that says the work loop's gate, held across the
 * callback, was usable rather than deadlocked).
 *
 * **Every key here is live-only, like 492's nine and like every reading since 454**, and 493 does not
 * change that: the report epilogue that writes `entry_write_485_kv` has not run since the boot reached
 * `vm_pageout` (490 measured it), and putting a driver's reading in it would mean exporting the
 * driver's state into `entry_stubs.c` for the report to restate - a second definition of a number the
 * driver already measured, which is the shape this project keeps recording. `entry_live_write` is the entry instrument's
 * own entry point, declared here rather than included, exactly as `MSM8974RootResource.cpp` declares it
 * and for the same reason: this object is linked by the entry build, which is the only link that
 * defines it, and a build that left it out would be an undefined symbol and a loud link failure rather
 * than a silent zero.
 */
#include <IOKit/IOService.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
/*
 * 498: `IODTMapInterrupts` and the two keys the OS files its answer under. The header is the one
 * `MSM8974PlatformExpert.cpp` already includes, and the two `OSSymbol *` keys it needs -
 * `gIOInterruptSpecifiersKey` and `gIOInterruptControllersKey` - are declared by
 * `IOKit/IOService.h` (`:144-145`), which is the first include above. Nothing about this mapping is
 * transcribed here: the function and both key names come out of Apple's headers.
 */
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSSymbol.h>

extern "C" void entry_live_write(const char *key, uint32_t value);

/*
 * 498's one entry point, declared here for the reason `MSM8974GIC.cpp` declares its two: this file
 * cannot include `entry_gic.h` (that header's `g_stage90_gic_dist_typer` has C linkage and no
 * `extern "C"` wrapper, so including it from C++ would give the symbol C++ linkage and a link error),
 * so the signature is written twice - once in the header and once here - and the two are compared
 * character by character by `tools/check_driver_catalogue.py`.
 */
extern "C" uint32_t entry_irq_register_client(uint32_t intid, uint32_t handler, uint32_t refCon);

/*
 * **500's entry point, and the one that turns a registration into a delivery.** A registration makes an
 * intid *serviceable*; the distributor's per-line state is what makes it *arrive*, and until this call
 * nothing in the image wrote any of it for this line. The payload owns the arithmetic - it derives the
 * enable word and bit, the target byte and the group bit from the intid - and the driver names only the
 * CPU mask it wants the line on, because that is the one thing the caller knows and the payload cannot
 * guess.
 *
 * Declared here with the same `extern "C"` prototype as the header for the reason above: this file
 * cannot include `entry_gic.h`, so this declaration *is* the ABI between the driver and the payload, and
 * `tools/check_driver_catalogue.py` requires one for every `entry_irq_*` function this file calls.
 */
extern "C" uint32_t entry_irq_enable_line(uint32_t intid, uint32_t target);

/*
 * The register this driver reads through its mapping, at the offset the node's `reg[0]` itself starts
 * at. It is a named symbol rather than a bare `0` for the reason the GIC's two offsets are named: an
 * offset written into the expression cannot be read out of the file and held against anything, and
 * `tools/check_driver_catalogue.py` resolves this name and requires it to be the one the read uses.
 * **This node has no counterpart in the payload's headers** - nothing in this project has ever read a
 * word out of the GPT - so there is no second definition of this offset and none is claimed.
 */
#define MSM8974_TIMER_REG0_OFF  0x000u

/*
 * 498: the frame's registers, at the offsets the *device's own kernel* uses.
 *
 * Grounding, and it is the whole reason this block is a reading rather than a guess. The block this
 * node describes at `0xf9021000` is the Qcom/ARM memory-mapped timer's frame, and the offsets below
 * are copied from the device's own driver, which is in this repository:

 *     external/android_kernel_xiaomi_cancro/arch/arm/kernel/arch_timer.c
 *       :60  #define QTIMER_CNTP_LOW_REG   0x000
 *       :61  #define QTIMER_CNTP_HIGH_REG  0x004
 *       :62  #define QTIMER_CNTV_LOW_REG   0x008
 *       :63  #define QTIMER_CNTV_HIGH_REG  0x00C
 *       :64  #define QTIMER_CTRL_REG       0x02C
 *       :65  #define QTIMER_FREQ_REG       0x010
 *       :66  #define QTIMER_CNTP_TVAL_REG  0x028
 *       :67  #define QTIMER_CNTV_TVAL_REG  0x038
 *       :51  #define ARCH_TIMER_CTRL_ENABLE   (1 << 0)
 *       :52  #define ARCH_TIMER_CTRL_IT_MASK  (1 << 1)
 *       :53  #define ARCH_TIMER_CTRL_IT_STAT  (1 << 2)
 *       :623 timer_base = of_iomap(frame, 0);              <- the frame's *first* reg
 *       :629 arch_timer_spi = irq_of_parse_and_map(frame, 0);  <- the frame's *first* interrupt
 *
 * and that kernel applies them to the frame's first `reg` region (`:623`), which is `0xf9021000` -
 * this node's **second** `reg` entry and the first `reg` entry of the frame node in
 * `arch/arm/boot/dts/msm8974.dtsi:161-167`. `tools/check_timer_line.py` reads those lines out of that
 * file and refuses a build where any offset here disagrees with them, so this block is a copy with a
 * check behind it rather than a second definition.
 *
 * The control word's three bits are read as three bits and not as one number: `IT_MASK` is the bit
 * that *clears* a level-sensitive line (masking the output drops the level, which is what a
 * distributor sees as pending), and `IT_STAT` is the read-only bit that says the output is asserted -
 * so a record that published `_ctl` alone could not distinguish "the timer expired and the line is
 * still held" from "the timer expired and the driver silenced it".
 */
#define MSM8974_FRAME_CNTP_LOW_OFF   0x000u
#define MSM8974_FRAME_CNTP_HIGH_OFF  0x004u
#define MSM8974_FRAME_CNTV_LOW_OFF   0x008u
#define MSM8974_FRAME_CNTV_HIGH_OFF  0x00Cu
#define MSM8974_FRAME_FREQ_OFF       0x010u
#define MSM8974_FRAME_CNTP_TVAL_OFF  0x028u
#define MSM8974_FRAME_CTRL_OFF       0x02Cu
#define MSM8974_FRAME_CNTV_TVAL_OFF  0x038u
/*
 * 502: the virtual timer's control word. It is not one of the eight offsets the device's kernel
 * declares - that file writes and reads the *physical* timer's control word at `0x02C` and never
 * touches the virtual side - so the address comes from the layout the two pairs share in its own
 * table, where a countdown and its control word are four bytes apart:
 *
 *       :66  QTIMER_CNTP_TVAL_REG  0x028
 *       :64  QTIMER_CTRL_REG       0x02C
 *       :67  QTIMER_CNTV_TVAL_REG  0x038
 *
 * so the virtual side's control word is the same pattern one timer over, at `0x03C`. The *identity* of
 * the word at a countdown's `+4` is not an inference: 501 measured it on the physical pair, where the
 * frame's `0x02C` and the coprocessor's `CNTP_CTL` read the same value (`_rt_ctl_fr` 2, `_rt_ctl_cpu` 2,
 * `_rt_ctl_agree` 1). This address is that confirmed pattern applied to the pair beside it, and the
 * claim is that the machine's answer is published (`_cv_v_ctl_fr`, `_cv_v_ctl_agree`) rather than that
 * the address is right.
 *
 * It is written as the countdown's address plus four rather than as a second literal `0x03C`, because a
 * literal beside a derivation is this project's oldest defect class with the smallest possible blast
 * radius: the two spellings cannot be made to disagree if there is only one of them.
 */
#define MSM8974_FRAME_CNTV_CTL_OFF   (MSM8974_FRAME_CNTV_TVAL_OFF + 0x4u)

#define MSM8974_FRAME_CTRL_ENABLE    0x1u
#define MSM8974_FRAME_CTRL_IT_MASK   0x2u
#define MSM8974_FRAME_CTRL_IT_STAT   0x4u

/*
 * 502: the bounds the two readings at the end of the block below are built on, and neither is a claim
 * about the machine.
 *
 * A *bracket sample* is three readings, and the two that are of the same side measure nothing but the
 * cost of the reads between them - so a sample whose own cost is large was interrupted, and the
 * difference it holds is the interrupt's duration rather than the machine's phase. `CV_COST_MAX` is
 * that refusal bound, set an order of magnitude above the measured cost (12-13 ticks) rather than at
 * it, so that a *correct* machine never has a sample refused: an unrefused sample set is what makes
 * `_cv_xv_n` eight, and a refusal is visible as a smaller count rather than as a different sum.
 *
 * The rate loop is bounded twice and both bounds are necessary: it stops when the frame's counter has
 * advanced by `CV_RATE_TICKS`, *or* when the core's clock has advanced by `CV_RATE_BOUND`, whichever
 * comes first. A frame whose counter is stopped - which would make every ratio in this file meaningless,
 * and is therefore a thing to find out rather than to wait for - ends the loop instead of hanging the
 * boot in it, and `_cv_r_ok` records which of the two bounds ended it.
 */
#define MSM8974_CV_COST_MAX     0x00002000u   /* 8192 ticks = 426 us, against a measured cost of 12 */
#define MSM8974_CV_RATE_TICKS   0x00008000u   /* 32768 ticks = 1.7 ms of the frame's counter */
#define MSM8974_CV_RATE_BOUND   0x00080000u   /* 524288 ticks = 27 ms of `mach_absolute_time` */

/*
 * Which of the node's `reg` entries is the frame. `reg` is `{parent, frame@1000, frame@2000}` - the
 * three `{address, size}` pairs the tree has carried since 492 - and entry 1 is the frame's first view,
 * the one the device's kernel maps with `of_iomap(frame, 0)`. Written once and used by the mapping
 * *and* by the physical-address comparison, so the two cannot come to disagree.
 */
#define MSM8974_TIMER_FRAME_ENTRY  1u

/*
 * 501: the frame's *second* view, and the frame's other route to the same counters.
 *
 * The frame in the device's own tree declares two `reg` regions -
 * `reg = <0xf9021000 0x1000>, <0xf9022000 0x1000>` (`arch/arm/boot/dts/msm8974.dtsi:161-167`) - and
 * the binding that sits beside that kernel says what they are:
 *
 *     Documentation/devicetree/bindings/arm/arch_timer.txt:43-44
 *         - reg : The first and second view base addresses in that order. The second view
 *           base address is optional.
 *
 * The device's kernel maps the first (`of_iomap(frame, 0)`, `arch_timer.c:623`) and nothing in it
 * ever maps the second. This project's tree has carried both since 492, flattened one level up onto
 * the node this driver matched, so the second view is that node's third `reg` entry and therefore
 * device-memory object 2 - `MSM8974_TIMER_VIEW2_ENTRY` below, and *not* the frame's entry, which is
 * the whole point of reading it separately.
 *
 * **Why a second view is worth a step.** 500 proved the frame's first view against itself - the rate
 * measured against `mach_absolute_time`, the control word's own bits - and against nothing else. The
 * *counters* are reachable a second way, and the device's own kernel is where that second way lives.
 * The block below is opened with that kernel's path on a line of its own for the same reason the 498
 * offsets block is: `tools/check_timer_line.py` finds a block that way, and every bare citation in it
 * is then read with the name written beside it and held against the line it names.
 *
 * external/android_kernel_xiaomi_cancro/arch/arm/kernel/arch_timer.c
 *     :297  counter_get_cntpct_mem      (reads the frame's 0x000/0x004)
 *     :310  counter_get_cntpct_cp15     (`mrrc p15, 0, ..., c14`)
 *     :318  counter_get_cntvct_mem      (reads the frame's 0x008/0x00C)
 *     :331  counter_get_cntvct_cp15     (`mrrc p15, 1, ..., c14`)
 *     :339  get_cntpct_func is `counter_get_cntpct_cp15`
 *     :340  get_cntvct_func is `counter_get_cntvct_cp15`
 *     :577  has_cp15 = false in its own declaration
 *     :581  has_cp15 = true, from the CPU's feature words
 *     :607  if (!has_cp15) {
 *     :608  get_cntpct_func = counter_get_cntpct_mem;
 *     :609  get_cntvct_func = counter_get_cntvct_mem;
 *
 * Two definitions of one value in one file, and the choice between them is made once and never
 * revisited: the memory route is taken **only** when the CPU has no coprocessor timer (`has_cp15`,
 * which the kernel sets from the CPU's own feature words), so on a machine that has both - this one -
 * the loser of that switch is never read again. That is this project's oldest defect family ("one
 * value, two definitions, N − 1 uncompared") sitting inside the *evidence* for the frame. This step is
 * the missing comparison: every counter the frame exposes is read through the frame and through the
 * coprocessor, and both readings are published with their difference, so a wrong offset answers as a
 * difference of ~10^7 ticks rather than as an argument.
 *
 * `QTIMER_CNTV_TVAL_REG` (`:67`, 0x038) earns its own sentence, and 501's run gave it three. The
 * device's kernel *declares* the frame's virtual countdown and never reads or writes it - it is the
 * only one of that file's eight timer offsets with no use anywhere in the file, and a search of the
 * whole kernel tree finds the name exactly once, on its own `#define` - so the name is a definition
 * with no reader in the file that wrote it. The coprocessor route to that register is `c14, c3, 0`
 * and the control beside it is `c14, c3, 1`, the pair this image's own `entry_timebase.c` writes for
 * XNU's decrementer. **And the run says the offset is not the register**: `_rt_tval_fr` reads
 * `0xf912a3cc` where `_rt_tval_cpu` reads `0x0002d8cb`, `_rt_tval_ok` 0 - a difference of 0x06f034ff
 * ticks, not of a few reads. So the one offset that file never reads is also the one offset this
 * machine contradicts, and nothing in that kernel could have noticed. **The reading is allowed to
 * say what the register is *not* and not what it is**: whether the frame's virtual countdown lives
 * in its second view, or behind a control word neither this step nor that kernel writes, is a
 * question that needs a write to answer, which is exactly what this step refuses to make.
 */
#define MSM8974_TIMER_VIEW2_ENTRY  2u

/*
 * The GIC binding's cell rule, from the same tree that declares the frame:
 *
 *     arch/arm/boot/dts/msm8974.dtsi - a node's `interrupts` is `<type number trigger>` with the
 *     controller's `#interrupt-cells = <3>`, `type 0` = a shared peripheral interrupt and `type 1` =
 *     a private peripheral interrupt. The `type 0` numbers are the ones the distributor's
 *     `GICD_ISENABLER<n>` bits are counted from intid 32; the `type 1` numbers *are* their intid.
 *
 * So the tree's `<0 8 4>` is GIC intid 40 and not 8: the +32 is the whole of the translation, and it
 * is the number a distributor register address is derived from below. A driver that read "8" off the
 * tree and enabled `ISENABLER0` bit 8 would be enabling SGI 8 - a line it can raise itself and which
 * would therefore look like it worked.
 */
#define MSM8974_GIC_SPI_BASE     32u
#define MSM8974_INTR_TYPE_SPI    0u
#define MSM8974_INTR_TYPE_PPI    1u

/*
 * 495: the interval this driver asks the OS for, and how many times the callback re-arms itself before
 * it stops. **One definition, used by both `setTimeout` calls and by the deadline the record publishes**
 * - a second copy of the number in the arithmetic would be a second definition of the same decision,
 * which is the defect class this project has paid for twenty-nine times.
 *
 * 100 ms is chosen to be unmistakable in either direction: long enough that the callback cannot be
 * confused with the work of arming it (the arming path is a few hundred instructions and one thread
 * creation), and short enough that three of them are over well before anything else in the boot
 * notices. The countdown the OS must reach for the first fire is 100 ms of a 19.2 MHz counter -
 * 1 920 000 ticks - against a `DECREMENTER_MAX` of `0x7fffffff`.
 */
#define MSM8974_TIMER_ASK_MS    100u
#define MSM8974_TIMER_FIRES      3u

/*
 * ------------------------------------------------------------------------------------------------
 * 500: the deadline this driver puts in its own device, and where it wants the line
 * ------------------------------------------------------------------------------------------------
 *
 * **The frame's deadline, and it is this driver's own interval rather than the OS's.** 495's
 * `MSM8974_TIMER_ASK_MS` is what the driver asks *the OS* for through `setTimeout`; this is what it
 * puts into the frame's own `CNTP_TVAL`, and the two are different questions with the same shape. Ten
 * milliseconds at 19.2 MHz is 192 000 ticks - short enough that three of them are over while the first
 * service is still in the log, long enough that the arming code is not what the callback measures. The
 * tick count is *derived* from the frequency the frame itself reports (`_frame_freq`, 19 200 000, and
 * 498 measured that the tree's, the CPU's and the frame's three definitions agree), never written down:
 * the register the driver programs counts that clock, so a literal here would be a fourth definition of
 * the same rate.
 *
 * **The target byte, and why the number is 1.** `GICD_ITARGETSR`'s byte for an SPI is a CPU mask - bit
 * *n* selects CPU interface *n* - and this image runs on one CPU. `1` is therefore "CPU 0", and it is the
 * value this machine already holds for the word the OS's own line lives in: 498's `_irq_targets_word`
 * read `0x01010101` for the PPI word. The caller names it rather than the payload because the *driver* is
 * the one that knows which CPU it wants its line on; the payload owns the arithmetic that turns it into
 * the byte offset.
 *
 * **What the driver does not name is the priority.** It is not written at all: the distributor's own
 * value for the line is kept and published beside the CPU interface's mask, so "the line never arrived"
 * and "the line's priority is outside the mask" stay different readings.
 */
#define MSM8974_FRAME_ARM_MS     10u
#define MSM8974_TIMER_LINE_TARGET 1u

/* `CNTP_TVAL` is a 32-bit down-counter: a deadline of 0 is "already expired" and anything above this is
 * not a value the register holds. Both are refusals rather than arming with a wrapped number. */
#define MSM8974_FRAME_TICKS_MAX  0x7fffffffu

/*
 * The three OS functions 495 adds to this file's reading of the machine. `mach_absolute_time` is the
 * kernel's own clock (on this part, the architectural counter); `clock_interval_to_deadline` is the
 * OS's own conversion of an interval into a deadline, which is what the record publishes as the moment
 * the callback *should* have run; and `absolutetime_to_nanoseconds` converts a difference of two
 * readings of that clock into a number that can be held against the interval the driver asked for.
 *
 * All three come in through `<IOKit/IOTimerEventSource.h>` -> `<kern/clock.h>` -> `<mach/mach_time.h>`,
 * so they are declared by Apple's headers and not transcribed here. Reading them out of the header is
 * the point: the units of `clock_interval_to_deadline`'s third argument are the OS's decision, and a
 * driver that assumed a return value instead of the out-parameter the header declares would compute a
 * deadline no one could compare with the fire.
 */
static uint32_t g_timer_fires;
static uint32_t g_timer_rearms;
static uint32_t g_timer_rearm_rc;
static uint64_t g_timer_armed;
static uint64_t g_timer_last;

/*
 * The callback. This is the function handed to `IOTimerEventSource::timerEventSource` and the only
 * thing in this image that the OS's timeout machinery calls back into a driver.
 *
 * It is a plain function rather than a member because that is the type the event source takes
 * (`typedef void (*Action)(OSObject *owner, IOTimerEventSource *sender)`, `IOTimerEventSource.h:158`),
 * and it runs **on the thread-call daemon with the work loop's gate held** (`IOTimerEventSource.cpp`
 * :146-176: `timeoutAndRelease` closes the gate, invokes the action and opens it again), which is why
 * re-arming from inside it is legal - it is the same call a real driver makes from its own timeout, and
 * the run is what says the gate is usable rather than deadlocked.
 *
 * Every number it publishes is a reading of the *machine*: `mach_absolute_time()` before and after, the
 * OS's own conversion of the difference into nanoseconds, and the count of fires. `_fires` is published
 * on every call and its low values on the way up are published too, because the log is captured when
 * the watchdog brings the phone back and a number written only at the end is a number never read.
 *
 * **And `_fire_lat_ns` means the same thing at every fire, which is 495's first run's one defect.** The
 * re-arm moves `g_timer_armed` to a fresh `mach_absolute_time()` taken immediately before the
 * `setTimeout`, exactly as `start` reads its own arming instant. Without that line the key would be
 * "since the *first* arming" - a cumulative sum that grows by one interval per fire and reads in the
 * log exactly like a latency, so fire 1's 101.105 ms, fire 2's 202.151 ms and fire 3's 303.199 ms would
 * all be printed under a name that says "how long the OS took to call this driver back". With it, every
 * value is the arm-to-fire time of *that* fire, the asked-for interval is `_ms` for all of them, and
 * the difference between the two is the OS's dispatch latency - the number a reader holds the 100 ms
 * against.
 */
static void
msm8974_timer_timeout( OSObject * owner, IOTimerEventSource * sender )
{
    uint64_t now = mach_absolute_time();
    uint64_t since_arm = 0u;
    uint64_t since_last = 0u;
    uint64_t previous = g_timer_last;
    uint32_t fires = ++g_timer_fires;

    (void) owner;

    g_timer_last = now;

    absolutetime_to_nanoseconds( now - g_timer_armed, &since_arm );
    if( fires > 1u && previous != 0u)
        absolutetime_to_nanoseconds( now - previous, &since_last );

    entry_live_write( "xnu_live_timerdrv_fires", fires );
    entry_live_write( "xnu_live_timerdrv_fire_lat_ns", (uint32_t) since_arm );
    entry_live_write( "xnu_live_timerdrv_fire_lo", (uint32_t) now );
    entry_live_write( "xnu_live_timerdrv_fire_hi", (uint32_t)( now >> 32 ) );
    if( fires > 1u)
        entry_live_write( "xnu_live_timerdrv_gap_ns", (uint32_t) since_last );

    if( fires < MSM8974_TIMER_FIRES && sender != 0) {
        g_timer_armed = mach_absolute_time();
        g_timer_rearm_rc = (uint32_t) sender->setTimeout( MSM8974_TIMER_ASK_MS, kMillisecondScale );
        ++g_timer_rearms;
        entry_live_write( "xnu_live_timerdrv_rearms", g_timer_rearms );
        entry_live_write( "xnu_live_timerdrv_rearm_rc", g_timer_rearm_rc );
    }
}

/*
 * 498: the frame's address, kept where the interrupt handler can find it.
 *
 * `start` returns and the handler may run long afterwards, on the boot thread's stack unwound and
 * inside the payload's exception path - so the mapping is saved in a file-scope variable exactly as
 * `MSM8974GIC.cpp` saves `g_gic_mapvaddr` for the same reason. A handler that re-derived the address
 * from the provider would be a handler doing memory mapping with interrupts masked.
 *
 * **It is a record because it is read where its value is not already known, and 497 is why that
 * sentence is here rather than "it is a record because it is a variable".** `start` writes it and the
 * handler reads it *from another call*, which is the one shape the compiler cannot forward across;
 * the handler also publishes it, so a mapping that silently failed is visible as a zero rather than as
 * a handler that did nothing.
 */
static uint32_t g_timer_frame_va;
static uint32_t g_timer_line_intid;

static uint32_t g_timer_isr_calls;
static uint32_t g_timer_isr_intid;
static uint32_t g_timer_isr_agree;
static uint32_t g_timer_isr_ctl;
static uint32_t g_timer_isr_stat;
static uint32_t g_timer_isr_ctl_after;
static uint32_t g_timer_isr_done;

/*
 * **500's two counters, and their storage is a different question from the three above them.** The
 * handler's readings of its device are written and read inside one call, so a local would do (and one
 * of them - the countdown's value - is published from a local for exactly that reason); these two
 * *accumulate*: call *n*'s value is call *n − 1*'s plus one, so the value at the moment of the publish
 * is not something this call computed, which is the shape 497's rule keeps storage for. They are also
 * the pair that says which of the handler's two paths the deliveries took - reload the deadline, or
 * mask the frame - and `tools/check_irq_routing.py`'s claim 8 holds this file to declaring only records
 * the linked image actually has, so a declaration here that the optimizer removed would be a red check
 * rather than a silent one. The handler's other two readings are republished zeroes in `start`, before
 * the line exists, so "the path was never taken" is a reading rather than a missing key.
 */
static uint32_t g_timer_isr_rearmed;
static uint32_t g_timer_isr_masked;

/*
 * **500's one new file-scope record, and it earns its storage by 497's rule.** `start` derives the
 * frame's deadline from the frequency the frame reports and stores the tick count here; the handler reads
 * it from *another call*, on another stack, with interrupts masked - which is the one place the value is
 * not already known and the one shape the compiler cannot forward across. It is written **before** the
 * deadline goes into the device, so a handler that fires between the two writes finds the count it needs
 * instead of a zero, and a zero in the handler means "the arming did not happen" rather than "the arming
 * happened and the handler could not see it".
 */
static uint32_t g_timer_arm_ticks;

/*
 * The handler registered for the frame's line with the payload's own registry, in the shape that
 * registry takes: `typedef void (*entry_irq_client_t)( void * refCon, uint32_t intid )`
 * (`entry_irq.c:184`), called from the dispatcher between the timer case and the spurious case.
 *
 * **It clears the device before it publishes anything, and that order is the step's safety
 * property.** The frame's line is level-sensitive: the output stays asserted until the device's
 * control word is written, so a handler that returned without masking the timer would be re-entered
 * immediately and forever. The write is `CTRL |= IT_MASK`, which is the *device's* own clear - the
 * frame drops its output the moment the mask bit is set - and it is the one thing this function must do
 * even if every key below it were to be dropped. The read-back (`_isr_ctl_after`) is there because a
 * write to a device with no clock behind it can vanish, and "the driver wrote the bit" and "the device
 * took the bit" are different findings.
 *
 * The interrupt id is published because it is *not* known in advance which of the frame's two lines
 * this machine asserts (`arch/boot/dts/msm8974.dtsi` gives the frame `interrupts = <0 8 0x4>, <0 7
 * 0x4>`, and the driver registered for the first): `_isr_intid` is the machine's answer to a question
 * the tree only describes. And it is held against the line the driver registered for
 * (`_isr_agree`), which is the reading that separates "the dispatcher called this handler for the
 * line it was registered under" from "some other line reached it".
 *
 * **`g_timer_line_intid` is read here and nowhere else, which is 497's rule and not an accident.**
 * The first cut of this step wrote the variable in `start` and never read it anywhere - it is the
 * registration's own input, and `entry_irq_register_client` was handed the local instead - and `nm`
 * on the linked image shows no `g_timer_line_intid` at all: a store nothing reads is a store the
 * compiler removes. The reader below is in a *different call* on a different stack, which is the one
 * place the value is not already known, and the seven file-scope records of this step are checked by
 * `tools/check_irq_routing.py`'s claim 8 for exactly this reason.
 */
static void
msm8974_timer_isr( void * refCon, uint32_t intid )
{
    uint32_t ctl = 0u;
    uint32_t tval = 0u;

    (void) refCon;

    ++g_timer_isr_calls;
    g_timer_isr_intid = intid;
    g_timer_isr_agree = ( g_timer_line_intid == intid ) ? 1u : 0u;

    if( g_timer_frame_va != 0u) {
        ctl = *(volatile uint32_t *)(uintptr_t)( g_timer_frame_va + MSM8974_FRAME_CTRL_OFF );
        tval = *(volatile uint32_t *)(uintptr_t)( g_timer_frame_va + MSM8974_FRAME_CNTP_TVAL_OFF );
        g_timer_isr_ctl = ctl;
        g_timer_isr_stat = ( ctl & MSM8974_FRAME_CTRL_IT_STAT ) ? 1u : 0u;
        /*
         * **The device is written on every path, and which write it is is the whole of the handler.**
         * The frame's line is level-sensitive, so the handler cannot simply return: 498 masked the
         * frame's output and left the condition latched, which is one delivery and no more. 500 has a
         * deadline to keep, so the first `FIRES - 1` calls *reload the deadline* - and that write is
         * itself the acknowledgement, because `CNTP_TVAL` is the down-counter: loading it clears the
         * expired condition, which drops `IT_STAT`, which drops the line. The last call masks instead,
         * which is the same end state 498 measured (`_isr_stat` 1 with `IT_MASK` set: the condition is
         * latched and the driver silenced it).
         *
         * **Both paths write the device before they publish to it, and why the order matters is the
         * step's safety property rather than a style.** The frame's line is level-sensitive, so a
         * handler that returned with the frame's output still asserted would be re-entered at once and
         * forever; the two branches are the two ways `CTRL` has of dropping the line - unmask and reload
         * the deadline, or mask and leave the condition latched - and neither of them returns before the
         * write lands. `_isr_ctl_after` is read back *from the device* rather than remembered, so a
         * write that vanished is a reading and not a silence.
         *
         * **And the run measured all three of the things this paragraph claims**, which is why the keys
         * beside it are worth their storage: call 1 of 3 read `_isr_ctl` 0x5 (ENABLE|IT_STAT - the
         * deadline had passed and the output was asserted), `_isr_tval` 0xffffff73 (141 ticks *past*
         * zero, so the delivery came ~7 µs after the deadline), wrote `CNTP_TVAL` and `CTRL`, and read
         * back `_isr_ctl_after` 0x1 - the reload cleared `IT_STAT`, which is the acknowledgement. Calls 1
         * and 2 re-armed (`_isr_rearmed` 1 then 2, `_isr_tval` 0xffffffae on call 2, another 10 ms and
         * another expiration); call 3 masked (`_isr_masked` 1, `_isr_ctl_after` 0x7 - latched and
         * silenced, exactly 498's end state), and there was no fourth call. `_isr_intid` 0x28 and
         * `_isr_agree` 1 on all three: the dispatcher called this function for the line it was registered
         * under.
         */
        if( g_timer_arm_ticks != 0u && g_timer_isr_calls < MSM8974_TIMER_FIRES) {
            *(volatile uint32_t *)(uintptr_t)( g_timer_frame_va + MSM8974_FRAME_CNTP_TVAL_OFF ) =
                g_timer_arm_ticks;
            *(volatile uint32_t *)(uintptr_t)( g_timer_frame_va + MSM8974_FRAME_CTRL_OFF ) =
                MSM8974_FRAME_CTRL_ENABLE;
            g_timer_isr_rearmed++;
        } else {
            *(volatile uint32_t *)(uintptr_t)( g_timer_frame_va + MSM8974_FRAME_CTRL_OFF ) =
                ctl | MSM8974_FRAME_CTRL_IT_MASK;
            g_timer_isr_masked++;
        }
        g_timer_isr_ctl_after =
            *(volatile uint32_t *)(uintptr_t)( g_timer_frame_va + MSM8974_FRAME_CTRL_OFF );
    }

    g_timer_isr_done = 1u;

    entry_live_write( "xnu_live_timerdrv_isr_calls", g_timer_isr_calls );
    entry_live_write( "xnu_live_timerdrv_isr_intid", g_timer_isr_intid );
    entry_live_write( "xnu_live_timerdrv_isr_agree", g_timer_isr_agree );
    entry_live_write( "xnu_live_timerdrv_isr_ctl", g_timer_isr_ctl );
    entry_live_write( "xnu_live_timerdrv_isr_stat", g_timer_isr_stat );
    /* The countdown's own value at the moment the device was written, and it is published from the
     * *local*: `tval` is read off the device in this call and used nowhere else, so 497's rule says a
     * local is what it is - a file-scope record would be storage the machine does not have. The two
     * counters below are the opposite case and are declared: they accumulate across calls, so their
     * value at this point is not something this call computed. */
    entry_live_write( "xnu_live_timerdrv_isr_tval", tval );
    entry_live_write( "xnu_live_timerdrv_isr_rearmed", g_timer_isr_rearmed );
    entry_live_write( "xnu_live_timerdrv_isr_masked", g_timer_isr_masked );
    entry_live_write( "xnu_live_timerdrv_isr_ctl_after", g_timer_isr_ctl_after );
    entry_live_write( "xnu_live_timerdrv_isr_done", g_timer_isr_done );
}

/*
 * 501: the coprocessor route to the counters the frame also exposes, as five accessors.
 *
 * Two readings of one value, and the device's kernel holds both. Its coprocessor side is
 * `counter_get_cntpct_cp15` (`arch/arm/kernel/arch_timer.c:310`), and its two `mrrc` encodings are
 * what the two accessors below are:
 *
 *     :331  counter_get_cntvct_cp15 is the second one
 *
 * and its frame side is `counter_get_cntpct_mem` (`:297`), the other half of the same pair:
 *
 *     :318  counter_get_cntvct_mem reads the frame's 0x008/0x00C
 *
 * The citations are one per line on purpose: `tools/check_timer_line.py` binds a citation to the name
 * written beside it *on the same line*, so a line that names two things and cites two lines is a
 * citation that check refuses - which is how the first version of this comment was written, and how
 * it was found. The virtual countdown's coprocessor pair is `c14, c3, 0` / `c14, c3, 1` - the pair
 * this image's own `entry_timebase.c` writes for XNU's decrementer, and the reason this step reads
 * CNTV at all: the frame's virtual countdown is *XNU's*, so the two routes to it are two readings of
 * a register the OS is already using.
 */
static inline uint64_t msm8974_cpu_cntpct(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("mrrc p15, 0, %0, %1, c14" : "=r"(lo), "=r"(hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static inline uint64_t msm8974_cpu_cntvct(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("mrrc p15, 1, %0, %1, c14" : "=r"(lo), "=r"(hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static inline uint32_t msm8974_cpu_cntv_tval(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c14, c3, 0" : "=r"(v));
    return v;
}

static inline uint32_t msm8974_cpu_cntv_ctl(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c14, c3, 1" : "=r"(v));
    return v;
}

static inline uint32_t msm8974_cpu_cntp_ctl(void)
{
    uint32_t v;
    __asm__ volatile ("mrc p15, 0, %0, c14, c2, 1" : "=r"(v));
    return v;
}

/* The distance between two readings of one counter, as a magnitude. A counter read twice through two
 * routes differs by however long the reads took, so the comparison is `|Δ| <= slack` with the slack
 * measured on this machine - never `==`, which would fail on a correct machine every time. */
static inline uint32_t msm8974_delta_abs(uint32_t a, uint32_t b)
{
    int32_t d = (int32_t)(a - b);
    return (uint32_t)(d < 0 ? -d : d);
}

/* 502: the frame's counter, read the way the device's own kernel reads it - the high word, the low
 * word, the high word again, and the low word kept only if the high word did not move. The kernel's
 * own reader of this frame's *virtual* counter is `counter_get_cntvct_mem` (`arch_timer.c:318`) and it
 * is these three reads in this order; the reasons 502 wants the same shape are its two, so it is the
 * same function for both counters rather than two spellings of one idea. */
static inline uint32_t msm8974_frame_lo( uintptr_t frame_va, uint32_t off_lo )
{
    uint32_t h0, h1, lo;

    do {
        h0 = *(volatile uint32_t *)(uintptr_t)( frame_va + off_lo + 0x4u );
        lo = *(volatile uint32_t *)(uintptr_t)( frame_va + off_lo );
        h1 = *(volatile uint32_t *)(uintptr_t)( frame_va + off_lo + 0x4u );
    } while( h0 != h1 );

    return lo;
}

class MSM8974Timer : public IOService
{
    OSDeclareDefaultStructors(MSM8974Timer);

public:
    virtual bool start( IOService * provider ) APPLE_KEXT_OVERRIDE;
};

/* `super` is a per-file macro in this tree rather than a keyword, so a file that wants
 * `super::start` has to say so - the same line the other two platform sources carry. */
#define super IOService

OSDefineMetaClassAndStructors(MSM8974Timer, IOService);

static uint32_t g_timer_starts;

bool
MSM8974Timer::start( IOService * provider )
{
    OSString * matched;
    OSNumber * freq;
    OSData   * reg;
    uint32_t   which = 0u;
    uint32_t   have  = 0u;
    uint32_t   freq_kind = 0u;
    uint32_t   freq_hz = 0u;
    uint32_t   reg0  = 0u;
    uint32_t   reg1  = 0u;
    uint32_t   regwords = 0u;
    uint32_t   cntfrq;
    OSArray  * devmem = 0;
    uint32_t   devcount = 0u;
    uint32_t   objkind = 0u;
    uint32_t   objlen = 0u;
    uint32_t   phys0 = 0u;
    uint32_t   len0  = 0u;
    uint32_t   resolve = 0u;
    /* 494 */
    IOMemoryDescriptor * range = 0;
    IOMemoryMap * themap = 0;
    uint32_t   mapvaddr = 0u;
    uint32_t   mapvlen  = 0u;
    uint32_t   rd0  = 0u;
    /* 495 */
    IOWorkLoop * workloop = 0;
    IOTimerEventSource * timer = 0;
    uint32_t   addrc = 0u;
    uint32_t   enabled = 0u;
    uint32_t   tormc = 0u;
    uint64_t   armed_at = 0u;
    uint64_t   due = 0u;
    /* 498 */
    OSData   * intr = 0;
    uint32_t   dt_cells = 0u;
    uint32_t   dt_type = 0u;
    uint32_t   dt_num = 0u;
    uint32_t   dt_trig = 0u;
    uint32_t   line = 0u;
    uint32_t   line_rule = 0u;
    uint32_t   line_parent = 0u;
    bool       osmap = false;
    OSArray  * osmap_specs = 0;
    OSArray  * osmap_ctrlrs = 0;
    uint32_t   osmap_n = 0u;
    uint32_t   osmap_w0 = 0u;
    uint32_t   osmap_w1 = 0u;
    uint32_t   osmap_w2 = 0u;
    uint32_t   osmap_same = 0u;
    uint32_t   osmap_ph = 0u;
    uint32_t   osmap_ph_agree = 0u;
    IOMemoryDescriptor * framerange = 0;
    IOMemoryMap * framemap = 0;
    uint32_t   frame_objkind = 0u;
    uint32_t   frame_phys = 0u;
    uint32_t   frame_len = 0u;
    uint32_t   frame_va = 0u;
    uint32_t   frame_freq = 0u;
    uint32_t   cnt_lo_a = 0u;
    uint32_t   cnt_hi_a = 0u;
    uint32_t   cnt_lo_b = 0u;
    uint32_t   cnt_hi_b = 0u;
    uint64_t   frame_cnt_a = 0u;
    uint64_t   frame_cnt_b = 0u;
    uint64_t   frame_mac_a = 0u;
    uint64_t   frame_mac_b = 0u;
    uint32_t   frame_cnt_d = 0u;
    uint32_t   frame_mac_d = 0u;
    uint32_t   frame_slack = 0u;
    uint32_t   frame_rate_ok = 0u;
    uint32_t   frame_ctl = 0u;
    uint32_t   frame_ctl_en = 0u;
    uint32_t   frame_ctl_mask = 0u;
    uint32_t   frame_ctl_stat = 0u;
    uint32_t   line_cli_rc = 0u;
    /* 500 */
    uint32_t   arm_ticks = 0u;
    uint32_t   arm_ctl_before = 0u;
    uint32_t   arm_ctl = 0u;
    uint32_t   arm_tval_after = 0u;
    uint32_t   arm_rc = 0u;
    uint32_t   arm_line_rc = 0u;
    /* 501 - the frame's second view, and the frame's two routes to the same counters */
    IOMemoryDescriptor * v2range = 0;
    IOMemoryMap * v2map = 0;
    uint32_t   v2_objkind = 0u;
    uint32_t   v2_phys = 0u;
    uint32_t   v2_len = 0u;
    uint32_t   v2_va = 0u;
    uint32_t   v2_freq = 0u;
    uint32_t   v2_freq_agree = 0u;
    uint32_t   v2_slack = 0u;
    uint32_t   v2_cntv_lo = 0u;
    uint32_t   v2_cntv_hi = 0u;
    uint32_t   v2_cntv_d = 0u;
    uint32_t   v2_cntv_ok = 0u;
    uint32_t   v2_cntp_lo = 0u;
    uint32_t   v2_cntp_hi = 0u;
    uint32_t   v2_cntp_d = 0u;
    uint32_t   v2_cntp_ok = 0u;
    uint32_t   v2_tval = 0u;
    uint32_t   fr_cntv_lo = 0u;
    uint32_t   fr_cntv_hi = 0u;
    uint32_t   fr_cntp_lo = 0u;
    uint32_t   fr_cntp_hi = 0u;
    uint32_t   rt_slack = 0u;
    uint32_t   rt_cntv_lo_fr = 0u;
    uint32_t   rt_cntv_hi_fr = 0u;
    uint32_t   rt_cntv_lo_cpu = 0u;
    uint32_t   rt_cntv_hi_cpu = 0u;
    uint32_t   rt_cntv_d = 0u;
    uint32_t   rt_cntv_ok = 0u;
    uint32_t   rt_cntp_lo_fr = 0u;
    uint32_t   rt_cntp_hi_fr = 0u;
    uint32_t   rt_cntp_lo_cpu = 0u;
    uint32_t   rt_cntp_hi_cpu = 0u;
    uint32_t   rt_cntp_d = 0u;
    uint32_t   rt_cntp_ok = 0u;
    uint32_t   rt_tval_fr = 0u;
    uint32_t   rt_tval_cpu = 0u;
    uint32_t   rt_tval_d = 0u;
    uint32_t   rt_tval_ok = 0u;
    uint32_t   rt_ctl_fr = 0u;
    uint32_t   rt_ctl_cpu = 0u;
    uint32_t   rt_ctl_agree = 0u;
    uint32_t   rt_cntv_ctl_cpu = 0u;
    /* 502 - the frame's compare values, and whether the frame's counter is the core's */
    uint32_t   cv_slack = 0u;
    uint32_t   cv_p_tval1 = 0u;
    uint32_t   cv_p_ct1_lo = 0u;
    uint32_t   cv_p_tval2 = 0u;
    uint32_t   cv_p_ct2_lo = 0u;
    uint32_t   cv_p_sum1 = 0u;
    uint32_t   cv_p_sum2 = 0u;
    uint32_t   cv_p_inv_d = 0u;
    uint32_t   cv_p_inv_ok = 0u;
    uint32_t   cv_p_cval_lo = 0u;
    uint32_t   cv_p_arm_ct_lo = 0u;
    uint32_t   cv_p_lag = 0u;
    uint32_t   cv_p_gt_now = 0u;
    uint32_t   cv_v_tval1 = 0u;
    uint32_t   cv_v_tval2 = 0u;
    uint32_t   cv_v_ct1_lo = 0u;
    uint32_t   cv_v_sum1 = 0u;
    uint32_t   cv_v_sum2 = 0u;
    uint32_t   cv_v_inv_ok = 0u;
    uint32_t   cv_v_zero_d = 0u;
    uint32_t   cv_v_zero_ok = 0u;
    uint32_t   cv_v_ctl_fr = 0u;
    uint32_t   cv_v_ctl_cpu = 0u;
    uint32_t   cv_v_ctl_agree = 0u;
    uint32_t   cv_v_ctl_mask = 0u;
    uint32_t   cv_n = 0u;
    uint32_t   cv_inside = 0u;
    uint32_t   cv_wmax = 0u;
    uint32_t   cv_wmin = 0u;
    uint32_t   cv_d0 = 0u;
    uint32_t   cv_hi_agree = 0u;

    /* 502b - the same pair read both ways round, the physical pair as the control, the frame's own
     * windows, and the two counters' rates against `mach_absolute_time`. */
    uint32_t   cv_xv_x8 = 0u;
    uint32_t   cv_xv_y8 = 0u;
    uint32_t   cv_xv_x0 = 0u;
    uint32_t   cv_xv_y0 = 0u;
    uint32_t   cv_xv_n = 0u;
    uint32_t   cv_xv_m_in = 0u;
    uint32_t   cv_xv_sum = 0u;
    uint32_t   cv_xv_dif = 0u;
    uint32_t   cv_xp_x8 = 0u;
    uint32_t   cv_xp_y8 = 0u;
    uint32_t   cv_xp_x0 = 0u;
    uint32_t   cv_xp_y0 = 0u;
    uint32_t   cv_xp_n = 0u;
    uint32_t   cv_xp_m_in = 0u;
    uint32_t   cv_xp_sum = 0u;
    uint32_t   cv_xp_dif = 0u;
    uint32_t   cv_win_x8 = 0u;
    uint32_t   cv_win_y8 = 0u;
    uint32_t   cv_win_x0 = 0u;
    uint32_t   cv_win_y0 = 0u;
    uint32_t   cv_win_n = 0u;
    uint32_t   cv_win_wmax = 0u;
    uint32_t   cv_win_sum = 0u;
    uint32_t   cv_win_dif = 0u;
    uint32_t   cv_r_fd = 0u;
    uint32_t   cv_r_cd = 0u;
    uint32_t   cv_r_ppm = 0u;
    uint32_t   cv_r_ok = 0u;
    uint32_t   cv_r_n = 0u;

    if( !super::start( provider )) return( false );
    if( provider == 0) return( false );

    g_timer_starts++;

    /* Which of the personality's two names the candidate test matched. The key is the kernel's own
     * (`IOService::matchInternal` leaves the matched name in the driver's property table as
     * `IONameMatched`, `IOService.cpp:5443-5448`), so this is a reading of the match and not a
     * restatement of the personality. */
    matched = OSDynamicCast( OSString, getProperty( "IONameMatched" ));
    if( matched != 0) {
        if( matched->isEqualTo( "timer" ))               which = 1u;
        else if( matched->isEqualTo( "qcom,msm-timer" )) which = 2u;
        else                                             which = 3u;
    }

    entry_live_write( "xnu_live_timerdrv_seq", g_timer_starts );
    entry_live_write( "xnu_live_timerdrv_prov", (uint32_t)(uintptr_t) provider );
    entry_live_write( "xnu_live_timerdrv_match", which );

    /* The device's own description, read from the provider and not from a header. `reg` is an
     * `OSData` of the node's u32 words, read one byte at a time: an unaligned 32-bit load off an
     * `OSData` buffer is a fault this driver does not need to risk to read six words.
     *
     * **And `frequency` is an `OSData` too, which is what this step's first run measured the hard
     * way.** The reader below was an `OSDynamicCast( OSNumber, ... )` and the run came back with
     * `_have = 2` and `_freq = 0`: `reg` was found and the tree's `frequency` - which the payload
     * does write, `stage90_main.c`'s `/timer` node - was not. The reason is in the plane rather than
     * in the tree: every property of a device-tree node becomes an **`OSData`** when the node is
     * turned into a registry entry - `data = OSData::withBytes( prop, propSize )`
     * (`IODeviceTreeSupport.cpp:379`), for `name` and `compatible` as much as for `frequency`. So
     * `have` and `freq` are read with the kind the plane produces, and the kind is published beside
     * them (`_freqkind`) rather than being assumed: a driver that reports "the tree has no
     * frequency" when its own cast was the wrong type is the artifact this project keeps meeting.
     * Both kinds are accepted because both are legal for a property the kernel itself can set, and
     * the record says which one this machine's node produced. */
    freq = OSDynamicCast( OSNumber, provider->getProperty( "frequency" ));
    if( freq != 0) {
        have |= 1u;
        freq_kind = 1u;
        freq_hz = freq->unsigned32BitValue();
    }
    else {
        OSData * freqdata = OSDynamicCast( OSData, provider->getProperty( "frequency" ));
        if( freqdata != 0) {
            have |= 1u;
            if( freqdata->getLength() == 4u) {
                const unsigned char * fb = (const unsigned char *) freqdata->getBytesNoCopy();
                if( fb != 0) {
                    freq_kind = 2u;
                    freq_hz = (uint32_t)fb[0] | ((uint32_t)fb[1] << 8) | ((uint32_t)fb[2] << 16) |
                              ((uint32_t)fb[3] << 24);
                } else {
                    freq_kind = 5u;
                }
            } else {
                freq_kind = 3u;
            }
        }
    }
    reg = OSDynamicCast( OSData, provider->getProperty( "reg" ));
    if( reg != 0 && reg->getLength() >= 8u) {
        const unsigned char * b = (const unsigned char *) reg->getBytesNoCopy();
        if( b != 0) {
            have |= 2u;
            reg0 = (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
            /* The node's own size for its first window, and the number the OS's `len0` is compared
             * with: the size is a `reg` fact like the address, so it is read here rather than written
             * as a constant in the comparison (492's lesson, in the driver). */
            reg1 = (uint32_t)b[4] | ((uint32_t)b[5]<<8) | ((uint32_t)b[6]<<16) | ((uint32_t)b[7]<<24);
        }
        regwords = reg->getLength() / 4u;
    }
    entry_live_write( "xnu_live_timerdrv_have", have );
    entry_live_write( "xnu_live_timerdrv_freqkind", freq_kind );
    entry_live_write( "xnu_live_timerdrv_freq", freq_hz );
    entry_live_write( "xnu_live_timerdrv_reg0", reg0 );
    /* The node's own size for its first window, published because it is the right-hand side of the
     * `_resolve` comparison (`len0 == reg1`): with `_reg0`, `_reg1`, `_regwords`, `_phys0`, `_len0`
     * and `_devcount` all in the record, a `_resolve` of 2 says which of the three conjuncts failed
     * instead of only that one did (493, and the reason 492's fifteen keys became sixteen). */
    entry_live_write( "xnu_live_timerdrv_reg1", reg1 );
    entry_live_write( "xnu_live_timerdrv_regwords", regwords );

    /* 493: the OS's own answer to the same question, and the comparison. `getDeviceMemory()` is the
     * array `IODTResolveAddressing` filed under `IODeviceMemoryKey` when this nub's `getResources`
     * ran, i.e. before this `start` - so a zero here means the OS resolved nothing, which is a
     * different finding from a resolution that is wrong. Nothing is dereferenced: the entries are
     * memory descriptors of physical ranges, and `getPhysicalSegment` reads the range out of the
     * object rather than reading the device.
     *
     * **The entry is read as an `IOMemoryDescriptor`, not as an `IODeviceMemory`, and that is 493's
     * second run's finding rather than a convenience.** The first run read it as an `IODeviceMemory` -
     * the type the array is documented to hold (`IODeviceMemory.h:40-45`) - and came back with
     * `_devcount = 3` (the node's own number of `{address, size}` pairs, so the OS *had* resolved this
     * node) beside `_phys0 = 0`, `_len0 = 0` and `_resolve = 2`. The cause is a blind cast in Apple's
     * file:
     *
     *     IODeviceMemory * IODeviceMemory::withRange( start, length )
     *     { return( (IODeviceMemory *) IOMemoryDescriptor::withAddressRange( start, length, ... )); }
     *     // iokit/Kernel/IODeviceMemory.cpp:34-39
     *
     * and `withAddressRange` builds an `IOGeneralMemoryDescriptor` (`IOMemoryDescriptor.cpp:1162-1181`),
     * which is a **sibling** of `IODeviceMemory` - both derive from `IOMemoryDescriptor`
     * (`IOMemoryDescriptor.h:986`, `IODeviceMemory.h:45`) - so `OSDynamicCast( IODeviceMemory, ... )`
     * answers 0 for every entry the OS files, the read below is skipped, and the record said the OS
     * resolved nothing. `_objkind` is the reading that names this: 1 = the entry really is an
     * `IODeviceMemory`, 2 = it is an `IOMemoryDescriptor` and not one (Apple's blind cast, which is
     * what this machine produces), 0 = neither. `_objlen` is that descriptor's own `getLength()`,
     * which does not go through the segment walk, so "the object is well formed" and "the segment read
     * returned nothing" are two numbers instead of one zero.
     *
     * `_resolve` is the comparison and it distinguishes four outcomes: 0 = the OS answered with
     * nothing, 2 = it answered and either the first entry is not this node's first `reg` pair or its
     * count is not the number of pairs the node carries (two ways to be wrong, and the count is
     * published beside it so the reader can say which), 3 = it answered with an empty array, and 1 =
     * they agree. A single boolean would fold "the OS resolved nothing" into "the OS resolved the
     * wrong thing", which is the distinction 492's run turned on. */
    devmem = provider->getDeviceMemory();
    devcount = provider->getDeviceMemoryCount();
    if( devmem != 0 && devcount != 0u) {
        OSObject * entry = devmem->getObject( 0 );
        range = OSDynamicCast( IOMemoryDescriptor, entry );
        if( OSDynamicCast( IODeviceMemory, entry ) != 0)       objkind = 1u;
        else if( range != 0)                                   objkind = 2u;
        if( range != 0) {
            IOByteCount len = 0u;
            IOPhysicalAddress phys;
            objlen = (uint32_t) range->getLength();
            /* `kIOMemoryMapperNone` because the entry describes a *physical* range (`withRange`
             * creates it with `kIODirectionNone | kIOMemoryMapperNone`) and the number wanted here is
             * the device's address, not a mapped one. */
            phys = range->getPhysicalSegment( 0u, &len, kIOMemoryMapperNone );
            phys0 = (uint32_t) phys;
            len0  = (uint32_t) len;
        }
    }
    if( devmem == 0 || devcount == 0u)             resolve = (devmem == 0) ? 0u : 3u;
    else if( phys0 == reg0 && len0 == reg1 && devcount == (regwords / 2u) ) resolve = 1u;
    else                                            resolve = 2u;
    entry_live_write( "xnu_live_timerdrv_devmem", (uint32_t)(uintptr_t) devmem );
    entry_live_write( "xnu_live_timerdrv_devcount", devcount );
    entry_live_write( "xnu_live_timerdrv_objkind", objkind );
    entry_live_write( "xnu_live_timerdrv_objlen", objlen );
    entry_live_write( "xnu_live_timerdrv_phys0", phys0 );
    entry_live_write( "xnu_live_timerdrv_len0", len0 );
    entry_live_write( "xnu_live_timerdrv_resolve", resolve );

    /* The machine's own answer to the tree's `frequency`, read live from the CPU rather than from the
     * cached value the payload's `timebase.c` keeps. Reading the register here is deliberate: a
     * comparison against a value another program cached would be a comparison of two tree-side
     * numbers, and the question is whether the tree agrees with the hardware. */
    __asm__ volatile ( "mrc p15, 0, %0, c14, c0, 0" : "=r" (cntfrq) );
    entry_live_write( "xnu_live_timerdrv_cntfrq", cntfrq );
    entry_live_write( "xnu_live_timerdrv_agree", (freq_hz == cntfrq) ? 1u : 0u );

    /* ---- 494: the mapping, and the device read through it ------------------------------------
     *
     * The same two calls as `MSM8974GIC.cpp`, on the same object 493's resolution filed, and one word
     * read through the address the OS's mapping machinery returns. `IOMemoryDescriptor::map(
     * kIOMapAnywhere )` is the kext route to a device's registers, and this driver takes it for the
     * reason Apple's own files give: the entry is `kIOMemoryTypePhysical64` (`withRange` passes a NULL
     * task, `IOMemoryDescriptor.cpp:1176-1179`), so `doMap` reaches its `_task == NULL` arm -
     * `device_pager_setup` + `mach_memory_object_memory_entry_64` (`:641-678`) - and the page behind
     * the returned address is the physical range `getPhysicalSegment` named, mapped with the cache
     * mode a device address gets. `MSM8974GIC.cpp`'s block carries the derivation with its citations;
     * the two files are the same code on purpose, so that a defect in the reading is a defect in both.
     *
     * **What this node does not have is a second definition of what it reads.** `GICD_TYPER` is
     * read-only, so the GIC driver can hold its own read against the payload's own read of the same
     * register, and `/timer`'s node has no such counterpart: nothing in this project has ever read a
     * word out of the GPT at `0xf9020000` - the tree declares it, `PE_state_stage90.timerBase` names
     * it, and the payload's handlers use the *architectural* counter, not this block. So `_rd0` is a
     * **first reading with no second definition**, and the step says so rather than dressing it up:
     * it is published raw, the mapping is published beside it (`_map`, `_mapvaddr`, `_mapvlen`), and
     * what it means is owed to the step that finds a second way to read the same block. `_rd0` at
     * offset 0 is the node's own `reg[0]` word - the block's first register, whichever register that
     * is - and it is read once, because this step's question is whether a read lands at all.
     *
     * Nothing here writes a device register. */
    if( range != 0) {
        themap = range->map( kIOMapAnywhere );
        if( themap != 0) {
            mapvaddr = (uint32_t)(uintptr_t) themap->getVirtualAddress();
            mapvlen  = (uint32_t) themap->getLength();
            if( mapvaddr != 0u)
                rd0 = *(volatile uint32_t *)(uintptr_t)( mapvaddr + MSM8974_TIMER_REG0_OFF );
        }
    }
    entry_live_write( "xnu_live_timerdrv_map", (uint32_t)(uintptr_t) themap );
    entry_live_write( "xnu_live_timerdrv_mapvaddr", mapvaddr );
    entry_live_write( "xnu_live_timerdrv_mapvlen", mapvlen );
    entry_live_write( "xnu_live_timerdrv_rd0", rd0 );

    /* ---- 495: the OS's own timeout, asked for by this driver ---------------------------------
     *
     * 494 gave this driver a *mapping* and one read through it; the driver was still something the
     * boot did not need. This is the step where the OS calls the driver back: the driver builds a work
     * loop, puts a timer event source on it, and asks the OS for a timeout. Everything after that is
     * the kernel's own machinery, and it is machinery that has never run on this machine - the
     * thread-call subsystem (`thread_call_initialize`, `osfmk/kern/startup.c:446`), the timer queue
     * expired from the payload's interrupt handler (`entry_irq.c` calls `rtclock_intr(0)` for the
     * line, `rtclock.c:273` -> `timer_intr` -> `timer_queue_expire`), and the daemon thread that
     * `thread_call_initialize` started. Nothing here is this image's own code: the callback is called
     * *by* the kernel, from `IOTimerEventSource::timeoutAndRelease`, on the thread-call thread, with
     * the work loop's gate held.
     *
     * The sequence is the one IOKit requires and each return value is published, because a chain of
     * five calls that can each fail silently is a reading that cannot be told from a driver that never
     * armed anything:
     *
     *     workLoop()                  -> `_wl`   (0 = the OS could not create one, which includes the
     *                                             `kernel_thread_start` inside `init()` failing)
     *     timerEventSource(this, fn)  -> `_ts`   (0 = the event source could not be allocated; its
     *                                             `setTimeoutFunc` also panics if built twice, so a
     *                                             non-NULL `_ts` is a statement about `init()`)
     *     addEventSource(timer)       -> `_add`  (0 = kIOReturnSuccess; a non-zero here means the work
     *                                             loop refused the source, and `setTimeout` below
     *                                             would then do nothing at all - `wakeAtTime` requires
     *                                             `workLoop` non-NULL, `IOTimerEventSource.cpp:476`)
     *     enable() then isEnabled()   -> `_en`   (1 = the source is enabled; `setTimeout` on a disabled
     *                                             source stores the time and arms nothing)
     *     setTimeout(100 ms)          -> `_to`   (0 = kIOReturnSuccess)
     *
     * **`_fires` and the four keys the callback owns are published as zeroes *before* the arming**, so
     * the two findings this step has to keep apart are two different records rather than one absent
     * one: `_fires = 0` beside a non-zero `_ts` and a zero `_to` is "the OS accepted a deadline and
     * never called back", while `_wl = 0` or `_add != 0` is "the driver never armed anything". A key
     * that only exists after the callback runs would make those the same silence.
     *
     * **`_due` is the OS's own conversion of the interval this driver asked for** -
     * `clock_interval_to_deadline( MSM8974_TIMER_ASK_MS, kMillisecondScale, &due )`, the same function
     * `setTimeout` calls internally - so the moment the callback should run is in the record beside the
     * moment it did (`_arm_at`, `_fire_*`). The interval itself is one `#define` shared by both
     * `setTimeout` calls and by this computation, so "the number the driver asked for" and "the number
     * the driver compared against" cannot drift apart, and the callback publishes the OS's own
     * `absolutetime_to_nanoseconds` of the elapsed difference (`_fire_lat_ns`, `_gap_ns`) rather than a
     * number this file computed from a frequency.
     *
     * The driver is started once (`_seq` = 1 in every run of 492, 493 and 494), so this arms one timer;
     * a second `start` would arm a second rather than silently reusing the first, which is the right
     * failure for a kext that was never matched twice. Nothing here touches a device register, and the
     * callback writes nothing but keys: the timeout's whole purpose is to be called.
     */
    entry_live_write( "xnu_live_timerdrv_fires", 0u );
    entry_live_write( "xnu_live_timerdrv_rearms", 0u );
    entry_live_write( "xnu_live_timerdrv_rearm_rc", 0u );
    entry_live_write( "xnu_live_timerdrv_fire_lat_ns", 0u );
    entry_live_write( "xnu_live_timerdrv_gap_ns", 0u );

    workloop = IOWorkLoop::workLoop();
    if( workloop != 0) {
        timer = IOTimerEventSource::timerEventSource( this, msm8974_timer_timeout );
        if( timer != 0) {
            addrc = (uint32_t) workloop->addEventSource( timer );
            timer->enable();
            enabled = timer->isEnabled() ? 1u : 0u;
            armed_at = mach_absolute_time();
            g_timer_armed = armed_at;
            clock_interval_to_deadline( MSM8974_TIMER_ASK_MS, kMillisecondScale, &due );
            tormc = (uint32_t) timer->setTimeout( MSM8974_TIMER_ASK_MS, kMillisecondScale );
        }
    }
    entry_live_write( "xnu_live_timerdrv_ms", MSM8974_TIMER_ASK_MS );
    entry_live_write( "xnu_live_timerdrv_wl", (uint32_t)(uintptr_t) workloop );
    entry_live_write( "xnu_live_timerdrv_ts", (uint32_t)(uintptr_t) timer );
    entry_live_write( "xnu_live_timerdrv_add", addrc );
    entry_live_write( "xnu_live_timerdrv_en", enabled );
    entry_live_write( "xnu_live_timerdrv_to", tormc );
    entry_live_write( "xnu_live_timerdrv_arm_lo", (uint32_t) armed_at );
    entry_live_write( "xnu_live_timerdrv_arm_hi", (uint32_t)( armed_at >> 32 ));
    entry_live_write( "xnu_live_timerdrv_due_lo", (uint32_t) due );
    entry_live_write( "xnu_live_timerdrv_due_hi", (uint32_t)( due >> 32 ));

    /* ---- 498: the frame, and the line the tree names for it -----------------------------------
     *
     * 492-497 answered "which line does a driver own" with a line the driver raises itself: SGI 0,
     * pended by a write to `GICD_SGIR` in `MSM8974GIC::start`. This step is the same question asked of
     * a **device**, and the device is the one this driver already matched: the timer's frame.
     *
     * Where the frame is, and why 494 read a zero
     * -------------------------------------------
     * 494 mapped this node's `reg[0]` - `0xf9020000` - and read one word out of it: `_rd0 = 0`. The
     * node's `reg` has three `{address, size}` entries (`_regwords = 6`), and the block that reads
     * zero is the *first* of them. The device's own tree says what the other two are:
     *
     *     arch/arm/boot/dts/msm8974.dtsi:153-167
     *       timer@f9020000 {                      <- the node this driver matched; reg 0xf9020000
     *           compatible = "arm,armv7-timer-mem";
     *           reg = <0xf9020000 0x1000>;
     *           clock-frequency = <19200000>;
     *           frame@f9021000 {
     *               frame-number = <0>;
     *               interrupts = <0 8 0x4>, <0 7 0x4>;
     *               reg = <0xf9021000 0x1000>, <0xf9022000 0x1000>;
     *           };
     *       };
     *
     * and the device's own kernel reads both of a frame's registers and its line out of the *frame*:
     * `timer_base = of_iomap(frame, 0)` (`arch/arm/kernel/arch_timer.c:623`) and `arch_timer_spi =
     * irq_of_parse_and_map(frame, 0)` (`:629`). So `0xf9020000` is a container with no registers of
     * its own - which is what a stored zero looks like when it is read - and the timer is one page up,
     * at `0xf9021000`, where the tree puts the `interrupts`. **That is 494's owed second definition,
     * and it is structural rather than numerical**: the property that says this block is a device
     * (`interrupts`) is on the frame, one `reg` entry away from the block that read zero.
     *
     * The line, and the rule that turns the tree's cells into it
     * --------------------------------------------------------
     * The tree's frame declares `interrupts = <0 8 0x4>, <0 7 0x4>`, and the binding - copied into
     * this repository by the same kernel - says of a frame: "interrupts : Interrupt list for physical
     * and virtual timers in that order" (`Documentation/devicetree/bindings/arm/arch_timer.txt`). So
     * the first specifier is the **physical** timer's line and the second is the virtual timer's, and
     * the device's kernel takes `index 0` for the timer it uses. `MSM8974Timer` takes the same one.
     *
     * The three cells are `<type number trigger>` because the tree's controller declares
     * `#interrupt-cells = <3>` (`stage90_main.c`'s `/interrupt-controller`), and the type is what the
     * number means: `0` is a shared peripheral interrupt, whose GIC interrupt id is `32 + number`, and
     * `1` is a private one, whose id *is* the number. So `<0 8 0x4>` is **intid 40**, level-high. The
     * rule is published as `_line_rule` beside the number it produced, because "8" and "40" are both
     * defensible-looking readings of that property and only one of them is this machine's.
     *
     * Two readings of one property, and the one that is Apple's
     * -------------------------------------------------------
     * The driver reads the property itself, and then asks the kernel's own device-tree code to read
     * it: `IODTMapInterrupts` (`IODeviceTreeSupport.cpp:790`) walks `interrupts` through
     * `IODTFindInterruptParent` (`:467`) - which resolves the `interrupt-parent` phandle this step
     * adds to the tree - files the specifier under `IOInterruptSpecifiers` and the *controller's name*
     * under `IOInterruptControllers`, where the name is `IOInterruptController%08X` built from the
     * **controller node's phandle** (`IODTInterruptControllerName`, `:489-501`). That last part is a
     * property of the tree the OS itself reads back to us, so `_osmap_ph` is the OS's answer to "which
     * node answers this line's interrupts", and `_osmap_ph_agree` is that answer held against the
     * phandle `interrupt-parent` names. A tree whose two halves disagree about which controller owns
     * the timer is a tree where every other reading below is a reading of the wrong contract.
     *
     * **The phandle's *key* is `AAPL,phandle`, and the first 498 run is where that was learned.** The
     * tree this step first shipped spelled the property `phandle` - Linux's spelling, and the one the
     * device's own `msm8974.dtsi` uses - while the plane reads it under
     * `gIODTPHandleKey = OSSymbol::withCStringNoCopy("AAPL,phandle")` (`:137-138`). `AddPHandle`
     * (`:422-431`) therefore registered nothing, `FindPHandle` (`:434-447`) answered 0, and the OS's
     * own publication walk (`:221-225` - it maps *every* node that has an `interrupts` property,
     * which is what made this path reachable for the first time) called `IODTGetICellCounts(0, ...)`
     * and aborted on a vtable load from address 0 at `IODTGetICellCounts+8`. The step's own comment
     * asserted the pair `#interrupt-cells` + `phandle`; the pair is right and the *spelling* was
     * wrong, which is why `tools/check_timer_line.py` now derives the key from Apple's source rather
     * than reading it out of ours.
     *
     * **And the OS's route stops here, which is why the line is registered with the payload's registry
     * instead - and the reason is a hang, not a preference.** `IOService::registerInterrupt`
     * (`IOService.cpp:6337`) is `lookupInterrupt` then the controller's `registerInterrupt`, and
     * `lookupInterrupt`'s second step is `getPlatform()->lookUpInterruptController(name)`
     * (`IOService.cpp:6290`). That function is
     *
     *     IOPlatformExpert::lookUpInterruptController(OSSymbol * name)          // :379-397
     *     {   IOLockLock(gIOInterruptControllersLock);
     *         while (1) {
     *             object = gIOInterruptControllers->getObject(name);
     *             if (object != 0) break;
     *             IOLockSleep(gIOInterruptControllersLock, gIOInterruptControllers, THREAD_UNINT);
     *         }   ... }
     *
     * - a sleep that only `registerInterruptController` (`:354-363`) can wake, and the only caller of
     * *that* in the whole tree is `IOCPU.cpp:783`, inside the CPU interrupt controller's own
     * initialiser. This image instantiates no CPU driver and registers no controller (405 measured the
     * consequence of the first), so `gIOInterruptControllers` is empty and **every call to
     * `IOService::registerInterrupt` on this machine sleeps forever on the calling thread** - before
     * `IOCPUInterruptController::registerInterrupt` and its `thread_block` are ever reached. 496 read
     * the *CPU controller's* block out of the tree and called the route unusable; this step's finding
     * is that the block is one layer earlier and unconditional, so no driver may call it at all.
     * `tools/check_timer_line.py` refuses a build in which any of this image's sources calls it.
     *
     * The result is the registration this driver does make: `entry_irq_register_client`, the payload's
     * own registry, which is 496's mechanism and the only interrupt-registration mechanism this
     * machine has.
     *
     * What it reads, and what it does not write
     * -----------------------------------------
     * The frame is mapped through the same route as 494's `reg[0]` - the OS's own resolution, then
     * `map( kIOMapAnywhere )` - and three things are read out of it: the frame's own frequency
     * register, its 64-bit count twice with `IODelay( 1000 )` between, and its control word. Nothing
     * is written: the line is not enabled at the distributor and the timer is not armed, so this step
     * cannot raise an interrupt the machine has to survive, and the device is silent throughout.
     * Arming it is the next step's business and it will need the distributor's enable, its target and
     * its priority - three registers whose addresses are all derived from the intid.
     *
     * The frequency is a **third definition** of 19.2 MHz and it is read as a register: the tree says
     * 19200000 (`_freq`), the CPU says it in `CNTFRQ` (`_cntfrq`), and the frame says it in its own
     * `FREQ` register (`_frame_freq`). 492 compared the first two and said in its record that the
     * third did not exist; it exists and this is it.
     *
     * The rate is *measured* rather than declared: the frame's count delta across a one-millisecond
     * busy wait is held against `mach_absolute_time()`'s delta over the same window, and the two are
     * the same counter class at the same 19.2 MHz. The tolerance is derived from a measured register
     * read (`_frame_slack` = four of them, the number of frame reads inside the window) rather than
     * chosen: the frame's window is *inside* the kernel clock's window by construction, so a correct
     * rate gives `0 <= mac_d - cnt_d <= slack`, and a frame running at any other rate misses by the
     * ratio - a 32 kHz frame would answer `cnt_d` ~= 32 against a `mac_d` of ~19200.
     */
    frame_va = 0u;

    intr = OSDynamicCast( OSData, provider->getProperty( "interrupts" ));
    if( intr != 0) {
        const unsigned char * ib = (const unsigned char *) intr->getBytesNoCopy();
        dt_cells = intr->getLength() / 4u;
        if( ib != 0 && dt_cells >= 3u) {
            dt_type = (uint32_t)ib[0] | ((uint32_t)ib[1]<<8) | ((uint32_t)ib[2]<<16) | ((uint32_t)ib[3]<<24);
            dt_num  = (uint32_t)ib[4] | ((uint32_t)ib[5]<<8) | ((uint32_t)ib[6]<<16) | ((uint32_t)ib[7]<<24);
            dt_trig = (uint32_t)ib[8] | ((uint32_t)ib[9]<<8) | ((uint32_t)ib[10]<<16) | ((uint32_t)ib[11]<<24);
            if( dt_type == MSM8974_INTR_TYPE_SPI ) {
                line = dt_num + MSM8974_GIC_SPI_BASE;
                line_rule = 1u;
            } else if( dt_type == MSM8974_INTR_TYPE_PPI ) {
                line = dt_num;
                line_rule = 2u;
            }
        }
    }
    /* The phandle `interrupt-parent` names, read the way the plane produces it: an `OSData` of one
     * word. It is the left-hand side of `_osmap_ph_agree` and the reason the two numbers are read
     * separately rather than assumed equal (a phandle is written twice by construction). */
    {
        OSData * ip = OSDynamicCast( OSData, provider->getProperty( "interrupt-parent" ));
        if( ip != 0 && ip->getLength() >= 4u) {
            const unsigned char * pb = (const unsigned char *) ip->getBytesNoCopy();
            if( pb != 0)
                line_parent = (uint32_t)pb[0] | ((uint32_t)pb[1]<<8) | ((uint32_t)pb[2]<<16) |
                              ((uint32_t)pb[3]<<24);
        }
    }

    osmap = IODTMapInterrupts( provider );
    osmap_specs  = OSDynamicCast( OSArray, provider->getProperty( gIOInterruptSpecifiersKey ));
    osmap_ctrlrs = OSDynamicCast( OSArray, provider->getProperty( gIOInterruptControllersKey ));
    if( osmap_specs != 0) {
        osmap_n = osmap_specs->getCount();
        OSData * s0 = OSDynamicCast( OSData, osmap_specs->getObject( 0 ));
        if( s0 != 0 && s0->getLength() >= 12u) {
            const unsigned char * sb = (const unsigned char *) s0->getBytesNoCopy();
            if( sb != 0) {
                osmap_w0 = (uint32_t)sb[0] | ((uint32_t)sb[1]<<8) | ((uint32_t)sb[2]<<16) | ((uint32_t)sb[3]<<24);
                osmap_w1 = (uint32_t)sb[4] | ((uint32_t)sb[5]<<8) | ((uint32_t)sb[6]<<16) | ((uint32_t)sb[7]<<24);
                osmap_w2 = (uint32_t)sb[8] | ((uint32_t)sb[9]<<8) | ((uint32_t)sb[10]<<16) | ((uint32_t)sb[11]<<24);
                if( osmap_w0 == dt_type && osmap_w1 == dt_num && osmap_w2 == dt_trig)
                    osmap_same = 1u;
            }
        }
    }
    if( osmap_ctrlrs != 0) {
        OSSymbol * cn = OSDynamicCast( OSSymbol, osmap_ctrlrs->getObject( 0 ));
        if( cn != 0) {
            const char * cs = cn->getCStringNoCopy();
            /* The name the OS built is `IOInterruptController%08X`; the eight trailing hex digits are
             * the controller node's phandle, so they are read back out of the OS's own string rather
             * than out of the tree. */
            if( cs != 0) {
                uint32_t v = 0u;
                uint32_t seen = 0u;
                for( uint32_t i = 0; cs[i] != 0 && i < 64u; i++ ) {
                    uint32_t d;
                    if( cs[i] >= '0' && cs[i] <= '9')      d = (uint32_t)(cs[i] - '0');
                    else if( cs[i] >= 'A' && cs[i] <= 'F')  d = (uint32_t)(cs[i] - 'A') + 10u;
                    else if( cs[i] >= 'a' && cs[i] <= 'f')  d = (uint32_t)(cs[i] - 'a') + 10u;
                    else { v = 0u; seen = 0u; continue; }
                    v = (v << 4) | d;
                    seen++;
                }
                if( seen >= 8u)
                    osmap_ph = v;
            }
        }
    }
    if( line_parent != 0u && osmap_ph == line_parent)
        osmap_ph_agree = 1u;

    if( devmem != 0 && devcount > MSM8974_TIMER_FRAME_ENTRY) {
        OSObject * fentry = devmem->getObject( MSM8974_TIMER_FRAME_ENTRY );
        framerange = OSDynamicCast( IOMemoryDescriptor, fentry );
        if( OSDynamicCast( IODeviceMemory, fentry ) != 0)      frame_objkind = 1u;
        else if( framerange != 0)                              frame_objkind = 2u;
        if( framerange != 0) {
            IOByteCount flen = 0u;
            IOPhysicalAddress fphys = framerange->getPhysicalSegment( 0u, &flen, kIOMemoryMapperNone );
            frame_phys = (uint32_t) fphys;
            frame_len  = (uint32_t) flen;
            framemap = framerange->map( kIOMapAnywhere );
            if( framemap != 0) {
                frame_va = (uint32_t)(uintptr_t) framemap->getVirtualAddress();
                g_timer_frame_va = frame_va;
                if( frame_va != 0u) {
                    uint64_t t0, t1;
                    frame_freq = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_FREQ_OFF );
                    /* The cost of one register read, measured here so that the rate comparison's
                     * tolerance is this machine's own number rather than a chosen percentage. */
                    t0 = mach_absolute_time();
                    (void) *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_LOW_OFF );
                    t1 = mach_absolute_time();
                    frame_slack = 4u * (uint32_t)( t1 - t0 );
                    frame_mac_a = mach_absolute_time();
                    cnt_lo_a = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_LOW_OFF );
                    cnt_hi_a = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_HIGH_OFF );
                    frame_cnt_a = ((uint64_t) cnt_hi_a << 32) | (uint64_t) cnt_lo_a;
                    IODelay( 1000u );
                    cnt_lo_b = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_LOW_OFF );
                    cnt_hi_b = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_HIGH_OFF );
                    frame_cnt_b = ((uint64_t) cnt_hi_b << 32) | (uint64_t) cnt_lo_b;
                    frame_mac_b = mach_absolute_time();
                    frame_cnt_d = (uint32_t)( frame_cnt_b - frame_cnt_a );
                    frame_mac_d = (uint32_t)( frame_mac_b - frame_mac_a );
                    if( frame_mac_d >= frame_cnt_d && ( frame_mac_d - frame_cnt_d ) <= frame_slack)
                        frame_rate_ok = 1u;
                    frame_ctl = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CTRL_OFF );
                    frame_ctl_en   = ( frame_ctl & MSM8974_FRAME_CTRL_ENABLE  ) ? 1u : 0u;
                    frame_ctl_mask = ( frame_ctl & MSM8974_FRAME_CTRL_IT_MASK ) ? 1u : 0u;
                    frame_ctl_stat = ( frame_ctl & MSM8974_FRAME_CTRL_IT_STAT ) ? 1u : 0u;
                }
            }
        }
    }

    /* ---- 501: the frame's second view, and the frame's second route to the same counters ---------
     *
     * Two measurements, both **read-only**, and that is the step's whole construction: the previous
     * step's risk was a write to a device nothing had written, and this one's is a wrong *offset*,
     * which no write can create and no argument can settle. Nothing below stores to either view or to
     * the coprocessor's control words - `tools/check_driver_catalogue.py`'s claim 17 refuses a build
     * in which this block writes, because "a step that only reads cannot disturb the OS" is exactly
     * the kind of claim that has to be structural rather than intended.
     *
     * 1. **The second view.** The frame's `reg` is two regions (see `MSM8974_TIMER_VIEW2_ENTRY`), the
     *    device's kernel maps only the first, and this project has never read the second. If it is a
     *    second window onto the same frame, its `FREQ` must read the same word and its counters must
     *    read the *same count* as view 1's within the time the two reads take; if it is another
     *    device, the difference is not a few ticks but the whole counter. The comparison is therefore
     *    `|Δ| <= slack` with the slack measured here, on this machine, around a view-2 read - and the
     *    high words are compared exactly, because a 32-bit difference means nothing if the two
     *    readings are a 2^32 boundary apart. **The run answers the question and the answer is no**:
     *    the view maps (`_v2_phys` 0xf9022000, `_v2_va` 0xc218d000, `_v2_len` 0x1000) and reads
     *    **zero everywhere** - `_v2_freq` 0, `_v2_cntv_lo` 0, `_v2_cntp_lo` 0, `_v2_tval` 0 - so
     *    `_v2_cntv_d` is the frame's own counter (`0x06ed5bca`) and both `_v2_*_ok` are 0. The second
     *    view the tree has carried since 492 is not a window onto this frame; it reads like the
     *    container at 0xf9020000 that 494 read a zero from, and the binding calls it *optional*.
     * 2. **The two routes to one counter.** `CNTPCT`, `CNTVCT` and `CNTV_TVAL` are read through the
     *    frame and through the coprocessor, adjacently, and published with their difference. The
     *    device's kernel has both routes and takes one by `has_cp15` (`arch_timer.c:607`), so on this
     *    machine - which has both - the memory route it does not choose is never read at all. This is
     *    that missing comparison, and it is the strongest reading of the frame's offsets available
     *    here: an offset that is off by one register answers as a difference of ~10^7 ticks.
     *    **The run's answer is two agreements and one disagreement.** `_rt_cntp_lo_fr` 0x06ed5c6b
     *    against `_rt_cntp_lo_cpu` 0x06ed5c72 is 7 ticks, and `_rt_cntv_lo_fr` 0x06ed5c1f against
     *    `_rt_cntv_lo_cpu` 0x06ed5c66 is 71, both inside `_rt_slack` 0x68 (104 ticks, measured here) -
     *    so `_rt_cntp_ok` and `_rt_cntv_ok` are 1, and the frame's two counters are the CPU's own. The
     *    control words are equal exactly (`_rt_ctl_fr` 2, `_rt_ctl_cpu` 2, `_rt_ctl_agree` 1), which is
     *    the coprocessor's `CNTP_CTL` and the frame's `0x02C` reading the same register - the offset
     *    the whole driver is built on, measured rather than argued. The virtual countdown is the
     *    exception and the finding (see the block above): `_rt_tval_ok` 0.
     *    The *control* word is compared exactly rather than with a slack, because a control word does
     *    not run: at this point nothing has armed the physical timer (`_arm_ctl_before` is read later
     *    and must equal `_rt_ctl_fr`), and `CNTV_CTL` belongs to the kernel's own decrementer, which is
     *    published for the reader rather than compared with anything here. `_rt_cntv_ctl_cpu` reads
     *    1 - `ENABLE` with the mask *off* - which is the state `entry_timebase.c` leaves XNU's
     *    decrementer in when its arming succeeded, so the reading is also the independent evidence
     *    that this step did not disturb the OS's timer.
     */
    if( devmem != 0 && devcount > MSM8974_TIMER_VIEW2_ENTRY) {
        OSObject * ventry = devmem->getObject( MSM8974_TIMER_VIEW2_ENTRY );
        v2range = OSDynamicCast( IOMemoryDescriptor, ventry );
        if( OSDynamicCast( IODeviceMemory, ventry ) != 0)      v2_objkind = 1u;
        else if( v2range != 0)                                 v2_objkind = 2u;
        if( v2range != 0) {
            IOByteCount vlen = 0u;
            IOPhysicalAddress vphys = v2range->getPhysicalSegment( 0u, &vlen, kIOMemoryMapperNone );
            v2_phys = (uint32_t) vphys;
            v2_len  = (uint32_t) vlen;
            v2map = v2range->map( kIOMapAnywhere );
            if( v2map != 0) {
                uint64_t vt0, vt1;
                v2_va = (uint32_t)(uintptr_t) v2map->getVirtualAddress();
                if( v2_va != 0u) {
                    vt0 = mach_absolute_time();
                    (void) *(volatile uint32_t *)(uintptr_t)( v2_va + MSM8974_FRAME_CNTV_LOW_OFF );
                    vt1 = mach_absolute_time();
                    v2_slack = 4u * (uint32_t)( vt1 - vt0 );
                    v2_freq = *(volatile uint32_t *)(uintptr_t)( v2_va + MSM8974_FRAME_FREQ_OFF );
                    v2_freq_agree = ( v2_freq != 0u && v2_freq == frame_freq ) ? 1u : 0u;
                    v2_tval = *(volatile uint32_t *)(uintptr_t)( v2_va + MSM8974_FRAME_CNTV_TVAL_OFF );
                    if( frame_va != 0u) {
                        fr_cntv_lo = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_LOW_OFF );
                        v2_cntv_lo = *(volatile uint32_t *)(uintptr_t)( v2_va + MSM8974_FRAME_CNTV_LOW_OFF );
                        fr_cntp_lo = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_LOW_OFF );
                        v2_cntp_lo = *(volatile uint32_t *)(uintptr_t)( v2_va + MSM8974_FRAME_CNTP_LOW_OFF );
                        fr_cntv_hi = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_HIGH_OFF );
                        v2_cntv_hi = *(volatile uint32_t *)(uintptr_t)( v2_va + MSM8974_FRAME_CNTV_HIGH_OFF );
                        fr_cntp_hi = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_HIGH_OFF );
                        v2_cntp_hi = *(volatile uint32_t *)(uintptr_t)( v2_va + MSM8974_FRAME_CNTP_HIGH_OFF );
                        v2_cntv_d = msm8974_delta_abs( v2_cntv_lo, fr_cntv_lo );
                        v2_cntv_ok = ( v2_cntv_hi == fr_cntv_hi && v2_cntv_d <= v2_slack ) ? 1u : 0u;
                        v2_cntp_d = msm8974_delta_abs( v2_cntp_lo, fr_cntp_lo );
                        v2_cntp_ok = ( v2_cntp_hi == fr_cntp_hi && v2_cntp_d <= v2_slack ) ? 1u : 0u;
                    }
                }
            }
        }
    }

    if( frame_va != 0u) {
        uint64_t rt0, rt1;
        uint64_t rt_cntv_ct, rt_cntp_ct;
        uint32_t rt_hi_again;

        /* One coprocessor read's cost, in the counter's own ticks, for the same reason as
         * `_frame_slack` and `_v2_slack`: the tolerance is measured rather than chosen. */
        rt0 = mach_absolute_time();
        (void) msm8974_cpu_cntvct();
        rt1 = mach_absolute_time();
        rt_slack = 4u * (uint32_t)( rt1 - rt0 );

        /* The counters, frame then coprocessor, adjacent so that the difference is the reads and not
         * the run. The frame's own high word is read twice and the pair is taken only when they agree
         * - the loop `counter_get_cntvct_mem` (`arch_timer.c:318`) uses, because a 64-bit counter read
         * through two 32-bit windows can tear. The coprocessor's `mrrc` is one instruction and cannot. */
        do {
            rt_cntv_hi_fr = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_HIGH_OFF );
            rt_cntv_lo_fr = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_LOW_OFF );
            rt_hi_again   = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_HIGH_OFF );
        } while( rt_cntv_hi_fr != rt_hi_again );
        rt_cntv_ct     = msm8974_cpu_cntvct();
        rt_cntv_lo_cpu = (uint32_t) rt_cntv_ct;
        rt_cntv_hi_cpu = (uint32_t)( rt_cntv_ct >> 32 );
        rt_cntv_d = msm8974_delta_abs( rt_cntv_lo_cpu, rt_cntv_lo_fr );
        rt_cntv_ok = ( rt_cntv_hi_cpu == rt_cntv_hi_fr && rt_cntv_d <= rt_slack ) ? 1u : 0u;

        do {
            rt_cntp_hi_fr = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_HIGH_OFF );
            rt_cntp_lo_fr = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_LOW_OFF );
            rt_hi_again   = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_HIGH_OFF );
        } while( rt_cntp_hi_fr != rt_hi_again );
        rt_cntp_ct     = msm8974_cpu_cntpct();
        rt_cntp_lo_cpu = (uint32_t) rt_cntp_ct;
        rt_cntp_hi_cpu = (uint32_t)( rt_cntp_ct >> 32 );
        rt_cntp_d = msm8974_delta_abs( rt_cntp_lo_cpu, rt_cntp_lo_fr );
        rt_cntp_ok = ( rt_cntp_hi_cpu == rt_cntp_hi_fr && rt_cntp_d <= rt_slack ) ? 1u : 0u;

        /* The virtual countdown: the frame's `QTIMER_CNTV_TVAL_REG` (`arch_timer.c:67`) against the
         * coprocessor's `c14, c3, 0`. `CNTV_TVAL` is XNU's own decrementer - this image's
         * `entry_timebase.c` writes it - so this is a reading of a register the kernel is using, and
         * it is read and never written. */
        rt_tval_fr  = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_TVAL_OFF );
        rt_tval_cpu = msm8974_cpu_cntv_tval();
        rt_tval_d   = msm8974_delta_abs( rt_tval_cpu, rt_tval_fr );
        rt_tval_ok  = ( rt_tval_d <= rt_slack ) ? 1u : 0u;

        /* And the control words, which do not run, so these two comparisons are exact. */
        rt_ctl_fr     = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CTRL_OFF );
        rt_ctl_cpu    = msm8974_cpu_cntp_ctl();
        rt_ctl_agree  = ( rt_ctl_fr == rt_ctl_cpu ) ? 1u : 0u;
        rt_cntv_ctl_cpu = msm8974_cpu_cntv_ctl();
    }

    entry_live_write( "xnu_live_timerdrv_line_cells",  dt_cells );
    entry_live_write( "xnu_live_timerdrv_line_type",   dt_type );
    entry_live_write( "xnu_live_timerdrv_line_num",    dt_num );
    entry_live_write( "xnu_live_timerdrv_line_trig",   dt_trig );
    entry_live_write( "xnu_live_timerdrv_line_rule",   line_rule );
    entry_live_write( "xnu_live_timerdrv_line_intid",  line );
    entry_live_write( "xnu_live_timerdrv_line_parent", line_parent );
    entry_live_write( "xnu_live_timerdrv_osmap_ok",    osmap ? 1u : 0u );
    entry_live_write( "xnu_live_timerdrv_osmap_n",     osmap_n );
    entry_live_write( "xnu_live_timerdrv_osmap_w0",    osmap_w0 );
    entry_live_write( "xnu_live_timerdrv_osmap_w1",    osmap_w1 );
    entry_live_write( "xnu_live_timerdrv_osmap_w2",    osmap_w2 );
    entry_live_write( "xnu_live_timerdrv_osmap_same",  osmap_same );
    entry_live_write( "xnu_live_timerdrv_osmap_ph",    osmap_ph );
    entry_live_write( "xnu_live_timerdrv_osmap_ph_agree", osmap_ph_agree );
    entry_live_write( "xnu_live_timerdrv_frame_objkind", frame_objkind );
    entry_live_write( "xnu_live_timerdrv_frame_phys",  frame_phys );
    entry_live_write( "xnu_live_timerdrv_frame_len",   frame_len );
    entry_live_write( "xnu_live_timerdrv_frame_map",   (uint32_t)(uintptr_t) framemap );
    entry_live_write( "xnu_live_timerdrv_frame_va",    frame_va );
    entry_live_write( "xnu_live_timerdrv_frame_freq",  frame_freq );
    /* The other two definitions of the same rate, both already in this record: the tree's
     * (`_freq`) and the CPU's (`_cntfrq`). Held against the frame's here so that one key answers
     * "do all three agree" rather than three keys that a reader has to compare by hand. */
    entry_live_write( "xnu_live_timerdrv_frame_freq_agree",
                      ( frame_freq != 0u && frame_freq == freq_hz && frame_freq == cntfrq ) ? 1u : 0u );
    entry_live_write( "xnu_live_timerdrv_frame_cnt_lo", cnt_lo_a );
    entry_live_write( "xnu_live_timerdrv_frame_cnt_hi", cnt_hi_a );
    entry_live_write( "xnu_live_timerdrv_frame_cnt_d",  frame_cnt_d );
    entry_live_write( "xnu_live_timerdrv_frame_mac_d",  frame_mac_d );
    entry_live_write( "xnu_live_timerdrv_frame_slack",  frame_slack );
    entry_live_write( "xnu_live_timerdrv_frame_rate_ok", frame_rate_ok );
    entry_live_write( "xnu_live_timerdrv_frame_ctl",    frame_ctl );
    entry_live_write( "xnu_live_timerdrv_frame_ctl_en", frame_ctl_en );
    entry_live_write( "xnu_live_timerdrv_frame_ctl_mask", frame_ctl_mask );
    entry_live_write( "xnu_live_timerdrv_frame_ctl_stat", frame_ctl_stat );

    /* 501. Every reading of the second view and of the two routes, and then the comparisons. The
     * five outcome bits are published whatever the device did, and the keys that say *why* are
     * `_v2_phys`/`_v2_objkind`/`_v2_va` on one side and `_rt_*_lo_fr` on the other: a zero outcome
     * with `_v2_va` 0 is "the second view never mapped", and a zero outcome with `_v2_va` non-zero is
     * "it mapped and the two readings disagree" - which is the whole point of publishing both. */
    entry_live_write( "xnu_live_timerdrv_v2_objkind",    v2_objkind );
    entry_live_write( "xnu_live_timerdrv_v2_phys",       v2_phys );
    entry_live_write( "xnu_live_timerdrv_v2_len",        v2_len );
    entry_live_write( "xnu_live_timerdrv_v2_map",        (uint32_t)(uintptr_t) v2map );
    entry_live_write( "xnu_live_timerdrv_v2_va",         v2_va );
    entry_live_write( "xnu_live_timerdrv_v2_freq",       v2_freq );
    entry_live_write( "xnu_live_timerdrv_v2_freq_agree", v2_freq_agree );
    entry_live_write( "xnu_live_timerdrv_v2_slack",      v2_slack );
    entry_live_write( "xnu_live_timerdrv_v2_cntv_lo",    v2_cntv_lo );
    entry_live_write( "xnu_live_timerdrv_v2_cntv_hi",    v2_cntv_hi );
    entry_live_write( "xnu_live_timerdrv_v2_cntv_d",     v2_cntv_d );
    entry_live_write( "xnu_live_timerdrv_v2_cntv_ok",    v2_cntv_ok );
    entry_live_write( "xnu_live_timerdrv_v2_cntp_lo",    v2_cntp_lo );
    entry_live_write( "xnu_live_timerdrv_v2_cntp_hi",    v2_cntp_hi );
    entry_live_write( "xnu_live_timerdrv_v2_cntp_d",     v2_cntp_d );
    entry_live_write( "xnu_live_timerdrv_v2_cntp_ok",    v2_cntp_ok );
    entry_live_write( "xnu_live_timerdrv_v2_tval",       v2_tval );
    entry_live_write( "xnu_live_timerdrv_fr_cntv_lo",    fr_cntv_lo );
    entry_live_write( "xnu_live_timerdrv_fr_cntv_hi",    fr_cntv_hi );
    entry_live_write( "xnu_live_timerdrv_fr_cntp_lo",    fr_cntp_lo );
    entry_live_write( "xnu_live_timerdrv_fr_cntp_hi",    fr_cntp_hi );
    entry_live_write( "xnu_live_timerdrv_rt_slack",      rt_slack );
    entry_live_write( "xnu_live_timerdrv_rt_cntv_lo_fr",  rt_cntv_lo_fr );
    entry_live_write( "xnu_live_timerdrv_rt_cntv_hi_fr",  rt_cntv_hi_fr );
    entry_live_write( "xnu_live_timerdrv_rt_cntv_lo_cpu", rt_cntv_lo_cpu );
    entry_live_write( "xnu_live_timerdrv_rt_cntv_hi_cpu", rt_cntv_hi_cpu );
    entry_live_write( "xnu_live_timerdrv_rt_cntv_d",      rt_cntv_d );
    entry_live_write( "xnu_live_timerdrv_rt_cntv_ok",     rt_cntv_ok );
    entry_live_write( "xnu_live_timerdrv_rt_cntp_lo_fr",  rt_cntp_lo_fr );
    entry_live_write( "xnu_live_timerdrv_rt_cntp_hi_fr",  rt_cntp_hi_fr );
    entry_live_write( "xnu_live_timerdrv_rt_cntp_lo_cpu", rt_cntp_lo_cpu );
    entry_live_write( "xnu_live_timerdrv_rt_cntp_hi_cpu", rt_cntp_hi_cpu );
    entry_live_write( "xnu_live_timerdrv_rt_cntp_d",      rt_cntp_d );
    entry_live_write( "xnu_live_timerdrv_rt_cntp_ok",     rt_cntp_ok );
    entry_live_write( "xnu_live_timerdrv_rt_tval_fr",     rt_tval_fr );
    entry_live_write( "xnu_live_timerdrv_rt_tval_cpu",    rt_tval_cpu );
    entry_live_write( "xnu_live_timerdrv_rt_tval_d",      rt_tval_d );
    entry_live_write( "xnu_live_timerdrv_rt_tval_ok",     rt_tval_ok );
    entry_live_write( "xnu_live_timerdrv_rt_ctl_fr",      rt_ctl_fr );
    entry_live_write( "xnu_live_timerdrv_rt_ctl_cpu",     rt_ctl_cpu );
    entry_live_write( "xnu_live_timerdrv_rt_ctl_agree",   rt_ctl_agree );
    entry_live_write( "xnu_live_timerdrv_rt_cntv_ctl_cpu", rt_cntv_ctl_cpu );

    /* The handler's own keys are published as zeroes *before* the registration, for 495's reason: a
     * key that only exists after the handler runs would make "the line never fired" and "the driver
     * never registered" the same silence. */
    entry_live_write( "xnu_live_timerdrv_isr", (uint32_t)(uintptr_t) &msm8974_timer_isr );
    entry_live_write( "xnu_live_timerdrv_isr_calls", 0u );
    entry_live_write( "xnu_live_timerdrv_isr_intid", 0u );
    entry_live_write( "xnu_live_timerdrv_isr_agree", 0u );
    entry_live_write( "xnu_live_timerdrv_isr_ctl", 0u );
    entry_live_write( "xnu_live_timerdrv_isr_stat", 0u );
    entry_live_write( "xnu_live_timerdrv_isr_tval", 0u );
    entry_live_write( "xnu_live_timerdrv_isr_rearmed", 0u );
    entry_live_write( "xnu_live_timerdrv_isr_masked", 0u );
    entry_live_write( "xnu_live_timerdrv_isr_ctl_after", 0u );
    entry_live_write( "xnu_live_timerdrv_isr_done", 0u );
    /* And the arming's own, for the same reason: `_arm_rc` of 1 means the device's `ENABLE` read back,
     * and a pre-published 0 makes "the arming refused" a reading rather than a missing key. */
    entry_live_write( "xnu_live_timerdrv_arm_rc", 0u );
    entry_live_write( "xnu_live_timerdrv_arm_line_rc", 0u );

    /* The line is registered only if the tree gave one, and the registration's return is published
     * either way: `_line_cli_rc` of 1 is the payload's registry taking the handler, 0 is it refusing
     * (its per-slot cap or its full table) - and with `_line_intid` of 0 beside it, "the tree had no
     * interrupts property" is a third, distinct record. */
    if( line != 0u) {
        g_timer_line_intid = line;
        line_cli_rc = entry_irq_register_client( line, (uint32_t)(uintptr_t) &msm8974_timer_isr, 0u );
    }
    entry_live_write( "xnu_live_timerdrv_line_cli_rc", line_cli_rc );

    /*
     * ------------------------------------------------------------------------------------------------
     * 500: the deadline goes into the device, and then the line is enabled - in that order
     * ------------------------------------------------------------------------------------------------
     *
     * **The device is the half that was missing, and the run is what says so.** The plan for this step
     * was that a line is not delivered unless the distributor's per-line state says so - true of a GIC at
     * reset, and false of this machine, whose distributor arrived with intid 40 already enabled, already
     * targeted at CPU 0 and already in Group 0 (the run's `_line_isen_before = 0x100`,
     * `_line_target_before = 0x01010101`, `_line_group_before = 0`). The reason is the frame itself: the
     * device's own kernel takes this frame's deadline on this SPI, so the machine that booted before this
     * image had enabled exactly this line for exactly this timer. What no boot had done was put a
     * *deadline* in the frame, and a line with no device behind it asserts nothing - which is why 498's
     * `_isr_calls` was 0. 500 writes both, and the before-values are published so that "the distributor
     * had to be configured" and "it was already configured" stay different readings. The run's answer:
     * `_arm_rc` 1, `_arm_ctl_after` 0x1 (ENABLE, unmasked), `_arm_tval_after` = the count 27 ticks down,
     * `_arm_line_rc` 1 - and three deliveries on the same registration.
     *
     * **And the order below is the image's discipline rather than what made this machine safe.** The
     * earlier draft of this comment called the order "the safety argument" - that the frame must be
     * programmed while the line is still disabled so there is no window with an enabled line and no
     * deadline behind it. The run shows that window was already open before this driver ran (the line was
     * enabled at boot; the frame was masked and disabled, `_arm_ctl_before = 2`) and that nothing came of
     * it, which is the answer the mask state gives: **a disabled or masked frame asserts nothing, so the
     * invariant that holds across the whole boot is "the frame is masked except while a deadline is being
     * serviced"** - `_arm_ctl_before` 2, `_isr_ctl_after` 0x1 on the two re-arms and 0x7 (latched +
     * masked) on the last. The order is still the right discipline and is checked (the store must precede
     * the enable, and the deadline must precede the store); what it is *not* is the thing that kept this
     * machine out of trouble.
     *
     * **Every precondition is a gate, and each refusal is a distinct set of keys.** The frame must have
     * been mapped, the registration must have taken (an unregistered line is the dispatcher's *stop*,
     * so arming it would end the run for a line nobody owns), the frame must report a frequency, and
     * the derived tick count must land in `[1, MSM8974_FRAME_TICKS_MAX]` - a `CNTP_TVAL` of 0 is
     * "already expired" and a wrapped one is a deadline in the far past, and both would fire at once
     * rather than in 10 ms. A refused arming leaves the frame exactly as the machine had it.
     *
     * **The tick count is derived from the frame's own frequency** (`_frame_freq`, and 498 measured the
     * tree's, the CPU's and the frame's three definitions of that rate to agree). A literal 192000 here
     * would be a fourth definition of 19.2 MHz, which is the defect class this file's whole offset block
     * exists to avoid. The run's `_arm_ticks` = 0x2ee00 = 192000 is that derivation, not the number.
     */
    if( line != 0u && line_cli_rc != 0u && frame_va != 0u && frame_freq != 0u) {
        uint64_t want = ((uint64_t) frame_freq * (uint64_t) MSM8974_FRAME_ARM_MS) / 1000u;

        arm_ticks = (uint32_t) want;
        arm_ctl_before = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CTRL_OFF );

        if( want != 0u && want <= (uint64_t) MSM8974_FRAME_TICKS_MAX && arm_ticks == want) {
            /* The record is written **before** the deadline goes into the device, so a handler that
             * fires between the two writes finds the count it needs instead of a zero - the rule
             * `g_timer_arm_ticks` carries above, and the one shape 497 is about. */
            g_timer_arm_ticks = arm_ticks;

            *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_TVAL_OFF ) = arm_ticks;
            *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CTRL_OFF ) =
                MSM8974_FRAME_CTRL_ENABLE;

            /* Read both back *from the device*. A write to a device with no clock behind it can vanish,
             * and "the driver wrote ENABLE" and "the frame is counting" are different findings: the
             * second is what the line is being enabled for. */
            arm_ctl = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CTRL_OFF );
            arm_tval_after = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_TVAL_OFF );
            arm_rc = (( arm_ctl & MSM8974_FRAME_CTRL_ENABLE ) != 0u) ? 1u : 0u;

            if( arm_rc != 0u) {
                arm_line_rc =
                    entry_irq_enable_line( line, MSM8974_TIMER_LINE_TARGET );
                if( arm_line_rc == 0u) {
                    /* The line did not take. Mask the frame rather than leave it counting at a CPU the
                     * distributor will never tell: the deadline would lapse with nobody to service it,
                     * and `_arm_line_rc` of 0 beside `_arm_rc` of 1 is the pair that says which of the
                     * two halves failed. The record goes back to zero with the device, so a nonzero
                     * `_arm_ticks` in the log means the handler is allowed to re-arm. */
                    *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CTRL_OFF ) =
                        arm_ctl | MSM8974_FRAME_CTRL_IT_MASK;
                    g_timer_arm_ticks = 0u;
                }
            } else {
                /* The device did not take the write. It is masked and disabled as the machine had it,
                 * and the line is left alone: enabling it would be enabling a line for a frame nothing
                 * started. */
                *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CTRL_OFF ) =
                    MSM8974_FRAME_CTRL_IT_MASK;
                g_timer_arm_ticks = 0u;
            }
        }
    }

    entry_live_write( "xnu_live_timerdrv_arm_ms",        MSM8974_FRAME_ARM_MS );
    entry_live_write( "xnu_live_timerdrv_arm_ticks_max", MSM8974_FRAME_TICKS_MAX );
    entry_live_write( "xnu_live_timerdrv_arm_ticks",     arm_ticks );
    entry_live_write( "xnu_live_timerdrv_arm_ctl_before", arm_ctl_before );
    entry_live_write( "xnu_live_timerdrv_arm_ctl_after", arm_ctl );
    entry_live_write( "xnu_live_timerdrv_arm_tval_after", arm_tval_after );
    entry_live_write( "xnu_live_timerdrv_arm_rc",        arm_rc );
    entry_live_write( "xnu_live_timerdrv_arm_target",    MSM8974_TIMER_LINE_TARGET );
    entry_live_write( "xnu_live_timerdrv_arm_line_rc",   arm_line_rc );

    /* ---- 502: what the frame's `0x038` is, and whether the frame's counter is the core's ----------
     *
     * 501 read `0x038` - `QTIMER_CNTV_TVAL_REG` (`arch_timer.c:67`), the one of that file's eight timer
     * offsets with no reader anywhere in the kernel - and found it disagreeing with the coprocessor's
     * `CNTV_TVAL` by `0x06f034ff` ticks (`_rt_tval_fr` `0xf912a3cc` against `_rt_tval_cpu` `0x0002d8cb`,
     * `_rt_tval_ok` 0). It recorded the disagreement and said what it could not say: **whether the frame's
     * virtual countdown lives behind a control word neither step writes, or somewhere else, is a question
     * that needs a write to answer**. This block answers it with reads, because the ARM generic timer
     * defines `TVAL` as a *view* of a compare value rather than a register of its own:
     *
     *       TVAL  =  CVAL - <the counter>
     *
     * - which the device's kernel relies on without ever naming, because it programs deadlines by writing
     * `QTIMER_CNTP_TVAL_REG` (`arch_timer.c:76`) and reading the countdown back from the same register
     * (`arch_timer.c:115`), and never reads a compare-value register at all. Two consequences follow,
     * and both are checkable here:
     *
     *   - **While the compare value is fixed, `TVAL + counter` is a constant.** So the sum is invariant
     *     across two readings a measured interval apart, on either side of the frame. That is a
     *     prediction with nothing unknown in it - no reference value, no ground truth, no assumption
     *     about which timer it is - and it is the test that says "this register is a countdown to a
     *     fixed compare value" rather than "this register happens to hold a number".
     *   - **An unprogrammed compare value reads as minus the counter.** If nothing ever wrote the
     *     frame's virtual compare value, then `0 - CNTVCT` is what `0x038` must read, and 501's
     *     `0xf912a3cc` is that number: `2^32 - 0xf912a3cc` = `0x06ed5c34`, and the frame's `CNTVCT` read
     *     in the same block is `0x06ed5c1f` - 21 ticks apart, which is the two reads' own cost.
     *
     * **The control is the same frame's other timer**, and it is the reason this block sits *after* the
     * arming above rather than before it: `0x028` has just been written by this driver (`_arm_ticks`
     * 192000, read back 27 ticks down as `_arm_tval_after`), so the physical side's compare value is
     * *this file's own write* and its sum must be `CNTPCT_at_write + _arm_ticks` - a number whose two
     * halves are both published. The virtual side's compare value is nobody's write, and its sum must be
     * zero. One frame, two timers, the same arithmetic, one armed and one not.
     *
     * **And the second half of 501's finding, which its own slack cannot settle.** 501 compared the
     * frame's counter with the coprocessor's and found them 71 ticks apart inside a measured slack of
     * 104; the conclusion it drew - "the two routes agree" - is true of the *numbers*, and the bound it
     * used admits a rival explanation: **two separate counters that both count the same 19.2 MHz clock
     * would also read within a hundred ticks of each other** if they are phase-locked or nearly so. The
     * bracket below separates the two readings as tightly as this machine can: read the frame's counter,
     * then the core's, then the frame's again, and require the core's reading to lie **between** the two
     * frame readings. If they are one counter, it must - the bracket is the cost of one `mrrc` and the
     * core's reading is inside it by construction. If they are two counters, the core's reading sits
     * outside by their phase offset, and the test says so. **The test's power is bounded and the bound is
     * published** (`_cv_wmax`, `_cv_wmin`, `_cv_d0`): a phase offset smaller than the bracket is not
     * detectable by this method, and a reader who sees `_cv_inside` = 8 with `_cv_wmax` = 400 has
     * learned less than one who sees it with `_cv_wmax` = 30.
     *
     * Nothing below stores to a device or to a coprocessor register: the compare values are read, not
     * programmed, and `tools/check_driver_catalogue.py`'s claim 18 refuses a build in which this block
     * writes through either base.
     */
    if( frame_va != 0u) {
        uint64_t cv_t0, cv_t1;
        uint32_t cv_i;

        cv_t0 = mach_absolute_time();
        (void) *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_LOW_OFF );
        cv_t1 = mach_absolute_time();
        cv_slack = 4u * (uint32_t)( cv_t1 - cv_t0 );

        /* The physical side, twice: `0x028` against `0x000`, and the same pair again a few reads later.
         * `arm_ticks` is the count this file wrote, so `_cv_p_lag` should be the ticks the countdown has
         * run since - tens, not thousands. */
        cv_p_tval1  = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_TVAL_OFF );
        cv_p_ct1_lo = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_LOW_OFF );
        cv_p_tval2  = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_TVAL_OFF );
        cv_p_ct2_lo = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_LOW_OFF );
        cv_p_sum1 = cv_p_tval1 + cv_p_ct1_lo;
        cv_p_sum2 = cv_p_tval2 + cv_p_ct2_lo;
        cv_p_inv_d  = msm8974_delta_abs( cv_p_sum1, cv_p_sum2 );
        cv_p_inv_ok = ( cv_p_inv_d <= cv_slack ) ? 1u : 0u;

        /* The compare value the sum implies, held against this file's own write. `_cv_p_gt_now` is the
         * deadline being ahead of the counter (what an armed timer reads) and `_cv_p_lag` is how far the
         * countdown has already run - `_arm_ticks` minus the countdown, which is a *derived* number and
         * not a second copy of the interval. */
        cv_p_cval_lo  = cv_p_sum1;
        cv_p_arm_ct_lo = cv_p_cval_lo - arm_ticks;
        cv_p_gt_now   = ((int32_t)( cv_p_cval_lo - cv_p_ct1_lo ) > 0) ? 1u : 0u;
        cv_p_lag      = ( cv_p_tval1 <= arm_ticks ) ? ( arm_ticks - cv_p_tval1 ) : 0u;

        /* The virtual side, the same arithmetic. The compare value is nobody's write, so the sum is
         * zero - within the reads' own cost, which is what `_cv_slack` is. */
        cv_v_tval1  = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_TVAL_OFF );
        cv_v_ct1_lo = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_LOW_OFF );
        cv_v_tval2  = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_TVAL_OFF );
        cv_v_sum1 = cv_v_tval1 + cv_v_ct1_lo;
        cv_v_sum2 = cv_v_tval2 + *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_LOW_OFF );
        cv_v_inv_ok = ( msm8974_delta_abs( cv_v_sum1, cv_v_sum2 ) <= cv_slack ) ? 1u : 0u;
        cv_v_zero_d = ( cv_v_sum1 <= 0x80000000u ) ? cv_v_sum1 : ( 0u - cv_v_sum1 );
        cv_v_zero_ok = ( cv_v_zero_d <= cv_slack ) ? 1u : 0u;

        /* The virtual control word, and the register beside it is the one that settles which timer it
         * belongs to: the core's own `CNTV_CTL` is `ENABLE` with no mask (501's `_rt_cntv_ctl_cpu` 1),
         * while a compare value of zero has been passed by the counter since boot, so an *unmasked*
         * frame-side virtual timer would have to read `ISTATUS` set. */
        cv_v_ctl_fr    = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_CTL_OFF );
        cv_v_ctl_cpu   = msm8974_cpu_cntv_ctl();
        cv_v_ctl_agree = ( cv_v_ctl_fr == cv_v_ctl_cpu ) ? 1u : 0u;
        cv_v_ctl_mask  = ( cv_v_ctl_fr & MSM8974_FRAME_CTRL_IT_MASK ) ? 1u : 0u;

        /* The bracket. Eight samples, and what is published is the aggregate rather than the eight -
         * `_cv_inside` out of `_cv_n`, the widest and narrowest bracket, and the first sample's
         * `_cv_d0` (the core's reading measured from the frame's low end), because a count alone cannot
         * say how much room the test had. `_cv_d0` is the first sample's difference as plain
         * two's-complement arithmetic, and it has **three** cases rather than the two this comment named
         * before the run: a value between zero and the bracket's width is the core's reading inside it, a
         * value near `2^32` is a reading that many ticks *before* the frame's low end, and - the case the
         * machine produced - a positive value *larger* than the width is a reading past the bracket's
         * **upper** end. The run measured `_cv_inside` 0 of `_cv_n` 8 with `_cv_wmax` 13 and `_cv_d0` 70,
         * i.e. the third case by `0x39` ticks, and that is why the block below asks the same question a
         * second time with the two sides the other way round. A sample whose bracket is wider than
         * `0x00ffffff` ticks is not counted at all (`_cv_n` then reads less than 8): a preemption inside
         * the bracket would widen it without bound, and a widened bracket is a weaker test, never a
         * false one. */
        for( cv_i = 0u; cv_i < 8u; cv_i++ ) {
            uint32_t w0, w1, h0, h1, h2, w;
            uint64_t c;

            do {
                h0 = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_HIGH_OFF );
                w0 = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_LOW_OFF );
                h1 = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_HIGH_OFF );
            } while( h0 != h1 );
            c = msm8974_cpu_cntvct();
            do {
                h0 = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_HIGH_OFF );
                w1 = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_LOW_OFF );
                h2 = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_HIGH_OFF );
            } while( h0 != h2 );

            if( h1 == h2 && (uint32_t)( c >> 32 ) == h1 )
                cv_hi_agree++;

            w = w1 - w0;
            if( w <= 0x00ffffffu ) {
                if( cv_n == 0u || w > cv_wmax) cv_wmax = w;
                if( cv_n == 0u || w < cv_wmin) cv_wmin = w;
                if( (uint32_t)c >= w0 && (uint32_t)c <= w1 )
                    cv_inside++;
                if( cv_n == 0u )
                    cv_d0 = (uint32_t)c - w0;
                cv_n++;
            }
        }

        /* ---- 502: the same bracket the other way round, and two more pairs asked the same question.
         *
         * The bracket above read the frame, the core, the frame, and required the core's reading to lie
         * *between* the two frame readings. It measured `_cv_inside` 0 of `_cv_n` 8 - and not because the
         * core's reading was near the bracket's edge: `_cv_d0` was 70 while `_cv_wmax` was 13, so the
         * core's reading was past the bracket's *upper* end by 57 ticks, which is a case the bracket's
         * own comment did not name. Three machines produce that reading, and one order cannot tell them
         * apart:
         *
         *   - **the frame's counter and the core's are two registers**, offset by a fixed phase, so a
         *     reading of one is always ahead of a reading of the other taken at the same instant;
         *   - **one of the two paths returns its reading late**: the frame's counter is a device read
         *     and the core's is a coprocessor read of `counter_get_cntvct_cp15` (`arch_timer.c:331`),
         *     and neither is ordered against the other, so a reading can be returned after a read that
         *     follows it in program order - which is the same reading, with the same sign;
         *   - **they run at different rates**, in which case a single comparison of two readings means
         *     nothing at all, and every frame-against-core difference this driver publishes (498's, 501's
         *     `_rt_cntp_*`/`_rt_cntv_*`, `_cv_p_lag` above) carries a bias that grows with the interval.
         *
         * Reading the pair *both* ways round separates them, because two of the three terms cancel and
         * the third does not - but which of the three the sum ends up holding is the run's question, not
         * this comment's answer, so the arithmetic is written out and then bounded rather than asserted.
         * Take one sample of the difference `core - frame` and take it twice: once with the frame's
         * counter read first and once with the core's read first. Writing `a` for the spacing between two
         * readings of one sample, `K` for the phase (the core's reading minus the frame's, at one instant)
         * and `dL` for the difference between the two *paths'* latencies, the two samples are
         *
         *       order X (frame, core, frame)   core - frame  =  +a + K + dL
         *       order Y (core, frame, core)    core - frame  =  -a + K + dL
         *
         * so
         *
         *       the sum of the two orders   =  2K + 2dL   (the spacing cancels: +a once, -a once)
         *       the difference of the two   =  2a        (the phase cancels, and so does `dL`)
         *
         * **The sum holds the phase *and* a path-latency difference, and one pair cannot tell those two
         * apart - which is why there are three.** The frame's own two windows take one path on both
         * sides, so `dL` is zero there by construction and `_cv_win_sum` is the reading that needs no
         * model: near zero over eight samples means the frame's `0x000` and `0x008` are one register read
         * twice, and a value that stands above the samples' own noise means they are two registers that
         * do not agree. `_cv_xv_sum` asks the same question across two *different* paths - the frame's
         * window and `mrrc p15, 1` - and `_cv_xp_sum` asks it of the physical pair as well, because `dL`
         * is a property of the two paths and is therefore the same in both pairs: it cancels in their
         * difference, while a register offset between the frame's counters does not. The physical pair is
         * 501's own control (measured 7 ticks apart inside a slack of 104), so a control that answers
         * "two registers" would be saying that slack was hiding this.
         *
         * Every sample carries its own witness for the read cost - the two readings of the same side,
         * which measure nothing else - and a sample whose witness exceeds `MSM8974_CV_COST_MAX` is
         * refused rather than averaged (`_cv_win_wmax` publishes the widest witness accepted, so the
         * refusal bound's headroom is in the record and not only in the constant).
         *
         * The rate is measured first and measured directly, because it is the one answer that would make
         * the sums above meaningless: the frame's counter's advance over an interval timed by
         * `mach_absolute_time`, published as `_cv_r_ppm` - parts per million, where 1000000 is "the frame
         * ticks at the core's rate". An interrupt inside that interval lengthens both numbers and so
         * leaves the ratio alone, which is why the rate loop needs no rejection test and the bracket
         * samples do.
         *
         * Nothing below stores to the frame, to the core or to any other device: every line is a read.
         */
        {
            uint32_t cv_r_f0 = msm8974_frame_lo( frame_va, MSM8974_FRAME_CNTP_LOW_OFF );
            uint32_t cv_r_c0 = (uint32_t)mach_absolute_time();
            uint32_t cv_r_f1 = cv_r_f0;
            uint32_t cv_r_c1 = cv_r_c0;
            uint32_t cv_i2;

            for( cv_i2 = 0u; cv_i2 < 0x00100000u; cv_i2++ ) {
                cv_r_f1 = msm8974_frame_lo( frame_va, MSM8974_FRAME_CNTP_LOW_OFF );
                cv_r_c1 = (uint32_t)mach_absolute_time();
                if( ( cv_r_f1 - cv_r_f0 ) >= MSM8974_CV_RATE_TICKS ||
                    ( cv_r_c1 - cv_r_c0 ) >= MSM8974_CV_RATE_BOUND )
                    break;
            }
            cv_r_fd  = cv_r_f1 - cv_r_f0;
            cv_r_cd  = cv_r_c1 - cv_r_c0;
            cv_r_ok  = ( cv_r_fd >= MSM8974_CV_RATE_TICKS ) ? 1u : 0u;
            cv_r_n   = cv_i2 + 1u;
            cv_r_ppm = ( cv_r_cd != 0u )
                     ? (uint32_t)( ( (uint64_t)cv_r_fd * 1000000u ) / (uint64_t)cv_r_cd )
                     : 0u;

            for( cv_i2 = 0u; cv_i2 < 8u; cv_i2++ ) {
                uint32_t vf0, vf1, vc, vg0, vg1, vfm;
                uint32_t pf0, pf1, pc, pg0, pg1, pfm;
                uint32_t wx0, wx1, wy0, wy1, wq, wp;

                /* The virtual pair, frame counter against `CNTVCT`. Order X is the shape the bracket
                 * above used - the frame, then the core, then the frame - and order Y puts the core's
                 * reading at the ends. The witness is the two readings of the same side. */
                vf0 = msm8974_frame_lo( frame_va, MSM8974_FRAME_CNTV_LOW_OFF );
                vc  = (uint32_t)msm8974_cpu_cntvct();
                vf1 = msm8974_frame_lo( frame_va, MSM8974_FRAME_CNTV_LOW_OFF );
                vg0 = (uint32_t)msm8974_cpu_cntvct();
                vfm = msm8974_frame_lo( frame_va, MSM8974_FRAME_CNTV_LOW_OFF );
                vg1 = (uint32_t)msm8974_cpu_cntvct();

                /* The physical pair, the same two orders, with `CNTPCT` on the core's side. */
                pf0 = msm8974_frame_lo( frame_va, MSM8974_FRAME_CNTP_LOW_OFF );
                pc  = (uint32_t)msm8974_cpu_cntpct();
                pf1 = msm8974_frame_lo( frame_va, MSM8974_FRAME_CNTP_LOW_OFF );
                pg0 = (uint32_t)msm8974_cpu_cntpct();
                pfm = msm8974_frame_lo( frame_va, MSM8974_FRAME_CNTP_LOW_OFF );
                pg1 = (uint32_t)msm8974_cpu_cntpct();

                /* The frame's own two windows, the same two orders, both readings through the device. */
                wx0 = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_LOW_OFF );
                wq  = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_LOW_OFF );
                wx1 = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_LOW_OFF );
                wy0 = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_LOW_OFF );
                wp  = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTP_LOW_OFF );
                wy1 = *(volatile uint32_t *)(uintptr_t)( frame_va + MSM8974_FRAME_CNTV_LOW_OFF );

                if( ( vf1 - vf0 ) <= MSM8974_CV_COST_MAX && ( vg1 - vg0 ) <= MSM8974_CV_COST_MAX ) {
                    if( cv_xv_n == 0u ) {
                        cv_xv_x0 = vc - vf0;
                        cv_xv_y0 = vg0 - vfm;
                    }
                    cv_xv_x8 += ( vc - vf0 );
                    cv_xv_y8 += ( vg0 - vfm );
                    if( vg0 <= vfm && vfm <= vg1 )
                        cv_xv_m_in++;
                    cv_xv_n++;
                }
                if( ( pf1 - pf0 ) <= MSM8974_CV_COST_MAX && ( pg1 - pg0 ) <= MSM8974_CV_COST_MAX ) {
                    if( cv_xp_n == 0u ) {
                        cv_xp_x0 = pc - pf0;
                        cv_xp_y0 = pg0 - pfm;
                    }
                    cv_xp_x8 += ( pc - pf0 );
                    cv_xp_y8 += ( pg0 - pfm );
                    if( pg0 <= pfm && pfm <= pg1 )
                        cv_xp_m_in++;
                    cv_xp_n++;
                }
                if( ( wx1 - wx0 ) <= MSM8974_CV_COST_MAX && ( wy1 - wy0 ) <= MSM8974_CV_COST_MAX ) {
                    if( cv_win_n == 0u ) {
                        cv_win_x0 = wq - wx0;
                        cv_win_y0 = wy0 - wp;
                    }
                    cv_win_x8 += ( wq - wx0 );
                    cv_win_y8 += ( wy0 - wp );
                    if( cv_win_n == 0u || ( wx1 - wx0 ) > cv_win_wmax )
                        cv_win_wmax = wx1 - wx0;
                    if( ( wy1 - wy0 ) > cv_win_wmax )
                        cv_win_wmax = wy1 - wy0;
                    cv_win_n++;
                }
            }

            /* The estimator, and it is a sum rather than an average so that a refused sample is a
             * smaller count beside the same arithmetic rather than a number divided by a count that
             * moved. The two's-complement reading is the same one `_cv_d0` carries above: 0 means the
             * pair is one register, and a value near `2^32` means the second side reads *below* the
             * first by that many ticks. */
            cv_xv_sum = cv_xv_x8 + cv_xv_y8;
            cv_xv_dif = cv_xv_x8 - cv_xv_y8;
            cv_xp_sum = cv_xp_x8 + cv_xp_y8;
            cv_xp_dif = cv_xp_x8 - cv_xp_y8;
            cv_win_sum = cv_win_x8 + cv_win_y8;
            cv_win_dif = cv_win_x8 - cv_win_y8;
        }
    }

    entry_live_write( "xnu_live_timerdrv_cv_slack",      cv_slack );
    entry_live_write( "xnu_live_timerdrv_cv_p_tval1",    cv_p_tval1 );
    entry_live_write( "xnu_live_timerdrv_cv_p_tval2",    cv_p_tval2 );
    entry_live_write( "xnu_live_timerdrv_cv_p_sum1",     cv_p_sum1 );
    entry_live_write( "xnu_live_timerdrv_cv_p_sum2",     cv_p_sum2 );
    entry_live_write( "xnu_live_timerdrv_cv_p_inv_d",    cv_p_inv_d );
    entry_live_write( "xnu_live_timerdrv_cv_p_inv_ok",   cv_p_inv_ok );
    entry_live_write( "xnu_live_timerdrv_cv_p_cval_lo",  cv_p_cval_lo );
    entry_live_write( "xnu_live_timerdrv_cv_p_arm_ct_lo", cv_p_arm_ct_lo );
    entry_live_write( "xnu_live_timerdrv_cv_p_gt_now",   cv_p_gt_now );
    entry_live_write( "xnu_live_timerdrv_cv_p_lag",      cv_p_lag );
    entry_live_write( "xnu_live_timerdrv_cv_v_tval1",    cv_v_tval1 );
    entry_live_write( "xnu_live_timerdrv_cv_v_tval2",    cv_v_tval2 );
    entry_live_write( "xnu_live_timerdrv_cv_v_sum1",     cv_v_sum1 );
    entry_live_write( "xnu_live_timerdrv_cv_v_sum2",     cv_v_sum2 );
    entry_live_write( "xnu_live_timerdrv_cv_v_inv_ok",   cv_v_inv_ok );
    entry_live_write( "xnu_live_timerdrv_cv_v_zero_d",   cv_v_zero_d );
    entry_live_write( "xnu_live_timerdrv_cv_v_zero_ok",  cv_v_zero_ok );
    entry_live_write( "xnu_live_timerdrv_cv_v_ctl_fr",   cv_v_ctl_fr );
    entry_live_write( "xnu_live_timerdrv_cv_v_ctl_cpu",  cv_v_ctl_cpu );
    entry_live_write( "xnu_live_timerdrv_cv_v_ctl_agree", cv_v_ctl_agree );
    entry_live_write( "xnu_live_timerdrv_cv_v_ctl_mask", cv_v_ctl_mask );
    entry_live_write( "xnu_live_timerdrv_cv_n",          cv_n );
    entry_live_write( "xnu_live_timerdrv_cv_inside",     cv_inside );
    entry_live_write( "xnu_live_timerdrv_cv_wmax",       cv_wmax );
    entry_live_write( "xnu_live_timerdrv_cv_wmin",       cv_wmin );
    entry_live_write( "xnu_live_timerdrv_cv_d0",         cv_d0 );
    entry_live_write( "xnu_live_timerdrv_cv_hi_agree",   cv_hi_agree );
    entry_live_write( "xnu_live_timerdrv_cv_xv_x0",      cv_xv_x0 );
    entry_live_write( "xnu_live_timerdrv_cv_xv_y0",      cv_xv_y0 );
    entry_live_write( "xnu_live_timerdrv_cv_xv_x8",      cv_xv_x8 );
    entry_live_write( "xnu_live_timerdrv_cv_xv_y8",      cv_xv_y8 );
    entry_live_write( "xnu_live_timerdrv_cv_xv_sum",     cv_xv_sum );
    entry_live_write( "xnu_live_timerdrv_cv_xv_dif",     cv_xv_dif );
    entry_live_write( "xnu_live_timerdrv_cv_xv_n",       cv_xv_n );
    entry_live_write( "xnu_live_timerdrv_cv_xv_m_in",    cv_xv_m_in );
    entry_live_write( "xnu_live_timerdrv_cv_xp_x0",      cv_xp_x0 );
    entry_live_write( "xnu_live_timerdrv_cv_xp_y0",      cv_xp_y0 );
    entry_live_write( "xnu_live_timerdrv_cv_xp_x8",      cv_xp_x8 );
    entry_live_write( "xnu_live_timerdrv_cv_xp_y8",      cv_xp_y8 );
    entry_live_write( "xnu_live_timerdrv_cv_xp_sum",     cv_xp_sum );
    entry_live_write( "xnu_live_timerdrv_cv_xp_dif",     cv_xp_dif );
    entry_live_write( "xnu_live_timerdrv_cv_xp_n",       cv_xp_n );
    entry_live_write( "xnu_live_timerdrv_cv_xp_m_in",    cv_xp_m_in );
    entry_live_write( "xnu_live_timerdrv_cv_win_x0",     cv_win_x0 );
    entry_live_write( "xnu_live_timerdrv_cv_win_y0",     cv_win_y0 );
    entry_live_write( "xnu_live_timerdrv_cv_win_x8",     cv_win_x8 );
    entry_live_write( "xnu_live_timerdrv_cv_win_y8",     cv_win_y8 );
    entry_live_write( "xnu_live_timerdrv_cv_win_sum",    cv_win_sum );
    entry_live_write( "xnu_live_timerdrv_cv_win_dif",    cv_win_dif );
    entry_live_write( "xnu_live_timerdrv_cv_win_n",      cv_win_n );
    entry_live_write( "xnu_live_timerdrv_cv_win_wmax",   cv_win_wmax );
    entry_live_write( "xnu_live_timerdrv_cv_r_fd",       cv_r_fd );
    entry_live_write( "xnu_live_timerdrv_cv_r_cd",       cv_r_cd );
    entry_live_write( "xnu_live_timerdrv_cv_r_ppm",      cv_r_ppm );
    entry_live_write( "xnu_live_timerdrv_cv_r_ok",       cv_r_ok );
    entry_live_write( "xnu_live_timerdrv_cv_r_n",        cv_r_n );

    return( true );
}
