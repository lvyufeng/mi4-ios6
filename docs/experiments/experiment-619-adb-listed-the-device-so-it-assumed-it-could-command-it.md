# 619: adb listed the device, so the runner assumed it could command it

`run_and_capture.sh` mode detection asked one question:

```sh
if sudo adb devices 2>/dev/null | grep -q "^$SERIAL"; then
  MODE=adb
```

`adb devices` prints `SERIAL<TAB>STATE`, and that grep matches field 1. **Every state column matches.**
Measured on this host, all seven:

| state | matches `grep -q "^$SERIAL"` |
| --- | --- |
| `device`, `unauthorized`, `offline`, `no permissions`, `sideload`, `recovery`, `bootloader` | **yes** |

So the branch that needs a device it can *command* was entered on a listing that only proved the device
is *there*. The two are not the same, and the failure is the expensive kind: from `unauthorized` — the
host's key not accepted on the device, after a wipe or a cleared `/data/misc/adb/adb_keys` — the
`adb -s "$SERIAL" reboot bootloader` that follows **fails**, the 30-poll wait for fastboot expires, and
the run ends at `die "device did not appear in fastboot"`: **exit 1, sixty seconds later, on a message
about fastboot, with a press spent and nothing sent.** The operator is told to look at the wrong device
state; the device was sitting there waiting for an RSA prompt.

Host-side only: one script, one rehearsal, one document, one index row. No boot, no build, no device,
no `fastboot`, no `adb`, **nothing written to storage**; the arm and both parks were hashed and never
modified. **TWRP stays withheld.**

## 1. Where this came from, and what I did with it

The finding is the **peer session's** (`run-experiment-526`), who measured all six states against the
predicate, grepped both the runner and `stages/stage90/` for `unauthorized` (absent), and flagged that
the adb-first order means a bad-state adb listing is evaluated *before* a good fastboot one. I
reproduced the predicate result independently on this host before changing anything — the table above is
my measurement, not a quotation — and the two defects are in my file and my lane.

Reachability, honestly: **low, and the low reachability is the reason this is a small step rather than an
urgent one.** The device's adb key has been accepted before (533/535/574 all rebooted it over adb) and
that key lives in `/data/misc/adb/adb_keys`, which a boot does not clear — so `unauthorized` is a
first-contact-after-a-wipe risk, not a routine one. `offline` is the more likely trigger, and it is the
more interesting one because it is **a race rather than a state**: adb can report a device `offline` for
minutes while Android is up and adbd is wedged, and a run that started in that window would spend the
press.

## 2. The fix: a second reader, at the one call site that needs it

A reader that returns the **state**, with the same UNREAD convention the host-log readers use — nothing
when adb cannot be read at all, or does not list the serial, because a failed read and a read of "not
there" must not be one value:

```sh
adb_state() {
  local out
  out=$(sudo adb devices 2>/dev/null) || return 0
  [[ -n $out ]] || return 0
  printf '%s\n' "$out" | awk -v s="$SERIAL" '$1 == s { print $2; exit }'
  return 0
}
```

and mode detection is now **four questions in an order with no unreachable branch** — 616's lesson applied
to a different predicate:

| # | condition | result |
| --- | --- | --- |
| 1 | adb lists it **and** the state is `device` or `recovery` | the adb path |
| 2 | `fastboot devices` lists it | the fastboot path |
| 3 | adb lists it in a state adbd cannot be commanded through, fastboot does not have it | **a message naming the state and what to do about it** |
| 4 | neither | the dark-device message |

**fastboot is asked before the bad-state message, deliberately.** The usual argument is that asking adb
first cannot mask a fastboot listing, because a serial cannot be in adb and fastboot at once — a fact
about USB, not about this script. A branch that only survives because of a fact it does not check is the
shape this file keeps paying for, so the check costs one command and the assumption goes away.

`recovery` counts as usable and `sideload` does not — that is a **choice, not a fact about adb**:
`reboot bootloader` is a command any rooted, running adbd will accept, and TWRP's adbd answers it, while
sideload mode's adbd serves exactly one command and it is not one this file sends. The choice is written
down where it is made.

## 3. The same defect one line later, and the return read where it is *correct*

Two adjacent sites, both deliberate:

