# 616: every device call pinned the serial except the one that acts

The one press this phase gets is spent by a single line in `run_and_capture.sh`:

```sh
sudo fastboot boot "$IMAGE"
```

Every other device-touching call in the file pins the serial —
`sudo adb -s "$SERIAL" reboot bootloader`, and three separate
`sudo fastboot devices | grep -q "^$SERIAL"` checks. **That one did not.** `fastboot` with more than one
device listed does not refuse; it picks one. So a second phone in fastboot at the same moment redirects the
boot to it, and the run then waits for a `usb 3-10` / `4a2fe00b` return that cannot come — a **false
non-return, on the press the whole phase exists for**.

Host-side only: one script's boot step, one rehearsal's stubs and states, one document, one index row. No
boot, no build, no device, no `fastboot`, no `adb`, **nothing written to storage**; both parks were hashed
and never modified. **TWRP stays withheld.**

## 1. The second device is real, and it is a phone

`usb 3-3` carries a second phone-class device that cycles Android → fastboot → QDL, repeatedly. Measured
out of the host log rather than remembered:

| port | serial | records | ids seen |
| --- | --- | --- | --- |
| `usb 3-3` | `33e80afe…` | 218 × `18d1:d001`, 49 × `33e80afe` | `d001`, `d00d` (**fastboot**), `4ee2`/`4ee7` (Android), `9008` (QDL) |
| `usb 3-10` | `4a2fe00b` | **1530** records | the Mi 4, and only the Mi 4 |

`usb 3-3` entered **fastboot** (`18d1:d00d`) **12 times** in this log window. The Mi 4 has never appeared
on 3-3 — 1530 records of `4a2fe00b`, every one on 3-10 — so the two are separable, but only if the call
says which one it means. The `05c6:9008` on 3-3 that a bare vendor grep finds is that neighbour in QDL
mode, which the safety gate's own rule already warns about ("read the port, not a vendor id"); this step
found a second consequence of the same neighbour, one layer further along.

## 2. The fix: the guard counts, and the call names

The boot step now reads the fastboot list once, refuses an ambiguous count, and pins the call:

```sh
FB_LIST=$(sudo fastboot devices 2>/dev/null || true)
FB_COUNT=$(printf '%s\n' "$FB_LIST" | grep -c . || true)
if [[ $FB_COUNT -ne 1 ]]; then
  ... say ... ; die "fastboot lists $FB_COUNT device(s); nothing was booted. ..."
fi
sudo fastboot boot -s "$SERIAL" "$IMAGE"
```

**The count is deliberately the only new branch, and that is the third version of it.** The first added a
second branch refusing "the one device is not `$SERIAL`" — **unreachable**, because the `grep -q "^$SERIAL"`
two lines above already refuses that list. The rehearsal's own cell is what showed it: the cell asserting
the new message failed because the older check had fired first. A branch nothing can reach, carrying a
comment that says what it does, is this project's most-repeated defect. So the three guards are now a
partition, and naming it is the point of the step:

| guard | the state it refuses |
| --- | --- |
| `step "device"` mode detection | no device with `$SERIAL` anywhere |
| the boot step's presence check | adb mode, serial never shows in fastboot |
| **the boot step's count guard** (new) | **`$SERIAL` present *among others*** |

The other two ask whether `$SERIAL` is *present*. Neither asks whether it is **alone** — which is exactly
the state `fastboot boot` without `-s` silently picks from. Only the third was missing.

## 3. Both halves are falsified, and the rehearsal enforces the pin

`tools/rehearse_live_path.sh` (26 ok / 0 failed: 9 live-path + 13 reading + 4 path) gained two states and a
stub change that makes the pin structural rather than asserted:

* the fastboot stub now accepts the boot **only** as `boot -s 4a2fe00b <image>`. A bare `fastboot boot` is
  refused *by the stub*, so the happy-path state itself proves the serial is on the command line. Its
  `case` arm matched `boot *` before — which is precisely why the pin could have been removed with every
  cell still green.
* `two-devices-in-fastboot` and `one-device-wrong-serial` are the two lists the guard must refuse.

**Measured falsifications, one run each, runner restored byte-identically afterwards:**

| mutation | result |
| --- | --- |
| remove `-s "$SERIAL"` from the boot call | `happy-adb` **FAIL — exit 64** (the stub refuses a bare boot); 6 of 9 states red |
| remove the count guard | `two-devices-in-fastboot` **FAIL — exit 0, promised 1**: it *booted* with two devices in fastboot |

The second is the hazard reproduced rather than described. The first is the pin held structurally.

**And the rehearsal's third cell was wrong twice, which is the finding about the instrument.** 
`one-device-wrong-serial` was asserted first against the unreachable new branch, then against the boot
step's presence check — also unreachable, because mode detection is earlier still. A lone wrong-serial
device is refused at `step "device"`, the *first* of the three. Two wrong expectations in a row on one cell
is the same shape as 614's hand-written count: the instrument's own claim about which check fires, written
before the run that would settle it.

## 4. A defect in the fix, caught by an older guard

The first version of the new `say` text used an **unescaped backtick** around `fastboot devices`. The
runner has refused that since 594 — a printed string with a bare backtick is a command bash will run — and
it fired on the very first `--dry-run`, before anything else in this step ran:

```
run_and_capture: a printed string contains a backtick, so bash will RUN it:
2161:    say "          `fastboot devices` checks above pass in this state - they ask whether $SERIAL is"
```

The fix was one escape, and the guard behaved exactly as its own doc says. Recorded because it is the
second time this step's own text tripped a check written for the same failure: **the guard is older than
the defect it caught, and that is the only reason it was cheap.**

## 5. What this does not do

* **It does not touch the device, the arm, the payload or any parked byte.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…`, the arm is still the sleeper, and the press is still
  the user's.
* **It does not clear the neighbour from the bus.** It refuses to act while the list is ambiguous; if
  `usb 3-3` is in fastboot at the same moment, the run **refuses and boots nothing**, and the operator's
  next action is to wait for the neighbour to leave or to unplug it. That is a refusal, not a failed run —
  worth knowing before the press, because it changes what a red run means.
* **It does not make `fastboot boot` fail-safe in general.** `-s` and the count guard cover *this* host's
  measured state (two phone-class devices, one fastboot-capable neighbour). A third device appearing
  between the check and the call is a race the guard cannot see, which is why the pin is on the command
  line as well as in the check.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus — last
  `usb 3-10:` disconnect with serial `4a2fe00b` at **2026-09-23 01:18:06**, now **8.6 h**; `adb devices`
  and `fastboot devices` both empty; `usb 3-3` currently holds the neighbour in QDL (`05c6:9008`), which is
  not a fastboot state. The one event that can move the goal is the user's power press, and what this step
  changes is that the press cannot be spent on the wrong phone.

## 6. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One script
edited (`stages/stage90/run_and_capture.sh` — the boot step's list read, count guard, and `-s` pin), one
rehearsal extended (`tools/rehearse_live_path.sh` — two states, three markers, and a stub that requires the
serial). The gate was re-run → **EXIT=0 / 549 lines / 0 stderr**, unchanged; the runner's own
`--dry-run --allow-xnu-entry` → **exit 0 / 0 stderr**. `rehearse_live_path.sh` → **26 ok / 0 failed**, exit
0, and both mutations above were measured with the runner restored byte-identically (`cmp`). Both parks
verify **exit 0** against their recorded sets, hashed in place with no file modified. The payload, both
parks and the arm the next press sends are unmodified. `fastboot boot` only — never `flash` — so no outcome
of any of this can write to storage.
