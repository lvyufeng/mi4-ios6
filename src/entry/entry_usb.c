/*
 * 910a: the USB2 OTG controller's identification and state, read from inside the kernel and written
 * nowhere.
 *
 * ------------------------------------------------------------------------------------------------
 * What this rung is, and the one thing it must not do
 * ------------------------------------------------------------------------------------------------
 *
 * The goal's added requirement is 「保持在xnu里，可以通过usb进行调试」 - a resident XNU must also be
 * *debuggable over USB* - and 910's map established the shape of the work: **there is no USB code in
 * the XNU tree at all** (`iokit/Families/` holds only `IOSystemManagement` and `IONVRAM`; `grep` for
 * `IOUSBFamily`/`AppleUSB`/`dwc3`/`chipidea` over `external/xnu-4570.1.46` is empty) and the Mi 4's
 * micro-B port is served by an Android-only OTG/PHY/gadget stack. So the rung is a **controller +
 * PHY port**, not a configuration flip, and 910 is laddered: 910a reads the controller; 910a2 runs
 * the PHY init and brings the core to device mode; 910b drives the event loop and exposes an
 * enumerable endpoint; 910c binds KDP (the operator's choice, 2026-10-07) to that endpoint.
 *
 * **Every earlier device in this walk was probed before it was written** (692's storage probe; rung
 * A for the card), and USB is the highest-stakes device so far: `adb`/`fastboot` were enumerating on
 * this very port up to the handoff, so the core may be **powered and already in device mode** and a
 * wrong write - a `USBCMD.RST`, a PHY reset - could drop the host's enumeration. The escape from a
 * dark phone is the physical VolDown+Power press, not USB, so `fastboot` stays reachable through the
 * bootloader regardless ([[mi4-device-dark-needs-power-press]]); but the honest first step is still a
 * read. **This file writes NOTHING.**
 *
 * ------------------------------------------------------------------------------------------------
 * What makes it a reading rather than a poke
 * ------------------------------------------------------------------------------------------------
 *
 * The probe is the same four-part shape as `entry_storage.c`'s and `entry_gic.c`'s:
 *
 *   1. **the mapping, and its refusal is the step.** `entry_mmio_section(STAGE90_USB_OTG_BASE, …)`
 *      installs the 1 MB section covering `0xf9a`. A result of 0 means the slot at `0xf9a >> 20`
 *      already held a descriptor and this probe then reads **no USB register at all** - the mapping it
 *      would be reading through is not one it installed, and a read of an address whose translation it
 *      cannot vouch for is a fault, not a measurement. Its four numbers are published under `_usb_*`
 *      so they are never read as the GIC's or the storage block's (693's rule).
 *   2. **the identification, read once and republished by name.** `USB_ID_CAP`, `HWGENERAL`'s PHY
 *      field, `CAPLENGTH`/`HCIVERSION`, `DCIVERSION`. These say *which core this is* - the answer that
 *      turns 910's "DWC-OTG" guess into the ChipIdea fact `entry_usb.h` records - and `USB_ID_CAP` is
 *      exported as `g_stage90_usb_id_cap` so a later reader can compare two ways of reaching it.
 *   3. **the state the bootloader left.** `USBMODE`, `USBCMD`, `USBSTS`, `PORTSC`, `OTGSC`. `USBMODE`'s
 *      low bits are the decisive cell: `2` is device mode, `3` is host, `0` is neither - and this is
 *      the "the mode sequence is a re-do" case (531 section 6) that 910a2 must read before it writes.
 *      `USBCMD.RST` (bit 1) set would mean a reset is in progress; `USBSTS`'s `HCHalted` (bit 12) and
 *      the PHY-suspend bit in `PORTSC` say how far the core got.
 *   4. **the endpoint table, shallowly.** `ENDPTCTRL(0..3)` - four words, enough to say whether the
 *      core's endpoint table is reachable and whether the bootloader left anything configured on the
 *      first four endpoints. Sixteen words are readable; four are enough to answer the question and
 *      keep the record short.
 *
 * Everything is a `volatile` 32-bit read of Device memory, 4-aligned, no `dsb` needed because reads
 * have no write buffer to drain. The ULPI viewport is **read as a register and not transacted** - a
 * transaction is a WRITE with `RUN` set, which is a bus wait (692's class) and belongs to 910a2.
 *
 * ------------------------------------------------------------------------------------------------
 * The controller is a ChipIdea CI13xxx, and the map's owner is named here
 * ------------------------------------------------------------------------------------------------
 *
 * 910's map called this a "DWC-OTG gadget". The device's own checkout says it is a **ChipIdea
 * CI13xxx (MSM72K)**: the node is `qcom,hsusb-otg`, `msm_otg.c` is the OTG/PHY, and the gadget is
 * `drivers/usb/gadget/ci13xxx_msm.c` → `ci13xxx_udc.c`. Reading a DWC3 map off this core's registers
 * would have made every number a reading of the wrong file's bytes, and the `USB_CAPLENGTH`/
 * `USB_USBCMD`/`USB_USBMODE` layout `entry_usb.h` transcribes is ChipIdea's, not DWC's. The DWC3 map
 * belongs to the *other* controller (`0xf9200000`, USB3 SS), which this probe reads only for its
 * identification and treats as a different part.
 */
