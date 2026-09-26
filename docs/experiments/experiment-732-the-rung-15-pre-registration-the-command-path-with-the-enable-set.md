# 732: the rung-15 pre-registration — the command path with the enable standing, and the first build of this arm that its own predecessor's log refuted

Host-side. No device action was taken for anything in this document, and the arm it describes is
**ARMED, NOT PRESSED**: the last press is still 730's (2026-09-26 07:38:48–07:39:59 UTC, exit 0, 71 s).
This is the pre-registration for the arm that follows the **pressed** rung-14 arm
`armed-storage-completion-6f49e880`, written from 730 section 5's plan. The set it names is
`armed-storage-enable-953ad0f6` — those eight hex characters are `xnu_arm_entry.bin`'s own sha256
prefix, which is the naming rule every storage arm already in the record follows (measured: 16 of the 16
`armed-storage-*` sets that predate this one). The first name this arm was given broke that rule, and
section 3 says why the name is the entry image's and not the wrapped payload's.

## 0. Two spellings of every rung number, and why both are in this file

The ladder in `src/entry/entry_storage.c` counts a rung by the **value** of
`STAGE90_XNU_STORAGE_PROBE` — its clauses read `14 = 13 plus …`, so the value 13 is the arm 730 pressed.
The commit subjects and the other pre-registrations count **ordinal arms**: they call that same arm
rung 14. This document uses the ordinal in its title and in any sentence about *an arm*, and says "the
ladder's value N" when it means the switch; the record spells both. They part company at the ladder's
value 9, which has two arms (the second is the same wait with the mask off), so value 9 is rung 10 from
there on. **This arm's ladder value is 14 and its ordinal is rung 15**, and both names one arm.

## 1. What 730 measured, and the one thing it leaves open

730's press of `armed-storage-completion-6f49e880` answered the question 726 could only refute. The two
cells that matter are one store apart:

    _int_status_before = 0x00000000        (the ladder's value-13 window, before its INT_ENABLE store)
    _int_status_after  = 0x00000001        (the same register, after it)

Between those two readings that arm makes exactly one store, to `INT_ENABLE 0x34`, of
`SDHCI_INT_RESPONSE` — and `INT_ENABLE` read `0x00000000` both before and after that store on the arms
below it. So **the completion was in the block all along and the enable was the mask**: `_cmd_gated = 1`,
which 726 read as "this controller completes a command and does not set its interrupt-status register",
was the answer of a **masked poll** and not a fact about the block.

What it leaves open is the only question worth a press: **with the enable set across the command's own
send, does the completion path get past its own gate?** That arm could not ask it, because its store
came *after* CMD0 had already been sent and its poll had already given up — in a body that runs
immediately before the between-commands gate.

## 2. What the rung is

**The same two commands, the same three gates, the same census and the same bodies — plus ONE 32-bit
store, placed so that a command runs under it.**

    #if STAGE90_XNU_STORAGE_PROBE >= 14
        ST_LIVE("xnu_live_storage_ena_wrote", int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE);
        st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE);
        ST_LIVE("xnu_live_storage_ena_held",
                st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE));
        ST_LIVE("xnu_live_storage_ena_status_pre",
                st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS));
        ST_LIVE("xnu_live_storage_ena_host_version",
                (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_HOST_VERSION));
    #endif

It stands **immediately before `st_send_command(CMD0)`**, and the window closes on one unconditional
line immediately after CMD0's publishes:

    #if STAGE90_XNU_STORAGE_PROBE >= 14
        st_cmd_enable_restore(int_enable, 1u);
    #endif

**The position is the experiment and not a preference.** `st_send_command`'s completion poll is *inside*
CMD0 and it reads `INT_STATUS`; if the enable is written after the command, that poll runs masked. The
window therefore has **one entrance and one exit, both unconditional**, and every branch of the body —
the census's refusal and the register half of the gate above the window, the between-commands gate below
it — stands outside them. The enabled interval is exactly CMD0's own send and its poll.

**It closes before `st_int_report()` on purpose.** The value-13 body's `_int_status_before` is *defined*
as the baseline taken with both enables clear; leaving this window open across that body would silently
redefine a cell of the rung underneath while its name and its reader stayed the same.

`SIGNAL_ENABLE 0x38` is **still ZERO**, which is the one thing 730's press measured is safe: the
specification's path is `INT_STATUS → INT_ENABLE → SIGNAL_ENABLE → the line`, and enabling a status bit
while the signal bit is clear is what makes the completion *visible to the image* without making it
*raise the line*.

