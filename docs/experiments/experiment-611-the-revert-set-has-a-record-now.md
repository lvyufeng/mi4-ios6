# 611: the revert set has a record now, and the one manifest it had verified elsewhere

The user's hardest condition on this device is not the goal — it is 「一定要保证不要让设备彻底死机或者变砖」,
and the mechanism behind that claim has never been the watchdog. It is that **the arm in `out/` can always
be put back** to one that was known to reach a specific point: the frozen 574 pair, whose identity this
project has repeated in prose since 595 as *"revert is two `cp`s and no build"*.

That sentence is a claim about **a directory of files** — which files, with which bytes — and this step
found that the claim had no record and that the one manifest the revert path *did* have verifies a
different directory. Both are now fixed: `stages/stage90/revert-set.txt` is the record, and
`tools/verify_revert_set.sh` is the comparison.

Host-side only: two new tools, one new record, one reference pointer, one index row. No boot, no build, no
device, no `fastboot`, no `adb`, **nothing written to storage**, and **the parked set was read but never
modified** — the tool only hashes. **TWRP stays withheld.**

## 1. Why a revert set needs a record at all

The files that make a revert possible are **outside version control**: `out/` is gitignored (`.gitignore:15`)
and the 574 park is at `/tmp/r594/frozen-payload/`. The hashes were not absent from the tree — `914f45ac…`
and `151425c4…` appear in **22 documents** — but every one of those is prose in a step write-up. Nothing a
program could compare against existed, so the only way to know a park was still the park was to read a
document and trust a table.

The record is now one line per file with its sha256, its size and a **role**, written by hand in the step
that measured it, in the shape `tool-images.txt` established for the TWRP image (608). A build-generated
record would agree with itself and constrain nothing; the whole value is that this one was written by
looking at bytes.

## 2. What makes a set complete is a derivation, not a list

The obvious way to write the record was to copy the park's file listing. That would have been wrong, and
measurably so. **A revert is complete when the gate is green on the reverted tree** — so the set is *the
files the gate reads*, which the gate's own source answers:

```sh
grep -oE '\$OUT/[A-Za-z0-9_.-]+|out/stage90/[A-Za-z0-9_.-]+' stages/stage90/preflight_boot_check.sh \
  | sed 's|\$OUT/||; s|out/stage90/||' | sort -u
```

which yields **nine**: `SHA256SUMS.txt`, `stage90.bin`, `stage90-build-config.txt`, `stage90.elf`,
`stage90-qcdt.img`, `xnu_arm_entry.bin`, `xnu_arm_entry-config.txt`, `xnu_arm_entry.elf`,
`xnu_arm_entry-sources.txt`. The stronger check is available to anyone at any time and is not a
derivation at all: revert, then run the gate.

**Both spellings are required, and that is the first defect this step found in its own work.** The gate
names eight of the nine through `$OUT/name` and one — `xnu_arm_entry.elf`, in a narration path — through
`out/stage90/name`. My first derivation grepped only `out/stage90/`, which returns **three**; the record
was then written with **eight** lines, missing `stage90.elf`. A record built that way certifies as
complete a set that leaves the gate red — the exact failure §2 is about, committed in the paragraph
explaining it. It is now structural: `tools/rehearse_revert_set.sh` derives the list from the gate at run
time and refuses when the record does not cover it, so the check is no longer a human reading a grep.

The park's own listing, for contrast, covers **five** files. The gate reads **three** of those five and
**omits six** that it does read, and **two** of the five (`stage90_fixture.macho`, `stage90.img`) are
files the gate never opens at all. That is why the `role=` field matters: a file nobody recognised is a
file nobody copies.

## 3. The manifest that verifies elsewhere

The finding that made this a step rather than a chore. Every build writes its own `SHA256SUMS.txt` with
**absolute paths** into the tree that produced it — and a park is a copy of those files *outside* that
tree. So, run inside the park:

```
$ cd /tmp/r594/frozen-payload && sha256sum -c SHA256SUMS.txt
/mnt/data/mi4-ios6/out/stage90/stage90_fixture.macho: OK
/mnt/data/mi4-ios6/out/stage90/stage90.elf: FAILED
/mnt/data/mi4-ios6/out/stage90/stage90.bin: FAILED
/mnt/data/mi4-ios6/out/stage90/stage90.img: FAILED
/mnt/data/mi4-ios6/out/stage90/stage90-qcdt.img: FAILED
sha256sum: WARNING: 4 computed checksums did NOT match
```

Every one of those `FAILED` lines is about **the live tree, which has moved on** — `out/` now holds the
sleeper — and **not one byte of the park is wrong**. Measured in place, all seven parked artifacts equal
what the documents record: `914f45ac…` qcdt, `0f108392…` bin, `8274b1c4…` img, `c869a319…` elf,
`52bc9c35…` fixture, `151425c4…` entry bin, `3bc72605…` entry elf.

**A manifest of absolute paths is a record of a *location*, not of a *directory*.** It verifies wherever
you run it; what it verifies is the path it names. And it fails in the direction that costs most: the one
file that did not change (`stage90_fixture.macho`) prints `OK`, so the output looks like a partly-damaged
park rather than a manifest pointed at the wrong place. This project has already been bitten once — 595's
session, whose rehearsal mirror re-read the real `out/` from inside the mirror because of exactly these
paths — and until now nothing structural stopped it happening again.

