/*
 * 910b (first arm): the enumeration rung's FIRST SLICE - arm EP0 and answer GET_DESCRIPTOR(device,
 * config, string) plus SET_ADDRESS / SET_CONFIGURATION / GET_STATUS, polled once per idle pass.
 *
 * ------------------------------------------------------------------------------------------------
 * Why a first slice, and not the whole rung
 * ------------------------------------------------------------------------------------------------
 *
 * The design (`docs/experiments/experiment-910b-design.md`) designs the whole EP0 control-transfer
 * state machine and the one IN endpoint, and §8 recommends the FIRST arm be the smallest thing a host
 * will accept - the way 910a was a read before 910a2 a write. **This arm is that slice:** it brings the
 * three registers 910a2 deliberately omitted (`USBINTR`, `ENDPOINTLISTADDR`, EP0 `ENDPTCTRL`) and
 * answers the standard requests a host sends during enumeration, but it **does not yet publish a live IN
 * payload** - the one IN endpoint is armed and its descriptor advertised, and the endpoint accepts a
 * read (it returns the arm's magic on the first prime), but a *stream* is 910c's. Descriptor reading and
 * the address/configuration handshake are what `lsusb` measures, and that is the rung's verdict.
 *
 * ------------------------------------------------------------------------------------------------
 * The architectural decision: POLL, do not interrupt
 * ------------------------------------------------------------------------------------------------
 *
 * The vendor's `udc_irq` (`ci13xxx_udc.c:3653`) is an interrupt handler. This image runs no USB
 * interrupt: the USB2 core is not in its vector table and wiring a GIC SPI for it would grow the GIC
 * probe. So `entry_usb_enum_poll` does exactly what `udc_irq` does, minus enable/ack, **once per idle
 * pass** (it is called from `__wrap_Idle_load_context`, the one wrapper every pass reaches - §9.6):
 *
 *     intr = USBSTS & USBINTR;   ... run the vendor's fixed-priority handlers ...   USBSTS = intr;
 *
 * This is honest only because `USBSTS` **latches** (it is R/WC): a transfer that completes between two
 * passes is still set when the next pass reads it, so the poll is not sampling, it is draining. The
 * trade is latency for no interrupt wiring, which is fine for host-paced control transfers (ms
 * timeouts) and is a 910c question for bulk throughput. `USBINTR` is written `0x147` not to fire an
 * interrupt but to make `USBSTS & USBINTR` the right mask - the vendor's enabled set, which excludes
 * `USBi_NAKI` (bit 16) that the vendor never enables or handles (§10.1).
 *
 * ------------------------------------------------------------------------------------------------
 * The static graph, and the cache direction
 * ------------------------------------------------------------------------------------------------
 *
 * The entry image is identity-mapped (`physBase == virtBase == 0x80000000`), so a static, aligned array
 * in `.bss` **is directly DMA-able by the core** - the vendor's whole dma_pool/kzalloc graph collapses
 * to the arrays below (§2 of the design). The core indexes endpoint N at `base + N*64`, so the base is
 * `aligned(64)`. Two cache operations and their directions, stated because a first attempt gets the
 * direction wrong ([[mi4-idle-exit-l2-line]] is the same class one cache level off):
 *
 *   CPU -> core (the qh/dTD the CPU writes before priming): **clean** with `CleanPoU_DcacheRegion`, so
 *   the core - which reads DRAM, bypassing the cache - sees the CPU's bytes. `CleanPoU_DcacheRegion` is
 *   the symbol this image already links (`osfmk/arm/caches.c` uses it around page-table edits).
 *
 *   core -> CPU (the SETUP bytes the core DMA'd, and a dTD `token` the core updated): **invalidate**, so
 *   a stale cached line does not shadow the core's write. This arm uses `FlushPoU_Dcache` (clean+invalidate);
 *   it is sound here because the CPU never *writes* the core-written region, so there is no dirty line
 *   for the clean half to write back over the core's data - the two are equivalent when the CPU's line
 *   is clean. A targeted `invalidate` is a later refinement.
 *
 * ------------------------------------------------------------------------------------------------
 * The hazard, and the gate 910a2 set
 * ------------------------------------------------------------------------------------------------
 *
 * This arm writes the endpoint set, the interrupt mask, and `DEVICEADDR`; it **never writes
 * `USBCMD.RST`** (910a2 owns the one link reset; a re-reset in the poll would fight the running device -
 * a build refusal, not a comment, guards this). And it writes nothing at all unless the core is ALREADY
 * in device mode (`USBMODE[1:0] == 2`), the 910a2 rule: a core the bootloader left in another mode is one
 * this arm does not understand, and it refuses. `STAGE90_XNU_USB_DEV_FORCE=1` relaxes that gate, and this
 * arm honours it the same way 910a2 does.
 */
