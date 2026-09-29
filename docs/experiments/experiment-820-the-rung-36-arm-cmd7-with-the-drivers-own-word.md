# 820 — the rung-36 arm: CMD7, the driver's own word, and the one bit that has never been on a working chain

**A BUILD STEP. SOMETHING WAS BUILT AND NOTHING WAS SENT.** No `fastboot`, no gate, no runner, no
press, no device action of any kind; `33e80afe` was never checked for because no press was prepared.
The rung-36 entry image and its payload were built, the arm was parked with eleven members, the record
was extended, readiness is **5 of 5 exit 0** and `make check` is **exit 0**. **The press is the
operator's and no press is owed or authorized.**

---

## 1. What the rung-35 press left to do, and what this arm is

819 measured the frontier: CMD9 came back with a genuine eMMC v4 CSD, and the driver's own statement
after `mmc_send_csd` is `mmc.c:1436`'s

```c
err = mmc_select_card(card);
```

which is `mmc_ops.c:26`'s `_mmc_select_card` with a non-NULL card:

```c
cmd.opcode = MMC_SELECT_CARD;                 /* mmc_ops.c:33 */
cmd.arg    = card->rca << 16;                 /* mmc_ops.c:36 */
cmd.flags  = MMC_RSP_R1 | MMC_CMD_AC;         /* mmc_ops.c:37 */
```

`mmc.h:36` states the same three things in the vendor's shorthand — `#define MMC_SELECT_CARD 7 /* ac
[31:16] RCA R1 */` — so the opcode, the argument and the response format are read off the vendor's
files and not chosen here. The arm is:

| | |
| --- | --- |
| set | **`armed-storage-18b0ccf3`**, eleven members, `out/stage90/frozen/armed-storage-18b0ccf3/` |
| switch | **`STAGE90_XNU_STORAGE_PROBE=35`** (VALUE 35 = ordinal rung 36) |
| entry image | `18b0ccf370e3fe3b660b783bba38c25f0c26dbee17de772ce53b085d9aef71e2`, 5,552,764 B — **the size did not move from the rung-35 arm** |
| entry elf | `8cd0309cd036331a4e865ad69390cc0a726ae95ddec3aa8fd91229d15287e177`, **6,732,588 B — +32 on the rung-35 arm, which is the CMD7 body** |
| payload | `stage90-qcdt.img` `35ad5fff…`, 8,572,928 B |
| payload switches | `stage90-build-config.txt` **byte-identical** to rungs 23 through 35 (`6c2b6038…`) — **no payload switch moved** |
| readiness | **5 of 5**, exit 0 |
| `make check` | **exit 0** |

## 2. The flag value 21 is the arm

`MMC_RSP_R1` is `MMC_RSP_PRESENT | MMC_RSP_CRC | MMC_RSP_OPCODE` = `0x01 | 0x04 | 0x10` = **21 =
`0x15`**, and `sdhci.c:1140-1143` maps the third of those onto

```c
SDHCI_CMD_INDEX 0x10
```

which asks the block to compare the response's index field against the command index and raise
`SDHCI_INT_INDEX` (`0x00080000`) when it differs. **That bit is the one rung 21 removed from CMD3**
(`ST_MMC_RSP_R1_NOIDX`, live word `0x030A`, flag byte `0x0A`) on the evidence of the rung-19 press, and
817 read that press and the whole archive and measured that CMD1 had never completed on any arm before
rung 30 — so the hang that justified the removal was a block still inhibited by a CMD2 that never
finished, and the deviation was inherited from an artifact.

**So the two readings differ by what a completion means.** With the bit clear — every arm from rung 21
up — a completion with no error says only that a CRC-valid 48-bit frame arrived: exactly what 813 §5
could say and no more. With it set, `_sel_err = 0` beside `_sel_complete = 1` says **the frame that
arrived IS CMD7's response**. That is the attribution this ladder has never been able to make about any
command.

**And the bit is on this rung and not put back on CMD3**, because one rung moves one thing: CMD3's word
stays `0x030A` in the same boot, so `_nidx_*` beside `_sel_*` is a comparison of two words on one bus.
If this body hangs, the rung has bought 817's answer and not CMD7's, and the successor drops the bit.
That risk is stated in the source and in the record rather than discovered by the press.

## 3. `MMC_RSP_R1` is not `0x031A` — the defect the compiler caught, in the arm that documents the class

The first draft of rung 36's `_Static_assert` was

```c
_Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_SELECT_CARD, ST_MMC_RSP_R1) == 0x031Au, ...);
```

and **the compiler refused it.** `ST_SDHCI_CMD_WORD(op, fl)` is `(op << 8) | flags`, so `0x031A` is
**opcode 3's** word: 819's own CMD3 assert three blocks up reads `0x031A` because CMD3 *is* opcode 3.
CMD7 is opcode 7, so **CMD7's command word is `0x071A`**.

The mistake was reading `0x031A` as *"the driver's `MMC_RSP_R1` word"* when it is *"CMD3's word, one of
whose two halves is `MMC_RSP_R1`"*. Two values, and the sentence used one where it meant the other —
**`one value, two definitions`, in the rung whose prose is a correction of that exact class, and caught
by the assertion rather than by the paragraph.** It is also the class 819 had just committed one rung
down (m819: `{1,2}` for an eMMC's `structure`), and 814 one rung below that (the raw-word decode).

**The flag byte is the bridge between the two statements**, and it is now two assertions rather than a
sentence:

```c
_Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_SELECT_CARD, ST_MMC_RSP_R1) == 0x071Au, ...);
_Static_assert((ST_SDHCI_CMD_WORD(ST_CMD_OP_SELECT_CARD,    ST_MMC_RSP_R1) & 0xFFu) ==
               (ST_SDHCI_CMD_WORD(ST_CMD_OP_SET_RELATIVE_ADDR, ST_MMC_RSP_R1) & 0xFFu), ...);
