# 959 — the first D13 USB probe arm (2026-10-09)

The goal's clause 3 「可以通过usb进行调试」 advanced onto the Darwin-13 line. Every prior USB arm —
`cabba670`/`377fb57a`/`b3fbcd31` (910a/910a2), `880a3568`/`979ded08` (910b), `77a51d33`/`6deae815`
(910c) — is built against **4570**. Since [[mi4-913-ios7-rebase-decision]] the working line is D13, so
none of them can be pressed as a D13 arm. This rung builds the first one.

## What it is

The 958 arm (the 3 GB memory total) **plus one key**: `STAGE90_XNU_USB_PROBE=1`. Nothing else moved:
the D13 kernel pool is the `STAGE90_XNU` kernel, the payload switch set is unchanged (`6c2b6038`),
because `USB_PROBE` (like `MEM_TOTAL`) is an **ENTRY build parameter** — it reaches the payload through
the generated header, not a payload switch ([[mi4-build-variant-comes-from-an-env-default]]).

## Why nothing needed building

951 already made the USB/SMEM probes' **call site follow the tree**: on 4570 they are called from
`__wrap_Idle_load_context`, on D13 from `__wrap_machine_idle` (937 compiles the idle-load wrapper out;
D13 ships no `cpu_idle`). The D13-only copy of the five-probe seam sits in `entry_trace.c` immediately
before `__real_machine_idle()`, calling the same five probes in the same order. So the arm is one
switch on the already-working site, and the `xnu_entry_910` clause **confirms it from the linked
image** rather than trusting the command line:

```
xnu_entry_910: the USB probes are called from `__wrap_machine_idle` and nowhere else
               (read_probe=1, write_arm=0, enum=0, stream=0)
```

`read_probe=1` is the whole point: the clause reads the **linked ELF** and finds exactly one `bl
entry_usb_probe` from the D13 idle site and zero from anywhere else. A probe left at the dead 4570
site would be refused (`[[mi4-silence-is-a-reading-only-if-success-is-silent]]`).

## The arm is read-only

`src/entry/entry_usb.c`'s probe maps the USB2 OTG register window and reads it — `ID_CAP`, `HWGENERAL`,
`HWDEVICE`, `HWTXBUF`, `HWRXBUF`, `CAPLENGTH`, `HCCPARAMS`, `DCIVERSION`, `USBMODE`, `USBCMD`,
`USBSTS`, `USBINTR`, `FRINDEX`, `DEVICEADDR`, `ENDPOINTLISTADDR`, `BURSTSIZE`, … — **it writes
nothing** (the file's own contract, and the reason 910 was laddered). On a device the host has already
enumerated (`18d1:d00d`), a wrong write — a `USBCMD.RST`, a PHY reset — could drop the host's
enumeration; a read cannot.

## The arm

`armed-d13-96f53c02` (entry bin `96f53c02`, `STAGE90_XNU_TREE_D13=1 STAGE90_XNU_MEM_TOTAL=1
STAGE90_XNU_USB_PROBE=1`):

- `entry_usb_probe` at `0x80013d1c` in the linked ELF, and — from 958 — `entry_xnu_mem_total_arm_on`
  at `0x8001d5d8`.
- qcdt `5bd897d2…`.
- **PARKED, NOT PRESSED.** `make check` 0; `verify_press_ready` 5/5.

## The reading a press looks for

The `xnu_live_usb_*` keys the probe publishes (`_id_cap`, `_hwgeneral`, `_hwdevice`, `_hwtxbuf`,
`_hwrxbuf`, `_caplength_hciversion`, `_hccparams`, `_dciversion`, `_usbmode`, `_usbcmd`, `_usbsts`, …).
A key present means the probe ran at the D13 idle site and read the controller; the values identify the
core (ChipIdea CI13xxx / MSM72K, `[[mi4-910-usb-debug-map]]`). **PRESS IS THE OPERATOR'S.**

*Provenance: `src/entry/build_entry.sh`'s `xnu_entry_910` clause (the linked-image reading above);
`out/stage90/xnu_arm_entry-config.txt`; the payload build (`scripts/build.sh` rc=0). Host-side,
reversible, **device unmodified**. Follows [[mi4-958-3gb-rides-the-report]], the D13 tree-pin series
951-958.*