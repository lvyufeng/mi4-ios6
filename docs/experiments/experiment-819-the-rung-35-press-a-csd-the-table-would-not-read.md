# 819 — the rung-35 press: CMD9 came back with a CSD, and the arm's own table would not read it

**A PRESS, under the operator's authorization asked for and given in this session** — the answer was
*"Press rung 35 first, then build 36"*, chosen over building first and over staying host-side. **ONE
gate exit 0 / 612 lines; ONE runner exit 0**; `fastboot boot` only; nothing flashed; nothing written
to storage; no reboot commanded; `33e80afe` absent from both device lists, checked by hand immediately
before the gate (neither log carries that check — it is this session's own reading, and it is named
here rather than implied). The phone returned to Android **25 s after the send** (the runner's own
line, `seen via: adb`).

**AND THE READ IS THE ONE THE ARM WAS NOT BUILT TO EXPECT.** CMD9 completed with no error, the four
`RESPONSE` registers **moved**, and the four words the driver's assembler makes of them decode — by
five independent cross-checks, not by plausibility — to a **genuine eMMC v4 CSD**. The arm's own
outcome table calls that row *"a 136-bit register read that is NOT a response to this command."* **The
table is wrong and the card answered.**

---

## 1. The press, and what it cost

| | |
| --- | --- |
| arm | `armed-storage-47c657af`, switch **VALUE 34 = ordinal rung 35**, CMD9 |
| what was sent | `stage90-qcdt.img` `b703a0a5…`, 8,572,928 B — read out of the gate's own log and compared against the park before firing |
| the gate read the image as | `STAGE90_XNU_STORAGE_PROBE=34`, `STAGE90_XNU_ENTRY_SHA256=47c657af…`, the config bound to that hash |
| capture | `out/stage90/captures/rung35-csd-20260929-003809-last_kmsg.txt` **639,650 B sha `ce33dd94b9ceafffe797e4d0a7943e723ef8e836aea000f122a619f66e5806e0`** |
| beside it | `…-gate.log` 52,323 B sha `a02b7391…`, `…-run.log` 89,267 B sha `e5b72f9b…` |
| readiness | `tools/verify_press_ready.sh` resolved the arm **as the recorded set** before the gate: 5 of 5, exit 0 |
| nothing under `out/` that the arm owns moved | `xnu_arm_entry.bin` is still `47c657af…`, `stage90-qcdt.img` still `b703a0a5…` |

**THE ARM IS NOW SPENT.** Its park stays in `out/stage90/frozen/` because a park is the record of what
was built — **`frozen/` is a record, not a queue**, and re-pressing this arm buys nothing that is not
in §2 below.

## 2. The cells

The command, as it went out and as it was read back:

| cell | value | reading |
| --- | --- | --- |
| `_csd_op` / `_csd_arg` | `0x00000009` / `0x00010000` | CMD9, argument `ST_MMC_RCA_1` — the constant the **driver** assigns (`mmc.c:1400` `card->rca = 1`) |
| `_csd_flags` | `0x00000007` | `ST_MMC_RSP_R2` = PRESENT\|136\|CRC — CMD9's own flags, not a stale cell |
| `_csd_word` / `_csd_word_read` | `0x00000909` / `0x00000909` | the word written and the word read back agree (`_Static_assert` at `entry_storage.c:2335`) |
| `_csd_sent` / `_csd_calls` / `_csd_done` | `1` / `1` / `1` | one command, one body, one exit |

The completion:

| cell | value | reading |
| --- | --- | --- |
| `_csd_complete` | `0x00000001` | `INT_STATUS` bit 1, `SDHCI_INT_RESPONSE`, latched |
| `_csd_err` | `0x00000000` | **none of the four command-error bits** — and they were enabled (`_csd_ena_wrote = 0x000f0001`), so a bad CRC-7 on the 136-bit frame would have been latched here |
| `_csd_timeout` | `0x00000000` | the poll was not exhausted |
| `_csd_any_polls` / `_csd_status_any` | `0x00000c4d` = 3,149 / `1` | the interrupt latched on the **first** poll |
| `_csd_inhibit_seen` / `_before` / `_after` | `0` / `0` / `0` | **the block was never inhibited** — unlike the dead-chain CMD3 of rung 19, whose `CMD_INHIBIT` was asserted at all 1,024 samples |
| `_csd_stale` / `_clear_wrote` / `_clear_after` | `1` / `1` / `0` | `stale` is **`INT_STATUS` as found — the latch's baseline**, *not* a statement about the response word: the previous command's bit was found, written back, and the register read `0` after |

