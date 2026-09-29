# 821 — the rung-36 press: CMD7 completed, the block's own opcode check passed, and the card says STBY

**A PRESS, under the operator's authorization asked for and given in this session** — the answer was
*"Press rung 36 now"*, chosen over staying host-side and over stopping. **ONE gate exit 0 / 613 lines,
0 `FAIL`; ONE runner exit 0**; `fastboot boot` only; nothing flashed; nothing written to storage; no
reboot commanded; **`33e80afe` absent from both device lists, hand-checked immediately before the
gate** (neither log carries that check — it is this session's own reading, and it is named here rather
than implied). The phone returned to Android **29 s after the send** (the runner's own line,
`seen via: adb`).

**AND THE ANSWER IS TWO READINGS, BOTH OF THEM NEW, AND ONE OF THEM IS 817's.** `SDHCI_INT_INDEX`
**was enabled** and it **did not fire** — so the block compared the response's Index field against 7
and it matched, which means **the frame that arrived IS CMD7's response**. And the card's own status
word reports the state **`R1_STATE_STBY`** — one step above the IDENT every arm of this ladder has
been reading since rung 34, which answers 819 §5's first open question: **CMD3 was accepted.**

---

## 1. The press

| | |
| --- | --- |
| arm | `armed-storage-18b0ccf3`, switch **VALUE 35 = ordinal rung 36**, CMD7 |
| what was sent | `stage90-qcdt.img` `35ad5fff…`, 8,572,928 B — the runner's own line confirms the bytes the gate read were the bytes sent, `unchanged across the send` |
| the runner's arm check | `declared arm 'armed-storage-18b0ccf3' == the recorded set of the bytes to be sent`; 11/11 files of `out/` match |
| capture | `out/stage90/captures/rung36-cmd7-20260929-012553-last_kmsg.txt` **641,956 B sha `2d0a463d19233f2d6e32b36adf5b27d4333dea8bff11c5f23a935d26f7c6c8ac`** |
| beside it | `…-gate.log` 52,547 B sha `4bd751c0…`, `…-run.log` 89,491 B sha `d6ce6086…` |
| readiness | `tools/verify_press_ready.sh` resolved the arm **as the recorded set** before the gate: 5 of 5, exit 0 |
| the run's ending | `_post_end_calls = 8`, `_seam_post_end_ticks = 0x06ddd000 = 115,200,000`, `_post_cntfrq = 0x0124f800 = 19,200,000` — **6.0000 s, the ladder's own deadline**, and the known forced ending (`[[mi4-the-run-ends-at-entry-epilogue]]`), unchanged from every run since 520 |

**THE ARM IS NOW SPENT.** Its park stays in `out/stage90/frozen/armed-storage-18b0ccf3/` because a park
is the record of what was built — **`frozen/` is a record, not a queue**, and re-pressing this arm buys
nothing that is not in §2 below.

## 2. The cells

The command, as it went out and as it was read back:

| cell | value | reading |
| --- | --- | --- |
| `_sel_op` / `_sel_flags` / `_sel_arg` | `7` / **`0x00000015` = 21** / `0x00010000` | the driver's own three values (`mmc_ops.c:33/36/37`) |
| `_sel_word` / `_sel_word_read` | **`0x0000071a`** / `0x0000071a` | CMD7's command word: opcode 7 << 8 \| the flag byte `0x1A`. **The word 820 predicted and the compiler made the source assert** — and it is `0x031A` with the opcode changed, which is the whole of §4 |
| `_sel_sent` / `_sel_calls` / `_sel_done` | `1` / `1` / `1` | one command, one body, one exit |
| `_sel_pre_cid_sent` | `1` | **published and not gated on**: CMD2 did reach the bus in this boot |

The completion, and the reading this rung was built for:

| cell | value | reading |
| --- | --- | --- |
| `_sel_complete` | `0x00000001` | `INT_STATUS` bit 0, `SDHCI_INT_RESPONSE`, latched |
| `_sel_err` | **`0x00000000`** | **none of the four command-error bits — and `SDHCI_INT_INDEX` is one of them** (see below) |
| `_sel_timeout` | `0x00000000` | the poll was not exhausted |
| `_sel_any_polls` / `_sel_status_any` | `0x00000585` = 1,413 / `1` | the interrupt latched on the **first** poll |
| `_sel_inhibit_seen` / `_before` / `_after` | `0` / `0` / `0` | **the block was never inhibited** — the word `0x071A` does **not** hang it |
| `_sel_stale` / `_clear_wrote` / `_clear_after` | `1` / `1` / `0` | `stale` is **`INT_STATUS` as found — the latch's baseline**, not a statement about the response |

