# 823 — the rung-37 press: the card is in TRAN, and CMD7 landed

**A PRESS, under the operator's authorization asked for and given in this session** — the answer was
*"Press rung 37 now"*, chosen over building rung 38 first and over stopping. **ONE gate exit 0 / 614
lines, 0 `FAIL`; ONE runner exit 0**; `fastboot boot` only; nothing flashed; nothing written to
storage; no reboot commanded; **`33e80afe` absent from BOTH device lists, hand-checked immediately
before the gate** (neither log carries that check — it is this session's own reading, and it is named
here rather than implied). The phone returned to Android **29 s after the send** (the runner's own
line, `seen via: adb`).

**AND THE ANSWER IS THE ROW THE ARM WAS BUILT TO MAKE POSSIBLE. `_sta_state = 4` = `R1_STATE_TRAN`.**
CMD7's own response had said `3` (STBY); CMD13's fresh response says `4` (TRAN). **The card is in the
transfer state** — the state from which every data command is legal — and **CMD7 is the only command
between the two samplings.** The frontier is no longer a state question. **It is the data path.**

---

## 1. The press

| | |
| --- | --- |
| arm | `armed-storage-054f8269`, switch **VALUE 36 = ordinal rung 37**, CMD13 |
| what was sent | `stage90-qcdt.img` `f94e7c4e…`, 8,589,312 B — the runner's own line confirms the declared arm is the recorded set of the bytes to be sent |
| the runner's arm check | `declared arm 'armed-storage-054f8269' == the recorded set of the bytes to be sent` |
| capture | `out/stage90/captures/rung37-cmd13-20260929-164239-last_kmsg.txt` **658,021 B sha `9bbb3695…`** |
| beside it | `…-gate.log` 52,771 B `94dab03a…`, `…-run.log` 89,438 B `5b0bca3c…` |
| readiness | `tools/verify_press_ready.sh` resolved the arm **as the recorded set** before the gate: 5 of 5, exit 0 |
| the run's ending | `_post_end_calls = 8`, `_seam_post_end_ticks = 0x06ddd000 = 115,200,000`, `_post_cntfrq = 0x0124f800 = 19,200,000` — **6.0000 s, the ladder's own deadline**, and the known forced ending (`[[mi4-the-run-ends-at-entry-epilogue]]`), unchanged from every run since 520 |

**The name's timestamp is the moment the capture was written** (16:42:39 UTC). Earlier archives in this
directory name the *start* of the press sequence, which is a gate plus a runner earlier; the convention
drifted and this file says which moment its name means rather than leaving a reader to infer it.

**THE ARM IS NOW SPENT.** Its park stays in `out/stage90/frozen/armed-storage-054f8269/` because a park
is the record of what was built — **`frozen/` is a record, not a queue**, and re-pressing this arm buys
nothing that is not in §2 below.

## 2. The cells

The command, as it went out and as it was read back:

| cell | value | reading |
| --- | --- | --- |
| `_sta_op` / `_sta_arg` / `_sta_arg_wrote` | `13` / `0x00010000` / `0x00010000` | CMD13, argument `ST_MMC_RCA_1` |
| `_sta_word` / `_sta_word_read` | **`0x00000d1a`** / `0x00000d1a` | **CMD13's command word, opcode 13 << 8 | the flag byte `0x1A`** — the word 822 predicted and the source asserts |
| **`_sta_flags_driver`** | **`0x00000195` = 405** | **`mmc_ops.c:479`'s own flags, on the wire** |
| `_sta_flags` / `_sta_flags_mapped` | `0x00000015` = 21 / **`0x0000001a` = 26** | the ladder's `ST_MMC_RSP_R1` beside **what `sdhci_cmd_to_flags` makes of 405** — see §5 |
| `_sta_sent` / `_sta_calls` / `_sta_done` | `1` / `1` / `1` | one command, one body, one exit |

The completion:

