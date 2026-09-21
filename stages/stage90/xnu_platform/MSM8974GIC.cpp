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
 * **496's two entry points, and the shape of the ABI is the reason they are spelled here as three
 * `uint32_t`s rather than as a function-pointer type.** This driver cannot include `entry_gic.h`
 * (that header declares `g_stage90_gic_dist_typer` with C linkage and no `extern "C"` wrapper, so
 * including it from C++ would give the symbol C++ linkage and a link error), which is why every
 * declaration in this file is written twice - once in the header and once here with `extern "C"`.
 * A function-pointer parameter would then be two definitions of one signature with nothing comparing
 * them; three integers are comparable character by character, and
 * `tools/check_driver_catalogue.py` does compare them. The driver hands over its own ISR's address,
 * so the number in `_isr` below is a reading of the same value the entry side files as
 * `xnu_live_irq_cli_handler`.
 */
extern "C" uint32_t entry_irq_register_client(uint32_t intid, uint32_t handler, uint32_t refCon);
extern "C" uint32_t entry_irq_unregister_client(uint32_t intid);

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

/*
 * 496: the four registers this driver **writes**, its own line, and how many times it asks for it.
 * Every number here has a second definition in the payload - `gic.c`'s register map and
 * `entry_gic.h`'s transcription of the same map - and none of them is invented: the distributor
 * offsets match the ones 494 already reads through, `GICD_SGIR` and its target-self encoding are
 * `gic.c`'s `GICD_SGIR`/`GICD_SGIR_TARGET_SELF` (the pair the payload's own `gic_sgi_selftest`
 * writes), and the intid is `gic.c`'s `GIC_SGI0_ID`. `tools/check_driver_catalogue.py` refuses the
 * link when any of them disagrees with the payload's spelling of the same value.
 *
 * **SGI 0 and not the line the kernel is already on.** Raising the timer's line would be raising the
 * OS's own clock source, and an SGI is the one interrupt in this machine that a driver may pend
 * itself: it belongs to no peripheral, its pending state is cleared by the acknowledgement, and the
 * payload has already measured the delivery end to end (`gic_sgi_sgi0_count = 1`, `gic_sgi_last_iar
 * = 0`, `gic_sgi_spurious_count = 0` in every run) - so this is the second reader of a measurement
 * taken on the other side of the handoff, not a first guess at how the part behaves.
 */
#define MSM8974_GICD_ISENABLER0_OFF 0x100u
#define MSM8974_GICD_ISPENDR0_OFF   0x200u
#define MSM8974_GICD_ICPENDR0_OFF   0x280u
#define MSM8974_GICD_SGIR_OFF       0xf00u
#define MSM8974_GICD_SGIR_SELF      (2u << 24)
#define MSM8974_GIC_OWN_INTID       0u
#define MSM8974_GIC_ASK_PENDS       3u

/*
 * **The one switch that decides whether this driver writes its device at all, and it is a `#define` in
 * the source for the reason `entry_irq.c`'s `STAGE90_IRQ_ENABLE_LINE` is**: `tools/check_irq_routing.py`
 * checks the *value* as well as the block, so the switch cannot become a flag on a command line where
 * the build that ran and the build the check read are two different machines. Everything from the
 * line's enable to the first request is one `#if` block, and the value itself is published to the live
 * buffer (`_drive_compiled`) so that a run's log carries which of the two machines it was - a key that
 * is the honest answer to "did this image write the distributor", and that no reading of the artifact
 * afterwards has to reconstruct.
 *
 * `0` leaves the machine exactly as 494 left it: the map is still made and the same registers are
 * still read, the client registry is still linked (it is dead code if nothing registers), and no
 * device register is written and no line is owned.
 */
#define MSM8974_GIC_DRIVE_SGI 1