The response, and the four R1 fields the vendor's own macros name:

| cell | value | reading |
| --- | --- | --- |
| `_sel_resp` / `_sel_resp_read` / `_sel_rsp_present` | `0x00000700` / `1` / `1` | the R1 card status, read |
| `_sel_state` | **`0x00000003`** | `mmc.h:141`'s `R1_CURRENT_STATE` = **(x & 0x1E00) >> 9** — and `3` is **`R1_STATE_STBY`** (`mmc.h:150`) |
| `_sel_ready` | `0x00000001` | `mmc.h:142`'s `R1_READY_FOR_DATA`, bit 8 — the card is ready for data |
| `_sel_switch_err` | `0x00000000` | `mmc.h:143`, bit 7 |
| `_sel_illegal` | **`0x00000000`** | bit 22 **clear** — **the card did NOT refuse the command** |

And the register the short response writes:

| cell | value |
| --- | --- |
| `_sel_resp_pre` (read before the command) | **`0xef8a4040`** — the CSD's word at `RESPONSE + 0`, the same value `_csd_raw3` published in this boot |
| `_sel_resp_post` / `_sel_resp_moved` | **`0x00000700`** / `1` |
| `_sel_resp_is_arg` | `0` |

**`RESPONSE + 0` moved off the CSD's last word and onto the R1**, which is exactly what a 48-bit
response does — that register alone, and no byte below any word. **That is the direct evidence the frame
was read**, and it is stated for `RESPONSE + 0` and for nothing else: see §6 for the three upper
registers, whose movement this arm measures and cannot explain.

## 3. The first reading: `SDHCI_INT_INDEX` did not fire, so the frame is CMD7's

`sdhci.c:1140-1143` maps `MMC_RSP_OPCODE` onto `SDHCI_CMD_INDEX` `0x10`, and the vendor's own header
says what that bit does:

```c
#define  SDHCI_INT_INDEX	0x00080000      /* sdhci.h:133 */
#define  SDHCI_INT_CMD_MASK	(SDHCI_INT_RESPONSE | SDHCI_INT_TIMEOUT | \
		SDHCI_INT_CRC | SDHCI_INT_END_BIT | SDHCI_INT_INDEX | \
				 SDHCI_INT_AUTO_CMD_ERR)     /* sdhci.h:144-146 */
```

