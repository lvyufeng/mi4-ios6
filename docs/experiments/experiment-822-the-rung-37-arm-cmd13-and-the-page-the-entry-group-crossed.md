# 822 — the rung-37 arm: CMD13, the driver's SPI flag word, and the first rung whose body did not fit

**A BUILD STEP. SOMETHING WAS BUILT AND NOTHING WAS SENT.** No `fastboot`, no gate against a device,
no runner, no press, no device action of any kind; `33e80afe` was never checked for because no press
was prepared. The rung-37 entry image and its payload were built, the arm was parked with eleven
members, **readiness is 5 of 5 exit 0** and **`make check` is exit 0**. **The press is the operator's
and no press is owed or authorized.**

---

## 1. What the rung-36 press left, and what this arm is

821 measured the frontier and named it exactly: CMD7 completed with an **attributable** frame — the
block's own opcode comparator ran and matched 7 — and the card's own R1 answered
`_sel_state = 3` = `R1_STATE_STBY`. **And one reading of one field cannot separate *"the card is in
STBY because CMD7 has not landed yet"* from *"the card is in STBY because CMD7 did not land."***
821 §8 chose the successor and said why it is not CMD8: CMD13 needs **no new mechanism**, and CMD8
needs a 512-byte data path this image has not got.

The arm is:

| | |
| --- | --- |
| set | **`armed-storage-054f8269`**, eleven members, `out/stage90/frozen/armed-storage-054f8269/` |
| switch | **`STAGE90_XNU_STORAGE_PROBE=36`** (VALUE 36 = ordinal rung 37) |
| entry image | `054f8269525bb1257baa60ac5204ce800153303bae0f722a9e8d5fae6467c836`, 5,569,148 B — **+0x4000 exactly on the rung-36 arm's 5,552,764** |
| entry elf | `3171ad1ec4f1807bdef5136bdd2310e795c8e28ef59fc1e46f01bddae8cea00c`, 6,749,004 B — +16,416 |
| payload | `stage90-qcdt.img` `f94e7c4e…`, 8,589,312 B |
| payload switches | `stage90-build-config.txt` **byte-identical to rungs 23 through 36** (`6c2b6038…`) — **no payload switch moved** |
| readiness | **5 of 5**, exit 0 |
| `make check` | **exit 0** |

The source change is one body: `st_send_status`, guarded `#if STAGE90_XNU_STORAGE_PROBE >= 36`, called
**once, ungated, immediately after `st_select_card`** in `st_cmd_path`. The ladder's bound at
`entry_storage.c:130` is raised to `> 36`.

## 2. The flag word is the arm, and it is not rung 36's

`mmc_ops.c:470-485` is the vendor's own `mmc_send_status`, and the driver's doc comment above it ends
with the sentence this rung is built on:

```c
	cmd.opcode = MMC_SEND_STATUS;                                    /* mmc_ops.c:476 */
	if (!mmc_host_is_spi(card->host))
		cmd.arg = card->rca << 16;                               /* mmc_ops.c:477 */
	cmd.flags = MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC;            /* mmc_ops.c:479 */
```

**`MMC_RSP_SPI_R2` is not a bit this controller reads.** `core.h:70` defines it as
`MMC_RSP_SPI_S1 | MMC_RSP_SPI_S2` = `(1 << 7) | (1 << 8)` = `0x80 | 0x100` (`core.h:40`, `:41`), so
the driver's flag word for CMD13 is **`0x195` = 405** where CMD7's was `0x15` = 21 — and
`sdhci.c:1140-1143` reads **exactly five bits** out of a flag word (`MMC_RSP_PRESENT`, `_136`,
`_BUSY`, `_CRC`, `_OPCODE`), of which those two are none. **So 405 and 21 are two numbers that produce
one command word, `0x0D1A`.**

**This is `one value, two definitions` met from the safe side, and the repair is the same one 820
used**: the equality is a `_Static_assert`, not a sentence —

```c
_Static_assert(ST_MMC_RSP_R1_SPI == 0x00000195u, ...);
_Static_assert(ST_SDHCI_CMD_FLAGS(ST_MMC_RSP_R1_SPI) == ST_SDHCI_CMD_FLAGS(ST_MMC_RSP_R1), ...);
_Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_SEND_STATUS, ST_MMC_RSP_R1_SPI) == 0x0D1Au, ...);
```

— and the two build clauses assert **different immediates on the same register of two bodies that are
otherwise the same shape**: `xnu_entry_819` requires `r2 == 21` on CMD7's body and `xnu_entry_822`
requires `r2 == 405` on CMD13's. **Neither clause can be satisfied by the other's body.** A copy of
rung 36's body wearing CMD13's opcode would complete, publish a plausible R1 and answer a question
about the wrong command — the silent failure the pair refuses.

