/*
 * 910a2: the USB2 OTG PHY init and the device-mode transition - THE FIRST ARM OF THIS WALK THAT WRITES
 * TO THIS PORT.
 *
 * ------------------------------------------------------------------------------------------------
 * Why a second file, and why it is the one that takes the risk
 * ------------------------------------------------------------------------------------------------
 *
 * 910a (`entry_usb.c`) reads the ChipIdea core and writes nothing; its guard, `tools/check_usb_probe.py`,
 * refuses a `usb_write32` in that file, because the micro-B port had `adb`/`fastboot` enumerating on it
 * up to the handoff and a read cannot drop the host's enumeration. 910a2 is the rung the map's ladder
 * (§5) calls "the PHY init + the device-mode transition (the WRITES)": it runs the vendor's PHY sequence
 * (`msm_otg.c`'s `msm_otg_reset`) and the ChipIdea device-mode sequence (`ci13xxx_udc.c`'s
 * `hw_device_reset`), and it is the first arm here that stores through the USB window. So it lives in
 * its own file with its own `usb_dev_write32`, its own header (`entry_usb_dev.h`), and its own guard
 * (`tools/check_usb_dev.py`) - the write-nothing property 910a publishes stays a property of 910a's
 * source, and this file's stores are compared against the Android source that OWNS each one.
 *
 * **THE HAZARD, NAMED AND BOUNDED.** `msm_otg_link_reset` writes `USBCMD.RST` (bit 1), which resets the
 * controller - and a reset on a port the host is enumerating **drops that enumeration**. The host then
 * sees a disconnect-reconnect, which is recoverable, but a wedged PHY is not. The escape from a dark
 * phone is the physical VolDown+Power press, not USB (`[[mi4-device-dark-needs-power-press]]`), so
 * `fastboot` stays reachable through the bootloader regardless. **The arm bounds the hazard two ways:**
 *
 *   1. **It does not write a USB register at all unless 910a found the core ALREADY a device**
 *      (`USBMODE[1:0] == 2`). That is the "the mode sequence is a re-do" case (531 §6): the bootloader
 *      left the core in the mode the vendor driver wants, so this arm's device-mode shaping is not the
 *      thing that makes it a device - the reset is. A core the bootloader left in some *other* mode is a
 *      core whose live state this arm does not understand, and it refuses rather than resets it blind.
 *      The opt-in `STAGE90_XNU_USB_DEV_FORCE=1` relaxes that gate for a deliberate experiment.
 *   2. **Every wait is bounded by the vendor's own number** (`ULPI_IO_TIMEOUT_USEC = 10 ms`, the link
 *      reset's 250 ms), so a viewport or a reset that never completes is a record, not an unending spin.
 *
 * ------------------------------------------------------------------------------------------------
 * The sequence, and where each number comes from
 * ------------------------------------------------------------------------------------------------
 *
 * The order is the vendor's: `msm_otg_reset` (PHY) then `hw_device_reset` (UDC device mode), which is
 * the order they run in the live system - the OTG reset fires on cable-attach, the UDC's on VBUS. Both
 * are transcribed, step by step, with the owning function named:
 *
 *   `msm_otg_phy_reset`      clear `AHB2AHB_BYPASS` (bit 31 of `USB_AHBMODE 0x98`) if set;
 *                            `USB_PORTSC = (PORTSC & ~(3<<30)) | (3<<30)`  [PTS = ULPI]
 *   `msm_otg_link_reset`     `USB_USBCMD = USBCMD_RST (2)`; poll `RST` clear <= 250 ms;
 *                            `USB_PORTSC = 0x80000000`; `USB_AHBBURST (0x90) = 0`
 *   (the vendor's `msleep(100)`)
 *   `usb_phy_reset`          `USB_PHY_CTRL (0x240)` bit 0 assert, ~12 us, deassert
 *   `ulpi_init`              `ulpi_write(phy, 0x63, 0x81)` - the dtsi's `hsusb-otg-phy-init-seq`
 *   `usb_phy_reset` again    the same POR pair
 *   `otg-control == PHY`     `USB_OTGSC |= OTGSC_BSVIE`; `ulpi_write(ULPI_INT_SESS_VALID, reg 0x0d)`
 *                            and the same to reg 0x10
 *   `hw_device_reset`        `USB_USBMODE` step by step: `CM_IDLE(0)` -> `CM_DEVICE(2)` -> `| USBMODE_SLOM`;
 *                            verify read-back `USBMODE[1:0] == 2`; `USBCMD.ITC <- 0`
 *   `hw_device_state` (partial) `USBCMD.RS <- 1` (run)
 *
 * **What it deliberately does NOT do, and publishes that it did not:** it does not write
 * `USB_ENDPOINTLISTADDR` or `USBINTR`, and it does not program EP0's `USB_ENDPTCTRL`. Those three are
 * what `hw_device_state` does with a **queue-head list**, and this image has no `ci13xxx` gadget, no
 * dTD/qh pool, and no DMA-visible buffer for endpoint 0 to point at - so writing `ENDPOINTLISTADDR`
 * with no list behind it is a core told to DMA from nowhere. That is the next rung (910b), and the arm's
 * `_dev_intr`/`_dev_eplist` cells record the omission as a reading rather than a silence. **No control
 * transfer is answered and no interrupt is enabled, so this arm is a PHY + mode transition and not yet a
 * device the host can talk to** - the goal is not met here.
 *
 * **The vendor collapses into ONE reset here.** `msm_otg_link_reset` and `hw_device_reset` both write
 * `USBCMD.RST`; on this path they are the same store, and doing it twice only widens the window in which
 * the host sees the controller come and go. The arm writes it once, in the link-reset step, and
 * publishes `_dev_rst_count = 1` so the collapse is a reading and not a silent difference.
 * `msm_otg.c`'s `qcom,hsusb-otg-disable-reset` makes the vendor's PHY init a one-shot on the real path;
 * the ARM switch is this image's one-shot, exactly as the vendor's is the driver's.
 *
 * ------------------------------------------------------------------------------------------------
 * The mapping: reuse-of-record vs a fresh install, and the difference a read depends on
 * ------------------------------------------------------------------------------------------------
 *
 * `entry_mmio_section` installs a 1 MB L1 section and **returns 0 if the slot is already occupied**
 * ([[mi4-one-value-two-definitions]] one layer down: a section the instrument did not install is not one
 * it can vouch for). 910a installs megabyte `0xf9a` first, so on a run with BOTH arms on this arm's own
 * install returns 0 - and then the slot is **not empty, it holds 910a's desk**:
 * `entry_section_install` refuses when `(before & 0x3) != 0` (not a *fault* descriptor), and 910a's
 * section is a *block* descriptor (`type == 2`) whose physical-address field is megabyte `0xf9a`. So the
 * arm proves the occupant is 910a's own section - a **block** descriptor pointing at **this** megabyte -
 * and reads on; any other occupant leaves `_dev_owned = 0` and the arm reads NO USB register, the same
 * refusal 910a makes when its own install is refused. The proof is the `slot_before` the mapper already
 * returns; the arm spells the test in `usb_dev_section_is_ours`.
 */
