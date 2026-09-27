# 752: 747 §5's **branch 2** has the same defect as branch 1 — and its two producers are a command *with* an `INDEX` bit and a command *without* one

**Read out of the archived captures. No device action of any kind** — no `fastboot`, no `adb`, no press,
no gate, no runner, no build. Nothing under `src/` or `out/` is touched and `armed-storage-46fe6737` sits
exactly as it was left: **ARMED AND NOT PRESSED.**

**The one-line finding.** 749 fixed 747 §5's **branch 1** (its condition was satisfied by two shapes).
**Branch 2 has the same defect and its two producers are worse**: `_nidx_inhibit_seen > 0` with
`_nidx_complete = 0` is satisfied by

- **rung 19's CMD3-R1** (`0x031A`) — `_inhibit_last = 0x01f80001`, the inhibit bit **still SET** at the last
  of 1024 samples: taken and **stuck**; and
- **rung 13's and rung 14's CMD0** (`0x0000`) — `_inhibit_last = 0x01f80000`, the bit **CLEAR**:
  **taken, released, and nothing latched** — and `0x0000` is a word with **no response demand and no
  `INDEX` bit at all**.

So a rung-21 log landing in branch 2 would print *"the rule's `INDEX` half is KILLED — `0x031A` and
`0x030A` behave alike"* about a reading **whose only archived producers include a command that carries
neither of the bits the branch is about.** The discriminator is **bit 0 of `_nidx_inhibit_last`**, already
published, and it is the second time in two experiments that a bit sitting unused inside an
already-printed word was the whole of the missing reading (bit 24 in 749).

## 0. What this is, and the tool that found it