The park's `SHA256SUMS.txt` is **deliberately not rewritten**. It is a frozen artifact with its own
recorded hash (591: `aeb7862a…`), and editing it would falsify that record and this one. The defect is
recorded instead, which is why the file that cannot be trusted as a manifest is still pinned as bytes —
with `role=build-manifest-absolute-pathed-see-the-trap-above`.

## 4. The tool, and the one thing it refuses to read

`tools/verify_revert_set.sh DIR [--record=…] [--set=…]` hashes every file of a named set **in the given
directory** and refuses when a member is absent, empty, the wrong size, or the wrong bytes — each with a
distinct message, because *absent* and *does not match* are different faults and 601's class is reading
one as the other. It prints an `ok` line with the hash for every file (success is loud) and the summary
names the **absolute path that was hashed**, since a verdict about a directory that does not say which
directory it was about is how a check taken against a copy gets quoted as a check against the original.

**It never reads the target's own `SHA256SUMS.txt`** — it hashes from the record. But when the target
carries one, it prints where those paths point:

```
  note  this directory carries its own build manifest with 5 absolute path(s), pointing at: /mnt/data/mi4-ios6/out/stage90
        Those paths are not read here. A manifest of absolute paths records a location, not a
        directory: run inside a park it hashes the tree the build ran in, and prints FAILED for
        every file that has changed since. This script hashes from the record instead.
```

That is the trap stated at the moment a reader meets the directory, rather than rediscovered as a
"corrupt park". It is narration and never a refusal: a *correct* park has one of these too.

## 5. The rehearsal, and the four defects it found

`tools/rehearse_revert_set.sh` runs the real tool against real directories with no device, no build, and
no copy of the park, and asserts both the exit code and a phrase of the output for **25 cells**. The
fixture is built from the live `out/` — real bytes, so a hash mismatch is a real hash mismatch — and the
record for it is regenerated to describe the copies.

The cells that matter most point the **real** record at the **live** tree: it must refuse, because the
live tree holds the sleeper and the record describes the frozen 574 bytes. A verifier that accepted that
would be the very defect §3 is about. Beside them, a cell shows the two readings disagreeing on one
directory: a park with a one-byte edit *and* its own manifest regenerated to describe the edited bytes
passes `sha256sum -c` (`manifest-disagrees`, exit 0) and is refused by the record
(`self-consistent-manifest`, exit 1). The last cell derives the gate's read list at run time and checks
the record covers it — the check that would have caught §2's missing `stage90.elf` before the record was
committed.

**Four defects were found this way, and every one was in the step's own instruments rather than in the
mechanism** — which is the class this project keeps meeting:

* `live-tree` **passed when it should have refused**, because the fixture was *copied from the live tree*
  and the cell compared a directory with itself. The fix was to use the real record, which describes a
  different set — and that cell is now a stronger one than the one it replaced.
* `typo-in-field-name` **passed by doing nothing**: the `sed` that was supposed to corrupt `sha256=` was
  anchored `s/^sha256=/`, and that field sits **mid-line** (`set=… sha256=… bytes=…`), so the pattern
  matched nothing and the "malformed" record was well-formed. Four cells now carry a comment saying why
  their patterns are unanchored.
* `manifest-disagrees` failed because the manifest was regenerated with the target file *inside* the list
  it writes: the redirect truncates `SHA256SUMS.txt` before `sha256sum` opens it, so the manifest recorded
  the hash of an empty file and its own check failed on the manifest rather than on the bytes.
* and the record itself shipped **eight** lines against a gate that reads nine — §2's defect, found by
  writing the coverage cell rather than by reading the record.

**Three of the four are the same shape**: a check that passed because its input was not what it was meant
to be. `typo-in-field-name` is the sharpest — a cell whose malformed input was never malformed is
indistinguishable from a check that works, which is why this battery asserts a phrase and not only an
exit code, and why the count it prints is derived rather than written.

## 6. What this does not do

* **It does not boot anything and changes no arm, payload, prediction or verdict.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…`, the parked arm is still the sleeper
  (`STAGE90_XNU_IDLE_NO_SLEEP=1`, entry `696a0f39…`), and the press is still the user's.
* **It does not revert anything.** The tool only hashes; it copies nothing and writes nothing. Reverting is
  still two `cp`s, and the record now says which two.
* **It does not call `sha256sum -c` on a park**, and it does not fix the absolute-path manifests — the
  build still writes them and the live tree still has one. What exists now is a record that does not
  depend on them.
* **It does not record the 526 and 533 parks** in `out/stage90/frozen/`, which an older convention put
  there. The record's comment says so, so a reader knows the boundary rather than assuming the park
  directory is complete.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus and no host
  action substitutes for a power press.

## 7. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. Two new tools
(`tools/verify_revert_set.sh`, `tools/rehearse_revert_set.sh`, both mode 100755), one new record
(`stages/stage90/revert-set.txt`), one pointer added to `docs/reference/recovery-and-rollback.md` and one
index row. Reads of the park **at `/tmp/r594/frozen-payload/`** — hashed in place, 9/9 matching, and **no
file modified**; reads of `out/stage90/`'s file list and of the gate's source, to derive the set from the
gate's own read list rather than from a remembered list. The boot gate was re-run afterwards →
**EXIT=0 / 549 lines / 0 stderr**, unchanged, and the new rehearsal ran green → **25 ok / 0 failed**. The
fixture directories the rehearsal builds are temporary and removed on exit. The payload, the parked frozen
pair and the arm the next press sends are unmodified. `fastboot boot` only — never `flash` — so no outcome
of any of this can write to storage.