#include <stdint.h>

#include "entry_usb_enum.h"

#ifndef STAGE90_XNU_USB_ENUM
#define STAGE90_XNU_USB_ENUM 0
#endif
#ifndef STAGE90_XNU_USB_DEV_FORCE
#define STAGE90_XNU_USB_DEV_FORCE 0
#endif

extern void entry_live_write(const char *key, uint32_t value);
/* The two cache primitives, and their prototype home `osfmk/arm/caches_internal.h`.
 *
 * **The core->CPU direction is `FlushPoC_DcacheRegion`, NOT `FlushPoU_Dcache`, and that is the one
 * deliberate change from the design.** The seam arm (`STAGE90_XNU_SEAM_POC`) puts
 * `--wrap=FlushPoU_Dcache` in the link, so EVERY call to `FlushPoU_Dcache` in the image is redirected
 * to `__wrap_FlushPoU_Dcache` - and that wrapper's identification is written against exactly the
 * kernel's four call sites (cache_xcall, the enter's else arm, the exit's own, cache_xcall_handler); a
 * fifth caller from this file is a build refusal. `FlushPoC_DcacheRegion` is the same clean-and-
 * invalidate operation aimed at the Point of Coherency instead of Unification, which is where the USB
 * core's DMA writes land, so it is the *correct* target here as well as the one the seam does not
 * wrap. `CleanPoU_DcacheRegion` (the CPU->core clean) is not wrapped and stays. */
extern void CleanPoU_DcacheRegion(uintptr_t va, unsigned length);
extern void FlushPoC_DcacheRegion(uintptr_t va, unsigned length);

#if STAGE90_XNU_USB_ENUM

#define USB_ENUM_LIVE(key, value) entry_live_write((key), (uint32_t)(value))

static uint32_t usb_enum_read32(uint32_t off)
{
    return *(volatile uint32_t *)(uintptr_t)(STAGE90_USB_OTG_BASE + off);
}

static void usb_enum_write32(uint32_t off, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)(STAGE90_USB_OTG_BASE + off) = value;
}

/* ------------------------------------------------------------------------------------------------
 * The static graph: the qh array, one dTD per slot, and the buffers. All 64-byte aligned (the hardware
 * stride), the buffers 4 KiB aligned so a dTD's `page[i]` low 12 bits are a well-defined page offset.
 * ------------------------------------------------------------------------------------------------
 */
static uint8_t g_usb_qh[STAGE90_USB_ENUM_QH_STRIDE * STAGE90_USB_ENUM_QH_COUNT]
    __attribute__((aligned(STAGE90_USB_ENUM_QH_STRIDE)));
static uint8_t g_usb_td[STAGE90_USB_ENUM_TD_STRIDE * 4]
    __attribute__((aligned(STAGE90_USB_ENUM_TD_STRIDE)));
static uint8_t g_usb_in_buf[64]     __attribute__((aligned(64)));

#define USB_ENUM_TD_OUT0 0u
#define USB_ENUM_TD_IN0  1u
#define USB_ENUM_TD_IN1  2u

static uint32_t g_usb_enum_state;      /* 0 idle, 1 armed, ... a small published enum. */
static uint32_t g_usb_enum_done;       /* the one-shot: arm once, then only poll.       */
static uint32_t g_usb_enum_addr;
static uint32_t g_usb_enum_configured;
static uint32_t g_usb_enum_requests;
static uint32_t g_usb_enum_resets;
static uint32_t g_usb_enum_in_reads;

/* The last SETUP packet, and a small enum naming the state the poll last reached. Published so a press
 * is a reading, not a silence ([[mi4-silence-is-a-reading-only-if-success-is-silent]]). State names:
 *   0 idle (not armed)   1 armed (EP0 waiting)   2 setup-read   3 data-primed
 *   4 status-primed      5 addressed             6 configured                          */