This arm's window is `ST_SDHCI_INT_ENABLE_CMD = 0x000F0001` = bits 0 and **16, 17, 18, 19** — the four
command-error bits, and **bit 19 is `SDHCI_INT_INDEX`**. `_sel_ena_wrote = 0x000f0001` and
`_sel_ena_held = 0x000f8001` say the window really held them (bit 15 is `SDHCI_INT_ERROR`, the summary
bit the driver's own `int_enable` already carried). `_sel_err = status_after & ST_SDHCI_INT_CMD_ERR` and
it is **`0x00000000`**.

**So the block's own comparator ran and passed.** With the bit clear — every arm from rung 21 up — a
completion with no error says only that *a CRC-valid 48-bit frame arrived*. With it set and
`SDHCI_INT_INDEX` silent, the frame that arrived **carries index 7**:

> **THE FRAME IS CMD7's RESPONSE.**

That is the attribution 813 §5 recorded as unreachable — *"the block reports that a frame arrived; it
does not report who sent it"* — and it is the first time this ladder has been able to make it about any
command.

**And it retires rung 21's deviation, measured on a live chain.** 817 read the archive and argued that
the rung-19 hang that justified removing the bit was a block still inhibited by a CMD2 that never
finished, and that the removal was inherited from an artifact. **This press is the positive control that
argument never had**: the same controller, the same five-bit window, the same chain, with the bit
**on** — `_sel_complete = 1`, `_sel_inhibit_seen = 0`, `_sel_err = 0`. **817's hypothesis is refuted and
rung 21's reason is gone.** What remains to be decided is not whether the bit is safe but whether CMD3
should carry it too, and that is a rung of its own.

## 4. What the press confirmed about the word itself

820 built the arm around three assertions and the compiler caught a wrong value in one of them. Every
one of the three is now measured on the wire:

| 820's assertion | the press |
| --- | --- |
| the word is `0x071A` | `_sel_word = 0x0000071a`, and `_sel_word_read` agrees |
| the flags are 21 | `_sel_flags = 0x00000015` |
| the flag byte is CMD3's live byte with the INDEX bit back | CMD3's own word in the same boot: `_nidx_word = 0x0000030a`, flags `_nidx_flags = 0x00000005`; CMD7's: `0x071a`, flags `0x15` — **same low byte `0x1A` against `0x0A`, one bit apart, exactly as 820's second `_Static_assert` says** |

**And `0x031A` appears nowhere on this boot's wire.** The value 820 removed from the record was never
sent by anything: CMD3 sent `0x030A`.

## 5. The second reading: the card reports STBY, so CMD3 was accepted

Three of this ladder's own cells, in one boot:

| where | cell | value | `R1_CURRENT_STATE` |
| --- | --- | --- | --- |
| CMD3's own response, this boot | `_nidx_state` | `0x00000002` | **2 = `R1_STATE_IDENT`** (`mmc.h:149`) |
| read between CMD3 and CMD9, this boot | `_csd_pre_state` | `0x00000002` | **2 = IDENT** |
| **CMD7's own response, this boot** | **`_sel_state`** | **`0x00000003`** | **3 = `R1_STATE_STBY`** (`mmc.h:150`) |

819 §5 left this open:

> *"It does not settle where the card is. `_csd_pre_state = 2` (IDENT) comes from CMD3's R1 status word
> … and a card moves to STBY when CMD3 completes, so the field may be reporting the state the card was
> in inside CMD3."*

**CMD7 is the second command to report the field, and it reads one step higher than CMD3 did.** The two
readings are consistent with one thing only: **the card advanced from IDENT to STBY between the two
responses, and the only command between them that can do that is CMD3.** So CMD3 was accepted — stated
now as a *field* rather than as the absence of a refusal, which is what 819 §5 said the CSD's arrival
could not be.

**AND THE CELL DOES NOT SETTLE WHETHER CMD7 SELECTED THE CARD.** `R1_STATE_STBY` is also what a CMD7
that did **not** take effect would leave behind, and the field is sampled at response time — before the
command's own effect on the JEDEC convention, which is exactly what makes CMD3's `2` and CMD7's `3` read
as a transition rather than as a failure. **One reading of one field cannot separate *"the card is in
STBY because CMD7 has not landed yet"* from *"the card is in STBY because CMD7 did not land"*.** The row
820 pre-registered for the refusal (`_sel_illegal = 1`) did **not** fire, and `_sel_ready = 1`
(`R1_READY_FOR_DATA`) says the card is out of its identify/programming busy window. What is needed is not
another reading of this cell — it is a command whose own answer is the state field, which is §8.

## 6. One thing this arm measures and cannot explain, recorded rather than passed over

`RESPONSE + 0` moving is §2's evidence. The other three registers **also** moved, and what they moved to
is not anything this image writes:

| register | before the command (`_sel_raw_pre*`) | after (`_sel_raw_post*`) |
| --- | --- | --- |
| `RESPONSE + 0x0C` | `0x00d00f00` | `0x8a40401e` |
| `RESPONSE + 0x08` | `0x320f5903` | `0xffffffef` |
| `RESPONSE + 0x04` | `0xffffffff` | `0x0f5903ff` |
| `RESPONSE + 0x00` | `0xef8a4040` | **`0x00000700`** (the R1) |

The three that moved hold **the CSD's *assembled* words** — `_csd_resp1 = 0x0f5903ff` at `+0x04`,
`_csd_resp2 = 0xffffffef` at `+0x08`, and `_csd_resp3 = 0x8a404000` at `+0x0C` **except for its low byte,
which reads `0x1e` where the assembled word has `0x00`.** The before-values are the CSD's *raw* registers
and the `_sel_raw_pre*` reads reproduce this boot's `_csd_raw*` cell for cell, so the two readings are of
the same four registers and neither is a transcription.

**No store to `RESPONSE` exists anywhere in this image** — the build clause asserts the body's device
surface is four reads and the window, and the census refuses an unlisted access. So this is either
something the *block* does to its own response registers when a 48-bit response arrives, or an artifact
of how those registers behave after a 136-bit read. **This arm cannot tell those apart and does not
claim to.** It is written down because it is the same shape as 814 — an assembled value and a raw value
sitting in the same register file, one rung up — and because a reader comparing `_csd_resp*` against a
later `_sel_raw_post*` would otherwise read a coincidence as a law. The `0x1e` in the low byte of
`RESPONSE + 0x0C` is in no cell and no comment in this tree.

## 7. Containment, and what the press cost the machine

| | |
| --- | --- |
| storage cells | **555 → 611** |
| keys lost | **0** |
| keys added | **56 — the entire `_sel_*` family, and nothing else** (checked: no added key lacks the `_sel_` prefix) |
| values identical | **534** |
| values moved | **21**, and every one is a free-running quantity or an address inside the image |

The 21 moved are poll counts, tick counts and the two absolute power-IRQ timestamps, plus
`_pwr_irq_reg_handler` (`0x8000d314 → 0x8000d320`, an address inside the entry image, which grew) and the
three `_*_cmdlow_seen` poll indices. **Not one decision cell moved**: `_cmd1_resp = 0x40ff8080`,
`_cmd1_word`, `_cid_gate_word = 0xc0ff8080`, `_nidx_resp = 0x00000500`, `_nidx_state = 2`,
`_csd_structure = 3`, `_csd_mmca_vsn = 4` and `_csd_capacity_blocks = 0x00200000` are identical across
the two boots. Whole-log cells: 1,467 → 1,525. The OS came up exactly as 818 measured it —
`BSD root: md0, major 2, minor 0`, the init load returned, pid 1's AST put it in user mode, and the
kernel's idle path entered 63,333 times (against 818's 62,283 — a free-running count).