#include <stdint.h>

#include "entry_usb_dev.h"
#include "entry_timebase.h"   /* stage90_cntvct_read - the payload's own 19.2 MHz counter reader. */

/*
 * The arm switch, and the gate-relax switch. Both default to 0; the arm switch gates the whole body, so
 * with it off this file is a one-line `void entry_usb_dev_init(void) { }` and the call site in
 * `entry_trace.c` is unconditional (the shape 910a uses). The declarations are outside the `#if`:
 * they are not code, so moving them changes no object byte.
 */
#ifndef STAGE90_XNU_USB_DEV
#define STAGE90_XNU_USB_DEV 0
#endif
#ifndef STAGE90_XNU_USB_DEV_FORCE
#define STAGE90_XNU_USB_DEV_FORCE 0
#endif

extern void entry_live_write(const char *key, uint32_t value);
extern uint32_t g_live_state;
/* 484's four numbers, read by `entry_mmio_section` at the install - see `entry_gic.c`/`entry_usb.c`. */
extern uint32_t g_live_mmio_l1;
extern uint32_t g_live_mmio_l1_moved;
extern uint32_t g_live_mmio_ttbr0;
extern uint32_t g_live_mmio_ttbr1;
extern uint32_t entry_mmio_section(uint32_t va, uint32_t pa, uint32_t *slot_before_out,
                                   uint32_t *desc_out);
/* 910a's two readings, which are this arm's gate and its "before" - the header's own reason for
 * publishing them by name. */
extern uint32_t g_stage90_usb_id_cap;
extern uint32_t g_stage90_usb_usbmode;