#define USB_ENUM_ST_IDLE     0u
#define USB_ENUM_ST_ARMED    1u
#define USB_ENUM_ST_SETUP    2u
#define USB_ENUM_ST_DATA     3u
#define USB_ENUM_ST_STATUS   4u
#define USB_ENUM_ST_ADDRESS  5u
#define USB_ENUM_ST_CONFIG   6u

/* ------------------------------------------------------------------------------------------------
 * The qh / dTD word accessors, by qh index and offset. `g_usb_qh` is 64-byte strided.
 * ------------------------------------------------------------------------------------------------
 */
static volatile uint32_t *usb_enum_qh(uint32_t idx, uint32_t off)
{
    return (volatile uint32_t *)(uintptr_t)((uintptr_t)g_usb_qh
                                            + (uintptr_t)idx * STAGE90_USB_ENUM_QH_STRIDE + off);
}

static volatile uint32_t *usb_enum_td(uint32_t idx, uint32_t off)
{
    return (volatile uint32_t *)(uintptr_t)((uintptr_t)g_usb_td
                                            + (uintptr_t)idx * STAGE90_USB_ENUM_TD_STRIDE + off);
}

/* A physical(=virtual) address as a `uint32_t`, for the DMA registers and the dTD `page[]`. */
static uint32_t usb_enum_pa(const void *p)
{
    return (uint32_t)(uintptr_t)p;
}

/* ------------------------------------------------------------------------------------------------
 * The descriptor tables - the vendor's values (design §5), with the arm's named divergences.
 * ------------------------------------------------------------------------------------------------
 */
static const uint8_t g_usb_dev_desc[18] = {
    18u, STAGE90_USB_ENUM_DESC_DEVICE,
    0x00u, 0x02u,                                  /* bcdUSB 0x0200, LE.                       */
    0x00u, 0x00u, 0x00u,                           /* class / subclass / protocol.             */
    STAGE90_USB_ENUM_MAX_PACKET0,                  /* 64 - must equal the EP0 qh maxpacket.    */
    (uint8_t)(STAGE90_USB_ENUM_ID_VENDOR & 0xffu), (uint8_t)(STAGE90_USB_ENUM_ID_VENDOR >> 8),
    (uint8_t)(STAGE90_USB_ENUM_ID_PRODUCT & 0xffu), (uint8_t)(STAGE90_USB_ENUM_ID_PRODUCT >> 8),
    (uint8_t)(STAGE90_USB_ENUM_BCD_DEVICE & 0xffu), (uint8_t)(STAGE90_USB_ENUM_BCD_DEVICE >> 8),
    STAGE90_USB_ENUM_STR_MANUFACTURER,
    STAGE90_USB_ENUM_STR_PRODUCT,
    STAGE90_USB_ENUM_STR_SERIAL,
    1u,                                            /* bNumConfigurations.                      */
};

static const uint8_t g_usb_cfg_desc[25] = {
    /* config (9) */
    9u, STAGE90_USB_ENUM_DESC_CONFIG,
    25u, 0x00u,                                    /* wTotalLength = 9+9+7 = 25.               */
    1u,                                            /* bNumInterfaces.                          */
    STAGE90_USB_ENUM_CONFIG_VALUE,
    0u,                                            /* iConfiguration.                          */
    0x80u,                                         /* bmAttributes: bus-powered, bit 7.        */
    STAGE90_USB_ENUM_MAX_POWER_MA,
    /* interface (9) */
    9u, 4u,                                        /* USB_DT_INTERFACE.                        */
    0u, 0u, 1u,                                    /* number, alt, bNumEndpoints = 1.          */
    STAGE90_USB_ENUM_IFACE_CLASS, STAGE90_USB_ENUM_IFACE_SUBCLASS, STAGE90_USB_ENUM_IFACE_PROTOCOL,
    0u,                                            /* iInterface.                              */
    /* endpoint (7) */
    7u, 5u,                                        /* USB_DT_ENDPOINT.                         */
    STAGE90_USB_ENUM_EP_IN_EPADDR,
    STAGE90_USB_ENUM_EP_IN_ATTR,
    (uint8_t)(STAGE90_USB_ENUM_EP_IN_MAXPKT & 0xffu), (uint8_t)(STAGE90_USB_ENUM_EP_IN_MAXPKT >> 8),
    0u,                                            /* bInterval.                               */
};