**Gate 1 is narrowed rather than removed.** Both reads happen at the top of the body, before anything is
written, and

    gate_kind = (sig_enable != 0u) ? 1u
              : (((int_enable & ~(uint32_t)ST_SDHCI_INT_RESPONSE) != 0u) ? 2u : 0u);

so a refusal is localised — it says *which register was already set* — instead of being one bit that two
different registers produced.

**Three reads 697 and 730 left owed come with it**: `HOST_VERSION 0xFE` as a halfword (`0xFE` is not
4-aligned, so no 32-bit access may reach it), `INT_STATUS 0x30` with the enable set and no command in
flight, and that same register read after the restore.

## 3. The first build of this arm, and how its predecessor's log refuted it without a press

**The first build stood the window after CMD0 instead of before it** — in the body that runs just before
the between-commands gate, which is where 728 section 1 put the *value-13* body and where a reader's eye
naturally leaves the next one. It was parked as `armed-storage-enable-306ecedb` and **it could not answer
its own question**:

- CMD0's poll is inside `st_send_command`, and it runs with `INT_ENABLE` still zero, so by 730's own
  measurement it reads nothing: `c0.complete == 0`.
- The between-commands gate is `if (c0.sent == 0u || c0.complete == 0u || c0.err != 0u)`, so it refuses,
  CMD1 is never sent, and **`_cmd_gated = 0` — the cell this arm's own pre-registration promised — was
  absent by construction**, while every other cell the build published read plausibly.

That is `[[mi4-silence-is-a-reading-only-if-success-is-silent]]`'s m720 shape: an absent key one of whose
producers is guaranteed. It cost nothing but a rebuild, because the measurement that refutes it is in
730's own archived log, and the press it would have spent is the press this document exists to protect.

**Two further cells of the first build were refuted from the same log.** 730 measured its own readback as
`_int_enable_held = 0x00008001` for a store of `0x00000001` and `_int_enable_readback = 0x00008000` for a
store of `0x00000000`, so **bit 15 (`SDHCI_INT_ERROR`) is set by a write to `0x34` and is not cleared by
one**: the restore is a restore of bit 0, `_ena_held` is expected `0x00008001` and `_ena_readback`
`0x00008000` — not the `0x00000001` / `0x00000000` the first cell table said. This is the cheaper half of
`[[mi4-one-value-two-definitions]]` the other way round: the previous press had *already* printed the
value, and a cell table written from the headline pair alone would have shipped a prediction its own
predecessor's log contradicts.

**And the build now refuses that class rather than describing it.** `st_cmd_path`'s disassembly is split
at its first `bl <st_send_command>` and each half is classified by the same `classify_body` the set
clauses use:

- the half **before** CMD0 must carry the enable's `str`, counted exactly once;
- the half **after** it must have `PRESENT_STATE 0x24` as its only own device access;
- the restore's call must fall between the two calls.

The split is by disassembly line, not by an offset pattern, because GCC materializes the block as
`movw/movt 0xf9824000` and reaches the register as `[r4, #2356]` — a fact about the compiler and not
about the arm. **The clause's own first draft matched `#52`, found zero matches, and died silently**:
`grep -c` in an assignment exits 1 on no match, and `set -e` then ends the build with no message at all
(`[[mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one]]`). Every list in the clause is now
counted before it is believed, and a list the clause cannot read is a refusal.