The precondition, and the register the command actually left behind:

| cell | value |
| --- | --- |
| `_csd_pre_resp` | **`0x00000500`** — `RESPONSE + 0`, read immediately before CMD9 |
| `_csd_pre_state` / `_pre_ready` / `_pre_illegal` | `2` (IDENT) / `1` (READY_FOR_DATA) / `0` (ILLEGAL_COMMAND **clear**) |
| the same four registers one command earlier (`_nidx_raw_post0..3`, same boot) | `0xe00bb102` `0x47014a2f` `0x44573136` **`0x00000500`** |
| `_csd_raw0..3` after CMD9 | `0x00d00f00` `0x320f5903` `0xffffffff` `0xef8a4040` |
| `_csd_resp0..3` (assembled) | `0xd00f0032` `0x0f5903ff` `0xffffffef` `0x8a404000` |

**`RESPONSE + 0` MOVED, and it is the one register whose before-value this arm measures: `_csd_pre_resp`
is CMD3's `0x00000500`, read by the same body one store earlier, and CMD3's own post-read in the same
boot publishes `_nidx_raw_post3 = 0x00000500` as well.** After CMD9 it is `0xef8a4040`. A 48-bit
response writes that register alone; **all four moved**, so the frame was read into all four — which is
what a 136-bit response does and what nothing else does.

## 3. The reading: it is a CSD, and here are five reasons that do not depend on liking it

The four registers are assembled the way `sdhci_finish_command` assembles them
(`sdhci.c:1163-1172`: `resp[i] = raw_i << 8 | raw_{i+1} >> 24`, word 3 first, no byte below the last),
and the vendor's own field offsets are applied to **that** array — 814's repair, and the reason the
four `_csd_resp*` cells above are not the four `_csd_raw*` cells. Decoded independently of this image
(the four registers, the assembler's arithmetic, and `UNSTUFF_BITS`'s bit numbering, in 30 lines of
python) they reproduce **every field the arm publishes**:

| field | bits | value | the vendor's own name for it |
| --- | --- | --- | --- |
| `CSD_STRUCTURE` | 127:126 | **3** | the **eMMC** CSD structure; `mmc.c:159` rejects only `0` |
| `SPEC_VERS` | 125:122 | **4** | `mmc.h:272` **`CSD_SPEC_VER_4`** *"Implements system specification 4.0 - 4.1"* |
| `TAAC` / `NSAC` / `TRAN_SPEED` | 119:96 | `0xf` / `0` / `0x32` | the canonical 25 MHz triple |
| `CCC` | 95:84 | **`0xf5`** | exactly the eMMC class set — 0, 2, 4, 5, 6, 7 |
| `READ_BL_LEN` / `WRITE_BL_LEN` | 83:80 / 25:22 | `9` / `9` | 512 bytes both ways |
| `R2W_FACTOR` | 28:26 | `2` | the eMMC value |
| `C_SIZE` / `C_SIZE_MULT` | 73:62 / 49:47 | **`0xfff`** / **`7`** | the mandated **dummy maximum** for a part whose real density is in the EXT_CSD |
| `capacity = (1+C_SIZE) << (C_SIZE_MULT+2)` | — | **`0x200000` = 2,097,152** | = **`4096 * 512`** |

**`4096 * 512` is not this project's number.** It is the literal at **`mmc.c:241`** —
`if (card->csd.capacity == (4096 * 512))` — the vendor's own test for *"this is a high capacity card
whose CSD carries the magic size"*, with the comment at `mmc.c:238` above it saying so. Three cells
this ladder published (`_csd_c_size = 0xfff`, `_csd_c_size_mult = 7`, `_csd_capacity_blocks =
0x200000`) reproduce a constant the shipping driver compares against.

The chain of evidence, stated as five things that would each have to be true by accident:

1. **All four `RESPONSE` registers moved during the command** (§2), and the one with a measured
   before-value moved off CMD3's word.
2. **`_csd_err = 0` with the error bits enabled** — a 136-bit frame arrived and its CRC-7 passed.
3. **The assembled words reproduce independently** — the arm's arithmetic is not self-certifying; a
   second implementation of `sdhci_finish_command` plus `UNSTUFF_BITS` gives the same six fields.
4. **Six fields land on canonical values that are not each other's all-ones** — a blank or wiped
   register file gives `SPEC_VERS = 15` and `CCC = 0xff`; this reads `4` and `0xf5`.
5. **The capacity is the vendor's own hard-coded high-capacity constant**, and the part is a
   **`SDW16G`** — 16 GB, read off the CID's `prod_name` in the same capture — for which a CSD with
   `C_SIZE = 0xfff` is precisely what the standard requires.

And the chain around it is the driver's own order, running for the first time on a chain that works:
`CMD1` (OCR `0x40ff8080`) → `CMD2` (CID, serial `0x4a2fe00b` = this phone's `ro.serialno`) → `CMD3`
(R1, ILLEGAL_COMMAND clear) → `CMD9` (CSD). Every one of CMD1's, CMD2's and CMD3's words is
**byte-identical to the previous boot's**: the card's answers are deterministic across presses, which
is what makes a cross-check against a vendor constant worth anything.

## 4. The arm's own outcome table would have called this a failure — and the table is what is wrong

The arm pre-registers three rows (`entry_storage.c:4680-4686`, repeated in `records/revert-set.txt`):

> `_csd_complete = 1` with `_csd_structure = 1` and `_csd_mmca_vsn = 4` is a CSD and CMD7 is next;
> `_csd_complete = 1` with a structure that is neither 1 nor 2 is a 136-bit register read that is NOT a
> response to this command, which is 738's stale-word trap one rung up; `_csd_complete = 0` with
> `_csd_pre_state = 2` puts the wall where the measurement already points.

Measured: `_csd_complete = 1`, `_csd_structure = 3`. **Row 2 fires.** Row 2 is false, and the reason
is visible in the vendor's source the table was supposed to be transcribing:

* **`mmc.c:159` rejects `0`, not `{1,2}`.** `if (csd->structure == 0) … return -EINVAL`. The vendor
  understands `1`, `2` **and `3`**, and its own comment (`mmc.c:153-157`) says which: *"We only
  understand CSD structure v1.1 and v1.2 … We also support eMMC v4.4 & v4.41."*
* **`3` is the structure an eMMC v4.41 card reports**, and this card says `SPEC_VERS = 4` — the
  vendor's `CSD_SPEC_VER_4`.
* **`{1, 2}` is the SD family's reading of the field**, and this is an eMMC. `structure = 1` is not
  "the CSD" in the MMC world; it is one MMC CSD revision among several.

This is **`one value, two definitions`** with the two definitions being *the same field's meaning per
card family* — and the ladder encoded the family it is not. It is the second time in three rungs that
this rung's pre-registration has been written from a reading the card does not have (814's was the
raw-word decode); the class is the one 813 named and 816 built a tool for.

**AND THE SAME MISTAKE IS ONE PARAGRAPH EARLIER, IN PROSE**, at `entry_storage.c:4525`:

> on the eMMC v4 path this card takes (`mmc.c:110`, the same `case 2/3/4` that carries the CID's
> 32-bit serial) the CSD is v1.2, so **`_csd_structure` must read 1 and `_csd_mmca_vsn` must read 4**

Two defects in one sentence. `mmc.c:110` is inside **`mmc_decode_cid`** — the switch there is on
`cid->mmca_vsn`, the *CID's* spec version, which is why `case 2/3/4` is the one carrying the 32-bit
serial. It is not the switch `mmc_decode_csd` makes, which is on `structure` (`mmc.c:158-163`). **Two
different switch variables in two different decoders, read as one.** And "the CSD is v1.2" does not
give `1` either: in this vendor's own comment v1.2 is `2`, and an eMMC 4.41 card is `3`.

