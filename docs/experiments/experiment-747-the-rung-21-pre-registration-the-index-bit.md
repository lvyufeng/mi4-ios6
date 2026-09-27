# 747: the rung-21 pre-registration — the driver's own CMD3 with its response demand PUT BACK and its `INDEX` bit TAKEN OUT, one bit from each measured arm

**The arm is `armed-storage-46fe6737`** (`STAGE90_XNU_STORAGE_PROBE=20`, ordinal rung 21):
the driver's own CMD3 `SET_RELATIVE_ADDR`, opcode 3, argument `card->rca << 16` = `0x00010000`, sent
through rung 17's one-bit `INT_ENABLE 0x34` window with `SIGNAL_ENABLE 0x38` at zero — **the same
command as rungs 19 and 20 with ONE FLAG BIT MOVED**: `MMC_RSP_R1` (`0x15`) → `ST_MMC_RSP_R1_NOIDX`
(`0x05`, `MMC_RSP_R1` with `MMC_RSP_OPCODE` removed), so the command word goes `0x031A` → **`0x030A`**.

**It sits one bit from each arm of a pair that is already measured**, and that is the whole reason it
is worth a press: `0x030A` is rung 19's `0x031A` with the `INDEX` bit gone, and it is rung 20's
`0x0300` with the response demand put back.

## 0. Two spellings of this rung's number

The ladder counts the **value** of `STAGE90_XNU_STORAGE_PROBE`; this record's commit subjects and
pre-registrations count **ordinal arms**. The two agree through value 8 and diverge from value 9, and
the divergence is stated once and used everywhere below:

| value | ordinal rung | what it is |
| --- | --- | --- |
| `18` | 19 | CMD3 with `MMC_RSP_R1`, word `0x031A` — pressed 2026-09-26 18:09 (742) |
| `19` | 20 | CMD3 with `MMC_RSP_NONE`, word `0x0300` — pressed 2026-09-27 01:21 (746) |
| **`20`** | **21** | **CMD3 with `ST_MMC_RSP_R1_NOIDX`, word `0x030A` — armed, NOT pressed** |

So the switch the arm carries is **`STAGE90_XNU_STORAGE_PROBE=20`** and the rung it is called in this
record is **rung 21**. Both spellings are in the arm's own record entry.

## 1. What 746 answered, and the correction that narrowed it

746's press answered its own threeway with **branch 1**: `_nrsp_complete = 0x00000001` beside
`_nrsp_status_any = 0x00000001`, the command finished in **0.255 ms** where rung 19's burned the whole
1.2 s bound, and `_nrsp_inhibit_last = 0x01f80000` — **bit 0 clear**, where rung 19's was
`0x01f80001`, bit 0 **still set**. **The response demand was the stall**, for the one command it was
measured on.

**And 746 §4b then read the same capture a second time and split one outcome into three.** The ladder
had been calling two different behaviours by one name:

| command | word | flags | `inhibit_after` | `inhibit_seen` | outcome |
| --- | --- | --- | --- | --- | --- |
| CMD0 `GO_IDLE_STATE` | `0x0000` | `0x00` | **1** | `0x219` (537 of 538) | **started → completed** |
| CMD1 `SEND_OP_COND` | `0x0102` | `0x02` | **0** | **0** of 5,088,256 | **NEVER STARTED** |
| CMD2 `ALL_SEND_CID` | `0x0209` | `0x09` | (cell absent) | **0** of 5,088,000 | **NEVER STARTED** |
| CMD3 rung 19 | `0x031a` | `0x1A` | **1** | `0x400` (all 1024) | started → never finished |
| CMD3 rung 20 | `0x0300` | `0x00` | **1** | `0x21a` (538 of 539) | **started → completed** |