/*
 * **And everything that only exists because of it is inside `#if MSM8974_GIC_DRIVE_SGI` too** - the
 * saved mapping, the counters, the handler and, in `start`, the enable, the registration and the
 * request. The property that buys is worth stating: with the switch at 0 there is no store to this
 * device through any name in this file, which is a claim a reader can check by looking for the two
 * `#if`s rather than by reading three functions and reasoning about which stores are reachable.
 * `tools/check_driver_catalogue.py` asserts it - every device store in the file has to lie inside one
 * of those blocks - so the invariant cannot be lost by an edit that adds a store somewhere else.
 */
#if MSM8974_GIC_DRIVE_SGI

/*
 * The distributor's base **as the driver's own mapping returned it**, saved because the ISR runs long
 * after `start` has returned and cannot see its locals. Zero means 494's map failed, and every access
 * below is guarded on it: a dereference of zero on this machine is a data abort, and an abort that
 * names a *zero* would say nothing about the mapping. It is a *saved reading* and not a second
 * address: `xnu_live_gicdrv_mapvaddr` publishes the same number in the same run, and the two are the
 * record that the ISR is writing the device the driver mapped and not one of the two other ways this
 * project can reach `0xf9000000` (the payload's identity section, and the node's own `reg[0]`).
 */
static uint32_t g_gic_mapvaddr;

/* What the ISR measured. The zeroes are published in `start` before the first request, so "the
 * kernel never called this driver back" is a value in the log and not an absence. */
static uint32_t g_gic_pends;            /* requests the driver made, from process context    */
static uint32_t g_gic_isr_calls;        /* calls the kernel made, one per delivery           */
static uint32_t g_gic_isr_pends;        /* requests the driver made, from inside its own ISR */
static uint32_t g_gic_isr_last;         /* the intid of the last call                        */
static uint32_t g_gic_isr_pend;         /* the pending word after the acknowledgement        */
static uint32_t g_gic_isr_guard;        /* 1 = the distributor said the line was quiet       */
static uint32_t g_gic_isr_unregs;       /* times the driver gave the line back               */
static uint32_t g_gic_isr_unreg_rc;
static uint32_t g_gic_isr_done;

/*
 * The driver's interrupt handler, and **the first function in this project that the kernel calls
 * because a *driver* asked for an interrupt**. 495 made the OS call a driver back on its own
 * timeout; this is called from the exception path, through `entry_irq_handler`, for a line the
 * driver enabled at the distributor and asked for itself.
 *
 * It runs with interrupts masked, on the entry image's interrupt stack, with the dispatcher having
 * already written `GICC_EOIR`. Its work is three readings and one decision:
 *
 *   - **what the acknowledgement did to the distributor.** The architecture says an SGI's pending
 *     state is cleared by reading `GICC_IAR`, and this reads `ISPENDR0` rather than believing it -
 *     if the bit is still set the driver clears it with the write-1-to-clear register, so the
 *     machine leaves the handler the way it found it either way.
 *   - **the next request, from the interrupt.** The second and third deliveries are asked for from
 *     *inside* the handler, which is the same shape 495's callback used to keep its own timer alive,
 *     and it is what makes `_isr_calls` a sequence rather than a single event: 1, 2, 3, with the
 *     first asked for in process context and the next two in interrupt context.
 *   - **the guard on giving the line back.** An intid that reaches the dispatcher with no client is
 *     the case that stops the run, so this driver unregisters only when `ISPENDR0` says nothing is
 *     pending on its line. If the bit is still set the driver keeps the registration and simply
 *     stops asking - the safe direction, and `_isr_guard = 0` is that reading.
 *
 * It is a plain function because that is what the registry stores, and it is defined above the class
 * because `start` takes its address.
 */
