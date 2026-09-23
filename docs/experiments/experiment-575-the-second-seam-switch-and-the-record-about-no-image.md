# 575: the second seam switch, and the record about no image

**(Numbering: this gate change was drafted as 574, and the peer session's step — 572 section 6's measurement
arm, built in the same window — had taken that number at `01:34` for its own document, which cites 574 inside
`entry_trace.c`; that file is a build input, so the number that moves is this one, written after theirs. The
number and the filename changed; nothing else did.)**

572's arm is `STAGE90_XNU_SEAM_MEASURE`. It is written by `build_entry.sh`, it selects the *same*
interception 535 built (`--wrap=FlushPoU_Dcache`, one wrapper, the same return-address filter
`lr == 0x800462dc`) with 535's operation taken out of the wrapper's body, and until this change
`stages/stage90/preflight_boot_check.sh` had never heard of it. **Measured, not predicted**: the
pre-change gate's own region, extracted from `HEAD` and run against the record `out/` actually carries
today, refuses it with

> `REFUSING: …/xnu_arm_entry-config.txt carries key(s) this gate does not print: STAGE90_XNU_SEAM_MEASURE
> - a switch recorded on the build side and not shown here is a switch the next run would go out with
> unread; add it to ENTRY_CFG_KEYS above`

— i.e. 570 section 1's prediction, one arm later and now on the real record: the switch arrives as a
refusal about **the gate's own bookkeeping**, upstream of the device step (safe) but blocking, on the one
boot window 557 measured.

**This is a gate change only.** No image, no `out/`, no rebuild, no device, no arm: 572's run is spent and
the device is hung. It is filed as an experiment because it is not a repeat of 570 — the *new* thing is
that the two switches are **one seam's two arms**, so the gate now has a state (both at 1) that is a record
about an image that cannot exist, and it refuses that pair rather than narrating either arm over it.

## 1. One seam, two switches, and `SEAM_ON` is what turns it on

The build side, read out of `xnu_arm_boot/build_entry.sh`:

```
:398  SEAM_POC=${STAGE90_XNU_SEAM_POC:-0}
:416  SEAM_MEASURE=${STAGE90_XNU_SEAM_MEASURE:-0}
:421  if [[ $SEAM_POC -eq 1 && $SEAM_MEASURE -eq 1 ]]; then   # refuses: two arms of one seam
:429  SEAM_ON=$(( SEAM_POC | SEAM_MEASURE ))
:435  [[ $ENTRY_TRACE -eq 1 && $SEAM_ON -eq 1 ]] && TRACE_LDFLAGS+=(--wrap=FlushPoU_Dcache)
```

and `entry_trace.c:437-438` `#error`s on the same pair, so an image with both set has never been built and
cannot be. Two consequences the gate had to absorb, and one of them is not narration:

1. **What turns the interception on is the OR, not `SEAM_POC`.** Every sentence in the gate that read
   `SEAM_POC=0` as "no interception" was true only while there was one seam switch. It is now false on
   exactly the record in front of us (`SEAM_POC=0`, `SEAM_MEASURE=1`), which is the "one value, two
   definitions" shape arriving from the build side: one bit meaning "the seam", spelled in two places.
2. **The pair is a state no image can be in**, and the gate is the only place it is visible at gate time
   (`entry_trace.c` `#error`s at *build* time; a record is not a build).

`entry_trace.c:2254` publishes `xnu_live_seam_op` as `(uint32_t)(STAGE90_XNU_SEAM_POC)` — so under 572's
arm the log's own arm key reads **0**, the same value a no-seam run publishes. That is not a defect in the
firmware key, it is the key answering a narrower question than "is there a seam", and the narration below
says so in the arm's own words: the two arms are told apart by whether the `xnu_live_seam_*` keys were
written **at all** (`xnu_live_seam_calls` present), not by that value. Absent is not zero (569, 526).

## 2. The change, site by site

Sixteen sites, in the file's own order. The four counts, the array, the readers, and then the two clauses
that are not prose:

| # | line | what it is |
|---|---|---|
| 1 | `:338` | "the **seven** that *are* the variant are not named like the artifact" |
| 2 | `:340` | the key list in that comment gains `SEAM_MEASURE` |
| 3 | `:342` | "prints four lines, drops all **seven** of the ones this clause exists to publish" |
| 4 | `:351` | "The list was nine, then twelve, and is **fourteen**" |
| 5 | `:368` | the new commented paragraph — "**The fourteenth is 572's, and it is the same seam's other arm.**" — naming the one-wrapper/one-filter identity, the build-side refusal of both-at-1, and `SEAM_ON = SEAM_POC \| SEAM_MEASURE` |
| 6 | `:380` | `ENTRY_CFG_KEYS` gains `STAGE90_XNU_SEAM_MEASURE` |
| 7 | `:387` | the required-key refusal text: "The **seven** variant keys (… SEAM_POC, SEAM_MEASURE)" |
| 8 | `:391` | the converse clause's history sentence: "(a *tenth* when the list held nine; a **fifteenth** now)" |
| 9 | `:418` | "The **seven** are read as `grep -c` of the whole `KEY=` prefix" |
| 10 | `:430` | the `for _vk` list gains `STAGE90_XNU_SEAM_MEASURE` |
| 11 | `:446` | `STAGE90_XNU_SEAM_MEASURE)      V_SEAM_MEASURE=$_vv ;;` in the derivation `case` |
| 12 | `:458` | the range refusal text: "these **seven** are switches" |
| 13 | `:459-463` | the comment for the pair clause: the two narrations are written for two different arms, so printing either over a record naming both "would put a run's story on an artifact that does not exist" |
| 14 | `:464-465` | **the pair clause itself** — `[[ ! ( $V_SEAM_POC -eq 1 && $V_SEAM_MEASURE -eq 1 ) ]] \|\| fail` |
| 15 | `:515-536` | the `SEAM_MEASURE=1` narration branch |
| 16 | `:555-562` | the `else` branch, rewritten from "no interception" into "**both** seam keys are at 0 here, which is the only state this paragraph is printed in" |

Sites 1–4, 7, 9, 12 are the prose counts, and they are the part that is invisible when it is wrong: there is
no check for a number in a comment, and 570 section 2 recorded four of them having already drifted. All
seven were changed in the same pass and then re-read on diff review, which is why the numstat is part of the
proof below rather than paperwork.

Site 14 is the only clause of its kind in this gate, and it is worth saying why the other two cannot carry
it. The range clause cannot: `0` and `1` are both legal values, and it is the *combination* that is
impossible. The required-key clause cannot: both keys are present. And the build side cannot: it is not run
at gate time, and the gate is reading a record, not a build. What the clause does **not** do is guess which
of the two values is wrong — it names the test instead (read `entry_seam_flush`'s body in the ELF, or
rebuild with the switch this arm really needs) and says plainly that **nothing is rebuilt by this refusal
and nothing should be**, because the frozen pair embeds this entry image (539's defect, one arm later).

Site 15 says the arm is safe by construction and says what it inverts:

> `**So read its pair the other way round**: with nothing behind it the two reads are of a line only
> Apple's flush touched, an UNEQUAL b/a pair is that flush writing the line back, and an *equal* pair is
> the arm working as designed - where in 535's arm an unequal pair is the operation's own write-back.`

That is the one reading rule a run of this arm is worth, and it is the reason the narration had to be
three-way rather than two-way: the same keys mean opposite things on the two arms.

## 3. Proof

```
bash -n stages/stage90/preflight_boot_check.sh                        clean
git diff --numstat -- stages/stage90/preflight_boot_check.sh          62  18
```

The 18 deletions are the `else` branch's old "NO interception" sentence (three `echo` lines, re-emitted
with the pair state added) plus the comment lines the other edits rewrote. Every region was re-located by
`grep` after the edit rather than by arithmetic, because this file's regions are read by line number and a
landing that moves them silently is a defect this project has paid for twice.

**The six record states were rehearsed from the file's own bytes** — lines `328-578` extracted from the
edited file into a harness that supplies only what the surrounding script supplies (`OUT`, `ENTRY_BIN`,
`fail`) — and the extract was re-compared byte for byte with a fresh `sed` of the file before this run:

| case | record | exit | what it printed |
|---|---|---|---|
| A | the real one (`SEAM_POC=0`, `SEAM_MEASURE=1`) | **0** | the `SEAM_MEASURE=1` narration, both keys printed |
| B | `SEAM_POC=1`, `SEAM_MEASURE=0` | 0 | 535's narration |
| C | both 0 | 0 | the `else` narration, "the only state this paragraph is printed in" |
| D | both 1 | **1** | the pair refusal, naming both keys and the test |
| E | no `SEAM_MEASURE` line | **1** | `has no STAGE90_XNU_SEAM_MEASURE line` — names the key |
| F | `SEAM_MEASURE=2` | **1** | `are not 0 or 1: these seven are switches` |

All three narration branches and all three refusals were observed, each in a run where the others cannot
fire (A/B/C are mutually exclusive states of two switches; D/E/F are decided by three disjoint inputs).
**And the "before" was taken the same way**: the `HEAD` file's region, extracted by the same `sed`, run
against case A returns exit 1 with the `_unshown` message quoted at the top of this document, and against
case D returns the same message — i.e. the old gate could not see this record at all, and the pair state
would have arrived as a complaint about the gate's key list.

**One defect in the new code, caught only because the rehearsal was run**: case D first exited 1 with
`body.sh: line 138: ENTRY_ELF: unbound variable` and **no refusal text**. `ENTRY_ELF` is assigned at
`:1736`, far below the new clause at `:464`, and the refusal message named it — a reference below its own
definition, the same seed problem 570 section 4 and 573 section 4 recorded twice each. The repair was to
put the **literal** path `out/stage90/xnu_arm_entry.elf` in the message, which also makes the refusal
readable without knowing the script. The tell was again the exit code disagreeing with the text: *an exit 1
with no refusal printed is not this clause refusing.* Also worth never re-litigating: bash parses
`[[ ! ( $V -eq 1 && $W -eq 1 ) ]]` as intended (verified in isolation: both-1 → the pair, `0/1` → not),
so no capture-through-`$?` was needed.

## 4. The live gate, read by section

Run on the tree as this lands, with the peer's build in flight:

```
GATE EXIT=1
```

— and it refused at **`== the entry image's own sources ==` (`:581`, the `fail` at `:711`)**, naming
`entry_trace.c`: the record on disk is bound to entry image `151425c4…`, whose sources manifest carries
`cf5e6c9d…`, while the file on disk hashed `3bee1963…` when the run was taken. That is the peer session's
mid-edit working tree — the same shape 573 section 3 documented — and it is a clause **downstream** of
everything here, so it says nothing about this change either way.

What matters is upstream of it: `== which arm the entry image in out/ is, in words ==` (`:466`) **cleared
and printed both of this change's keys and the new narration**, in this run, on the real record:

```
  STAGE90_XNU_SEAM_POC=0
  STAGE90_XNU_SEAM_MEASURE=1
  the seam (SEAM_MEASURE=1): the interception IS in this image and **535's operation is NOT** -
```

A red gate has to be read by *which section* refused, and 570 section 5's rule applies here in its easier
direction: this time the section that refused is *after* the change, so the changed text printing is a
reading of the change rather than an inference from a green elsewhere.

## 5. State

- **572's measured arm is built and unrun.** `out/` carried `SEAM_POC=0` / `SEAM_MEASURE=1` in the record at
  the time of writing (record `01:32:19`, entry image `151425c4…` / `5519996` B, payload `stage90.bin`
  `0f108392…` / `6015356` B re-embedded `01:33:12`), and a further `build_entry.sh` was observed running —
  so **any hash in this section is a present-tense claim about `out/`, not a property of the arm**; the
  record↔bin agreement at `:335` is the thing to re-read before the boot, and this change makes the
  *record* the thing that decides which arm the gate narrates.
- **535 ran and did not come back** (572): the device is **hung and needs a power press**. One boot equals
  one window (557), so the next run is the user's to spend. Nothing was touched here: no `fastboot`, no
  `adb`, no `flash`, nothing written to storage.
- **The gate is green on the record as soon as the sources clause clears** — the region this change lives
  in does not depend on the arm.
- **TWRP-to-storage stays withheld.** `如果os已经能进去了的话` is the same unmet clause as
  `起码要能进入操作系统`: the boot reaches pid 1 and does not survive the idle pass, and 535 moved a
  returning death into a non-returning one, which is further from the goal, not closer to it.
