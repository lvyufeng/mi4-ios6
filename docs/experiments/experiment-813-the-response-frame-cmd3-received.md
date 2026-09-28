# 813 — the response frame CMD3 received: the capture on record settles the branch 812 left open

**A HOST-SIDE READING. NOTHING WAS BUILT, NOTHING WAS SENT, NOTHING UNDER `out/` MOVED.** Rung 35
(`armed-storage-6cd6fae8`, switch value 34) is **armed and not pressed** and this step does not touch
it. Every number below was read out of two captures already committed to this repository.

---

## 1. The sentence this corrects

812 §3 says, of the word CMD3 left in the response register:

> Whether that is a card that declined the new RCA, or the block eking out a word for a command the card
> never processed, is not settled by any capture on record.

**The second branch is settled, and the capture that settles it is the one 811 was pressed for.** The
sentence was written from the *decision* cells (`_nidx_resp`, `_nidx_state`) without reading the
*command*-level cells that sit beside them in the same log — which is the class this project keeps
meeting: a claim written from the part of the evidence that was already in hand.

## 2. The cells, in the order the command ran

From `out/stage90/captures/rung34-cidgate-20260928-144115-last_kmsg.txt` (811's press) and
`out/stage90/captures/rung33-opcond-20260928-123120-last_kmsg.txt` (803's), **which agree on every cell
below except one** (§4):

| cell | value | what it is |
| --- | --- | --- |
| `_nidx_status_pre` | `0x00000001` | `INT_STATUS` **before** CMD3 went out: `INT_RESPONSE` was **already latched** — a leftover from CMD2 |
| `_nidx_clear_after` | `0x00000000` | the same register after `st_send_command`'s write-1-to-clear, re-read |
| `_nidx_sent` | `0x00000001` | the command was issued (the body returns with `sent = 0` if the clear did not take) |
| `_nidx_status_any` | `0x00000001` | the first non-zero `INT_STATUS` of **any kind**, unmasked — at poll `0x584` = 1,412 |
| `_nidx_complete` | `0x00000001` | `INT_RESPONSE` |
| `_nidx_err` | `0x00000000` | no `INT_CMD_ERR` bit |
| `_nidx_timeout` | `0x00000000` | the poll did **not** run out its budget |
| `_nidx_cmdlow_seen` | `0x000002b0` | **688 of the first 1,024 samples** had the CMD line LOW (`ST_SDHCI_CMD_LINE_LEVEL` clear) |
| `_nidx_inhibit_seen` | `0x00000000` | `CMD_INHIBIT` never sampled set — the command did not stall |
| `_nidx_inhibit_last` | `0x01f80000` | bit 24 set (line high at the last sample), bit 0 clear (not in progress) |
| `_nidx_resp_pre` → `_nidx_resp` | `0x2fe00bb1` → `0x00000500` | **the response register changed**; `_nidx_resp_moved = 1`, `_nidx_resp_read = 1` |
| `_nidx_arg` / `_nidx_arg_wrote` | `0x00010000` | `ST_MMC_RCA_1`, written and read back |
| `_nidx_word` / `_nidx_word_read` | `0x0000030a` | opcode 3, **48-bit response, CRC check on, INDEX check off** |

## 3. Why `_nidx_complete = 1` is this command's bit and not CMD2's

`_nidx_status_pre` reads **1** — a stale `INT_RESPONSE` was sitting in the latch when the window opened.
That is precisely the shape that has fooled this ladder before, so the body does not trust it: it reads
the word, **writes it straight back** (write-1-to-clear), re-reads, and **returns with `sent = 0`** if any
command bit survives (`entry_storage.c:2683-2691`).

`_nidx_clear_after = 0x00000000` and `_nidx_sent = 0x00000001` are the two halves of that check passing.
So the `INT_RESPONSE` the poll found at iteration 1,412 is a **new** one.

**This is worth recording on its own account**: the arm's own anti-stale discipline was exercised on
hardware — a stale latch really was present — and it separated the two correctly. The ladder has a
memory about exactly this trap ([`mi4-the-136-bit-response-has-one-word-order`]'s sibling class, 738's
stale-word trap), and here the guard worked.

## 4. What the vendor's own IRQ ladder makes of that word

`INT_STATUS` held **`0x00000001` and nothing else** — the unmasked word, so this is a reading of the whole
register and not of a mask. In `sdhci_cmd_irq` (`external/.../host/sdhci.c:2561`):

* `:2574` — `if (intmask & SDHCI_INT_TIMEOUT)` → `-ETIMEDOUT`;
* `:2576` — `else if (intmask & (SDHCI_INT_CRC | SDHCI_INT_END_BIT | SDHCI_INT_INDEX))` → `-EILSEQ`;
* `:2645` — and independently, `if (intmask & SDHCI_INT_RESPONSE) sdhci_finish_command(host);`.

`sdhci_finish_command` (`:1156`) is where the driver **reads the response register** (`:1162-1175`).
So in the vendor's own structure, `INT_RESPONSE` alone — no TIMEOUT, no CRC, no END_BIT, no INDEX — is the
condition under which a response frame is taken to have arrived and is read out.

**Therefore: the block received a response frame to CMD3.** The second branch of 812 §3's sentence — *a
command the card never processed* — is refuted at that end. A command the block never got a frame for
raises `INT_TIMEOUT`, and TIMEOUT is clear.

## 5. What it does NOT settle, said out loud

* **Whether the card is what rang.** The block reports that a frame arrived; it does not report who sent
  it. The reading does not distinguish a card's answer from any other CRC-valid frame on those lines.
* **Whether `0x00000500` is the card's `R1`.** The ladder decodes it as `R1_READY_FOR_DATA` set with
  `R1_CURRENT_STATE = 2` = `R1_STATE_IDENT`, and that decode is the vendor's own (`mmc.h:141`, `:142`,
  `:149`). But the CID had a witness outside the image — the phone's own `ro.serialno` — and this word has
  none. **The decode is arithmetic; that the word is an `R1` is an assumption.**
* **The 688 low samples.** They say the CMD line was low for most of the first 1,024 samples and high at
  the last one. They do not say whether that was the block driving the command or the card driving its
  answer — `CMD_INHIBIT` was never sampled set (§2), and the source's own comment notes that bit 24 is the
  line's **input** level, so a block reading back its own drive counts the same as a card answering.

So the honest statement is narrower than "the card refused" and wider than "nothing is known":
**the command completed, the block reported a response frame with no error of any kind, and the word it
left behind is the one 812 read.**

## 6. What this does to the press that is armed

It sharpens 812 §9's third row from *likely* to *predicted with a mechanism*:

* `_csd_pre_state` is `RESPONSE 0x10` read **between CMD3 and CMD9** (§812 §3), i.e. the state CMD3 left
  behind — and CMD3's own word already says `R1_STATE_IDENT`.
* So the press should read **`_csd_pre_state = 2`**, and CMD9 is legal in STBY (3) and TRAN (4) and **not
  in IDENT (2)**.
* If `_csd_complete = 0` beside `_csd_pre_state = 2`, that is not a failure of CMD9 — it is **the CMD3
  wall, confirmed from inside the same log**, and it names the next subject: **CMD3's acceptance**, not
  CMD7 and not CMD8.

**The press is still the operator's, and it is still worth taking** — it is the only run that reads
`_csd_pre_state` at all, and a prediction that is confirmed from inside the image is worth more than the
same conclusion drawn from a rung below.

## 7. Two more wrong vendor citations, measured here and OWED to the next build

The correction pass in 812 §6 re-measured the citations it had made and found eight wrong. Reading the
same file for §4 above found **five more, all in the response-read block**:

| written | measured | where | what it names |
| --- | --- | --- | --- |
| `sdhci.c:1169` | **`sdhci.c:1162`** | `entry_storage.c:2578` | the `MMC_RSP_PRESENT` guard |
| `sdhci.c:1169-1175` | **`sdhci.c:1162-1175`** | `entry_storage.c:2801`, `:2806`, `:3547` | the whole response-read branch |
| `sdhci.c:1169-1175` | **`sdhci.c:1162-1175`** | `verify_press_ready.sh:757`, `:759` | the same, in two other rungs' narration |

`sdhci.c:1162` is `if (host->cmd->flags & MMC_RSP_PRESENT) {` and `1169` alone is `host->cmd->resp[i] |=`
— so the range that contains *"the response is read only when the command asked for one"* starts at
1162, not 1169. **These are the same class 812 fixed eight of.**

**They are owed to the next build and not to this step, and the reason is COST, not lane.** Every one of
them is a comment in `entry_storage.c` or a narration belonging to another rung, so **none of them prints
for this arm**. Correcting them means editing `entry_storage.c` → an entry rebuild → a payload rebuild →
re-measuring the containment against the spent park → refreshing `out/`, the park and the record — a fifth
full cycle in one turn, spent on comments, **while an arm sits armed and unspent.** The next build cycle
is the next rung's and will carry them.

**And the class has no check.** 808's `check_line_citations.py` reports **NOT-IN-REPO 165** of the live
arm's 237 citations, because `external/` is untracked and `git` — the baseline 808 chose for *has this
site moved* — has no answer there. A vendor citation is unchecked rather than checked-and-green, which is
why thirteen have now been found the slow way. **That gap is the real defect; these five are its
symptoms.**

## 8. What this does not do, and the goal

It does not send anything, it does not build anything and it does not change what rung 35 will do. It
does not close the CMD3 question: it moves it from *"is the card in IDENT because it refused, or because
nothing ever reached it"* to *"a response frame arrived and the frame's own status says IDENT"*, which is
one branch narrower.

**THE GOAL IS NOT MET.** No transfer completes, no filesystem is reached, no mount is made, so
**TWRP-to-storage stays withheld**. Rung 35 is **armed and not pressed**; **the press is the operator's.**
