# 613: which of the three checks covers which direction, and the help text a header edit truncated

612 ended with the manifest covered twice — check **5** (the bytes) and checks **6a/6b** (the member list)
— and both of us left the obvious question unwritten: **they cannot be redundant, so which direction does
each one carry?** This step answers it by measurement, adds the cells that hold the answer, and then finds
a second defect in the same file, in this project's oldest shape: a header edit that quietly deleted what
came after it.

Host-side only: two scripts edited, one document added, one index row. No boot, no build, no device, no
`fastboot`, no `adb`, **nothing written to storage**; the park was hashed and never modified. **TWRP stays
withheld.**

## 1. The partition

The trio partitions cleanly, and the split is not the one the names suggest — 5 and 6a both read the
*record*, so neither is the on-disk check I had assumed 5 was:

| check | reads from | catches |
| --- | --- | --- |
| **5** | the record | the manifest **FILE** doctored — a member added **or removed**; a 4-member manifest is refused by its size and hash alone |
| **6a** | the record | the record naming a member it does not itself carry |
| **6b** | the target | a target manifest that has **GROWN** names outside the set |

So the **shrinkage** direction — the one 611's record was blind to, the one 612 exists to fix — is covered
**not by either member check but by pinning the manifest as bytes**. `manifest_members=` and check 5 are
complements, and a record that dropped the manifest from its file list would lose that direction entirely.

Measured, against a fixture whose manifest went from 10 members to 9 by deleting its `stage90.img` line,
with the *unmodified* record still pinning the 10-member manifest's hash and size:

```
== set fixture in /tmp/…/shrunk ==
  FAIL  SHA256SUMS.txt is 769 bytes, the record says 847. Two fields of one file disagreeing is a
        refusal before any hashing: these are not the bytes that were measured.

REFUSING: 1 of 22 file(s) did not match the record. Nothing in /tmp/…/shrunk was
          modified, and no file was copied anywhere: this run only hashed.
exit=1
```

and **nothing else fired**: no `not a file= line of set` (6a reads the record's own field, which the shrink
does not touch) and no `names file(s) the set does not` (a shrunk manifest has no *extra* names for 6b to
find). The twin directory, un-shrunk, prints `ok every one of the 10 member(s) its own manifest names is in
the set` and `VERIFIED`. The partition is a reading of two runs, not of the code.

## 2. The cells, and why an assertion of silence needs a preflight

The partition is now four cells in `tools/rehearse_revert_set.sh` (30 → **35 ok / 0 failed**):

| cell | what it asserts |
| --- | --- |
| `shrunk-fixture` | the fixture **actually shrank** — 10 members before, 9 after |
| `shrunk-refused-by-bytes` | the shipped tool refuses it **at check 5**: exit 1 **and** the phrase `bytes, the record says` |
| `shrunk-6b-silent` | 6b's refusal phrase is **absent** from that same output |
| `shrunk-6a-silent` | 6a's refusal phrase is **absent** from that same output |

Two of those cells assert *silence*, and silence is the one property a command that does nothing has. This
battery has already caught four cells that passed by doing nothing, so the run is captured **once** and
both silence cells get their evidence from that same capture, behind two guards: the fixture had to have
shrunk, and the capture had to contain the check-5 refusal. A silence about a run that said nothing is not
a reading, and the guard is what makes the difference.

**Both guards were falsified rather than argued.** Replacing the shrink with a `cp` — the cell's command
doing nothing, exactly the defect — produces:

```
  FAIL  shrunk-fixture             the fixture was not shrunk (10 members before, 10 after) - the cells below would
        then be asserting silence about a manifest that was never altered.
  FAIL  shrunk-refused-by-bytes    exit=0 without the check-5 size refusal, so the two cells below would pass by
        saying nothing about a run that said nothing - a silence asserting a silence.
  ok    shrunk-6b-silent           6b stayed silent, as the partition says
  ok    shrunk-6a-silent           6a stayed silent, as the partition says
```

**The two silence cells stayed green on a run that never happened** — which is precisely why they may not
be read on their own, and why the failure they would otherwise hide is printed above them.

