/*
 * The machine's interrupt controller gets a driver of its own (experiment 493) - the second driver this
 * project links in that is matched to a **device node**, and the one that exists to prove the first
 * was not a special case.
 *
 * Why a second one, and why this node
 * -----------------------------------
 * 492 gave `/timer` a driver, and a single driver cannot answer the question a driver *layer* is for.
 * The mechanism it used has three parts that could each have been a coincidence of that one node: the
 * personality's provider class (`IOPlatformDevice`, the class `IODTPlatformExpert::createNub` builds,
 * `IOPlatformExpert.cpp:1283`), the bucket `IOCatalogue::findDrivers` searches the service's own class
 * chain for (`IOCatalogue.cpp:199-231`), and the candidate test an `IOPlatformDevice` answers with
 * (`compareNubName`, `IOPlatformExpert.cpp:1688-1693`, i.e. `IODTCompareNubName` over the provider
 * nub's `name`, `compatible`, `device_type` and `model`, `IODeviceTreeSupport.cpp:799-824`). A second
 * personality under the same bucket, naming a node of this machine's own device tree that 492's
 * driver's names cannot match, is what turns "a driver started" into "the table matches nodes".
 *
 * The node is `/interrupt-controller` (`compatible = "qcom,msm-qgic2"`), and it is the right second
 * one for a reason beyond convenience: 482 and 483 already measured this machine's GIC from the
 * *payload* side - the line the virtual timer asserts, the offsets `entry_gic.{h,c}` installs, the
 * handler Apple's dispatcher reaches through `cpu_data`+180 - so the addresses this driver resolves
 * are addresses this project already has an independent reading of.
 *
 * What it does, and what it does not
 * ----------------------------------
 * It is a driver and not a stub: `start` reads the OS's answer to "where is your device" - the
 * `IODeviceMemory` array `IODTResolveAddressing` filed on this nub before `start` was called - and
 * publishes it beside the node's own `reg` words. The two are **two definitions of one value**, and
 * the comparison is the whole of the driver.
 *
 * The mechanism, in full, because it is the same code in two files and the point of the pairing:
 *
 *   `IOService::doServiceMatch` on the nub runs `kIOReturnSuccess == getResources()`
 *   (`IOService.cpp:3724`) **before** `probeCandidates( matches )` (`:3726`);
 *   a nub's `getResources` is `IOPlatformDevice::getResources` (`IOPlatformExpert.cpp:1700-1703`),
 *   which is `getNubResources( this )` -> `IODTResolveAddressing( nub, "reg", 0 )` (`:1358-1366`);
 *   and that function files the result under `IODeviceMemoryKey` on the nub
 *   (`IODeviceTreeSupport.cpp:1251`), which `IOService::getDeviceMemory()` returns
 *   (`IOService.cpp:6046-6049`).
 *
 * So the reading is not "what does `reg` say" but "what did the kernel make of `reg`" - and the two
 * can differ, because `IODTResolveAddressing` decides how wide one `reg` entry is from the **parent's**
 * `#address-cells`/`#size-cells` (`:1227`) and `IODTGetCellCounts` (`:1034-1041`) answers `1`/`2` for
 * a node that declares neither. That is Apple's default applied to a tree that did not answer the
 * question, and this tree writes `{address, size}` pairs; 493 declares the counts on the root (see
 * `stage90_main.c`), and this driver's `_devcount`/`_phys0`/`_len0`/`_resolve` are how the run says
 * whether the declaration arrived.
 *
 * **Up to 493 it did not touch the device; 494 is the step that does, and this node is the one that
 * can say whether the access was right.** `_phys0` was a number: the entries are `IOMemoryDescriptor`
 * *descriptions* of physical ranges and `getPhysicalSegment` reads the range out of the object, not the
 * hardware. 494 maps that object through the OS's own machinery - `IOMemoryDescriptor::map(
 * kIOMapAnywhere )`, which for a `kIOMemoryTypePhysical64` descriptor reaches the device-pager arm of
 * `IOGeneralMemoryDescriptor::doMap` (`IOMemoryDescriptor.cpp:641-678`) and returns a kernel VA whose
 * page is the range the OS resolved - and reads two distributor registers through it, at the offsets
 * the payload's own header names (`entry_gic.h`'s `STAGE90_GICD_CTLR` and `STAGE90_GICD_TYPER`).
 *
 * The read is the claim, and it has **two definitions**, which is why it is this node and not
 * `/timer`: `GICD_TYPER` is read-only and constant, so the value the driver reads through the OS's
 * mapping can be held against the value the payload's own probe read through its own 1 MB section at
 * `0xf9000000` - the same register, reached two ways, from two planes, by two pieces of code whose
 * only shared input is the address the device tree declares. `_hwok` is that comparison and `_map`/
 * `_mapvaddr`/`_mapvlen` are what says whether it was even attempted. Nothing here *writes* a device
 * register: the first write belongs to the step that owns the device, and this driver only reads it.
 *
 * The record it leaves
 * --------------------
 * Twenty-one live keys: `_seq` (the ordinal), `_prov` (the provider pointer - this driver's row in 491's
 * third census), `_match` (1 = the node's `name`, 2 = its `compatible`, 3 = neither), `_have` (bit 1 =
 * the node published `reg`), `_reg0`, `_reg1` and `_regwords` (the node's own first address, first
 * size and `reg` length in words), then the OS's answer - `_devmem` (the array, 0 = none), `_devcount`
 * (its count), `_objkind` (1 = the entry really is an `IODeviceMemory`, 2 = it is an
 * `IOMemoryDescriptor` and not one, 0 = neither), `_objlen` (that object's own `getLength()`),
 * `_phys0` and `_len0` (entry 0), and `_resolve` (1 = the OS's entry 0 is the node's first `reg` pair
 * and its count is the node's number of pairs; 2 = answered and one of those disagrees; 3 = answered
 * with no entries; 0 = not answered).
 *
 * **The thirteen keys from `_prov` to `_objlen` are the same thirteen `MSM8974Timer.cpp` publishes**,
 * under this driver's own prefix, and that is what makes the two rows comparable: the same reading
 * taken on two nodes, so a `_resolve` that differs between them is a difference between the two nodes
 * and not between two records of different shapes. `_reg1`, `_objkind` and `_objlen` are the keys 493
 * added to 492's timer record for the same reason - `_reg1` is the right-hand side of the
 * `len0 == reg1` half of the comparison, and the two kind keys are what name a wrong-kind read as a
 * reader's mistake rather than as the OS having resolved nothing.
 *
 * **And 494's seven, which are the access rather than the description**: `_map` (the `IOMemoryMap *`
 * the map call returned, 0 = the OS could not map the range it had just resolved), `_mapvaddr` and
 * `_mapvlen` (that map's `getVirtualAddress()` and `getLength()`), `_mapctlr` and `_maptyper` (the two
 * distributor registers read through `_mapvaddr`, at the offsets the payload's header names),
 * `_probetyper` (the payload's own read of `GICD_TYPER`, out of `g_stage90_gic_dist_typer`) and
 * `_hwok` (1 = the two agree). The guards are visible as numbers for the reason `/timer`'s are: a
 * `_map` of 0 leaves `_mapvaddr`, `_maptyper` and `_hwok` 0, so "the OS mapped nothing" and "the
 * driver read nothing through a map it had" are different readings.
 *
 * **Every one of them is live-only**, for the reason `MSM8974Timer.cpp` gives: the report epilogue has
 * not run since the boot reached `vm_pageout` (490), and a report key would mean `entry_stubs.c`
 * restating a number the driver measured. `entry_live_write` is the entry instrument's own entry
 * point, declared rather than included, as the other three platform sources declare it.
 */
