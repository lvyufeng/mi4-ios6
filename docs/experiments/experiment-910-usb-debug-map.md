# Experiment 910 — the USB-debug rung, mapped

Date: 2026-10-07

Status: **MAP + arm 1 (910a) BUILT AND PARKED, NOT PRESSED.** `entry_usb_probe` — the read-only USB2
OTG probe — is arm `armed-storage-377fb57a` (arm `cabba670` plus `STAGE90_XNU_USB_PROBE=1`). The map
below is §1-§7; **§8 is the arm.** This is the standing "map before the rung" step (the same shape as
`mi4-hfs-wiring-mapped`, 870/874, before the HFS port was built): the goal's added requirement is

> 「保持在xnu里，可以通过usb进行调试」 — a resident XNU must also be *debuggable over USB*.

Reconnaissance (a read-only sweep of `external/xnu-4570.1.46`, `src/`, and
`external/android_kernel_xiaomi_cancro`) answers what this rung can reuse and what it must build. The
verdict is one sentence: **there is no USB code in the XNU tree at all, and the hardware's USB2 controller
is served by an Android-only OTG/PHY stack — so the load-bearing work is a controller + PHY + gadget
sequence, and the protocol/console layers above it mostly exist already.**

## 1. What is genuinely absent

- **No USB subsystem in XNU 4570.** `iokit/Families/` holds only `IOSystemManagement` and `IONVRAM`;
  `bsd/dev/` has no `usb`; `grep -rl 'IOUSBFamily\|AppleUSB\|IOUSBHost\|IOUSBDevice' external/` → **zero
  hits**. `grep -ril 'dwc3\|dwc_usb\|chipidea\|msm_hsusb'` over the repo → **zero hits**. There is nothing
  to enable, patch, or recompile into USB support: it does not exist in this tree.
- **No USB code in the payload.** `grep -rnw -i -e usb -e udc -e dwc -e adb src/` returns only host-side
  observations (the ADB transport id advancing, `18d1:d00d` enumeration timestamps in `entry_trace.c:712`)
  and narrative in `src/stage90.h:4073-4075`. No `udc`, no `dwc`, no `adbd`.

## 2. What exists and is reusable

- **KDP — the kernel debug protocol — is fully present in the XNU source and deliberately NOT compiled.**
  `osfmk/kdp/` carries `kdp.c`, `kdp_core.c`, `kdp_serial.c`, `kdp_udp.c`, `processor_core.c`,
  `kdp_protocol.h`, `kdp_en_debugger.h`, and `osfmk/kdp/ml/arm/{kdp_machdep.c,kdp_vm.c}`. The build's gate
  is `mach_kdp` / `config_serial_kdp` (`osfmk/conf/files:86-91`, `files.arm:61-62`), and
  `tools/xnu_config/minimal/STAGE90_BOOT.local:21` lists them in `PERF_DBG_BASE` — a set that is **not** in
  the assembled `STAGE90_BOOT` (`:30`), so the image compiles the **disabled** arm:
  `build_entry.sh:5318-5323` — *"`kdp_init`, `kdp_register_send_receive`, … `kdp_unregister_link` are each
  a bare `bx lr`, and `kdp_get_interface`/`kdp_get_ip_address` are `mov r0, #0; bx lr`."* The manifest
  links **one** KDP object, `osfmk_kdp_kdp_udp.o` (`build_entry.sh:22046`, `:27210`, `:27637`) — the
  all-`bx lr` stub. `kdp_core`/`kdp_serial`/`kdp_machdep`/`kdp_vm` are not compiled.
- **The serial transport KDP would bind to already exists.** `osfmk/arm/pal_routines.c:41-59` implements
  `pal_serial_*`, declared in `pal_routines.h:46-49`, routed to the pexpert polled UART
  `pexpert/arm/pe_serial.c` (`uart_putc:813`, `uart_init`, `uart_initted` gate `:41`). The payload already
  **wraps `uart_putc`** for measurement (`src/entry/entry_trace.c:4148-4186`) but `uart_initted` never
  becomes true, so OS text has never reached a UART (`entry_trace.c:4151`, `entry_stubs.c:2586`).