/* The L1 descriptor's low two bits: 0b10 is a *block* (section) descriptor, 0b00 an invalid slot. */
#define USB_DEV_TTE_TYPE_MASK   0x00000003u
#define USB_DEV_TTE_TYPE_BLOCK  0x00000002u
/* A block descriptor's physical-address field - bits [31:20], i.e. the megabyte it maps. */
#define USB_DEV_TTE_PA_MASK     0xfff00000u
/* The megabyte the OTG core is in, as the mapper indexes it: `0xf9a55000 >> 20 == 0xf9a`. */
#define USB_DEV_SECTION_MB      (STAGE90_USB_OTG_BASE & USB_DEV_TTE_PA_MASK)

/* The vendor's three delays, in microseconds (`msleep(100)`, `usleep_range(1000,1200)` on the link
 * clock, `usleep_range(10,15)` on the PHY POR). The arm uses the vendor's own numbers. */
#define USB_DEV_MSLEEP_US       100000u
#define USB_DEV_PHY_POR_US      12u
#define USB_DEV_LINK_RESET_US   250000u   /* `LINK_RESET_TIMEOUT_USEC = 250 * 1000`. */
#define USB_DEV_ULPI_POLL_US    1u        /* the vendor's `udelay(1)` per viewport poll. */

/*
 * The value this arm wrote into `USBMODE`, published by name the way 910a publishes the one it read.
 * Defined unconditionally so a non-USB build carries a zero rather than an undefined symbol.
 */
uint32_t g_stage90_usb_dev_usbmode_wrote;

#if STAGE90_XNU_USB_DEV

#define USB_DEV_LIVE(key, value) entry_live_write((key), (uint32_t)(value))

/*
 * The store twin of 910a's read. **This is the first file in this walk that stores to a USB register,**
 * so the accessor is the seam the guard reads: `tools/check_usb_probe.py` refuses a `usb_write32` in
 * `entry_usb.c` and `tools/check_usb_dev.py` requires every store in THIS file to be one of the
 * transcribed offsets, matched to the Android function that makes the same store.
 */
static uint32_t usb_dev_read32(uint32_t off)
{
    return *(volatile uint32_t *)(uintptr_t)(STAGE90_USB_OTG_BASE + off);
}

static void usb_dev_write32(uint32_t off, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)(STAGE90_USB_OTG_BASE + off) = value;
}

/* CNTFRQ (`mrc p15, 0, r, c14, c0, 0`) - the same single instruction `entry_trace.c:2417` reads into
 * `g_post_clock_freq`. It is read here so the three delays are a function of the counter's actual
 * frequency rather than a transcribed 19.2 MHz, and published so "the delay used what the machine says"
 * is a reading. */
static uint32_t usb_dev_cntfrq(void)
{
    uint32_t freq;
    __asm__ volatile ("mrc p15, 0, %0, c14, c0, 0" : "=r"(freq));
    return freq;
}

/* A busy-wait of `us` microseconds against the free-running counter. Bounded by construction (it ends
 * when the counter has advanced by the requested amount), so it cannot be the unending spin 692 warns
 * about - it is a delay, not a poll of a device. A zero frequency would make the wait empty, so the
 * caller publishes the frequency and a 0 is visible rather than silent. */
static void usb_dev_delay_us(uint32_t us, uint32_t freq)
{
    uint64_t start, want;
    if (freq == 0u)
        return;
    start = stage90_cntvct_read();
    want = ((uint64_t)us * (uint64_t)freq) / 1000000ull;
    while ((stage90_cntvct_read() - start) < want)
        ;
}

/*
 * Whether the L1 slot the mapper refused to overwrite holds **910a's own section** - a *block*
 * descriptor whose physical-address field is megabyte `0xf9a`. This is the whole difference between
 * "this arm may read the core" and "some other device's descriptor is here and it may not": a read
 * through a mapping this image did not install is a fault, not a measurement (910a's refusal, one
 * layer down).
 */
static uint32_t usb_dev_section_is_ours(uint32_t slot_before)
{
    if ((slot_before & USB_DEV_TTE_TYPE_MASK) != USB_DEV_TTE_TYPE_BLOCK)
        return 0u;
    if ((slot_before & USB_DEV_TTE_PA_MASK) != USB_DEV_SECTION_MB)
        return 0u;
    return 1u;
}

