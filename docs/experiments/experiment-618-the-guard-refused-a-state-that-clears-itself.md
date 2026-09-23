# 618: the ambiguity guard refused a state that clears itself in seconds

616 added the guard this step rewrites, and its reason was sound: `fastboot boot` with more than one
device listed does not refuse, it *picks one*, so a second phone in fastboot at the same moment could
take the boot and the run would then wait for a return that cannot come. The guard refuses an ambiguous
list, and `-s "$SERIAL"` is on the acting call as well.

This step found that the guard refuses a state which, on this host, **usually clears by itself in
seconds** — and that on 2026-09-23 it was refusing one that had not cleared in 47 minutes. The device
this mattered for is the one the phase is waiting on: with the neighbour parked in fastboot, a press of
Vol-Down + Power produces a refusal and no run.

Host-side only: one script, one rehearsal, one document, one index row. No boot, no build, no device,
no `fastboot`, no `adb`, **nothing written to storage**; the arm and both parks were hashed and never
modified. **TWRP stays withheld.**

## 1. The measurement: the state is usually transient, sometimes not

The neighbour on `usb 3-3` (serial `33e80afe…`) enters fastboot repeatedly. Every window in the host
log, entered → left, most recent last:

| entered | window |
| --- | --- |
| 09-16 18:35:16, 09-16 20:05:05, 09-17 00:55:54, 09-17 16:10:10, 09-17 16:31:05, 09-17 16:53:29 | 12.9, 1.6, 13.0, 3.3, 3.2, 3.1 s |
| 09-17 16:59:18 | **1946 s (32 min)** |
| 09-21 02:08:21, 04:33:51, 14:06:20, 16:21:36, 19:00:29 | 3.1, 11.9, 1.9, 9.8, 152.9 s |
| **09-23 10:59:41** | **still open** — 47 min at the time of measurement |

**Eleven of the thirteen are under 14 s.** So the guard as 616 wrote it refuses, on its first look, a
state that in the common case would have cleared before a human finished reading the message — and the
cost of that refusal is not zero: the operator has to notice, work out which of two devices moved, and
re-run. The long tail is real (32 min, 152.9 s, and the current 47 min and counting), which is why a
wait cannot *replace* the refusal; it can only stop the common case from costing a re-run.

## 2. The change: judge the list as one value, and wait before refusing

Two edits, both inside `run_and_capture.sh`'s boot step.

**The condition is now "exactly one device, and it is `$SERIAL`"**, in one helper that takes the list as
an argument:

```sh
fastboot_pinned_only() {
  local list=$1 n
  n=$(printf '%s\n' "$list" | grep -c . || true)
  [[ ${n:-0} -eq 1 && $list == "$SERIAL"$'\t'* ]]
}
```

This is stronger than 616's `count != 1`, and the interesting part is *when* the two differ. At the
moment the guard is entered they are equivalent, because the `grep -q "^$SERIAL"` two lines above has
already established that `$SERIAL` is present — so "exactly one" implies "the one is `$SERIAL`", and a
separate wrong-serial branch would be unreachable, which is exactly what 616 measured and deleted. The
equivalence breaks **once a wait is in between**: the list can lose this phone and keep the stranger, and
a bare count test would then read `1` and pass.

What the stronger form buys is a **refusal instead of a raw `fastboot` error**. With `-s "$SERIAL"` and
no such device on the bus, `fastboot` fails with its own message — a failure whose status this file's
exit contract has no clause for, which is `mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one`
with `fastboot` as the producer. The case is reachable *only* through the wait, so it is folded into the
one condition rather than added as a second branch: **one test, entered twice, and no branch that
nothing can reach.**

**And the guard waits before it refuses:**

```sh
  FB_LIST=$(sudo fastboot devices 2>/dev/null || true)
  if ! fastboot_pinned_only "$FB_LIST"; then
    say "note: ... waiting up to ${FB_AMBIG_WAIT}s for the list to settle ..."
    _fb_t0=$(date +%s); _fb_deadline=$(( _fb_t0 + FB_AMBIG_WAIT ))
    while ! fastboot_pinned_only "$FB_LIST"; do
      [[ $(date +%s) -ge $_fb_deadline ]] && break
      sleep 2
      FB_LIST=$(sudo fastboot devices 2>/dev/null || true)
    done
    if fastboot_pinned_only "$FB_LIST"; then
      say "      the list settled to $SERIAL alone after $(( $(date +%s) - _fb_t0 ))s"
    fi
  fi
  if ! fastboot_pinned_only "$FB_LIST"; then ... die "fastboot did not settle to $SERIAL alone within ${FB_AMBIG_WAIT}s ..." ; fi
```

`FB_AMBIG_WAIT=${FB_AMBIG_WAIT:-60}` is a tunable beside `RETURN_TIMEOUT`, and 60 s is chosen against
the table above: it covers eleven of the thirteen measured windows and does not pretend to cover the
tail. **The wait cannot turn a refusal into a wrong boot** — the list is re-tested after it by the same
test — and it cannot cost anything else, because the phone is already in fastboot and nothing has been
sent: a `fastboot boot` that is never reached writes nothing.

