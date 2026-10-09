# 963 — the runner reads the clause-3 USB ladder (2026-10-09)

959–962 built the whole clause-3 ladder (「可以通过usb进行调试」) — four arms, one payload set
`6c2b6038`, each firing from `__wrap_machine_idle`. Every one of them was **parked, not pressed**.
This rung closes the gap that would have made their presses illegible: the capture runner's summary
read **none** of the four families' keys.

## The defect this closes

It is the recorded one: **a key the entry image writes and the runner never reads makes a press's
summary silent about that arm.** Five goal families once reached the runner unread and got readers
([[mi4-911-runner-now-reads-all-goal-clauses]]); the USB ladder is the family that grew since. On a
press of any of the four arms, `run_and_capture.sh`'s arm table printed the idle families and said
nothing about probe/device/enum/stream — the USB outcome had to be grepped out of the log by hand,
which is exactly the asymmetry the project keeps removing (every failure path prints its reading; the
path that *produced* the reading printed least).

## What it changes

**`scripts/run_and_capture.sh`** — a new block in `summarise_log()`, immediately after the idle arm
table, in the same **REPORTED, not scored** shape as the 646 seam family. It prints one row per family,
each the family's **top-of-body marker key**:

| rung | marker | kind of an absence |
|---|---|---|
| 1 probe  | `xnu_live_usb_loaded`       | absent by construction off the switch; else the idle was not reached |
| 2 device | `xnu_live_usb_dev_loaded`   | same |
| 3 enum   | `xnu_live_usb_enum_armed` **or** `_gated` | one of the two is written at the mode gate |
| 4 stream | `xnu_live_usb_stream_state` | written at the body's end on every pass |

The arm itself stays the **gate's and the record's** reading — an image publishes no build marker
(549), so a log alone cannot say which switch produced it; what the block adds is that the arm's own
outcome is in the summary at all. Three narrations, all readings and not failures: no USB key at all
(read the gate's record); device wrote and the enum **mode gate refused** (`USBMODE[1:0] != 2`, FORCE
off — 960's bound); the **top of the ladder** (stream running with EP1-IN armed).

**`tools/check_runner_usb_families.py`** (new, in `make check`) — the structural guard, **both
directions** ([[mi4-a-claim-in-a-comment-is-not-a-check]]):

1. the runner's USB block must name a marker for **each of the four families** — a dropped family is a
   build refusal, not a habit;
2. every `xnu_live_usb*` key the runner names must be **published by a `src/entry/entry_usb*.c`** — the
   reverse of the "one value, two definitions" class, a reader grepping a key nothing writes
   ([[mi4-one-value-two-definitions]]).

`--selftest` feeds three measured mutations — a family dropped, an invented key, the whole block
removed — and asserts each is refused (all three were, measured).

## Verification

- Synthetic logs for each reading: full ladder (→ top-of-ladder narration), probe-only, device +
  mode-gated enum (→ the mode-gate narration), and no-USB (→ the none narration). All four render.
- A **real** archived capture (`out/stage90/captures/rung61-hfsport-20261001-232751-press.log`, a
  pre-USB arm) reads all-absent and says so — the block does not misfire on an image that predates it.
- `make check` rc=0, including the four existing USB guards and the new one.

## Not in scope, and deliberately

No device action. 959–962 remain parked; this rung makes their press *readable* without making one.
The USB ladder's clause is still **owed a press** — `scripts/run_and_capture.sh --allow-xnu-entry`
(`fastboot boot` only, never flash; unplug `33e80afe`). **PRESS IS THE OPERATOR'S.**

*Provenance: `scripts/run_and_capture.sh` `summarise_log()`; `src/entry/entry_usb{,_dev,_enum,_stream}.c`
marker publishers; `tools/check_runner_usb_families.py --selftest`. Host-side, reversible, **device
unmodified**. Follows 962; see [[mi4-962-first-d13-usb-stream-arm]].*