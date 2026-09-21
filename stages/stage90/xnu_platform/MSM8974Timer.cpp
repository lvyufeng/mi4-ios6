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
 * **It does not yet touch the device.** The node's `reg[0]` is recorded and not dereferenced: on this
 * machine `0xf9020000` is inside the `io_ranges` window the payload maps (`stage90_main.c`'s
 * `io_ranges = {0, 0xf9000000, 0x07000000}`), but "the window is in the kernel's page tables" is a
 * property of `mmu.c`'s mapping rather than of the device tree, and a first driver that faults on its
 * own device's registers would be the instrument's fault and not the driver's. So the first register
 * read is the next step's, with the mapping proved first.
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
 * Eighteen live keys, written in the order that makes the record readable if the driver stops: `_seq`
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
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>

extern "C" void entry_live_write(const char *key, uint32_t value);

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
        IOMemoryDescriptor * range = OSDynamicCast( IOMemoryDescriptor, entry );
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

    return( true );
}