**`inhibit_seen = 0` is not "no inhibit was observed" — it is the block declining the command**, and
the sampler demonstrably catches this bit (CMD0's 537 in 538, against CMD1's 0 in 5,088,256). So the
reach of 746's headline is exactly: *the ladder cannot yet say a response demand makes the block
refuse a command, because two of the three response commands were never taken and the only
response-demanding command that WAS taken is CMD3.*

## 2. The rule this rung tests, and where it comes from

Five words are on the record and **one rule fits all five**:

    `0x0000`  started, completed     the ladder's first completion
    `0x0300`  started, completed     0.255 ms
    `0x031A`  started, never finished
    `0x0102`  NEVER STARTED
    `0x0209`  NEVER STARTED

**A word that asks for a response (`ST_MMC_RSP_PRESENT`) and carries no `INDEX` bit is DECLINED.**

`0x0300` is the only word here that asks for nothing, and it is the only *other* word that completes.
`0x031A` asks and carries `INDEX` — it is taken and then never finishes. `0x0102` and `0x0209` ask and
carry **no** `INDEX` — neither was ever started.

**746 §4b named this rule in the same breath as two one-constant moves that would test it, and this
rung is the second of those two moves:**

1. *send opcode 1 or 2 with `MMC_RSP_NONE`* — the same opcode as a command that was never taken, with
   the flag word changed to the value that demonstrably starts. **Refuted-in-advance by rung 20**:
   `0x0300` is exactly that shape on opcode 3, and it started. So *the flag word alone does not make
   the block refuse a command* — the rule's exclusion is already down to the `INDEX` bit;
2. *send CMD3 with `RESP_SHORT` alone* (word `0x0302`) — CMD1's flag shape on the opcode that
   demonstrably starts. **Not this rung.** It would test whether the *shape* is the refusal; `0x0302`
   also has no `INDEX`, so it is a second sample of the same rule rather than a different question.

**This rung is the narrower question the rule actually names**: take the ONE command this ladder has
ever driven to a start, put the response demand back, and remove the ONE bit whose absence is common
to both declarations. **`0x030A` is that word.**

## 3. What the rung is, and what it is not

| | |
| --- | --- |
| opcode | `MMC_SET_RELATIVE_ADDR` 3 (`mmc.h:32`) — unchanged from rungs 19 and 20 |
| argument | `card->rca << 16` with `card->rca = 1` (`mmc.c:1400`, used at `mmc.c:1409`) = **`0x00010000`** — unchanged |
| flags | **`ST_MMC_RSP_R1_NOIDX` = `ST_MMC_RSP_PRESENT \| ST_MMC_RSP_CRC` = `0x05`** — new |
| command word | **`0x030A`** — new |
| window | the same one-bit `INT_ENABLE 0x34` set immediately before the store and restored on ONE unconditional line after the publishes — unchanged |
| `SIGNAL_ENABLE 0x38` | **read and never written** — unchanged |
| the response block | **the WHOLE of it, `0x10`..`0x1c`, read at both moments** — new, and §4 says why |

**The command word is `_Static_assert`ed in the source** — `ST_SDHCI_CMD_WORD(ST_CMD_OP_SET_RELATIVE_ADDR,
ST_MMC_RSP_R1_NOIDX) == 0x030Au` and `ST_MMC_RSP_R1_NOIDX == (ST_MMC_RSP_R1 & ~ST_MMC_RSP_OPCODE)` —
because **a body's memory accesses and its `bl` sites carry no immediate**, so the build's clause can
assert the access set, the counts, the store set and the program order, and it cannot assert the value.
A perturbation that changed the flags back to `MMC_RSP_R1` **builds**, and it is cell 13 of §7.

**What it is not**: it is not a new register class, not a new store class, not a data phase, and not a
return to any register rungs 13/14/15 read. §6 is the contract as a set the build reads.

## 4. m756's repair, and it is READ-ONLY and DECLARED rather than smuggled

746 §4 records a defect and this rung carries its repair. **743 §7 wrote rung 20's three response
readings as *one arithmetic at three times, the driver's own
`(readl(RESPONSE + 0x1C) << 8) | readb(RESPONSE + 0x1B)`* — and it was not one.**