#include <IOKit/IOService.h>
#include <IOKit/IODeviceMemory.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSString.h>

extern "C" void entry_live_write(const char *key, uint32_t value);

/*
 * The payload's own reading of the distributor's identification register, published by name in
 * `entry_gic.c`/`entry_gic.h` for exactly this comparison. Declared rather than included, as the other
 * three platform sources declare `entry_live_write`.
 */
extern "C" uint32_t g_stage90_gic_dist_typer;

/*
 * The two distributor registers this driver reads through the mapping, at the offsets the payload's
 * own header names (`entry_gic.h`'s `STAGE90_GICD_CTLR` and `STAGE90_GICD_TYPER`) - one offset, two
 * definitions, and `tools/check_driver_catalogue.py` reads both files and refuses a build where they
 * disagree. `GICD_CTLR` is the enable bit the payload leaves set; `GICD_TYPER` is read-only and
 * constant, which is what makes it usable as the compared value.
 */
#define MSM8974_GICD_CTLR_OFF   0x000u
#define MSM8974_GICD_TYPER_OFF  0x004u

class MSM8974GIC : public IOService
{
    OSDeclareDefaultStructors(MSM8974GIC);

public:
    virtual bool start( IOService * provider ) APPLE_KEXT_OVERRIDE;
};

/* `super` is a per-file macro in this tree rather than a keyword, so a file that wants
 * `super::start` has to say so - the same line the other three platform sources carry. */
#define super IOService

OSDefineMetaClassAndStructors(MSM8974GIC, IOService);

static uint32_t g_gic_starts;