_Static_assert(ST_MMC_RSP_R1 == (ST_MMC_RSP_R1_NOIDX | ST_MMC_RSP_OPCODE), ...);
```

The second is 817's finding as arithmetic: the two commands' **flag bytes are the same `0x1A`**, so
what this rung sends is the byte CMD3 does not — the INDEX bit included — and the two words differ only
by the opcode in bits 15:8.

## 4. The ladder's first ungated command, and why that is the honest choice

Every command below rung 36 carries a gate, and each gate is a statement about *the command below it*:
CMD2 on `cid_sent`, CMD9 on the same value, CMD3 on CMD2 having reached `COMMAND 0x0e`. **CMD7's
precondition is none of those.** It needs the card in STBY, which is a fact about what the *card* made
of CMD3 — and this ladder has no cell that measures it: the register read immediately before CMD7 holds
`_csd_resp_short = 0xef8a4040`, the last 32 bits of the CSD, which is not an R1 and carries no
`R1_CURRENT_STATE`.

**A gate keyed on anything this file can read would therefore be a gate on the wrong quantity** — m812
and m819's defect installed as a control-flow decision, which is strictly worse than the same mistake in
an outcome table. So the command goes out unconditionally, `_sel_pre_cid_sent` publishes what a gate
*would* have read, and the reading is the command's own R1.

**And it is safe because of what CMD7 is.** `ac [31:16] RCA R1`: no data phase, no storage write, no
register this image can reach — one 48-bit command frame and one 48-bit response on CMD, the same shape
as the CMD3 four lines up that this ladder already sends on every press. It is not a brick path. The
device-safety argument is not *"a rung says it is harmless"*; it is that the whole command path this
rung extends already runs and this adds one frame to it.

## 5. What the build proved, so the park is not just bytes

`build_entry.sh` gained `xnu_entry_819`, modelled on 812's clause and reading the **linked image**:

| assertion | measured |
| --- | --- |
| the body is in the image | `st_select_card` at **`0x8000ebcc`** |
| exactly one command | one `bl <st_send_command>` (disassembly line 76) |
| the opcode | `r0 = #7` — `SELECT_CARD` (a 3 here would be a second CMD3) |
| the argument | `r1 = #65536` — `card->rca << 16`, `ST_MMC_RCA_1` |
| **the flags** | **`r2 = #21`** — `MMC_RSP_R1`; **5 here is `R1_NOIDX`, the silent failure this clause refuses** |
| the device surface, in program order, distinct | `f982491c:ldr f9824918:ldr f9824914:ldr f9824910:ldr f9824934:str f9824930:ldr f9824934:ldr f9824938:ldr f982492e:ldrb f9824924:ldr` |
| the counts | `f9824910:ldr=2`, `f982491c:ldr=2`, **`f9824934:str=2`**, `f9824934:ldr=2`, `f982492e:ldrb=1` |
| the image side | **empty** |
| the call | one `bl <st_select_card>` from `st_cmd_path`, **and not inside an `if`** |
| the cells | `_sel_state`, `_sel_illegal`, `_sel_err`, `_sel_resp_moved` are all in the image |

**`f9824910:ldr=2` is the assertion that keeps the freshness cell honest**: the four words are read
before the window opens and again after the command, and a count of **one** would mean the compiler
commoned the two loads — `_sel_resp_moved` would then compare a value with itself and read 0 on every
press, an arm whose one freshness cell could not be anything but a failure. **`f9824934:str=2` is the
window closing.**

**And there is no byte in the `0x13..0x1B` band and no second word**, which is the whole difference from
the body four hundred lines above it: CMD7's response is 48 bits, so no word has a byte below it.
CMD9's 136-bit assembler copied onto this response would parse, decode and publish a plausible R1 out
of a value assembled from a register that never moved — and that is a refusal, not a comment.

