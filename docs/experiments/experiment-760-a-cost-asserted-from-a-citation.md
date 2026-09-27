# 760: a cost asserted from a citation — the park is restorable, and the build claim under my own sentence was already refuted

**Host-side only. No device action, no press, no gate, no runner, no build, `out/` untouched.** The
measurements below are `stat`, `find`, `cmp` and `git` over `out/stage90/` and `src/`, plus two documents and
one memory entry read in full. **No arm is moved, and no press is spent or owed.**

**Why this exists.** 759 §6, landed yesterday, ends its *what this does not say* list with a sentence I wrote
in three places: *"A new rung costs the live payload, because `./build.sh` does not reproduce (408)."* It is
wrong twice, and both halves are cheap to check. **The claim it cites had been measured away four days
earlier**, and **the cost it names is not the cost.** This document states both corrections with the
evidence, applies them, and records the defect — because a wrong reason for a right rule is the kind of
sentence this project has learned to treat as a land mine: the rule survives, and the next reader inherits the
reason.

## 1. The sentence, and what it rests on

Three occurrences, all mine, all in the last two days:

| where | what it says |
| --- | --- |
| `docs/experiments/experiment-759-…:138-139` | *"A new rung costs the live payload, because `./build.sh` does not reproduce (408)."* |
| `records/revert-set.txt:5299` | *"the payload build does not reproduce (408), so a new rung spends the live payload"* |
| `scripts/preflight_boot_check.sh:280-283` (**this lane — §7**) | *"a payload rebuild does not reproduce (408) and would cost the freeze for a change in nothing the payload consumes"* |

**None of the three measures anything.** Each names *408* the way one names a rule, and 408 is an **index**.
An index is not a claim: the claim lives in the entry, and reading the entry is what would have shown it
recorded, dated and narrow. This is the whole defect, and §8 names it.

## 2. What 408 says, and what measured it away

