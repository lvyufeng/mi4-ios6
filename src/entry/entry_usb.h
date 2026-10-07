/*
 * 910a: the USB2 OTG controller's identification and state, read from inside the kernel and written
 * nowhere.
 *
 * **Why a header.** The same reason `entry_gic.h` and `entry_storage.h` are ones: this image cannot
 * include the device's own Linux headers, so every number `entry_usb.c` reads is transcribed, and a
 * transcribed offset is this project's most repeated defect ([[mi4-one-value-two-definitions]]). One
 * definition, in a file about the thing it describes, and the value's *owner* named beside it - here
 * the Android checkout at `external/android_kernel_xiaomi_cancro`, whose `include/linux/usb/
 * msm_hsusb_hw.h` and `arch/arm/mach-msm/include/mach/msm_hsusb_hw.h` spell every offset below as
 * `MSM_USB_BASE + N`.
 *
 * **THE CONTROLLER IS A CHIPIDEA CI13XXX (MSM72K), NOT A DWC-OTG, AND THIS HEADER IS WHERE THAT IS
 * PINNED.** The 910 map first called this port a "DWC-OTG gadget" - a guess from the family of
 * Qualcomm USB cores, and wrong for this one. The device's own checkout says otherwise three ways:
 * the USB2 node's `compatible` is `qcom,hsusb-otg` (`arch/arm/boot/dts/msm8974.dtsi`), `msm_otg.c`
 * is the OTG/PHY wrapper, and the *gadget* is `drivers/usb/gadget/{ci13xxx_msm.c,msm72k_udc.c}`
 * (`ci13xxx_udc.c`, the ChipIdea UDC) - none of which is a DWC register file. So the register layout
 * below is ChipIdea's, the one `msm_hsusb_hw.h` transcribes, and 910a reads it rather than the DWC3
 * map of the *other* controller (`0xf9200000`, USB3 SS). **Reading the wrong map would have made
 * every number a reading of nothing** - which is the whole reason the first arm is a read with the
 * map's owner named in the record.
 */
#ifndef STAGE90_ENTRY_USB_H
#define STAGE90_ENTRY_USB_H

/*
 * **The USB2 OTG core, from the device tree (`msm8974.dtsi`: `usb@f9a55000 { reg = <0xf9a55000
 * 0x400>; }`).** The window is 1 KB and it sits in megabyte `0xf9a`, which no other device this
 * image has mapped owns - the GIC is `0xf90`, the eMMC `0xf98`, the TLMM pad block its own megabyte -
 * so `entry_mmio_section`'s 1 MB-section install (indexed by `va >> 20`) applies with no occupancy
 * conflict. That is the *opposite* of the watchdog's fate, where two devices shared one megabyte and
 * the second install was refused ([[mi4-909-residence-rung-parked]]).
 */
#define STAGE90_USB_OTG_BASE    0xf9a55000u

/* The USB3 SS controller (`qcom,ssusb@f9200000`, `dwc3-msm`), megabyte `0xf92`, present so this arm
 * can read *its* identification too and let the record show which core the bootloader left live. It
 * is a DWC3 - a different register file - so nothing below treats the two as one. */
#define STAGE90_USB_SS_BASE     0xf9200000u

/*
 * The core's identification and capability registers. `mach/msm_hsusb_hw.h:20-28`. `USB_ID_CAP`
 * carries the controller revision; `USB_HWGENERAL`'s `PHYW`/`PHYM`/`PHYS` fields say which PHY the
 * core was built around (bit 4-5 `PHYW`: 0=UTMI+3, 1=ULPI, 2=UTMI+2, 3=serial), which is the field
 * that decides whether the ULPI viewport below is a real window or dead.
 */
#define STAGE90_USB_ID_CAP      0x000u
#define STAGE90_USB_HWGENERAL   0x004u
#define STAGE90_USB_HWHOST      0x008u
#define STAGE90_USB_HWDEVICE    0x00cu
#define STAGE90_USB_HWTXBUF     0x010u
#define STAGE90_USB_HWRXBUF     0x014u
#define STAGE90_USB_SBUSCFG     0x090u
#define STAGE90_USB_AHBMODE     0x098u
#define STAGE90_USB_GENCONFIG   0x09cu

/* The capability header, read as one 32-bit word: the low 8 bits are `CAPLENGTH` (the offset of the
 * operational block) and the high 16 are `HCIVERSION`. `include/linux/usb/msm_hsusb_hw.h:23`. */
#define STAGE90_USB_CAPLENGTH_HCIVERSION 0x100u
#define STAGE90_USB_HCCPARAMS   0x108u
#define STAGE90_USB_DCIVERSION  0x120u

