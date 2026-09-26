# 734: the rung-16 pre-registration — the quiet block, and the three rungs whose narration could not print

Host-side except where stated. **No device action was taken for anything in this document**, and the arm
it describes is **ARMED, NOT PRESSED**: the last press is still 733's (2026-09-26 10:12:21–10:13:31 UTC,
exit 0, 70 s, capture `rung15-enable-20260926-101331-last_kmsg.txt`, 636,013 B). This is the
pre-registration for the arm that follows the **pressed** rung-15 arm `armed-storage-enable-953ad0f6`,
written from 733's own failed prediction. The set it names is `armed-storage-quiet-c9738417` — those
eight hex characters are `xnu_arm_entry.bin`'s own sha256 prefix, the naming rule every storage arm in
the record follows.

## 0. Two spellings of every rung number, and why both are in this file

The ladder in `src/entry/entry_storage.c` counts a rung by the **value** of `STAGE90_XNU_STORAGE_PROBE`
— its clauses read `15 = 14 plus …`. The commit subjects and the pre-registrations count **ordinal
arms**, and the two part company at the ladder's value 9 (which has two arms), so **this arm's ladder
value is 15 and its ordinal is rung 16**. The record spells both; this document uses the ordinal in its
title and says "the ladder's value 15" when it means the switch.

## 1. What 733 measured, and the pair it left ambiguous

733's press of `armed-storage-enable-953ad0f6` measured the two cells it was built for and left one
sentence refuted:

    _int_status_before = 0x00000000        (the value-13 window, before its INT_ENABLE store)
    _int_status_after  = 0x00000001        (the same register, one store later)
    _ena_status_post   = 0x00000000        (the same register, after the restore)

730 read that `0 → 1` as **the enable revealed a completion**. 733's own log refutes the *reading*: the
register was read clear **twice** in the same run, and the transition sits across exactly one store to
`INT_ENABLE 0x34`. That pair therefore does not separate

- **(A)** a write to `0x34` that makes `0x30`'s RESPONSE bit **read** 1, from
- **(B)** a completion the block was holding, re-latched when the enable came back — 726's sixth
  hypothesis, which 733 did not put down.

**(B) needs a completion to have happened.** That is the whole design of this rung: the same one-bit
store, taken where no command has ever been issued, so (B) predicts `_quiet_status_after = 0` and (A)
predicts `1`. **Either reading is an answer**, which is what makes it a cheap rung rather than a new
act.

**And (A) may not be assumed either.** In 733's log, with the enable standing from before CMD0's send,
CMD0's own poll read `INT_STATUS = 0` for its first 538 samples (`_cmd0_any_polls = 0x21b` against
`_cmd0_inhibit_seen = 0x219`), so *writing `0x34` always makes `0x30`'s bit 0 read 1* is not a
description of the poll. It is fully consistent with *(A) makes the bit readable and the command's own
completion is what sets it* — which is why `_quiet_status_after` must be read beside `_cmd0_any_polls`
in the same log and not alone.

## 2. What the rung is

**The same two commands, the same three gates, the same census and the same bodies — plus one body of
its own, called at a moment this ladder has never used.**

    #if STAGE90_XNU_STORAGE_PROBE >= 15
    if (g_storage_mode_complete != 0u)
        st_quiet_enable_probe();
    #endif

placed in `entry_storage_probe` **immediately before `st_cmd_path()`** and after rung 9's wait: the
block is in SDHCI mode, powered and clocked (rungs 2–9 measured it), and **no command has been put on
its bus in this image's life**. The body is one function, `st_quiet_enable_probe`, and it is the whole
of the new code:

| step | action |
| --- | --- |
| 1 | read `INT_STATUS 0x30` → `_quiet_status_before` |
| 2 | read `INT_ENABLE 0x34` → `_quiet_enable_before` |
| 3 | read `SIGNAL_ENABLE 0x38` → `_quiet_sig_before` (**read, never written**) |
| 4 | write `INT_ENABLE <- (read \| SDHCI_INT_RESPONSE)` → `_quiet_wrote` |
| 5 | read `INT_STATUS` **immediately** → `_quiet_status_after` — **this rung's answer** |
| 6 | read `INT_ENABLE` back → `_quiet_held` |
| 7 | write `INT_ENABLE <- _quiet_enable_before` → `_quiet_wrote_back` |
| 8 | read `INT_STATUS` again → `_quiet_status_post`, and the enable again → `_quiet_readback` |

