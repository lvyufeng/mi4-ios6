# 771: the four commands read as ONE table — 768's `INT_ENABLE` attribution is refuted, and 737's window rationale with it

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER.** The live
arm in `out/` is still 770's `armed-storage-f72e9f18` (`STAGE90_XNU_STORAGE_PROBE=24`), **ARMED AND NOT
PRESSED**; rung 23's `armed-storage-3a92aa52` stays a superseded, unspent park. **Nothing in this
document was measured on hardware by this step.** It is a re-reading of one already-archived capture —
`out/stage90/captures/rung24-cmdline-20260927-090636-last_kmsg.txt`, 631,581 B, the rung-24 press — and
every number below is a key that capture already carries.

## 1. What this document is for

768's headline was **"the card is silent, the controller is exonerated"**, and its explanation of the
rungs-13–22 negative was one sentence: *this controller does set its status; it did not, because the
error-enable half of `INT_ENABLE 0x34` was zero*. That sentence is in the press path
(`tools/verify_press_ready.sh`, `rung_para 23`) and a reader who trusts it will spend the next arm on the
enable register.

**No arm has ever printed the ladder's four commands side by side.** Each command's cells are published
under its own prefix (`cmd0_`, `cmd1_`, `cid_`, `nidx_`), and every rung since 13 has read exactly one of
those rows and compared it against a sentence about another. Read as one table, in the order the commands
were issued, **two live explanations do not survive**, and the cell that does track the outcome is one the
ladder has been computing since rung 12 and reading one row at a time.

## 2. The table

All four rows are from the one capture. `INT_ENABLE` is the value in `0x34` when the command was put on
the bus; `inhibit_after` is `CMD_INHIBIT` one read after the store to `COMMAND 0x0E`; `inhibit_seen` is
the count over polls 1..1024.

| | **CMD0** `GO_IDLE_STATE` | **CMD1** `SEND_OP_COND` | **CMD2** `ALL_SEND_CID` | **CMD3** `SET_RELATIVE_ADDR` |
| --- | --- | --- | --- | --- |
| word written | `0x0000` | `0x0102` | `0x0209` | `0x030A` |
| response demanded | none | 48-bit | 136-bit + CRC | 48-bit + CRC |
| `INT_ENABLE` written before it | **`0x00000001`** (`ena_wrote`) | **none** | **`0x00000001`** (`cid_ena_wrote`) | **`0x000F0001`** (`nidx_ena_wrote`) |
| `inhibit_after` | `1` | **`0`** | **not published** | `1` |
| `inhibit_seen` (of 1024) | **536** | **0** | **0** | **1024** |
| `inhibit_last` bit 24 (CMD line) | `0x01f80000` HIGH | `0x00f80000` **LOW** | `0x01f80000` HIGH | `0x01f80001` HIGH |
| `word_read` after the store | `0x0000` (consumed) | `0x0102` (held) | `0x0209` (held) | `0x030A` (held) |
| `status_any` over the poll | **`0x00000001`** | **`0x00000000`** | **`0x00000000`** | **`0x00018000`** |
| polls | `0x219` = 537 | `0x4dac00` = 5,090,304 | `0x4db000` = 5,088,256 | `0x6fd` = 1,789 |
| `ticks` | `0x1328` | `0x15f9927` | `0x15fa1b7` | `0x31e3` |
| the poll's own end | completed | **step bound exhausted** (`timeout = 0`) | **tick budget** (`timeout = 1`) | break on `TIMEOUT` |
| outcome | `complete = 1` | nothing latched, 1.2 s | nothing latched, 1.2 s | `err = 0x00010000` at **665.2 µs** |

Two readings sit beside the table and are the reason it can be read as one:

- **`reg_clock_control = 0x0003` at rung 3, `clk_set_cc_after = 0x0000E045`, `cmd2_clock_control = 0x0000E047`.**
  Bit 2 (`SD_CLOCK_CARD_EN`) is clear at rung 3 and set from rung 6 on, bit 1 (`INT_STABLE`) is set in the
  CMD2-era read, and the divider is `0x1E0` = 480 against `clk_set_max_clk = 0x16E36000` = 384 MHz and
  `clk_set_real_div = 0x3C0` = 960 → **384e6 / 960 = 400,000 Hz exactly**. So every row above ran with the
  card clock enabled and correct, which is what makes the table a statement about the *commands*.
