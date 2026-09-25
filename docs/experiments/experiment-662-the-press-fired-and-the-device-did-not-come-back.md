# 662: the press fired, and the device did not come back — exit 2, no capture, and the only variable that changed was the operation

The owed press fired on **2026-09-25** at **03:53:50** and the phone **did not return**. That is the exit-2
case, and it is the first time in this project that the acting arm has been sent at all: the arm that ran
before it (17:24:03, the 574 park) was the *measure* form.

The run is recorded here for what it establishes, which is **not** a reading about the `pop`: exit 2 means
the payload's log never came back, so the pair the arm publishes was never read. §5 records the three
repairs that landed in the same window, because the press being spent lifted the edit freeze that had held
them.

## 1. The run, code first

| | |
| --- | --- |
| readiness | **exit 0 — 5 ok / 0 fail** (row 4 is now green: `fastboot` lists nothing, `adb` lists `4a2fe00b` as `device`) |
| gate | **exit 0**, 557 stdout lines |
| runner | **exit 2 — the device did not come back** |
| capture | **none**. `/tmp/cancro-last_kmsg.txt` does not exist; the previous run's log was parked to `.prev.3` first (598044 B, sha256 `708ff83e…` — the 652 capture), so nothing was lost by this run |
| bytes sent | `stage90-qcdt.img`, `Sending 'boot.img' (8340 KB) OKAY`, `Booting OKAY` — the recorded arm, `armed-seam-poc-a43304f2` |

The firing order was the pre-registered one, and every step passed until the return: readiness → gate →
**exactly one** runner. The blocker that had held this press since 2026-09-24 19:06 cleared at
**03:52:53**, when the neighbour `33e80afe` left the bus; the watcher recorded both of its conditions
(`fastboot: []`, `adb: [4a2fe00b device]`) and fired 33 s later.

## 2. The timeline, from the host's kernel log — by **serial**, which is the runner's own instruction

The runner's own note warns that this host's port has a second occupant, so a port-only test can read a
stranger as a return, and prescribes `sudo dmesg | grep 4a2fe00b`. Done:

```
[2376679.039] usb 3-3:  USB disconnect, device number 37          <- the neighbour leaves the bus (03:52:53)
[2376766.183] usb 3-10: USB disconnect, device number 29          <- the phone leaves adb (adb reboot bootloader)
[2376772.887] usb 3-10: new high-speed USB device number 39
[2376773.042] usb 3-10: New USB device found, idVendor=18d1, idProduct=d00d   <- fastboot mode
[2376773.042] usb 3-10: SerialNumber: 4a2fe00b
[2376776.030] usb 3-10: USB disconnect, device number 39          <- `fastboot boot` hands control over
              (nothing since)
```

**The last enumeration of the serial is the fastboot one at `2376773`**, and the disconnection four seconds
later is what `fastboot boot` always does when it hands the image over. `2376776` is the moment the payload
started, and **the phone has not been on the bus since** — measured 282 s later, and still true when this was
written. The runner's counters agree and are independent of my reading: `SerialNumber` enumerations
`1537 → 1537`, and enumerations on port 3-10 `1545 → 1545`.

**And the payload's 28 s watchdog did not bring it back.** That is worth stating plainly because it is the
one clause of 「不能变砖」 that this run tested: the net that returned the phone on the previous press
**failed here**, exactly as memory 517 recorded for a different arm. The phone is not bricked — it needs a
power press, which is the documented worst case — but a run whose non-return the watchdog cannot recover is
a cost the next arm has to be designed against, not merely hoped about.

## 3. What exit 2 does and does not establish

The runner's exit-2 text is explicit, and it is the honest reading of this run:

> *"If the payload armed its watchdog, the reboot that returns the device would have preserved the log - so a
> failure to return means the log is likely unrecoverable anyway, but waiting is free and power-cycling is
> not."*

So: **no pair, no `slot_post_calls`, no `poll_seq`** — 653's pre-registration is *unanswered*, not falsified,
and 657 §4.2's four-cell table has no entry filled in. The device's log lives in the top of DRAM; nothing
host-side can reach it while the phone is off the bus, and the power press that brings the phone back may
take it with it.

**One clarification the record needs, because exit 2 is not one thing.** The runner's own census says its
`2` has two producers in that section and both messages are read from the file: a payload that never
returned, and *"a phone whose Android failed to come up"*. Only the first owes a power press; the second is
a capture failure. This run's message is the first one (*"The device did not come back: no adb entry, and no
new enumeration in the host log"*), and the host log agrees by serial. So the power press is owed.

## 4. The one thing this run does establish: the single variable that changed is the operation

The two arms' records, read from their own config files rather than from memory:

| switch | the arm that **returned** (574 park, 17:24:03) | the arm that **did not** (acting arm, 03:53:50) |
| --- | --- | --- |
| `STAGE90_XNU_SEAM_MEASURE` | **1** | 0 |
| `STAGE90_XNU_SEAM_POC` | 0 | **1** |
| `STAGE90_XNU_IDLE_NO_SLEEP` | 0 | 0 |
| `STAGE90_XNU_SLOT_NULL` | 1 | 1 |

**The whole difference between "the phone came back" and "the phone did not" is the operation behind the
interception.** Both images enter the same seam, run the same interception, take the same pair of reads and
reach the same `pop {fp, pc}`; the acting arm additionally runs `FlushPoC_DcacheRegion(slot, 8u)` and the two
restore stores. That is 574's own point about the two bodies (`experiment-574`, §56-66: one interception,
two bodies, `SEAM_ON=$(( SEAM_POC | SEAM_MEASURE ))`), now measured from the other end.