749's repair was prose. 751 made it a check — `tools/read_storage_commands.py` prints each row's **shape**
and refuses the verdict for an ambiguous one. This document is what that tool found when it was pointed at
**the whole archive instead of one capture**: a `--branch-audit` mode that reads *N* captures and reports,
for each of 747 §5's three branches, **how many distinct producer shapes satisfy its condition**.

    $ python3 tools/read_storage_commands.py --branch-audit out/stage90/captures/*last_kmsg.txt ...
    ============================================================================
    747 section 5's branch table, audited against 29 capture(s)
    ============================================================================

      B1  condition: the block DECLINED the command - the rule is CONFIRMED
          *** 2 DIFFERENT SHAPES SATISFY IT, AND THE TABLE GIVES THEM ONE VERDICT ***
            - seen=0, CMD line MOVED (the CMD1 shape)      rung15/17/18:cmd1
            - seen=0, CMD line NEVER MOVED (the CMD2 shape) rung17/18/19:cid

      B2  condition: the block TOOK it and it never finished - the rule's INDEX half is KILLED
          *** 2 DIFFERENT SHAPES SATISFY IT, AND THE TABLE GIVES THEM ONE VERDICT ***
            - seen>0, complete=0, RELEASED with nothing latched (inhibit bit CLEAR)
                                                            rung13:cmd0   rung14:cmd0
            - seen>0, complete=0, STILL STUCK (inhibit bit SET at the last sample)
                                                            rung19:rca

      B3  condition: the word STARTED and COMPLETED - the rule is killed outright
          ONE shape satisfies it: seen>0, COMPLETED (inhibit bit clear, completion latched)

      RESULT: AT LEAST ONE BRANCH CONDITION HAS MORE THAN ONE PRODUCER - see above

**B1's two producers are 749's finding**, reproduced here by a check rather than by four hand searches —
which is the point of the mode. **B2's two are new**, and they are the substance of this document.
**B3 is clean**: one shape, six rows. The audit exits **1** when any branch has more than one producer.

## 1. The two producers of branch 2, in their own words

| | rung 13 `cmd0` | rung 14 `cmd0` | **rung 19 `rca` (CMD3-R1)** |
| --- | --- | --- | --- |
| `word` / `word_read` | `0x0000` / `0x0000` | `0x0000` / `0x0000` | `0x031A` / `0x031A` |
| `rsp_present` | `0` | `0` | `1` |
| `inhibit_after` | `1` | `1` | `1` |
| `inhibit_seen` | `0x21b` (539) | `0x219` (537) | `0x400` (1024 — **all of them**) |
| `inhibit_last` | `0x01f80000` — bit 0 **CLEAR** | `0x01f80000` — bit 0 **CLEAR** | `0x01f80001` — bit 0 **SET** |
| `polls` | `0x004da800` (5,089,280) | `0x004da400` (5,088,256) | `0x004da400` |

> **CORRECTED BY 778.** Both parenthesised decimals were wrong: `0x4DA800` is 5,089,280 (this row said
> 5,088,256) and `0x4DA400` is 5,088,256 (this row said 5,088,000, a number no capture in the archive
> produced). The row's point - three counts of the same size - is unmoved.
| `complete` | `0` | `0` | `0` |
| `status_any` | `0` | `0` | `0` |
| 747 §5 branch | **B2** | **B2** | **B2** |

**Both are branch 2, and they are not the same event.** rung 19's inhibit bit is set at the last sample of
all 1024 — the driver's own `sdhci_send_command` would still be waiting on it. rungs 13's and 14's bit is
**clear**, and it was seen set in 539 and 537 of the first 1024 samples: **the block raised
`CMD_INHIBIT`, held it for about 126 µs, released it, and latched nothing** — no completion, no error, not
one bit of the whole `INT_STATUS` register, across 5,088,256 polls and the arm's own 1.2 s bound.

**That is a different finding, and 746 §4b already has the vocabulary for it.** 746 wrote the third column
— *did the block take the command at all* — and named `inhibit_after` and `inhibit_seen` as the two cells
that answer it. **Branch 2 is about the third column's *second* half** (*how* it ended), and it needs one
more cell, which the arm already publishes.

## 2. Why a `CLEAR` bit 0 in branch 2 cannot be read as an `INDEX` fact

**Because the same shape was produced by a command that has neither an `INDEX` bit nor a response demand.**
`0x0000` is opcode 0 with flag word `0x00` — `RESP_NONE`, no `CRC`, no `INDEX` — and `rsp_present = 0` in
both logs. So:

- if rung 21 returns branch 2 with bit 0 **SET**, the run reproduces **rung 19's shape**, and 747's
  sentence (*"`0x031A` and `0x030A` behave alike"*) is the right reading;
- if rung 21 returns branch 2 with bit 0 **CLEAR**, the run reproduces **rung 13's and rung 14's shape** —
  and the ladder has already measured that shape on **a command with no `INDEX` bit**, so the reading
  cannot be about `INDEX` at all. Its subject is the one 726 named and 746 retired: **a block that runs a
  command to a released line and latches no completion**, which is 730's `INT_ENABLE` mask and not the flag
  word.

**And the three rungs make the confound unavoidable, because they differ in exactly one cell.**

| | word | `inhibit_seen` | `inhibit_last` | `complete` | `status_any` |
| --- | --- | --- | --- | --- | --- |
| rung 13 CMD0 | `0x0000` | `0x21b` | `0x01f80000` | **0** | **0** |
| rung 14 CMD0 | `0x0000` | `0x219` | `0x01f80000` | **0** | **0** |
| rung 15 CMD0 | `0x0000` | `0x219` | `0x01f80000` | **1** | **1** |

**The same word, the same `inhibit_last`, the same `inhibit_seen` on two of the three — and `complete`
differs by one.** Rung 13/14 are B2 and rung 15 is B3, **on an identical command word.** The ladder's own
record attributes that difference to the **`INT_ENABLE` window** — 730's press measured
`_int_status_after = 0x00000001` against `_int_status_before = 0x00000000` with one store between them,
and rung 15 is the arm whose record says the enable stood from before the send. **(That attribution is the
record's, carried here as the record states it; what this document measures is the three rows and the one
cell that separates them.)**

**So `complete` alone is a function of the window as well as of the command** — and a branch table that
reads `complete = 0` as a statement about the *command's flags* is reading a cell whose value the arm's own
`INT_ENABLE` window also decides.

## 3. The corrected reading of branch 2, and it is a refinement rather than a retraction

**Branch 2 is still a real branch and rung 19's member of it is still its centre.** What changes is that
its condition needs the third column, exactly as branch 1's did:

| reading | the shape | what it says |
| --- | --- | --- |
| `seen > 0`, `complete = 0`, **bit 0 of `inhibit_last` SET** | **STILL STUCK** — rung 19's CMD3-R1, 1024 of 1024 | the block took the word and held the CMD line for the whole bound. The rule's `INDEX` half is genuinely in question |
| `seen > 0`, `complete = 0`, **bit 0 CLEAR** | **RELEASED, NOTHING LATCHED** — rungs 13's and 14's CMD0 | the block transmitted, released, and latched nothing. **This is the mask subject, not the flag-word one** — and it has been measured on a word with no `INDEX` bit |
| `seen > 0`, `complete = 1` | **COMPLETED** — branch 3, one shape | the rule is killed outright |

**All three cells are already published by the parked arm**, so this costs no rebuild and the arm stays
valid. The tool prints these three shapes for the `_nidx_*` family the moment the log exists.

**And the sentence this replaces is narrower than 747's**: *branch 2's condition cannot distinguish "the
block stalls on a word it accepted" from "the arm's own enable window was closed", and `inhibit_last` bit 0
is the cell that separates them — with the `CLEAR` member already measured on a command that carries
neither of the bits branch 2's verdict is about.*

## 4. Branch 3, checked and clean, with one note

**One shape satisfies `complete = 1`: six rows, all with `inhibit_seen > 0`.** No multi-producer defect.

**But all six are commands that ask for nothing back** — CMD0 five times (`0x0000`) and rung 20's
CMD3-no-response (`0x0300`) once. **So branch 3's verdict — *"a response-demanding command can finish on
this block"* — is correct as a new reading and has simply never been produced**: it is the branch rung 21
exists to reach. That is a note about coverage and not a defect, and it is recorded because a reader
comparing branch 3's one shape with branch 2's two should know the six rows behind it are all
no-response arms.

## 5. What this does and does not change about rung 21

**It does not move the arm, the payload, `out/`, the record, or the entry image.** No rebuild, no re-park,
no re-hash — nothing here is compiled, and the parked arm's identity is still `46fe6737…`.

**It changes the reading of two of the three branches.** A press whose log lands in branch 1 or branch 2 is
now read by **shape**, and the tool names the shape; branch 3 is unchanged. **749 did this for branch 1 and
this does it for branch 2, which is the branch 747 §5 attached the *rule is KILLED* verdict to** — the
branch whose misreading would retire the ladder's best explanation on the strength of a command that does
not carry the bit in question.

## 6. What this document does not say

- **It does not say branch 2's verdict is wrong.** Rung 19's member of it is a real, well-measured stuck
  command and the `INDEX` question is genuinely raised by it. What is wrong is the **merge**: two shapes,
  one name, for the third time in three experiments.
- **It does not say the `INT_ENABLE` attribution in §2 is this document's measurement.** It is the
  record's (730), carried in the record's own words, and §2 says so at the point it is used.
- **It does not say `complete = 0` is unreliable.** It is a faithful cell. It is a cell that **more than
  one producer** can set — which is the same distinction 746 §4b drew about `inhibit_seen`.
- **It does not arm, move, press, or rebuild anything.** `armed-storage-46fe6737` is **ARMED AND NOT
  PRESSED**, no firer is armed, `out/` is untouched, and **no press may be spent without the operator's
  authorization.**
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. It makes the next press's
  reading mechanical for the second of its three branches, and that is the whole of what it claims.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist; 「让os可以正常启动并且挂载存储」 is
not reached, so **TWRP-to-storage stays withheld.**
