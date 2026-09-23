# 610: the reference's other open gap, measured

**608 §6** named it in its own list of what the step did not do:

> **It does not document an EDL path** for this phone, still the reference's other open gap.

And 605's gate named it first, from the other direction: the rollback it prints is a **fastboot** rollback,
and it assumes fastboot still works. EDL is the one recovery route that does not need fastboot, so the
gap is not cosmetic — it is the case the gate's own rollback cannot cover. This step measures it instead
of leaving it as a sentence.

Host-side only: one block rewritten in `docs/reference/recovery-and-rollback.md`, and `preflight_boot_check.sh`
is **read and run but not edited** (see §1 — it changed under this step, and not by me). No boot, no build,
no device, no `fastboot`, no `adb`, **nothing written to storage**. **TWRP stays withheld**, and the arm the
next press sends is unchanged.

## 1. The gate's half landed while this step was open — as a peer's commit, not this one

The subject of this repository's frontier work is shared, and `preflight_boot_check.sh` is
`run-experiment-526`'s lane. While this step was being written, the *other* half of 609's finding —
putting the channel condition into the **gate's** pre-boot narration, where the list of expected-absent
keys is printed — appeared in the working tree, uncommitted, at 08:21:57. It is the peer's work, and they
landed it themselves as **`8aea551`** (*"594 gate half: the absence list is conditioned on the channel
that carries it"*): twelve `echo` lines, one insertion, no clause, no test, no key, no count word, no new
`fail`/`die` site.

This step therefore **claims nothing about that change**. What it does contribute is an *independent*
measurement of it, taken before the peer's message arrived and reported here because two measurements of
one artifact by different methods are worth more than one:

| | exit | stdout | stderr |
| --- | --- | --- | --- |
| the gate as it stands at `HEAD` | 0 | **549 lines** | 0 bytes |
| `HEAD~1`'s gate (`1f1b9d2`), same directory | 0 | 537 lines | 0 bytes |
| `diff` | | **`77a78,89`** — one insertion, nothing removed | |

The peer reports the same 549 lines and `EXIT=0`; the second column above is what makes the first a fact
about the *edit* rather than about the file, and four earlier steps (606–609) each recorded 537, so the
delta is attributable and nothing else moved.

**One correction the peer passed on, recorded so it is not quoted forward.** The gate's *earlier* half
(`696b45c`) justified itself with *"on the arm the coming boot actually uses, this change executes no new
line"*. That was true of the frozen arm that was in `out/` **at the time**; `out/` now holds the
sleepless arm, on which the same change prints twelve lines. The sentence's subject was `out/`, not the
code — the same shape as every other stale-reference defect in this project's record, and the reason a
justification that names an artifact has an expiry date the code does not.

## 2. The `/tmp` oracle trap, hit again while checking a landed change

The comparison in §1 was run twice because the **first** attempt put `HEAD~1`'s gate in `/tmp` and got:

```
HEAD EXIT=1  lines=0  stderr=104
```

which reads as *"the committed gate fails"* and is false. `preflight_boot_check.sh` resolves `$REPO_ROOT`
from its own location, so a copy in `/tmp` cannot find the payload and refuses — a fact about where the
file was **run**, not about the file. Re-run from inside `stages/stage90/`, the same bytes are
`EXIT=0 / 537 lines / 0 stderr`. This is 595's lesson and 598's defect (a before/after comparison whose
old half sat one directory too shallow, so it read nothing and every line of the difference would have
looked like the change) arriving a third time; the rule it keeps teaching is that an oracle has to run
where the artifact runs.

## 3. The measurement: what exists, and what an EDL path would have to begin with

`recovery-and-rollback.md` carried one line for EDL and the emergency section carried a vaguer second
version of the same claim. Both are now **one** statement with a measurement behind it, taken 2026-09-23:

* **No EDL host tooling exists on this host.** `qdl`, `edl`, `firehose`, `QSaharaServer`, `fh_loader`,
  `sahara`, `emmcdl`, `QFIL` — nine of nine absent from `PATH`.
* **The backup is not an EDL recovery kit.** `xiaomi4-cancro-backup-20260604-112053/` holds partition
  images, `SHA256SUMS.txt` and two unpacked boot/recovery trees — and no programmer. A sweep for `*.mbn`,
  `rawprogram*.xml` and `patch*.xml` across the backup *and* the repository returns **nothing**. EDL needs
  a signed Firehose programmer (`prog_emmc_firehose_8974.mbn` for MSM8974) plus a `rawprogram*.xml`;
  partition images alone cannot be pushed over EDL.
* **`stages/stage90/firehose/` is not that programmer, despite the name.** It is Apple's libdispatch
  `firehose_buffer.c` and its headers, ported so `bsd/kern/subr_log.c` can link (experiment 255) — XNU's
  log-ring code, tracked in git, with no relation to Qualcomm's Firehose protocol. Worth writing down
  because the obvious search for EDL material in this tree finds a directory that looks like an answer.
* **And this tree does not know the phone's secure-boot state**, which decides what a programmer would
  have to be. `fastboot getvar secure` and `getvar unlocked` were both run at some point and both returned
  **empty** on this bootloader (`docs/reference/local-device-findings.md`); an unsigned programmer works
  only on a part that does not enforce signature checks, and a signed one is bound to the device's own
  signing chain.

So the honest position is not "EDL is pending" but **"EDL recovery for this phone begins by obtaining a
`cancro`/MSM8974 programmer and provenance-recording it — the shape `tool-images.txt` gives TWRP — and it
cannot be verified without putting a device into 9008 and writing to storage, which is the action the gate
exists to gate."** That is why it is left undocumented rather than half-documented.

The reference edit is also a small piece of hygiene in its own right: the file now states this gap
**once**, in the list of what the gate cannot do, and the emergency section *points at* that statement
instead of restating it in weaker words. Two versions of one fact is this project's most-repeated defect
class, and the emergency section's version was the one without the measurement.

## 4. Two claims written from memory that the tree refutes — mine, caught in the same edit

**The secure-boot sentence.** §3's first draft read *"this phone's bootloader is secure-boot enabled
(`fastboot getvar secure` is recorded in the preflight logs)"*. It is false, and the file it cites is the
file that refutes it: `local-device-findings.md` records `secure:` and `unlocked:` among the variables that
**returned empty**. I had written the *conclusion a secure-boot check usually yields* and attached a
citation to it — the exact shape of 596's defect, where the log that supplied a number falsified the claim
made about it. Found by grepping for the sentence's own citation before committing (`grep -rn 'getvar
secure'`) and not by anything structural; the fix is the more useful fact, that the state is **unknown**.

**The manifest's path, its role, and its counts.** The Safety section's first draft placed this project's
source manifest at `stages/stage90/xnu_arm_entry-sources.txt` and called `preflight_boot_check.sh` "a
manifest entry named in a comment". Both halves were wrong:

* the manifest is **`out/stage90/xnu_arm_entry-sources.txt`** — a build output at the repository root, not
  a tracked source file, which is *"not absent; build output"* in its most literal form;
* `preflight_boot_check.sh` is not an entry in it at all: it is the manifest's **reader**, named as such on
  the manifest's own line 3. The only `.sh` among the entries is `build_entry.sh`, which lives in
  `xnu_arm_boot/`;
* and the counts were wrong too — **20 file entries**, one per regular file in `xnu_arm_boot/` (20 files,
  checked against the directory), plus a `STAGE90_XNU_ENTRY_SHA256=696a0f39…` header on line **5**, not
  `21` entries and not line 4.

Three numbers in one paragraph, all written before being read. **The count of a list is a claim about the
list**, and it is the class of claim most likely to be written from memory because it looks like
bookkeeping rather than like an assertion.

## 5. And the index row's own edit deleted a prefix

Inserting this step's row into `docs/experiments/README.md` **removed 608's row prefix** — the
`| stage90 | [experiment-608](…) | ` that the new row's text also ended with. The result was a line
beginning `**the tool image has a record now…` with no table cells at all, in a file where every
neighbouring row has exactly four pipes.

It is *an edit on a prefix deletes the suffix* in the other direction: the anchor and the replacement both
ended with the same text, and the replacement's last line consumed the anchor instead of preceding it.
What found it was not reading the diff but **counting the delimiters** — `awk` reporting pipes per row over
five lines, where one odd row among identical neighbours is unmissable — and the count was re-run after the
repair (`4 pipes` on every row, **575** rows against **574**, index checks unchanged at the same
**6 pre-existing violations** that were there before this step and verified with `git stash` back in 598).
A structural check of the *shape* a table must have caught what a read of the text did not.

The same insertion also rotted a reference inside this document: §4 cited the Safety section as *"§6"*,
which it was until §5 was inserted between them. It now cites it **by name**, which is what this
repository's step docs do to each other — and the reason is this one: a number is a pointer a later edit
can invalidate without touching it.

## 6. What this does not do

* **It does not boot anything, and it changes no arm, payload, prediction or verdict.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…`, the parked arm is still the sleeper
  (`STAGE90_XNU_IDLE_NO_SLEEP=1`, entry `696a0f39…`), and the press is still the user's.
* **It does not claim the gate half of 594/609.** That change is `8aea551`, a peer's commit in a peer's
  lane; §1 measures it and §1's correction is the peer's own, reported here rather than quoted forward.
* **It does not add an EDL path**, and §3 explains why one cannot be added from a desk.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus and no host
  action substitutes for a power press.
* **It does not sweep the runner's remaining `FAIL` branches**, still owed from 603 §7.

## 7. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One reference
section rewritten (`docs/reference/recovery-and-rollback.md`) and one index row added
(`docs/experiments/README.md`); `stages/stage90/preflight_boot_check.sh` was **run** (twice, from its own
directory, with `--allow-xnu-entry`) and **not modified** — its working-tree state is the peer's `8aea551`
and `git diff` on it is empty. Reads of `entry_stubs.c`, of the backup directory, of the manifest
`out/stage90/xnu_arm_entry-sources.txt` and of `docs/reference/local-device-findings.md`. One same-depth
oracle copy of `HEAD~1`'s gate inside `stages/stage90/`, removed after the diff, plus one discarded `/tmp`
copy — the mistake §2 records. The boot gate was verified on `HEAD` → **EXIT=0 / 549 lines / 0 stderr**,
and the same directory's run of `HEAD~1`'s gate → **EXIT=0 / 537 lines / 0 stderr**. The payload, the
parked frozen pair at `/tmp/r594/frozen-payload/` and the arm the next press sends are unmodified.
`fastboot boot` only — never `flash` — so no outcome of any of this can write to storage.
