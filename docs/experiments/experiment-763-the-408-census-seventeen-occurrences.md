# 763: the 408 census — 760 corrected three occurrences of a refuted claim and seventeen were in the tree, one of them sixty lines below the site it fixed

**Host-side only. No device action, no press, no gate against a device, no runner, no build, `out/`
untouched.** The measurements are `git grep` over the tree at `4e126c9`, read in full at every hit, plus
`bash -n`, the gate, `tools/verify_press_ready.sh`, `tools/check_backtick_messages.sh` and `make check`.
**No arm is moved, nothing is parked, and no press is spent or owed differently than it was.**

**Why this exists.** 760 §1 opened with a census: *"Three occurrences, all mine, all in the last two
days"*, and §7 listed the three it corrected. **A `git grep` over the same tree finds eighteen lines
carrying that claim in five files, and one of the two sites 760 walked past is sixty lines below the one
it fixed, in the same record block.** A census that names *the sites its author remembers* is not a
census, and the claim's whole subject is that a wrong reason survives to corrupt the next decision — so
the count is the finding, and the repairs are the smaller half.

## 1. The census, run as a pattern and not as a recollection

    git grep -In -E 'does not reproduce|NOT byte-reproducible|not byte-reproducible|reproduce \(408\)|\(408\)' \
        HEAD -- . ':(exclude)docs' ':(exclude)archive' ':(exclude)external' ':(exclude)out'

| file | lines | occurrences |
| --- | --- | --- |
| `records/revert-set.txt` | 88, 2783, 2852, 3158, 3586, 3799, **5365–5366** | 7 |
| `scripts/preflight_boot_check.sh` | 286, 330, 1317, 1332 | 4 |
| `tools/verify_press_ready.sh` | 8, 420, 1190 | 3 |
| `src/entry/build_entry.sh` | 972, 33090 | 2 |
| `Makefile` | 95 | 1 |
| **total** | **18 lines** | **17** |

**Seventeen and not eighteen, because one occurrence spans two lines** — the record's 5365–5366 — and the
line-oriented grep that produced 760's number is exactly the instrument that under-counts that way. The
count is stated in occurrences because a census of *lines* has a producer the artifact controls.

And read each one rather than counting it, which is the other half of 760 §8's lesson:

| verdict | sites | what was done |
| --- | --- | --- |
| **already corrected** | `preflight_boot_check.sh:286` | 760 §7.1's rewritten comment, which *quotes* the refuted sentence and therefore still matches the pattern. Nothing to do. |
| **corrected by this step** | `preflight_boot_check.sh:330, 1317, 1332`; `verify_press_ready.sh:8, 420, 1190`; `Makefile:95`; `revert-set.txt:5365–5366` | §3 below. Nine occurrences. |
| **left standing as history** | `revert-set.txt:88, 2783, 2852, 3158, 3586, 3799` | §4 below. Six occurrences. |
| **not the refuted claim at all** | `build_entry.sh:972` | the phrase *"does not reproduce the arm in out/"* is about `build_entry.sh`'s **switch set** — *"four of these six switches are off their defaults"* — a true statement about a blind invocation, with no `408` citation. It matched the pattern and it is not the defect. |

**So 760's three was one corrected site, one quoted sentence, and a target the census never measured** —
and the habit of citing an index instead of a sentence is what let it look complete.

## 2. The sharpest instance: the same sentence twice, sixty-six lines apart, in the block 760 edited

The record's 759 block says, at 5299–5303:

> a new rung costs ONE PRESS and the arm in `out/` its REBUILDABILITY … **Corrected by 760; this block said
> the opposite before that step.**

and at 5365–5366, in the same block, in that block's closing *no arm moved* paragraph:

> **A NEW RUNG COSTS THE LIVE PAYLOAD - ./build.sh does not reproduce (408).**

