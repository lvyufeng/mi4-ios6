# Experiment 910 — the USB-debug rung, mapped

Date: 2026-10-07

Status: **MAP + arm 1 (910a) and arm 2 (910a2) BUILT AND PARKED, NEITHER PRESSED.** `entry_usb_probe`
— the read-only USB2 OTG probe — is arm `armed-storage-377fb57a` (arm `cabba670` plus
`STAGE90_XNU_USB_PROBE=1`); `entry_usb_dev_init` — the PHY init + the device-mode transition, **the first
arm of this walk that writes to the port** — is arm `armed-storage-b3fbcd31` (910a plus
`STAGE90_XNU_USB_DEV=1`; **rebuilt 2026-10-07 from the first park `1138fdc6` to fix a dead-code defect at
the probes' call site — see §9.6**). The map below is §1-§7; **§8 is arm 1 and §9 is arm 2.** This is the standing "map before the rung" step (the same shape as
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
plus a **ChipIdea CI13xxx (MSM72K)** *gadget* controller (`ci13xxx_msm.c` → `ci13xxx_udc.c`). **This is not
an XNU-usable object** — it is a Linux driver built on `regulator`, `usb_phy`, and the gadget framework,
none of which XNU has.

**CORRECTION (910a): the UDC is a ChipIdea CI13xxx, not a DWC-OTG.** This section first called it a
"DWC-OTG gadget" — a guess from the family of Qualcomm USB cores, and wrong for this one. The device's own
checkout says `qcom,hsusb-otg`, `msm_otg.c`, `ci13xxx_udc.c`; there is no DWC register file here. So the
register layout §4 and §5 name below from the DWC lineage is **wrong**, and the real one is ChipIdea's
(`USB_ID_CAP`, `USB_USBCMD`, `USB_USBMODE`, `USB_ENDPTCTRL(n)`, `USB_ULPI_VIEWPORT` — the ChipIdea map
`include/linux/usb/msm_hsusb_hw.h` transcribes). The DWC3 (<code>DCFG</code>/<code>DCTL</code>/<code>GINTSTS</code>)
belongs to the *USB3 SS* core (`0xf9200000`), which the micro-B port does not use.

**This is the decisive difference from the watchdog.** The WDT (`0xf9017000`) shared the GIC's 1 MB block
and its install was refused. Both USB bases are in **fresh megabytes** — `0xf92` and `0xf9a` — owned by no
device the image has mapped, so the same `entry_mmio_section`-install shape that worked for the card and
the GIC applies here with no occupancy conflict. The rung is not blocked on address space.

## 4. The three files a USB-debug rung must produce, and where each comes from

1. **A device-controller driver.** Nothing to reuse; the **ChipIdea** device-mode programming sequence
   (the *ChipIdea* register set, not DWC's: `USB_USBMODE`, `USB_ENDPOINTLISTADDR`, the EP0
   `USB_ENDPTCTRL`/`USB_ENDPTPRIME`/`USB_ENDPTFLUSH` handling, `USB_USBCMD`/`USB_USBSTS`, the `USBSTS`
   event loop, the queue-head + dTD list). It is a *port*: the register semantics come from the Android
   source (`ci13xxx_udc.c`, `msm_hsusb_hw.h`), the structure from this project's own `st_*` ladder style
   (`src/entry/entry_storage.c`).
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

- **910a — the controller's identification and state (a READING).** Install `0xf9a55000`'s section and
  READ the ChipIdea core's identification and the mode the bootloader left it in — `USB_ID_CAP`,
  `USB_HWGENERAL`, `USB_USBMODE`, `USB_USBCMD`/`USB_USBSTS`, `USB_PORTSC`, `USB_OTGSC`, `USB_ENDPTCTRL`.
  **Writes NOTHING** (the port may be live with the host). Success = the `xnu_live_usb_*` block answers:
  is the core mapped (the 1 MB `0xf9a` slot free), what mode is it in (`USBMODE` 2=device/3=host/0=neither),
  what PHY. **BUILT: arm `377fb57a`.**
- **910a2 — the PHY init + the device-mode transition (the WRITES).** Run the vendor PHY init
  (`msm_otg.c`'s `usb_phy`/ULPI viewport sequence, the `hsusb-otg-phy-init-seq` table), then bring the core
  to **device mode** (`USB_USBMODE <- 2`, `USB_ENDPOINTLISTADDR`, `USB_USBCMD.RS`, `USB_USBINTR`). It must
  READ `USBMODE` first (from 910a) and not fight the mode the bootloader left — the "mode sequence is a
  re-do" cell. Success = `USB_USBSTS`/`USB_PORTSC` report a speed/connect and a device event fires.
  **No gadget, no protocol.** It is the first arm that WRITES to this device, so it lives in its own file
  (`entry_usb_dev.c`) and 910a's `entry_usb.c` stays provably write-nothing. **HAZARD: a bad write can drop
  the host's enumeration — but the escape is the physical press, not USB.**
- **910b — the polling event loop + a print channel.** Drive `USB_USBSTS`/`USB_USBINTR`, answer the
  enumeration's control transfers (GET_DESCRIPTOR, SET_ADDRESS, SET_CONFIGURATION), and expose **one IN
  endpoint** a host can read. Publish `xnu_live_usb_enum_*`. Success = the host (`lsusb`) sees a device
  with the image's VID/PID and can read the endpoint. This is the first rung that gives a *live* channel
  rather than a post-mortem.
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
`__wrap_Idle_load_context` (after `entry_storage_probe`), idempotent, gated on the install.

> **CORRECTION (2026-10-07, §9.6):** the sentence that stood here named `__wrap_Idle_load_context`, but the
> arm as FIRST BUILT (`377fb57a`) actually called the probe from `__wrap_platform_cache_idle_exit` — a
> **tail call** the record did not match — and that wrapper is never entered on the `IDLE_NO_SLEEP=1` arm.
> The probe was dead code; it is now genuinely in `__wrap_Idle_load_context`, pinned by a build refusal.

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
909 — `scripts/press_909_normal.sh` names `cabba670` and would need its `EXPECT_ARM` moved to `377fb57a`).
**BUT READ §9.6 FIRST: `377fb57a` as built calls its probe from the UNREACHABLE
`__wrap_platform_cache_idle_exit`, so a press of it logs NO `xnu_live_usb_*` key — the corrected arm that
carries both probes for real is `b3fbcd31`.**
(2) read the `xnu_live_usb_*` block out of TWRP's `/proc/last_kmsg`; (3) then build 910a2 (the PHY +
device-mode bring-up — the writes). **The goal is NOT met** — a read of the controller is not a debug
transport, and 910b (event loop + an enumerable endpoint) and 910c (KDP over bulk, the operator's choice)
are each a separate press, none built yet.


## 9. Arm 2 — 910a2: the PHY init and the device-mode transition, `entry_usb_dev_init` (arm `b3fbcd31`, first parked as `1138fdc6`)

**910a2 is 910a plus ONE key**, `STAGE90_XNU_USB_DEV=1`, exactly as 910a was 909's residence arm plus
one. The key gates a new file, `src/entry/entry_usb_dev.c` (with its own header `entry_usb_dev.h` and its
own guard `tools/check_usb_dev.py`), whose ON body is called once from `entry_trace.c`, immediately after
the 910a call. **910a's `entry_usb.c` is untouched, so its write-nothing property stays a property of
that file** — the reason for a separate file rather than adding stores to the probe.

### 9.1 The sequence, transcribed from the vendor, step by step

The order is the vendor's: `msm_otg.c`'s `msm_otg_reset` (the PHY) then `ci13xxx_udc.c`'s
`hw_device_reset` + the partial `hw_device_state` (the device mode). Each store names the Android
function that makes the same store:

| step | source | stores |
| --- | --- | --- |
| PHY reset, first half | `msm_otg_phy_reset` | clear `AHB2AHB_BYPASS` (bit 31 of `USB_AHBMODE 0x98`) **if set**; `USB_PORTSC = (PORTSC & ~(3<<30)) \| (3<<30)` — **PTS = ULPI** |
| link reset | `msm_otg_link_reset` | `USB_USBCMD = USBCMD_RST (2)`; poll `RST` clear ≤ 250 ms; `USB_PORTSC = 0x80000000`; `USB_AHBBURST (0x90) = 0` |
| (the vendor's `msleep(100)`) | `msm_otg_reset` | — |
| PHY POR ×2 | `usb_phy_reset` | `USB_PHY_CTRL (0x240)` bit 0 assert, ~12 µs, deassert — **run twice** |
| ULPI init | `ulpi_init`, the dtsi's `hsusb-otg-phy-init-seq` | ONE viewport transaction, `ulpi_write(phy, reg 0x81, 0x63)` |
| OTG enables | `OTG_PHY_CONTROL` branch | `USB_OTGSC \|= OTGSC_BSVIE (bit 27)`; `ulpi_write(ULPI_INT_SESS_VALID, reg 0x0d)` and the same to `0x10` |
| device mode | `hw_device_reset` | `USB_USBMODE` `CM_IDLE(0)` → `CM_DEVICE(2)` → `\| USBMODE_SLOM`; read-back asserted `[1:0] == 2`; `USBCMD.ITC(23:16) ← 0` |
| run | `hw_device_state` (partial) | `USBCMD.RS(bit 0) ← 1` |

**What it deliberately does NOT write, and publishes that it did not:** `USBINTR`,
`USB_ENDPOINTLISTADDR`, and EP0's `USB_ENDPTCTRL`. Those three are what `hw_device_state` does with a
**queue-head list**; this image has no `ci13xxx` gadget, no qh/dTD pool and no DMA-visible buffer for
endpoint 0, so `ENDPOINTLISTADDR` with no list behind it is a core told to DMA from nowhere. That is
910b. The arm publishes `_dev_intr = _dev_eplist = _dev_ep0_ctrl = 0` so the omission is a reading and
not a silence.

**The vendor collapses into ONE reset here.** `msm_otg_link_reset` and `hw_device_reset` both write
`USBCMD.RST`; on this path they are the same store, and doing it twice only widens the window in which
the host sees the controller come and go. The arm writes it once and publishes `_dev_rst_count = 1` so
the collapse is a reading and not a silent difference.

### 9.2 The hazard, named and bounded

`msm_otg_link_reset` writes `USBCMD.RST`, which resets the controller — **on a port the host is
enumerating that drops the enumeration** (recoverable: the host sees a disconnect–reconnect). A wedged
PHY is not recoverable, and the escape from a dark phone is the physical VolDown+Power press, not USB
(`[[mi4-device-dark-needs-power-press]]`), so `fastboot` stays reachable through the bootloader
regardless. The arm bounds the hazard two ways:

1. **It writes no USB register at all unless the core is ALREADY a device** (`USBMODE[1:0] == 2`). That
   is 531 §6's "the mode sequence is a re-do" case: the bootloader left the core in the mode the vendor
   driver wants, so the shaping is not what makes it a device — the reset is. Anything else means this
   arm does not understand the core's live state and it refuses rather than resets it blind. The opt-in
   `STAGE90_XNU_USB_DEV_FORCE=1` relaxes that gate for a deliberate experiment, and the build refuses
   `FORCE=1` with `DEV=0` (a relaxation with nothing to relax).
2. **Every wait is bounded by the vendor's own number** — `ULPI_IO_TIMEOUT_USEC = 10 ms` for a viewport
   transaction, the link reset's 250 ms — so a viewport or a reset that never completes is a number (the
   `ULPI_IO_TIMEOUT` sentinel / a nonzero `_dev_rst_polls` with `_dev_rst_cleared = 0`) and not an
   unending spin.

### 9.3 The mapping: reuse-of-record vs a fresh install

`entry_mmio_section` installs a 1 MB L1 section and **returns 0 if the slot is already occupied**. 910a
installs megabyte `0xf9a` first, so on a run with BOTH arms on this arm's own install returns 0 — and
then the slot is not empty, it holds **910a's desk**: `entry_section_install` refuses when
`(before & 0x3) != 0`, and 910a's section is a *block* descriptor (`type == 2`) whose physical-address
field is megabyte `0xf9a`. So the arm proves the occupant is 910a's own section (`usb_dev_section_is_ours`)
and reads on; **any other occupant leaves `_dev_owned = 0` and the arm reads NO USB register at all** —
the same refusal 910a makes when its own install is refused. The proof is the `slot_before` the mapper
already returns, so the hardcoded `mapped != 0` safety check the earlier probes carry does **not** apply
here (`_dev_mapped=0` beside `_dev_owned=1` is the expected dual-USB reading).

### 9.4 The guard, `tools/check_usb_dev.py`

Source half (switch-independent, runs in `make check` with no build and no device):
- 5 cross-offsets and 36 cross-values, each compared against the Android header/source that OWNS it —
  the owner's own expression, evaluated (`BIT(n)`, `(1<<n)`, `(3<<30)`, sums), `UL`/`U` suffixes stripped.
- a **write-target whitelist**: every `usb_dev_write32(<NAME>…)` first argument must be a defined
  `STAGE90_USB_*`; a numeric offset is refused, so a store to an untranscribed register cannot slip in.
- the **mode gate**, matched by exact FORM (not presence): the
  `!= STAGE90_USB_USBMODE_CM_DEVICE ) && ( STAGE90_XNU_USB_DEV_FORCE == 0 )` regex plus a `return`, and
  the init body's first store must be AFTER it. This was a real defect found while writing the guard: the
  first version checked only that `CM_DEVICE` appears, and `CM_DEVICE` appears 4× in the file — the
  mutation battery ACCEPTED a gate with `CM_DEVICE` removed. The form regex plus the `return` check refuse
  it.
- the published omissions `_dev_rst_count`/`_dev_intr`/`_dev_eplist` must be present.

Image half (needs a build, runs inside `build_entry.sh`): the switch read out of the linked ELF both
ways. Selftest: 5/5 mutations refused.

### 9.5 Readiness, and what is owed

`tools/verify_press_ready.sh` is **5/5 green** on `b3fbcd31`, the park verifies against the record
(11 members, `tools/verify_revert_set.sh` `VERIFIED`), and `make check` exits 0. **Row 4 needed its own
repair again**: 910a2 carries `STAGE90_XNU_USB_PROBE=1` too, so it would be caught by the 910a branch —
the new branch reads `STAGE90_XNU_USB_DEV` and, when it is set, SUPERSEDES the 910a text (placed after
it), naming the WRITE arm and its different consequence rather than the read.

**The payload record is byte-identical to every arm since 909** (`6c2b6038…`, the eleventh time): `USB_DEV`
is an ENTRY switch, so no payload switch moved. The entry record is 29 keys; the arm `xnu_arm_entry.bin`
is unchanged in LENGTH (6498612 bytes) — the ON body is added inside `.text`. **`STAGE90_XNU_SEAM_LR` did
NOT move this time** (`0x8004e2dc` held), so the entry build exited 0 with no seam-clause refusal —
unlike 910a, where adding the probe crossed a page.

### 9.6 The dead-code defect at the probes' call site, found 2026-10-07 before either USB arm was pressed

**What was wrong.** Both USB probes were called from exactly one site: `__wrap_platform_cache_idle_exit`
in `entry_trace.c`. **BOTH USB arms carry `STAGE90_XNU_IDLE_NO_SLEEP=1`** (it comes in from 909 arm 6,
which 910a is built on), and that key's entire design is that `cpu_idle` leaves by its *first door* on
every pass, so `platform_cache_idle_enter`/`wfi`/`exit` and their wrappers are **never entered**. So on
both USB arms the probes were **dead code**: a press would have logged **zero `xnu_live_usb_*` keys**,
which reads as "the probe did not run" — a silence that is indistinguishable from a never-reached site
(`[[mi4-silence-is-a-reading-only-if-success-is-silent]]`). This is
`[[mi4-a-lower-rungs-side-effect-poisoned-the-rung-above]]` one rung over: a lower arm's switch (909
arm 6's `IDLE_NO_SLEEP`) made an upper arm's site unreachable — and the call site's own comment still
asserted the probe ran.

**It was worse than one arm.** The already-**pushed** arm `377fb57a` (910a) carries the same defect: its
*record* claims the probe is called from `__wrap_Idle_load_context`, but the **artifact** shows a **tail
call** (`b 80013ffc <entry_usb_probe>` at `804d2e74`) inside the unreachable
`__wrap_platform_cache_idle_exit`. The prose was wrong; the ELF was the fact. (`1138fdc6` used a `bl`
from the same wrong wrapper.) So **both USB arms were dead code, not just 910a2.**

**The repair.** Both probes move to `__wrap_Idle_load_context` — the one wrapper every pass reaches on
EVERY arm (`xnu_entry_513` pins it to `machine_idle` plus `cpu_idle`'s two first-door bodies, no fourth
site) — placed BEFORE `__real_Idle_load_context`, which is `noreturn`, so a probe after it would never
run. **909 arm 6 already made this exact move for the watchdog pet**, for the same reason.

**The refusal that keeps it from coming back** — in `src/entry/build_entry.sh`, after the USB guards: it
reads the **linked ELF** and refuses when a probe whose switch is ON is called from any symbol other than
`__wrap_Idle_load_context`, matching **both `bl` and the tail-call `b` form**. The `b`-form is
load-bearing: the first version matched only `bl` and thereby read `377fb57a`'s tail-call probe as *no
call at all*, accepting an image whose only USB call sat in the unreachable wrapper — the matcher that
fails to match reads as "no call there", not as "not checked". It also refuses the count both ways
(a switch ON whose probe is absent, or a probe present with its switch off). **A comment saying where the
call belongs is not a check** (`[[mi4-a-claim-in-a-comment-is-not-a-check]]`); this reads the artifact.

**The switches did not move** — only the call SITE did. So the arm keys of `b3fbcd31`'s entry record are
IDENTICAL to `1138fdc6`'s down to `STAGE90_XNU_USB_DEV=1`/`_FORCE=0`; the arm's identity is its bytes
(`b3fbcd31…`), not its switches. The first park `armed-storage-1138fdc6` remains in the record as the
(pre-fix) arm it was; **it was never pressed**, so no press result is invalidated. The already-pushed
`377fb57a` (§8) remains a park too, but its press, when it happens, will log no `xnu_live_usb_*` — the
repaired arm is `b3fbcd31`.

**Owed, in order:** (1) the operator's press of `b3fbcd31` (910a2, the corrected arm — it now carries
**both** the read probe and the write arm, so one press answers the whole USB question) — a plain boot,
the same staging and escape as 909; `scripts/press_909_normal.sh`'s `EXPECT_ARM` is now
`armed-storage-b3fbcd31`. Press `377fb57a` only if the read-only probe alone is wanted (it will log no
USB key — see above). (2) read the `xnu_live_usb_dev_*` block out of TWRP's `/proc/last_kmsg` and compare
it against the `xnu_live_usb_*` block the same press carries; (3) then build 910b (the event loop + an
enumerable endpoint) and 910c (KDP over bulk). **The goal is NOT met** — a PHY init and a mode transition
answer no control transfer and enable no interrupt, so this is not yet a device the host can talk to.