bool
MSM8974GIC::start( IOService * provider )
{
    OSString * matched;
    OSData   * reg;
    uint32_t   which = 0u;
    uint32_t   have  = 0u;
    uint32_t   reg0  = 0u;
    uint32_t   reg1  = 0u;
    uint32_t   regwords = 0u;
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
    uint32_t   mapctlr  = 0u;
    uint32_t   maptyper = 0u;
    uint32_t   probetyper = 0u;
    uint32_t   hwok = 0u;

    if( !super::start( provider )) return( false );
    if( provider == 0) return( false );

    g_gic_starts++;

    /* Which of the personality's two names the candidate test matched. The key is the kernel's own
     * (`IOService::matchInternal` leaves the matched name in the driver's property table as
     * `IONameMatched`, `IOService.cpp:5443-5448`). */
    matched = OSDynamicCast( OSString, getProperty( "IONameMatched" ));
    if( matched != 0) {
        if( matched->isEqualTo( "interrupt-controller" ))  which = 1u;
        else if( matched->isEqualTo( "qcom,msm-qgic2" ))   which = 2u;
        else                                               which = 3u;
    }

    entry_live_write( "xnu_live_gicdrv_seq", g_gic_starts );
    entry_live_write( "xnu_live_gicdrv_prov", (uint32_t)(uintptr_t) provider );
    entry_live_write( "xnu_live_gicdrv_match", which );

    /* The node's own description. `reg` is an `OSData` of u32 words - `IODeviceTreeSupport.cpp:379`
     * makes every device-tree property an `OSData` when the node becomes a registry entry - read one
     * byte at a time so the driver risks no unaligned load to read four words. */
    reg = OSDynamicCast( OSData, provider->getProperty( "reg" ));
    if( reg != 0 && reg->getLength() >= 8u) {
        const unsigned char * b = (const unsigned char *) reg->getBytesNoCopy();
        if( b != 0) {
            have |= 1u;
            reg0 = (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
            reg1 = (uint32_t)b[4] | ((uint32_t)b[5]<<8) | ((uint32_t)b[6]<<16) | ((uint32_t)b[7]<<24);
        }
        regwords = reg->getLength() / 4u;
    }
    entry_live_write( "xnu_live_gicdrv_have", have );
    entry_live_write( "xnu_live_gicdrv_reg0", reg0 );
    /* The node's own size for its first window, the right-hand side of the `len0 == reg1` half of the
     * resolution comparison (493): published so a `_resolve` of 2 names its own cause. */
    entry_live_write( "xnu_live_gicdrv_reg1", reg1 );
    entry_live_write( "xnu_live_gicdrv_regwords", regwords );

    /* The OS's answer, read exactly as `MSM8974Timer.cpp` reads its own - one idiom in two files, so
     * that a defect in the reading is a defect in both and the two records can be compared key by
     * key. Nothing here is derived from `reg`: a driver that computed the address itself and then
     * checked its own arithmetic would be measuring nothing.
     *
     * **The entry is read as an `IOMemoryDescriptor`, not as an `IODeviceMemory`, and 493's first run
     * is why**: casting to `IODeviceMemory` - the type the array is documented to hold - answered 0
     * for every entry, because `IODeviceMemory::withRange` is a blind cast of
     * `IOMemoryDescriptor::withAddressRange` (`IODeviceMemory.cpp:34-39`) and that factory builds an
     * `IOGeneralMemoryDescriptor`, a *sibling* of `IODeviceMemory`. This driver's first run read
     * `_devcount = 2` (the node's own pair count, so the resolution had happened) beside `_phys0 = 0`,
     * `_len0 = 0` and `_resolve = 2`. The full derivation is in `MSM8974Timer.cpp`'s resolution block;
     * `_objkind` (1 = a real `IODeviceMemory`, 2 = an `IOMemoryDescriptor` and not one - what this
     * machine produces, 0 = neither) and `_objlen` (the object's own `getLength()`, which does not go
     * through the segment walk) are how the record names the difference. */
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
            phys = range->getPhysicalSegment( 0u, &len, kIOMemoryMapperNone );
            phys0 = (uint32_t) phys;
            len0  = (uint32_t) len;
        }
    }
    if( devmem == 0 || devcount == 0u)             resolve = (devmem == 0) ? 0u : 3u;
    else if( phys0 == reg0 && len0 == reg1 && devcount == (regwords / 2u) ) resolve = 1u;
    else                                            resolve = 2u;
    entry_live_write( "xnu_live_gicdrv_devmem", (uint32_t)(uintptr_t) devmem );
    entry_live_write( "xnu_live_gicdrv_devcount", devcount );
    entry_live_write( "xnu_live_gicdrv_objkind", objkind );
    entry_live_write( "xnu_live_gicdrv_objlen", objlen );
    entry_live_write( "xnu_live_gicdrv_phys0", phys0 );
    entry_live_write( "xnu_live_gicdrv_len0", len0 );
    entry_live_write( "xnu_live_gicdrv_resolve", resolve );

    /* ---- 494: the mapping, and the device read through it ------------------------------------
     *
     * 493 ended with the OS's resolution as a *number* - `_phys0`, the physical address the kernel
     * made of the node's `reg[0]` - and a number is not an access. This is the access, and it is
     * taken the way a kext takes it: `IOMemoryDescriptor::map( kIOMapAnywhere )` on the very object
     * 493's resolution array holds, then `getVirtualAddress()` and a word read through that address.
     *
     * What that call does, read out of Apple's files rather than assumed. `IODeviceMemory::withRange`
     * is `IOMemoryDescriptor::withAddressRange( start, length, kIODirectionNone | kIOMemoryMapperNone,
     * NULL )` (`IODeviceMemory.cpp:34-39`) - **the task is NULL**, and
     * `IOMemoryDescriptor::withAddressRanges` turns that into `kIOMemoryTypePhysical64`
     * (`IOMemoryDescriptor.cpp:1176-1179`), so the range really is a physical address and not a
     * virtual one in some task. `map()` is `createMappingInTask( kernel_task, 0, kIOMapAnywhere, ... )`
     * (`:4375-4381`), which is where `kIOMap64Bit` is added - the flag `doMap` and `makeMapping` both
     * panic without (`:3596`, `:4515`). With `_task == NULL`, `IOGeneralMemoryDescriptor::doMap`'s
     * "mapping source == dest" shortcut cannot fire (it is guarded on `_task`, `:3623-3635`), so the
     * work goes to `memoryReferenceCreate`, whose `_task == NULL` arm is the **device pager** arm:
     * `device_pager_setup( NULL, reserved, size, pagerFlags )` with `DEVICE_PAGER_CONTIGUOUS` for a
     * single range, and `mach_memory_object_memory_entry_64` over it (`:641-678`). That pager's
     * `data_request` maps each page with `getPhysicalSegment` - which for a `Physical64` descriptor is
     * the physical address itself - at the cache mode `IODefaultCacheBits` gives a device address.
     * Every one of those symbols is defined in this image (`device_pager_setup` 0x800a4634,
     * `device_pager_data_request` 0x800a43f8, `getKernelReserved` 0x8015a0e0).
     *
     * So the address this driver reads through is not the node's `0xf9000000` and it is not the
     * payload's 1 MB section either: it is a kernel VA the OS's own mapping machinery produced, and
     * the page behind it is the one the OS resolved from `reg`. That is the whole claim, and the
     * falsifier is a value: `GICD_TYPER` read here must equal `GICD_TYPER` as the payload's own probe
     * read it - the same register, reached two ways, by two pieces of code with nothing shared but the
     * address the tree declares. `_hwok` is that comparison; `_map` is published so a mapping that
     * failed is visible as a zero rather than as an absent key.
     *
     * The read is guarded twice - on the map and on the address - because a dereference of zero on
     * this machine is a data abort, and an abort that names a *zero* would say nothing about the
     * mapping. Nothing here writes a device register: this is the first read, and it stays a read. */
    if( range != 0) {
        themap = range->map( kIOMapAnywhere );
        if( themap != 0) {
            mapvaddr = (uint32_t)(uintptr_t) themap->getVirtualAddress();
            mapvlen  = (uint32_t) themap->getLength();
            if( mapvaddr != 0u) {
                mapctlr  = *(volatile uint32_t *)(uintptr_t)( mapvaddr + MSM8974_GICD_CTLR_OFF );
                maptyper = *(volatile uint32_t *)(uintptr_t)( mapvaddr + MSM8974_GICD_TYPER_OFF );
                probetyper = g_stage90_gic_dist_typer;
                hwok = (maptyper == probetyper) ? 1u : 0u;
            }
        }
    }
    entry_live_write( "xnu_live_gicdrv_map", (uint32_t)(uintptr_t) themap );
    entry_live_write( "xnu_live_gicdrv_mapvaddr", mapvaddr );
    entry_live_write( "xnu_live_gicdrv_mapvlen", mapvlen );
    entry_live_write( "xnu_live_gicdrv_mapctlr", mapctlr );
    entry_live_write( "xnu_live_gicdrv_maptyper", maptyper );
    entry_live_write( "xnu_live_gicdrv_probetyper", probetyper );
    entry_live_write( "xnu_live_gicdrv_hwok", hwok );

    return( true );
}