**Nothing on the device changed.** `fastboot boot` only, no flash, no storage write; the capture's own
payload report shows the ladder's stores are the same set the rung below makes, and CMD7 added no store
to any device — its whole surface is four reads of `RESPONSE`, the `INT_ENABLE` window read-written
twice, `SIGNAL_ENABLE`, `INT_STATUS`, the `TIMEOUT_CONTROL` byte and `PRESENT_STATE`, all of them
registers the arms below it already touch.

## 8. What follows

**The frontier is no longer CMD7's mechanism or the INDEX bit. It is one field's sampling convention,
and it has a direct measurement.**

| candidate | what it would settle | cost |
| --- | --- | --- |
| **CMD13 `SEND_STATUS`** | **the state field itself, read by a command that changes nothing** — TRAN versus STBY, with a fresh R1 and no data phase. It is `mmc_ops.c`'s own `mmc_send_status`, it returns R1 through the same 48-bit path CMD7 just proved works, and it needs **no new mechanism at all** | one new body of the shape `st_select_card` already is |
| CMD8 `SEND_EXT_CSD` | the card's real density — and it is the driver's own next statement (`mmc.c:1446`) | **a 512-byte DATA read, which this image has no mechanism for**: no `sdhci_prepare_data`, no block size, no DMA or PIO path, no `TIMEOUT_CONTROL` raised (`_sel_tout_ctl = 0x00`, still untouched by every rung) |

**CMD13 is the honest next rung** and CMD8 is not yet: it answers the ambiguity §5 names, it is one
command of a shape already built and already proven, and it does not require the data path to exist
first. CMD8 is where the ladder stops being a command ladder — 819 §5 said it and this press does not
change it.

**AND ONE RUNG-21 QUESTION IS NOW WORTH A RUNG OF ITS OWN.** With `_sel_err = 0` beside the INDEX bit
enabled, the bit is measured safe on a live chain. Putting it back on **CMD3** would give this ladder an
attributable CMD3 as well — and it is now a change with a measured precedent rather than a change
inherited from a dead one. It is *not* what rung 37 should do: one rung moves one thing, and CMD13 is
the measurement the frontier actually needs.

## 9. The goal

**THE GOAL IS NOT MET.** 「把基础驱动跑起来」 moved again — the storage chain now has an *attributable*
completion (the block's own opcode check passed), the card is out of IDENT, and `R1_READY_FOR_DATA` is
set — but **no transfer completes, no filesystem is reached and no mount is made**, so
「让os可以正常启动并且挂载存储」 is not reached and **TWRP-TO-STORAGE STAYS WITHHELD**, for 818 §5's
reason: the storage it would be written to is the one thing this ladder has not got working. The first
clause, 「起码要能进入操作系统」, remains met and is unchanged by this press (§7).

**AND NOTHING IS ARMED.** No build has been made since this press; `out/` holds the spent rung-36 park;
no arm is armed and no press is owed or authorized.