## 10. The 910b reconnaissance — the EP0 control path, mapped (no arm built)

This is the standing "map before the rung" step (the same shape as `mi4-hfs-wiring-mapped`, 870/874,
before the HFS port was built): §5 named 910b as "the polling event loop + a print channel" and this
section is the reconnaissance that scopes it. It **builds nothing and presses nothing**; it is the
fact-base the arm will be designed against. Every line number is the device's own ChipIdea UDC,
`external/android_kernel_xiaomi_cancro/drivers/usb/gadget/ci13xxx_udc.c`, unless the header is named.

### 10.1 The loop is one ISR, and its priority is fixed

The vendor's entire device-mode runtime is **`udc_irq`** (`:3653`). One pass:

1. `intr = hw_test_and_clear_intr_active()` (`:785`) = `USBSTS & USBINTR`, and the ANDed value is
   **written back to `USBSTS`** — the ChipIdea status register is R/WC, so writing the read value clears
   exactly the latched bits (`hw_ctest_and_clear`, `:274`).
2. A fixed priority walk (`:3681`, comment: *"order defines priority - do NOT change it"*):

| bit | `USBi_*` | meaning | handler |
|---|---|---|---|
| 6 | `URI` | reset received | `isr_reset_handler` (`:2331`) |
| 2 | `PCI` | port change | `isr_resume_handler` (`:2383`) |
| 1 | `UEI` | USB error | **counted only, no handler** (`:3690`) |
| 0 | `UI` | transaction complete | `isr_tr_complete_handler` (`:2651`) |
| 8 | `SLI` | suspend | `isr_suspend_handler` (`:2405`) |

