# 962 — the first D13 USB stream arm (2026-10-09)

Rung 4 — **the top** — of the goal's clause 3 ladder 「可以通过usb进行调试」 on Darwin-13, after 959
(probe), 960 (device) and 961 (enum). 910c (`77a51d33`/`6deae815`) is this rung on 4570 and shipped the
same `QH_IN1=1` defect as 910b (fixed in tree at `entry_usb_enum.h:78` = `17u`).

## What it is

The 961 arm **plus one key**: `STAGE90_XNU_USB_STREAM=1`. The four USB switches and `MEM_TOTAL` are all
**ENTRY build parameters**, so the payload switch set is byte-identical `6c2b6038`
([[mi4-build-variant-comes-from-an-env-default]]). All four rungs now sit in one image: the
`xnu_entry_910` clause reads it from the **linked ELF** as
`read_probe=1, write_arm=1, enum=1, stream=1`.

## What it does

It **hands EP1-IN over**. The enum arm still **enables** the endpoint on `SET_CONFIGURATION`, but it
stops priming it — `entry_usb_enum.c` guards both EP1-IN primes on the stream switch, so the two files
**never both write `qh[IN1]`** (a build refusal pins the handover). The stream arm owns `qh[IN1]` and
its dTD from the first stream prime on, and on each completion sets EP1-IN's payload to a **cursor over
the RAM console ring** (via the `+0x01000000` alias) instead of 910b's fixed 4-byte magic. The count
comes from the dTD `token` (with the ACTIVE guard), **not `qh.curr`** — the vendor never reads `qh.curr`
for a count (`_hardware_dequeue:2176-2178`).

The stream **requires** the enum arm — a build refusal forbids `STREAM=1` with `ENUM=0`, because the
endpoint it streams is armed by the enum arm alone and a stream with no enumeration has no host to read
it. So this arm carries the whole ladder.

## Why this is the operative rung

This is the **arm that makes the XNU console reachable over USB** — the clause 「可以通过usb进行调试」 in
its working form. A host that enumerates and opens the bulk IN endpoint reads the console ring; until
then there is no USB-debug channel. Every rung below it (probe/device/enum) is a precondition measured
in its own keys; this is the one that carries bytes.

## The arm

`armed-d13-c54fc40d` (entry bin `c54fc40d`, `STAGE90_XNU_TREE_D13=1 STAGE90_XNU_MEM_TOTAL=1
STAGE90_XNU_USB_{PROBE,DEV,ENUM,STREAM}=1 STAGE90_XNU_USB_DEV_FORCE=0`):

- qcdt `6b27854a…`; payload switch set `6c2b6038` (unchanged).
- **PARKED, NOT PRESSED.** `make check` 0; `verify_press_ready` 5/5.

## The reading a press looks for

The four-family progression topped out: `xnu_live_usb_*` (probe ran) → `xnu_live_usb_dev_*` (device
wrote) → `xnu_live_usb_enum_*` (a host drove enumeration) → the stream's own prime/completion keys
(the console ring's bytes going out EP1-IN). A host that reads the endpoint gets the RAM console.

**PRESS IS THE OPERATOR'S.**

*Provenance: `src/entry/build_entry.sh`'s `xnu_entry_910` clause; `out/stage90/xnu_arm_entry-config.txt`;
the payload build (`scripts/build.sh` rc=0); `make check`'s `check_usb_stream.py`. Host-side, reversible,
**device unmodified**. Follows 961 and 910c; see [[mi4-961-first-d13-usb-enum-arm]].*