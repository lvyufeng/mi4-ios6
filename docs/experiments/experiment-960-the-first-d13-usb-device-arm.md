# 960 — the first D13 USB device arm (2026-10-09)

The second rung of the goal's clause 3 ladder 「可以通过usb进行调试」 on Darwin-13, after 959's read-only
probe. 910a2 (`b3fbcd31`) is this rung on 4570 — and it was **DEFECTIVE**: its probes sat in dead code,
so a press returned **zero** `xnu_live_usb_*` keys ([[mi4-910a2-usb-dev-write-arm-parked]]). This rung
fixes that defect **by construction** rather than by rebuilding the 4570 bytes.

## What it is

The 959 arm **plus one key**: `STAGE90_XNU_USB_DEV=1`. `USB_PROBE`, `USB_DEV` and `MEM_TOTAL` are all
**ENTRY build parameters**, so the payload switch set is byte-identical `6c2b6038` and nothing in the
payload moved ([[mi4-build-variant-comes-from-an-env-default]]).

## Why the dead-code defect cannot recur here

951 moved the USB probes' call site to follow the tree: on 4570 `__wrap_Idle_load_context`, on **D13
`__wrap_machine_idle`** (937 compiles the idle-load wrapper out; D13 ships no `cpu_idle`). That wrapper
is the one idle site **every pass reaches** on this tree. The build confirms it from the **linked
image**, not the command line:

```
xnu_entry_910: the USB probes are called from `__wrap_machine_idle` and nowhere else
               (read_probe=1, write_arm=1, enum=0, stream=0), which is the one idle site every pass
               reaches on this tree ...
```

`write_arm=1` is the whole point — the clause finds `entry_usb_dev_init` linked and called from exactly
that site. A probe left at the dead site would be refused ([[mi4-silence-is-a-reading-only-if-success-is-silent]]).

## This is the first arm that WRITES to the USB port

`entry_usb_dev.c` runs the vendor's PHY sequence (`msm_otg.c`'s `msm_otg_reset`) and the ChipIdea
device-mode sequence (`ci13xxx_udc.c`'s `hw_device_reset`). `check_usb_dev.py` (in `make check`)
re-derives, against the Android header that owns each value:

- **5 transcribed offsets**, each compared to its owner.
- **36 written values**, each compared to the owner's own expression.
- **8 write-targets** (`USBCMD`/`USBSTS`/`USBMODE`/`DEVICEADDR`/`ENDPOINTLISTADDR`/`USBINTR`/… + the
  PHY/ULPI ports).
- **the mode gate** — the `!= CM_DEVICE && FORCE == 0` form is present, it returns, and it is the
  init body's **first store**. The arm writes **nothing** unless 910a found the core already a device
  (`USBMODE[1:0] == 2`). `STAGE90_XNU_USB_DEV_FORCE=1` is the opt-in that relaxes the gate and is
  **left off (0)**.

The hazard the gate bounds: **`USBCMD.RST` can drop the host's enumeration** (`18d1:d00d`). The gate is
why this is bounded — and it is a build/check property, not a comment
([[mi4-a-claim-in-a-comment-is-not-a-check]]).

## The arm

`armed-d13-89210a26` (entry bin `89210a26`, `STAGE90_XNU_TREE_D13=1 STAGE90_XNU_MEM_TOTAL=1
STAGE90_XNU_USB_PROBE=1 STAGE90_XNU_USB_DEV=1 STAGE90_XNU_USB_DEV_FORCE=0`):

- qcdt `9eaf45bb…`; payload switch set `6c2b6038` (unchanged).
- **PARKED, NOT PRESSED.** `make check` 0; `verify_press_ready` 5/5.

## The reading a press looks for

Two families, and the split is the reading:

- **`xnu_live_usb_*`** (the 959 probe keys) — present means the probe ran at the D13 idle site.
- **`xnu_live_usb_dev_*`** (the device-arm keys) — present means the device arm **wrote**. If the probe
  keys are present but the dev keys are **absent**, the mode gate refused: the core was not in device
  mode, which is a reading of its own (a device that is a host, or already enumerated the other way).
- The device arm also **publishes its two deliberate omissions** — the single `USBCMD.RST` and the
  un-programmed `USBINTR`/`ENDPOINTLISTADDR` — so a press can tell "the gate held" from "the write
  silently no-op'd".

**PRESS IS THE OPERATOR'S.**

*Provenance: `src/entry/build_entry.sh`'s `xnu_entry_910` clause (the linked-image reading above);
`out/stage90/xnu_arm_entry-config.txt`; the payload build (`scripts/build.sh` rc=0); `make check`'s
`check_usb_dev.py`. Host-side, reversible, **device unmodified**. Follows 959 and 910a2; see
[[mi4-959-first-d13-usb-probe-arm]].*