**Three explanations are consistent with what is measured, and this run cannot separate them** — the
separation needs a log, which is exactly what a non-return destroys:

1. **The repair worked.** The operation wrote the dirty line out and invalidated it (657 §4.2's one `mcr`),
   the `pop` missed and fetched the repaired DRAM, the idle exit **returned**, and XNU continued past the
   point it has always died at — then hung somewhere later, in a state the payload's watchdog could not
   reset. The evidence for it is the table above and the cell 653/657 pre-registered as the expected one;
   the limit is that n = 1 on each side and exit 2 carries no log.
2. **The operation hung the machine itself**, before the `pop`, in a way that defeats the watchdog — e.g. a
   maintenance operation with `SCTLR.C` clear on a line whose level is not the one the operation reaches,
   leaving the CPU in a state where the watchdog's bite does not complete.
3. **The reset happened and Android failed to come up**, which the runner's own note says is the same
   reading as "never returned" from adb alone. This one is the cheapest to rule out and the most
   consequential if true, because it would mean a log *does* exist on the device — and nothing host-side can
   reach it.

**What would separate them is the next run's log**, so the next press is worth spending on the *same arm* —
a repeat is the only way to obtain the reading, and if it hangs again the reproducibility is itself the
finding that sends us to a different arm. What this step does **not** do is claim explanation 1, which would
be reading a favourable hypothesis into an absence.

## 5. The three repairs landed in the same window — the freeze lifted because the press was spent

With no watcher and no catcher armed, 660 §5's edit freeze is lifted, and 661's staged repairs were applied
in that order (land, **then** arm — 653's rule):

| # | change | verification |
| --- | --- | --- |
| **R1** | `/tmp/r654/press-on-clear.sh`: readiness is **tested** (`ready=${PIPESTATUS[0]}` read on the line after the pipeline, then `exit 1` on red) instead of `say`ed | `bash -n` clean; the block exercised at both exit codes: `0 → gate: proceeding`, `1 → REFUSED … nothing was sent` |
| **R2** | `preflight_boot_check.sh`: the new **`== the arm the record owes ==`** clause, which asks *which arm* rather than *is this consistent* — `out/` must equal **one** set recorded by hand in `stages/stage90/revert-set.txt` | the patched gate run as itself: **exit 0**, and `diff` against the committed gate shows **only** the two changed sections |
| **R3** | `preflight_boot_check.sh`: the storage-symbol patterns **anchored on a delimiter** — `(^|[^A-Za-z0-9])(sdcc\|emmc\|nand\|mmc\|ufs\|flash\|partition)` — and the scope **widened** to the entry image the payload jumps into | patched gate exit 0, 0 hits on both real images; 9/9 on 661 §3's positive control |

**The R2 clause's first real run prints:** `set armed-seam-poc-a43304f2: 11/11 file(s) of out/ match`, with the
other two recorded sets at `2/11` — the discriminating positive control, on the real tree, before a press
rather than after one. From this press on, **a fire is refused unless the bytes are a set somebody recorded
by hand**, which is the hole 660 §3 measured: image, config and manifest move *together* through a rebuild,
so every earlier clause was satisfied by construction. R4 (654 §6) waits still.

`preflight_boot_check.sh`'s own line count moved from 531 to 537 at the same gate state; the 557 the watcher
recorded at 03:53:50 is a *different* state (the phone was in adb then, and is dark now), which is why the
count is not comparable across states and the diff is the reading that matters.

## 6. The state now, and what is owed

* **The phone is dark and needs a power press** — *"hold Power ~10-15 s, release, press Power normally"*. It
  is not bricked: nothing was written to storage, `fastboot boot` was the only device command, and the worst
  case here is the documented one.
* **The press is owed again**, for the same arm: `out/` still holds `armed-seam-poc-a43304f2` (R2's clause
  just verified it, 11/11), so no new pre-registration is needed for a repeat.
* **The watcher has been re-armed** (single-shot, two conditions: the neighbour absent *and* `4a2fe00b`
  present in adb or fastboot), with R1 in place. It will fire when the phone comes back. The operator should
  expect exactly that: the phone will boot Android and then, within seconds, reboot into the payload again.
* **Waiting is free and power-cycling is not** (the runner's own words): if the log could survive, it does so
  only until the power press. Nothing host-side can reach it either way.

## 7. What this does not do

* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** No boot was observed; `poll_seq` still
  stops at 2 and `slot_post_calls` is still absent. If explanation 1 of §4 holds, the frontier has moved — and
  **this document does not claim that**, because an absence is not a reading.
* **It does not build anything, and does not change the arm.** A repeat fires the recorded arm; R2 would
  refuse anything else.
* **It does not put the phone at risk.** No `flash`, nothing written to storage, no EDL, no test on the
  neighbour's serial.
* **TWRP-to-storage stays withheld** — 「如果os已经能进去了的话」 is unmet, and an unobserved boot is not it.

## 8. Safety

One `fastboot boot`, non-persistent — the only device command this step or the run it records issued.
**`flash` was never used and nothing was written to storage**, so no outcome of this run can write to storage
and a brick is impossible by construction; the observed cost is a phone that needs a power press. Everything
else is host-side: the two logs the watcher wrote, the runner's own record, the host kernel log read by
serial, and three file edits (the gate, the watcher, this document). The arm still verifies against the
`armed-seam-poc-a43304f2` park, and the clause added in R2 is now what enforces that before any fire.