/* The string sources, one per index above. `bLength` is computed by the builder. */
static const char g_usb_str_manufacturer[] = "mi4-xnu";
static const char g_usb_str_product[]      = "xnu-arm-entry";
static const char g_usb_str_serial[]       = "4a2fe00b";

/* Build a USB string descriptor (UTF-16LE) into `buf`, return its length. `s` is ASCII. */
static uint32_t usb_enum_build_string(uint8_t *buf, const char *s)
{
    uint32_t n = 0u, i = 0u;
    while (s[n] != '\0' && n < 31u)
        n++;
    buf[0] = (uint8_t)(2u + 2u * n);
    buf[1] = STAGE90_USB_ENUM_DESC_STRING;
    for (i = 0u; i < n; i++) {
        buf[2u + 2u * i] = (uint8_t)s[i];
        buf[3u + 2u * i] = 0u;
    }
    return 2u + 2u * n;
}

/* The LANGID descriptor (string index 0): 0x0409 = US English. */
static uint32_t usb_enum_build_langid(uint8_t *buf)
{
    buf[0] = 4u;
    buf[1] = STAGE90_USB_ENUM_DESC_STRING;
    buf[2] = 0x09u;
    buf[3] = 0x04u;
    return 4u;
}

/* ------------------------------------------------------------------------------------------------
 * Priming one endpoint with one dTD.
 *
 * `qh_idx` names the queue head; `td_idx` the dTD slot; `buf` the data (0 and 0 for a zero-length
 * status packet); `len` the byte count; `in_dir` the direction (1 IN / 0 OUT). The CPU writes the qh's
 * cap and the embedded td link, cleans them, then primes; the core reads DRAM.
 * ------------------------------------------------------------------------------------------------
 */
static void usb_enum_prime(uint32_t qh_idx, uint32_t td_idx, const void *buf, uint32_t len,
                           uint32_t in_dir)
{
    volatile uint32_t *td = usb_enum_td(td_idx, 0u);
    volatile uint32_t *qh = usb_enum_qh(qh_idx, 0u);
    uint32_t token, cap, n;

    token = (len & 0x7fffu) << STAGE90_USB_ENUM_TD_TOTAL_SHIFT;
    token |= STAGE90_USB_ENUM_TD_ACTIVE | STAGE90_USB_ENUM_TD_IOC;

    td[STAGE90_USB_ENUM_TD_OFF_NEXT / 4u]  = STAGE90_USB_ENUM_TD_TERMINATE;
    td[STAGE90_USB_ENUM_TD_OFF_TOKEN / 4u] = token;
    td[STAGE90_USB_ENUM_TD_OFF_PAGE0 / 4u] = (buf != 0) ? usb_enum_pa(buf) : 0u;

    /* clean the dTD the core will read, and the buffer's first page (its page[] offset is in it). */
    CleanPoU_DcacheRegion((uintptr_t)td, STAGE90_USB_ENUM_TD_STRIDE);
    if (buf != 0 && len != 0u)
        for (n = 0u; n < len; n += 4096u)
            CleanPoU_DcacheRegion((uintptr_t)((const uint8_t *)buf + n), 4096u);

    /* the qh's cap (once) and the embedded link to the dTD. */
    cap = (qh_idx == STAGE90_USB_ENUM_QH_IN1) ? (STAGE90_USB_ENUM_QH_ZLT
                                                 | STAGE90_USB_ENUM_QH_MAX_PKT(STAGE90_USB_ENUM_EP_IN_MAXPKT))
                                              : (STAGE90_USB_ENUM_QH_IOS
                                                 | STAGE90_USB_ENUM_QH_MAX_PKT(STAGE90_USB_ENUM_MAX_PACKET0));
    qh[STAGE90_USB_ENUM_QH_OFF_CAP / 4u]    = cap;
    qh[STAGE90_USB_ENUM_QH_OFF_TDNEXT / 4u] = (uint32_t)(uintptr_t)td & ~0x1fu;
    qh[STAGE90_USB_ENUM_QH_OFF_TDTOK / 4u]  = 0u;
    CleanPoU_DcacheRegion((uintptr_t)qh, STAGE90_USB_ENUM_QH_STRIDE);

    usb_enum_write32(STAGE90_USB_ENDPTPRIME,
                     STAGE90_USB_ENUM_EPBIT(qh_idx == STAGE90_USB_ENUM_QH_IN1
                                                ? STAGE90_USB_ENUM_EP_IN : 0u,
                                            in_dir));
}

