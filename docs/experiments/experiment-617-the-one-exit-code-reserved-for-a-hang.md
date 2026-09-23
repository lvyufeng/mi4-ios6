# 617: the return criterion could not see the phone, and reported it as a hang

`run_and_capture.sh` decides whether the device came back by looking for one thing in the host log:

```sh
grep -c "SerialNumber: $SERIAL"
```

This step measured what that misses. On `usb 3-10` — the port this phone is on, every one of its
records — the host log holds **1536** `New USB device found` lines, of which **1530** carry
`SerialNumber: 4a2fe00b` and **6** carry no serial at all:

```
[2152823.387151] usb 3-10: New USB device found, idVendor=05c6, idProduct=f006, bcdDevice= 0.00
[2152823.387162] usb 3-10: New USB device strings: Mfr=0, Product=0, SerialNumber=0
[2153329.620416] usb 3-10: USB disconnect, device number 74
[2153344.905444] usb 3-10: new high-speed USB device number 75 using xhci_hcd
[2153345.054173] usb 3-10: SerialNumber: 4a2fe00b
[2153363.079941] usb 3-10: USB disconnect, device number 75
[2153368.682037] usb 3-10: new high-speed USB device number 76 using xhci_hcd
[2153368.844007] usb 3-10: New USB device found, idVendor=05c6, idProduct=f006, bcdDevice= 0.00
[2153368.844018] usb 3-10: New USB device strings: Mfr=0, Product=0, SerialNumber=0
```

Device numbers **74 → 75 → 76**, consecutive, on one port, four to five seconds apart: the same
device alternating between a Qualcomm mode with **all three USB descriptors empty** and its Android
mode where `SerialNumber: 4a2fe00b` is printed. That occupant is **this phone**, and a return in that
mode advances neither the adb test nor the serial test — so it fell through every branch and was
reported as **exit 2, "The device did not come back"**, the one code this file's contract reserves for
a hang and the one that tells the operator to press power. The device came back; the criterion could
not see it.

Host-side only: one script, one rehearsal, one document, one index row. No boot, no build, no device,
no `fastboot`, no `adb`, **nothing written to storage**; both parks and the arm were hashed and never
modified. **TWRP stays withheld.**

## 1. Why the hardening made this, and why the fix is not to loosen it

The serial test exists for a measured reason, recorded above it: a port-only test would read a
*stranger* on that port as the phone. 616's comment says the port "also carries a serial-less
`05c6:f006` occupant" — treating it as a hazard to the test. It is that, and it is also the phone
itself, which is the part the comment did not say.

So the fix is not to weaken the serial test. It is a **second, independent reading** on the same
port, with a different failure mode: the count of enumerations on the port the phone is on, **any
id**. The serial test answers *"the phone identified itself"*; the port test answers *"something
enumerated where the phone is"*. The second survives a mode with empty descriptors, and it is
strictly weaker — which is why it does not replace the first.

Three functions, and the port is **derived**, not written down:

```sh
phone_port()        # the port from the log's own `usb X-Y: SerialNumber: 4a2fe00b` lines -> "3-10"
port_enum_count()   # `usb $PHONE_PORT: New USB device found` — any id
port_enum_rollup()  # idVendor/idProduct -> `05c6:f006=6 18d1:4ee7=508 18d1:d00d=510 2717:0368=512`
```

Measured on this host: `phone_port` → `3-10`, and the rollup over the whole ring → those four ids,
which sum to 1536. A derived port cannot drift when someone re-cables, and the rollup is printed so
the **reading stays with the operator**: the claim this file makes is *"something enumerated on the
port `$SERIAL` is on, and here is what"* — not *"the phone came back"*. It cannot make the stronger
claim, because `05c6` is also the vendor of the neighbouring phone on `usb 3-3`, which the safety
gate's own rule ("read the port, not a vendor id") already warns about. The message says so, prints
both rollups, and leaves identity as the operator's call.

## 2. The defect inside the fix, and it is this step's own subject

The port branch's first version printed the refusal in full, set `ENUM_ADVANCED=port`, and then fell
out of the `if`:

```sh
ENUM_ADVANCED=port          # ... and no exit
elif [[ serial advance ]]   # not taken: a port-only return does not move the serial count
  ...
fi
if [[ $ENUM_BEFORE == UNREAD || $ENUM_AFTER == UNREAD ]]; then ... die (exit 1) fi
if [[ $ENUM_AFTER -lt $ENUM_BEFORE ]]; then ... exit 2 fi
say "The device did not come back: no adb entry, and no new enumeration in the host log."
exit 2
```