**The set was renamed before the press, and the rename is a measurement rather than a preference.**
The first draft named it `armed-storage-enable-73d4a8f3` — `stage90-qcdt.img`'s hash, the file
`fastboot boot` sends, which is the convention the one pre-ladder arm in the record
(`armed-selftest-wdog-ef0361a2`) happens to follow. Every `armed-storage-*` set names the **entry image**
instead, because the storage ladder is a change to the entry image and not to the payload: measured over
the record, **16 of the 16 storage arms that predate this one have their name's suffix equal to their own
`xnu_arm_entry.bin` sha256 prefix**, and the first draft was the only one that did not. Nothing had been
pressed and nothing had been committed, so the correction cost one `sed` over eleven record lines, one
`mv` of the park, one `cp -r` of it into `/mnt/data/mi4-ios6-export/` (an export this arm did not have
yet, and should have, since the park is about to be pressed), and this paragraph. **The first build's park was named
correctly** — `armed-storage-enable-306ecedb` is its own `xnu_arm_entry.bin`'s prefix, measured in the
export dir — so it is the *corrected* arm's name, and only that, which broke the rule. One consequence is
worth stating because it looks like a collision and is not: the superseded ladder payload parked beside
it carries the **same** entry bin (`953ad0f6`) as this arm, because the payload's `STAGE90_XNU_ENTRY`
switch does not reach the entry image — that directory's name is a description of what it is
(`superseded-ladder-payload-fca2d467-not-pressed`, from its own qcdt) and it is not a set in the record.
**The record's own `role=` sentence for `xnu_arm_entry.bin` already read "the 8 hex of this hash is the
set's name"** — so the rule was written
down, in the same block, as prose inside a string that no tool parses, and the name on the line beside it
broke it. That is `[[mi4-a-claim-in-a-comment-is-not-a-check]]` with the claim and its counter-example one
line apart, and **no check enforces the rule today**: the suffix's source is the one thing about a set
name that a reader has to be told.

## 4. The safety contract

**The write set is one `str` at one address in each of two bodies.** `f9824934:str` occurs exactly
**twice** in the linked image: once in `st_cmd_path` (the enable, and the build holds that half-window to
exactly one store AND to its place before the first `bl` to `st_send_command`) and once in
`st_cmd_enable_restore` (the restore, whose call count the build holds to exactly **one**). There is no
new command, no `POWER_CONTROL 0x29`, no GCC word, no `core_mem` word, no `INT_STATUS` write, and no byte
of the medium: CMD0 and CMD1 are `bc`/`bcr` commands with no data phase.

**The failure mode is a diagnosis, not a lost device.** If the block does raise `hc_irq` the delivery
reaches the dispatcher as an unknown line — `_irq_other_count = 1`, `_irq_other_iar = 155` — which this
ladder already reads and survives: 709's rung-7 press ended exactly that way on intid 170, and its log
came back with every cell it promised. `SIGNAL_ENABLE` at zero is why the delivery is improbable, and the
window's single unconditional exit is why a set enable cannot persist.

## 5. The cell table

| cell | expected | read against |
| --- | --- | --- |
| `_cmd_gate_kind` | `0` | the narrowed gate did not fire |
| `_ena_wrote` | `0x00000001` | the store's own value, derived from the gate's read |
| `_ena_held` | `0x00008001` | **730's own readback of a `0x1` store** — bit 15 rides along |
| `_ena_status_pre` | `0` | no command in flight: the bit is latched BY the completion, not held |
| `_ena_host_version` | ≠ 0, ≠ 0xffff | 697's owed reading |
| `_cmd0_complete` | `1` | **the enable was the mask** |
| `_cmd0_status_any` / `_cmd0_any_polls` | ≠ 0 / 1 or small | **when** the bit appeared |
| `_cmd_gated` | `0` | **the arm's own cell** — the command path got past its gate |
| `_cmd1_sent`, `_cmd1_*` | present | **the first CMD1 this line has ever issued** |
| `_ena_restore_seq` | `1` present | the window's one exit ran |
| `_ena_wrote_back` / `_ena_readback` | `0` / `0x00008000` | the restore cleared bit 0 and only bit 0 |
| `_ena_status_post` | `0` or `1` | **gated vs latched** — either is a reading |
| `_int_status_before` / `_int_status_after` | `0` / `0` | the value-13 pair, with the reason below |
| `_cmd1_resp` | a SHAPE | bit 30 set with a non-zero voltage window (bits 23:15); `0x40ff8080`/`0xc0ff8080` are the reference words |
| `_cmd1_err` | `0`, or `0x00010000` | `SDHCI_INT_TIMEOUT` is the **card** not answering; any other bit is the controller |

**`_int_status_after` is expected to move from 730's `1` to `0`, and the movement is the finding.**
730's `1` was the *uncleared* RESPONSE bit appearing when the enable was set after the command. In this
arm the enable is set *before* the command, so CMD0's own poll sees the bit and its write-1-to-clear
consumes it: read at the same place, the same register is 0. The pair is still worth having in the log,
because `_cmd0_status_any` beside it is where the bit went.