**408 (2026-09-22, 533's session).** `526`'s frozen payload `7819cddb…` against a same-tree rebuild's
`76bf4ea7…`: 48 bytes apart, localised to 44 bytes inside
`stage90_xnu_pmap_bootstrap_contract_selftest` / `stage90_xnu_pmap_table_dryrun_contract_selftest` and 4
immediately before `stage90_embedded_macho_size` / `stage90_embedded_macho`, with the embedded Mach-O
byte-identical once the offset delta is applied. The reading drawn was *the payload link consumes something
beyond the committed sources*, and 533 §6 states it as an open cause, with the consequence that **arms are
compared through the entry image**.

**666 §5 (2026-09-25), recorded as m675 — three payload builds, all host-side:**

| build | `stage90.elf` | `stage90.bin` | `stage90-qcdt.img` |
| --- | --- | --- | --- |
| plain, twice, no flags | `40a847d4…` | `cc286b79…` | `2ae69c9f…` — **identical both times** |
| `-DSTAGE90_XNU_ENTRY=1`, pre-fix sources | `936c2741…` | `a47bc89f…` | `a5997bae…` |
| the parked acting arm `armed-seam-poc-a43304f2` | `936c2741…` | `a47bc89f…` | `a5997bae…` |

The third row is a **rebuild from the tree**, compared file by file against the park: **all seven members
identical**, entry image included. And the 48 bytes that started 408 are exactly one switch —
`kernel_size` **6,015,356** with `STAGE90_XNU_ENTRY=1` against **6,015,308** without it, the
`bl stage90_xnu_entry_run` path and nothing else. Two plain builds agreeing is the control that makes the
flag experiment mean something.

**So the citation I used was a claim its own project had refuted, and the refutation was on the record four
days before the sentence was written.** Read as a claim about the artifact, *"`./build.sh` does not
reproduce"* is false: it reproduces, twice, and it reproduces a parked arm file-for-file.

## 3. The residual 666 §5 did leave standing — and it is not the sentence either

666 §5's own scoping is explicit and narrow, and it is worth quoting because it is the true rule the project
ended up with:

> **the bytes of an arm are a function of the tree and its switch set** … and, the cost of the same step, an
> arm built before a source change genuinely cannot be rebuilt after it, so **the park rule stands for a
> better reason than the one it was adopted for.**

A payload **embeds `xnu_arm_entry.bin`**. So once `src/` has moved, a past arm's bytes can still be *sent*
and can be *restored from its park*, but can no longer be *produced*. That is a statement about **a past
arm's rebuildability**, and it is not a statement about a future build spending anything.

## 4. The other half: a build does not write the park

**Measured this step.**

    $ find out/stage90 -maxdepth 1 -type f | wc -l
    128
    $ ls out/stage90/frozen/armed-storage-46fe6737/ | wc -l
    11

and the 11 park members, each against its live counterpart:

    for f in out/stage90/frozen/armed-storage-46fe6737/*; do cmp -s "$f" "out/stage90/$(basename "$f")" …
    IDENTICAL  SHA256SUMS.txt        IDENTICAL  stage90.img          IDENTICAL  xnu_arm_entry.elf
    IDENTICAL  stage90.bin           IDENTICAL  stage90-qcdt.img     IDENTICAL  xnu_arm_entry-config.txt
    IDENTICAL  stage90-build-config.txt  IDENTICAL  xnu_arm_entry.bin   IDENTICAL  xnu_arm_entry-sources.txt
    IDENTICAL  stage90.elf           IDENTICAL  stage90_fixture.macho

**11 of 11 identical, and 117 of the 128 top-level files are not in the park at all** (plus every file in the
subdirectories). **What the 117 are is the point**: `build.sh`'s intermediates — `.o`, `.log`, `.map`,
`.disasm`, `.size`, `.symbols`, the generated headers, `xnu_arm_entry_blob.c`, `xnu_arm_entry_realstubs.c`,
`stage90_aes_kat`. A rebuild rewrites them, and **they are not the arm.** The arm's definition is the 11 the
gate reads and the runner sends.

**And a rebuild does not write `frozen/`.** Nothing in the build path names it; the park is bytes on disk
under a directory the builder never opens. So the operated cost of a build is not the park, and the park's
restoration is a `cp` — which is what the park is *for*, and what the record says it is for.

## 5. So what does a new rung actually cost, stated exactly

1. **One press**, if the rung is to be measured — and a press is the operator's to authorize, not the
   ladder's to spend.
2. **The arm in `out/` its rebuildability from the tree** (§3): once `src/` moves, its bytes can still be
   sent and can still be restored from the park, but cannot be produced again.
3. **Not the park, and not the capture.** Both are bytes on disk outside the build path (§4).
4. **Not reproducibility**, which is not in doubt: `./build.sh` *does* reproduce (§2).

Items 1 and 2 are the real ones and they are enough to keep the discipline. The reason I gave was neither.

## 6. A same-tree entry reproduction, measured today — with its attribution stated

While checking §4, the mtimes said something worth recording. **The live entry image is newer than the copy
in its own park, and the bytes are identical:**

    $ stat -c '%y %n' out/stage90/xnu_arm_entry.bin out/stage90/frozen/armed-storage-46fe6737/xnu_arm_entry.bin
    2026-09-27 03:24:19  out/stage90/xnu_arm_entry.bin
    2026-09-27 02:49:11  out/stage90/frozen/armed-storage-46fe6737/xnu_arm_entry.bin

    $ cmp out/stage90/xnu_arm_entry.bin out/stage90/frozen/armed-storage-46fe6737/xnu_arm_entry.bin
    (identical)

and 36 files under `out/stage90` carry mtimes after 03:20 — `xnu_arm_start.o`, the `xnu_arm_entry_*.o` set,
`xnu_arm_entry.elf`, `xnu_arm_entry.map`, `xnu_arm_entry.bin`, `xnu_entry_text.bin`,
`xnu_arm_entry_seam.dis`. The payload's own files are at 02:56. **So an *entry* build ran at 03:24 and the
payload was not relinked.** Its result is byte-for-byte the park's entry image.

**What that shows:** the entry image was rebuilt from the current tree and reproduced — 533 §6's entry half,
re-measured on today's tree, by `cmp` and not by argument.

**What it does not show, said plainly:** *this document did not run that build and does not attribute it* —
no tool in `tools/` or `scripts/` invokes `build_entry.sh` (checked: every reference is prose, a `fail`
message, or a `build.sh` hint), so it was a manual one and the record of why is not here. **Nor does it
re-measure the payload half** — no payload was relinked, so 408's `7819cddb…`/`76bf4ea7…` pair is untouched
by it. And two arms over two days is evidence, not a determinism proof; m675's control (two plain builds
agreeing) plus this one is what the claim rests on, and it is stated at that width.

## 7. The correction, applied — including the site with the worst consequence

- **`experiment-759-…:138-139`** — rewritten to name the real cost (a press, and a past arm's
  rebuildability) and to say that the bullet said the opposite before this step.
- **`records/revert-set.txt:5299`** — rewritten the same way; the block's own arm, press and reading are
  unchanged.
- **`scripts/preflight_boot_check.sh:280-283`** — **rewritten, and this is the site that mattered.** That
  file is this lane's (`run-experiment-526` owns the gate; `ListAgents` re-run this step says *this session
  is `run-experiment-526`*, and the gate-repairs record written from this session's own side calls it *my
  lane's gate* — **the lane memory's mine/theirs list is written from `mi4-ios6-1a`'s side and inverts for
  it**). The old sentence read: *"the remedy this clause prescribes — rebuild — is the one thing this phase
  cannot do, because a payload rebuild does not reproduce (408) and would cost the freeze for a change in
  nothing the payload consumes."* **That is the worst instance of the three**: the comment is the stated
  reason *not to* run the remedy the clause itself prints, so a wrong reason there is not a wrong footnote —
  it is an instruction. It now says what refuses the rebuild: a rebuild replaces the arm in `out/`, and once
  `src/` has moved a past arm can no longer be produced from the tree. `bash -n` and a gate run follow in
  §7.1.

### 7.1 What editing the gate costs, and the check that it cost nothing

The gate is the file a live catcher fires by path, so a comment edit there is not free of procedure. On the
record: **nothing reads the gate's narration** — `run_and_capture.sh:2341`/`:2343` invoke it and branch only
on **its exit status**, with no command substitution, pipe or `grep` over its output; `press-watcher.sh:287`
appends the whole stdout to its own log and then tests the status. **So a comment cannot change what a run
does, only what the gate prints into its own record** — and the one mechanical risk is an edit that makes the
gate **exit non-zero**, which would spend a press on nothing. It is checkable without a press:

    $ sha256sum scripts/preflight_boot_check.sh      # before the edit
    07496d8e0ecb89f76880671094360413554a49857e8dd864e7f43a9aa359ecf0
    $ bash -n scripts/preflight_boot_check.sh        # syntax ok
    $ git diff -U0 -- scripts/preflight_boot_check.sh | grep -E '^[+-]' | grep -vcE '^[+-][[:space:]]*#'
    0                                                # every changed line is a comment
    $ ./scripts/preflight_boot_check.sh --allow-xnu-entry
    exit 0, 589 lines
    $ ./tools/verify_press_ready.sh
    ok: 5 check(s), exit 0
    $ sha256sum scripts/preflight_boot_check.sh      # after
    163ebf39164fe5257f4552678e1646a9d9bac05ec6f7e3041e5079018eec454e
    # file 2859 -> 2873 lines, 14 added, all comment

**The strongest of those is the third**: the diff being comment-only is a *structural* statement, not a
review — it cannot be satisfied by a store or a `fail` that happens not to fire. And **no catcher is alive**
(`ps`, this step: zero matches for `press-watcher` / `press-on-clear` / `run_and_capture` /
`preflight_boot_check`), so there is no armed press for the edit window to land inside — which is the
condition the 620 rule requires, not a convenience.

**Nothing else is rewritten.** 408 and 666 §5 stay exactly as they are: the project's rule is that a
superseded reading is *superseded*, not deleted, and the correction is the new step.

## 8. The defect, as an instance — **m770: a cost asserted from a citation rather than measured**

I read `(408)` in the record, took it as a licence to write *a stronger sentence than 408 supports* — 408 says
*a cause is unresolved for the payload*; I wrote *a new rung spends the live payload* — and repeated it three
times in two days without one `cmp`. **The cited claim had been refuted by a step already on the record.**

**Shape to suspect first: a citation that names a *number* and not a *sentence*.** "(408)" reads as settled
because it is precise, and an index is the one form of reference that cannot be verified by reading it — you
have to open the entry, and the entry is where the refutation lives. This is m679's shape (the FAIL row's
reason was whatever matched first) read from the other end: there the tool attached a *plausible* text to a
failure; here I attached a *plausible* cost to a number. **The test is the same in both directions — name
the artifact in words and check the words, not the label.**

**And the second half is its own lesson, which is the one worth keeping:** a rule can be right and its stated
reason wrong, and the wrong reason is what corrupts the next decision. Had the operator weighed *"a new rung
costs the live payload"* against *"a new rung costs one press and one past arm's rebuildability"*, they would
have been weighing two different questions — and the second one has a `cp` as its recovery.

## 9. What this document does not say

- **It does not say a rebuild is free or that the park should be changed.** It says the cost I named was the
  wrong cost, and names the right one (§5). The park's contents, the 11-file set, and the arm in `out/` are
  all untouched.
- **It does not re-open 408's `526` pair.** §6 states the limit: no payload was relinked, so that pair is
  exactly where 666 §5 left it.
- **It does not authorise a build, an arm, or a press.** No firer is armed, `out/` still holds the arm 758's
  press spent, and the press path remains the two commands readiness prints, run by hand once each.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount, no reading of the hardware. What
  it buys is one thing: **the next reader who weighs a rung weighs the real cost.**

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block starts a command and never
completes one — and 「让os可以正常启动并且挂载存储」 is not reached, so **TWRP-to-storage stays withheld.**