- **The real console today is a RAM ring, not a channel.** `src/ram_console.c` writes into
  `RAM_CONSOLE_BASE 0xde500000`, 2 MiB, signature `'DBGC'` (`src/stage90.h:13-15`); the `xnu_live_*` keys
  go through `entry_write_kv`/`entry_live_write` (`entry_stubs.c:2526-2538`) into the same buffer, which
  TWRP later reads as `/proc/last_kmsg`. This is the escape, not debugging — it is read *after* the run.
- **The userland ABI is already exercised.** `src/entry/entry_ramdisk.s` (`entry_code:1067`) runs pid 1:
  `getpid → mmap → poll×2 → open → read → open(ENOENT) → fork → exit → wait4×2 → open → write → fsync →
  park`. `fork`/`poll`/`open`/`read`/`write` all return, so a debug client (or an `adbd`-like process)
  can ride the same `/sbin/launchd` slot. The fixture Mach-O (`stage90_fixture.macho`, 1744 B) is
  explicitly inert, and the HFS+ root is built by `tools/build_hfs_root_image.sh` into
  `src/entry/blob/xnu_arm_entry_root_hfs.img` (524288 B).

## 3. What the hardware is (from the device's own Android checkout)

`external/android_kernel_xiaomi_cancro/arch/arm/boot/dts/msm8974.dtsi` names **two** USB controllers:

| node | compatible | reg | the port it serves |
|---|---|---|---|
| `usb_otg: usb@f9a55000` (`:267`) | `qcom,hsusb-otg` | `0xf9a55000 0x400` | **USB2 OTG** — the micro-B port a PC/`adb`/`fastboot` uses |
| `qcom,ssusb@f9200000` (`:1660`) | `qcom,dwc-usb3-msm` + `dwc3@f9200000` | `0xf9200000 0xfc000` | USB3 SS — unused by the micro-B port |

The USB2 controller is driven by an **Android-only** stack: `drivers/usb/otg/msm_otg.c` (the OTG/PHY:
regulators `HSUSB_1p8/3p3/VDDCX`, `ULPI` viewport, `hsusb-otg-phy-init-seq = <0x63 0x81 …>` from the dtsi)
plus a DWC-OTG *gadget* controller. **This is not an XNU-usable object** — it is a Linux driver built on
`regulator`, `usb_phy`, and the gadget framework, none of which XNU has.

**This is the decisive difference from the watchdog.** The WDT (`0xf9017000`) shared the GIC's 1 MB block
and its install was refused. Both USB bases are in **fresh megabytes** — `0xf92` and `0xf9a` — owned by no
device the image has mapped, so the same `entry_mmio_section`-install shape that worked for the card and
the GIC applies here with no occupancy conflict. The rung is not blocked on address space.

## 4. The three files a USB-debug rung must produce, and where each comes from

1. **A device-controller driver.** Nothing to reuse; the DWC-OTG programming sequence (device mode:
   `DCFG`, `DCTL`, `DEVTEN`, the EP0/EP1-4 `DIEPCTL*/DOEPCTL*` registers, `USBCMD`/`USBSTS`, the `GINTSTS`
   event loop, the FIFO/DMA split). It is a *port*: the register semantics come from the Android source,
   the structure from this project's own `st_*` ladder style (`src/entry/entry_storage.c`).
2. **A PHY/clock/power init.** The dtsi's `hsusb-otg-phy-init-seq = <0x63 0x81>` and the ULPI viewport
   writes from `msm_otg.c`. The regulators are the same RPM-SMD family the card rung already reaches
   (`mi4-vendor-path-powers-the-card`) — `HSUSB_1p8/3p3` join `sdhci-msm`'s rails on the ladder's power
   path.