#include <stdint.h>

#include "entry_usb.h"

/*
 * The live channel and the mapper, both defined in `entry_stubs.c`, and the arm switch. **The switch
 * gates BOTH the body and the records**, the way `STAGE90_XNU_STORAGE_PROBE` gates `entry_storage.c`:
 * with it 0 the whole file is a one-line `void entry_usb_probe(void) { }`, so the call site in
 * `entry_trace.c` is unconditional and the ON and OFF arms are the same source file rather than two -
 * and an OFF build costs one call and no register access. The declarations are **outside** the `#if`
 * (692's repair in `entry_gic.c`): they are not code, so moving them out changes no object byte.
 */
#ifndef STAGE90_XNU_USB_PROBE
#define STAGE90_XNU_USB_PROBE 0
#endif

extern void entry_live_write(const char *key, uint32_t value);
extern uint32_t g_live_state;
/* 484's four numbers, read by `entry_mmio_section` at the install - see `entry_gic.c`. */
extern uint32_t g_live_mmio_l1;
extern uint32_t g_live_mmio_l1_moved;
extern uint32_t g_live_mmio_ttbr0;
extern uint32_t g_live_mmio_ttbr1;
extern uint32_t entry_mmio_section(uint32_t va, uint32_t pa, uint32_t *slot_before_out,
                                   uint32_t *desc_out);

/*
 * 494's shape, applied to this device: the identification and mode words, published under names so a
 * later reader can compare two ways of reaching one register rather than two moments in its life.
 * `USB_ID_CAP` is read-only and constant for the life of the part; `USBMODE` is what 910a2 must read
 * before it writes one. Both are defined unconditionally (the header declares them `extern`), so a
 * non-USB build carries zeros rather than an undefined symbol.
 */
uint32_t g_stage90_usb_id_cap;
uint32_t g_stage90_usb_usbmode;

#if STAGE90_XNU_USB_PROBE

#define USB_LIVE(key, value) entry_live_write((key), (uint32_t)(value))

/*
 * The one access, and the reason it is one: a peripheral register read that the compiler may reorder
 * or elide is not a reading, so the load is a `volatile` dereference and the address is the base plus
 * a transcribed offset. There is no write twin in this file - the whole arm is a read - so the
 * asymmetric pair `entry_irq.c` owns for the GIC has no analogue here yet; 910a2 adds it.
 */
static uint32_t usb_read32(uint32_t off)
{
    return *(volatile uint32_t *)(uintptr_t)(STAGE90_USB_OTG_BASE + off);
}

static uint32_t usb_ss_read32(uint32_t off)
{
    return *(volatile uint32_t *)(uintptr_t)(STAGE90_USB_SS_BASE + off);
}

static uint32_t g_usb_probed;

