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
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>

extern "C" void entry_live_write(const char *key, uint32_t value);

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

    return( true );
}