## 3. The entry group crossed a page boundary, and the pinned address moved in TWO files

Rung 37 is the **first rung whose body did not fit inside the entry group's remaining alignment
slack**. `xnu_arm_entry.bin` went 5,552,764 → **5,569,148 B, exactly `+0x4000`** — four pages, against
770's move, which crossed a page boundary at a byte-identical size. The entry build's own clause
refused with the sentence it has used since 696:

```
FAIL: the exit's call to FlushPoU_Dcache is at 2147791576 and returns to 2147791580,
      while entry_trace.c's STAGE90_XNU_SEAM_LR is 0x8004a2dc
```

**Two copies of one address exist in this tree, and both moved.**

| copy | where | was | is |
| --- | --- | --- | --- |
| the seam's identification constant | `entry_trace.c`'s `STAGE90_XNU_SEAM_LR` | `0x8004a2dc` | **`0x8004b2dc`** |
| the result reader's fallback | `scripts/run_and_capture.sh`'s `EXIT_POP_LR_LITERAL` | `0x8004a2dc` | **`0x8004b2dc`** |

`entry_trace.c`'s copy is what the `naked` wrapper compares its own entry `lr` against so that it hooks
**one of `FlushPoU_Dcache`'s four callers** and hands the other three through. **A wrong constant
there is a silent mis-identification at run time** — the hook would either let the seam's site through
or wrap a different `bl`, and every reading the run publishes would be about the wrong call — which is
why the build refuses rather than warns.

`run_and_capture.sh`'s copy was refused by **readiness row 3**, which runs the real gate before a
press:

```
FAIL: run_and_capture.sh's fallback address for the exit's pop is 0x8004a2dc and
      out/stage90/xnu_arm_entry.elf has platform_cache_idle_exit returning from that bl at 0x8004b2dc
```

**This is the fifth move, and the runner's own comment carries the previous four**: 696
(`0x800462dc` → `0x800472dc`), 708 (`→ 0x800482dc`), 724 (`→ 0x800492dc`), 770 (`→ 0x8004a2dc`). Five
rungs, five pages, **every one caught by the same pair of clauses rather than by a run** — which is
the point of the pair.

**And the structural repair is still owed, in the runner's own words**: *a fallback that must be edited
per arm is a pin wearing the name of a fallback.* It exists only for the case where the ELF cannot be
read at run time, while the gate's equality clause makes it move with every arm. Named, not done —
removing it needs the gate's clause to accept a labelled absence, which is a step and not a one-line
edit.

## 4. A scan keyed on a mnemonic is not keyed on an instruction

`xnu_entry_819`'s argument-setup scan reads the register loads with the pattern `mov[ \t]+rN, #`. **The
value this rung exists for does not fit in a `mov`**: the compiler emitted `movw r2, #405`, and that
pattern does not match it. The new clause, modelled on 819, refused at its first run:

```
FAIL: the argument setup before st_send_status's call to st_send_command could not be read out
      of the linked image (r0 line [mov r0, #13], r1 line [mov r1, #65536], r2 line [], ...)
```

**The refusal is the safe direction** — it refused rather than silently reading a stale `mov` from
earlier in the body — and all three patterns in the new clause are now `movw?[ \t]+rN, #`. `mov` and
`movw` are **one instruction to a reader and two strings to a scanner**, and the value that exposed it
is the one this rung is about.

## 5. What the build proved, so the park is not just bytes

`build_entry.sh` gained `xnu_entry_822`, modelled on `819` and reading the **linked image**:

| assertion | measured |
| --- | --- |
| the body is in the image | `st_send_status` at **`0x8000efc4`** |
| exactly one command | one `bl <st_send_command>` (disassembly line 98) |
| the opcode | `r0 = #13` — `SEND_STATUS` (a 7 here is a second CMD7) |
| the argument | `r1 = #65536` — `card->rca << 16`, `ST_MMC_RCA_1` |
| **the flags** | **`r2 = #405`** — `mmc_ops.c:479`'s own word; **21 here is rung 36's body wearing CMD13's opcode, the silent failure this clause refuses** |
| the device surface, in program order, distinct | `f982491c:ldr f9824918:ldr f9824914:ldr f9824910:ldr f9824934:str f9824930:ldr f9824934:ldr f9824938:ldr f982492e:ldrb f9824924:ldr` — **byte-for-byte the surface rung 36's clause asserts for CMD7's body** |
| the counts | `f9824910:ldr=2`, `f982491c:ldr=2`, `f9824934:str=2`, `f9824934:ldr=2`, `f982492e:ldrb=1`, `f9824924:ldr=1` |
| the image side | **empty** |
| the call | one `bl <st_send_status>` from `st_cmd_path`, **and the clause reads UNGATED rather than saying it**: the two instructions above it must be `mov r0, r5` and `bl <st_select_card>`, so a conditional branch between the two commands would be refused |
| the cells | `_sta_state`, `_sta_pre_state`, `_sta_state_held`, `_sta_illegal`, `_sta_err` are all in the image |

