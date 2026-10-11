# Experiment: USB-ladder verification (M1) — a per-rung verdict from observable keys

**Goal clause:** 「可以通过usb进行调试」 (and its original form, experiment 910's map).
**Ladder:** 959 probe → 960 device → 961 enum → 962 stream (`src/entry/entry_usb.c`,
`entry_usb_dev.c`, `entry_usb_enum.c`, `entry_usb_stream.c`).
**Status:** DESIGN + HOST TOOL. The arms are PARKED and, on D13, **have never been observed to
enumerate or stream even once**.

## 1. Why this exists (the gap)

Two facts sit beside each other and are easy to conflate:

1. The ladder's code is BUILT (959–962; `make check` clean) and each arm's record carries its
   switches (`STAGE90_XNU_USB_PROBE/DEV/ENUM/STREAM=1`).
2. On D13 the ladder has **never transmitted**. The 977 press died at `lock_set_init`
   ([[mi4-979-xnu-panicked-in-ipc-bootstrap]]) — i.e. **before `machine_idle`** — so the seam the
   ladder fires from was never reached, and the USB capture was empty.

`tools/check_usb_host_reader.py` only proves the **reader's** EP/VID/PID match
`src/entry/entry_usb_enum.h`; it says nothing about the payload transmitting. An arm **record** is a
switch set, not a wire observation — and reading "built" as "works" is exactly the defect M1 is
meant to close. **`tools/usb_ladder_verify.py` refuses to call any rung verified from the arm record
alone** (`--arm-record` is accepted only to be refused, exit 4).

## 2. The fire path, by value

The D13 seam is **`__wrap_machine_idle` in `src/entry/entry_trace.c:2235`** (not `entry_stubs.c`).
It is entered on every idle pass (the build pins it to `processor_idle`'s branch — `build_entry.sh`
512's reader, `:30187-30210`). The four probes sit at `entry_trace.c:2303-2329`, each under its own
`#if`:

| rung | switch (`entry_trace.c` line) | call | body / first observable key |
|------|------------------------------|------|------------------------------|
| probe  | `STAGE90_XNU_USB_PROBE` (`:2303`)  | `entry_usb_probe()` (`:2307`)      | `entry_usb.c:126`; gates on `entry_mmio_section` → `xnu_live_usb_mapped` (`:152-167`) |
| device | `STAGE90_XNU_USB_DEV` (`:2310`)    | `entry_usb_dev_init()` (`:2314`)   | `entry_usb_dev.c:246`; the mode gate → `xnu_live_usb_dev_proceed` (`:311-315`) |
| enum   | `STAGE90_XNU_USB_ENUM` (`:2317`)   | `entry_usb_enum_poll()` (`:2321`)  | `entry_usb_enum.c:532`; `usb_enum_arm_ep0()` → `xnu_live_usb_enum_armed` (`:548-553`) |
| stream | `STAGE90_XNU_USB_STREAM` (`:2324`) | `entry_usb_stream_poll()` (`:2328`)| `entry_usb_stream.c:225`; needs EP1-IN enabled → `xnu_live_usb_stream_armed` (`:255`) |

Then `xnu_live_usb_stream_*` on each completion (`entry_usb_stream.c:260-264`), and the EP1-IN write
itself is `usb_stream_write32(STAGE90_USB_ENDPTPRIME, EPBIT(EP_IN,1))` (`entry_usb_stream.c:185`) —
EP1-IN is qh index 17, not 1 (`entry_usb_enum.h:78`).

The build refuses a probe left at any other site (`build_entry.sh:38384-38406`: D13 must call from
`__wrap_machine_idle`, 4570 from `__wrap_Idle_load_context`). **The prerequisite chain** the 977/979
fault sat upstream of: XNU must reach `ipc_bootstrap` → scheduler → `machine_idle` before any probe
runs. `lock_set_init`, where 979 faulted, is earlier — **the probes never ran**.

## 3. Per-rung pass criterion + falsification

A rung is **fired** only on a value only a real transition produces; it is **did-not-fire** when the
payload reached the rung and the value proves it did not advance; otherwise **indeterminate** (a veto
on reading silence as absence — [[mi4-silence-is-a-reading-only-if-success-is-silent]]).

| rung | pass criterion (fired) | falsification (did-not-fire) |
|------|------------------------|------------------------------|
| **959 probe** | `xnu_live_usb_mapped=1` **and** `xnu_live_usb_id_cap` + `xnu_live_usb_hciversion` present (the core was identified through the installed section) | `xnu_live_usb_mapped=0` — `entry_mmio_section` refused, no register read (`entry_usb.c:160-166`) |
| **960 device** | `xnu_live_usb_dev_devmode_ok=1` **and** `xnu_live_usb_dev_loaded=1` (USBMODE read back `CM_DEVICE`) | `xnu_live_usb_dev_proceed=0` — not in device mode, FORCE off (`entry_usb_dev.c:311-314`) |
| **961 enum** | `xnu_live_usb_enum_armed=1` **and** `xnu_live_usb_enum_requests>0` (host drove control transfers; the counter increments per SETUP token, `entry_usb_enum.c:454`) | `xnu_live_usb_enum_gated=1` (core not CM_DEVICE, FORCE off) — or armed with `requests=0` (no enumeration on the wire) |
| **962 stream** | `xnu_live_usb_stream_armed=1` **and** `xnu_live_usb_stream_chunks>0` **and** the host captured **≥ 64** bytes (≥ one full FS bulk packet, `entry_usb_enum.h:186`) | `xnu_live_usb_stream_waiting=1` (EP1-IN never ENABLED by the enum arm) — or `chunks>0` with **0 host bytes** (reader rc=2 never-seen / rc=3 silent) — or `< 64` host bytes |

The ID check: the device **must** enumerate at **VID:PID `18d1:0910`** — read by value from
`src/entry/entry_usb_enum.h:174-175` (`ID_VENDOR=0x18d1`, `ID_PRODUCT=0x0910`). The prompt's ID is
**correct**.

## 4. `tools/usb_ladder_verify.py`

Takes a RAM-console / EP1-IN capture (the ` xnu_...=0x........` KV text that the 962 stream carries,
and that a non-resident run also leaves in `/proc/last_kmsg`), plus optionally the reader's exit code
(`--reader-rc`; `usb_console_read.py`: 0 = bytes, 2 = never seen, 3 = claimed but silent), and
returns per-rung **fired / did-not-fire / indeterminate** + an overall state
(`not-reached | blocked-at-X | reached-X | top-fired=X`). `--json` for machine use. Exit 0 iff the top
rung fired.

**The host byte count is an observation of the WIRE, not the size of the file handed in.** It is
derived from the capture size **only when `--reader-rc 0`** (the capture IS the EP1-IN stream); a
`/proc/last_kmsg` passed with rc=2/3 contributes **0** host bytes no matter how large it is, so
payload keys alone can never make the stream read as fired. An explicit `--host-bytes` is the
caller's own observation and is honoured.

It **REFUSES** the arm record (`--arm-record` → exit 4): an arm record is a switch set, not a wire
observation.

**Proof it bites:** `--selftest` runs 16 synthetic checks. The load-bearing ones:

- an **empty** capture → probe `indeterminate` / overall `not-reached` (NOT "did-not-fire"): the
  run died upstream, which is exactly 977/979's shape;
- the same `chunks=4` payload record yields `fired` with 200 host bytes but `did-not-fire` (`wrong
  place`) with 0 host bytes and reader rc=3 — **the verdict moves only with the host observation**;
- `chunks=4` with 10 host bytes (`< 64`) → `did-not-fire`;
- the arm record → `arm-record-refused` (exit 4).

Run: `python3 tools/usb_ladder_verify.py --selftest` → `ALL OK`.

## 5. The prerequisite (the single most important thing before the next press)

**The ladder can only be observed after the boot reaches `machine_idle`.** On D13 that is exactly
what 979's page-alignment fix (`2fd98af`) is meant to enable (it stops `pmap_init` setting
`L2_NX_BIT` on the last text page holding `lock_set_init`). **Arming a press with the ladder ON is
only meaningful once the 979 fix is CONFIRMED by a non-dark run** (see L1.1). Until then an ON
ladder is a switch on a seam the boot never reaches, and its silence reads as "did not fire"
when the truth is "never reached".

**If the arm is RESIDENT** (no ending by construction; `build_entry.sh` refuses `POST_END_*` when
`RESIDENT=1`) it keeps **no `/proc/last_kmsg`** — the ONLY capture channel IS this ladder
([[mi4-last-kmsg-reader-carrier]]). So the operator must **start the reader FIRST**, then press:

```
python3 tools/usb_console_read.py --wait 600 --seconds 0 --out out/stage90/captures/usb-console-<stamp>.txt
# THEN press. When it ends (or the press times out), verify:
python3 tools/usb_ladder_verify.py <capture> --reader-rc <rc>
```

A non-resident run instead leaves its console in the single-slot ramoops record, read on the VERY
NEXT boot by `scripts/recover_last_kmsg.sh` — do **not** run `fastboot boot` as the carrier (it both
exposes nothing and burns the slot).

Related: [[mi4-962-first-d13-usb-stream-arm]], [[mi4-910-usb-debug-map]],
[[mi4-977-resident-d13-arms-the-watchdog]], [[mi4-979-xnu-panicked-in-ipc-bootstrap]],
[[mi4-last-kmsg-reader-carrier]], [[mi4-silence-is-a-reading-only-if-success-is-silent]].