/* Clear an endpoint's completion bit (R/WC). */
static void usb_enum_complete_clear(uint32_t bit)
{
    usb_enum_write32(STAGE90_USB_ENDPTCOMPLETE, bit);
}

/* ------------------------------------------------------------------------------------------------
 * The setup read, under the vendor's `USBCMD.SUTW` guard (`ci13xxx_udc.c:2706`). The core DMA'd 8 bytes
 * into `qh[OUT0].setup`; invalidate the line so a stale cached copy does not shadow it, set SUTW, copy,
 * clear SUTW, retry if the core cleared SUTW mid-read (a newer setup raced us).
 * ------------------------------------------------------------------------------------------------
 */
static void usb_enum_read_setup(uint8_t out[8])
{
    volatile uint32_t *qh = usb_enum_qh(STAGE90_USB_ENUM_QH_OUT0, 0u);
    uint32_t cmd, i;

    for (i = 0u; i < 8u; i++)
        out[i] = 0u;

    for (;;) {
        /* Invalidate the qh's setup region so a stale cached line does not shadow the core's DMA. */
        FlushPoC_DcacheRegion((uintptr_t)qh + STAGE90_USB_ENUM_QH_OFF_SETUP, 8u);
        cmd = usb_enum_read32(STAGE90_USB_USBCMD);
        usb_enum_write32(STAGE90_USB_USBCMD, cmd | STAGE90_USB_ENUM_USBCMD_SUTW);
        {
            uint32_t w;
            for (w = 0u; w < 2u; w++)
                ((volatile uint32_t *)out)[w] =
                    qh[STAGE90_USB_ENUM_QH_OFF_SETUP / 4u + w];
        }
        if ((usb_enum_read32(STAGE90_USB_USBCMD) & STAGE90_USB_ENUM_USBCMD_SUTW) != 0u) {
            usb_enum_write32(STAGE90_USB_USBCMD, cmd);
            return;
        }
        /* SUTW was cleared by the core: a new setup arrived; loop and re-read. */
    }
}

/* Zero a qh's embedded td link and its dTD (the minimal `_ep_nuke`). */
static void usb_enum_nuke(uint32_t qh_idx)
{
    volatile uint32_t *qh = usb_enum_qh(qh_idx, 0u);
    qh[STAGE90_USB_ENUM_QH_OFF_TDNEXT / 4u] = STAGE90_USB_ENUM_TD_TERMINATE;
    qh[STAGE90_USB_ENUM_QH_OFF_TDTOK / 4u]  = 0u;
    CleanPoU_DcacheRegion((uintptr_t)qh, STAGE90_USB_ENUM_QH_STRIDE);
}

/* ------------------------------------------------------------------------------------------------
 * Arm the EP0 control path: the three registers 910a2 omitted, plus EP0's qh (index 0).
 * ------------------------------------------------------------------------------------------------
 */
static void usb_enum_arm_ep0(void)
{
    /* EP0: control type, both directions enabled, toggle reset, unstalled. */
    usb_enum_write32(STAGE90_USB_ENDPTCTRL(0u), STAGE90_USB_ENUM_ENDPTCTRL0_VALUE);
    /* the qh array base - the physical address of g_usb_qh (identity-mapped). */
    usb_enum_write32(STAGE90_USB_ENDPOINTLISTADDR, usb_enum_pa(g_usb_qh));
    /* the endpoint prime/complete/setup semaphores start clear. */
    usb_enum_write32(STAGE90_USB_ENDPTFLUSH, 0xffffffffu);
    usb_enum_write32(STAGE90_USB_ENDPTCOMPLETE, 0xffffffffu);
    usb_enum_write32(STAGE90_USB_ENDPTSETUPSTAT, 0xffffffffu);
    /* EP0's qh: cap = IOS | maxpacket(64); the embedded td linked to terminate. */
    usb_enum_nuke(STAGE90_USB_ENUM_QH_OUT0);
    usb_enum_nuke(STAGE90_USB_ENUM_QH_IN0);
    /* the interrupt mask: not to fire an interrupt, but to make USBSTS & USBINTR the vendor's set. */
    usb_enum_write32(STAGE90_USB_USBINTR, STAGE90_USB_ENUM_INTR_VALUE);
}