- **`cmd2_power_control = 0x0B` with `cmd2_power_bus = 1`** — the 1.8 V bus-power byte is on for all four.

## 3. Refutation 1 — the enable word is neither necessary nor sufficient

**`INT_ENABLE` was `0x00000001` when CMD0 was issued and `0x00000001` when CMD2 was issued** — the same
word, bit for bit, written by two different windows (`ena_wrote` by rung 14's, `cid_ena_wrote` by rung
16's). Both are `int_enable | SDHCI_INT_RESPONSE` with `int_enable = 0`, and both read back `0x00008001`
(`ena_held`, `cid_ena_held`), because bit 15 of that register reads 1 after any write to it (736's
measurement).

**One of them completed and latched `INT_RESPONSE`. The other latched nothing across 5,088,256 polls.**

So the enable word cannot be the reason the rungs-13–22 negative held. It is also not the reason CMD0 and
CMD3 *did* latch: CMD3 ran with `0x000F0001` and CMD0 with `0x00000001`, and the two agree only in being
non-empty. **The variable does not separate the rows in either direction**, and a sentence that names it
as *the* reason is a claim about a register where the capture supports a claim about a moment — **m737's
shape**, standing in the press path.

**What 768's own evidence actually was:** `_nidx_status_pre = 0x00018000` — `ERROR | TIMEOUT` — read
between the enable store and its readback. That reading is real and this document does not disturb it.
What it cannot carry is the attribution, because **CMD2's own window closed with `cid_status_post = 0`**
and the only write between that read and `_nidx_status_pre` is the enable store itself. Two readings of
`0x00018000` survive and neither is "the enable was off": *(a)* it arrived during CMD2 and the block
latches irrespective of the enable — in which case CMD2's own `status_any = 0` over 5.09 M polls is the
puzzle; or *(b)* it is the enable store's own effect on this block, which 736 already showed for bit 15 of
the sibling register. **Separating (a) from (b) is one read and is named in §6.**

## 4. Refutation 2 — the rung-16 window's rationale is refuted as a prediction

`src/entry/entry_storage.c:3024-3031` states why CMD2 is sent inside an enabled window, and the argument is
a prediction: *a CMD2 sent the way CMD1 was would answer the same way — the response would arrive and
nothing would latch — and this arm would learn nothing it does not already have.*

**CMD2 was sent inside the window, and `cid_status_any = 0`, `cid_complete = 0`, `cid_err = 0`.** The window
bought CMD2 nothing it did not already have. `cid_inhibit_seen = 0` and `cid_inhibit_last = 0x01F80000`
say why in the block's own terms: **CMD2 is the third row on which the block never raised
`CMD_INHIBIT` at all** — it never acknowledged the command, so there was no completion for any enable to
deliver.

**The window is not being removed by this document** — it is part of a pressed arm's evidence, and 770's
rule is that a narration of a spent arm is corrected and an act is not. What is corrected is the sentence
that says what the window *buys*: it buys one bit of enable for a command whose problem is upstream of the
enable register.

**The same paragraph's other half is already retired and the paragraph does not say so.** It reads
`_cmd1_resp = 0x40ff8080` as *WAS ANSWERED BY THE CARD … a valid OCR with the voltage window in bits
23:15*. **769 §5.3/§7 retired `RESPONSE` as a cell about the card in both directions**, and this capture
supplies the third datum for that table: rung 19 `0x40ff8080 → 0`, rung 20 (`MMC_RSP_NONE`)
`0x40ff8080 → 0`, **rung 24 `0 → 0x40FF8080`** — the direction tracks what the field held before the
command, not what the command asked for.

## 5. What the table DOES support, and it is one cell

**`CMD_INHIBIT`'s rise tracks the outcome on all four rows**: 536 → completed; 0 → nothing; 0 → nothing;
1024 → the block armed and fired its own response timeout. **`inhibit_after` — the same bit, one
instruction after the store — agrees on the three rows that publish it: `1`, `0`, `1`.**

That is the block's own acknowledgement that it took the command, and it is the reading 724 §3 introduced
this cell for. **No rung has ever read the four rows together, and the table makes the cell exact**: the
question "did the block start this command?" is answered before the first microsecond, and every other
cell in the row is downstream of it.

**And the fourth row's `inhibit_after` is measured and thrown away.** `st_all_send_cid` fills
`c2.inhibit_after` — `st_send_command` writes it for every command — and the publish block at
`entry_storage.c:3079-3092` prints fourteen of `c2`'s fields and **not that one**. `cid_inhibit_after` is
absent from **every** capture in the archive. It is the one cell the table is missing, and **publishing it
costs no device access at all**: the value is already in the struct.

**A third correction, one number wide, and it is in the same sentence.** That paragraph writes CMD1's poll
count as `_cmd1_polls = 0x004db000` = 5,088,000. The capture's `cmd1_polls` is **`0x004DAC00` = 5,090,304**
— the hexadecimal `0x004db000` is CMD2's `cid_polls` (`0x004DB000` = 5,088,256, to which the decimal is
also not equal). The sentence's point is unmoved by the number, which is exactly why nobody has checked it:
**a count quoted for one command and taken from another** is the smallest instance of the class this
document is about.

