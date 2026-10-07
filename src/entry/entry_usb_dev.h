/*
 * 910a2: the USB2 OTG PHY/device bring-up - the registers `entry_usb_dev.c` WRITES, and the values it
 * writes them, each named beside the Android source that OWNS it.
 *
 * **Why a second header and not `entry_usb.h`.** 910a's `entry_usb.c` is provably write-nothing
 * ([[mi4-off-option-two-spellings]] one build out: a store that exists is a store), and its guard,
 * `tools/check_usb_probe.py`, refuses a `usb_write32` in that file. 910a2 is the first arm that WRITES
 * to this port, so it lives in its own file with its own accessor and its own guard; the read header
 * carries only reads. Splitting the header along the same seam keeps `entry_usb.h`'s offsets the ones a
 * read uses and puts every *written* register - and every bit it sets - here, one definition each, the
 * value's owner named.
 *
 * **THE OWNER IS `external/android_kernel_xiaomi_cancro`.** Three files in that checkout spell the
 * numbers below: `include/linux/usb/msm_hsusb_hw.h` (the ChipIdea map `msm_otg.c` includes),
 * `arch/arm/mach-msm/include/mach/msm_hsusb_hw.h` (the same map, plus the endpoint-control bits), and
 * `drivers/usb/gadget/ci13xxx_udc.h` (the UDC driver's USBMODE/USBCMD/ENDPTCTRL bits). Where the two
 * msm_hsusb headers disagree (`PORTSC_PTS_ULPI`, `USBINTR`, EP0 `ENDPTCTRL`), the value the **live**
 * driver uses wins - `msm_otg.c`/`ci13xxx_udc.c` include the `linux/` header, so the live
 * `PORTSC_PTS_ULPI` is `3<<30`, not the mach header's `2<<30` - and `tools/check_usb_dev.py` reads the
 * owner to prove it ([[mi4-one-value-two-definitions]]).
 */
#ifndef STAGE90_ENTRY_USB_DEV_H
#define STAGE90_ENTRY_USB_DEV_H

#include <stdint.h>

#include "entry_usb.h"

/* -------------------------------------------------------------------------------------------------
 * The registers 910a2 WRITES that 910a's read header does not name.
 * -------------------------------------------------------------------------------------------------
 * `mach/msm_hsusb_hw.h` and `include/linux/usb/msm_hsusb_hw.h`, `MSM_USB_BASE + N`.
 */

/* `USB_AHBBURST` - the AHB burst register. The same offset `entry_usb.h` calls `USB_SBUSCFG` (0x090):
 * two names the Android headers give one register, which is why both are transcribed and both are
 * checked against their owner. `msm_otg_link_reset` writes it to 0. */
#define STAGE90_USB_AHBBURST     0x090u

/* `USB_PHY_CTRL` (`0x240`) - the Synopsys 28nm PHY's power-on-reset register. Bit 0 (`PHY_POR`) is the
 * PHY's own POR: assert (set), wait, deassert (clear). `usb_phy_reset` in `msm_otg.c` runs this pair
 * twice - once before and once after the ULPI PHY-override write - and only when the PHY is the
 * integrated 28nm part (`qcom,hsusb-otg-phy-type = <2>` from the dtsi, which this board is). */
#define STAGE90_USB_PHY_CTRL     0x240u

/* `USB_PHY_CTRL2` (`0x278`) - the second PHY control word; bit 16 enables the secondary PHY. Not set
 * on this board (`enable_sec_phy` is false), named here so the value the run must NOT write is a
 * reading rather than a silence. */
#define STAGE90_USB_PHY_CTRL2    0x278u

/* `USB_L1_EP_CTRL` (`0x250`) and `USB_L1_CONFIG` (`0x254`) - the L1 (USB2 link power) endpoint mask and
 * config. Only reached on an L1-capable gadget; named for completeness, not written by this arm. */
#define STAGE90_USB_L1_EP_CTRL   0x250u
#define STAGE90_USB_L1_CONFIG    0x254u

/* -------------------------------------------------------------------------------------------------
 * The bit fields, by the name the owning header gives them.
 * -------------------------------------------------------------------------------------------------
 * `USBCMD` (`mach/msm_hsusb_hw.h:60-65`, `ci13xxx_udc.h:205-208`).
 */
#define STAGE90_USB_USBCMD_RS        0x00000001u  /* `USBCMD_RS` / `USBCMD_ATTACH` - bit 0, run.  */
#define STAGE90_USB_USBCMD_RST       0x00000002u  /* `USBCMD_RESET` / `USBCMD_RST` - bit 1.     */
#define STAGE90_USB_USBCMD_ITC_MASK  0x00ff0000u  /* `USBCMD_ITC_MASK` - bits [23:16].         */
#define STAGE90_USB_USBCMD_ITC(n)    (((n) << 16))/* `USBCMD_ITC(n)`.                          */