/* Arm the one IN endpoint (EP1, bulk, 64-byte FS). */
static void usb_enum_arm_ep_in(void)
{
    usb_enum_write32(STAGE90_USB_ENDPTCTRL(STAGE90_USB_ENUM_EP_IN),
                     STAGE90_USB_ENUM_ENDPTCTRL1_VALUE);
    usb_enum_nuke(STAGE90_USB_ENUM_QH_IN1);
    g_usb_in_buf[0] = (uint8_t)(STAGE90_USB_ENUM_IN_MAGIC & 0xffu);
    g_usb_in_buf[1] = (uint8_t)((STAGE90_USB_ENUM_IN_MAGIC >> 8) & 0xffu);
    g_usb_in_buf[2] = (uint8_t)((STAGE90_USB_ENUM_IN_MAGIC >> 16) & 0xffu);
    g_usb_in_buf[3] = (uint8_t)((STAGE90_USB_ENUM_IN_MAGIC >> 24) & 0xffu);
    usb_enum_prime(STAGE90_USB_ENUM_QH_IN1, USB_ENUM_TD_IN1, g_usb_in_buf, 4u, 1u);
}

/* ------------------------------------------------------------------------------------------------
 * The bus-reset handler - MANDATORY (`ci13xxx_udc.c:835`). The host re-enumerates every plug-in;
 * skipping this wedges EP0 on the second plug, which reads as "the port worked once".
 * ------------------------------------------------------------------------------------------------
 */
static void usb_enum_bus_reset(void)
{
    usb_enum_write32(STAGE90_USB_DEVICEADDR, 0u);
    usb_enum_write32(STAGE90_USB_ENDPTFLUSH, 0xffffffffu);
    usb_enum_write32(STAGE90_USB_ENDPTSETUPSTAT, 0xffffffffu);
    usb_enum_write32(STAGE90_USB_ENDPTCOMPLETE, 0xffffffffu);
    g_usb_enum_addr = 0u;
    g_usb_enum_configured = 0u;
    g_usb_enum_resets++;
    g_usb_enum_state = USB_ENUM_ST_ARMED;
}

/* Build the response for a GET_DESCRIPTOR and prime EP0-IN. Returns the count primed. */
static uint32_t usb_enum_get_descriptor(const uint8_t setup[8])
{
    uint32_t wvalue = (uint32_t)setup[2] | ((uint32_t)setup[3] << 8);
    uint32_t wlength = (uint32_t)setup[6] | ((uint32_t)setup[7] << 8);
    uint32_t type = (wvalue >> 8) & 0xffu;
    uint32_t index = wvalue & 0xffu;
    const uint8_t *desc = 0;
    uint32_t alen = 0u, len;

    if (type == STAGE90_USB_ENUM_DESC_DEVICE) {
        desc = g_usb_dev_desc;
        alen = sizeof(g_usb_dev_desc);
    } else if (type == STAGE90_USB_ENUM_DESC_CONFIG) {
        desc = g_usb_cfg_desc;
        alen = sizeof(g_usb_cfg_desc);
    } else if (type == STAGE90_USB_ENUM_DESC_STRING) {
        if (index == 0u)
            alen = usb_enum_build_langid(g_usb_in_buf);
        else if (index == STAGE90_USB_ENUM_STR_MANUFACTURER)
            alen = usb_enum_build_string(g_usb_in_buf, g_usb_str_manufacturer);
        else if (index == STAGE90_USB_ENUM_STR_PRODUCT)
            alen = usb_enum_build_string(g_usb_in_buf, g_usb_str_product);
        else if (index == STAGE90_USB_ENUM_STR_SERIAL)
            alen = usb_enum_build_string(g_usb_in_buf, g_usb_str_serial);
        if (alen != 0u)
            desc = g_usb_in_buf;
        else
            return 0u;   /* unknown string: no data; caller sends a status-only response. */
    } else {
        return 0u;
    }

    len = (wlength < alen) ? wlength : alen;
    usb_enum_prime(STAGE90_USB_ENUM_QH_IN0, USB_ENUM_TD_IN0, desc, len, 1u);
    return len;
}

