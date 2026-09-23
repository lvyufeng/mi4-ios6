# 601: the runner's first step read a missing log as an empty one, and leaked eleven grep errors

The press is the scarcest thing this phase has, and the next one is already owed. So the question
this step asks is not what the log will say when it arrives — it is **whether the runner will get
through its own first step to produce one**, on the state the machine is actually in.

It would not have, cleanly. `$LOGFILE` (`/tmp/cancro-last_kmsg.txt`) does not exist at this moment,
and `summarise_log` — the function the run calls at step 1, before anything is booted, and again at
step 5 on the log it just captured — ran **eleven `grep` invocations against a path with no file
behind it**. Each wrote `grep: <path>: No such file or directory` to **stderr**, and the `|| true`
that swallows their exit status does not swallow that message. The run then printed
`MI4IOS6_STAGE90 lines: ` with **no number**, seven blank marker rows, and
`hardware watchdog: NOT confirmed armed`.

Host-side only: one file edited, no build, no device, no `fastboot`, no `adb`, nothing written to
storage. **TWRP stays withheld** — 「如果os已经能进去了的话」 is unmet.

## 1. How it was found, and why the reader could not have found it

Not by reading the file. By **rehearsing the live path** — 588's discipline, and this project's
standing rule that the step after the point of no return is the least tested. A `sudo` stub on
`PATH` (which never invokes the real `sudo`, `adb` or `fastboot`, and refuses anything it does not
model) made the runner take steps 1–5 end to end with no device: gate → detect → boot (a no-op stub)
→ return read from a synthesised host log → capture `cat /proc/last_kmsg` → **read the captured
log**.

`--summarise` **cannot reach this state at all**: the CLI guards it with
`[[ -f $SUMMARISE_ONLY ]] || die "no such log: ..."` (`run_and_capture.sh:1733`), so a missing path
is a usage error and exits 1 with one named line. That is correct for the CLI and it is exactly why
the defect survived: **the state is reachable only on the live path, and the live path was the one
with no reader.** Every `--summarise` reading this phase has ever taken — the 22 states of 600, the
eleven of 598, the six of 599 — ran on files that existed.

## 2. The defect, precisely

```sh
  local n
  n=$(grep -a -c 'MI4IOS6_STAGE90' "$log" || true)
  say "MI4IOS6_STAGE90 lines: $n"
  if [[ $n -eq 0 ]]; then      # <- TRUE when $n is the empty string
```

Three things compound, and the third is the one that matters:

1. `grep -c` writes its error to **stderr**; `|| true` discards only its **exit status**. Eleven
   such calls (the count, seven markers, and the three read-back counts) are all unsuppressed.
2. `n` comes back as the **empty string**, not as `0`, because grep produced no number at all.
3. `[[ "" -eq 0 ]]` **is true** in bash — an empty operand is arithmetically zero. So the branch
   that says "the log has no payload output" was reached by a *value that was never produced* and
   not by a *count of zero*, and the same trick silently carried `watchdog`, `deadman` and `abort`
   through their `-gt 0` tests as zeros.

And then it made a claim it must not:

```
reading:
  hardware watchdog: NOT confirmed armed (no counter_running=1). If this run also
                     failed, the watchdog is the first thing to investigate.
```

That line is printed twelve lines above a comment which says the opposite in as many words —
*"Saying anything about the watchdog here would be an inference from absence, which is the
misattribution this whole session has been correcting."* The `-eq 0` guard that is supposed to
prevent it was satisfied by the empty string, so it did not prevent it.

This is the project's most-repeated class in its plainest form: **a producer that delivered
nothing, read as a value that was delivered** — here an absent *file* rendered as a count of
`0`, and a missing *number* rendered as a measurement about the hardware watchdog.

## 3. The fix

Two states, named separately, because they are different facts — 588's rule, and the reason the
`nofile`/`parked`/`nopark` case block exists at all — placed at the top of `summarise_log`, before
any grep runs:

```sh
  if [[ ! -e $log ]]; then
    say "MI4IOS6_STAGE90 lines: NONE (no file at $log)"
    say "NONE. There is no log at that path at all, so no line of it was counted and every count"
    say "below is absent rather than zero: ..."
    return 0
  fi
  if [[ ! -r $log ]]; then
    say "MI4IOS6_STAGE90 lines: UNREAD (a file is there at $log and is not readable)"
    say "UNREAD. ... this is a permissions or ownership state (511/512) and not a reading. ..."
    return 0
  fi
```

Both `return 0`, and that is deliberate rather than incidental: this function's last statement was
once `[[ ... ]] && say ...`, whose status-1-on-false made the **caller** exit under `set -e` before
it booted anything — four consecutive experiment-267 "runs" that never happened. A guard that can
make the run stop before the boot is a worse defect than the noise it removes, so both branches end
in an explicit `return 0`, and the note about that earlier defect is cited in the new comment block.

The `-e` / `-r` split is not decoration: an **empty log is a file that exists**, and it must keep
taking the old `NONE` branch (596a read that state). It does — measured below.

## 4. Attribution: pre-existing, and measured rather than assumed

The defect is **not 600's**, and saying so required running HEAD's own runner through the same stub
rather than reasoning from the diff. With HEAD's runner staged at `stages/stage90/zz-head-cmp.sh`
(the **same depth**, because the script does `cd "$(dirname "$0")"` and resolves `STAGE_DIR` from
there) and both run against the same stub:

```
HEAD   EXIT=0 stderr_lines=11
NOW600 EXIT=0 stderr_lines=11
-> identical after normalising the path
```

Both print `MI4IOS6_STAGE90 lines: ` with the trailing space and no number. 600 is cleared.

