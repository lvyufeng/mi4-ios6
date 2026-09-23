# 612: the record was two files short of its own criterion

611 landed a record and a verifier for the revert set — the thing that makes 「一定要保证不要让设备彻底死机
或者变砖」 a mechanism rather than a hope — and stated the criterion in one line: **the set is the files the
gate reads.** The peer session holding `preflight_boot_check.sh` took that line literally and tested it
three ways. The criterion was right; **my derivation of it was not**, and the record shipped **two files
short of the set it claimed to describe**.

Commit `ef6aa65` is not amended — it is pushed, and this is the correction.

Host-side only: one record edited, one tool's parser and checks extended, one rehearsal extended, one
reference line unchanged. No boot, no build, no device, no `fastboot`, no `adb`, **nothing written to
storage**; the park was hashed and never modified. **TWRP stays withheld.**

## 1. What the gate reads, in two ways

`preflight_boot_check.sh:139` is:

```sh
( cd "$OUT" && sha256sum -c SHA256SUMS.txt ) || fail "image does not match SHA256SUMS.txt; rebuild before booting"
```

`sha256sum -c` opens **every path the manifest names**. So the gate reads:

* **A** — the nine paths it names **in its own text**, which is what a grep finds; and
* **B** — the members of the manifest it *verifies*, which no grep of the gate can return, because the
  list lives in **a file that is itself a member of the set**.

611's record was built from A alone. B is five names — `stage90_fixture.macho`, `stage90.elf`,
`stage90.bin`, `stage90.img`, `stage90-qcdt.img` — of which three were already in A and **two were not**.
That is the defect in one sentence, and it is worse than a missing line: B is not derivable from the gate,
so no amount of care with the gate's text would have found it.

## 2. What the omission did, measured rather than argued

A nine-file revert, simulated in a directory with the park's manifest restored and its absolute paths
retargeted, and the gate's own line run on it:

```
stage90_fixture.macho: OK
stage90.elf: OK
stage90.bin: OK
stage90.img: FAILED            <- the live file, never reverted
stage90-qcdt.img: OK
sha256sum: WARNING: 1 computed checksum did NOT match
```

**The gate red, on a record whose entire purpose is that a revert cannot leave the gate red.** With
`stage90.img` in the set, the same simulation prints **five `OK` and `rc=0`**.

`stage90.img` is the one that bites: live `28ee2c22…` against the park's `8274b1c4…`. The other import,
`stage90_fixture.macho`, is `52bc9c35…` in **both** arms — identical — which is why it was benign and why
"it happens not to matter today" is exactly the reasoning a record exists to replace. Its `role=` says so:
*fixture-macho-same-bytes-in-both-arms-but-a-manifest-member-all-the-same*.

## 3. The repair, and why B is pinned in the record rather than read from the tree

The obvious fix is to derive B at run time from `out/stage90/SHA256SUMS.txt`. That would be wrong for the
reason this project keeps meeting: **that is the one file whose contents the next build changes**, and
today the live and parked manifests name the *same five* only by coincidence — so the mistake would be
invisible until the build that adds a sixth member, at which point the derivation would describe an arm
that is not the one being reverted.

So B is pinned **in the record**, as a new field on the manifest's own line:

```
set=frozen-574 sha256=aeb7862a… bytes=560 file=SHA256SUMS.txt role=build-manifest-… \
  manifest_members=stage90_fixture.macho,stage90.elf,stage90.bin,stage90.img,stage90-qcdt.img
```

The criterion is now **closed over the record**: A comes from the gate's text, B from a file the record
itself pins by hash and bytes.

Two checks enforce it, both in `tools/verify_revert_set.sh`:

* **6a** — every name in `manifest_members=` must also be a `file=` line of the same set. This is the
  check that would have refused 611's record.
* **6b** — **every name in the target's own manifest must be in the set.** Coverage is one-directional, so
  6a alone cannot see a manifest that *grows*; 6b is the direction that catches a build writing a sixth
  output, and it reads the file on disk rather than a remembered list.

## 4. The rehearsal grew the two cells that matter, and caught itself twice

`tools/rehearse_revert_set.sh` is now **30 cells / 0 failed** (was 25), and the completeness section
derives **A ∪ B** instead of A:

| cell | what it asserts |
| --- | --- |
| `manifest-passes` | a directory whose manifest names the recorded set passes `sha256sum -c` (exit 0) |
| `stale-member-turns-it-red` | one stale member → `stage90.img: FAILED` — **§2's defect, reproduced** |
| `target-manifest-names-outside` | a manifest naming a file the set does not carry → refuse (6b) |
| `manifest-member-not-in-set` | a record naming a manifest member that is not a `file=` line → refuse (6a) |

**Two of those cells were wrong on the first run, in the two ways this project knows best.**

* The first version of `manifest-passes` / `stale-member-turns-it-red` **passed by doing nothing**: the
  command was a `bash -c` that always exited 0 by `echo`ing a sentence, and the cell's phrase assertion then
  matched text *inside that sentence*. A cell whose command cannot fail is not a test. Both now run
  `sha256sum -c` itself and assert *its* status.
* `target-manifest-names-outside` passed by **deriving nothing**: 6b extracted member basenames with a
  pattern requiring a directory (`.*/`), and the manifest in that fixture is written with **relative**
  names, so the derivation returned an empty list, found nothing outside the set, and printed green. A
  build writes absolute paths, so the park was unaffected — but the check could not see a relative
  manifest at all. The pattern now takes the directory as optional, and the cell fails loudly if the
  record it builds carries no `manifest_members=` (a record without the field would make both new cells
  pass by having nothing to check).

That is the third time in three steps that a check passed because its input was not what it was meant to
be — and the second time the *fix* was a cell. See `mi4-measurement-defects`.

## 5. What this does not do

* **It does not boot anything and changes no arm, payload, prediction or verdict.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…`, the parked arm is still the sleeper, and the press
  is still the user's.
* **It does not amend `ef6aa65`.** That commit is pushed and its record is wrong; the correction is
  forward, in a new commit, with the record's own comment saying which version was wrong and how.
* **It does not make a revert complete by construction.** Both derivations are still derivations, and the
  authoritative test is unchanged: **revert, then run the gate.** The verifier's summary now says that in
  its own words rather than implying its checks are the whole criterion.
* **It does not carry the 526/533 parks** in `out/stage90/frozen/`, still unrecorded by design.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus.

## 6. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One record
edited (`stages/stage90/revert-set.txt` — two added lines, a new field, and the header's criterion rewritten
to A∪B), one tool extended (`tools/verify_revert_set.sh` — one new field, checks 6a/6b, a corrected basename
pattern), one rehearsal extended (`tools/rehearse_revert_set.sh` — four cells, the B derivation moved onto
the record, and a guard that refuses when the fixture record lacks `manifest_members=`). Reads of the park,
hashed in place **11/11 with no file modified**. The boot gate was re-run afterwards → **EXIT=0 / 549 lines /
0 stderr**, unchanged; the rehearsal ran green → **30 ok / 0 failed**. The payload, the parked frozen pair
and the arm the next press sends are unmodified. `fastboot boot` only — never `flash` — so no outcome of any
of this can write to storage.