/* The ULPI viewport transaction poll: wait for `RUN` (bit 30) to clear, bounded by the vendor's
 * `ULPI_IO_TIMEOUT_USEC`. Returns the poll count, and a sentinel greater than the bound on timeout. */
static uint32_t usb_dev_ulpi_poll(uint32_t freq)
{
    uint32_t i;
    for (i = 0u; i < STAGE90_USB_ULPI_IO_TIMEOUT; i++) {
        if ((usb_dev_read32(STAGE90_USB_ULPI_VIEWPORT) & STAGE90_USB_ULPI_RUN) == 0u)
            return i;
        usb_dev_delay_us(USB_DEV_ULPI_POLL_US, freq);
    }
    return STAGE90_USB_ULPI_IO_TIMEOUT;
}

/* One viewport WRITE, `ulpi_write(phy, val, reg)` from `msm_otg.c`: the transaction word is
 * `RUN | WRITE | ADDR(reg) | DATA(val)`. */
static uint32_t usb_dev_ulpi_write(uint32_t reg, uint32_t val, uint32_t freq)
{
    usb_dev_write32(STAGE90_USB_ULPI_VIEWPORT,
                    STAGE90_USB_ULPI_RUN | STAGE90_USB_ULPI_WRITE |
                    STAGE90_USB_ULPI_ADDR(reg) | STAGE90_USB_ULPI_DATA(val));
    return usb_dev_ulpi_poll(freq);
}

/* The PHY power-on-reset pair, `usb_phy_reset`: assert (bit 0 set), wait ~12 us, deassert (bit 0
 * clear). The arm calls it twice, once either side of the ULPI PHY-override write, exactly as
 * `msm_otg_reset` does. */
static void usb_dev_phy_por(uint32_t freq)
{
    uint32_t val;

    val = usb_dev_read32(STAGE90_USB_PHY_CTRL);
    val &= ~STAGE90_USB_PHY_POR_BIT_MASK;
    val |= STAGE90_USB_PHY_POR_ASSERT;
    usb_dev_write32(STAGE90_USB_PHY_CTRL, val);
    usb_dev_delay_us(USB_DEV_PHY_POR_US, freq);

    val = usb_dev_read32(STAGE90_USB_PHY_CTRL);
    val &= ~STAGE90_USB_PHY_POR_BIT_MASK;
    val |= STAGE90_USB_PHY_POR_DEASSERT;
    usb_dev_write32(STAGE90_USB_PHY_CTRL, val);
}

static uint32_t g_usb_dev_done;