**760 corrected one occurrence of a sentence and left the other in the same paragraph run.** That is not a
citation defect — the citation was corrected — it is the census defect one level down: the step fixed what
it found where it was looking. It is m719's shape (*a value nothing reads* → *a sentence nothing checks*)
read in a record file rather than in a build.

## 3. What the nine corrected occurrences now say, and why it is a better reason

**The rule was right and the reason was wrong, and the right reason turns out to be the very measurement
408 made.** 408's 48 bytes are `kernel_size` **6,015,356** with `STAGE90_XNU_ENTRY=1` against **6,015,308**
without it — *the switch*. So:

- **The payload build is reproducible** from the tree **and its switch set** (666 §5: three builds, twice
  plain and identical, and the parked acting arm rebuilt from the tree with all seven members identical).
- **What a careless `./build.sh` actually costs is the SWITCH.** `STAGE90_XNU_ENTRY` is the switch whose
  default the build leaves **OFF** (`check_payload_config_entry` measures exactly this, across every parked
  arm), so a plain `./build.sh` rebuilds the payload into an image **that never jumps into XNU** and
  overwrites the armed bytes in `out/`. After a source change the arm then exists only in its park, and
  restoring it is `tools/verify_revert_set.sh DIR --set=NAME` — bytes on disk, outside the build path.

**That is a strictly stronger instruction than the one it replaces**, and it is checkable: the old reason
said the rebuild is dangerous *because it is not deterministic*, which is false and therefore ignorable; the
new one says the rebuild is dangerous *because of one switch's default*, which a reader can look up. Each
corrected site says which sentence it used to carry, so the record of the wrong reason survives next to its
correction — the project's rule about superseding.

The three sites that are **instructions a reader acts on** were done first and are the ones that mattered:
`verify_press_ready.sh:1190` is the refusal the operator reads **at the moment of deciding whether to
press**; `preflight_boot_check.sh:1332` is the gate's own refusal text; `Makefile:95` is `make help`. 760 §7
called the gate comment *the worst of the three* for exactly this reason — a wrong reason in a refusal is
not a wrong footnote, **it is an instruction**.

## 4. The six that are left, and why leaving them is the rule and not an omission

`revert-set.txt:88, 2783, 2852, 3158, 3586, 3799` are the **blocks of past steps** — 594's, 710's, 724's,
738's, 747's and so on — and each is the record of what was believed when that step ran. **This project's
standing rule is that a superseded reading is superseded, not deleted**, and 760 §9 restated it: *"408 and
666 §5 stay exactly as they are."* Rewriting six historical blocks would erase the history of the defective
reason while buying nothing a reader of the *live* tree needs.

**What they get instead is this document's census.** The next reader who greps the pattern finds eighteen
lines, and §1's table says which are corrected, which are quotations and which are history. **That is the
durable repair: a census that can be re-run is worth more than six edits that cannot be told from the
wording around them.**

## 5. The one site this step deliberately did NOT fix, and it was measured rather than assumed

`src/entry/build_entry.sh:33090` carries the refuted claim in a comment explaining why the gate's entry
sources are hashed by content rather than by mtime. **I edited it, and the gate refused:**

    == the entry image's own sources ==
    files that differ from the manifest … was written with:
      on disk:  build_entry.sh 0db3ff31…
      manifest: build_entry.sh ed378814…
    REFUSING: the entry image is not the build of these sources: build_entry.sh

**`build_entry.sh` is a build input of the armed image's manifest**, so a comment in it is not a comment to
the gate — it is a source change to the one file that says whether the entry image is the build of this
tree. Reverting the edit restored the gate to **exit 0 / 590 lines** in one command.

**And the prescribed repair is an entry rebuild of a live, unspent arm** — the branch the gate prints, and
the branch this very step had just rewritten. `build_entry.sh` is byte-for-byte reproducible and the bin
would almost certainly come out identical (760 §6 measured a same-tree entry reproduction at 03:24), and
the park holds all eleven members so a restore is a `cp`. **A comment that buys a rebuild of the arm a press
is waiting on is not worth it**, and *almost certainly identical* is not a standard this project accepts for
a step whose whole subject is an unverified claim.