**A pre-registration is a claim in a comment with a table drawn round it, and nothing checks it.** The
arm's six field *expressions* are now checked (`tools/check_response_word_order.py` rule 4 refuses the
raw-word decode); its one *classification rule* is not checked by anything, and it is the rule that
decides what the press means. What the next arm must carry is stated in §8.

## 5. What this does not settle

* **It does not settle where the card is.** `_csd_pre_state = 2` (IDENT) comes from CMD3's R1 status
  word, taken *before* CMD9 — and a card moves to STBY when CMD3 **completes**, so the field may be
  reporting the state the card was in *inside* CMD3. The press cannot separate *"the state field is
  sampled before the transition"* from *"the card is still in IDENT and answered CMD9 anyway"*, and
  the pre-registered row 3 rests on the second reading.
  **The vendor's own init runs CMD3 then CMD9 back to back** (`mmc.c:1409` → `:1420`) on this device,
  so the sequence is the shipping driver's and a CSD is its expected outcome — but *expected* is not
  *measured*. **CMD7's own R1 carries a state field and a last-legal-command field, and it is the
  direct measurement.** See §8.
* **It does not settle whether CMD3's response is CMD3's.** The ladder's CMD3 sends `0x030A`, with
  `SDHCI_CMD_INDEX` clear (817). A CRC-valid frame arrived (`_nidx_err = 0`) and the register moved,
  which is what 813 §5 could say and no more. The CSD's arrival is *evidence* that CMD3 was accepted —
  a card that had refused CMD3 would be in IDENT — but it is evidence of the same shape as the above,
  not an attribution.
* **It reaches no block.** No data transfer has ever run on this ladder: no CMD8 (`SEND_EXT_CSD`), no
  CMD16, no CMD17/18, no filesystem, no mount. The CSD's capacity is the *legacy* 1 GiB placeholder
  and the real density is in the EXT_CSD, which is CMD8's 512-byte **data** read — a mechanism this
  image does not have.