static void
msm8974_gic_isr( void * refCon, uint32_t intid )
{
    const uint32_t bit = 1u << MSM8974_GIC_OWN_INTID;
    uint32_t calls = ++g_gic_isr_calls;
    uint32_t pend = 0u;

    g_gic_isr_last = intid;

    if( g_gic_mapvaddr != 0u) {
        pend = *(volatile uint32_t *)(uintptr_t)( g_gic_mapvaddr + MSM8974_GICD_ISPENDR0_OFF );
        if(( pend & bit ) != 0u) {
            *(volatile uint32_t *)(uintptr_t)( g_gic_mapvaddr + MSM8974_GICD_ICPENDR0_OFF ) = bit;
            __asm__ volatile ("dsb sy" ::: "memory");
        }
        g_gic_isr_pend = *(volatile uint32_t *)(uintptr_t)( g_gic_mapvaddr + MSM8974_GICD_ISPENDR0_OFF );
    }

    entry_live_write( "xnu_live_gicdrv_isr_calls", calls );
    entry_live_write( "xnu_live_gicdrv_isr_last", intid );
    entry_live_write( "xnu_live_gicdrv_isr_refcon", (uint32_t)(uintptr_t) refCon );
    entry_live_write( "xnu_live_gicdrv_isr_pend_after_ack", pend );
    entry_live_write( "xnu_live_gicdrv_isr_pend_final", g_gic_isr_pend );

    if( calls < MSM8974_GIC_ASK_PENDS && g_gic_mapvaddr != 0u) {
        *(volatile uint32_t *)(uintptr_t)( g_gic_mapvaddr + MSM8974_GICD_SGIR_OFF )
            = MSM8974_GICD_SGIR_SELF | MSM8974_GIC_OWN_INTID;
        __asm__ volatile ("dsb sy" ::: "memory");
        ++g_gic_isr_pends;
        entry_live_write( "xnu_live_gicdrv_isr_pends", g_gic_isr_pends );
        entry_live_write( "xnu_live_gicdrv_isr_sgir", MSM8974_GICD_SGIR_SELF | MSM8974_GIC_OWN_INTID );
        return;
    }

    g_gic_isr_guard = 0u;
    if( g_gic_mapvaddr != 0u && ( g_gic_isr_pend & bit ) == 0u)
        g_gic_isr_guard = 1u;
    entry_live_write( "xnu_live_gicdrv_isr_guard", g_gic_isr_guard );

    if( g_gic_isr_guard == 1u) {
        g_gic_isr_unreg_rc = entry_irq_unregister_client( MSM8974_GIC_OWN_INTID );
        g_gic_isr_unregs++;
        entry_live_write( "xnu_live_gicdrv_isr_unregs", g_gic_isr_unregs );
        entry_live_write( "xnu_live_gicdrv_isr_unreg_rc", g_gic_isr_unreg_rc );
    }

    g_gic_isr_done = 1u;
    entry_live_write( "xnu_live_gicdrv_isr_done", g_gic_isr_done );
}
#endif /* MSM8974_GIC_DRIVE_SGI */

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
    /* 496 - the writing half: the enable word read before and after, the pending word the line had
     * before anything was asked of it, and the registration's return. Four readings around three
     * writes, so that every write this driver makes has a value beside it that the write is supposed
     * to move, and one that says what was there first. */
    uint32_t   en_before = 0u;
    uint32_t   en_after  = 0u;
    uint32_t   pend_before = 0u;
    uint32_t   cli_rc = 0u;

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

    /*
     * 496: the line, the writes, and the registration.
     *
     * The driver's device *is* the interrupt distributor, and up to here this file had only read it.
     * This is the part where it programs it - three writes through the same VA 494 established, each
     * with its own reading beside it, because a device write whose effect is not read back is an
     * assumption about the part and not a measurement of it:
     *
     *     ISENABLER0 |= bit      -> `_en_before` / `_en_after`: the line was already enabled by the
     *                               boot chain (`0xc7fff` has bit 0 set in every run of the payload's
     *                               own self-test), so this is an idempotent re-assertion and the two
     *                               numbers say so rather than claiming the driver turned it on
     *     SGIR = SELF | intid    -> the request itself, in the register `gic.c` measured
     *     ISPENDR0 & bit (in the ISR) -> what the acknowledgement did, and ICPENDR0 only if it did
     *                               not clear it
     *
     * **The order is the safety property.** The registration happens before the request: an intid
     * that arrives with no client is the dispatcher's stop path, so a driver that pended first would
     * end the run at the one place this step exists to keep it out of. And the zeroes are published
     * before the request, for the reason 495's are: a driver that is never called back then has a
     * record that says "asked, never called" instead of keys that are simply absent.
     *
     * All of it is inside `#if MSM8974_GIC_DRIVE_SGI`, the switch above; the compiled value is
     * published outside the block so a run always says which machine it was.
     */
    entry_live_write( "xnu_live_gicdrv_drive_compiled", MSM8974_GIC_DRIVE_SGI );
