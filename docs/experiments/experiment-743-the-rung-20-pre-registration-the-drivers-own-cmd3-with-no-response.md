# 743: the rung-20 pre-registration — the driver's own CMD3 with the response demand REMOVED, one constant different, and a register read three times

Host-side. **No device action of any kind**: no `fastboot`, no `adb` to the device, no press. The arm it
describes is **ARMED, NOT PRESSED**. The last press is 742's (2026-09-26 18:08:10–18:09:22 UTC, exit 0,
capture `rung19-rca-20260926-180922-last_kmsg.txt`, 630,486 B).

**The builds WERE run for this arm** — the entry image several times — and that is a disclosure and not a
routine step (`./scripts/build.sh` does not reproduce, 408). §7 is the account of the entry image's builds,
including the ones made deliberately wrong.

## 0. Two spellings of this rung's number

The ladder in `src/entry/entry_storage.c` counts a rung by the **value** of
`STAGE90_XNU_STORAGE_PROBE`, so this arm's `#error` clause reads `19 = 18 plus …`. The commit subjects and
the pre-registrations count **ordinal arms**, and the two part company at the ladder's value 9 (which has
two arms), so **this arm's ladder value is 19 and its ordinal is rung 20**. The record spells both. 741 §0
and 742 §0 are the same sentence for the two arms immediately below.

## 1. What 742 answered, and what it left

742 pressed rung 19 and returned a reading the ladder had never seen. Its `st_set_relative_addr` put the
driver's own CMD3 on the bus — opcode 3, argument `0x00010000`, flags R1, word `0x031A` — and every cell
came back in one shape:

    _rca_word = _rca_word_read = 0x031a     the block holds the word the driver wrote: ACCEPTED
    _rca_arg  = _rca_arg_wrote = 0x00010000  and the argument with it
    _rca_inhibit_seen = 0x400                CMD_INHIBIT, in ALL 1024 samples of the poll
    _rca_inhibit_last = 0x01f80001           bit 0 STILL SET when the 1.2 s bound ran out
    _rca_complete = 0  _rca_err = 0  _rca_timeout = 1   neither finished, nor failed, nor timed out
    _rca_status_any = 0                      nothing latched in INT_STATUS over 5,081,600 polls
    _rca_resp_pre = _rca_resp = 0x40ff8080   CMD1's word, still there inside the command
    _rca_resp_post = 0x00000000              and EMPTY after it
    _rca_resp_moved = 1

So the command was **taken** — the block held its own command line — and it **never finished**, and the
response register went from CMD1's leftover word to zero across it. 742 §1 states it in one sentence: *a
response-expecting command is accepted and it never completes, never times out and never errors*, and the
one command this ladder ever drove to a completion is CMD0, the one that asks for no response.

**What is left is a difference of exactly one constant.** §8 of 742 names the next rung and it is not a
plan to invent: *the same opcode and the same argument with the flag word carrying no response demand* —
`MMC_RSP_NONE` on opcode 3, word `0x0300`, argument `0x00010000` — through the same window, publishing the
same triplet. That is this rung, and its whole design is that the second half of the record's claim is
already written: the *word* changes and nothing else does.

## 2. What the rung is

**The whole of rung 19, plus the same command with its response demand removed.**

    #if STAGE90_XNU_STORAGE_PROBE >= 19
        if (cid_sent != 0u) {
            ST_LIVE("xnu_live_storage_nrsp_gated", 0u);
            st_cmd3_noresp(int_enable);
        } else {
            ST_LIVE("xnu_live_storage_nrsp_gated", 1u);
            ST_LIVE("xnu_live_storage_nrsp_gated_reason", 1u);
        }
    #endif

at the same call site in `st_cmd_path`, after rung 19's own call and inside the same `#if`.