## 6. What the table cannot separate, and the two reads that would

**The table says which commands the block refused. It does not say why** — and the rows exclude the
obvious answers. CMD1 and CMD2 are the two the block never started, and they differ from each other in
opcode, response width (`0x0102` short / `0x0209` long), CRC bit (clear / set), and enable word (empty /
`0x00000001`); they agree only in **argument `0`** — which CMD0 also carries, and CMD0 was started. CMD3
is the one the block did start, and its argument is `0x00010000`, its word `0x030A`, its enable
`0x000F0001`. **No cell in the row separates CMD2 from CMD0, and they ran with the same enable word and
the same argument.**

Two candidates survive, and each has a read:

- **the block held the command and dropped it**, or **the block never loaded it**. `inhibit_after` is
  taken a few cycles after the store, when the block may not have reacted; **one `PRESENT_STATE 0x24` read
  after the poll loop ends** — outside it, so the poll's traffic is unchanged — says which. For CMD1 and
  CMD2 it reads `0` on the first candidate and `1` on the second, and `inhibit_last`'s bit 24 beside it
  says whether the line was still LOW at 1.2 s.
- **`_nidx_status_pre = 0x00018000` is CMD2's or the enable store's.** One `INT_STATUS 0x30` read
  immediately **before** the enable store and one immediately **after** it, with the pre-read's own value
  published, separates them.

**Both are one read and zero stores, and both are pre-registered here and NOT built.** A rung carrying
them is a normal host-side build; spending it is the operator's decision, and rung 25's pad read is
ahead of them in the queue.

## 7. Two readings beside the table that no rung has quoted

- **`ena_host_version = 0x00001102`** — the `HOST_VERSION 0xFE` halfword rung 14 took, and the low byte
  is `0x02`: **this block reports SDHCI specification 2.00**. The comment that introduced that cell says a
  3.00-or-later block is the reading under which `INT_STATUS`'s latching behaviour is described at all. The
  register answers 2.00. Any argument in this ladder that rests on "the specification says X about
  latching" has to say which specification the block says it is.
- **CMD1 and CMD2 ended for different reasons.** `cmd1_timeout = 0` with `cmd1_polls` an exact multiple of
  the inner count — the outer step bound ran out. `cid_timeout = 1` with `cid_polls` one inner loop short
  of it — the tick budget fired. So the ladder's "1.2 s budget" is enforced *between* inner loops and can
  overshoot by one; the two rows are not the same experiment.

## 8. What this document does not say

- **It does not say the card answered, or that it did not.** `RESPONSE` is retired (§4) and the table is
  about the block. The card's silence is where 768 left it.
- **It does not re-open 764 §1 or 768 §4**, and it does not move the safety contract.
- **It does not say the enable register is irrelevant to *interrupt delivery*.** It says it is not the
  discriminator between these four rows, which is a claim about `INT_STATUS` and not about `SIGNAL_ENABLE`
  (`cmd2_sig_enable = 0`, `cid_sig_enable = 0`, `nidx_sig_enable = 0`, no `irq_other_*` key anywhere).
- **It does not spend a press and it does not authorize one.** Reading an archived capture is host-side.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount, no driver.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist, and 「让os可以正常启动并且挂载存储」 is
not reached, so **TWRP-to-storage stays withheld.**