Neither of the two tests below can fire, so the operator got **the right refusal and then `exit 2`** —
the one code the branch was written to prevent. A message and a status that disagree, with the status
being what the operator's next action reads. Found by tracing the control flow after the branch was
written, not by reading it back; **the rehearsal's own cell could not have caught it until it was
given a state that reaches that block** (§4).

Two more things fell out of the same repair:

* **`ENUM_ADVANCED` was never initialized.** Under this file's `set -euo pipefail`, `[[ -n
  $ENUM_ADVANCED ]]` on an unset variable is `unbound variable`, not false — measured
  (`bash: UNSETVAR: unbound variable`, bash 5.1.16). It is now set to the empty string where
  `RETURN_HOW` is, because the branch that reads it needs "neither fired" to be a value.
* **The park note moved out of the serial branch**, where it had lived since 588 only because that
  branch was the only one. Both branches end by telling the operator how to re-read the log by hand,
  and the name it reads *into* is the one step 2b may have parked — so *which file is at `$LOGFILE`*
  is owed by both. Left where it was, "the previous run's log is NOT at that name" would have been a
  property of which test happened to fire.

## 3. The two branches are ordered, and the ordering was a claim with no cell behind it

The port branch was written first, and the reasoning for moving the serial branch to the `if` was that
a normal return advances **both** counts (the re-enumeration is on the same port), so the port branch
would have fired for every return the post-wait block saw, leaving the serial branch — the one whose
message can say the device came back *with* its serial — unreachable.

Then the ordering was falsified by putting the port branch back first, and **the battery stayed
green**. The only state that reached that block with an advance was the port-only one, which by
construction cannot produce the case the reasoning was about. The claim was covered by nothing.

`return-after-last-poll` is the missing half: a normal return, timed to land after the last in-wait
poll so that the post-wait block sees **both** counts advance. With it, reverting the order turns that
cell red and leaves `qdl-return` green — each branch is now pinned by a state only it can answer, and
each state is pinned against being answered by the other branch (`forbid:`, below).

## 4. The stub was a model of the runner, and that is what broke

Three separate findings, all in the rehearsal, all of the same kind — **the instrument encoding a
claim about the artifact instead of a property of the device**:

* **The return was keyed on the number of `dmesg` reads.** It appeared on the *second* read after the
  boot, which was true while the runner took exactly one baseline read. 617 gives the runner a second
  baseline (the port count, taken in the same instant), so under that model the return landed **inside
  the port's own baseline**: `PORT_BEFORE` came out already carrying the enumeration, `PORT_AFTER`
  equalled it, and the state the new cell exists for could not be produced. A real device does not
  re-enumerate between two `dmesg` calls microseconds apart. The model is now **elapsed seconds since
  the boot**, which stays true however many baseline reads the runner makes.
* **`no_serial_ever` emitted an empty log**, which the runner correctly reads as a channel it could not
  read (UNREAD) rather than as a log with no phone in it — 609's distinction ("the machine stopped, or
  the channel that records it did") one layer in. So that state now emits a **readable** log that
  simply never mentions the phone: unrelated devices, no `usb 3-10` line. That is what makes the port
  *underivable* rather than the log *unreadable*, and the cell asserts the difference.
* **`RETURN_TIMEOUT=3` gave the wait loop exactly one poll**, which happens before its `sleep 3`, so
  nothing that takes any time could be seen *inside* the wait and two states that had been testing the
  in-wait capture path silently started testing the post-wait block. The rehearsal uses 6 now — two
  polls — and which branch a return exercises is chosen per state by `return_after:N`, because that is
  a function of whether the return lands inside the window or after it.

## 5. The cells, and the value that is a window

`tools/rehearse_live_path.sh` grew **9 → 12** states in the live-path section (**12 + 13 + 4 = 29 ok /
0 failed**, exit 0) and gained one facility: a `forbid:` marker, so a state can assert what the runner
must **not** say. That is the third form of 615's rule — an absence assertion carries its evidence in a
sibling — and the pairs here are exact: `qdl-return` forbids the non-return line, `port-underivable`
forbids the port refusal, `return-after-last-poll` forbids the port branch's message.

| state | what it asserts |
| --- | --- |
| `qdl-return` | a return by a mode with **no serial** is exit 3, by the port reading, and is **not** "The device did not come back" |
| `port-underivable` | with the port absent from the log, the reading is **UNREAD** and the file falls back to the serial criterion — exit 2, and **not** the port refusal |
| `return-after-last-poll` | a normal return after the last wait poll is reported by the **serial** branch, not the port one |

`return_after:5` is the whole difficulty of the first and third, and the value is a **window**, not a
number. The runner reads the host log at the *end* of its wait, so a return that is to be seen there
and not during the wait must land in the last `sleep 3` of the window: with `RETURN_TIMEOUT=6` the
in-wait polls are at ~0.05 s and ~3.05 s and the post-wait reads at ~6.05 s, so the band is
`(3.05, 6.05]`. It is one sleep wide, so no value can have more than ~1.5 s of margin on both sides —
a property of the runner (the wait ends and the reads happen at once), not of the stub.

## 6. Falsifications, one run each, runner restored byte-identically afterwards

| mutation | result |
| --- | --- |
| the shared `exit 3` narrowed to the serial branch (the pre-fix fall-through) | `qdl-return` **FAIL — exit 2, promised 3**, *and* `said what this state must NOT say: The device did not come back`. The right message printed, the wrong code returned — the defect reproduced exactly |
| the serial branch's `if` and the port branch's `elif` swapped back | `return-after-last-poll` **FAIL** — did not say *"the host log shows the phone enumerating again"* and **said what it must not** (*"an enumeration appeared on"*); `qdl-return` stayed **green** |
| `port-underivable`'s forbidden phrase changed to one that **is** printed | that cell **FAIL — said what this state must NOT say**, so the absence assertion can fail |

Before `return-after-last-poll` existed, the second mutation produced **no failure at all** — which is
the measurement that made the cell necessary, and the reason it is in the table.

## 7. What this does not do

* **It does not touch the device, the arm, the payload or any parked byte.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…` (8,540,160 bytes), the entry still `696a0f39…`,
  the arm is still the sleeper, and the press is still the user's.