The detector was checked from the other side too: a record whose `manifest_members=` names a stray *does*
put `not a file= line of set` into the tool's output, so the silence cells are capable of seeing the phrase
they assert the absence of. A cell that cannot see its own signal cannot assert its absence. (My first
attempt at this control used a record with no `manifest_members=` field at all, in which 6a is skipped by
design — so it printed "blind" for a reason that was about the control, not the cell.)

## 3. The second defect: `--help` was pinned to a line number

Adding the partition table to `verify_revert_set.sh`'s header moved text past line 45, and that file's
`--help` was:

```sh
--help|-h)  sed -n '2,45p' "$0" | sed 's/^# \{0,1\}//' | grep -v '^$' | head -24; exit 1 ;;
```

So `--help` silently stopped **at check 5**, dropping the 6a/6b description, the absolute-path trap, the
union criterion, the `Usage:` line and the exit contract — and it kept exiting 1, so nothing anywhere went
red. This is `mi4-an-edit-on-a-prefix-deletes-the-suffix` with the boundary in the *help text* rather than
in the file: the deleted part was still in the file, and the help had been told not to read it.

The fix derives the range instead of writing it:

```sh
--help|-h)  awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$0"; exit 1 ;;
```

It prints every leading comment line and stops at the first line of code, whatever the header's length —
**76 lines** now, ending on the exit contract, where 24 ended on check 5. A help text pinned to a line
number is a claim about how long the comment is, and the check for a claim like that has to be structural,
so the new cell asserts the **last** line of the header:

```
cell "help-prints-the-whole-header" 1 "Exit: 0 = every file of every requested set matched" bash "$TOOL" --help
```

That cell is load-bearing, not decorative: restoring the old `sed -n '2,45p' … | head -24` line (and
nothing else) makes it the battery's only failure — `exit=1 but said nothing about: Exit: 0 = …` — **while
the exit code stays 1 in both versions**, so an assertion on the status alone would have been blind to the
whole defect. The tool was restored byte-identically afterwards (`cmp` against a copy) and the battery went
back to 35 ok / 0 failed.

## 4. What this does not do

* **It does not boot anything and changes no arm, payload, prediction or verdict.** The arm in `out/` is
  still the sleeper, `stage90-qcdt.img` = `60063c47…`; both edits are to host-side tools that no boot path
  reads, and the press is still the user's.
* **It does not touch the record.** `stages/stage90/revert-set.txt` is unchanged by this step; the Park
  still verifies 11/11, untouched.
* **It does not make the pair exhaustive in general.** 5, 6a and 6b partition the cases *these two
  artifacts can differ in* — the manifest's bytes and its names, each from the record and from the target.
  A revert that also changed something the gate reads outside both derivations would still need the
  authoritative test: **revert, then run the gate.**
* **It does not carry the 526/533 parks** in `out/stage90/frozen/`, still unrecorded by design.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus: the last
  `usb 3-10:` disconnect with serial `4a2fe00b` was **7.74 h** ago (27,849 s, at 2026-09-23 01:18:06), `adb
  devices` and `fastboot devices` both return empty, and the only phone-class device on the host is the
  neighbouring `05c6:9008` on bus 003, which is QDL and supports no run.

## 5. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. Two host-side
files edited: `tools/verify_revert_set.sh` (the header's partition table, and `--help`'s range made
structural) and `tools/rehearse_revert_set.sh` (four partition cells, one `--help` cell, and two guards that
make the silence cells readable). Reads of the park, hashed in place, **no file modified** — the shipped
tool run against `/tmp/r594/frozen-payload` exits **0** with **11 file(s)** verified and 6 manifest-member
checks agreeing. The boot gate
was re-run afterwards → **EXIT=0 / 549 lines / 0 stderr**, unchanged; the rehearsal ran green → **35 ok / 0
failed**. The payload, the parked frozen pair and the arm the next press sends are unmodified. `fastboot
boot` only — never `flash` — so no outcome of any of this can write to storage.