**The negative cells, named before the press.** `_cmd_gated = 1` with `_cmd_gate_kind = 2` says
`INT_ENABLE` was **already holding a bit other than RESPONSE** at the top of the body — a fact about the
block no earlier rung could see. `_cmd_gated = 1` with `_cmd_gate_kind = 0` means the gate fired for the
third reason (`c0.complete == 0`) *with the enable standing*: **the first evidence that the two enables
are ANDed somewhere the image cannot see**, and the answer this rung was built to be able to give.
`_cmd_gated = 1` with `_cmd_gate_kind = 1` contradicts 726 and 730, both of which read `SIGNAL_ENABLE` as
zero. And `_ena_held = 0x00000001` — the written value read back unchanged — would say the first build's
prediction was right and 730's readback was an artifact of its own window; either reading is a fact about
the block.

## 6. What the restructure cost, and why a `./build.sh` ran at all

**This arm exists only because the 2026-09-26 restructure's build path was repaired first.** The
constraint says `./build.sh` must not be run — it does not reproduce (408) and would overwrite the live
payload — so it was not run during the restructure (`0cc5245`..`829106e`), and four literals that had
resolved by virtue of *where the script sat* were left pointing at a directory the payload's sources had
left. Measured, one build at a time:

| file | the literal | why no check saw it |
| --- | --- | --- |
| `scripts/build.sh` | `SOURCES=(start.S vectors.S …)`, `-Wl,-T,linker.ld`, `--c-output macho_fixture.c`, four `-include stage90.h` | **no prefix at all** — `check_stage_paths.sh` looks for the old tree's spelling, and these names never had one |
| `scripts/xnu_link_proof.sh` | `SUPPORT_SRC=xnu_link_support.c`, `NOENTRY_SRC`, `xnu_link.ld` | the first post-move `./build.sh` died here, on `missing support xnu_link_support.c` |
| `scripts/xnu_object_subset_compile.sh` | `SHIM_SRC`, `-Ishims`, twelve `shims/…` entries | the same bare-name shape; its own failure mask recorded `0x00000018` and the log's `grep: No such file` line was the only trace |
| `scripts/xnu_compile_graph_scan.py` | `root = stage_dir.parent.parent` | **depth arithmetic, not a path**: the scan then reported `exists=0` for all 23 candidates, which reads exactly like an empty checkout (m702/m698) |

`build.sh` is repaired by **`cd "$SRC_DIR"`** with the four helper calls re-pointed at `$SCRIPT_DIR` — the
payload's names are relative to the source tree, and the honest way to say that is to run from it — and
the other three are anchored on `$SRC_DIR` explicitly. `tools/check_stage_paths.sh` gained the spelling
that would have caught them: a **bare** name that the restructure moved, with no prefix to grep for and no
`stages/stage` on the line.

**And the new spelling paid for itself on its first run**, in a file no build has reached since the move:
`scripts/xnu_arm_sweep.sh:94-101` carried eight glued include flags — `-Ishims`, `-Ishims/kern`,
`-Ishims/mach` and the five `-Ishims_arm…` equivalents — pointing at the directory the shims left. The
glued form needed its own scan, because in `-Ishims` the `I` is a word character and the lookbehind that
keeps `tools` from matching inside `mytools` also kept `shims` from being seen at all: **the first version
of the pass, built for exactly that literal, did not fire on it.** It is anchored now. Two files carry one
marker line each (`# check_stage_paths: bare-names-resolve-against=src`) saying their bare names are the
source tree's, and the check *verifies* the names rather than trusting the line.

**Cost, stated plainly:** this step ran `src/entry/build_entry.sh` and `./scripts/build.sh` — the entry
image rebuilt twice (once for the first build, once for the corrected one) and the payload once. The
pressed arm was parked and exported before either, the payload's own sources are unchanged, and the entry
image's byte count is unchanged, so the seam constant did not move; but the operator should know the
build was run.

## 7. What the rung is not

It is not a storage driver: `st_cmd_path` is two commands, no data phase, and no byte of the medium can
change. It is not a new rung of *act* — the store set is one offset wider than the value-13 arm's and
every other window is unchanged. It does not meet the goal: **「把基础驱动跑起来」/「起码要能进入操作系统」 is
still unmet**, so **TWRP-to-storage stays withheld** (the clause is conditioned on 「如果os已经能进去了的话」).
It is not a claim that CMD1 will complete: CMD1's own poll runs *after* the window has closed, so
`_cmd1_complete` is expected to be 0 and that reading is the next rung's question, not this one's. And it
is not the last rung: whatever `_cmd_gated` reads, the next step is the one this press names.
