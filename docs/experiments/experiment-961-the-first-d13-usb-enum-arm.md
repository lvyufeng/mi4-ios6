# 961 — the first D13 USB enumeration arm (2026-10-09)

Rung 3 of the goal's clause 3 ladder 「可以通过usb进行调试」 on Darwin-13, after 959 (read-only probe) and
960 (device mode). 910b (`880a3568`, then `979ded08`) is this rung on 4570 — **DEFECTIVE twice over**:

1. **dead code** — its probes sat at a site the arm never entered (the 910a2 defect);
2. **the qh index** — `880a3568` built EP1-IN's qh at index **1**, but the qh index **is** the register
   bit `num + dir*ENDPT_MAX/2`, so EP1-IN is **17** ([[mi4-910b-enum-arm-parked]]).

## What it is

The 960 arm **plus one key**: `STAGE90_XNU_USB_ENUM=1`. `USB_PROBE`, `USB_DEV`, `USB_ENUM` and
`MEM_TOTAL` are all **ENTRY build parameters**, so the payload switch set is byte-identical `6c2b6038`
and nothing in the payload moved ([[mi4-build-variant-comes-from-an-env-default]]).

## Why both defects are closed here

- **Defect 1** cannot recur: the poll fires from **`__wrap_machine_idle`** (951), the one idle site
  every pass reaches on D13. The `xnu_entry_910` clause confirms it from the **linked image**:
  `read_probe=1, write_arm=1, enum=1, stream=0` ([[mi4-silence-is-a-reading-only-if-success-is-silent]]).
- **Defect 2's fix is already in the tree** — `src/entry/entry_usb_enum.h:78`,
  `STAGE90_USB_ENUM_QH_IN1 17u`. `check_usb_enum.py` **recomputes** the index from
  `num + dir*ENDPT_MAX/2` (`ENDPT_MAX=32`) rather than trusting the constant, so this D13 build is the
  first to actually carry the fix ([[mi4-a-claim-in-a-comment-is-not-a-check]]).

## What it writes — and what it refuses to

`entry_usb_enum.c`'s poll drains the latched `USBSTS` and advances the EP0 control-transfer state
machine, answering the standard requests a host sends during enumeration: the endpoint set, the
interrupt mask, `DEVICEADDR`. `check_usb_enum.py` (in `make check`) finds **18 stores, none of them
`USBCMD_RST`** — a **build refusal, not a comment**: 910a2 owns the one link reset, and a re-reset here
would fight the running device. It keeps 910a2's **mode gate** (writes nothing unless the core is
already a device, `USBMODE[1:0] == 2`) with `USB_DEV_FORCE` still off.

EP1-IN is owned by this file (it primes the fixed 4-byte `STAGE90_USB_ENUM_IN_MAGIC` on completion). The
**stream** arm, off here, would take EP1-IN over — and `entry_usb_enum.c` guards both EP1-IN primes on
the stream switch so the two files never both write `qh[IN1]`.

## The arm

`armed-d13-4894f20a` (entry bin `4894f20a`, `STAGE90_XNU_TREE_D13=1 STAGE90_XNU_MEM_TOTAL=1
STAGE90_XNU_USB_PROBE=1 STAGE90_XNU_USB_DEV=1 STAGE90_XNU_USB_ENUM=1 STAGE90_XNU_USB_DEV_FORCE=0`):

- qcdt `d7f7735b…`; payload switch set `6c2b6038` (unchanged).
- **PARKED, NOT PRESSED.** `make check` 0; `verify_press_ready` 5/5.

## The reading a press looks for

Three families, and the progression is the reading: `xnu_live_usb_*` (probe ran) → `xnu_live_usb_dev_*`
(device arm wrote) → **`xnu_live_usb_enum_*`** (the EP0 state machine advanced — a host actually drove
enumeration). The enum keys absent while the dev keys are present means the host sent no control
transfer — which is the whole point of the ladder: a USB-debug channel exists only once the host
enumerates this device, and only then can `USB_STREAM` (rung 4) carry the console out.

**PRESS IS THE OPERATOR'S.**

*Provenance: `src/entry/build_entry.sh`'s `xnu_entry_910` clause; `out/stage90/xnu_arm_entry-config.txt`;
the payload build (`scripts/build.sh` rc=0); `make check`'s `check_usb_enum.py`. Host-side, reversible,
**device unmodified**. Follows 960 and 910b; see [[mi4-960-first-d13-usb-device-arm]].*