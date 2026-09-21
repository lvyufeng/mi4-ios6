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
 * **It does not touch the device.** `_phys0` is a number: the entries are `IODeviceMemory`
 * *descriptions* of physical ranges and `getPhysicalSegment` reads the range out of the object, not
 * the hardware. This node's `0xf9000000` is the GIC the payload maps, but "the payload mapped it" is
 * not "the kernel's page tables map it", and 492's rule stands: the first register read belongs to the
 * step that proves the mapping first.
 *
 * The record it leaves
 * --------------------
 * Fourteen live keys: `_seq` (the ordinal), `_prov` (the provider pointer - this driver's row in 491's
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
        IOMemoryDescriptor * range = OSDynamicCast( IOMemoryDescriptor, entry );
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

    return( true );
}