**`USBi_NAKI` (bit 16) is never enabled and never handled anywhere in the driver.** `USBINTR` is written
with bits `{0,1,2,6,8}` = **`0x00000147`** (`hw_device_state`, `:417-419`). A one-endpoint port can ignore
NAK entirely.

### 10.2 The three-stage control transfer, and the two "semaphores"

There is **no `_ep0_setup`/`_ep0_read`/`_ep0_write`** in this driver — the equivalent is a `switch` inside
the UI handler. The core writes the 8 SETUP bytes **into the EP0 qh at offset 32** (`qh->setup`, the
`struct ci13xxx_qh` `setup_data[8]`) and raises `ENDPTSETUPSTAT.bit0`; it also auto-clears the EP0-OUT qh
`td.token` status. The driver then:

```
:2689  if (mEp->type != XFER_CONTROL || !hw_test_and_clear_setup_status(i)) continue;
:2693  if (i != 0) { warn("ctrl traffic received at endpoint"); continue; }
:2702  _ep_nuke(&udc->ep0out); _ep_nuke(&udc->ep0in);           // drop the previous setup's leftovers
:2706  do {
:2707      hw_test_and_set_setup_guard();                       // USBCMD.SUTW = 1
:2708      memcpy(&req, &mEp->qh.ptr->setup, 8);                // read the 8 bytes under the guard
:2710      mb();
:2711  } while (!hw_test_and_clear_setup_guard());              // if HW cleared SUTW, a new setup raced: re-read
:2715  udc->ep0_dir = (type & USB_DIR_IN) ? TX : RX;
```

