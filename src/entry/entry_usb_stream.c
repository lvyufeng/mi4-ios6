/*
 * 910c (first arm): the bulk STREAM - the one IN endpoint's payload becomes a cursor over the RAM
 * console's key/value ring, instead of the fixed 4-byte magic 910b sent forever.
 *
 * ------------------------------------------------------------------------------------------------
 * What this changes, and what it deliberately does not
 * ------------------------------------------------------------------------------------------------
 *
 * 910b (`entry_usb_enum.c`) arms EP0 and answers enumeration, and arms one bulk IN endpoint (EP1) whose
 * payload is, on every completion, the same `STAGE90_USB_ENUM_IN_MAGIC` word (`entry_usb_enum.c:490`).
 * This arm makes that endpoint a **stream**: it takes its bytes from the RAM console ring the live
 * channel itself publishes into, so a host `read()` receives the boot's own `xnu_live_*` lines - a verdict
 * measured from OUTSIDE the phone, self-describing, which is the rung's whole point.
 *
 * It is a SEPARATE file from the enum arm for 910b's own reason: **the properties of one arm must be
 * properties of one file.** `entry_usb_enum.c` is provably "answers control, sends a fixed magic"; this
 * file is "moves a variable-length run with a cursor and back-pressure". The enum guard's clause ("EP1-IN
 * sends only the magic") stays true and checkable, and this arm's clause ("EP1-IN sends `[cursor,
 * cursor+len)` bounded by the ring's size") is a separate assertion over a separate file.
 *
 * **THE ENDPOINT IS HANDED OVER, NOT SHARED.** When this switch is ON the enum arm arms EP1's
 * endpoint-control register (enable) but does NOT prime it and does NOT re-prime it on completion
 * (`entry_usb_enum.c` guards both with `#if !STAGE90_XNU_USB_STREAM`), so EP1-IN has exactly ONE owner of
 * its qh and dTD at any time: the enum arm at arm time, this arm from the first stream prime on. A second
 * writer of the same qh would be the exact defect [[mi4-one-value-two-definitions]] names.
 *
 * ------------------------------------------------------------------------------------------------
 * The three stream changes (design §3)
 * ------------------------------------------------------------------------------------------------
 *
 * 1. **A page-aligned buffer, and the dTD `page[0]` beyond a constant.** `g_usb_stream_buf[4096]` is
 *    `aligned(4096)`, so `page[0]`'s low 12 bits are a well-defined in-page offset and one dTD - never a
 *    `page[1..4]` chain - covers a chunk of up to one page (the map §2.1 20 KiB spread is a later rung).
 * 2. **A cursor-driven re-prime, reading the dTD `token`'s ACTUAL count.** The vendor reads the arrived
 *    byte count from the dTD `token` - `actual = len - ((token & TD_TOTAL_BYTES) >> 16)` - and NEVER from
 *    `qh.curr` (`ci13xxx_udc.c:2176-2178`). So on completion this arm invalidates the dTD line, checks
 *    `TD_STATUS_ACTIVE` is clear, reads the count, and **advances the source cursor by `actual`** (the
 *    bytes the host accepted), not by the length it offered.
 * 3. **Back-pressure = leave `BIT(17)` unset.** The vendor never enables `USBi_NAKI`, so an unprimed
 *    endpoint makes the core NAK the host automatically - there is no register to write. The arm primes
 *    only when the source has bytes past the cursor, and never overwrites the buffer while the current
 *    dTD is still ACTIVE (writing the next chunk before the core finished DMA'ing the previous one
 *    silently corrupts both).
 *
 * ------------------------------------------------------------------------------------------------
 * The cache direction (the same discipline 910b paid)
 * ------------------------------------------------------------------------------------------------
 *
 * CPU -> core (the buffer, the dTD): **clean** with `CleanPoU_DcacheRegion` - the core reads DRAM.
 * core -> CPU (the dTD `token` the core updated): **invalidate** with `FlushPoC_DcacheRegion` - the same
 * PoC operation 910b uses for the SETUP bytes (`entry_usb_enum.c:300`), and NOT `FlushPoU_Dcache`, which
 * the seam wraps (910b design §9.b). The RAM console is read with the CPU *and* written by the CPU-side
 * `entry_write_kv`, so no invalidate is needed for it: the CPU's own view is coherent.
 *
 * ------------------------------------------------------------------------------------------------
 * The gate
 * ------------------------------------------------------------------------------------------------
 *
 * Writes nothing unless the core is ALREADY a device (`USBMODE[1:0] == 2`, the 910a2 rule) AND the enum arm
 * has already enabled EP1 (its `ENDPTCTRL` TXE bit is set - the endpoint the enum arm enables on
 * `SET_CONFIGURATION`). Even with this switch ON and the enum arm OFF, this arm does not enable the
 * endpoint itself: it refuses and publishes why once, because enabling it here would be a second, silent
 * definition of "who owns EP1-IN". A build refusal in `build_entry.sh` stops the two switches being set
 * in an order that could leave the endpoint armed twice or not at all.
 */