| | rung 19 | **rung 20** |
| --- | --- | --- |
| opcode | `MMC_SET_RELATIVE_ADDR` 3 (`mmc.h:32`) | **3, unchanged** |
| argument | `card->rca << 16` = `0x00010000` | **`0x00010000`, unchanged** |
| flags | `MMC_RSP_R1` = `PRESENT\|CRC\|OPCODE` (`core.h:51`) | **`MMC_RSP_NONE` = `0u` (`core.h:50`)** |
| word | `0x031A` | **`0x0300`** |
| body | `st_set_relative_addr` | **`st_cmd3_noresp`** |
| gate | `cid_sent != 0u` (CMD2 was SENT) | **the same value, unchanged** |

**The two words differ by exactly the demand.** `MMC_RSP_NONE` is the absence of `MMC_RSP_PRESENT`, and
every other term `sdhci_cmd_to_flags` (`sdhci.c:1131-1143`) adds is tested on it, so `0x031A` loses
`RESP_SHORT`, `CRC` **and** `INDEX` together and becomes `0x0300`. The arm carries that as two
`_Static_assert`s rather than as this paragraph, and the pair is the reason the rung is worth a press: the
ladder's first `INDEX` bit does **not** survive it.

**Why rung 19's body is excluded from this image.** Both bodies send the same opcode with the same
argument; an image carrying both would put two response-demand variants of one command on the same bus in
one boot, and the two cells a press is spent for would answer each other's question. The source compiles
`st_set_relative_addr` for the value 18 and no other (`#if … == 18`, a widening of rung 19's own
`>= 18`), its call site the same, and `xnu_entry_743`'s exclusion clause makes that a property of the
**linked image** rather than a claim about the source. 737 is the same mechanism for rung 15's exclusion.

## 3. The answer is a THREEWAY, and that is what the pair buys

Rung 19's press left the ladder's oldest question smaller and sharper. This rung moves one variable and the
result is read three ways:

| the log | what it says |
| --- | --- |
| `_nrsp_complete = 1` with `_nrsp_status_any != 0` | **the response DEMAND was the stall.** The one command this ladder ever completed, CMD0, is the one that asks for nothing back — and then the subject is the response path: `RESPONSE 0x10`, `SDHCI_INT_RESPONSE`, and the `INT_ENABLE` bit this ladder has carried since rung 13 |
| `_nrsp_inhibit_seen > 0` with `_nrsp_complete = 0` | **the demand is EXONERATED.** A no-response command stalls the same way, so the subject is the opcode or the block's own state, and the census moves to `SLOT_INT_STATUS 0xFC` and `CORE_PWRCTL_STATUS 0xDC` — the two registers 726 §3 named and no rung has yet read at a moment when there was a completion to report |
| `_nrsp_inhibit_seen = 0` with `_nrsp_word_read = 0x0300` | **the block never started it.** The word was taken and the command line was never held |

**And the third reading is the response register.** The body reads it three times through the driver's own
word-0 derivation — `(readl(RESPONSE + 0x1C) << 8) | readb(RESPONSE + 0x1B)`, `sdhci.c:1164-1172` — before
the window, inside the command object's own `resp`, and after the publishes. With the demand removed
nothing should clear `RESPONSE 0x10` during the command, so `_nrsp_resp_post` should **still hold CMD1's
word `0x40ff8080`**, which rungs 17 and 18 both measured there. **If the R1 command emptied the register
and the no-response command does not, the CLEARING is tied to the response demand** — and that is 742 §1's
reading *confirmed* rather than inferred from a single press.

**`_nrsp_resp` must be read with care and the arm says so in a comment**: the command asked for no
response, so `rsp_present` is zero and that word is a reading of the **register**, not this command's
answer. That is exactly why the three R1-decode cells rung 19 publishes — `_state`, `_ready`, `_illegal` —
are **absent** from this body. **The R1 fields belong to rung 19's log and to no other**: a word decoded
here would be CMD1's OCR wearing a card-status shape, which is 738's stale-word trap in its cheapest form.