| cell | what it actually read |
| --- | --- |
| `_nrsp_resp_pre` | `(readl(+0x1C) << 8) \| readb(+0x1B)` — the **136-bit** derivation |
| `_nrsp_resp` | `readl(+0x10)` — **word 0**, inside `st_send_command` |
| `_nrsp_resp_post` | `(readl(+0x1C) << 8) \| readb(+0x1B)` — the 136-bit derivation again |

**So `0x40ff8080 → 0x00000000` compared word 0 against words 2 and 3**, and the two agreed at the
first moment only because CMD1's `0x40ff8080` was still sitting in word 0 while CMD2's leftover was
still sitting in words 2/3. The instance is **m756**, and 746 §4 says what it costs: *the ceremony
built for a three-time comparison compared two different words, and the number that came back agreed
with the middle one for a reason nobody chose.*

**This body's pair is `readl(RESPONSE 0x10)` at BOTH moments** — the same expression
`st_send_command` reads for `resp`, and the word a 48-bit response actually lives in. The three cells
are one arithmetic at last. **And the four raw words are published at the two moments this body
owns** (`_nidx_raw_pre0..3`, `_nidx_raw_post0..3`), word-3-first in rung 18's own order, so a reader
can check the claim rather than trust it — **plus `_nidx_resp_moved` and `_nidx_resp_is_arg`**, which
are the two questions 746 §4 left open as a comparison.

**What is deliberately NOT done.** The middle moment's four raw words are **not** published, because
that reading lives in `st_send_command`, **the one body every command in this ladder shares**, and
putting four new offsets into it is a change this arm does not need to make its own point. That is a
stated omission and not an oversight: it is the difference between repairing this rung's own pair and
re-instrumenting the ladder's shared command path.

**And the R1 decode is back, for the reason rung 20 left it out.** Rung 20 asks for no response, so
decoding the register as its own answer would be 738's stale-word trap. **This command DOES ask for a
48-bit response**, so `_nidx_resp` is this command's to read by the driver's own condition —
`_nidx_state`, `_nidx_ready` and `_nidx_illegal` are published beside the raw word, with rung 19's own
caveat: a field is a decode of whatever the register holds, and **whether the card answered is a
separate question this arm also asks** (`_nidx_complete`, `_nidx_inhibit_last`).

## 5. The answer is a THREEWAY, and each branch names the next act

| the reading | what it says | what the next act is |
| --- | --- | --- |
| **`_nidx_inhibit_seen = 0`** with `_nidx_word_read = 0x030A` | the block **declined** the command. **The rule is CONFIRMED**, and it explains CMD1 and CMD2 in the same stroke | the subject becomes *why a response-demanding word with no `INDEX` is refused* — and the ladder's first question about the block's own sequencer rather than about a flag word |
| **`_nidx_inhibit_seen > 0`** with `_nidx_complete = 0` | the block took it and it never finished. **The rule's `INDEX` half is KILLED** — `0x031A` and `0x030A` behave alike | the subject is the response itself, and rung 20's exoneration of the demand is withdrawn for want of a second sample |
| **`_nidx_complete = 1`** | the word started **and** completed. **The rule is killed outright** | a response-demanding command can finish on this block, and the subject becomes what is different about rung 19's `INDEX`-bearing word |

**And the response cells discriminate the two "never finished" readings from each other**, which is
what the four new raw words are for: `_nidx_resp_moved = 0` with `_nidx_resp_read = 1` is a **reading**
of an unchanged block where rung 19's `_rca_resp_post = 0` was a reading of a word that moved, and
`_nidx_resp_is_arg = 1` says the register holds the argument the driver wrote — the one value this
rung knows the block was given.

## 6. The safety contract, as a set a build can read