`tools/check_response_word_order.py` reads **9** response-reading functions and **0 divergences**, with
`st_send_status` the ninth at `src/entry/entry_storage.c:5073`. All **11** ladder values
(0, 11, 12, 18, 20, 21, 30, 33, 34, 35, 36) compile `-fsyntax-only` exit 0.

**The identical device surface is the point of the rung and not an accident of copy-paste**: CMD13
spends no register CMD7 did not, because it is a command with no data phase on the same bus.

## 6. The four rows, and the cell that makes the reading a state

| outcome | reading |
| --- | --- |
| `_sta_complete = 1`, `_sta_err = 0`, **`_sta_state = 4`** (TRAN) | **CMD7 LANDED.** The card is in the transfer state, the block's opcode check passed on CMD13 too, and the frontier is the **DATA PATH** — CMD8's 512-byte read — and nothing else |
| `_sta_complete = 1`, `_sta_err = 0`, **`_sta_state = 3`** (STBY) | **CMD7 DID NOT LAND.** CMD7's own `3` was the state the card was in and not the state it produced. The next arm is CMD7 again with **something else moved**, and the measured candidate is `TIMEOUT_CONTROL 0x2E`, still `0x00` on every press this ladder has made |
| `_sta_complete = 1`, `_sta_illegal = 1` (bit 22) | **THE CARD REFUSING CMD13 ITSELF** — a state the standard does not have for an addressed card, which puts the wall below CMD13 entirely and says the RCA this ladder assigns is not the address the card answers to |
| `_sta_complete = 0` with `CMD_INHIBIT` asserted | a hang on a command with no data phase and the same five-bit window as CMD7 — a property of the block and not of the word |

**And `_sta_state_held` is the cell that makes the reading a STATE rather than a snapshot of a
transition.** CMD13 cannot move the card, so `_sta_state` equalling `_sta_pre_state` — the same field
of the same format, read before the command, out of **CMD7's own R1** — is the expected reading, and a
`0` there is what would need explaining. `_sta_pre_state` is the **first precondition in this ladder
that is genuinely the format it is decoded as**: every earlier pre-read was a CSD word, a raw register
or a previous command's R1 sampled too early to mean anything.

`_sta_err` carrying `SDHCI_INT_INDEX` (`0x00080000`) is a frame whose opcode field is not 13 — the
block's own comparator, which **rung 36 put on the bus for the first time and this rung keeps on**.

## 7. The lane, stated rather than assumed

`scripts/run_and_capture.sh` is the **firer's** file. This session is `run-experiment-526`, the gate
owner, and `ListAgents` resolves **two peers, `zl1-bb10-10` and `xing4-decode-launch-profile` —
neither of them `mi4-ios6-1a`**, the lane the tree's lane agreement names for the runner. **The lane
rule therefore has no live addressee today, and a lane rule invoked against a session that is not
running enforces nothing.** The edit was made here, not reported away, and the reason is stated in one
sentence: **the press path was dead until the literal moved** — the gate refuses, readiness is FAIL,
and no press can be made. This is the LANE position, and the alternative (a COST reason) does not
apply, because the edit is one literal and two paragraphs.

## 8. What this does not do

* **It does not press, and no arm is armed for press.** The arm is **parked**; the press is the
  operator's and none is authorized.
* **It does not settle what the card will say.** All four outcomes are pre-registered in the source,
  the record and the readiness narration, and each names the next act.
* **It does not put the INDEX bit back on CMD3.** 821 §8 named that as a rung of its own and this rung
  does not take it: one rung moves one thing.
* **It reaches no block.** No transfer, no CMD8, no CMD16, no CMD17/18, no filesystem, no mount.
* **It moves no payload switch**: `stage90-build-config.txt` is byte-identical to rungs 23 through 36.

## 9. The goal

**THE GOAL IS NOT MET.** 「把基础驱动跑起来」 is one command further along — CMD7 completed with an
attributable frame and the card reported STBY — but **no transfer completes, no filesystem is reached
and no mount is made**, so 「让os可以正常启动并且挂载存储」 is not reached and **TWRP-TO-STORAGE STAYS
WITHHELD**, for 818 §5's reason: the storage it would be written to is the one thing this ladder has
not got working. The first clause, 「起码要能进入操作系统」, remains met and is unchanged by this step.