**My first attempt at that comparison was itself defective**, and in the way this project has now
paid for three steps running: the HEAD copy was written to `/tmp/r600live/head-runner.sh`, so
`STAGE_DIR` resolved to `/tmp/r600live`, the gate was looked for at
`/tmp/r600live/preflight_boot_check.sh`, and the run died at `the gate refused` — a rehearsal
*harness* failure wearing the shape of a runner failure. Same trap as 598 §5 (one directory too
shallow) and 600 §7 (a HEAD copy that was not executable). The lesson is not "remember the depth";
it is that a copy of this script run from anywhere else is not the script, and the comparison is
only meaningful at the same depth in the same tree.

## 5. Verified in both directions

**The live path, with the fix, `$LOGFILE` absent (the real state):**

| | before | after |
| --- | --- | --- |
| exit status | 0 | 0 |
| stderr | **715 bytes, 11 lines** of `grep: ... No such file or directory` | **0 bytes** |
| step 1's count line | `MI4IOS6_STAGE90 lines: ` (blank) | `MI4IOS6_STAGE90 lines: NONE (no file at /tmp/r601/live/last_kmsg.txt)` |
| watchdog line | `NOT confirmed armed` — an inference from an absent file | not printed |
| later steps | reached the capture and the reading (545 / 548) | reached them (545 / 548) |

**And the fix changes nothing where the file is present.** 24 states, the edited runner against
HEAD's, `cmp`-ing stdout *and* byte-counting stderr:

| states | result |
| --- | --- |
| 513 run 1 / run 2, `syn-A`…`syn-G`, `syn-empty`, 514 ×2, 515 ×2, 516 ×3, 533-A/-D/-J, `cancro-last_kmsg.txt.prev`, `t566-real520.txt`, **an empty file** | **byte-identical, stdout and stderr — 23 of 24** |
| an **unreadable** file (`chmod 000`, owned by root) | **differs, and only here**: HEAD 550 bytes of stderr with a blank count and seven blank markers; the fix 0 bytes with the state named |

The empty-file row is the one that matters most: it proves the new `-e` branch did not capture a
state that belongs to the old one.

**Exit-status and gate checks:** `--summarise <missing path>` still refuses with
`run_and_capture: no such log: ...` and exit 1 (unchanged, pre-existing); the gate
`preflight_boot_check.sh --allow-xnu-entry` → **EXIT=0 / 518 lines / 0 stderr**, its exit census
still 3 sites with codes `2 3`, now at `:1921`/`:1953`/`:1963` (600 saw `:1884`/`:1916`/`:1926`; this
step is +37/−0 and adds no `exit` or `die`). `out/stage90/stage90-qcdt.img` is still `60063c47…`.

## 6. The rehearsal itself, recorded so it can be repeated or promoted

```
stub:  /tmp/r600live/bin/sudo      (built for 600's rehearsal, reused here; intercepts sudo adb / sudo fastboot / sudo dmesg only;
                                    REFUSES everything else, so no real device tool is reachable)
state: /tmp/r600live/state/booted  (flips when the stubbed `fastboot boot` runs)
run:   PATH=/tmp/r600live/bin:$PATH \
       LOGFILE=/tmp/r601/live/last_kmsg.txt RETURN_TIMEOUT=6 CAPTURE_WAIT=6 \
       stages/stage90/run_and_capture.sh --allow-xnu-entry
```

It never boots anything — `fastboot boot` is a stub that touches a file — and it never reaches a
device, because the stub replaces `sudo` itself rather than the tools behind it. This is the only
exercise the live path has had since 588, and it is **not** a committed tool: it lives in `/tmp`,
and promoting it to `tools/` is a step this one names and does not take.

## 7. What this does not do

* **It does not boot anything, and it does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** No
  device action was taken or is possible: the phone is off the bus (last event `usb 3-10` disconnect,
  serial `4a2fe00b`, 2026-09-23 01:18:06), `adb devices` and `fastboot devices` are both empty, and
  the one Qualcomm device on the bus is **not** the phone — `05c6:9008` at `usb 3-3`, whose own serial
  is `33e80afe-v63-usbd-disabled-rndis`: the neighbouring device this project's safety note already
  names, and at this moment the exact vendor:product the Mi 4 would present *in EDL*. Nothing was
  attempted on it.
* **It changes no arm, payload, prediction or gate verdict.** The image the press sends is still
  `60063c47…`, and rung 0 and rung 1 still decide it.
* **It does not make the run's reading better** — only its *entry* honest. What the capture says is
  still 594/595's ladder and 596's goal block.
* **It does not fix the other unsuppressed counts.** `keyval` already carries `2>/dev/null`; the
  `grep -a -c` at `:545`/`:546`/`:804`/`:805`/`:1597-1599` and the two at `:2098`/`:2123` are still
  bare — they are unreachable on a missing file (the guard returns first) and the last two run only
  after a successful capture, so this is a **sweep left named rather than taken**, not an oversight
  discovered and forgotten.

## 8. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One
host-side file edited (`stages/stage90/run_and_capture.sh`, +37/−0, not in
`xnu_arm_entry-sources.txt`), one temporary same-depth copy of HEAD's runner created for the oracle
and deleted, one root-owned file created under `/tmp` for the unreadable state and removed, and one
`sudo` stub under `/tmp` that cannot reach a device. The payload, the parked frozen pair at
`/tmp/r594/frozen-payload/` and the arm the next press sends are unmodified — verified by re-running
the gate, not by assumption. `fastboot boot` only — never `flash` — so no outcome of any of this can
write to storage.