**Two distinct "semaphores" that are easy to conflate:**

- **`ENDPTSETUPSTAT`** — the setup-token semaphore. Cleared by writing the read value back. `hw_ep_prime`
  (`:582`) refuses to prime EP0-OUT while its bit is still set (`:586`/`:591` → `-EAGAIN`).
- **`USBCMD.SUTW` (bit 13)** — the setup-lockout guard around the `memcpy` (`:799`/`:810`). The retry loop
  exists so a setup arriving mid-read forces a re-read instead of a torn 8 bytes.

**Stage 2 (DATA).** Direction from `bRequestType` bit 7. The gadget's `->setup()` callback queues a request
on EP0; `ep_queue` (`:3083-3092`) redirects a length-bearing control request to the half matching
`udc->ep0_dir` (`ep0out` for RX, `ep0in` for TX), nuking any stale request on that half first.
`_hardware_enqueue` (`:1926`) fills the qh's `td` and primes via `hw_ep_prime` with `is_ctrl=1`.
**Stage 3 (STATUS).** `isr_setup_status_phase` (`:2522`) queues a zero-length request on the **opposite**
half (`mEp = (ep0_dir==TX) ? ep0out : ep0in`); a no-data request forces `ep0_dir=TX` (`:2844`) so status
lands on `ep0out`. The `UI`-side completion path is `:2673-2687`: on `ENDPTCOMPLETE(i)`, run
`isr_tr_complete_low`; for control, `err>0` (data moved) queues the status phase, `err<0` halts.