#include <stdint.h>

#include "entry_usb_stream.h"

#ifndef STAGE90_XNU_USB_STREAM
#define STAGE90_XNU_USB_STREAM 0
#endif

extern void entry_live_write(const char *key, uint32_t value);
extern void CleanPoU_DcacheRegion(uintptr_t va, unsigned length);
extern void FlushPoC_DcacheRegion(uintptr_t va, unsigned length);

#if STAGE90_XNU_USB_STREAM

#define USB_STREAM_LIVE(key, value) entry_live_write((key), (uint32_t)(value))

static uint32_t usb_stream_read32(uint32_t off)
{
    return *(volatile uint32_t *)(uintptr_t)(STAGE90_USB_OTG_BASE + off);
}

static void usb_stream_write32(uint32_t off, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)(STAGE90_USB_OTG_BASE + off) = value;
}

/* ------------------------------------------------------------------------------------------------
 * The graph: one page-aligned buffer and one 32-byte-aligned dTD, both this arm's own. The qh is the
 * enum arm's array (the hardware has one endpoint list); the enum arm exports `entry_usb_enum_qh_in1()`
 * and this arm is its sole writer while the switch is on.
 * ------------------------------------------------------------------------------------------------
 */
static uint8_t g_usb_stream_buf[STAGE90_USB_STREAM_CHUNK]
    __attribute__((aligned(STAGE90_USB_STREAM_CHUNK)));
static uint8_t g_usb_stream_td[STAGE90_USB_ENUM_TD_STRIDE] __attribute__((aligned(32)));

static uint32_t g_usb_stream_cursor;   /* bytes already handed to the core.                     */
static uint32_t g_usb_stream_last;     /* bytes in the current chunk (offered to the core).     */
static uint32_t g_usb_stream_active;   /* 1 while a chunk's dTD is outstanding at the core.     */
static uint32_t g_usb_stream_chunks;   /* IN transfers completed.                               */
static uint32_t g_usb_stream_actual;   /* the last dTD's arrived count (the `token` read).      */
static uint32_t g_usb_stream_state;    /* a small published enum.                               */

#define USB_STREAM_ST_IDLE     0u   /* not a device yet.                         */
#define USB_STREAM_ST_WAITING  1u   /* a device; the endpoint is not enabled yet. */
#define USB_STREAM_ST_STREAM   2u   /* priming / re-priming chunks.               */
#define USB_STREAM_ST_DRAINED  3u   /* the source is consumed past the cursor.     */

static volatile uint32_t *usb_stream_td(void)
{
    return (volatile uint32_t *)(uintptr_t)g_usb_stream_td;
}

/* ------------------------------------------------------------------------------------------------
 * The source read: `available = min(size, ceiling) - cursor`, with `size` read LIVE from the ring.
 * A forward-only cursor (design §2): the ring is a monotonic append, so the source only grows and the
 * cursor only moves toward it. A wrap would need a second invariant this arm need not take on.
 * ------------------------------------------------------------------------------------------------
 */
static uint32_t usb_stream_size(void)
{
    uint32_t size = *(volatile uint32_t *)(uintptr_t)(STAGE90_USB_STREAM_CONSOLE_VA
                                                      + STAGE90_USB_STREAM_OFF_SIZE);
    if (size > STAGE90_USB_STREAM_SIZE_MAX)
        size = STAGE90_USB_STREAM_SIZE_MAX;
    return size;
}