* **It does not make the port reading an identity claim.** The count cannot tell the phone from a
  stranger that enumerates on the same port; the rollup is printed for the operator and the branch
  says outright that `05c6` is also the neighbour's vendor. It converts a **false hang** into a
  **reading that needs a human**, which is the correct direction: exit 2 spends the device, and a
  wrong exit 2 sends the operator to press power on a device that came back.
* **It does not fix the reverse error.** A stranger enumerating on `usb 3-10` while the phone is
  silent still advances the port count. That is why the branch does not claim a return — but it does
  mean a run can now report "something enumerated" about a device that is not the phone, and the
  operator has to read the ids. The alternative (exit 2 on evidence that something is there) is worse,
  and this file already carries the reasoning at `serial_enum_count`.
* **It does not re-run the gate against a boot.** The gate is green and the arm is unchanged, but the
  only test of the reading is a run, and a run is the press.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus — last
  `usb 3-10` disconnect with serial `4a2fe00b` at **2026-09-23 01:18:06**, now **9.7 h**; `adb
  devices` and `fastboot devices` both empty; the only phone-class device on the host is the
  neighbouring `05c6:9008` QDL on `usb 3-3`. The one event that can move the goal is the user's
  **Vol-Down + Power**, and what this step changes is what the run *after* it is allowed to conclude.

## 8. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One
script edited (`stages/stage90/run_and_capture.sh` — `phone_port`, `port_enum_count`,
`port_enum_rollup`, the port reading in the wait loop, the port branch, the `exit 3` block, the park
note's move, and `ENUM_ADVANCED`'s initialization), one rehearsal extended
(`tools/rehearse_live_path.sh` — three states, a `forbid:` facility, `return_after:`, the elapsed-time
return model, and `RETURN_TIMEOUT` 3 → 6). The gate was re-run → **EXIT=0 / 549 lines / 0 stderr**,
unchanged; the runner's own `--dry-run --allow-xnu-entry` → **exit 0 / 0 stderr**;
`rehearse_live_path.sh` → **29 ok / 0 failed**, exit 0, and all three mutations above were measured
with the runner restored byte-identically (`cmp`). Both parks verify **exit 0 / 11 files** against
their recorded sets, hashed in place with no file modified. The payload, both parks and the arm the
next press sends are unmodified. `fastboot boot` only — never `flash` — so no outcome of any of this
can write to storage.