**One more thing the pair is armed to separate.** Rung 19's `_rca_resp_moved = 1` collapsed a three-way
outcome — *zeroed*, *replaced*, *unchanged* — into a boolean, and 742 §2 records that as a
pre-registration defect. This arm keeps the boolean **and** adds `_nrsp_resp_pre`, `_nrsp_resp` and
`_nrsp_resp_post` as three separate cells, so the same press cannot lose that distinction again.

## 4. The reading that is a CONTRAST, and it is the point

Every rung since 12 has published a per-command key block, `ST_CMD_PUBLISH`, and this rung's command is
published **through the driver's own object**: `st_send_command` fills the same `struct st_cmd_result` the
three commands below it fill, and the same macro prints it.

That gives the press a contrast no earlier rung could make, and 742 §4 already has the table it goes in:

| | CMD0 | CMD3 (rung 19) | **CMD3 (rung 20)** |
| --- | --- | --- | --- |
| response demand | none | `R1` | **none** |
| `_inhibit_seen` | 537 of 1024 | all 1024 | **?** |
| `_inhibit_last` bit 0 | clear | **set** | **?** |
| `_complete` | **1** | 0 | **?** |
| `_ticks` | 4,906 | 14,201,303 | **?** |

*The last row is `_cmd0_ticks = 0x132a` and `_rca_ticks = 0x015f92d7` from the two logs.* If the new row
looks like CMD0's, the demand is the stall; if it looks like rung 19's, the opcode is. **Two commands with
the same opcode, the same argument and one flag word between them, in two logs, is what makes the
difference attributable** — and it is why this rung is the same command rather than the driver's next
statement.

## 5. The safety contract

**Two stores, both to `INT_ENABLE 0x34`**, and the class of write is the one rungs 13, 14, 15, 17 and 19
already make:

1. the one-bit window `(read | SDHCI_INT_RESPONSE)`, taken **immediately before the command is put on the
   bus** — because 733's press measured that a command's status bit is latched only while its enable stands
   (CMD0 completed inside rung 14's window; CMD1, sent with it closed, was answered and never latched);
2. its restore, on **one unconditional line** immediately after the publishes. The value written back is
   the value the gate read, not a constant.

`SIGNAL_ENABLE 0x38` is **read and never written, at every rung**. It is the register whose conjunction
with `INT_ENABLE` raises this block's SPI 123 → intid 155, a line nobody here owns. No new command class
beyond CMD3, no data path, no `POWER_CONTROL 0x29`, no GCC word, no `core_mem` word, and **no byte of the
medium**.

**The restore is a PARTIAL one and that is a measurement, not a bug.** Bit 15 (`SDHCI_INT_ERROR`) rides
along with any write to `0x34` and no write clears it, so by this point the register reads `0x00008000`;
the expected cells are `_nrsp_ena_held = 0x00008001` for a store of `0x00000001` and `_nrsp_readback =
0x00008000` for the restore. 730, 733, 736, 738, 740 and 742 all measured that pair.

**The failure mode is a DIAGNOSIS and not a lost device.** The 1.2 s command bound is inside
`st_send_command`; a delivery on that line would end the run at the dispatcher as `_irq_other_count = 1` /
`iat = 155`, an ending this image already reads and survives (709 ended that way on intid 170).

**And rung 15's body is excluded as well as rung 19's.** `st_quiet_enable_probe` is compiled for the value
15 and no other, because its store above `st_cmd_path`'s gate leaves bit 15 set and refuses the whole
command path (736's press). **A log from this arm carrying any `_quiet_*` key, or any `_rca_*` key, is a
log from an arm that is not this one.**

## 6. What the arm's own build holds it to

`src/entry/build_entry.sh` gains a clause for `STORAGE_PROBE -ge 19`, `xnu_entry_743`, whose checks refuse
in this order. For `st_cmd3_noresp`:

- the **symbol** is present — an unreferenced static is dropped, and a build without it publishes every
  `_nrsp_*` cell as absent while the record claims the rung (m720's shape, with one of the three producers
  of an absent key guaranteed);
- it has a **size** in `nm -S`;
- its device access **SET** is exactly
  `f982491b:ldrb f982491c:ldr f9824924:ldr f9824930:ldr f9824934:ldr f9824934:str f9824938:ldr`;
- its **COUNTS** are exactly
  `f982491b:ldrb=2 f982491c:ldr=2 f9824924:ldr=1 f9824930:ldr=1 f9824934:ldr=2 f9824934:str=2 f9824938:ldr=1`;
- its **STORES** are exactly `f9824934:str` — two of them, both to the same address, and nothing else
  anywhere;
- its device accesses **IN PROGRAM ORDER, distinct** are
  `f982491c:ldr f982491b:ldrb f9824934:str f9824934:ldr f9824938:ldr f9824930:ldr f9824924:ldr` —
  **the freshness baseline first, before any store**, which is the half neither the set nor the counts can
  carry;
- its **image side is EMPTY**;
- `st_cmd_path` calls it **exactly once**;
- that call is ordered **AFTER `st_all_send_cid`** and **after the command path's last
  `bl <st_send_command>`**;
- the four `st_send_command` call sites are `[st_cmd_path 2, st_all_send_cid 1, st_cmd3_noresp 1]`;
- and the **exclusion**: `st_set_relative_addr` is **absent** from the linked image.

**The `_want` strings are rung 19's, character for character, and that is the one-variable claim made
structural rather than narrated.** If the flag word this rung changes had moved a single device access, a
width or an ordering, `xnu_entry_743` would refuse — so *the response demand is the only thing that
changed* is a property a build can falsify and not a sentence in this document.

**What the clause does NOT assert, said here so it is not mistaken for it**: the OPCODE, the ARGUMENT, the
FLAGS and the WORD. A body's memory accesses and its `bl` sites carry no immediate. What carries them is
the source's two `_Static_assert`s and **the run's own `_nrsp_op`, `_nrsp_flags`, `_nrsp_arg` and
`_nrsp_word` cells**. A perturbation that changed the flags back to `MMC_RSP_R1` would build, and this
record would then carry `_nrsp_flags = 0x00000015` beside a document that says `0` — a disagreement
between the log and the record, and not one this build can see. §7 cell 12 is that perturbation, run and
reported as a build.

## 7. The falsifications

Each perturbation was applied to a copy of the final source; every one rebuilt the entry image under the
arm's own switch set, and **every edit asserted `t.count(old) == 1` before it edited** — an anchor that is
not UNIQUE is a refusal here rather than a verdict about a clause. That assertion is m748, and the rung-19
batch had to be re-run for want of it.

| # | perturbation | verdict |
| --- | --- | --- |
| 1 | the `0x1b` byte read made a 32-bit `ldr` at `0x18` | **SET**, reporting `NODECL-f982-2328:ldr` |
| 2 | the post half taken from the pre half instead of the device | **COUNTS**, `f982491b:ldrb=1 f982491c:ldr=1` |
| 3 | the window is never written back | **COUNTS**, `f9824934:str=1` |
| 4 | a write-1-to-clear store into `INT_STATUS 0x30` | **SET**, `f9824930:str` |
| 5 | a store to `SIGNAL_ENABLE 0x38` | **SET**, `f9824938:str` |
| 6 | the baseline duplicated below the window's store | **COUNTS**, `f982491b:ldrb=3 f982491c:ldr=3` — see below |
| 7 | a fifth `st_send_command` call inside the body | the **four-call-site** clause, `[st_cmd_path 2, st_all_send_cid 1, st_cmd3_noresp 2]` |
| 8 | the call site duplicated | the **one-call-site** clause: *"st_cmd_path makes 2 call(s) to st_cmd3_noresp and rung 20 makes exactly one"* |
| 9 | the call site removed | the **compiler**: `entry_storage.c:3141:48: error: 'st_cmd3_noresp' defined but not used [-Werror=unused-function]` |
| 10 | a **live** call inserted above CMD2 | the **one-call-site** clause, `2` calls — see below |
| 11 | rung 19's body widened back to `>= 18` | **the SEAM clause**, `xnu_entry_535` — see below |
| 11b | the same, **with `STAGE90_XNU_SEAM_LR` set to the address cell 11 measured** | **THE EXCLUSION CLAUSE**: *"st_set_relative_addr IS in the linked image while `STAGE90_XNU_STORAGE_PROBE=19`"* |
| 12 | the flags changed back to `MMC_RSP_R1` | **NOTHING — it builds.** See below |

**Cell 6 is not the clause it was written for, and the reason is the perturbation rather than the clause.**
The edit put the baseline's derivation a second time below the window's store and **left the original in
place**, so the body read the pair three times per half instead of once — and the COUNTS clause, which
asserts `0x1b`/`0x1c` at `=2`, sits **before** it in the same clause and refused first. **The ORDER clause
was never reached.** What cell 6 does establish is the direction the clause exists for: the counts catch a
second reading before the ordering does. 741 §7's cell 7 is the same shape one rung down, and the fix for a
future batch is to make the move a move rather than an addition.

**Cell 10 is the same shape for the same reason.** The insertion added a second `st_cmd3_noresp` call
rather than relocating the gated one, so the **one-call-site** clause refused at `2` before the ORDER
clause could. A cell that *moved* the gated block above `st_all_send_cid` would be refused by the compiler
first (741 §7 cell 7: `cid_sent` becomes provably `0`, the branch and the call are folded away, and the
SYMBOL clause answers) — so **the ORDER clause is reached by no perturbation in this table either**, and
that is recorded rather than dressed up. It is not a decoration: `xnu_entry_743` runs it on the real image,
where it passes, and its subject is the one thing about this rung's position in `st_cmd_path` the source
guard does not say.

**Cell 11's verdict is a different clause, twenty-two hundred lines earlier, and both halves are the
finding.** Widening rung 19's guard back to `>= 18` puts a second CMD3 body into the image, and the entry
image's own layout moves with it: the exit's `bl <__wrap_FlushPoU_Dcache>` — the seam rung 9 and every
storage rung since has been built on — moved from **`0x800492d8`** to **`0x8004a2d8`**, exactly `0x1000`,
and `xnu_entry_535` refused because its return address no longer equals `entry_trace.c`'s
`STAGE90_XNU_SEAM_LR`. **That clause sits at `build_entry.sh:29102`; `xnu_entry_743`'s exclusion sits at
`:31351`.** So the exclusion clause cannot be the first thing to answer *this* perturbation, and it did not.
**Cell 11b exists because of that**: with the seam constant set to the address cell 11's own build printed,
the seam clause passes and the exclusion fires — *"st_set_relative_addr IS in the linked image while
STAGE90_XNU_STORAGE_PROBE=19… an image carrying both puts two variants of one command on the same bus in
one boot and each reading becomes unattributable to its own flag word."* **Two clauses guard this rung's
exclusion and the stronger one is the seam**, and the same measurement says something about the rung itself:
the pressed arm's entry image and this arm's are **the same size to the byte (5,552,764)** with the seam at
the same address, so rung 20's body occupies rung 19's bytes — which is what a one-constant change should
look like and is the reason `xnu_entry_535` passes at value 19 at all.

**Cell 9 is the control rather than the clause.** With the call site gone the body is an unreferenced
static and `-Werror=unused-function` stops the build before the symbol clause is reached; the symbol clause
stays reachable by a rename, so it is not dead — but a reader comparing this table to the mechanism should
know which of the two fired.

**Cell 12 is the honest note and it is the one cell that is not a refusal.** It is here because a table of
twelve cells in which every cell refused would be a claim the mechanism cannot support: the clause's own
text names the opcode, argument, flags and word as things it does not assert, and a battery without such a
cell would be a battery testing its own expectation. **Its verdict is a build** — `rc=0`, no clause fired —
and the disagreement it creates lives between the run's log and this document, not in this build.

## 8. Host-side work, and what verified it

`tools/verify_press_ready.sh` gains the value-19 narration — an `elif [[ $wst == 19 ]]` arm and a
`rung_para 19` paragraph — and its own row-4 check reads the `rung_para N` call sites back out of the file
and refuses if a paragraph exists that nothing appends. **That row is this rung's first finding about this
file, and it fired twice before it passed.** The `rung_para 19` paragraph was in the file and the **arm**
branch was not, so the first readiness run refused **4 of 5** (no `is 743's STORAGE arm` sentence for the
rung the record carries, and every other row red behind the unresolved set), and after the branch was added
a second run refused **1 of 5** — the branch named the rung and not the **second naming key**,
`STAGE90_XNU_PWR_WAIT_TICKS`. Both refusals are the mechanism working: **a narration can be WRONG and still
non-empty, which is all that row used to ask** (698's defect, and the reason the check exists) — and both are
the kind of thing a press does not get to discover. `tools/rehearse_revert_set.sh` re-reads the record after the arming block
is appended. `make check` runs `tools/check_stage_paths.sh`, `tools/check_set_name_rule.sh` and — new with
this rung — **`tools/check_backtick_messages.sh`**.

**The check is this rung's own finding, promoted to a class (m752).** Inside a double-quoted shell string a
backtick is a **command substitution**, not punctuation, and `src/entry/build_entry.sh` carried nineteen
such lines: rung 18's `xnu_entry_739` clause printed `so 738's doubt (is  the card's or the block's?)`
where the sentence says `is 0x40ff8080 the card's or the block's?`, and rung 19's clause would have lost
`mmc.c:1409` the same way. **A report that succeeds with a word missing from it is the failure this check
exists to stop**, and it was found by rung 20's build — which is m730's mechanism, one file over.
The scanner skips heredoc bodies deliberately (a `<<'PY'` body is another language's text) and states that
blind spot in its own header rather than hiding it; it refuses when the tracked file list is empty or when
its own scanner prints no completion line, so a check that could not run cannot pass.

**And the sweep that repaired the class damaged one line that was never an instance of it (m754).** Twenty-two
lines in four files were escaped; one of them was inside a `<<PY` heredoc body in
`tools/host_resolve_entry_addr.sh`, where the text is **python** — and in python an unrecognised escape keeps
the backslash, so the "repair" turned a report that prints `` the `bl` `` into one that prints
``the \`bl\` ``. Measured directly (`python3 -c 'print("a \`b\`")'` prints `a \`b\``), reverted to HEAD, and
the checker **still passes with the line unescaped** — which is the proof that the edit was outside the
check's own subject. **A repair is correct in one language, and the checker's skip-list is the authority on
which lines are in scope.**

## 9. What the rung is not

- **It is not the driver's next statement.** `mmc.c` would say CMD9 `SEND_CSD` after CMD3. Arming a further
  response-expecting command on a block whose command line rung 19 held for 1.2 s would stack the same
  unknown on itself; this rung changes one constant inside a command the ladder has already driven.
- **It is not evidence about a card.** `_rca_ps_after`'s clear `CARD_PRESENT` bit (742 §5) is a property of
  what this image has done to the block, not a statement about the medium, and this arm reads the same
  register at the same place with the same caveat.
- **It does not read a byte of the medium.** No data path, no `core_mem` word, no LBA, no `EFI PART`.
- **It is not a repair.** Nothing is added to make the boot work that does not already work; both stores
  and all five reads are rung 19's, and the census is asserted equal to prove it.

## 10. The arm, and the press

**ARMED, NOT PRESSED.** `out/` holds `armed-storage-b7e5a18f` — `STAGE90_XNU_STORAGE_PROBE=19`, the eight
hex of the name being the entry bin's own sha256 prefix — and nothing is owed. The two commands carry the
`scripts/` prefix from the repo root, and `33e80afe` must be absent from **both** device lists before
firing:

    sudo -n adb devices
    ./scripts/preflight_boot_check.sh --allow-xnu-entry
    ./scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-b7e5a18f

**No firer is armed**, and the press path is those two commands run by hand once each — 730 §6's finding,
since the launcher's bytes are kept nowhere. **The press itself requires the operator's authorization and
this document does not carry it.**

**The measurements the record keeps.** Entry image `b7e5a18f35ebfa7a7f1d1b411bac405ecdbb7490875ebf17c6126f93be5efc5d`,
**5,552,764 B — the same size as the pressed arm's**, so the seam constant did not move:
`platform_cache_idle_exit` at `0x800492d4` (size `0x6c`), its `bl <__wrap_FlushPoU_Dcache>` at
`0x800492d8` returning to **`0x800492dc`** = `entry_trace.c:2688`'s `STAGE90_XNU_SEAM_LR`, re-derived
read-only from the fresh disassembly (four wrapped call sites in the image: `0x80048d08`, `0x80049284`,
`0x800492d8`, `0x800493bc`). `.text` **5,329,120**. `st_cmd3_noresp` at **`0x8000df54`, size `0x29c`**
(668 B) — against rung 19's `st_set_relative_addr` at `0x8000df60`, size `0x2d8` (728 B): the same body
with three published cells removed. `st_all_send_cid` at `0x8000dcb4`, size `0x2a0`; `st_cmd_path` at
`0x8000e1f0`, size `0x788`; `st_send_command` at `0x8000cff8`, size `0x314`; `st_resp_before` at
`0x8000d7c8`, size `0x17c`. **`st_set_relative_addr` is not in the image**, and
`tools/check_arm_variant_pair.sh` agrees the pair (`STAGE90_XNU_STORAGE_PROBE=19` against
`STAGE90_XNU_ENTRY 1`) is consistent.

| command | reading |
| --- | --- |
| `tools/verify_press_ready.sh` | **5 of 5, exit 0** — flags `--allow-xnu-entry`, `--expect-arm=armed-storage-b7e5a18f`; the value-19 arm paragraph and its consequence paragraph both ran in the narration |
| `tools/rehearse_revert_set.sh` | **38 ok / 0 failed** |
| `tools/rehearse_live_path.sh` | **39 ok / 0 failed, exit 0** — the runner's own state machine against stubs with `sudo`, `adb` and `fastboot` replaced first on `PATH` and all three asserted, so no device was touched |
| `tools/verify_revert_set.sh out/stage90/frozen/armed-storage-b7e5a18f --set=…` | VERIFIED, 11 files, 6 manifest-member checks |
| the same against `/mnt/data/mi4-ios6-export/armed-storage-b7e5a18f` | VERIFIED, 11 files — the park is exported |
| `tools/check_set_name_rule.sh` | exit 0 — 29 sets, the suffix is the entry bin's own sha256 prefix |
| `tools/check_arm_variant_pair.sh` | the pair agrees |
| `make check` | exit 0 — `check_stage_paths.sh`, `check_set_name_rule.sh`, **`check_backtick_messages.sh`** (`ok - 71 tracked shell file(s)`) |
| two clean entry builds of the final source | `b7e5a18f…` and 5,552,764 B, byte for byte |
| the payload's config against the pressed arm's | **byte-identical** (`6c2b6038…`, `cmp`-measured) |

**The builds, stated as the disclosure they are.** The entry image was built for the final source twice
(both `b7e5a18f…`, `cmp`-measured) and thirteen times more for the falsification battery, where every
perturbation except the deliberate one was refused. **The payload was built once, with the flag**, and its
config matched the pressed arm's on the first try — which is 741's mistake not repeating itself and *not* a
check that now exists: `tools/gate_flags_for_arm.sh` derives the **gate's** flags from the arm, and nothing
in the tree compares a fresh payload's own config against the arm's record line. **That one-line `cmp` in
`make check` is still owed and is still not in this commit.** The pressed arm's park
(`armed-storage-rca-c333d09c`) was already exported and was not touched; neither was any other park.

**The goal is still NOT met**: no storage, no filesystem, the OS not observed reaching userland, so
**TWRP-to-storage stays withheld**.