**Two stores, both to `INT_ENABLE 0x34`**: the window's set and its one unconditional restore.
**`SIGNAL_ENABLE 0x38` is read and NEVER written**, which is what keeps this block's SPI 123 → intid
155 off a line nobody here owns. **No data path, no `POWER_CONTROL 0x29`, no GCC word, no `core_mem`
word, no `INT_STATUS 0x30` write and no byte of the medium.**

**The failure mode is a diagnosis and not a lost device.** The 1.2 s bound is inside
`st_send_command`, the ending is unmoved (§8), and **a delivery on this block's shared SPI 123 line
ends the run at the dispatcher as `_irq_other_count = 1` / `iat = 155` — an ending this image already
reads and survives** (709 ended that way on intid 170). So the worst outcome of a press here is a log
with one more key in it, not a device that does not come back.

## 7. What the arm's own build holds it to, and the falsifications

**The clause is `xnu_entry_746` in `src/entry/build_entry.sh`, and it is guarded `-ge 20`** — the same
family as the clauses below it, and the reason rung 20's own clause is guarded `-eq 19` and not
`-ge 19`. **That narrowing is not a tidy-up: the first build of rung 21 refused with
`FAIL: st_cmd3_noresp is not in the linked image while STAGE90_XNU_STORAGE_PROBE=20`**, because a
`-ge 19` guard demands rung 20's symbol in a rung-21 image. **A clause guarded `-ge` for a body the
source compiles for one value is a clause that refuses a correct arm one rung later.**

What the clause asserts, and what each assertion is for:

| the assertion | why it is a refusal rather than a sentence |
| --- | --- |
| `st_cmd3_noidx` **exists**, and its **size** is readable | an unreferenced static is dropped, and an image without the symbol and a census of zero are **one build** — every `_nidx_*` cell would publish as absent while the record claims the rung (m720's shape). **And it is a SECOND net, which was measured rather than assumed**: the cell that removes the call site never reaches this check — the build refuses one stage earlier, `error: 'st_cmd3_noidx' defined but not used [-Werror=unused-function]`. The refusal message said "three ways to get here" and named the removed call site as one of them; that was wrong and has been corrected in the source |
| the **access set** is exactly `f9824910:ldr f9824914:ldr f9824918:ldr f982491c:ldr f9824924:ldr f9824930:ldr f9824934:ldr f9824934:str f9824938:ldr` | a set that differs is either a new register class this rung never declared or a reading it promised and does not take |
| the **counts** are exactly `f9824910:ldr=2 f9824914:ldr=2 f9824918:ldr=2 f982491c:ldr=2 f9824924:ldr=1 f9824930:ldr=1 f9824934:ldr=2 f9824934:str=2 f9824938:ldr=1` | **and the counts ARE m756's repair as a number**: every one of the four `RESPONSE` words is read **twice**, once before the window and once after the command, which is what makes the freshness pair one arithmetic of the whole block instead of a comparison of two derivations |
| the **stores** are exactly `f9824934:str`, **two** of them | the rung's safety contract as a set: a store to `0x38` would put SPI 123 on the wire, a write-1-to-clear into `0x30` would clear the very bit a poll reads, and a store anywhere else is an undeclared class |
| the **program order** (distinct accesses) is `f982491c:ldr f9824918:ldr f9824914:ldr f9824910:ldr f9824934:str f9824934:ldr f9824938:ldr f9824930:ldr f9824924:ldr` | the four `RESPONSE` words word-3-first, then the window's store and its two readbacks, then the tail — **and nothing more than that: it is first-appearance order of DISTINCT keys, so a re-ordering of an access that REPEATS is invisible to it** (m702's shape). §7b is the clause that closes that hole, and the defect was found by a cell building rather than by reading |
| **the SPLIT**: the body is cut at its own `bl <st_send_command>` and **two** access sets are asserted | the half **before** the command must be exactly `f982491c:ldr f9824918:ldr f9824914:ldr f9824910:ldr f9824934:str f9824934:ldr f9824938:ldr`, and what comes **after** it exactly `f982491c:ldr f9824918:ldr f9824914:ldr f9824910:ldr f9824934:str f9824930:ldr f9824934:ldr f9824924:ldr` — so the baseline is before the store **and** the restore is the tail's first store, ahead of `INT_STATUS 0x30` and `PRESENT_STATE 0x24`. This is the mechanism 732's clause already uses on `st_cmd_path` |
| the **image side is EMPTY** | twelve device reads into locals and two stores to a register, with no image word written by this body |
| the **four command call sites are `[2, 1, 1]`** (`st_cmd_path`, `st_all_send_cid`, this body) | this body sends ONE command, so a fourth site anywhere is a command this arm did not declare |
| `st_cmd_path` calls it **once**, **after** `st_all_send_cid` and **after** its last `st_send_command` | the gate is the value `st_all_send_cid` **returns**, so a call above it makes the branch — and the command — provably dead; and this command is the LAST on the bus, which is the driver's own order in `mmc_attach_mmc` |
| **`st_cmd3_noresp` is ABSENT** and **`st_set_relative_addr` is ABSENT** | the same structural claim rung 20's clause makes one rung down: the three bodies send the SAME opcode with the SAME argument and DIFFERENT flag words, so an image carrying two of the three puts two variants of one command on one bus in one boot and neither reading is attributable to its own flag word. **This clause is also what makes the `-eq 19` narrowing above a property rather than a spelling**: at rung 21 that clause is not run, and this one refuses the arm it was protecting against |