**Two stores, both to `0x34`, the second the undo of the first.** No new command, no `POWER_CONTROL
0x29`, no GCC word, no `core_mem` word, and no byte of the medium. `SIGNAL_ENABLE` at zero is the one
thing 730's press measured is safe — the specification's path is `INT_STATUS → INT_ENABLE →
SIGNAL_ENABLE → the line`, so a status bit enabled while the signal bit is clear is visible to the image
without raising the line — and this body **reads** it rather than writing it.

## 3. What the arm's own build holds it to

`src/entry/build_entry.sh` gains a clause for `STORAGE_PROBE -ge 15`. For `st_quiet_enable_probe` it
asserts: the symbol is present and sized; its device access set is exactly
`f9824930:ldr f9824934:ldr f9824934:str f9824938:ldr`; its counts are exactly
`f9824930:ldr=3 f9824934:ldr=3 f9824934:str=2 f9824938:ldr=1`; its distinct store set is exactly
`f9824934:str`; its program-order distinct accesses are exactly
`f9824930:ldr f9824934:ldr f9824938:ldr f9824934:str`; its image side is empty; it has exactly one call
site; **and its call line precedes `st_cmd_path`'s own call line** in `entry_storage_probe`'s
disassembly.

**That last clause is the one that matters most**, because the failure mode it refuses is the one 732's
first build actually shipped: a body placed *after* the command path re-measures the rung below while
publishing this rung's cell names. **The clause's store-set assertion is a set and not an order**, and
that is a documented limit rather than a claim: `classify_body` reports ordered-**distinct** accesses, so
it sees one `f9824934:str` where there are two. The order and the values are carried by the arm's own
cells (`_quiet_wrote`, `_quiet_wrote_back`, `_quiet_held`, `_quiet_readback`) and by the count clause
above them, which does see two.

## 4. The defect this step found, and the check that now catches it

**Three consecutive rungs had never narrated their own reading, and nothing said so.** The rung-10
(`ff753a23`), rung-11 (`1c4d0d50`) and rung-14 (`87fda503`) consequence paragraphs in
`tools/verify_press_ready.sh` were each written as

    if [[ $wst == N ]]; then
      entry_conseq+=" …N's own reading, in five things to look for…"
    fi

**inside the `sw_on STAGE90_HW_WATCHDOG_SELFTEST` branch**, where no storage arm reaches them. Each one
was anchored on the previous rung's block, so the misplacement propagated by imitation — and this step's
own rung-15 paragraph, written the same way, was the fourth.

**Nothing caught it because every other check row 4 makes is about the ARM'S NAME**, and the arm's name
was right in all four cases: the paragraph that never ran looks exactly like a paragraph that had
nothing to add. That is `[[mi4-silence-is-a-reading-only-if-success-is-silent]]`'s shape one level above
a key — an absent narration that no reader can tell from an empty one — and it is the same class as
m737/m738, where a sentence was copied from one rung's block into the next and nothing could contradict
it.

**The repair is structural rather than a move.** The paragraphs are now one statement each:

    rung_para N " …N's own reading…"

    rung_para() {
      [[ $wst == $1 ]] || return 0
      RUNG_HIT+=("$1")
      entry_conseq+="$2"
      return 0
    }

so the rung's value appears **once** — the guard and the record are the same occurrence, and they cannot
drift the way a value in a branch and a value in a comment can — and every call sits at the storage
ladder's own level, where a new paragraph cannot inherit a nesting by being anchored on its neighbour.
All eleven (rungs 3–11, 14, 15) were converted, and the rung-15 **arm** paragraph became an ordinary
`elif [[ $wst == 15 ]]` of the arm-naming chain, whose end is where the chain's own `else` is.