| cell | value | reading |
| --- | --- | --- |
| `_sta_complete` | `0x00000001` | `INT_STATUS` bit 0, `SDHCI_INT_RESPONSE`, latched |
| `_sta_err` | **`0x00000000`** | **none of the four command-error bits, and `SDHCI_INT_INDEX` is enabled** — the block's own comparator ran and matched 13 |
| `_sta_timeout` / `_sta_any_polls` / `_sta_polls` | `0` / `0x000004ed` = 1,261 / 1,261 | the interrupt latched on the **first** poll |
| `_sta_inhibit_seen` / `_sta_inhibit_after` / `_sta_inhibit_timeout` | `0` / `0` / `0` | **the block was never inhibited** — the word `0x0D1A` does not hang it |
| `_sta_ena_wrote` / `_sta_ena_held` | `0x000f0001` / `0x000f8001` | the window held bits 0 and 16–19, **bit 19 = `SDHCI_INT_INDEX` among them** |
| `_sta_stale` / `_sta_clear_wrote` / `_sta_clear_after` | `1` / `1` / `0` | `stale` is `INT_STATUS` as found — the latch's baseline |
| `_sta_tout_ctl` | **`0x00`** | `TIMEOUT_CONTROL` is still read-only-rung territory: **no press of this ladder has ever written it** |

The response, and the two states that are the answer:

| cell | value | reading |
| --- | --- | --- |
| `_sta_resp_pre` / `_sta_raw_pre3` | **`0x00000700`** | **CMD7's own R1, still in `RESPONSE + 0`** — the precondition, and the first this ladder has had that is genuinely the format it is decoded as |
| `_sta_pre_state` | **`0x00000003`** | `R1_CURRENT_STATE` = **3 = `R1_STATE_STBY`** (`mmc.h:150`) — the state CMD7 *answered from* |
| `_sta_pre_ready` / `_sta_pre_illegal` | `1` / `0` | `R1_READY_FOR_DATA` set; the card had not refused CMD7 |
| `_sta_resp` / `_sta_resp_post` / `_sta_raw_post3` | **`0x00000900`** | CMD13's fresh R1 |
| **`_sta_state`** | **`0x00000004`** | **4 = `R1_STATE_TRAN`** (`mmc.h:151`) — **the card is in the TRANSFER state** |
| `_sta_ready` / `_sta_illegal` / `_sta_switch_err` / `_sta_exception` | `1` / `0` / `0` / `0` | ready for data; **the card did not refuse CMD13**; no switch error, no exception event |
| `_sta_resp_moved` / `_sta_resp_is_arg` | `1` / `0` | `RESPONSE + 0` moved `0x00000700` → `0x00000900`, and not to the argument |
| `_sta_rsp_present` / `_sta_resp_read` | `1` / `1` | the response was read |
| `_sta_state_held` | **`0x00000000`** | the two readings **DIFFER** — see §4, which is the one row this press falsifies |

## 3. The reading: TRAN, and the argument that attributes it

Three of this ladder's own cells, across **two independent boots**, and they walk the card's state
machine in order:

| command | cell | boot of the rung-36 press | this boot | `R1_CURRENT_STATE` |
| --- | --- | --- | --- | --- |
| CMD3 | `_nidx_state` | `0x02` | `0x02` | **2 = `R1_STATE_IDENT`** |
| CMD7 | `_sel_state` | `0x03` | `0x03` | **3 = `R1_STATE_STBY`** |
| **CMD13** | **`_sta_state`** | (no CMD13) | **`0x04`** | **4 = `R1_STATE_TRAN`** |

**IDENT → STBY → TRAN, reproduced across two boots, one step per command.** And 821 §5's objection to
reading CMD3's `3` as *"CMD7 landed"* does not reach here, for a reason that is worth stating exactly:

> `R1_STATE_STBY` is also what a CMD7 that took **no** effect would leave. **CMD13's `4` is not**, because
> between the sampling that produced the `3` and the sampling that produced the `4` **exactly one
> command was put on the bus** — CMD13 itself, which cannot move a card — and the transition STBY → TRAN
> has exactly one producer in the JEDEC state machine: **a completed `SELECT_CARD`.**

**So CMD7 landed.** And the two readings are not two observations of a coincidence: they are the same
field of the same format, sampled at two moments, with a known and complete set of commands between
them. 821 §5 said *"what is needed is not another reading of this cell — it is a command whose own
answer is the state field"*, and this press is that command, used as the compare.