/* ------------------------------------------------------------------------------------------------
 * The transaction/setup handler - the body of `udc_irq`'s `USBi_UI` case (`ci13xxx_udc.c:2651`).
 * ------------------------------------------------------------------------------------------------
 */
static void usb_enum_ui(void)
{
    uint8_t setup[8];
    uint32_t bRequest, bRequestType, wValue, wLength;

    if ((usb_enum_read32(STAGE90_USB_ENDPTSETUPSTAT) & 0x1u) != 0u) {
        usb_enum_read_setup(setup);
        usb_enum_write32(STAGE90_USB_ENDPTSETUPSTAT, 0x1u);   /* R/WC: clear the token. */
        usb_enum_nuke(STAGE90_USB_ENUM_QH_OUT0);
        usb_enum_nuke(STAGE90_USB_ENUM_QH_IN0);
        g_usb_enum_requests++;
        g_usb_enum_state = USB_ENUM_ST_SETUP;

        bRequestType = setup[0];
        bRequest = setup[1];
        wValue = (uint32_t)setup[2] | ((uint32_t)setup[3] << 8);
        wLength = (uint32_t)setup[6] | ((uint32_t)setup[7] << 8);
        (void)wValue;

        if ((bRequestType & STAGE90_USB_ENUM_TYPE_MASK) == STAGE90_USB_ENUM_TYPE_STANDARD) {
            if (bRequest == STAGE90_USB_ENUM_REQ_GET_DESCRIPTOR && (bRequestType & STAGE90_USB_ENUM_DIR_IN)) {
                if (usb_enum_get_descriptor(setup) != 0u) {
                    g_usb_enum_state = USB_ENUM_ST_DATA;
                    return;
                }
                /* no data: fall through to a status-only response */
            } else if (bRequest == STAGE90_USB_ENUM_REQ_SET_ADDRESS) {
                g_usb_enum_addr = wValue & 0x7fu;
                usb_enum_write32(STAGE90_USB_DEVICEADDR,
                                 (g_usb_enum_addr << STAGE90_USB_ENUM_DEVICEADDR_ADDR_SHIFT)
                                 | STAGE90_USB_ENUM_DEVICEADDR_USBADRA);
                g_usb_enum_state = USB_ENUM_ST_ADDRESS;
            } else if (bRequest == STAGE90_USB_ENUM_REQ_GET_STATUS && (bRequestType & STAGE90_USB_ENUM_DIR_IN)) {
                g_usb_in_buf[0] = 0u;   /* not self-powered, no remote wakeup - the honest value */
                g_usb_in_buf[1] = 0u;
                usb_enum_prime(STAGE90_USB_ENUM_QH_IN0, USB_ENUM_TD_IN0, g_usb_in_buf,
                               (wLength < 2u) ? wLength : 2u, 1u);
                g_usb_enum_state = USB_ENUM_ST_DATA;
                return;
            } else if (bRequest == STAGE90_USB_ENUM_REQ_SET_CONFIGURATION) {
                g_usb_enum_configured = (wValue != 0u) ? 1u : 0u;
                if (g_usb_enum_configured)
                    usb_enum_arm_ep_in();
                g_usb_enum_state = USB_ENUM_ST_CONFIG;
            }
            /* GET_CONFIGURATION / CLEAR_FEATURE / SET_FEATURE: status-only. */
        }

        /* The status phase for a no-data request goes IN (ep0in), zero length, and is the end of it. */
        usb_enum_prime(STAGE90_USB_ENUM_QH_IN0, USB_ENUM_TD_IN0, 0, 0u, 1u);
        g_usb_enum_state = USB_ENUM_ST_STATUS;
        return;
    }

    /* EP0-IN completion: the data stage (or a status) finished; if data was sent, send the OUT status. */
    if ((usb_enum_read32(STAGE90_USB_ENDPTCOMPLETE) & STAGE90_USB_ENUM_EPBIT(0u, 1u)) != 0u) {
        usb_enum_complete_clear(STAGE90_USB_ENUM_EPBIT(0u, 1u));
        if (g_usb_enum_state == USB_ENUM_ST_DATA) {
            usb_enum_prime(STAGE90_USB_ENUM_QH_OUT0, USB_ENUM_TD_OUT0, 0, 0u, 0u);
            g_usb_enum_state = USB_ENUM_ST_STATUS;
        } else {
            g_usb_enum_state = USB_ENUM_ST_ARMED;
        }
    }

    /* EP0-OUT completion: the host read our data and acked the status. */
    if ((usb_enum_read32(STAGE90_USB_ENDPTCOMPLETE) & STAGE90_USB_ENUM_EPBIT(0u, 0u)) != 0u) {
        usb_enum_complete_clear(STAGE90_USB_ENUM_EPBIT(0u, 0u));
        g_usb_enum_state = USB_ENUM_ST_ARMED;
    }

    /* EP1-IN completion: the host read the IN endpoint's payload; re-prime (the magic again). */
    if ((usb_enum_read32(STAGE90_USB_ENDPTCOMPLETE) & STAGE90_USB_ENUM_EPBIT(STAGE90_USB_ENUM_EP_IN, 1u)) != 0u) {
        usb_enum_complete_clear(STAGE90_USB_ENUM_EPBIT(STAGE90_USB_ENUM_EP_IN, 1u));
        g_usb_enum_in_reads++;
        usb_enum_prime(STAGE90_USB_ENUM_QH_IN1, USB_ENUM_TD_IN1, g_usb_in_buf, 4u, 1u);
    }
}