* **`sudo adb -s "$SERIAL" reboot bootloader` now carries `|| die`.** Without it, a failing `adb` aborts
  the script under `set -e` and the exit code the operator sees is **adb's own**, for which this file's
  contract has no clause — `mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one`, one line below
  the fix for a different instance of the same family. The state check makes this *should be* unreachable
  from `unauthorized`/`offline`; "should be unreachable" is what the check is, not a proof about adb.
* **The return read at the wait loop keeps the plain `grep` on purpose.** The question there is *"did the
  device come back"*, not *"can adb command it"* — and a device listed `unauthorized` or `offline` **has**
  come back: the SoC is running and the USB link is up. The verdict that fits is exit 3 (returned, capture
  out of reach), which is where that path leads. Tightening it would turn a real return into "did not come
  back" — exit 2, and a power press. **Two call sites, the same `grep`, opposite correct answers**, which
  is why the state reader was added at the mode-detection site rather than by rewriting the predicate
  everywhere. The reasoning is now a comment at that line, so a future reader does not "fix" it.

## 4. The stub was made as fine-grained as the reading

`rehearse_live_path.sh`'s adb stub printed `4a2fe00b\tdevice` unconditionally. It now prints the state
column from a marker (`adb_unauthorized`, `adb_offline`), because a stub whose output is coarser than
what the runner reads leaves the new branch untestable — the same defect as 616's `boot *` glob, which is
why a missing serial pin could have stayed green.

Two cells for one branch, and that is on purpose: the message **interpolates `$ADB_STATE`**, so
`adb-offline` fails a version that hard-codes `unauthorized` while `adb-unauthorized` passes it. One cell
per branch is the rule; this is one cell per *value the branch reads*.

Battery **31 → 33 ok / 0 failed** (16 + 13 + 4), exit 0, on the exact bytes committed.

| mutation | result |
| --- | --- |
| mode detection back on the serial alone (`grep -q "^$SERIAL"`) | **14 ok / 2 failed** — exactly the two new cells, both *"did not say `its state is …`"* **and** *"said what this state must NOT say: `device did not appear in fastboot`"*. The other 31 stayed green, so the two cells are the only ones that move |

That is the original defect reproduced rather than described: the broken version prints the fastboot
sentence after its sixty-second wait, and the cell catches it on both halves — the message it lacked and
the message it should not have given. Runner restored byte-identically afterwards (`cmp`).

## 5. What this does not do

* **It does not touch the device, the arm, the payload or any parked byte.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…` (8,540,160 bytes), the entry still `696a0f39…`,
  the arm is still the sleeper, and the press is still the user's.
* **It does not make the current press work.** Both of its gates are elsewhere and neither is code: the
  neighbour on `usb 3-3` must not be in fastboot when the run starts (measured: it entered fastboot at
  10:59:41 and left at ~14:56, having mode-changed twice inside one session), and the phone must be
  reachable at all. **What this changes is a failure mode that would have spent a press silently.**
* **It does not sweep `adb` usage everywhere.** Three other sites use `adb`: the `-s "$SERIAL"` pin on
  every call (which is the same pin as the fastboot one), the `exec-out` capture, and the return read
  discussed above. The first two are unaffected by the state question — a pin is a pin — and the third is
  deliberately left loose.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus — last
  `usb 3-10` event at **12:53:09**, ~2.1 h; the only thing on the port before that was a 14 s Android
  bring-up at 12:51:37 that **fell into `05c6:f006` instead of `18d1:4ee7`** — the failure this file's own
  comment already names, so that press produced no run and no log. The one event that can move the goal is
  still the user's **Vol-Down + Power**.

## 6. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One script
edited (`stages/stage90/run_and_capture.sh` — `adb_state`, the four-question mode detection, the `|| die`
on the reboot, the comment at the return read, and the header's exit-1 clause), one rehearsal extended
(`tools/rehearse_live_path.sh` — the stub's state column, two markers, two cells). The gate was re-run →
**EXIT=0 / 549 lines / 0 stderr**, unchanged; the runner's own `--dry-run --allow-xnu-entry` → **exit 0 /
0 stderr**; `rehearse_live_path.sh` → **33 ok / 0 failed**, exit 0, on the committed bytes, with the
mutation above measured and the runner restored byte-identically. Both parks verify **exit 0 / 11 files**
against their recorded sets, hashed in place with no file modified. The payload, both parks and the arm the
next press sends are unmodified. `fastboot boot` only — never `flash` — so no outcome of any of this can
write to storage.