## 7b. The hole this clause had, and how it was found: a cell BUILT

**The first version of the ORDER assertion claimed more than it proved, and the falsification battery
is what said so.** It read:

> the order is where this rung's two structural claims live: the FOUR-WORD BASELINE … is read BEFORE
> the window's store … and the window's restore (the second `f9824934:str`) comes BEFORE the readbacks
> that follow it, so `_nidx_readback` and `_nidx_ps_after` are taken with the enable closed

**The second half of that is not something that assertion can see.** `classify_body`'s first field is
the **first appearance of each distinct key**, so a re-ordering of an access that *repeats* is
invisible to it — and this rung repeats twice over: the four `RESPONSE` words are read at both
moments, and `INT_ENABLE 0x34` is stored at two. Moving the restore below the readbacks leaves the
set, the counts and the distinct order **all identical**. `m702`'s shape, and no arrangement of that
one line fixes it.

**It was found by measurement, not by reading.** Cell 7 of the first battery moved the restore to the
last line of the body, and the build came back **`rc=0 BUILT-OK`** where the cell expected a refusal.
Rungs 15's, 16's and 19's clauses already state this limit about themselves in as many words — *"the
ORDER and the VALUES of the two are not asserted here … a clause that said the order WAS asserted
while the reader cannot see it would be m702's shape"* — and rung 21's did not, which is exactly the
mistake those three sentences exist to prevent.

**The repair is the SPLIT, and it reuses a mechanism that is already in this build**: 732's clause on
`st_cmd_path` cuts a body at its first `bl` and classifies each half, because a store cannot be found
by matching an offset (`INT_ENABLE 0x34` is reached as `[r4, #2356]`, a fact about the compiler).
**The pair of slices here is deliberately not head/tail**: `classify_body` resolves a base register
only from a `movw`/`movt` it has itself seen, so a bare tail half would report every one of its
accesses as `UNK:` — the materialisation is in the head. So the first slice is the head alone, and the
second is the **whole body with the head's device accesses deleted**, which removes everything before
the command and leaves the head's `movw`/`movt` in place.

