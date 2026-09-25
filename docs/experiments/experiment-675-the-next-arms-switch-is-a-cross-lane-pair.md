# 675: the next arm's switch is a coordinated edit, and half of it is the other lane's file

673 answered *whether* the arm after the press needs source that does not exist yet — it does — and that the
edit is inside the manifest the gate recomputes. This step asks the mechanical question that decides how long
the build takes once the press lands: **what exactly has to change, in which files, and what refuses if one of
them is missed.** The answer is that 673's "~10 lines behind a new switch" is right about the C and wrong about
the step, because a switch is not a `#define` here — it is a **key in a record**, and the record's key set is
compared for equality against a list that lives in the other lane's file.

Nothing was built, nothing was sent, no device was addressed, and **no file in 660 §5's closure was edited** —
every reading below is `grep`/`awk` over the frozen tree and over the arm's own record in `out/`.

## 1. A new switch is a key in a record, and the record is compared for equality

The entry build writes `out/stage90/xnu_arm_entry-config.txt` (`build_entry.sh`'s `ENTRY_ARM_KEYS` loop), and
the gate reads it as `ENTRY_CFG` (`preflight_boot_check.sh:489`). The gate then asks **two opposite questions**
about it (`:564`–`:588`):

* every key in `ENTRY_CFG_KEYS` must be **present** in the record — else
  *"has no $_k line … a record without that key would let a run go out with a switch nobody recorded"*;
* and no `STAGE90_*` key in the record may be **absent from** `ENTRY_CFG_KEYS` — else
  *"carries key(s) this gate does not print … a switch recorded on the build side and not shown here is a switch
  the next run would go out with unread; add it to ENTRY_CFG_KEYS above"*.

**Measured, the two sets are exactly each other today**, which is what makes the second clause a live one and
not a formality:

| list | where | size |
| --- | --- | --- |
| `ENTRY_ARM_KEYS` | `build_entry.sh:500-504` | **13** |
| the record `xnu_arm_entry-config.txt` writes from it | `out/stage90/` | **15** = the 13 + `STAGE90_XNU_ENTRY_SHA256` + `_BYTES` |
| `ENTRY_CFG_KEYS` | `preflight_boot_check.sh:564-569` | **15** |

`comm`-ed both ways, the record's `STAGE90_*` keys and `ENTRY_CFG_KEYS` are identical sets, and the build's list
differs from the gate's only by those two artifact keys. So a switch added on the build side and not on the gate
side makes the gate **refuse** — and this is the same two-sided constraint 594 §b measured for
`STAGE90_XNU_IDLE_NO_SLEEP`, where gate and record "were one change that had to land together".

**And that half is not this lane's file.** `stages/stage90/preflight_boot_check.sh` is `run-experiment-526`'s
lane by the working agreement ([[mi4-file-lanes]]), so the pair that has to land together is a **cross-lane
pair**, and the build side cannot close it alone.

## 2. The roster: a new seam switch is six sites in the build, three in the gate, one in the C

Traced from the two switches that already exist and from 594's, all of which are in the same shape. For a switch
`STAGE90_XNU_SEAM_END_RUN` (673 §2's name) that gates a block in `entry_seam_flush`:

| # | file | site | what it is |
| --- | --- | --- | --- |
| 1 | `build_entry.sh:418-422` | `SEAM_POC=${…:-0}` + `case` | the variable and its 0/1 refusal — the pattern to copy |
| 2 | `build_entry.sh:436-448` | `SEAM_MEASURE` + the mutuality refusal | a **third** arm joins this: 673 §2 asks for the three-arm assertion |
| 3 | `build_entry.sh:450-451` | `STUB_DEFINES+=(…)` | reaches `entry_stubs.c`/`entry_timebase.c` — **not** `entry_trace.c` (§3) |
| 4 | `build_entry.sh:500-504` | `ENTRY_ARM_KEYS=(…)` | the record's key set — the half 675 §1 measures |
| 5 | `build_entry.sh:551-565` | the record loop's `case` | how the value reaches the record |
| 6 | `build_entry.sh:827-836` | the `entry_trace.c` compile line | the switch has to be passed **here** (§3) |
| 7 | `entry_trace.c:437-439` | the `#error` | extended to three arms, per 673 §2 |
| 8 | `preflight_boot_check.sh:564-569` | `ENTRY_CFG_KEYS` | **the other lane** |
| 9 | `preflight_boot_check.sh:615-634` | the variant loop (`_vmiss`/`_vdup`/`_vbad`) | if the gate is to *narrate* it as a 0/1 variant |
| 10 | `preflight_boot_check.sh:574` and `:513` | the prose that counts | see §4 |

Nothing in the roster is surprising except #6, which has a measured defect behind it.

## 3. The sharp one: a switch used in `entry_trace.c` must be passed on that file's own compile line

`build_entry.sh:820-826` is a comment written after 517's build, and it is the whole of this section:

> *"`STUB_DEFINES` reaches `entry_stubs.c` and `entry_timebase.c` and not this file, so the flag-on build
> compiled `#if STAGE90_XNU_EXIT_POC_FLUSH` to its default of 0 — the image would have been byte-identical to
> the flag-off one while the build reported the flag as on."*

So item #3 (the `STUB_DEFINES` line every existing seam switch has) **does not reach the file 673's block lives
in**. The seam's body is `entry_trace.c:2250`'s `entry_seam_flush`, and that translation unit is compiled at
`build_entry.sh:827-836` with seven switches passed explicitly (`EXIT_POC_FLUSH`, `SLOT_NULL`,
`IDLE_CACHE_ENABLE`, `ISTACK_SEPARATE`, `SEAM_POC`, `SEAM_MEASURE`, `IDLE_NO_SLEEP`). A new switch missing from
**that** line produces the arm 517 produced: the record says 1, the gate prints 1, the readiness tool names the
arm by the record, and the image **is the other arm** — which for 663 §2's arm means a run that returns to the
idle exit and dies at the `pop`, i.e. a press spent reproducing the acting arm while every surface says
otherwise.

The lesson generalises to the roster: **#3 and #6 are not two spellings of one thing, and a build that has one
of them has the defect, not a partial fix.** For a switch whose block is in `entry_seam_flush`, #6 is the one
that matters and #3 is optional; for a block in `entry_stubs.c` it is the reverse.

## 4. Two pieces of prose count the keys, and both go stale on the same commit

Neither is a check, and both are read by the next operator as if they were:

* `preflight_boot_check.sh:513` — *"The list was nine, then twelve, and is now fifteen"* — with the additions
  narrated one per paragraph below it (`:522` "the thirteenth is 535's", and so on). A sixteenth key makes the
  sentence false, which is the shape [[mi4-a-claim-in-a-comment-is-not-a-check]] names.
* `preflight_boot_check.sh:574` — *"The eight variant keys (SLOT_NULL, EXIT_POC_FLUSH, IDLE_CACHE_ENABLE,
  ISTACK_SEPARATE, IDLE_STACK, SEAM_POC, SEAM_MEASURE, IDLE_NO_SLEEP)"* — the refusal's own message, naming a
  count **and** a membership. The loop it belongs to (`:615`) lists exactly those eight, so adding the new key
  there is item #9; the message then names nine while saying "eight".

Both live in the peer's file (§1), and neither is caught by anything — the gate cannot fail on its own prose.
**The remedy is the same rule 673 §5 applied to itself: name the count's source rather than the count, or update
it in the same commit as the list.**

## 5. What this changes about the plan, and what it does not

* **The post-press build is one commit on the build side and one on the gate side, and they must land
  together** (594 §b). Neither is on the press's critical path: the press is armed, gated, parked and
  pre-registered, and the switch does not exist in any image.
* **The sequencing 673 §3 derived is unchanged and now has a second reason.** 673's reason was the gate's
  sources manifest; §1 here is a second, independent refusal on the same edit, from a different file, and it
  is the one that cannot be closed from this lane alone.
* **It does not re-open the press order** (674 §4's rule). Which arm the new switch builds — 663 §3.1's
  rehearsal or §2's arm, bite alone or both nets — is decided by the press's own reading (a time), and this
  step was deliberately taken so that the *plumbing* is known before that reading exists.
* **One thing the roster implies for the build after the press, and it is worth saying before it is
  discovered:** the arm's config must record **which** net it forced (663 §2, 574's rule). That is a second
  new key in `ENTRY_ARM_KEYS` — unless the two nets are one switch with two values, which §1's whole subject
  is: **a key with two values and one definition is the class this project has paid for most often**
  (`build_entry.sh:432`'s own words about the seam's two switches). So it is two keys or one key whose value
  is an enumerated string, and the gate's variant loop is written for 0/1 — a third form the `_vbad` clause
  would refuse by construction.

## 6. Safety, and what this does not do

Read-only throughout: `grep`, `awk`, `comm` and `Read` over the frozen tree, plus one `grep -o` over the arm's
own record under `out/`. **No build, no byte written under `out/`, no edit to `stages/stage90/xnu_arm_boot/**`
or to `preflight_boot_check.sh` (the two files §1–§4 are about), no gate or runner invocation, no `fastboot`,
no boot, nothing written to storage, the neighbour `33e80afe` untouched.** The armed launcher (pid **426955**,
deadline **13:21:30 UTC**) was left alone: a `ps` of its pid and a `tail` of its log.

**It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** No boot, no reading, no device. The frontier
is where 652 left it — XNU reaches pid 1, runs the userland phase, dies at the idle exit's `pop {fp, pc}` — and
this step shortens the interval between the press and the next arm's build without being the answer.
**TWRP-to-storage stays withheld**, because 「如果os已经能进去了的话」 is unmet.