/* ------------------------------------------------------------------------------------------------
 * The poll - the one entry point, called once per idle pass from `__wrap_Idle_load_context`.
 * ------------------------------------------------------------------------------------------------
 */
void entry_usb_enum_poll(void)
{
    uint32_t usbmode, intr;

    /* The gate: never write unless the core is already a device (the 910a2 rule), unless FORCE. */
    usbmode = usb_enum_read32(STAGE90_USB_USBMODE);
    if (((usbmode & STAGE90_USB_USBMODE_CM) != STAGE90_USB_USBMODE_CM_DEVICE)
        && (STAGE90_XNU_USB_DEV_FORCE == 0)) {
        if (g_usb_enum_done == 0u) {
            USB_ENUM_LIVE("xnu_live_usb_enum_gated", 1u);
        }
        return;
    }

    if (g_usb_enum_done == 0u) {
        g_usb_enum_done = 1u;
        usb_enum_arm_ep0();
        g_usb_enum_state = USB_ENUM_ST_ARMED;
        USB_ENUM_LIVE("xnu_live_usb_enum_armed", 1u);
        USB_ENUM_LIVE("xnu_live_usb_enum_intr_wrote", STAGE90_USB_ENUM_INTR_VALUE);
        USB_ENUM_LIVE("xnu_live_usb_enum_eplist", usb_enum_pa(g_usb_qh));
        USB_ENUM_LIVE("xnu_live_usb_enum_ep0_ctrl", usb_enum_read32(STAGE90_USB_ENDPTCTRL(0u)));
    }

    intr = usb_enum_read32(STAGE90_USB_USBSTS) & usb_enum_read32(STAGE90_USB_USBINTR);

    /* fixed priority, the vendor's order (ci13xxx_udc.c:3681): URI, PCI, UEI, UI, SLI. */
    if ((intr & STAGE90_USB_ENUM_STS_URI) != 0u)
        usb_enum_bus_reset();
    if ((intr & STAGE90_USB_ENUM_STS_UI) != 0u)
        usb_enum_ui();
    /* PCI (port change) and SLI (suspend) need no action for a minimal port; UEI is a count. */

    /* R/WC: write the enabled-and-latched set back to clear exactly those bits. */
    usb_enum_write32(STAGE90_USB_USBSTS, intr);

    USB_ENUM_LIVE("xnu_live_usb_enum_state", g_usb_enum_state);
    USB_ENUM_LIVE("xnu_live_usb_enum_requests", g_usb_enum_requests);
    USB_ENUM_LIVE("xnu_live_usb_enum_resets", g_usb_enum_resets);
    USB_ENUM_LIVE("xnu_live_usb_enum_addr", g_usb_enum_addr);
    USB_ENUM_LIVE("xnu_live_usb_enum_configured", g_usb_enum_configured);
    USB_ENUM_LIVE("xnu_live_usb_enum_in_reads", g_usb_enum_in_reads);
}

#else /* !STAGE90_XNU_USB_ENUM */

void entry_usb_enum_poll(void)
{
}

#endif /* STAGE90_XNU_USB_ENUM */