#if MSM8974_GIC_DRIVE_SGI
    g_gic_mapvaddr = mapvaddr;
    entry_live_write( "xnu_live_gicdrv_isr", (uint32_t)(uintptr_t) &msm8974_gic_isr );
    entry_live_write( "xnu_live_gicdrv_isr_self", (uint32_t)(uintptr_t) this );
    entry_live_write( "xnu_live_gicdrv_own", MSM8974_GIC_OWN_INTID );
    entry_live_write( "xnu_live_gicdrv_isr_ask", MSM8974_GIC_ASK_PENDS );
    entry_live_write( "xnu_live_gicdrv_pends", 0u );
    entry_live_write( "xnu_live_gicdrv_isr_calls", 0u );
    entry_live_write( "xnu_live_gicdrv_isr_pends", 0u );
    entry_live_write( "xnu_live_gicdrv_isr_unregs", 0u );
    entry_live_write( "xnu_live_gicdrv_isr_done", 0u );

    if( mapvaddr != 0u) {
        const uint32_t bit = 1u << MSM8974_GIC_OWN_INTID;

        en_before = *(volatile uint32_t *)(uintptr_t)( mapvaddr + MSM8974_GICD_ISENABLER0_OFF );
        *(volatile uint32_t *)(uintptr_t)( mapvaddr + MSM8974_GICD_ISENABLER0_OFF ) = bit;
        __asm__ volatile ("dsb sy" ::: "memory");
        en_after = *(volatile uint32_t *)(uintptr_t)( mapvaddr + MSM8974_GICD_ISENABLER0_OFF );
        pend_before = *(volatile uint32_t *)(uintptr_t)( mapvaddr + MSM8974_GICD_ISPENDR0_OFF );
    }
    entry_live_write( "xnu_live_gicdrv_en_before", en_before );
    entry_live_write( "xnu_live_gicdrv_en_after", en_after );
    entry_live_write( "xnu_live_gicdrv_pend_before", pend_before );

    cli_rc = entry_irq_register_client( MSM8974_GIC_OWN_INTID,
                                        (uint32_t)(uintptr_t) &msm8974_gic_isr,
                                        (uint32_t)(uintptr_t) this );
    entry_live_write( "xnu_live_gicdrv_cli_rc", cli_rc );

    if( cli_rc == 1u && mapvaddr != 0u) {
        *(volatile uint32_t *)(uintptr_t)( mapvaddr + MSM8974_GICD_SGIR_OFF )
            = MSM8974_GICD_SGIR_SELF | MSM8974_GIC_OWN_INTID;
        __asm__ volatile ("dsb sy" ::: "memory");
        ++g_gic_pends;
        entry_live_write( "xnu_live_gicdrv_pends", g_gic_pends );
        entry_live_write( "xnu_live_gicdrv_sgir", MSM8974_GICD_SGIR_SELF | MSM8974_GIC_OWN_INTID );
    }
#endif /* MSM8974_GIC_DRIVE_SGI */

    return( true );
}