3. **A gadget or a bulk transport.** For *debugging*, the cheapest is **KDP over a bulk endpoint** (the
   protocol engine exists) or a **USB-serial CDC-ACM** channel a host `minicom`/`screen` opens. An
   `adbd`-equivalent (the goal's literal 「通过usb进行调试」) is a *second* rung on top: `adb` needs the
   ADB protocol over bulk endpoints plus a daemon, which is a userland program on the HFS root, not a
   kernel driver.

## 5. The recommended ladder (each rung one pressable arm)

- **910a — the controller comes up (device mode).** Install `0xf9a55000`'s section, run the vendor PHY
  init, bring the core to device mode, and publish `xnu_live_usb_*` (`_base`, `_hwsection`, `_phy`,
  `_devctl`, `_speed`, `_intr`). Success = the core reports a speed/connection and the first `GINTSTS`
  device event fires. **No gadget, no protocol** — this rung is a *reading* that the controller and PHY
  answer, exactly as rung A read the card.
- **910b — the polling event loop + a print channel.** Drive `GINTSTS`, answer the enumeration's control
  transfers (GET_DESCRIPTOR, SET_ADDRESS, SET_CONFIGURATION), and expose **one IN endpoint** a host can
  read. Publish `xnu_live_usb_enum_*`. Success = the host (`lsusb`) sees a device with the image's VID/PID
  and can read the endpoint. This is the first rung that gives a *live* channel rather than a post-mortem.
- **910c — KDP over that endpoint (or a CDC-ACM).** Swap `kdp_udp.o`'s disabled stub for the real
  `kdp_core`/`kdp_serial` (or a USB transport), add them to the manifest, and flip `mach_kdp` into
  `STAGE90_BOOT`. Success = a host debugger can `read`/`write` kernel memory over USB. **Or** the simpler
  sibling: enumerate as CDC-ACM and let the kernel's `printf` reach a host terminal.
- **910d — `adbd`.** The ADB protocol + a userland daemon on the HFS root. This is the literal goal clause
  and the largest; it sits on 910b's endpoint, not on KDP.

## 6. The honest risks, named

- **The controller may be held by the bootloader.** `fastboot`/`adb` were live up to the handoff, so the
  USB2 core is *powered and possibly already in device mode* — this is like the SDHCI "the mode sequence
  is a re-do" cell (531 §6). 910a must read the core *before* writing it (`xnu_live_usb_state`), and a
  reset that drops the host's enumeration is a genuine hazard: a wrong `USBCMD.RST` on a live port can
  lose the very channel the operator would use to recover — **but the escape here is the physical
  VolDown+Power press, not USB**, so fastboot stays reachable through the bootloader regardless.
- **The PHY's ULPI viewport is a second device window** (the `msm_otg.c:63` `ULPI_IO_TIMEOUT_USEC` path);
  it needs its own section install, and a viewport read that hangs is a bus wait (the 692 class).
- **The regulators are shared with the card.** Enabling `HSUSB_1p8/3p3` must not disturb the SDHCI rails
  the mount depends on; the ladder's power path must add, not replace.
- **Residence is a prerequisite.** A live debug channel is only useful once 909 makes the boot *stay*;
  until then the channel would go dark with the run. This makes **the 909 press the first thing to do**,
  and 910 a rung that *builds* while 909 is being pressed.

## 7. The fork, and the operator's decision

The choice between **KDP-over-bulk** and **CDC-ACM** for the first live channel, and whether `adbd` is
worth building versus a kernel-print console, is the operator's. **Decided 2026-10-07: KDP.** So 910c is
KDP over a USB bulk endpoint (the engine is in-tree, `osfmusb_kdp_kdp_udp.o`'s disabled stub is swapped for
the real `kdp_core`/`kdp_serial` + a USB transport, and `mach_kdp` is flipped into `STAGE90_BOOT`), not
CDC-ACM. `adbd` (910d) remains the literal goal clause and sits on 910b's endpoint afterward.

**The first arm is a READ, not a bring-up.** The established pattern for a new device in this project is a
read-only probe before any write (692's storage probe; rung A for the card). USB is the highest-stakes
device so far — the port may be live with the host — so **910a is `entry_usb_probe`: install
`0xf9a55000`'s 1 MB section and READ the core's identification and state registers, publishing
`xnu_live_usb_*`, writing NOTHING.** It answers: is the core mapped; what mode did the bootloader leave it
in; is it already in device mode (the SDHCI "the mode sequence is a re-do" cell). 910a2 then does the PHY
+ device-mode bring-up (the writes). Both are pressable arms and the read arm carries near-zero hazard.

**§7 originally named this arm with DWC3 registers — `HWVERSION`, `CAP`, `DCFG`, `DCTL`, `GINTSTS`,
`DSTS`. That was the map's own error, corrected by the arm (see §8): this core is a ChipIdea CI13xxx,
and reading a DWC map off it would have made every number a reading of the wrong file's bytes.** The
registers the arm actually reads are ChipIdea's — `USB_ID_CAP`, `USB_HWGENERAL`, `USB_HWDEVICE`,
`USB_CAPLENGTH`/`USB_HCIVERSION`, `USB_USBCMD`, `USB_USBSTS`, `USB_USBMODE`, `USB_PORTSC`, `USB_OTGSC`,
`USB_ENDPTCTRL(n)`, `USB_ULPI_VIEWPORT` — transcribed in `src/entry/entry_usb.h`, each offset owned by
`external/android_kernel_xiaomi_cancro`'s `msm_hsusb_hw.h` and cross-checked there by `tools/check_usb_probe.py`.
## 8. Arm 1 — 910a: the read-only probe `entry_usb_probe` (arm `377fb57a`)

**Built and parked, NOT pressed. The press is the operator's.** The arm is `armed-storage-377fb57a`
(11 members in `out/stage90/frozen/armed-storage-377fb57a/`, recorded in `records/revert-set.txt`),
which is 909's residence arm `cabba670` plus ONE switch, `STAGE90_XNU_USB_PROBE=1`. Nothing else moved:
the payload's own switch record is byte-identical (`6c2b6038…`, now the **ninth** arm in a row), and the
entry record is arm 6's 26 keys plus `STAGE90_XNU_USB_PROBE=1`.

### 8.1 The controller is a ChipIdea CI13xxx, not a DWC-OTG — the map's error, corrected

910's map (§3) called the USB2 port a "DWC-OTG gadget". The device's own checkout says otherwise three
ways: the node's `compatible` is `qcom,hsusb-otg` (`arch/arm/boot/dts/msm8974.dtsi`, `usb@f9a55000`),
`msm_otg.c` is the OTG/PHY wrapper, and the *gadget* is `drivers/usb/gadget/{ci13xxx_msm.c,msm72k_udc.c}`
→ `ci13xxx_udc.c` — none of which is a DWC register file. The DWC3 map belongs to the *other*
controller (`0xf9200000`, USB3 SS), which this arm reads for its identification only and treats as a
different part. **Reading a DWC map off this core would have made every number a reading of the wrong
file's bytes**, which is why the arm's first act is a read with the map's owner named in the record.

### 8.2 The three guard clauses (`tools/check_usb_probe.py`)

1. **One value, two definitions.** `entry_usb.h` cannot include the device's Linux headers, so all 30
   register offsets are transcribed; the check compares each, both directions, against the Android
   header that owns it (`MSM_USB_BASE + N`), and checks the base against the `msm8974.dtsi` node.
2. **The write-nothing property, at the source.** Comment-stripped, `entry_usb.c` must define no write
   accessor and take no store through the window — a store is a `*(volatile …)` that is not a `return`.
3. **The switch reached the image, both ways.** Read `STAGE90_XNU_USB_PROBE` out of the arm's record and
   the linked ELF: with it 1 the body must be a real `T` body reading the `0xf9a5` window, with it 0 a
   bare return. The image half runs **after** the record writer in `build_entry.sh` (a check before it
   compares the image against the *previous* arm's record — that is what the first 910a ON build refused
   with, and the placement is why it is where it is now).

The battery is 4 mutations (one per failure mode) and all 4 are refused.

### 8.3 The entry-group page move, and the seam re-pin (both copies)

Adding the probe's ON body to the entry group pushed it past a page boundary, so the build's own clause
refused — the exact refusal 905's programming wait produced:
`the exit's call to FlushPoU_Dcache is at 2147803864 and returns to 2147803868` = **`0x8004e2dc`**. So
`STAGE90_XNU_SEAM_LR` was re-derived `0x8004d2dc → 0x8004e2dc` (+0x1000) in `entry_trace.c` **and**
`EXIT_POP_LR_LITERAL` in `scripts/run_and_capture.sh` — both copies, one edit each. **The value is a
function of the entry group's size and not of any rung's meaning.** 908 had moved it *back* to
`0x8004d2dc` without updating the comment thread; this move restored the value and the comment now
records the history (`0x8004e2dc` at 905 → `0x8004d2dc` at 908 → `0x8004e2dc` here).

### 8.4 What the run would be

The same four-part probe shape as `entry_storage.c`/`entry_gic.c`, called once from
`__wrap_Idle_load_context` (after `entry_storage_probe`), idempotent, gated on the install:

- **(1) the mapping first.** `entry_mmio_section(0xf9a55000, …)` installs the 1 MB section for
  megabyte `0xf9a`, which no other device this image maps owns (the *opposite* of the watchdog's fate at
  `0xf9017000`, where the WDT and the GIC share `0xf90` and the install was refused). A 0 result
  publishes `xnu_live_usb_mapped=0` and reads no USB register at all.
- **(2) identification.** `USB_ID_CAP`, `HWGENERAL` (decoded `PHYW`), `HWDEVICE`, `CAPLENGTH`/`HCIVERSION`,
  `DCIVERSION`, `HCCPARAMS`.
- **(3) the mode the bootloader left.** `USBMODE` and its decoded bits (2=device, 3=host, 0=neither —
  the "mode sequence is a re-do" cell 910a2 must read before it writes), `USBCMD`/`RST`, `USBSTS`,
  `FRINDEX`, `DEVICEADDR`, `ENDPOINTLISTADDR`, `BURSTSIZE`, `ULPI_VIEWPORT` (read-only; a transaction is
  a WRITE and belongs to 910a2), `PORTSC`/`CCS`/`PHCD`, `OTGSC`/`BSV`/`ID`.
- **(4) the endpoint table, shallowly.** `ENDPTCTRL(0..3)` and `_epctrl_any`.
- then the **USB3 SS** core's identification only (`0xf9200000`, a DWC3 — `GSNPSID`/`GHWPARAMS0`).

**Everything is a `volatile` 32-bit read; the arm writes NOTHING.** The keys are `xnu_live_usb_*`, with
the SS core's under `xnu_live_usb_ss_*` so they are never read as the OTG core's. `xnu_live_usb_loaded=1`
is the last key and is the "read completed" marker.

### 8.5 Readiness, and what is owed

`tools/verify_press_ready.sh` is **5/5 green** on `377fb57a`, and `make check` exits 0. **Row 4 needed a
repair to get there**: it names the arm by the ELF reading joined to the record, but the USB switch is an
*entry* switch that changes no reachability sentence and leaves `STAGE90_XNU_STORAGE_PROBE=60` — so row 4
printed `ok` while naming the rung-61 CARD arm for a USB press. The row now reads `STAGE90_XNU_USB_PROBE`
and, when it is 1, names the 910a arm (this file's own subject matter, repaired the way 683/686/690/692/716
repaired it one switch over).

**Owed, in order:** (1) the operator's press of `377fb57a` (a plain boot, the same staging and escape as
909 — `scripts/press_909_normal.sh` names `cabba670` and would need its `EXPECT_ARM` moved to `377fb57a`);
(2) read the `xnu_live_usb_*` block out of TWRP's `/proc/last_kmsg`; (3) then build 910a2 (the PHY +
device-mode bring-up — the writes). **The goal is NOT met** — a read of the controller is not a debug
transport, and 910b (event loop + an enumerable endpoint) and 910c (KDP over bulk, the operator's choice)
are each a separate press, none built yet.