static uint32_t usb_stream_available(void)
{
    uint32_t size = usb_stream_size();
    return (size > g_usb_stream_cursor) ? (size - g_usb_stream_cursor) : 0u;
}

/* ------------------------------------------------------------------------------------------------
 * Prime one chunk: copy `len` bytes from the ring at the cursor into the buffer, build a one-dTD one-page
 * transfer, link it into the endpoint's qh, clean, and prime `ENDPTPRIME = BIT(1+16) = BIT(17)`.
 * ------------------------------------------------------------------------------------------------
 */
static void usb_stream_prime(uint32_t len)
{
    volatile uint32_t *td = usb_stream_td();
    volatile uint32_t *qh = entry_usb_enum_qh_in1();
    volatile uint8_t *src;
    uint32_t token, cap, i;

    if (len > STAGE90_USB_STREAM_CHUNK)
        len = STAGE90_USB_STREAM_CHUNK;

    /* copy the chunk out of the ring (the console is CPU-coherent; a volatile read per byte, appended
     * by the CPU-side entry_write_kv, needs no invalidate - the CPU's own view is the live one). */
    src = (volatile uint8_t *)(uintptr_t)(STAGE90_USB_STREAM_CONSOLE_VA
                                          + STAGE90_USB_STREAM_OFF_DATA + g_usb_stream_cursor);
    for (i = 0u; i < len; i++)
        g_usb_stream_buf[i] = src[i];

    token = ((len & 0x7fffu) << STAGE90_USB_ENUM_TD_TOTAL_SHIFT)
            | STAGE90_USB_ENUM_TD_ACTIVE | STAGE90_USB_ENUM_TD_IOC;

    td[STAGE90_USB_ENUM_TD_OFF_NEXT / 4u]   = STAGE90_USB_ENUM_TD_TERMINATE;
    td[STAGE90_USB_ENUM_TD_OFF_TOKEN / 4u]  = token;
    td[STAGE90_USB_ENUM_TD_OFF_PAGE0 / 4u]  = (uint32_t)(uintptr_t)g_usb_stream_buf;

    /* CPU -> core: clean the dTD and the (one page) buffer the core will DMA. */
    CleanPoU_DcacheRegion((uintptr_t)td, STAGE90_USB_ENUM_TD_STRIDE);
    CleanPoU_DcacheRegion((uintptr_t)g_usb_stream_buf, STAGE90_USB_STREAM_CHUNK);

    /* the endpoint's qh: cap (ZLT | maxpacket(FS 64)) and the embedded link to this arm's dTD. The cap
     * value is the enum arm's for this endpoint; this arm is its one writer while the stream is on. */
    cap = STAGE90_USB_ENUM_QH_ZLT | STAGE90_USB_ENUM_QH_MAX_PKT(STAGE90_USB_ENUM_EP_IN_MAXPKT);
    qh[STAGE90_USB_ENUM_QH_OFF_CAP / 4u]    = cap;
    qh[STAGE90_USB_ENUM_QH_OFF_TDNEXT / 4u] = (uint32_t)(uintptr_t)td & ~0x1fu;
    qh[STAGE90_USB_ENUM_QH_OFF_TDTOK / 4u]  = 0u;
    CleanPoU_DcacheRegion((uintptr_t)qh, STAGE90_USB_ENUM_QH_STRIDE);

    g_usb_stream_last = len;
    g_usb_stream_active = 1u;
    g_usb_stream_state = USB_STREAM_ST_STREAM;

    usb_stream_write32(STAGE90_USB_ENDPTPRIME, STAGE90_USB_ENUM_EPBIT(STAGE90_USB_ENUM_EP_IN, 1u));
}

/* ------------------------------------------------------------------------------------------------
 * On completion: read the dTD's ACTUAL count, advance the cursor by it, clear the completion bit, and
 * re-prime iff the source has more.
 * ------------------------------------------------------------------------------------------------
 */