**And the row now reads the placement instead of trusting it.** Row 4 parses **this file** for the
`rung_para N` call sites (executable lines only — a comment is skipped, the rule
`tools/check_stage_paths.sh` uses) and refuses when the rung the arm carries is one of them and no call
recorded it in `RUNG_HIT`. The set is read out of the file, so a new rung joins the check by being
written and no list in a comment can be outgrown by the ladder. Three further refusals come with it: a
rung claimed by two call sites (a paragraph that would be appended twice), a parse that reads **no**
call site at all while the rung is ≥ 3 (the extractor failing, not the file being empty — m728's shape),
and a `$SELF` the row cannot read.

**And the same arms measure it, before and after, on their own parked bytes.** Each park is staged as a
live arm (a copy in a scratch tree, `--gate /bin/true`, nothing on the device) and the same grep is run
against the row's narration:

| arm | its own paragraph's phrase | pre-repair | post-repair |
| --- | --- | --- | --- |
| rung 14, `armed-storage-enable-953ad0f6` | "THE ENABLE IS THE ARM AND THE RESTORE" | **0** | 1 |
| rung 11, `armed-storage-cmd-892b8b68` | "THE GATE ABOVE THE GATE" | **0** | 1 |

Both arms are 5 of 5 ok in both revisions, which is the point: the missing paragraph was invisible to
every row.

**Four falsifications, all host-side, and one of them found a defect in this check's own first draft:**

| perturbation | expected | measured |
| --- | --- | --- |
| the rung-15 paragraph put back inside the watchdog-self-test branch | refuse, naming `rung_para 15` | **FAIL**, "has a consequence paragraph of its own in this file (rung_para 15) and NOTHING APPENDED IT" |
| one `rung_para 15` call site duplicated | refuse, naming the value | **FAIL**, "1 rung value(s) are claimed by more than one `rung_para` call site … : 15" |
| the extractor's own pattern broken (call sites untouched) | refuse, naming the parse | **FAIL**, "read no `rung_para` call site out of … at all" |
| the **rung-12** park (`armed-storage-instant-e8dc64f7`) staged as a live arm | **pass** — no paragraph exists for 12, so none is required | **5 of 5 ok** |

**The third perturbation's first attempt changed the wrong thing** — it renamed the call sites *and* the
extractor's pattern together, because both contain the same string, so the parse still found them and
the refusal that fired was the other branch. That is a perturbation which measured its own edit.

**And the fourth falsification is what exposed the check's own bound bug.** The empty-parse refusal was
written as `$wst =~ ^[3-9][0-9]*$` — which reads as "rung ≥ 3" and means "**starts with** 3–9", so it was
**false for exactly the rung this step is for** (15) and the refusal could not fire on it. With the
pattern broken the row printed **no FAIL at all**: the check was silent in the one case it exists for,
which is the same class it was added to catch, one level up. It is now `(( wst >= 3 ))` behind a numeric
guard, and the perturbed-extractor cell above is the measurement after that repair.

## 5. The safety contract

**The write set is two stores in one new function and nothing else.** `f9824934:str` occurs exactly twice
inside `st_quiet_enable_probe`, both to `INT_ENABLE 0x34`, the second writing back the value the function
read at step 2. `st_cmd_path`'s own two stores (732's enable and its restore) are unchanged and their
call counts are still held to one each. `SIGNAL_ENABLE 0x38` is read and never written. The linked
image's `POWER_CONTROL 0x29` (`hc_mem`'s `47 44 44 41`), the GCC set and the `core_mem` set are all
unchanged, there is no new command and no data phase, so **no byte of the medium can change**.

**The failure mode is a diagnosis, not a lost device.** If the block raises `hc_irq` (SPI 123 → intid
155) the delivery reaches the dispatcher as an unknown line — `_irq_other_count = 1`,
`_irq_other_iar = 155` — an ending this ladder already reads and survives (709 ended that way on intid
170 with every promised cell in the log). `SIGNAL_ENABLE` at zero is why it is improbable, and the
function's own step 7 is why a set enable cannot persist.

## 6. The cell table