**What the press does not do is read a block.** TRAN is the state in which data commands are legal; it
is not a transfer. No CMD8, no CMD16, no CMD17/18, no filesystem, no mount — §8.

## 4. The row this press falsifies, and why the falsification is the finding

Rung 37's source pre-registered **`_sta_state_held = 1` as the expected reading**, with this reasoning:

> *CMD13 reads the card's status WITHOUT changing it, so a press in which `_sta_state` equals
> `_sta_pre_state` is one where the card reported the same state twice, a few microseconds apart, across
> a command that cannot move it … `1` is the expected reading and is NOT a defect.*

**Measured: `_sta_state_held = 0`.** The premise is right and the conclusion does not follow from it,
because the two readings are **not** two samples of one moment:

* `_sta_pre_state` is the field of **CMD7's response** — the state the card was in **when it answered
  CMD7**, before CMD7's own effect was applied.
* `_sta_state` is the field of **CMD13's response**, taken after that effect.

**A command that cannot move the card can still read a different value, because the field is sampled at
response time and something else moved the card in between.** Here that something is CMD7 itself. The
pre-registration's error is `[[mi4-one-value-two-definitions]]` one level down: *"the state field"* was
read as *"the card's state now"*, when it is *"the card's state when this response was formed"*. It is
the **same** error 819's table made about `CSD_STRUCTURE`, and the same one 821 §5 caught CMD3 with —
and this press turns it into the measurement rather than the caveat.

**A `0` in `_sta_state_held` is therefore a transition, not a defect** — and if a later arm ever reads it
as `1`, that will mean the two commands observed the same state, which given §3's chain is itself worth
explaining.

## 5. The SPI flag word, measured on the wire

822 §2 argued from `mmc_ops.c` that the driver's CMD13 flags are `0x195` and that the block reads five
bits of them, so 405 and 21 produce one command word. **All three numbers are now cells:**

| cell | value | what it is |
| --- | --- | --- |
| `_sta_flags_driver` | `0x00000195` | `mmc_ops.c:479`'s `MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC` |
| `_sta_flags_mapped` | `0x0000001a` | **what `sdhci_cmd_to_flags` makes of it** — the two SPI bits dropped, `0x1A` left |
| `_sta_word` / `_sta_word_read` | `0x00000d1a` | opcode 13 << 8 | `0x1A` |

**The upper two bits of 405 are read by nothing, and that is now a measurement rather than a reading of
`sdhci.c:1140-1143`.** `_sta_flags = 0x15` is the ladder's own `ST_MMC_RSP_R1` and it is a *different
number from the one on the wire* — which is exactly why the two build clauses must assert different
immediates (`xnu_entry_819` requires 21 on CMD7's body, `xnu_entry_822` requires 405 on CMD13's).

## 6. The three upper `RESPONSE` registers did not move this time

821 §6 recorded, as unexplained, that CMD7's completion moved **all four** `RESPONSE` registers and that
the three upper ones came to hold the CSD's **assembled** words — including an `0x1e` in
`RESPONSE+0x0C`'s low byte that is in no cell and no comment in this tree.

**On this boot they did not move during CMD13:**

| register | `_sta_raw_pre*` | `_sta_raw_post*` |
| --- | --- | --- |
| `RESPONSE + 0x0C` | `0x8a40401e` | `0x8a40401e` |
| `RESPONSE + 0x08` | `0xffffffef` | `0xffffffef` |
| `RESPONSE + 0x04` | `0x0f5903ff` | `0x0f5903ff` |
| `RESPONSE + 0x00` | `0x00000700` | **`0x00000900`** |

and the three carried-over values are **exactly** this boot's `_csd_resp3/2/1` — the CSD's assembled
words, `0x1e` and all. **So 821 §6's movement is a settling and not a per-command behaviour**: at some
point in each boot the three upper registers take the CSD's assembled values and then stop, and only
`RESPONSE + 0` moves per command. That narrows the unexplained observation without explaining it — the
`0x1e` is still in no cell in this tree, and this arm still cannot tell a block behaviour from an
artifact.