| slice | the assertion | what it is for |
| --- | --- | --- |
| **HEAD** — lines up to and including the `bl` | exactly `f982491c:ldr f9824918:ldr f9824914:ldr f9824910:ldr f9824934:str f9824934:ldr f9824938:ldr` | **seven** accesses before the command, and not four: the baseline, then the store, then the register read back, then `SIGNAL_ENABLE 0x38` |
| **STRIPPED** — the whole body with the head's device accesses removed | exactly `f982491c:ldr f9824918:ldr f9824914:ldr f9824910:ldr f9824934:str f9824930:ldr f9824934:ldr f9824924:ldr` | the tail's accesses, where the restore is the **first** store to appear — so it precedes `INT_STATUS 0x30` and `PRESENT_STATE 0x24`, which is the condition `_nidx_readback` and `_nidx_ps_after` are read through |

**And the scan is asserted before it is believed** (732's rule, and the first draft of this clause
ignored it): the split point and both sets are checked non-empty before either is compared, because
`awk` on a body that does not name the call prints nothing, and "the head has no device accesses"
would then read as agreement with a want-list that happened to be empty.

**Two refusal messages in this clause were also corrected, and both were over-claims of the same
kind** — a claim about what refuses a build, where a measurement says something else does:

| the message said | the measurement |
| --- | --- |
| "**three** ways to get here: renamed, INLINED …, or **its call site removed**" | **two.** The call site's removal refuses one stage **earlier**, at the compiler: `error: 'st_cmd3_noidx' defined but not used [-Werror=unused-function]` — the body is `static` and the build treats warnings as errors. The check is a second net behind the compiler |
| the exclusion clause "**is what refuses** the arm it was protecting against" | **false as the tree stands.** Both exclusion cells — including one that widened **all four** of rung 20's guards to `>= 19` — were refused by the **seam identification** (`the exit's call to FlushPoU_Dcache … returns to 2147787484, while entry_trace.c's STAGE90_XNU_SEAM_LR is 0x800492dc`), because a body added to this translation unit moves the exit path and that check runs first. The exclusion clause is a net behind a net whose outer layer is an **address constant**, and it is worth having for the arm whose seam constant was re-derived — not for the one the cell built |

**Everything a battery can and cannot reach is therefore stated rather than implied**, and the two
messages now say which net is the first one.

**The falsification battery** is `/tmp/vprfix/falsify21.sh` (revision 2) with
`/tmp/vprfix/falsify21_extra.sh` for the two extra cells, driven against
`/tmp/vprfix/entry_storage.r21armed.c` (the armed source, sha `38fb069c…`), **every anchor checked
unique against that file before the first cell** (m748), and every cell naming the clause it expects
to fire. **A cell that BUILDS is reported as `BUILT-OK` and is the honest note, not a claim that a
clause fired.** The results are in §8, with the source restored between cells.

**Two process defects the battery found in itself, both worth carrying forward.** Revision 1's cells
11, 12 and 15 never ran: their multi-line arguments were **single-quoted and contained an
apostrophe** (`rung 20's`), so the shell ended the string and tried to execute the next word. And
**`build_entry.sh` was edited while a build of it was running**, which corrupted that build
(`./build_entry.sh: line 32839: s: command not found`, and a clause printing text from the revision
before) — **bash re-reads a script file as it executes**, so the clause was frozen before revision 2
was started.

## 8. Host-side work, and what verified it

**No device action of any kind.** No `fastboot`, no `adb` to the device, no press, nothing flashed.

The entry image is built by `src/entry/build_entry.sh` with the arm's own switch set on the command
line — `STAGE90_XNU_STORAGE_PROBE=20` and the fifteen other switches the arm inherits unchanged — and
the build returns **rc=0 with 0 `FAIL:` lines**. What it produced:

| | |
| --- | --- |
| `out/stage90/xnu_arm_entry.bin` | **5,552,764 B**, sha256 **`46fe673799d8150594e428b76698639e24221e040058cc9814c6b10ead0f14ad`** — and the eight hex of the arm's name is this prefix |
| the seam constant | **UNMOVED**: `STAGE90_XNU_SEAM_LR (0x800492dc` — the exit's `bl FlushPoU_Dcache` still returns to the same instruction, so the arm is identified by the same constant every arm in this family has been |
| `st_cmd3_noidx` | at **`0x8000df54`**, size **`0x338`** |
| `st_cmd3_noresp` / `st_set_relative_addr` | **0 occurrences each** — both exclusions hold in the bytes |
| the keys | **44** `xnu_live_storage_nidx_*` strings in the body plus the two `_nidx_gated*` cells in `st_cmd_path` = **46** in the image |
| the encoded command | the disassembly carries `mov r2, #5` (flags `0x05` = `PRESENT\|CRC`), `mov r1, #65536` (argument `0x00010000`), `mov r0, #3`, `bl 8000cff8 <st_send_command>` |

**And the four raw baseline reads resolve to the four `RESPONSE` offsets this rung declares**, in the
order §7 asserts: `[r4, #2332/2328/2324/2320]` = `0xF9824A1C / 4A18 / 4A14 / 4A10` — word 3 first,
word 0 last, with the mirroring post-reads after the command. **`0xF9824A10` is `RESPONSE 0x10`**: the
word a 48-bit response lives in, and the word 743 §7's arithmetic never read.

**And the build's own clause prints the split it makes**: `SPLIT at its own st_send_command on line 71
so that the half before it must be exactly [f982491c:ldr f9824918:ldr f9824914:ldr f9824910:ldr
f9824934:str f9824934:ldr f9824938:ldr] and what follows it exactly [f982491c:ldr f9824918:ldr
f9824914:ldr f9824910:ldr f9824934:str f9824930:ldr f9824934:ldr f9824924:ldr]` — the §7b assertion,
read out of the bytes rather than off this page.

### 8b. The falsification battery, cell by cell

**Sixteen cells in `falsify21.sh` revision 2 plus two in `falsify21_extra.sh`.** Every cell names the
clause it expects and the first refusal is quoted. **`BUILT-OK` is the honest note and not a failure
of the battery** — a cell that builds is a limit of the check, and one of them (13) is a limit the
record already declares.

| # | the perturbation | the first refusal | the first net, when it is not this clause |
| --- | --- | --- | --- |
| 1 | a baseline word (0x14) read as a byte | **SET** — `f9824914:ldrb` | |
| 2 | the 0x18 baseline word not read at all | **COUNT** — `f9824918:ldr=1` | |
| 3 | the 0x14 post-half read moved onto 0x10 | **COUNT** — `f9824910:ldr=3 f9824914:ldr=1` | |
| 4 | the window is never written back | **COUNT** — `f9824934:str=1` | |
| 5 | a W1C store into `INT_STATUS 0x30` | **SET + STORES** — `f9824930:str` | |
| 6 | a store to `SIGNAL_ENABLE 0x38` | **SET + STORES** — `f9824938:str` | |
| 7 | **the restore moved below the readbacks** | **STRIPPED** — the tail is not the eight declared accesses | **it BUILT against revision 1 and that is §7b** |
| 8 | a fifth `st_send_command` call in the body | the four-call-site clause — `[2, 1, 2]` | |
| 9 | the call site duplicated in `st_cmd_path` | the once clause — 2 calls | |
| 10 | the call site removed from `st_cmd_path` | **the COMPILER** — `error: 'st_cmd3_noidx' defined but not used [-Werror=unused-function]` | the symbol-absent clause is a second net |
| 11 | rung 20's body **and** call site widened to `>= 19` | **the COMPILER** — `error: 'ST_MMC_RSP_NONE' undeclared` | the source's `#if == 19` guard on the constant |
| 11b | rung 20's **FOUR** guards widened to `>= 19` | **the seam identification** — the exit returns to `2147787484`, `STAGE90_XNU_SEAM_LR` is `0x800492dc` | the exclusion clause did not fire |
| 12 | rung 19's body and call site widened to `>= 18` | **the seam identification** | |
| 12b | rung 19's guards widened to `>= 18` | **the seam identification** | the exclusion clause did not fire |
| 13 | the flags changed back to `MMC_RSP_R1` | **`BUILT-OK`** | **declared, not a surprise**: a body's accesses and `bl` sites carry no immediate, so `0x030A` lives in two `_Static_assert`s and in the run's own `_nidx_op` / `_nidx_flags` / `_nidx_arg` / `_nidx_word` cells |
| 14 | the baseline back through the 136-bit formula (m756 restored verbatim) | **SET** — `f982491b:ldrb` appears | |
| 15 | a `POWER_CONTROL 0x29` read added | **SET** — `NODECL-f982-41:ldrb` | |
| 16 | the post-command baseline reads moved above the command | **COUNT** — `f9824910:ldr=3 …` | the cell expected the STRIPPED clause; the COUNT check is the earlier net, and the cell's expectation is the thing that was wrong |