/* The operational block. `mach/msm_hsusb_hw.h:37-59`. */
#define STAGE90_USB_USBCMD      0x140u
#define STAGE90_USB_USBSTS      0x144u
#define STAGE90_USB_USBINTR     0x148u
#define STAGE90_USB_FRINDEX     0x14cu
#define STAGE90_USB_DEVICEADDR  0x154u
#define STAGE90_USB_ENDPOINTLISTADDR 0x158u
#define STAGE90_USB_BURSTSIZE   0x160u
#define STAGE90_USB_ULPI_VIEWPORT 0x170u
#define STAGE90_USB_ENDPTNAK    0x178u
#define STAGE90_USB_ENDPTNAKEN  0x17cu
#define STAGE90_USB_PORTSC      0x184u
#define STAGE90_USB_OTGSC       0x1a4u
#define STAGE90_USB_USBMODE     0x1a8u
#define STAGE90_USB_ENDPTSETUPSTAT 0x1acu
#define STAGE90_USB_ENDPTPRIME  0x1b0u
#define STAGE90_USB_ENDPTFLUSH  0x1b4u
#define STAGE90_USB_ENDPTSTAT   0x1b8u
#define STAGE90_USB_ENDPTCOMPLETE 0x1bcu
/* `USB_ENDPTCTRL(n)` = `0x1c0 + 4n`, `mach/msm_hsusb_hw.h:59`. Sixteen endpoint control words; this
 * probe reads the first four (`n = 0..3`) and not all sixteen, because the log's question is "is the
 * core's endpoint table reachable and what did the bootloader leave configured", and EP0..EP3 answer
 * it - a bootloader that configured a gadget leaves non-zero entries in the low four if anywhere. */
#define STAGE90_USB_ENDPTCTRL(n) (0x1c0u + 4u * (n))

/*
 * `USBMODE`'s two low bits, `mach/msm_hsusb_hw.h`'s `USBMODE_DEVICE 2` / `USBMODE_HOST 3` /
 * `(idle 0)`. Reading this register is the single most decisive cell in the probe: it says which mode
 * the core is in *right now*, and `2` therefore says the bootloader left it a device - the "the mode
 * sequence is a re-do" case (531 section 6) that 910a2's bring-up must not fight.
 */
#define STAGE90_USB_USBMODE_MASK 0x00000003u
#define STAGE90_USB_USBMODE_IDLE 0u
#define STAGE90_USB_USBMODE_DEVICE 2u
#define STAGE90_USB_USBMODE_HOST 3u

/* `PORTSC`'s connection and PHY-suspend bits (`include/linux/usb/msm_hsusb_hw.h:56-62`): `CCS` bit 0
 * is "current connect status" (a device *or* the cable is present on the host port), `PHCD` bit 23
 * is "PHY clock disable" (a core read with PHCD set has its PHY suspended). */
#define STAGE90_USB_PORTSC_CCS  0x00000001u
#define STAGE90_USB_PORTSC_PHCD 0x00800000u

/* `OTGSC`'s session/id bits (`include/linux/usb/msm_hsusb_hw.h:102-113`): `BSV` bit 11 is "B-session
 * valid" (VBUS present, the thing `msm_otg.c` waits on), `ID` bit 8 is the ID pin (0 = an A-cable or
 * an OTG host, 1 = a B-cable, i.e. a PC). For the micro-B port a PC plugs into, ID = 1 and BSV = 1
 * is the connected-to-a-host reading, and the pair is what 910a2 would have to see before it does
 * anything with the PHY. */
#define STAGE90_USB_OTGSC_BSV   0x00000800u
#define STAGE90_USB_OTGSC_ID    0x00000100u

/* `USB_ULPI_VIEWPORT`'s fields (`include/linux/usb/msm_hsusb_hw.h:64-71`): the read is a plain
 * register read of the viewport's *status*, not a viewport transaction - a transaction needs a WRITE
 * with `RUN` set, which is a bus wait and is 910a2's business. `SYNC_STATE` (bit 27) is the one bit
 * worth decoding here: an unsynchronised viewport is a viewport whose next transaction would be a
 * wait, so the probe records it rather than touching it. */
#define STAGE90_USB_ULPI_SYNC_STATE 0x08000000u

/*
 * The probe, called once from the first wrapped idle the handed-off kernel makes - the same site
 * `entry_storage_probe` uses and for the same reason: the live channel is only a console write's own
 * proof (`g_live_state`), which is exactly `entry_mmio_section`'s first refusal, so a device read is
 * meaningful only after that proof. **It is idempotent (`g_usb_probed`) and returns without touching
 * a USB register if the section it needs could not be installed**, so a build that calls it twice, or
 * a machine whose L1 slot is already occupied, cannot make it a fault. When `STAGE90_XNU_USB_PROBE`
 * is 0 the whole body compiles to a `return`, so the call site and the OFF arm are one source.
 *
 * **IT WRITES NOTHING.** The port may be live with the host (`adb`/`fastboot` were enumerating up to
 * the handoff), so this arm's entire output is `xnu_live_usb_*` keys. The writes - the PHY init
 * sequence, the device-mode transition, the event loop - are 910a2 and later, each a separate press.
 */
void entry_usb_probe(void);

/*
 * The one reading a *second* reader has to be able to compare, published as a named symbol the way
 * `g_stage90_gic_dist_typer` is (494): the OTG core's identification word is read-only and constant
 * for the life of the part, so a later stage that reaches the same core through a different mapping
 * (the OS-side nub, or the payload's own driver once it exists) compares two *ways of reaching one
 * register* rather than two moments in a value's life. A live key cannot be the right-hand side of
 * that comparison, and a constant typed into the driver would be a claim rather than a reading.
 */
extern uint32_t g_stage90_usb_id_cap;

/*
 * The mode word, likewise, and for the same reason sharpened: 910a2's first act must read the mode
 * *the bootloader left* before it writes one, and the number it must not fight is this one. Publishing
 * it under a name, next to the live key that carries it to the log, is one read into two channels.
 */
extern uint32_t g_stage90_usb_usbmode;

#endif /* STAGE90_ENTRY_USB_H */