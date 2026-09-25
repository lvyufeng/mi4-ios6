# 679: readiness printed nothing at all on every arm but a self-test one — and it had been that way since 667

678 parked its arm and the next thing to do was read the tool the safety gate tells you to run first. It
did not print a verdict. `tools/verify_press_ready.sh` **exited 1 after its header and before its first
row**, with `line 640: conseq: unbound variable`. That is not a refusal and it is not a reading: it is the
one instrument that decides whether a press is worth spending, producing nothing, on the arm the next press
would send.

Two defects, one rename, and they run in opposite directions.

## 1. What 667 did, and the two readers

667 needed a second kind of consequence text. The row that names the arm has two halves — the **entry** half
(which arm the entry image is, and what its log can be read for) and the **sub-arm** half (what the payload
does with that entry, which today only the two self-test switches change) — and 667 renamed the one variable
to make room:

| | before 667 | after 667 |
| --- | --- | --- |
| the entry-level prose | `conseq` | `entry_conseq` |
| the sub-arm prose | *(did not exist)* | `conseq`, assigned **inside the two self-test branches only** |
| the reader above (`elif [[ -n $sub_arm ]]`) | `$conseq` | `$conseq` — the sub-arm one, correct |
| the reader below (`else`) | `$conseq` | `$conseq` — **the entry one was intended, and it is now unset** |

So the rename left **one variable with no reader and one reader with no variable**. `set -u` makes the
second one fatal, and the branch it is fatal in is the one taken by every arm whose payload has **neither**
self-test switch on.

## 2. Measured, both ways

| | rows printed | exit | stderr |
| --- | --- | --- | --- |
| before | **0** | 1 | `tools/verify_press_ready.sh: line 640: conseq: unbound variable` |
| after | **5** — 4 `ok`, 1 `FAIL` | 1 | none |

The reproduction is a copy of the fixed file with the one line reverted (`tools/.r679-pre.sh`, run from the
same directory so that `REPO_ROOT` resolves, deleted afterwards): **exit 1, 0 verdict rows, `conseq: unbound
variable`**. The single `FAIL` in the repaired run is the gate's refusal on the cross-lane key (678 §6), not
a readiness finding.

**And what dies is worse than a sentence.** The row that aborts is row 4, so **row 5 is never reached** —
`a press now would be caught`, the one row that decides whether the press is worth spending. An operator
reading that output has a header, a flag derivation and nothing else, and a launcher that waits on readiness
(as the armed one does, 672) waits **forever** rather than being refused.

**It was invisible in the step that introduced it, and for a reason worth naming.** 667 measured its own
five rows against **666's arm** — a self-test arm, where `sw_on STAGE90_HW_WATCHDOG_SELFTEST` is true,
`sub_arm` is non-empty, and the `elif` reader above runs. This is a refactor validated on **one arm whose own
switch selects exactly the branch that hides the break**. And it is not about that one arm:
`stage90-build-config.txt` is byte-identical (`6c2b6038...`) across the 653 seam arm, the sleeper and 678's
arm, because the entry switches are not in the payload's dump — so **every non-self-test arm since 666 has
aborted here**, including the arm that was in `out/` when the owed press was spent.

## 3. The repair

The reader 646 wrote, restored: an arm that reaches the entry reads its consequence from `entry_conseq`.

```sh
else
  conseq=$entry_conseq
  ok 'the arm is named by a reading' "$arm: '$vline' and the entry record's STAGE90_XNU_IDLE_NO_SLEEP=$swe agrees, so $conseq"
fi
```

That line is also what makes `entry_conseq` reachable **at all** — 667 renamed the four assignment sites and
left the variable read by nothing, so the entry-level reading rule for all four arms has not been printed
since 667. The `poc` variant is the one the press's log is read with, and it now prints.

### 3.1 One address inside that prose had already gone stale, and the repair removed it

