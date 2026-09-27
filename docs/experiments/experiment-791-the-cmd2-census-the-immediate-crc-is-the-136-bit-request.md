# 791: the CMD2 census — the immediate `CRC` is unique to the one command that asks for a 136-bit response, and the narrow arms' zero is a MASKED reading, measured

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER, NOTHING
BUILT.** `out/` was **not** touched and no source was edited, so the spent rung-30 arm's park and every
other park are byte-identical before and after this step. One press spent: **none**. **No press is
authorized and no arm is armed.**

Every number below is read out of a capture already on disk under `out/stage90/captures/`. Nothing is
inferred from the source, and no cell was computed from another cell.

## 1. The census: nine arms carry CMD2's cells, and exactly two of them are non-zero

| capture | ordinal / value | `INT_ENABLE` written at CMD2 | `_cid_status_any` | at poll | `_cid_ticks` |
| --- | --- | --- | --- | --- | --- |
| `rung17-cid` | 17 / 16 | `0x00000001` | `0x00000000` | — | `0x015f9651` |
| `rung18-rb` | 18 / 17 | `0x00000001` | `0x00000000` | — | `0x015f9dfb` |
| `rung19-rca` | 19 / 18 | `0x00000001` | `0x00000000` | — | `0x015f9672` |
| `rung20-nrsp` | 20 / 19 | `0x00000001` | `0x00000000` | — | `0x015f9ceb` |
| `rung21-noidx` | 21 / 20 | `0x00000001` | `0x00000000` | — | `0x015f9dc4` |
| `rung22-dllcensus` | 22 / 21 | `0x00000001` | `0x00000000` | — | `0x015f95d5` |
| `rung24-cmdline` | 24 / 23 | `0x00000001` | `0x00000000` | — | `0x015fa1b7` |
| `rung29-2win` | 29 / 28 | **`0x000f0001`** | **`0x00028000`** | **1** | **`0x00000011`** |
| `rung30-c1win` | 30 / 29 | **`0x000f0001`** | **`0x00028000`** | **1** | **`0x00000011`** |

`0x00028000` is `SDHCI_INT_ERR` (bit 15) `| SDHCI_INT_CRC` (bit 17). **The CRC bit appears in this
ladder's CMD2 history on exactly the two arms whose CMD2 window carried it, and on no other arm.**

## 2. The bracket: the block latches it on being handed the command word

On the two wide arms the ladder brackets the event more tightly than any previous rung, and the bracket
is a reading rather than an argument:

| key | rung 29 / rung 30 | what it is |
| --- | --- | --- |
| `_cid_stale` | `0x00000001` | `INT_STATUS` at CMD2's entrance, **after** its own `INT_ENABLE` store: bit 0 and nothing else |
| `_cid_clear_wrote` | `0x00000001` | the word `st_send_command` wrote back (write-1-to-clear) |
| `_cid_clear_after` | `0x00000000` | `INT_STATUS` immediately after that write: **the latch was CLEAN** |
| `_cid_word` / `_cid_word_read` | `0x00000209` / `0x00000209` | the block's own copy of the command word |
| `_cid_status_any` | `0x00028000` | the **first** poll read |
| `_cid_any_polls` | `0x00000001` | and it is the **first** poll |
| `_cid_ticks` | `0x00000011` = **17** | ticks from the store to that read |

Between `_cid_clear_after` and the first poll read there are **four device accesses and no others**:
`ARGUMENT <- 0` (`:2571`), `COMMAND <- 0x0209` (`:2593`), `COMMAND -> word_read` (`:2618`),
`PRESENT_STATE -> inhibit_after` (`:2619`). **So the block latched `ERR | CRC` between being handed that
command word and the next read of `INT_STATUS`, and it had already accepted the word into its own
`COMMAND` register.**

## 3. And it is far too early for a response — a calibration that needs no clock frequency

`_cmd0_ticks` is the same counter over the same bus in the same boot for `GO_IDLE_STATE`: one 48-bit
command frame with nothing coming back, and it is the one command this ladder has ever seen complete.

| capture | 17 | 19 | 21 | 22 | 24 | 29 | 30 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `_cmd0_ticks` | `0x132d` | `0x132a` | `0x1325` | `0x132d` | `0x1328` | `0x1327` | `0x132a` |
| = | 4,909 | 4,906 | 4,901 | 4,909 | 4,904 | 4,903 | 4,906 |

**Seven arms, a spread of 8 ticks, 0.16 per cent.** So a no-response 48-bit command costs
**4,901–4,909 ticks** on this bus at this clock, on every arm.