**What the press does add is a negative that constrains the next reader**: a 48-bit response moves
`RESPONSE + 0` **alone**, on this block, twice in two boots. Saying *"the registers moved, so the frame
was read into all of them"* is true only of a 136-bit format.

## 7. Containment, and what the press cost the machine

| | |
| --- | --- |
| storage cells | **611 → 673** |
| keys lost | **0** |
| keys added | **62 — the entire `_sta_*` family, and nothing else** |
| values identical | **585** |
| values moved | **26**, and every one is a free-running quantity |

The 26 moved are poll counts, tick counts, the two absolute power-IRQ timestamps (`_pwr_irq_at`,
`_pwr_wait_t0`), `_pwr_wait_ticks` and the clock-stability counts `_clk_set_cc_polls` / `_clk_set_cc_ticks`
(6 → 19 and 92 → 193 ticks — a poll whose length depends on when the block reports stable). **Not one
decision cell moved**: `_cmd1_resp = 0x40ff8080`, `_cid_gate_word = 0xc0ff8080`, `_nidx_resp = 0x00000500`,
`_nidx_state = 2`, `_csd_structure = 3`, `_csd_mmca_vsn = 4`, `_csd_capacity_blocks = 0x00200000`,
`_sel_state = 3`, `_sel_word = 0x071A`, `_sel_illegal = 0`, `_sel_err = 0`, `_sel_complete = 1` and the
CID's four assembled words are **identical across the two boots**. Whole-log cells: 1,525 → 1,587. The
OS came up exactly as 818 measured it — `BSD root: md0, major 2, minor 0`, the init load returned,
pid 1's AST put it in user mode.

**Nothing on the device changed.** `fastboot boot` only, no flash, no storage write; CMD13's whole
surface is the registers CMD7's body already touches, and `TIMEOUT_CONTROL` is read and still `0x00`.

## 8. What follows: the data path, and where the command ladder stops

**The frontier is the DATA PATH.** `mmc.c:1436`'s `mmc_select_card` has landed; the driver's own next
statement is `mmc.c:1447`'s

```c
err = mmc_get_ext_csd(card, &ext_csd);
```

which is **CMD8 `SEND_EXT_CSD`** — a **512-byte data read**. 821 §8 said CMD8 is where this ladder stops
being a command ladder, and this press does not change that: the image has no `sdhci_prepare_data`, no
block-size register write, no DMA or PIO path, and `TIMEOUT_CONTROL` has never been written on any rung.
Those are mechanisms, and the next rung is the first one that needs one.

| candidate | what it would settle | cost |
| --- | --- | --- |
| **CMD16 `SET_BLOCKLEN`** | the block size for every later read — the driver's own `mmc_set_blocklen`, one 48-bit command, no data phase | **the cheapest first step into the data path**, and one command of the shape this ladder has now proved four times |
| **CMD8 `SEND_EXT_CSD`** | the card's real density, and the driver's own next statement | a 512-byte DATA read: block size, `TIMEOUT_CONTROL`, a transfer mode |
| putting the INDEX bit back on **CMD3** | an attributable CMD3 (821 §8) | one bit, and now a change with a measured precedent |
| **the page-move structural repair** | the runner's own owed item: `EXIT_POP_LR_LITERAL` is a fallback that must be edited per arm | a step, not an edit — the gate's clause must accept a labelled absence |

**CMD16 is the honest next rung**: it is one command, it needs no transfer mechanism, and it is the
driver's own prerequisite for the one that does.

## 9. The goal

**THE GOAL IS NOT MET.** 「把基础驱动跑起来」 took its largest single step so far — **the card is in
TRAN**, its state machine has been walked command by command with attributable responses, and the
frontier is now a mechanism rather than a question — but **no transfer completes, no filesystem is
reached and no mount is made**, so 「让os可以正常启动并且挂载存储」 is not reached and
**TWRP-TO-STORAGE STAYS WITHHELD**, for 818 §5's reason: the storage it would be written to is the one
thing this ladder has not got working. The first clause, 「起码要能进入操作系统」, remains met and is
unchanged by this press (§7).

**AND NOTHING IS ARMED.** No build has been made since this press; `out/` holds the spent rung-37 park;
no arm is armed and no press is owed or authorized.