The refusal message changed with it. It used to say `fastboot lists 2 device(s)`, which is only true of
one of the three states the condition now catches; it says `did not settle to $SERIAL alone within
${FB_AMBIG_WAIT}s (it lists $N device(s))`, which is true for N = 0, 1 and 2. The exit code is unchanged
(**1**, host-list failure, nothing booted) and the header's exit-1 clause now names this producer.

## 3. The stub was made faithful, because one cell depends on it

`rehearse_live_path.sh`'s fastboot stub printed a list from a marker. Two changes:

* **A call counter.** `leave_after:N` prints the two-device list for the first N calls and then the
  state's normal list, which is how the wait is exercised: the switch has to land on the call the wait
  makes. The count is not guessed — with the phone already in fastboot, the runner's calls are (1) mode
  detection, (2) the boot step's presence check, (3) the guard's first read, then one per wait poll, so
  `leave_after:3` puts the switch on the wait's first refetch. If that call count ever changes, the cell
  goes red rather than quietly testing nothing, because a wait that never runs leaves the refusal in the
  output and the cell forbids it.
* **The boot is checked against the list.** The stub now refuses `boot -s 4a2fe00b <image>` when
  `4a2fe00b` is not in the last list it printed, with `fastboot: error: Device 4a2fe00b not found`. A stub
  that booted anyway would have made `-s` look like a guarantee in a state where it is a no-op — and it
  would have let the count-only condition pass the cell that exists to catch it.

## 4. The cells, and both falsifications

The battery grew **29 → 31 ok / 0 failed** (14 live-path + 13 reading + 4 path), exit 0.

| cell | what it asserts |
| --- | --- |
| `neighbour-leaves-fastboot` (new) | the tripwire case: the list is ambiguous when the guard looks, unambiguous when the wait refetches → the run **waits and then boots**. Forbids `cannot act on a`. |
| `fastboot-other-after-wait` (new) | the phone leaves fastboot and the stranger stays → **refuse with a message**, not with `fastboot`'s error |
| `two-devices-in-fastboot` | the permanent ambiguity: the guard waits its whole budget and refuses — one cell covering both halves of the change |

| mutation | result |
| --- | --- |
| the wait removed (`while false`), nothing else | `neighbour-leaves-fastboot` **FAIL — exit 1, promised 0**, and *said what this state must NOT say: `cannot act on a`*. The other 13 green, including `fastboot-other-after-wait`, which must still refuse |
| the condition cut back to the count alone (`-eq 1`) | `fastboot-other-after-wait` **FAIL — did not say: `did not settle to`**; 13 green. It exits 1 for the *wrong reason* — `fastboot`'s own error — which is precisely the failure the stronger condition exists to prevent |

Both were measured one run each, runner restored byte-identically afterwards (`cmp`).

## 5. What this does not do

* **It does not make the press work right now.** Measured at 11:46 on 2026-09-23, `sudo fastboot devices`
  lists `33e80afe fastboot` and has since 10:59:41 — the *parked* case, not the transient one. A press in
  that state now waits 60 s and then refuses with a message that says it waited; the fix for the parked
  case is the operator's (unplug the neighbour, or wait for it to leave), which is what I would recommend
  rather than relaxing a guard the operator has not asked me to relax. **What this changes is the common
  case: eleven of the thirteen measured windows no longer cost a re-run.**
* **It does not touch the device, the arm, the payload or any parked byte.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…` (8,540,160 bytes), the entry still `696a0f39…`,
  the arm is still the sleeper, and the press is still the user's.
* **It does not weaken the pin.** `-s "$SERIAL"` was already on the acting call and still is; the guard
  is a warning that refuses, and this step makes it refuse *later* in the transient case, never
  conditionally.
* **It does not sweep the other `fastboot devices` reads.** The mode detection at `step "device"` and the
  boot step's presence check still use a bare `grep -q "^$SERIAL"`, and that is correct for them: they
  ask whether this phone is present, and a list holding it *among others* answers yes. Only the acting
  call's guard needs the whole list.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus — last
  `usb 3-10` disconnect with serial `4a2fe00b` at **2026-09-23 01:18:06**, now **10.5 h**; `adb devices`
  is empty, and the only phone-class device on the host is the neighbour. The one event that can move
  the goal is the user's **Vol-Down + Power**, and what this step adds is that a press is less likely to
  be answered with a refusal it did not deserve.

## 6. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One script
edited (`stages/stage90/run_and_capture.sh` — `FB_AMBIG_WAIT`, `fastboot_pinned_only`,
`fastboot_list_count`, the boot step's guard and its two messages, and the header's exit-1 clause), one
rehearsal extended (`tools/rehearse_live_path.sh` — the call counter and `leave_after:N`, the boot stub's
list check, and three cells). The gate was re-run → **EXIT=0 / 549 lines / 0 stderr**, unchanged; the
runner's own `--dry-run --allow-xnu-entry` → **exit 0 / 0 stderr**; `rehearse_live_path.sh` → **31 ok /
0 failed**, exit 0, and both mutations above were measured with the runner restored byte-identically.
`rehearse_revert_set.sh` → **38 ok / 0 failed**. Both parks verify **exit 0 / 11 files** against their
recorded sets, hashed in place with no file modified. The payload, both parks and the arm the next press
sends are unmodified. `fastboot boot` only — never `flash` — so no outcome of any of this can write to
storage.