**Standard requests**, each ending in a status phase: `GET_STATUS` (`:2450`, 2 bytes into `status_buf`,
forced to 1 byte for `OTG_STATUS_SELECTOR`), `SET_ADDRESS` (`:2760` → `hw_usb_set_address`, `:821`,
`DEVICEADDR = (v<<25)|USBADRA`), `CLEAR/SET_FEATURE` (`:2720`/`:2775`), `SET_CONFIGURATION` (`:2771`,
only sets `udc->configured`; the real work is the gadget driver's `delegate` at `:2843`).

### 10.3 What 910a2 omitted, and what the minimum port must add

910a2 (`§9`) did `USBCMD.RST`, the PHY POR/ULPI writes, `USBMODE` `IDLE→DEVICE→|SLOM`, `USBCMD.ITC←0`,
`USBCMD.RS←1`, and **deliberately skipped `USBINTR`, `ENDPOINTLISTADDR`, and EP0 `ENDPTCTRL0`**
(`entry_usb_dev.c:416-420`). The minimum 910b port adds exactly those, in vendor form:

- **(a) `USBINTR ← 0x147`** (`hw_device_state`, `:417`).
- **(b) `ENDPOINTLISTADDR ← <phys addr of the EP0-OUT qh>`** (`:404`). The qh array is contiguous and the
  hardware indexes endpoint N as **`ENDPOINTLISTADDR + N*64`**; the vendor uses a dma_pool with **align 64,
  boundary 4096** (`:3452`), so the base and every qh must be **64-byte aligned**. For MSM72K EP0-IN's qh
  is at index `hw_ep_max/2` (the TX half), so it lands at `list + (hw_ep_max/2)*64`.
- **(c) `ENDPTCTRL(0) ← 0x00C000C0`** — the vendor never writes it literally (its `ep_enable` *skips*
  `hw_ep_enable` for `num==0`, `:2925`: "ep0 is always enabled"), so the reset-default is relied on; a
  bare-metal port must set it. Bits: `TXE|TXR|TXT=0` in the high half, `RXE|RXR|RXT=0` in the low half
  (`ci13xxx_udc.h:242-249`) → still control type, unstalled, toggle reset, both directions enabled.
- **(d) `USBCMD`** — 910a2 already set `RS`; re-asserting is harmless. The two runtime bits are
  `SUTW` (bit 13, §10.2) and `ATDTW` (bit 14, an "endpoint data-toggle write" interlock used only when
  appending to a non-empty queue, `:2038-2051`) — neither is needed until the endpoint has traffic.

**The bus-reset re-init is mandatory, not optional** (`hw_usb_reset`, `:835`): on every `URI` reset,
`DEVICEADDR←0`, `ENDPTFLUSH←~0`, `ENDPTSETUPSTAT←0`, `ENDPTCOMPLETE←0`, then drain `ENDPTPRIME→0` (100 µs
bound). Skipping it wedges EP0 permanently on the *second* enumeration — the host re-enumerates from
`USBMODE` state and stale semaphores never clear. This is the one "optional-looking" step that is not.

### 10.4 The qh and dTD, sized

`struct ci13xxx_qh` (`ci13xxx_udc.h:63-78`) is 48 bytes but the hardware stride is **64**:

| field | off | init |
|---|---|---|
| `cap` | 0 | EP0 = `QH_IOS(bit15) | (maxpacket<<16)` = `| 0x00400000` for 64 B → **`0x00408000`** |
| `curr` | 4 | read-only (HW fills the in-flight dTD addr) |
| `td` (embedded `ci13xxx_td`) | 8 | `td.next = TD_TERMINATE(bit0)` when idle |
| `RESERVED` | 28 | |
| `setup` | 32 (8 B) | the SETUP packet — read under `SUTW` |

`struct ci13xxx_td` (`:40-60`) is 28 bytes, `aligned(4)`: `next` (0), `token` (4), `page[5]` (8..27).
`_hardware_enqueue` builds it as `token = (length<<16) | TD_STATUS_ACTIVE | TD_IOC`; `next = TD_TERMINATE`;
`page[0] = phys(buf)`, `page[i] = (phys+i*4096) & ~0xFFF`. **The `page[]` low 12 bits are a byte offset
within the page**, so each 4 KiB span of the buffer must be physically contiguous and **cache-flushed
before prime** (`wmb()` at `:2007`, `:2107`); the core DMAs by **physical address**, and this entry image
is identity-mapped (`physBase==virtBase==0x80000000`), so a **static 64-byte-aligned qh array and dTDs in
`.bss` are directly DMA-able** — replacing the vendor's `dma_pool`/`kzalloc` entirely.

**The qh's embedded `td` is a link/status shadow**, not the real descriptor: `qh.td.next ← phys(dTD)`.
`hw_ep_prime(num,dir,is_ctrl)` writes `ENDPTPRIME = BIT(n)` with **`n = num + (dir?16:0)`** — EP0-OUT is
`BIT(0)`, EP0-IN is `BIT(16)`.

### 10.5 The one enumerable IN endpoint

Beyond EP0, 910b needs exactly one IN endpoint. The recurring pattern from `hw_ep_enable` (`:512-545`):

- `ENDPTCTRL(1)` high half = `TXE(bit23) | TXR(bit22) | TXT<<18`, `TXT = 2` for **bulk** (`0x03` is
  interrupt/iso by the low two bits; **`USB_ENDPOINT_XFER_BULK = 2`**, so `TXT = 2`, i.e. bits 18-19 = `10`)
  → e.g. **`0x00C80000`** (enable + toggle-reset + bulk), then `mb()`.
- A second qh at index 1 in the same array, `cap = QH_ZLT(bit29) | QH_MULT? | (maxpacket<<16)`; bulk uses
  `QH_ZLT` (the vendor ORs it in `_hardware_enqueue` `:2104`).
- An IN dTD whose `page[0]` names the buffer the host will read; prime with `BIT(1+16)=BIT(17)`.

**Descriptors** come from the vendor tree, and the values matter only as the *shape*: `android.c` carries
`VENDOR_ID 0x18D1` / `PRODUCT_ID 0x0001` as **compile-time defaults** that Google's adb driver overrides at
runtime (`idVendor 18d1`, `idProduct d00d` for the adb-only config) — so a bare-metal port should pick its
own **stable** `ID_VENDOR`/`ID_PRODUCT` pair and a `bcdDevice`, not chase the runtime values. The
descriptors required for a host to enumerate are: **device** (18 B), **config** (9 B) + the interface's
**endpoint** descriptor (7 B), and the **string** set (`iManufacturer`/`iProduct`/`iSerialNumber`). The
`GET_DESCRIPTOR` answer is a `memcpy` out of a static table, with `wLength` clamped and a short/zero-length
status handled by §10.2's status phase.

### 10.6 The reduction, honestly stated

What a minimum one-IN-endpoint port can **omit** (from the vendor's 3890 lines): `USBi_NAKI` entirely;
the `UEI`/suspend/resume/remote-wakeup machinery; all debugfs/`dbg_*`/`isr_statistics`/tracepoints; the
`ep_prime_timer` robustness watchdog (`:1860`); `dma_pool` and the `ci13xxx_req` request-list machinery
(a single request replaces `_ep_nuke`/`ep_dequeue`/`isr_tr_complete_low`'s walk); multi-request and
`CI13XXX_PAGE_SIZE*4` chunking (bulk >16 KiB only); the MSM SPS vendor-DMA mode (`MSM_ETD_*`,
`CAP_ENDPTPIPEID`); `AHB2AHB_BYPASS`; halt/wedge; test modes and OTG SRP/HNP; every endpoint but 0 and
the one IN. What it can **not** omit is §10.3(d): the bus-reset re-init of `ENDPTSETUPSTAT`/`ENDPTCOMPLETE`/
`ENDPTFLUSH`/`ENDPTPRIME`.

**This is a large rung** — a from-scratch EP0 control-transfer state machine plus one endpoint, against a
driver written for Linux's `usb_request`/`dma_pool`/workqueue world. The map above is the raw material;
the arm's own design (the static qh/dTD layout, the ISR body, the descriptor table, the guard) is the next
step, and it is worth its own document before a line is written — the same discipline §9.6 enforced for the
*small* USB arm.

### 10.7 What 910b still does not close

A host that enumerates and can read one endpoint is a **live channel**, not a debug transport: reading a
buffer the kernel chose to publish. The goal clause 「可以通过usb进行调试」 needs either 910c (KDP over
that endpoint, or a CDC-ACM whose `printf` reaches a terminal) or 910d (`adbd`, the literal clause, which
sits on 910b's endpoint). 910b is the rung that makes any of them possible; it is not yet the goal.

### 10.8 The descriptor, endpoint and PHY values, from the vendor tree (exact)

§10.5 gave the *shape*; this is the vendor's literal values, so the arm's tables are transcriptions with
a named owner rather than inventions (`[[mi4-one-value-two-definitions]]`). Source: the Android composite
gadget `drivers/usb/gadget/android.c` + `f_adb.c`, the UDC `ci13xxx_udc.c`, and the PHY `drivers/usb/otg/
msm_otg.c`, all under `external/android_kernel_xiaomi_cancro/`.

**The endpoint array is 32 entries, and direction is a half offset.** `hw_ep_max = DCCPARAMS.DEN × 2`
(`ci13xxx_udc.c:315-319`); on this IP `DEN=16` physical endpoints, so **`hw_ep_max = 32`** (16 RX + 16 TX
halves), capped at `ENDPT_MAX = 32` (`ci13xxx_udc.h:26`). `ep0out = ci13xxx_ep[0]`, `ep0in =
ci13xxx_ep[hw_ep_max/2] = ci13xxx_ep[16]`. **`USB_ENDPTCTRL(n)` is one word per *number*** with RX in the
low half and TX in the high half — so EP1-IN and EP1-OUT are the two halves of `ENDPTCTRL(1)`, and the
IN half is not a separate register. The address a host sees is `(_usb_addr`) `0x80|num` for IN, `num` for
OUT (`ci13xxx_udc.c:1855-1858`).

**The reference interface** (`f_adb.c`, the ADB function) — the shape 910b should mirror even though it
needs only one IN endpoint: `bInterfaceClass 0xFF`, `SubClass 0x42`, `Protocol 1` (`f_adb.c:67-69`, the
Google-ADB signature), `bNumEndpoints = 2`, **bulk**, `wMaxPacketSize` **512 high-speed / 64 full-speed**
(`f_adb.c:106-135`). The two endpoint addresses are assigned by `usb_ep_autoconfig` (`f_adb.c:261-289`),
so the ported table reproduces the interface and lets its own EP0/EP1 choice stand; a single-config port
needs only the FS (or HS) descriptor pair.

**The device descriptor** (`android.c:269-278`, 18 bytes): `bcdUSB 0x0200`, `bDeviceClass
USB_CLASS_PER_INTERFACE (0)`, `bNumConfigurations 1`, and — the fields that are runtime-mutable via sysfs
(`android.c:2539-2547`) and therefore board-decided, **not** compile-time facts:

- `idVendor`/`idProduct` defaults `0x18D1`/`0x0001` (`android.c:107-108`), but the board's own
  `init.qcom.usb.rc` writes `05C6:901D` and the adb-only config sets `18d1:d00d` — so a bare port must
  **pick its own stable pair** and not chase a runtime value.
- `bcdDevice` = `0x0200 + gcnum` at bind (`android.c:2693-2700`).
- `bMaxPacketSize0` is **not** in the struct: it comes from `CTRL_PAYLOAD_MAX = 64` (`ci13xxx_udc.h:27`),
  set as `ep0in/out.ep.maxpacket = 64`.
- **`iSerialNumber`**: the kernel default is the literal `"0123456789ABCDEF"` (`android.c:2681`); the
  phone's `4a2fe00b` appears only because a userspace script echoes `ro.serialno` into the sysfs
  `iSerial` node (`init.qcom.usb.sh:36-47`). **A bare port that boots the gadget gets the placeholder,
  not the CID — the serial string must be hard-coded to be the phone's.**
- Strings: `iManufacturer`/`iProduct` default `"Android"`; ids are assigned dynamically by
  `usb_string_id` at bind (`android.c:2663-2688`); `iConfiguration` is 0 (not defined). No `iSerialNumber`
  → host reads nothing there, which is fine for enumeration.

**The PHY facts** (from the device's own dtsi, `arch/arm/boot/dts/msm8974.dtsi` `usb_otg` `:267`):
`qcom,hsusb-otg-phy-type = <2>` = **`SNPS_28NM_INTEGRATED_PHY`** (`include/linux/usb/msm_hsusb.h:95-102`);
`qcom,hsusb-otg-phy-init-seq = <0x63 0x81 0xffffffff>` — **exactly one ULPI write, value `0x63` to register
`0x81`** (the `0xffffffff` is `ulpi_init`'s sequence sentinel, `msm_otg.c:427-438`). These are the two
values 910a2 already transcribed (`entry_usb_dev.h:125-129`), which is the cross-check that the map and the
arm agree. The regulators the vendor enables are `HSUSB_1p8` / `HSUSB_3p3` / `HSUSB_VDDCX`
(`msm_otg.c:108-109`, `:181-193`) — RPM-SMD resources, **not touched by this image's ladder**
(`[[mi4-vendor-path-powers-the-card]]`), and 910a2's success/failure reading is what says whether they
matter here.

**One correction to §10.5, from the same source:** the reference function has **two** bulk endpoints, not
one. 910b's own requirement is one *enumerable IN* endpoint (§5); the descriptor may advertise two and
simply never service the OUT until 910c, or advertise one — a design decision for the arm, not a fact.