void entry_usb_dev_init(void)
{
    uint32_t slot_before = 0u, desc = 0u, mapped, owned;
    uint32_t freq, usbmode_before, ahbmode, usbcmd, portsc, otgsc, usbmode_after, val;
    uint32_t rst_polls = 0u, rst_cleared = 0u, ulpi_polls = 0u, ulpi_polls2 = 0u;
    uint64_t t0, now;

    if (g_usb_dev_done != 0u)
        return;
    g_usb_dev_done = 1u;

    USB_DEV_LIVE("xnu_live_usb_dev_live_state", g_live_state);
    USB_DEV_LIVE("xnu_live_usb_dev_base", STAGE90_USB_OTG_BASE);

    /*
     * **(1) THE MAPPING, BY RECORD OR BY REUSE.** The mapper returns 0 both when the slot was occupied
     * and when the live channel does not exist; the two are told apart by `g_live_state` and by the
     * occupant's descriptor. A fresh install (`mapped == 1`) is this arm's own; a refused install is
     * 910a's section if `usb_dev_section_is_ours` says so. Either way `owned == 1` is the licence to
     * read the core, and `owned == 0` is 910a's refusal: no USB register is read.
     */
    mapped = entry_mmio_section(STAGE90_USB_OTG_BASE, STAGE90_USB_OTG_BASE, &slot_before, &desc);
    owned = 0u;
    if (mapped != 0u) {
        owned = 1u;
    } else if (usb_dev_section_is_ours(slot_before) != 0u) {
        owned = 1u;
    }
    USB_DEV_LIVE("xnu_live_usb_dev_map", mapped);
    USB_DEV_LIVE("xnu_live_usb_dev_slot_before", slot_before);
    USB_DEV_LIVE("xnu_live_usb_dev_desc", desc);
    USB_DEV_LIVE("xnu_live_usb_dev_reuse", (mapped == 0u && owned != 0u) ? 1u : 0u);
    USB_DEV_LIVE("xnu_live_usb_dev_ttbr0", g_live_mmio_ttbr0);
    USB_DEV_LIVE("xnu_live_usb_dev_ttbr1", g_live_mmio_ttbr1);
    USB_DEV_LIVE("xnu_live_usb_dev_l1_moved", g_live_mmio_l1_moved);
    if (owned == 0u) {
        USB_DEV_LIVE("xnu_live_usb_dev_owned", 0u);
        USB_DEV_LIVE("xnu_live_usb_dev_mapped", 0u);
        return;
    }
    USB_DEV_LIVE("xnu_live_usb_dev_owned", 1u);
    USB_DEV_LIVE("xnu_live_usb_dev_mapped", 1u);

    freq = usb_dev_cntfrq();
    USB_DEV_LIVE("xnu_live_usb_dev_cntfrq", freq);

    /*
     * **(2) THE GATE: THE MODE THE BOOTLOADER LEFT.** Read fresh here (the authoritative gate) and
     * published beside 910a's own reading of the same register, so the two are a comparison of one
     * value by two readers rather than a second definition. `2` is device mode; anything else means this
     * arm does not understand the core's live state and it refuses to reset it (`FORCE` overrides).
     * **The dependency on 910a is real and stated:** with `STAGE90_XNU_USB_PROBE=0`, `g_stage90_usb_usbmode`
     * is 0 and the two readings disagree, which the record shows as `_dev_probe_mode = 0` against a
     * nonzero `_dev_mode_before` - the arm still gates on its own fresh read.
     */
    usbmode_before = usb_dev_read32(STAGE90_USB_USBMODE);
    USB_DEV_LIVE("xnu_live_usb_dev_mode_before", usbmode_before);
    USB_DEV_LIVE("xnu_live_usb_dev_probe_mode", g_stage90_usb_usbmode);
    USB_DEV_LIVE("xnu_live_usb_dev_id_cap", g_stage90_usb_id_cap);
    USB_DEV_LIVE("xnu_live_usb_dev_is_device",
                 ((usbmode_before & STAGE90_USB_USBMODE_CM) == STAGE90_USB_USBMODE_CM_DEVICE) ? 1u : 0u);
    USB_DEV_LIVE("xnu_live_usb_dev_force", (uint32_t)STAGE90_XNU_USB_DEV_FORCE);

    if (((usbmode_before & STAGE90_USB_USBMODE_CM) != STAGE90_USB_USBMODE_CM_DEVICE)
        && (STAGE90_XNU_USB_DEV_FORCE == 0)) {
        USB_DEV_LIVE("xnu_live_usb_dev_proceed", 0u);
        USB_DEV_LIVE("xnu_live_usb_dev_skipped", 1u);
        return;
    }
    USB_DEV_LIVE("xnu_live_usb_dev_proceed", 1u);

    /*
     * **(3) THE PHY: `msm_otg_phy_reset`'s first half.** Clear the AHB2AHB bypass if the bootloader left
     * it set, then select the ULPI PHY on `PORTSC`. The bypass clear is conditional (the vendor only
     * writes when `AHB2AHB_BYPASS` is set); the PTS write is unconditional and read-modify-write.
     */
    ahbmode = usb_dev_read32(STAGE90_USB_AHBMODE);
    USB_DEV_LIVE("xnu_live_usb_dev_ahbmode_before", ahbmode);
    if ((ahbmode & STAGE90_USB_AHBMODE_AHB2AHB_BYPASS) != 0u) {
        usb_dev_write32(STAGE90_USB_AHBMODE,
                        ahbmode & ~STAGE90_USB_AHBMODE_AHB2AHB_BYPASS);
        USB_DEV_LIVE("xnu_live_usb_dev_bypass_cleared", 1u);
    } else {
        USB_DEV_LIVE("xnu_live_usb_dev_bypass_cleared", 0u);
    }
    portsc = usb_dev_read32(STAGE90_USB_PORTSC);
    USB_DEV_LIVE("xnu_live_usb_dev_portsc_before", portsc);
    usb_dev_write32(STAGE90_USB_PORTSC,
                    (portsc & ~STAGE90_USB_PORTSC_PTS) | STAGE90_USB_PORTSC_PTS_ULPI);
    USB_DEV_LIVE("xnu_live_usb_dev_portsc_pts_wrote",
                 (usb_dev_read32(STAGE90_USB_PORTSC) & STAGE90_USB_PORTSC_PTS));

    /*
     * **(4) THE LINK RESET - THE HAZARD.** `msm_otg_link_reset`: `USBCMD.RST = 1`, poll `RST` clear
     * within the vendor's 250 ms, then `PORTSC = 0x80000000` (bit 31 only, a distinct value from the PTS
     * ULPI write above) and `AHBBURST = 0`. **This is the store that can drop the host's enumeration.**
     * It is written ONCE (the collapse the file's header states), and `_dev_rst_count = 1` records it.
     */
    usbcmd = usb_dev_read32(STAGE90_USB_USBCMD);
    USB_DEV_LIVE("xnu_live_usb_dev_usbcmd_before", usbcmd);
    usb_dev_write32(STAGE90_USB_USBCMD, STAGE90_USB_USBCMD_RST);
    USB_DEV_LIVE("xnu_live_usb_dev_rst_count", 1u);
    t0 = stage90_cntvct_read();
    for (;;) {
        now = stage90_cntvct_read();
        if ((usb_dev_read32(STAGE90_USB_USBCMD) & STAGE90_USB_USBCMD_RST) == 0u) {
            rst_cleared = 1u;
            break;
        }
        rst_polls++;
        if (freq != 0u
            && (now - t0) > ((uint64_t)USB_DEV_LINK_RESET_US * (uint64_t)freq / 1000000ull))
            break;
    }
    USB_DEV_LIVE("xnu_live_usb_dev_rst_polls", rst_polls);
    USB_DEV_LIVE("xnu_live_usb_dev_rst_cleared", rst_cleared);
    usb_dev_write32(STAGE90_USB_PORTSC, STAGE90_USB_PORTSC_RST_BIT31);
    usb_dev_write32(STAGE90_USB_AHBBURST, 0u);
    USB_DEV_LIVE("xnu_live_usb_dev_portsc_rst_wrote", usb_dev_read32(STAGE90_USB_PORTSC));

    /* The vendor's `msleep(100)` between the link reset and the PHY reset. */
    usb_dev_delay_us(USB_DEV_MSLEEP_US, freq);

    /*
     * **(5) THE PHY POR AND THE ULPI PHY-OVERRIDE WRITE.** `usb_phy_reset` (assert/deassert), the one
     * `ulpi_init` write from the dtsi's `hsusb-otg-phy-init-seq` (`0x63` -> PHY reg `0x81`), then
     * `usb_phy_reset` again. The viewport poll count is published so a transaction that timed out is a
     * number (the sentinel `ULPI_IO_TIMEOUT`) rather than a hang.
     */
    usb_dev_phy_por(freq);
    ulpi_polls = usb_dev_ulpi_write(STAGE90_USB_ULPI_PHY_INIT_REG, STAGE90_USB_ULPI_PHY_INIT_VAL, freq);
    usb_dev_phy_por(freq);
    USB_DEV_LIVE("xnu_live_usb_dev_ulpi_phy_polls", ulpi_polls);

    /*
     * **(6) THE OTG INTERRUPT ENABLES** - the `OTG_PHY_CONTROL` branch of `msm_otg_reset`
     * (`qcom,hsusb-otg-otg-control = <1>`, the base dtsi's value): set `OTGSC_BSVIE` and enable
     * B-session-valid on both the rising and falling ULPI interrupt registers. The secondary-PHY bit
     * (`USB_PHY_CTRL2` bit 16) is NOT set - this board is not `enable_sec_phy` - and that decision is
     * published rather than left silent.
     */
    otgsc = usb_dev_read32(STAGE90_USB_OTGSC);
    USB_DEV_LIVE("xnu_live_usb_dev_otgsc_before", otgsc);
    usb_dev_write32(STAGE90_USB_OTGSC, otgsc | STAGE90_USB_OTGSC_BSVIE);
    ulpi_polls2 = usb_dev_ulpi_write(STAGE90_USB_ULPI_USB_INT_EN_RISE, STAGE90_USB_ULPI_INT_SESS_VALID,
                                     freq);
    (void)usb_dev_ulpi_write(STAGE90_USB_ULPI_USB_INT_EN_FALL, STAGE90_USB_ULPI_INT_SESS_VALID, freq);
    USB_DEV_LIVE("xnu_live_usb_dev_ulpi_int_polls", ulpi_polls2);
    USB_DEV_LIVE("xnu_live_usb_dev_sec_phy", 0u);
    USB_DEV_LIVE("xnu_live_usb_dev_otgsc_wrote", usb_dev_read32(STAGE90_USB_OTGSC));

    /*
     * **(7) THE DEVICE-MODE TRANSITION.** `hw_device_reset`'s `USBMODE` step, by the book: `CM_IDLE`,
     * then `CM_DEVICE`, then set `USBMODE_SLOM` (setup-lockout). Each is a store; the read-back after
     * the last is the assertion the vendor's own `hw_device_reset` makes (`cannot enter in device mode`
     * if it is not `CM_DEVICE`). `USBCMD.ITC <- 0` is the driver's `CI13XXX_ZERO_ITC` flag.
     */
    usb_dev_write32(STAGE90_USB_USBMODE, STAGE90_USB_USBMODE_CM_IDLE);
    usb_dev_write32(STAGE90_USB_USBMODE, STAGE90_USB_USBMODE_CM_DEVICE);
    val = usb_dev_read32(STAGE90_USB_USBMODE);
    usb_dev_write32(STAGE90_USB_USBMODE, val | STAGE90_USB_USBMODE_SLOM);
    usbmode_after = usb_dev_read32(STAGE90_USB_USBMODE);
    g_stage90_usb_dev_usbmode_wrote = usbmode_after;
    USB_DEV_LIVE("xnu_live_usb_dev_usbmode_after", usbmode_after);
    USB_DEV_LIVE("xnu_live_usb_dev_devmode_ok",
                 ((usbmode_after & STAGE90_USB_USBMODE_CM) == STAGE90_USB_USBMODE_CM_DEVICE) ? 1u : 0u);

    usbcmd = usb_dev_read32(STAGE90_USB_USBCMD);
    usb_dev_write32(STAGE90_USB_USBCMD, usbcmd & ~STAGE90_USB_USBCMD_ITC_MASK);

    /* **(8) RUN, AND THE DELIBERATE OMISSIONS.** `USBCMD.RS <- 1`, then the three stores this arm does
     * NOT take are published as zeros: `USBINTR`, `ENDPOINTLISTADDR` (both need the queue-head list this
     * image has not built - 910b), and EP0's `ENDPTCTRL`. */
    usbcmd = usb_dev_read32(STAGE90_USB_USBCMD);
    usb_dev_write32(STAGE90_USB_USBCMD, usbcmd | STAGE90_USB_USBCMD_RS);
    USB_DEV_LIVE("xnu_live_usb_dev_usbcmd_after", usb_dev_read32(STAGE90_USB_USBCMD));
    USB_DEV_LIVE("xnu_live_usb_dev_intr", 0u);
    USB_DEV_LIVE("xnu_live_usb_dev_eplist", 0u);
    USB_DEV_LIVE("xnu_live_usb_dev_ep0_ctrl", 0u);

    /* **(9) THE STATE AFTER.** The connected-to-host reading the next rung depends on: `PORTSC.CCS` and
     * `OTGSC.BSV`, and the whole `USBSTS`. */
    portsc = usb_dev_read32(STAGE90_USB_PORTSC);
    otgsc = usb_dev_read32(STAGE90_USB_OTGSC);
    USB_DEV_LIVE("xnu_live_usb_dev_portsc", portsc);
    USB_DEV_LIVE("xnu_live_usb_dev_portsc_ccs", (portsc & STAGE90_USB_PORTSC_CCS) ? 1u : 0u);
    USB_DEV_LIVE("xnu_live_usb_dev_portsc_phcd", (portsc & STAGE90_USB_PORTSC_PHCD) ? 1u : 0u);
    USB_DEV_LIVE("xnu_live_usb_dev_otgsc", otgsc);
    USB_DEV_LIVE("xnu_live_usb_dev_otgsc_bsv", (otgsc & STAGE90_USB_OTGSC_BSV) ? 1u : 0u);
    USB_DEV_LIVE("xnu_live_usb_dev_usbsts", usb_dev_read32(STAGE90_USB_USBSTS));

    USB_DEV_LIVE("xnu_live_usb_dev_loaded", 1u);
}

#else /* !STAGE90_XNU_USB_DEV */

/*
 * The OFF arm: a one-line empty body, the shape `entry_usb.c`'s own OFF arm has. The call site in
 * `entry_trace.c` is guarded by the same switch; the `-Werror` build is what proves the body is empty.
 */
void entry_usb_dev_init(void)
{
}

#endif /* STAGE90_XNU_USB_DEV */