The `poc` sentence quoted the operation's call site as `entry_seam_flush:0x8047ca64`. **That address is a
site inside `entry_seam_flush`** — the function 678 edited — and it is `0x8047cab4` in 678's image. Printing
it would have put a false statement about *this* arm into the output of the tool that gates the press, which
is the class this project treats as the most serious one. The clause is removed and its absence is stated,
because the gate prints the reading that matters (the seam's own call count and the eight bytes it covers)
and the site address is not the reading.

**The other addresses in that sentence were re-measured against 678's image rather than assumed**, and they
are correct there:

```
8004589c <FlushPoC_DcacheRegion>
800458b0 <cfmdr_loop>          ee070f3e  mcr 15, 0, r0, cr7, cr14, {1}
8004632c <platform_cache_idle_exit+0x58>
8004632c: ee1d0f90  mrc 15, 0, r0, cr13, cr0, {4}     ; TPIDRPRW
80046330: e3a01001  mov   r1, #1
80046334: e59005cc  ldr   r0, [r0, #1484]
80046338: e5801130  str   r1, [r0, #304]
```

They sit **before** `entry_seam_flush` in `.text`, which is why the one address that moved is the one inside
the function that changed — a rule, not a coincidence, and the reason the fix is "drop the site address"
rather than "update it".

### 3.2 A second flag honoured in one place and not another, said out loud

Validating the repair turned up the same defect class one flag over. **`--live` moves rows 1, 2, 4 and 5 and
cannot move row 3**: the gate resolves its tree from its own directory (`preflight_boot_check.sh:21` is
`OUT=$REPO_ROOT/out/stage90`, and there is no seam for it). Measured 2026-09-25: `--live
out/stage90/frozen/armed-selftest-wdog-ef0361a2` printed rows 1/2/4/5 about the self-test park and a row 3
whose refusal named `/mnt/data/mi4-ios6/out/stage90/xnu_arm_entry-config.txt`.

Nothing is wrong with the gate's answer — it is right about its own tree — but a **green** row 3 read under
`--live` is a verdict about a different arm, four lines above the row the operator pointed the tool at. It is
not repaired by teaching the gate a seam (that would change what row 3 means for every caller, including
646's documented `--live` cells), so it is **said in the reading**:

```
**AND THIS ROW READ /mnt/data/mi4-ios6/out/stage90 AND NOT /mnt/data/mi4-ios6/out/stage90/frozen/armed-selftest-wdog-ef0361a2**
```

The clause is empty on the live run, so the ordinary reading is byte-for-byte what it was.

## 4. What was verified, and how

* **The repair, by before/after**: 0 verdict rows → 5, and the error gone. Both runs are in
  `/tmp/g679/` (`r-pre.log`, `r-live2.log`, `r-park.log`).
* **No regression on the branch the rename was written for**: with `--live`/`--park`/`--set` pointed at
  `armed-selftest-wdog-ef0361a2`, row 4 still names *the HARDWARE-WATCHDOG SELFTEST arm (666)* and still
  prints the `conseq` text — "**THIS PAYLOAD NEVER REACHES THE ENTRY**…" — so the `elif` reader and the
  `sub_arm` sentence are untouched.
* **`bash -n` clean**, and no `command not found` / `unbound` / `No such` anywhere in the output — see §5 for
  why that line is here at all.
* **The `--live` clause**: present when `--live` names another directory, absent otherwise (0 occurrences on
  the live run).

## 5. A defect this step created and then removed, recorded because it is the file's own class

The first edit of the `poc` sentence used **unescaped backticks** inside a double-quoted string. Bash ran
them: three `command not found` lines on stderr, and the printed sentence came out with its clauses hollowed
out — `0x800458b0 is  under its own  symbol`. The file's own convention is `\`` (escaped), used at lines 690
and 694, and the runner has a structural check for exactly this ("a printed string contains a backtick, so
bash will RUN it", 594's repair for the same mistake in the same file class).

**Readiness has no such check.** A tool whose output is prose is a tool whose prose can be silently
mutilated, and the mutilation is invisible from the exit code. That check is not added here — it belongs with
a decision about every string in the file, and inventing it at the end of a repair step is how a fix becomes
a second defect — but it is **owed**, and it is named so the next step does not have to rediscover it.

## 6. State, and what this changes about the press

**The arm in `out/` is unmoved** (`armed-seam-endrun-88972ba9`, `94c95342...`, 678's), the park and the
record are unmoved, **no device was touched, nothing was flashed, nothing was written to storage, and no
press is armed.**

What it changes is the shape of the blocker. Readiness now runs end to end, and its five rows for the live
arm are:

| row | verdict |
| --- | --- |
| live arm is the recorded arm | ok — 11 files, all hashed, all byte-identical to the park |
| the park verifies against the record | ok |
| the gate accepts this tree | **FAIL — the one clause the peer lane owns** (678 §6) |
| the arm is named by a reading | ok — and it now prints the entry-level consequence it has been computing since 667 |
| the press would be caught | ok — `adb` lists `4a2fe00b` as `device` |

**So the press is now blocked by exactly one thing and it is not in this lane**: the single
`STAGE90_XNU_SEAM_END_RUN` name the peer lane must add to `ENTRY_CFG_KEYS`. Everything this lane can check is
green, and the device is on the bus in adb — 677's "the next run needs no keypress" still holds.

**This does not advance 「起码要能进入操作系统，把基础驱动跑起来」** — no boot, no device, no `fastboot` — so
**TWRP to storage stays withheld**.