**CMD2's 17 ticks is 0.35 per cent of one such command.** The block latched `ERR | CRC` in less than
one three-hundredth of the time a single command frame takes — **before a start bit, a command index and
a CRC could have been shifted out, let alone a 136-bit response shifted in.** This needs no clock
frequency and no frame model: the numerator and the denominator are the same counter, the same boot and
the same bus.

**For contrast, the narrow arms' CMD2 poll ran `_cid_ticks = 0x015f9xxx` ≈ 23,040,000 ticks — about
4,700 `_cmd0` frames — and read zero every time.**

## 4. The isolation: the correlate is the response LENGTH, not the CRC enable

Three commands have now been given an enable wide enough to report an error on this ladder. The command
word is a property of the command and not of the enable, so the words below are comparable across arms:

| command | flags | word | response | CRC check in the word | what the wide window read |
| --- | --- | --- | --- | --- | --- |
| CMD1 `SEND_OP_COND` | `PRESENT` | `0x0102` | 48-bit | **off** | **completed**, bit 0 at poll 1,236, no error bit |
| CMD3 `SET_RELATIVE_ADDR` | `R1_NOIDX` | `0x030a` | 48-bit | **on** (`|0x08`) | `ERR | TIMEOUT` — **and never bit 17** |
| CMD2 `ALL_SEND_CID` | `R2` | `0x0209` | **136-bit** | **on** (`|0x08`) | **`ERR | CRC` at poll 1, 17 ticks** |

**CMD3's word carries the same CRC check as CMD2's** — the `0x08` bit is set in both — and CMD3 latched
`TIMEOUT` without the CRC bit on rungs 24 and 29 (`_nidx_status_any = 0x00018000` at polls 1,789 and
1,493). **So the CRC bit's presence is not explained by the command's own CRC check, and the one thing
CMD2 has that no other command in this ladder has is a 136-bit response request.**

**The missing corner of that table is 136-bit with the CRC check off — one bit — and §7 pre-registers it.**

## 5. The narrow arms' zero is a MASKED reading, measured, and rung 24 is the control

Rung 24 is the one narrow arm whose *next* body opens a wide window, and that makes it the control the
census otherwise lacks:

| rung 24, CMD2's own window | value | read under |
| --- | --- | --- |
| `_cid_ena_wrote` | `0x00000001` | one bit — **no CRC** |
| `_cid_status_any` | `0x00000000` | that one bit, over `_cid_polls = 0x004db000` = 5,091,328 reads |
| `_cid_status_post` | `0x00000000` | `INT_ENABLE` restored to **0** |
| **`_nidx_status_pre`** (the next body) | **`0x00018000`** | the **five-bit** word |

**`ERR | TIMEOUT` was sitting in the latch at the top of the next body, one command after a window whose
poll had read zero 5,091,328 times.** So, at CMD2:

1. **the condition is real on a narrow arm** — the narrow zero is a reading about the *enable*, not about
   the block or the card, which is 788 §3's conclusion for `INT_STATUS` generally and is now a
   *measurement* at this window rather than an inference from other windows; and
2. **`_cid_status_post = 0` does not mean the latch is clean.** It is read after the restore, so it is a
   read through `INT_ENABLE = 0`, and rung 24 proves it can sit over a latched bit.

**That second point is a defect class and it is not confined to this cell.** Every `*_wrote_back` cell in
the archive is `0x00000000` — the ladder's windows all close by storing `int_enable`, which is zero — and
so **every `*_status_post` cell in the archive is read through a zeroed enable**:

| cell | arms carrying it | value on every one |
| --- | --- | --- |
| `_ena_status_post` | 14 onward | `0x00000000` |
| `_cid_status_post` | 16 onward | `0x00000000` |
| `_nidx_status_post` | 20 onward | `0x00000000` |
| `_rca_status_post` | 18/19 | `0x00000000` |
| `_*_readback` | all | `0x00008000` — bit 15, which no write clears |

**Four cells, every one zero, and every one a 100 per cent masked read** — an `m`-class instance worth
naming (*a zero read under an enable that was already restored*). The `_*_readback` beside them is the
tell that the enable write happened at all and is not itself a status reading.

## 6. The one thing this does not separate, named rather than left

On the narrow control the latched value has bit 16 and **not** bit 17; on the two wide arms it has bit 17.
**The command word is `0x0209` on all three**, so the CRC check was on in every case. Two mechanisms are
admissible and the archive cannot choose between them:

- **(i) the enable switches the check on** — no CRC check runs unless CRC is enabled, so on rung 24 the
  block's condition was its own response timeout and no CRC condition existed to latch;
- **(ii) the enable gates visibility only**, and the CRC bit is an *early spurious latch* the block raises
  when a CRC check is enabled on a 136-bit request, which on rung 24 was invisible and which the block
  later superseded or never raised.

**There is a third asymmetry that a reader must not read past**: because the wide arms break at poll 1,
the ladder has **never** observed whether the block goes on to latch `TIMEOUT` under a wide enable at
CMD2. The wide reading is a *first* latch and the narrow reading is a *later* one, and whether they are
one condition seen twice or two conditions is not in the archive.

## 7. The pre-registered arm: ONE BIT OUT OF CMD2'S FLAGS, and its best row reaches the CID

**CMD2 (`ALL_SEND_CID`, opcode 2, argument 0) sent with `MMC_RSP_PRESENT | MMC_RSP_136` — the CRC flag
dropped and nothing else moved.** Same opcode, same argument, same window, same position in the driver's
order, same five-bit `INT_ENABLE`.

| | now | the arm |
| --- | --- | --- |
| flags | `PRESENT | 136 | CRC` = `0x07` | **`PRESENT | 136`** |
| word | `0x0209` | **`0x0201`** |
| all else | — | unchanged |

Dropping the CRC check from a 136-bit response is legal on the wire: the card still returns the CID, the
block simply does not check its CRC. **The four rows, and the first is the one that matters:**

| row | reading |
| --- | --- |
| `_cid_complete = 1` | **THE CID IS TAKEN.** CMD2 completed, the ladder has the card's CID, and the next commands are CMD9 (CSD), CMD7 (select), CMD16 (block length) and then data — **the single biggest step available from here.** |
| `ERR | TIMEOUT` (`0x00018000`) | the CRC latch needs the command's own CRC flag, and the real condition at CMD2 is the block's own response timeout — which moves the subject to why a 136-bit response never arrives and puts the card and the power/clock path back in front. |
| **`ERR | CRC` (`0x00028000`) anyway** | the CRC bit is not about the command's CRC flag at all, **which refutes §4's reading and puts the 136-bit path itself on the block**. |
| nothing, full bound, next wide read shows `TIMEOUT` | the same as row 2 read from the other side, and it separates (i) from (ii) by whether the bit appears at all. |

**And the control is in the same log**: `_cmd0_ticks` and CMD1's own completion bracket the new word from
both ends, exactly as they bracketed rung 30's.

## 8. What this does not do

- **It builds nothing and spends no press.** `out/` is byte-identical, every park is intact, and the arm
  this step prepares is **pre-registered and NOT built**.
- **It does not decide §6's (i)-versus-(ii) question**, and it does not say CMD2's condition is on the card
  rather than on the block. §4 isolates the *correlate*; it does not name the cause.
- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made, so
  「让os可以正常启动并且挂载存储」 is not reached and **TWRP-to-storage stays withheld.**

## 9. Owed, and named rather than left to be inferred

- **The pre-registered one-bit arm of §7** — built by a later step, pressed only by the operator.
- **The `*_status_post` class of §5** — four cells in the archive whose zero is an artifact of the enable
  they were read under. The repair is a read of `INT_STATUS` *before* the restore at each window, which is
  a source change and is COST-owed to a build.
- **Whether the wide enable at CMD2 also latches `TIMEOUT` later** (§6) — the ladder breaks at poll 1 and
  has never looked. One more read after the break would carry it.
- Unchanged from 787–790: the `0x40ff8080` "the card ANSWERED" comment at CMD1's head (COST); a
  `_cmd1_raw_pre*` / `_cmd1_raw_post*` pair around CMD1's own `RESPONSE` registers (COST); `c3.inhibit_timeout`
  not published for the `nidx` family (COST); the four `5,088,000`s and the mis-citation at
  `entry_storage.c:302-303` (COST); a check that counts `ST_LIVE` sites per key (789 §4); the
  `_Static_assert` message's bit map at `entry_storage.c:2360` (789 §5); `_cid_ps_after`'s second producer
  (789 §2, COST); the set-comparison pad repair (779 §7); the `rung_para` correction for values 12..23;
  the seam-address class; `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic
  FDT cell (782 §6); 784's `rail_name` cell; and 783's window-scope check — landed at rung 30's window and
  not at the ladder's other windows.