/* `USBMODE` (`mach/msm_hsusb_hw.h:69-77`, `ci13xxx_udc.h:232-237`). `CM` is bits [1:0]; `2` is device,
 * `3` host, `0` neither. `SLOM` (bit 3) is "setup lockout mode"; `SDIS` (bit 4) is "stream disable",
 * which the live driver sets (`CI13XXX_DISABLE_STREAMING`). */
#define STAGE90_USB_USBMODE_CM        0x00000003u
#define STAGE90_USB_USBMODE_CM_IDLE   0x00000000u
#define STAGE90_USB_USBMODE_CM_DEVICE 0x00000002u
#define STAGE90_USB_USBMODE_CM_HOST   0x00000003u
#define STAGE90_USB_USBMODE_SLOM      0x00000008u  /* `USBMODE_SLOM` - bit 3.                   */
#define STAGE90_USB_USBMODE_SDIS      0x00000010u  /* `USBMODE_SDIS` - bit 4.                   */

/* `ENDPTCTRL(n)` (`mach/msm_hsusb_hw.h:142-165`): the RX and TX halves of one endpoint's control word.
 * EP0's enabled value is `RXE|RXR|TXE|TXR` with type CONTROL (0), which is `0x00c000c0`. */
#define STAGE90_USB_ENDPTCTRL_RXS     0x00000001u
#define STAGE90_USB_ENDPTCTRL_RXT     0x0000000cu  /* type - bits [3:2].                        */
#define STAGE90_USB_ENDPTCTRL_RXR     0x00000040u
#define STAGE90_USB_ENDPTCTRL_RXE     0x00000080u
#define STAGE90_USB_ENDPTCTRL_TXS     0x00010000u
#define STAGE90_USB_ENDPTCTRL_TXT     0x000c0000u  /* type - bits [19:18].                       */
#define STAGE90_USB_ENDPTCTRL_TXR     0x00400000u
#define STAGE90_USB_ENDPTCTRL_TXE     0x00800000u

/* `USBINTR` (`ci13xxx_udc.h:211-215`): the interrupt mask `hw_device_state` sets. The **live** value is
 * the ci13xxx set `USBi_UI|UEI|PCI|URI|SLI = 0x147`; `msm72k_udc.c` writes `0x145` (no UEI). This arm
 * publishes the mask it wrote, so the two spellings are two numbers in the log, not one guess. */
#define STAGE90_USB_USBINTR_VALUE     0x00000147u

/* `OTGSC` (`include/linux/usb/msm_hsusb_hw.h:110-111`, `mach/...:211-217`). */
#define STAGE90_USB_OTGSC_IDIE        0x01000000u  /* bit 24 - ID interrupt enable.             */
#define STAGE90_USB_OTGSC_BSVIE       0x08000000u  /* bit 27 - B-session-valid interrupt enable.*/

/* `PORTSC` (`include/linux/usb/msm_hsusb_hw.h:57-58`). `PTS` is bits [31:30]; the ULPI selection is
 * `3`. **`PORTSC_PTS_ULPI` is `3<<30` in the header `msm_otg.c` includes** (the `linux/` one); the
 * `mach/` header's `2<<30` belongs to the unbuilt `msm72k_otg.c` and is NOT what this board's live
 * driver writes. The raw `0x80000000` (bit 31 only) that `msm_otg_link_reset` writes first is a
 * distinct value, published separately so the two are never read as one. */
#define STAGE90_USB_PORTSC_PTS         0xc0000000u
#define STAGE90_USB_PORTSC_PTS_ULPI    0xc0000000u
#define STAGE90_USB_PORTSC_RST_BIT31   0x80000000u

/* `USB_AHBMODE` (`include/linux/usb/msm_hsusb_hw.h:52-54`): bit 31 is the AHB2AHB bypass. `msm_otg_phy_reset`
 * clears it if set. */
#define STAGE90_USB_AHBMODE_AHB2AHB_BYPASS      0x80000000u
#define STAGE90_USB_AHBMODE_AHB2AHB_BYPASS_CLEAR 0x00000000u

/* `USB_PHY_CTRL` bit 0 (`include/linux/usb/msm_hsusb_hw.h:93-95`): the PHY POR. */
#define STAGE90_USB_PHY_POR_BIT_MASK   0x00000001u
#define STAGE90_USB_PHY_POR_ASSERT     0x00000001u
#define STAGE90_USB_PHY_POR_DEASSERT   0x00000000u