**What the battery establishes, in one sentence**: of the eighteen cells, **twelve were refused by
`xnu_entry_746` itself** (cells 16's refusal is that clause's COUNT check, one net earlier than the
cell's expectation), **one built as declared, two were refused by the compiler and three by the seam
identification** — **and the three that reached a net other than their own are all recorded above
with the name of the net that fired**, because a battery that reported only "refused" would hide
exactly the thing §7b exists to say.

## 9. What the rung is not

- **It is not a step toward 「挂载存储」, and it does not claim to be.** One command more on this
  block's bus, and there is still no sector, no partition table and no mount. A completed CMD3 would
  still leave CMD9 `SEND_CSD`, CMD7 `SELECT_CARD` and a data phase at CMD17 before anything mounts.
- **It is not a storage driver.** The fixture's `/dev/rmd0` (`bsd/dev/memdev.c`) is still the only
  driver that answers a user call, exactly as 745 §3 records.
- **It does not predict a card answered.** `PRESENT_STATE 0x24` has read `0x01f80000` — bit 16
  (`SDHCI_CARD_PRESENT`) clear — at every one of the twelve moments 746's capture published it, **and
  `entry_storage.c:218` says in this arm's own source that the bit must NOT be read as "no card"
  here.** A bit that cannot be read either way is a bit that needs a second reading beside it, not a
  verdict — and this rung adds none.
- **It does not touch the ending.** `STAGE90_XNU_POST_END_TICKS=115200000` (6.0000 s) and the two
  ending switches are unchanged, so the run still ends at `entry_seam_end_run`'s own faulting store
  6.05 s in, exactly as 745 §2 measures. **Nothing here is aimed at 「正常启动」** — that is 745 §5's
  design question for the operator, and it is untouched.
- **It does not withdraw 746's headline.** The response demand was the stall for the ONE command it
  was measured on. This rung asks whether the rule that would generalise it is real, and §5 names
  what each answer costs.

## 10. The arm, and the press

**The arm is armed: `out/stage90/` holds `armed-storage-46fe6737`** — the entry image of §8, the
payload built with the switch record's own `STAGE90_XNU_ENTRY=1` (the value every parked arm carries,
which `tools/check_payload_config_entry.sh` derives from the parks rather than asserts), and the
`stage90-qcdt.img` the readiness chain hashes. **The press is NOT authorized and has not been made.**

When it is, the two commands are the two the readiness header prints, run **by hand, once each**:
**exactly one** `./scripts/preflight_boot_check.sh <printed flags>` and **exactly one**
`./scripts/run_and_capture.sh <printed flags> --expect-arm=<printed set>`, after
`tools/verify_press_ready.sh` first. `fastboot boot` only, **never flash**; the neighbour `33e80afe`
absent from BOTH device lists before firing; exit **0** returned+captured / **1** host-side / **2** did
not return / **3** a return `adb` missed. **No firer is armed**, and none is needed: the press path is
the two printed commands.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met — user mode
and the `memdev.c` fixture, per 745 — and the SDHCI storage driver does not exist. 「让os可以正常启动
并且挂载存储」 is not reached, so **TWRP-to-storage stays withheld.**