`tools/check_response_word_order.py` reads **8** response-reading functions and **0 divergences**, with
`st_select_card` the eighth.

## 6. Five owed citations landed, and they are proved comment-only

813 and 816 assigned this class to "the next build cycle" with a **COST** reason: any edit to
`entry_storage.c` moves `xnu_arm_entry-sources.txt`, one of the eleven members of a parked arm, and
would have forced a withdrawal-and-repark of an arm that was armed and unspent. **The arm is spent and
rung 36 edits that file regardless, so the repair is free under this arm.**

| site | was | is | measured |
| --- | --- | --- | --- |
| `entry_storage.c:266` and the prose at `:253` | `sdhci.h:79` | **`sdhci.h:162`** | `#define SDHCI_HOST_CONTROL2 0x3E` is defined exactly once in `external/`, at that line (816 §3) |
| the CSD field table `:4645-4650` | `mmc.c:147` structure | **`mmc.c:158`** | |
| | `:173` cmdclass | **`:174`** | |
| | `:185` read_blkbits | **`:180`** | `:185` is `write_blkbits` — **a different field** |
| | `:175` C_SIZE | **`:177`** | |
| | `:174` C_SIZE_MULT | **`:176`** | |
| the capacity sentence | `mmc.c:174-176` | **`mmc.c:178`** | |
| 813's four | `sdhci.c:1169-1175` | **`sdhci.c:1162-1175`**, and `:1162` for `rsp_present` | the guard is `if (host->cmd->flags & MMC_RSP_PRESENT)` at `sdhci.c:1162` |

**And the proof that they are comment-only is a measurement, not a claim**: the entry image was rebuilt
after them and the two `xnu_arm_entry.bin` files are **byte-identical** (`cmp` clean, `18b0ccf3…`) —
this project's linker-fill rule (`[[mi4-linker-fill-term]]`) applied as a build-time proof.

## 7. The rung-35 arm's false outcome table, repaired as a predicate

Rung 35's pre-registration classified `CSD_STRUCTURE` against `{1, 2}` and called anything else *"a
136-bit register read that is NOT a response to this command."* The press answered **3**, a genuine
eMMC v4 CSD. The vendor's predicate is `mmc.c:159`'s `if (csd->structure == 0)` and nothing else.

It is now a **build refusal** rather than a sentence, guarded `#if >= 35`:

```c
#define ST_CSD_STRUCTURE_REJECTED 0u
#define ST_CSD_STRUCTURE_IS_CSD(s) ((s) != ST_CSD_STRUCTURE_REJECTED)
_Static_assert(!ST_CSD_STRUCTURE_IS_CSD(ST_CSD_STRUCTURE_REJECTED), ...);
_Static_assert(ST_CSD_STRUCTURE_IS_CSD(1u) && ST_CSD_STRUCTURE_IS_CSD(2u) &&
               ST_CSD_STRUCTURE_IS_CSD(3u), ...);
```

The prose at `:4525` that reached *"must read 1"* through `mmc.c:110` — which is inside
`mmc_decode_cid` and switches on `cid->mmca_vsn`, a **different variable in a different decoder** from
`mmc_decode_csd`'s switch on `structure` at `mmc.c:158-163` — is corrected, and so are the two
sentences in `build_entry.sh` that repeated *"must read 1 and 4"*.

## 8. What this does not do, and the goal

* **It does not press, and no arm is armed for press.** The arm is **parked**; the press is the
  operator's and none is authorized.
* **It does not settle what the card will say.** All four outcomes are pre-registered in the source,
  the record and the readiness narration, and each names the next act: `_sel_complete = 1` with
  `_sel_err = 0` and `_sel_state = 4` (TRAN) is the card selected and CMD8 next; `_sel_err` carrying
  `SDHCI_INT_INDEX` is a frame whose opcode field is not 7; `_sel_illegal = 1` is the card refusing,
  which would say it is in IDENT and CMD3 was not accepted; `_sel_complete = 0` with `CMD_INHIBIT` is
  the word `0x071A` hanging a live block.
* **It reaches no block.** No transfer, no CMD8, no CMD16, no CMD17/18, no filesystem, no mount.
* **It moves no payload switch**: `stage90-build-config.txt` is byte-identical to rungs 23 through 35.

**THE GOAL IS NOT MET.** 「把基础驱动跑起来」 is one command further along — CMD9 answered with a real
eMMC v4 CSD and CMD7 is now the arm — but **no transfer completes, no filesystem is reached and no
mount is made**, so 「让os可以正常启动并且挂载存储」 is not reached and **TWRP-TO-STORAGE STAYS
WITHHELD**, for 818 §5's reason: the storage it would be written to is the one thing this ladder has not
got working. The first clause, 「起码要能进入操作系统」, remains met and is unchanged by this step.