void entry_usb_probe(void)
{
    uint32_t slot_before = 0u, desc = 0u, mapped;
    uint32_t id_cap, hwgeneral, hwdevice, hwtxbuf, hwrxbuf;
    uint32_t caplen_hciver, hccparams, dciversion;
    uint32_t usbcmd, usbsts, usbintr, frindex, deviceaddr, endpointlistaddr, burstsize;
    uint32_t ulpi_viewport, portsc, otgsc, usbmode;
    uint32_t epctrl0, epctrl1, epctrl2, epctrl3;
    uint32_t ss_id, ss_hwgeneral;

    if (g_usb_probed != 0u)
        return;
    g_usb_probed = 1u;

    USB_LIVE("xnu_live_usb_live_state", g_live_state);
    USB_LIVE("xnu_live_usb_base", STAGE90_USB_OTG_BASE);
    USB_LIVE("xnu_live_usb_section", (uint32_t)(STAGE90_USB_OTG_BASE >> 20));
    USB_LIVE("xnu_live_usb_ss_base", STAGE90_USB_SS_BASE);
    USB_LIVE("xnu_live_usb_ss_section", (uint32_t)(STAGE90_USB_SS_BASE >> 20));

    /*
     * **(1) THE MAPPING, BEFORE ANYTHING IS READ.** A 0 means the slot already held a descriptor and
     * this probe then reads no USB register - the mapping it would read through is not one it
     * installed. THE LI-FACT: `entry_mmio_section` covers `0xf9a00000..0xf9afffff` (1 MB, indexed by
     * `va >> 20`), which contains the whole `0xf9a55000` window and no other device this image maps.
     */
    mapped = entry_mmio_section(STAGE90_USB_OTG_BASE, STAGE90_USB_OTG_BASE, &slot_before, &desc);
    USB_LIVE("xnu_live_usb_map", mapped);
    USB_LIVE("xnu_live_usb_slot_before", slot_before);
    USB_LIVE("xnu_live_usb_desc", desc);
    USB_LIVE("xnu_live_usb_ttbr0", g_live_mmio_ttbr0);
    USB_LIVE("xnu_live_usb_ttbr1", g_live_mmio_ttbr1);
    USB_LIVE("xnu_live_usb_l1_moved", g_live_mmio_l1_moved);

    if (mapped == 0u) {
        /* The same refusal shape as the storage pad census: a register read through a section this
         * probe did not install is not a reading, so it is not taken. `_usb_mapped = 0` and no
         * `_usb_*` register cells in the log is the whole of this branch. */
        USB_LIVE("xnu_live_usb_mapped", 0u);
        return;
    }
    USB_LIVE("xnu_live_usb_mapped", 1u);

    /*
     * **(2) THE IDENTIFICATION.** Which core, which PHY, which capability header. `USB_HWGENERAL`'s
     * `PHYW` field (bits 5:4) is the one that decides whether the ULPI viewport read below is a real
     * window: 0 = UTMI+3, 1 = ULPI, 2 = UTMI+2, 3 = serial. The two are published beside each other
     * so "the viewport is dead" and "the viewport is the wrong kind" are different readings.
     */
    id_cap = usb_read32(STAGE90_USB_ID_CAP);
    hwgeneral = usb_read32(STAGE90_USB_HWGENERAL);
    hwdevice = usb_read32(STAGE90_USB_HWDEVICE);
    hwtxbuf = usb_read32(STAGE90_USB_HWTXBUF);
    hwrxbuf = usb_read32(STAGE90_USB_HWRXBUF);
    caplen_hciver = usb_read32(STAGE90_USB_CAPLENGTH_HCIVERSION);
    hccparams = usb_read32(STAGE90_USB_HCCPARAMS);
    dciversion = usb_read32(STAGE90_USB_DCIVERSION);

    g_stage90_usb_id_cap = id_cap;

    USB_LIVE("xnu_live_usb_id_cap", id_cap);
    USB_LIVE("xnu_live_usb_hwgeneral", hwgeneral);
    USB_LIVE("xnu_live_usb_phyw", (hwgeneral >> 4) & 0x3u);
    USB_LIVE("xnu_live_usb_hwdevice", hwdevice);
    USB_LIVE("xnu_live_usb_hwtxbuf", hwtxbuf);
    USB_LIVE("xnu_live_usb_hwrxbuf", hwrxbuf);
    USB_LIVE("xnu_live_usb_caplength", caplen_hciver & 0xffu);
    USB_LIVE("xnu_live_usb_hciversion", (caplen_hciver >> 16) & 0xffffu);
    USB_LIVE("xnu_live_usb_hccparams", hccparams);
    USB_LIVE("xnu_live_usb_dciversion", dciversion & 0xffffu);
    /* The capability length is the offset of the operational block; a core whose `CAPLENGTH` is not
     * the 0x40 the `msm_hsusb_hw.h` offsets assume would put every operational register below at the
     * wrong address. Published as the difference so the assumption is a reading. */
    USB_LIVE("xnu_live_usb_op_block_delta",
             (caplen_hciver & 0xffu) - (STAGE90_USB_USBCMD - 0x100u));

    /*
     * **(3) THE STATE THE BOOTLOADER LEFT.** `USBMODE` first, and it is the decisive cell: the low
     * two bits are the mode the core is in *right now*.
     */
    usbmode = usb_read32(STAGE90_USB_USBMODE);
    g_stage90_usb_usbmode = usbmode;
    usbcmd = usb_read32(STAGE90_USB_USBCMD);
    usbsts = usb_read32(STAGE90_USB_USBSTS);
    usbintr = usb_read32(STAGE90_USB_USBINTR);
    frindex = usb_read32(STAGE90_USB_FRINDEX);
    deviceaddr = usb_read32(STAGE90_USB_DEVICEADDR);
    endpointlistaddr = usb_read32(STAGE90_USB_ENDPOINTLISTADDR);
    burstsize = usb_read32(STAGE90_USB_BURSTSIZE);
    ulpi_viewport = usb_read32(STAGE90_USB_ULPI_VIEWPORT);
    portsc = usb_read32(STAGE90_USB_PORTSC);
    otgsc = usb_read32(STAGE90_USB_OTGSC);

    USB_LIVE("xnu_live_usb_usbmode", usbmode);
    USB_LIVE("xnu_live_usb_usbmode_bits", usbmode & STAGE90_USB_USBMODE_MASK);
    USB_LIVE("xnu_live_usb_usbmode_device", ((usbmode & STAGE90_USB_USBMODE_MASK)
                                             == STAGE90_USB_USBMODE_DEVICE) ? 1u : 0u);
    USB_LIVE("xnu_live_usb_usbmode_host", ((usbmode & STAGE90_USB_USBMODE_MASK)
                                           == STAGE90_USB_USBMODE_HOST) ? 1u : 0u);
    USB_LIVE("xnu_live_usb_usbcmd", usbcmd);
    USB_LIVE("xnu_live_usb_usbcmd_rst", (usbcmd >> 1) & 1u);
    USB_LIVE("xnu_live_usb_usbcmd_rs", usbcmd & 1u);
    USB_LIVE("xnu_live_usb_usbsts", usbsts);
    USB_LIVE("xnu_live_usb_usbintr", usbintr);
    USB_LIVE("xnu_live_usb_frindex", frindex);
    USB_LIVE("xnu_live_usb_deviceaddr", deviceaddr);
    USB_LIVE("xnu_live_usb_endpointlistaddr", endpointlistaddr);
    USB_LIVE("xnu_live_usb_burstsize", burstsize);
    USB_LIVE("xnu_live_usb_ulpi_viewport", ulpi_viewport);
    USB_LIVE("xnu_live_usb_ulpi_sync_state",
             (ulpi_viewport & STAGE90_USB_ULPI_SYNC_STATE) ? 1u : 0u);
    USB_LIVE("xnu_live_usb_portsc", portsc);
    USB_LIVE("xnu_live_usb_portsc_ccs", (portsc & STAGE90_USB_PORTSC_CCS) ? 1u : 0u);
    USB_LIVE("xnu_live_usb_portsc_phcd", (portsc & STAGE90_USB_PORTSC_PHCD) ? 1u : 0u);
    USB_LIVE("xnu_live_usb_otgsc", otgsc);
    USB_LIVE("xnu_live_usb_otgsc_bsv", (otgsc & STAGE90_USB_OTGSC_BSV) ? 1u : 0u);
    USB_LIVE("xnu_live_usb_otgsc_id", (otgsc & STAGE90_USB_OTGSC_ID) ? 1u : 0u);

    /*
     * **(4) THE ENDPOINT TABLE, SHALLOWLY.** Four control words: enough to say whether the core's
     * endpoint table is reachable and whether the bootloader left anything on EP0..EP3. A bootloader
     * that had a gadget configured leaves non-zero low words if anywhere.
     */
    epctrl0 = usb_read32(STAGE90_USB_ENDPTCTRL(0));
    epctrl1 = usb_read32(STAGE90_USB_ENDPTCTRL(1));
    epctrl2 = usb_read32(STAGE90_USB_ENDPTCTRL(2));
    epctrl3 = usb_read32(STAGE90_USB_ENDPTCTRL(3));
    USB_LIVE("xnu_live_usb_epctrl0", epctrl0);
    USB_LIVE("xnu_live_usb_epctrl1", epctrl1);
    USB_LIVE("xnu_live_usb_epctrl2", epctrl2);
    USB_LIVE("xnu_live_usb_epctrl3", epctrl3);
    USB_LIVE("xnu_live_usb_epctrl_any",
             (epctrl0 | epctrl1 | epctrl2 | epctrl3) ? 1u : 0u);

    /*
     * **The USB3 SS controller, identification only.** It is megabyte `0xf92`, a fresh section, and a
     * DWC3 - a *different* register file - so its `ID`/`HWGENERAL` are read only to record which core
     * the bootloader left live; nothing below treats the two as one part. The install is a second
     * `entry_mmio_section` and its four numbers are published under `_usb_ss_*` so they are never read
     * as the OTG core's.
     */
    ss_id = 0u;
    ss_hwgeneral = 0u;
    {
        uint32_t ss_slot_before = 0u, ss_desc = 0u, ss_mapped;
        ss_mapped = entry_mmio_section(STAGE90_USB_SS_BASE, STAGE90_USB_SS_BASE,
                                       &ss_slot_before, &ss_desc);
        USB_LIVE("xnu_live_usb_ss_map", ss_mapped);
        USB_LIVE("xnu_live_usb_ss_slot_before", ss_slot_before);
        USB_LIVE("xnu_live_usb_ss_desc", ss_desc);
        if (ss_mapped != 0u) {
            ss_id = usb_ss_read32(0x000u);        /* DWC3 `GSNPSID` - the Synopsys ID at offset 0. */
            ss_hwgeneral = usb_ss_read32(0x004u); /* DWC3 `GHWPARAMS0` at the same offset. */
        }
    }
    USB_LIVE("xnu_live_usb_ss_id", ss_id);
    USB_LIVE("xnu_live_usb_ss_hwgeneral", ss_hwgeneral);

    USB_LIVE("xnu_live_usb_loaded", 1u);
}

#else /* !STAGE90_XNU_USB_PROBE */

/*
 * The OFF arm: a one-line empty body, the shape `entry_storage_probe`'s own `#else`-less arm has.
 * The call site in `entry_trace.c` is guarded by the same switch, so this body is dead code on a
 * traced build with the switch off - but the build compiles every translation unit's whole `#if`
 * tree, and an object that defined no `entry_usb_probe` at all would be an undefined symbol at the
 * call site the moment the switch flipped on. So the empty definition is the OFF arm, and the
 * `-Werror` build is what proves it is empty.
 */
void entry_usb_probe(void)
{
}

#endif /* STAGE90_XNU_USB_PROBE */