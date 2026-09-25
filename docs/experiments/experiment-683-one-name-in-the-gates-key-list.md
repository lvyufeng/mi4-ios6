# 683: one name in the gate's key list, and the name row 4 owed

Two instruments stood between the parked arm and a press, and both were **names**, not checks. This step
repaired them, verified both directions of each, and put the press in reach. **No device was touched here**;
the press this unblocked is `experiment-684`.

## 1. The gate refused the arm over one missing key — and that was the whole blocker

`678`'s arm is the first this project built whose entry record carries a key the gate does not print:

```
xnu_arm_entry-config.txt   (last line)   STAGE90_XNU_SEAM_END_RUN=1
build_entry.sh:543         ENTRY_ARM_KEYS  ... STAGE90_XNU_SEAM_END_RUN STAGE90_XNU_IDLE_NO_SLEEP)
preflight_boot_check.sh    ENTRY_CFG_KEYS  ... STAGE90_XNU_SEAM_MEASURE
                                            STAGE90_ENTRY_CHECKPOINT ...
```

Three lists, and the third one is a copy that had fallen one name behind. The gate's *converse* clause
(`:585-588`) refuses any record key it does not print — deliberately, because "a switch recorded on the build
side and not shown here is a switch the next run would go out with unread" — so **every press of 678's arm
would have been refused before the device was touched**, and the launcher, which waits on a refusal rather
than exiting (672), would have waited forever.

Measured: exit 1 / 69 lines without the name, exit 0 / **550 lines** with it. The armed launcher would have
spent nothing and reported `GATE REFUSED`.

**This file is the peer lane's, and the change was made here** because its owner session had been unreachable
for two days with the press waiting on exactly this. The authorisation for that is the operator's, and it is
recorded in the commit. **What the change does and does not do:** it adds one name to a list this gate
*prints* — so the arm's record is read out loud rather than filtered — and it touches no safety clause. The
entry-sources manifest check, the storage tripwire, the arm-identity comparison and the exit census are all
byte-unchanged, and the gate's census still reads **5 exit site(s) / codes `1 2 3`**.

The two prose pieces that counted the variant set as eight are corrected in the same edit (`:500-504` and
`:574`, the `fail` message's own enumeration), because a gate whose prose names the wrong number of keys is
the same defect one layer up. The `9 in, 4 out` measurement further up is **left as it is**: it is a record of
a measurement taken when the record held nine keys, and rewriting it would claim a measurement nobody took.

**The lesson is the one 675 §4 named and this step paid for**: a key list written down three times goes stale
in the copy that nobody edits, and the refusal it produces looks exactly like a safety refusal while being a
bookkeeping one.

## 2. Row 4 named 678's arm as 653's, and that is 680's defect one file over

Readiness was green on four rows and red on the gate's, so the next thing to do was read *what* it said. Row
4 printed:

> the ACTING arm (653: the seam WITH its operation, SEAM_POC=1 and SEAM_MEASURE=0) …

and then 535's consequences: *the pop's death*, *a pop that still died on a stale value*. **On 678's arm the
pop is never executed** — the seam publishes and then ends the run — so that sentence describes a code path
this press's log cannot contain. That is exactly what 680 found in `run_and_capture.sh` and repaired there;
readiness had no reader for the ending switch at all:

```
tools/verify_press_ready.sh   grep -c SEAM_END_RUN  ->  0
```

**And the entry reading alone cannot fix it**: 653's arm and 678's are the same sentence, because the
operation is byte-identical between them and only the ending is new. So the arm's *name* has to be the reading
**joined to the record**, which is the rule 667 wrote the row for in the first place — it just had not been
extended to the third switch.

The repair reads `STAGE90_XNU_SEAM_END_RUN` from the same record the rest of the row reads, and appends:

* when it is `1` — the arm is named as **678's ending arm**, and the consequence says the FACTS above hold and
  their CONSEQUENCES do not, what `STALE LINE, WRITTEN OUT` reads on this arm, that `seam_calls >= 1` with no
  pair at all is *the operation did not return* and not *a dirty line* (680's sentence, said here before the
  press instead of after it), and that **a hang at the pop is not a possible failure of this press**;
* when the seam is the operation arm and the record does **not** name the switch — the image predates 678 and
  its seam returns through the pop, *which is a fact about which image this is and never a zero*.

The append happens **once**, after the existing name-and-consequence block, rather than as a second copy of
those sentences: two copies of one reading is the defect class this project pays for most often.

**Verified both directions**, because a repair validated on one arm is how 667's break survived:

| arm | row 4 |
| --- | --- |
| `armed-seam-endrun-88972ba9` (the live arm) | names 678's ending arm; no "predates" clause |
| `armed-seam-poc-a43304f2` read as a live tree | keeps the ACTING arm name and gains *does not name SEAM_END_RUN at all … its seam RETURNS*; **does not** claim the ending |
| `armed-selftest-wdog-ef0361a2` | unchanged — the sub-arm branch uses its own `conseq` |

`bash -n` clean, and no `command not found` / `unbound variable` on any of the three runs.

**One thing this step found and did not repair:** `ARM_CFG` is `$LIVE/xnu_arm_entry-config.txt` (`:567`), so
under `--park`/`--set` row 4 still describes the **live** arm while rows 1 and 2 describe the park. That is
679 §3.2's "a flag honoured in one place and not another" one flag over, it is pre-existing, and this step's
clause inherits the row's existing scope rather than extending the inconsistency. It is named here so the
next step does not have to rediscover it — and so that nobody reads a `--park` row 4 as a verdict about the
park.

## 3. State

`bash -n` clean on both files, readiness **5 of 5 ok, exit 0**, the gate **exit 0 / 550 lines**, the arm in
`out/` unmoved (`armed-seam-endrun-88972ba9`), the park and the record unmoved. **No device was touched in
this step** — every command was a read of the tree or a run of a tool against it. Nothing was flashed,
nothing was written to storage, and no firer was armed at any point, so 660 §5's closure was not crossed.

**This does not advance 「起码要能进入操作系统，把基础驱动跑起来」** — it removes two obstacles to finding
out. **TWRP-to-storage stays withheld** until a boot is observed entering the OS.