**So the site is recorded as owed, not fixed**, in the same shape the project uses for 697's `HOST_VERSION
0xFE` debt: the next step that rebuilds the entry image — the rung-23 arm, or anything else that moves
`src/entry/` — should carry this one sentence with it. **The disclosure is the point**: an unfixed site that
is named is a debt, and an unfixed site that is not is what this document is about.

**And the lane question was re-resolved rather than remembered.** `ListAgents` this step reports *this
session is `run-experiment-526`* and two peers, **neither of them `mi4-ios6-1a`** — so the tree's lane
agreement, whose other half is that session, has no live addressee today, and a `SendMessage` "report" would
have gone nowhere. **The reason this site is unfixed is the build cost and not the lane** — which is the
distinction worth keeping, because a lane rule invoked against a session that is not running is a rule
enforcing nothing.

## 6. And the project's own check caught this step's new refusal text

`make check` came back **`check_backtick_messages: REFUSED`** on the first run after the edits:
`tools/verify_press_ready.sh:425: 2 unescaped backtick(s) inside a double-quoted string`. The message I had
just written said *"the only remaining copy of the armed arm once `` `src/` `` has moved"* — **inside a
double-quoted string, which is a command substitution**, so the refusal would have fired with a word missing
from its own explanation. That is **m730 exactly**, caught by the check m730's own step added, and it is why
this file's messages name a path without backticks.

## 7. What this document does not say

- **It does not re-open 408 or 666 §5.** 408's cause is left exactly where it was; what this step corrects is
  what *later steps believed* about it. The census is a reading of the tree, not a reading of the hardware.
- **It does not rebuild anything, and it does not move `out/`.** The armed arm
  `armed-storage-c3007c37` is untouched and still un-pressed.
- **It does not edit `docs/experiments/**`.** Experiment logs are the historical record and their rows
  describe what was true when written.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. What it buys is that **the
  operator's own press decision is made against a true reason** — that is the whole of it, and it is the
  reason the three refusal sites were done first.

**Verified host-side:** `bash -n` clean on all three touched shell files; gate `--allow-xnu-entry` **exit 0,
590 lines**; readiness **5 of 5, exit 0**; `check_backtick_messages` ok across 73 files; `check_stage_paths`
312 files; `check_set_name_rule` 31 sets; `check_payload_config_entry` `STAGE90_XNU_ENTRY 1` across 31 parked
arms; `make check` **exit 0**.

## 8. New instance — **m772: a census that counted the occurrences its author remembered**

760 §1 stated *"Three occurrences, all mine, all in the last two days"* and §7 listed the three. The
pattern has **seventeen** occurrences in five files at that step's own tree, and **one of them is sixty-six
lines below the site 760 fixed, in the same record block** — the step corrected the occurrence it was
looking at and walked past its twin.

**Shape to suspect first: a census stated as a NUMBER of sites rather than as a PATTERN.** *"Three
occurrences"* reads as a measurement and is a recollection; the only artifact it can be checked against is
the pattern, and the pattern is one command away. **The test is to write the count as the command that
produces it** — and then the second test, which the count cannot do: **read every hit and classify it**,
because a corrected site still *quotes* the sentence it refutes and a shared phrase (*"does not reproduce"*)
appears in claims that are true. **Count occurrences, not lines** — a sentence can span two, and a
line-oriented census under-counts by a producer no reader can see.

Related: [[mi4-a-claim-in-a-comment-is-not-a-check]] (m770, the citation the census was about; m737, a claim
about a register that was really about a time), [[mi4-measurement-defects]] (m730, the backtick that fired
again here), [[mi4-silence-is-a-reading-only-if-success-is-silent]] (m719, a value nothing reads).

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block starts a
response-demanding command and never completes one — and 「让os可以正常启动并且挂载存储」 is not reached, so
**TWRP-to-storage stays withheld.**