| cell | expected | read against |
| --- | --- | --- |
| `_quiet_calls` | `1` | the body ran |
| `_quiet_status_before` | `0` | **the reading that says the block was quiet at the store** |
| `_quiet_enable_before` | `0` | 733's `_int_enable_before` |
| `_quiet_sig_before` | `0` | 726 and 730 both read `SIGNAL_ENABLE` as zero |
| `_quiet_wrote` | `0x00000001` | `read \| RESPONSE` with the read at zero |
| **`_quiet_status_after`** | **`0` is (B), `1` is (A)** | **this rung's answer — read it beside `_cmd0_any_polls`** |
| `_quiet_held` | `0x00008001`, or `0x1` | 730's own readback of a `0x1` store was `0x00008001` — bit 15 rides along |
| `_quiet_wrote_back` | equals `_quiet_enable_before` | this body's own read, not a constant |
| `_quiet_status_post` | `0` | no completion exists to latch |
| `_quiet_readback` | `0x00008000`, or `0` | **the restore is a partial one**: bit 15 is set by a write to `0x34` and not cleared by one |
| `_cmd_gated`, `_cmd1_*`, `_cmd0_complete` | as 733's | the rung below is inherited unchanged |
| `_cmd0_status_any`, `_cmd0_any_polls` | as 733's (`1`, `0x21b`) | **the counter-evidence (A) has to live with** |

**A `_quiet_held` of `0x00000001` — the written value read back unchanged — would contradict 730's and
733's readbacks**, and either reading is a fact about the block rather than a failed run. **A
`_quiet_status_after` of `1` retires 726's sixth hypothesis rather than confirming it**; of `0` confirms
it at the one moment it could be tested. Neither is a failure, and neither is an absent key to be
explained away.

## 7. What the rung is not

It is not a storage driver: the command path is still two commands with no data phase, and the new body
sends nothing at all. It is not a new rung of *act* — one store and its undo, on a block that has no
command in flight. It does not meet the goal: **「把基础驱动跑起来」/「起码要能进入操作系统」 is still
unmet**, so **TWRP-to-storage stays withheld** (the clause is conditioned on 「如果os已经能进去了的话」). And
it is not the last rung: whatever `_quiet_status_after` reads, **a delivery on this line has never been
taken on this ladder**, and that is the next question either way.

## 8. Host-side work, and what verified it

No `fastboot`, no `adb`, no `sudo`, no build of the payload's sources for this document, nothing written
to the device, and no press. What was run, and what it answered:

| command | reading |
| --- | --- |
| `tools/verify_press_ready.sh` | **5 of 5, exit 0** — row 4 green with the repaired narration |
| `tools/rehearse_revert_set.sh` | **38 ok / 0 failed** |
| `tools/verify_revert_set.sh out/stage90/frozen/armed-storage-quiet-c9738417 --set=…` | VERIFIED, 11 files |
| `tools/verify_revert_set.sh /mnt/data/mi4-ios6-export/armed-storage-quiet-c9738417 --set=…` | VERIFIED, 11 files — the park is exported |
| the seam constant, re-derived read-only from `out/stage90/xnu_arm_entry_seam.dis` | the `bl <__wrap_FlushPoU_Dcache>` at **`0x800492d8`**, returning to **`0x800492dc`** = `entry_trace.c`'s `STAGE90_XNU_SEAM_LR` — **did not move** |
| `src/entry/build_entry.sh` (the rung-16 build, before this document) | entry bin `c9738417…`, 5,552,764 B |
| `./scripts/build.sh` with `STAGE90_XNU_ENTRY_ALLOW=1 STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1'` | payload built; `stage90-build-config.txt` **byte-identical** to the pressed arm's (`6c2b6038…`) |

**The build was run for this arm — the entry image once and the payload once, each with the pressed
arm's own switch set read out of the record, and with the two configs compared afterwards.** The pressed
arm was parked and exported before either. `./scripts/build.sh` does not reproduce the payload (408), so
this is a disclosure and not a routine step.

The arm is **ARMED, NOT PRESSED**: `out/` holds `armed-storage-quiet-c9738417` and nothing is owed. The
press is the operator's, and it is spent only against a green readiness print whose flags and
`--expect-arm=` come from that print's own header.
