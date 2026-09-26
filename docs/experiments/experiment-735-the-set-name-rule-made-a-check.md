# 735: the set-name rule made a check — 25 sets measured, and the mis-name that had no enemy

Host-side. **No device action of any kind**: no `fastboot`, no `adb`, no `sudo`, no `out/` write, no
build, no press. The live arm is unchanged — `out/stage90` still holds `armed-storage-quiet-c9738417`
(`STAGE90_XNU_STORAGE_PROBE=15`), ARMED and NOT PRESSED, and the last press is still 733's
(2026-09-26 10:12:21–10:13:31 UTC, exit 0, 70 s).

## 1. The rule, and the step that broke it with nothing in the way

Every set name in `records/revert-set.txt` ends in eight hex characters, and those eight characters are
the sha256 prefix of **one of that set's own members**. The rule was written down — inside a `role=`
string in the record itself, on the very line for the entry image:

    role=entry-image-embedded-in-the-payload-this-is-the-STORAGE_PROBE-15-entry-
    the-8-hex-of-this-hash-is-the-sets-name-and-its-SIZE-IS-BYTE-IDENTICAL-...

and the **next** arm's first draft broke it: 732 named its arm `armed-storage-enable-73d4a8f3`, taking
the suffix from `stage90-qcdt.img` — the file `fastboot boot` sends, which is the convention the one
pre-ladder arm happens to follow — instead of from the entry image. Nothing refused. The mistake was
found by hashing the other sixteen storage sets by hand, and 732 §3 wrote the finding as a sentence:

> **no check enforces the rule today**: the suffix's source is the one thing about a set name that a
> reader has to be told.

**That sentence is what this step closes.** The claim and its counter-example sat one line apart in the
same block, inside prose no tool parses — `[[mi4-a-claim-in-a-comment-is-not-a-check]]` in the form where
the cost is a spent press, because a set name is what the record is searched by after a compaction.

## 2. What the record actually says, measured over all 25 sets

| suffix's source | sets | which |
| --- | --- | --- |
| `xnu_arm_entry.bin` | **23** | every `armed-storage-*` (17) and six earlier entry arms |
| `stage90-qcdt.img` | **1** | `armed-selftest-wdog-ef0361a2`, the one **pre-ladder** arm |
| not a hash at all | **1** | `frozen-574` — a human label for the 574 park |

Three facts that only a census could produce, and each one changes what a check may say:

1. **The rule has two halves and they are not the same half.** *Universal*: an 8-hex suffix must be the
   sha256 prefix of exactly one of the set's own members — that is what makes the name evidence about
   the bytes rather than about the author. *The storage family*: for an `armed-storage-*` set that
   member must be `xnu_arm_entry.bin`, because the storage ladder is a change to the **entry image**
   and the qcdt and the payload `.bin` both move for reasons that are not the arm. A name taken from
   either would name the wrapper.
2. **The other families are measured and not constrained, deliberately.** The pre-ladder arm's
   qcdt-derived name is kept: this project supersedes a record rather than editing it, and a check that
   refused the historical arm would be **red on `master`** — a check nobody reads. The tool prints the
   census instead, so a new convention appears as a line in that table on the day it appears.
3. **`frozen-574` is a different kind of record, not a violation.** Its suffix is a word. A rule about
   names that claim to be a hash has nothing to say about a name that claims to be a label, so it is
   reported in its own line and skipped.

## 3. The check, and what it refuses

`tools/check_set_name_rule.sh` — one process, `--record PATH` for falsification, `--verbose` to print
every set. It refuses, naming the set and the reason:

- the record is unreadable, or yields **no member lines at all** (an empty parse is the extractor
  failing, not a record with nothing to say — m728's shape);
- a line that is not a well-formed member line, **quoted**, because a set this cannot parse is a set
  this cannot examine;
- a suffix that is the prefix of **none** of that set's own members, with all of them listed;
- a suffix that **two** of its own members could claim;
- an `armed-storage-*` set whose suffix came from a member **other than** the entry image;
- an `armed-storage-*` set that carries **no** `xnu_arm_entry.bin` member at all — because a rule that
  cannot be applied is a refusal and not a pass.

**Eight cells, and every one of them measured on a perturbed copy of the real record:**

| # | perturbation | verdict |
| --- | --- | --- |
| — | the real record, 25 sets | **exit 0**, 24 hash-named + 1 label, census as §2 |
| 1 | the rung-16 arm named after its qcdt — **732's exact first-draft mistake** | REFUSED, naming `stage90-qcdt.img` against `xnu_arm_entry.bin` |
| 2 | the suffix borrowed from a **different** set's entry image | REFUSED, listing all 11 of the set's own members |
| 3 | a qcdt-named storage set with the entry-image member **removed** | REFUSED, naming the missing member |
| 4 | a suffix that **two** members could claim | REFUSED, naming both |
| 5 | one malformed member line | REFUSED, quoting the line |
| 6 | an **empty** record | REFUSED |
| 7 | an **absent** record | REFUSED |

**Cell 4's first construction did not exercise the branch it was built for**: dropping the entry-image
member from a set still named after that member's hash made the *universal* half refuse first, so the
"carries no entry-image member" refusal was never reached. It was rebuilt as cell 3 — a set named after
its qcdt, whose entry-image line is gone — and then it fired. A perturbation that refuses for the wrong
reason is a perturbation that has not measured what it claims to.

**And it is wired into the build, which is where the project's own rule says such a claim belongs**:
`make check` now runs `tools/check_stage_paths.sh` **and** `tools/check_set_name_rule.sh`. Neither
touches the device, `out/`, or the gate — verified by reading them, not by assertion.

## 4. What it does not do

It does not say a park on disk is the set its name claims — that is `tools/verify_revert_set.sh`, and the
two are complements: this one reads the **record**, that one reads the **bytes**. It cannot see whether
the *chosen* member is the interesting one outside the storage family (a set named after its own
`SHA256SUMS.txt` would pass the universal half). It does not retro-name the historical arms, and it does
not edit the record — the census reports what is there.

**It does not advance 「把基础驱动跑起来」/「起码要能进入操作系统」.** The OS is not observed booting and no
storage exists, so **TWRP-to-storage stays withheld** (the clause is conditioned on 「如果os已经能进去了的话」).
What it buys is that the next arm's name — the one string a reader has after a compaction — is now a
reading rather than a promise.