/* `USB_ULPI_VIEWPORT` (`include/linux/usb/msm_hsusb_hw.h:64-71`): a transaction is a WRITE with `RUN`
 * set. `ULPI_ADDR(reg)` puts the PHY register in bits [23:16]; `ULPI_DATA(v)` the value in bits [7:0];
 * a read returns the data in bits [15:8]. */
#define STAGE90_USB_ULPI_RUN          0x40000000u  /* bit 30.                                   */
#define STAGE90_USB_ULPI_WRITE        0x20000000u  /* bit 29.                                   */
#define STAGE90_USB_ULPI_READ         0x00000000u  /* bit 29 clear.                             */
#define STAGE90_USB_ULPI_ADDR(n)      (((n) & 0xffu) << 16)
#define STAGE90_USB_ULPI_DATA(v)      ((v) & 0xffu)
#define STAGE90_USB_ULPI_DATA_READ(x) (((x) >> 8) & 0xffu)

/* The ULPI PHY-override write the dtsi's `qcom,hsusb-otg-phy-init-seq = <0x63 0x81 0xffffffff>` yields:
 * ONE write of value `0x63` to PHY register `0x81` (`msm_otg.c`'s `ulpi_init` consumes `{val, reg}`
 * pairs; the `0xffffffff` terminator is read as a negative `int` and ends the loop). */
#define STAGE90_USB_ULPI_PHY_INIT_VAL 0x63u
#define STAGE90_USB_ULPI_PHY_INIT_REG 0x81u

/* The ULPI interrupt-enable registers, and the session-valid bit (`include/linux/usb/ulpi.h:71-72,147`).
 * `OTG_PHY_CONTROL` (the base dtsi's `qcom,hsusb-otg-otg-control = <1>`) enables B-session-valid on
 * both rise and fall. */
#define STAGE90_USB_ULPI_USB_INT_EN_RISE 0x0du
#define STAGE90_USB_ULPI_USB_INT_EN_FALL 0x10u
#define STAGE90_USB_ULPI_INT_SESS_VALID  0x00000004u  /* `ULPI_INT_SESS_VALID` - bit 2.         */

/* `USB_L1_CONFIG` (`include/linux/usb/msm_hsusb_hw.h:46-50`) - the L1 enable set `ci13xxx_msm_set_l1`
 * programs when the gadget supports L1. Named so a run that must NOT program it (this arm, which has no
 * gadget) is a reading rather than an omission. */
#define STAGE90_USB_L1_CONFIG_LPM_EN         0x00000010u
#define STAGE90_USB_L1_CONFIG_REMOTE_WAKEUP  0x00000020u
#define STAGE90_USB_L1_CONFIG_GATE_SYS_CLK   0x00000080u
#define STAGE90_USB_L1_CONFIG_PHY_LPM        0x00000400u
#define STAGE90_USB_L1_CONFIG_PLL            0x00000800u
#define STAGE90_USB_L1_CONFIG_VALUE          (STAGE90_USB_L1_CONFIG_LPM_EN |    \
                                              STAGE90_USB_L1_CONFIG_REMOTE_WAKEUP | \
                                              STAGE90_USB_L1_CONFIG_GATE_SYS_CLK | \
                                              STAGE90_USB_L1_CONFIG_PHY_LPM |    \
                                              STAGE90_USB_L1_CONFIG_PLL)           /* 0x000008b0 */

/* The ULPI viewport transaction's poll bound, `msm_otg.c`'s `ULPI_IO_TIMEOUT_USEC = 10 * 1000`. A
 * viewport write that never clears `RUN` is a bus wait (692's class); the arm bounds it with the
 * vendor's own number rather than spinning forever. */
#define STAGE90_USB_ULPI_IO_TIMEOUT    10000u

/*
 * The bring-up, called once - the site `entry_usb_probe` uses, for the probe's own reasons - and gated
 * both on `STAGE90_XNU_USB_DEV` (0 compiles the body to a `return`, so the OFF arm is this source) and
 * on 910a's reading: **by default it does not write a USB register at all unless the probe found the
 * core in `USBMODE` device mode** (`STAGE90_USB_USBMODE_CM_DEVICE`), because a port the bootloader
 * already left a device is a port a `USBCMD.RST` can drop off the host's enumeration. The opt-in switch
 * `STAGE90_XNU_USB_DEV_FORCE` relaxes that gate for a deliberate experiment; it defaults to 0.
 */
void entry_usb_dev_init(void);

/*
 * The value this arm wrote into `USBMODE`, published by name the way 910a publishes the one it read: a
 * later reader compares the mode the bootloader left (`g_stage90_usb_usbmode`) with the one the arm set,
 * two readings of one register rather than a claim.
 */
extern uint32_t g_stage90_usb_dev_usbmode_wrote;

#endif /* STAGE90_ENTRY_USB_DEV_H */