static void usb_stream_complete(void)
{
    volatile uint32_t *td = usb_stream_td();
    uint32_t token, remaining, actual;

    /* core -> CPU: invalidate the dTD line the core updated (PoC, the operation 910b uses for SETUP). */
    FlushPoC_DcacheRegion((uintptr_t)td, STAGE90_USB_STREAM_TD_LEN);
    token = td[STAGE90_USB_ENUM_TD_OFF_TOKEN / 4u];

    /* the vendor's guard: a still-ACTIVE dTD is not done (`ci13xxx_udc.c:2134`). Re-poll next pass. */
    if ((token & STAGE90_USB_ENUM_TD_ACTIVE) != 0u)
        return;

    remaining = (token & STAGE90_USB_ENUM_TD_TOTAL_MASK) >> STAGE90_USB_ENUM_TD_TOTAL_SHIFT;
    actual = (g_usb_stream_last > remaining) ? (g_usb_stream_last - remaining) : 0u;

    g_usb_stream_actual = actual;
    g_usb_stream_cursor += actual;   /* advance by the bytes the HOST accepted, not by what we offered */
    g_usb_stream_chunks++;
    g_usb_stream_active = 0u;
    usb_stream_write32(STAGE90_USB_ENDPTCOMPLETE, STAGE90_USB_ENUM_EPBIT(STAGE90_USB_ENUM_EP_IN, 1u));

    if (usb_stream_available() == 0u)
        g_usb_stream_state = USB_STREAM_ST_DRAINED;   /* stop priming: the core NAKs the host. */
    else
        usb_stream_prime(usb_stream_available());
}

/* ------------------------------------------------------------------------------------------------
 * The poll - the one entry point, called from `__wrap_Idle_load_context` after `entry_usb_enum_poll`.
 * ------------------------------------------------------------------------------------------------
 */
void entry_usb_stream_poll(void)
{
    uint32_t usbmode, avail;

    /* the gate: a device, and the endpoint already ENABLED by the enum arm (not this one). */
    usbmode = usb_stream_read32(STAGE90_USB_USBMODE);
    if ((usbmode & STAGE90_USB_USBMODE_CM) != STAGE90_USB_USBMODE_CM_DEVICE) {
        g_usb_stream_state = USB_STREAM_ST_IDLE;
        return;
    }
    if ((usb_stream_read32(STAGE90_USB_ENDPTCTRL(STAGE90_USB_ENUM_EP_IN))
         & STAGE90_USB_ENDPTCTRL_TXE) == 0u) {
        if (g_usb_stream_state != USB_STREAM_ST_WAITING) {
            g_usb_stream_state = USB_STREAM_ST_WAITING;
            USB_STREAM_LIVE("xnu_live_usb_stream_waiting", 1u);
        }
        return;
    }

    /* A chunk outstanding: only a completion advances us. Otherwise prime the next if the source has it. */
    if (g_usb_stream_active != 0u) {
        if ((usb_stream_read32(STAGE90_USB_ENDPTCOMPLETE)
             & STAGE90_USB_ENUM_EPBIT(STAGE90_USB_ENUM_EP_IN, 1u)) != 0u)
            usb_stream_complete();
    } else if (g_usb_stream_state != USB_STREAM_ST_DRAINED) {
        avail = usb_stream_available();
        if (avail == 0u) {
            g_usb_stream_state = USB_STREAM_ST_DRAINED;
        } else {
            if (g_usb_stream_chunks == 0u)
                USB_STREAM_LIVE("xnu_live_usb_stream_armed", 1u);
            usb_stream_prime(avail);
        }
    }

    USB_STREAM_LIVE("xnu_live_usb_stream_state", g_usb_stream_state);
    USB_STREAM_LIVE("xnu_live_usb_stream_cursor", g_usb_stream_cursor);
    USB_STREAM_LIVE("xnu_live_usb_stream_chunks", g_usb_stream_chunks);
    USB_STREAM_LIVE("xnu_live_usb_stream_actual", g_usb_stream_actual);
    USB_STREAM_LIVE("xnu_live_usb_stream_avail", usb_stream_available());
}

#else /* !STAGE90_XNU_USB_STREAM */

void entry_usb_stream_poll(void)
{
}

#endif /* STAGE90_XNU_USB_STREAM */