# Experiment 910 — the USB-debug rung, mapped

Date: 2026-10-07

Status: **MAP** — no arm. This is the standing "map before the rung" step (the same shape as
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

## 7. What this map does NOT decide

The choice between **KDP-over-bulk** and **CDC-ACM** for the first live channel, and whether `adbd` is
worth building versus a kernel-print console. That is the operator's call, and it is a real fork: KDP buys
a debugger (read/write memory, breakpoints) but no shell; CDC-ACM buys a log stream but no control; `adbd`
buys both and is the most work. **The recommendation is to build 910a and 910b first** — they are the
shared prerequisite of all three and each is a pressable reading on its own.