* **It does not say the reset tail is proven**, or anything else about the run's ending. The run ended
  at `_post_end_calls = 8` (6.0000 s, the ladder's own deadline) and the `fault_addr=0xfa0065c` panic
  is the known forced ending — `[[mi4-the-run-ends-at-entry-epilogue]]`. The OS came up exactly as
  818 measured: `open error 0x00000000`, `read` → `0xfeedface`, `getpid = 1`, `exit pid 2 rval 3`,
  `wait status 0x300`.

## 6. Five wrong vendor citations in the block this reading depends on

The field table at `entry_storage.c:4645-4650` is the thing a reader uses to check §3 by hand, and its
citations were re-measured against `external/android_kernel_xiaomi_cancro/drivers/mmc/core/mmc.c`:

| the table cites | the vendor's line there is | the field is actually read at |
| --- | --- | --- |
| `mmc.c:173` cmdclass | `csd->max_dtr = …` | **`mmc.c:174`** |
| `mmc.c:185` read_blkbits | `csd->write_blkbits = …` — **a different field** | **`mmc.c:180`** |
| `mmc.c:175` C_SIZE | *(blank)* — `:176` is the `C_SIZE_MULT` read | **`mmc.c:177`** |
| `mmc.c:174` C_SIZE_MULT | `csd->cmdclass = …` | **`mmc.c:176`** |
| `mmc.c:174-176` the capacity expression (`:4530`) | — | **`mmc.c:178`** |

**Five of the block's citations are wrong, and one of them names a different field.** `mmc.c:147`
(the function's definition), `:165`, `:162`, `:110`, `:201` and `:165-196` re-measure correct.

**A previous rung had already audited this area and declared it clean** — `records/revert-set.txt`
records a pass that corrected eight citations and then states that *"`mmc.c:1400`, `mmc.c:1409`,
`mmc.c:83`, `mmc.c:120` … and the `sdhci.c` ranges were **re-measured and are correct as written**."*
They were re-measured against **a list someone wrote down**, and these five were not on the list.
**An audit that walks a remembered list is not a citation check** — it is m810's shape (a clean verdict
about the cells that were read, published as a verdict about the file).

**They are owed to the next build cycle, and the cost has changed.** 813 and 816 assigned this class
to "the next build cycle" with a **COST** reason: any edit to `entry_storage.c` moves
`xnu_arm_entry-sources.txt`, one of the eleven members of the park, and would have forced a
withdrawal-and-repark of an arm that was armed and unspent. **The arm is spent, and rung 36 edits that
file regardless** — so under the new arm the repair is free, and it lands with the next build rather
than waiting for a cycle of its own.

## 7. Rung 34 → rung 35, cell by cell

| | |
| --- | --- |
| storage cells | **497 → 555** |
| keys lost | **0** |
| keys added | **58** — the entire `_csd_*` family, and nothing else |
| values identical | **475** |
| values moved | **22**, and every one is a free-running quantity |

The 22 moved are poll counts (`_*_polls`, `_*_any_polls`), tick counts (`_*_ticks`) and the two
absolute power-IRQ timestamps (`_pwr_irq_at`, `_pwr_wait_t0`); the largest move is 5 ticks and most
are 1 or 2. **Not one decision cell moved**: `_cmd1_resp = 0x40ff8080`, `_cid_resp0..3`, `_cid_raw0..3`,
`_nidx_resp = 0x00000500`, `_cid_gate_word = 0xc0ff8080` and every `_csd_pre_*` cell are identical
across the two boots. 200 of the 912 non-storage cells moved, which is the entry image's own text
having grown (814's repair) — the OS's pointers and addresses are read out of the image under test and
must move with it.

## 8. What follows: the driver's own next command, and the row the table should have had

The frontier is no longer CMD9. In the driver's own order it is **CMD7 `SELECT_CARD`** — `mmc.c:1436`'s
`err = mmc_select_card(card);`, whose body is at **`mmc_ops.c:26`** and whose own words are at
**`mmc_ops.c:36-37`**:

```c
cmd.arg   = card->rca << 16;
cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;      /* mmc_ops.c:37 */
```

**`MMC_RSP_R1` — `0x031A` — is the word 817 showed the ladder removed from CMD3 and has not sent
since rung 21.** So the rung-36 arm is one new command carrying the driver's own word, and it needs
**no new mechanism**: a 48-bit response, the arithmetic `st_cmd3_noidx` already runs, and the R1 decode
the CSD body already publishes (`R1_CURRENT_STATE` `mmc.h:141`, `R1_READY_FOR_DATA` `mmc.h:142`,
`ILLEGAL_COMMAND` bit 22). It is **R1, not R1B** — the vendor does not use the busy bit here, so there
is no DAT0 busy window to handle.

Three readings, and the first two are the two questions this press could not settle:

| outcome | reading |
| --- | --- |
| completes, `_err = 0`, state `4` (TRAN) | the card was in STBY, CMD7 selected it, **and the `INDEX` bit is safe on this controller on a live chain** — 817's question answered, on a new command, for free |
| completes, `_err = 0`, ILLEGAL_COMMAND set and state `2` | **the card is in IDENT and the CMD3 chain is not what it looks like** — the reading the pre-registration assumed, now measured rather than assumed |
| hangs with `CMD_INHIBIT` asserted and no interrupt | the removal was right and is now measured on a live chain; the successor drops the bit and CMD7 is re-armed without it |

**What the arm's table must not do is repeat §4.** The classification must be the vendor's own
predicate — **`structure == 0` is the only rejection (`mmc.c:159`)** — and the outcome rows for rung
36 must be written from the vendor's fields for *the card this is*, not from the SD family's reading of
a shared field name. Where a predicate can be made structural rather than written it should be: this
rung's own `_Static_assert`s (`entry_storage.c:2335` etc.) are the model, and §6's five citations are
the other half of the same repair.

**And the arm to press is not armed.** No build has been made since the press; `out/` holds the spent
rung-35 park; **no press is owed and none is authorized.**

## 9. The goal

**THE GOAL IS NOT MET.** The clause 「把基础驱动跑起来」 moved — the storage controller's chain now
answers at CMD9 and the card's own CSD is in the record — but **no transfer completes, no filesystem is
reached and no mount is made**, so 「让os可以正常启动并且挂载存储」 is not reached and
**TWRP-TO-STORAGE STAYS WITHHELD**, for 818 §5's reason: the storage it would be written to is the one
thing this ladder has not got working. The first clause, 「起码要能进入操作系统」, remains met and is
unchanged by this press (§5).
