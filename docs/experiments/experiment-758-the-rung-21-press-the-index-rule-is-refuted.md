# 758: the rung-21 press — the `INDEX` RULE IS REFUTED, and the block's answer is that the response DEMAND is the whole of the stall

**One press spent, with the operator's authorization, on `armed-storage-46fe6737` (`STAGE90_XNU_STORAGE_PROBE=20`,
entry ordinal rung 21).** `fastboot boot` only, never flash; nothing written to storage; neighbour `33e80afe`
absent from both the adb and fastboot lists before firing. Exactly one gate and exactly one runner, with the
flags readiness printed. **EXIT 0 — returned and captured, 29 s after the send.**
Capture `out/stage90/captures/rung21-noidx-20260927-last_kmsg.txt`, 639,147 B, sha256
`25512586ae087d17210b33debe6fb1d5217d2e21681ab137b90be3ca745a385b`.

**The one-line answer.** The arm asked whether a word that demands a response and carries no `INDEX` bit is
**declined**. It is not. `0x030A` was **accepted and started** — `_nidx_inhibit_seen = 0x00000400`, all 1024
of 1024 samples, `_nidx_inhibit_after = 1`, `_nidx_inhibit_last` bit 0 STILL SET at the last sample — and
then never finished: `_nidx_complete = 0`, `_nidx_status_any = 0` over `_nidx_any_polls = 0`, `_nidx_timeout = 1`,
`_nidx_ticks = 0x015f91be` = 1.20002 s. **`0x030A` and `0x031A` are one bit apart on the bus and the block
cannot tell them apart**, in any stopping cell. So the rule that fit all five words is dead, the `INDEX` bit
is not a discriminator, and what is left standing is the arm's second branch: **the response demand itself.**

## 1. What was fired, and the pre-press reading

Readiness refused the first time it was run against this arm — `1 of 5` — because **747 armed the arm and
never wrote its narration**: the branch chain stopped at `wst == 19`, so value 20 fell to the else arm's
sentence and this row would have printed 693's name. That is the 698 defect exactly, caught by the check
written for it. The narration was written (commit `89525c6`), readiness went **5 of 5, exit 0**, and only
then was the press spent.

The gate ran once, exit 0, with `--allow-xnu-entry`; the runner ran once, exit 0:

    Sending 'boot.img' (8372 KB)     OKAY [  0.264s]
    Booting                          OKAY [  0.011s]
    the bytes sent were the bytes the gate read: 3f11e29b80c259ff…, unchanged across the send
    the device came back 29s after this run called `fastboot boot` (seen via: adb)

## 2. The measurement: six words on one bus, four of them in this single capture

This is the ladder's first press whose log carries **both** command shapes that matter, and it is why the
comparison below is one capture and not four. The rows from other arms are marked.

| cell source | word | opcode | flags | demand | `inhibit_seen` | `inhibit_last` | `complete` | `status_any` | `ticks` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `_cmd0_*` | `0x0000` | 0 | `0x00` none | no | `0x219` (537/538) | `0x01f80000` bit 0 **clear** | **1** | **1** | 0.2553 ms |
| `_cmd1_*` | `0x0102` | 1 | `0x02` SHORT | yes | **`0`** (0/…, `_cmd1_any_polls = 0`) | `0x00f80000` bit 24 **clear** | 0 | 0 | 1.20001 s |
| `_cid_*` | `0x0209` | 2 | `0x09` LONG\|CRC | yes | **`0`** (0/…) | `0x01f80000` | 0 | 0 | 1.20018 s |
| **`_nidx_*` (this press)** | **`0x030A`** | **3** | **`0x0A` CRC\|SHORT** | **yes** | **`0x400` (1024/1024)** | **`0x01f80001` bit 0 SET** | **0** | **0** | **1.20002 s** |
| `_rca_*` (rung 19) | `0x031A` | 3 | `0x1A` INDEX\|CRC\|SHORT | yes | `0x400` (1024/1024) | `0x01f80001` bit 0 SET | 0 | 0 | 1.20004 s |
| `_nrsp_*` (rung 20) | `0x0300` | 3 | `0x00` none | no | `0x21a` (538/539) | `0x01f80000` bit 0 clear | **1** | **1** | 0.2554 ms |

**The two rows in bold type are the finding.** `0x031A` (INDEX set) and `0x030A` (INDEX clear) agree on
**every** stopping cell — the inhibit bit seen in all 1024 samples, the inhibit bit still set at the end,
no completion, no status bit across the whole register, one full 1.2 s bound. One bit differs on the bus
and nothing differs in the block's response. **The `INDEX` bit is not what the block is declining, because
the block did not decline this word at all.**

**And the two words that were NOT started are the interesting remainder.** `0x0102` and `0x0209` — CMD1 and
CMD2 — were declined in the rung-20 press and are declined again here, in the same run that started
`0x030A`. So the split in this capture is:

    started, completed   `0x0000`  (opcode 0, no demand)
    started, never done  `0x030A`  (opcode 3, demand, CRC, no INDEX)
    never started        `0x0102`  (opcode 1, demand, no CRC)
    never started        `0x0209`  (opcode 2, demand-with-136-bit, CRC)

**No single field of the command word separates the two started rows from the two not-started rows** —
that is §4, and it is the question this press opened rather than closed.

## 3. The arm's own three-way, resolved against its own published prediction

747's narration gave three outcomes and named the next act for each. **The second one fired:**

> `_nidx_inhibit_seen > 0` with `_nidx_complete = 0` means the block ACCEPTED and STARTED a word that asks
> for a response and carries no `INDEX`, which kills the rule's `INDEX` half and puts the subject back on
> the response itself.

`_nidx_inhibit_seen = 0x00000400` with `_nidx_complete = 0x00000000`. **Measured, not inferred.**

**And the rule's death is clean because the word was read back out of the block.** `_nidx_word = 0x0000030a`
and `_nidx_word_read = 0x0000030a` — the block held the arm's own word, so this is not a store that failed
to land. `_nidx_sent = 1`, `_nidx_gated = 0`, `_nidx_calls = 1`, `_nidx_done = 1`.

**The window behaved exactly as every rung since 13's has**, which is the arm's own declared safety and is
worth stating because it did not move: `_nidx_ena_wrote = 0x00000001`, `_nidx_ena_held = 0x00008001`,
`_nidx_readback = 0x00008000`, `_nidx_wrote_back = 0x00000000` — one store and one unconditional restore —
`_nidx_sig_enable = 0x00000000` (read and never written), `_nidx_status_post = 0x00000000`.

**The ending is unmoved**, as the arm promised: `_seam_sctlr = 0x30c57879`, `_seam_post_end_ticks = 0x06ddd000`,
`_post_end_calls = 0x00000006`, `_sleh_storm = 0x00000009` — every one the value the rung-20 press carried.

**The failure mode the arm declared did not fire, and it was declared anyway.** The narration said a
delivery on this block's line would end the run at the dispatcher. It did not: `EXIT 0`, 29 s, the capture
whole. That is the shape the project wants a rung to have — the bad ending named in advance, so its absence
is a reading rather than a relief.

## 4. The response register: 746's repair earned its keep on its first press

The arm carried **m756's repair** — its pair is one `readl(RESPONSE 0x10)` at both moments, where rungs 19
and 20 took theirs through the 136-bit formula whose top word is a different offset — and it published the
four raw words at **both** moments so a reader can check the claim instead of trusting it. **The check is
what found the next thing.**

| arm | `…_resp_pre` | `…_resp_post` | `…_resp_moved` |
| --- | --- | --- | --- |
| `_rca` (rung 19, `0x031A`) | `0x40ff8080` | **`0x00000000`** | 1 |
| `_nrsp` (rung 20, `0x0300`) | `0x40ff8080` | **`0x00000000`** | 1 |
| **`_nidx` (this press, `0x030A`)** | **`0x00000000`** | **`0x40ff8080`** | **1** |

and this press's raw words, which no earlier arm published:

    pre :  0x1C=0x0040ff80   0x18=0x80000000   0x14=0x00000000   0x10=0x00000000
    post:  0x1C=0x00000000   0x18=0x00000000   0x14=0x00000000   0x10=0x40ff8080

**Read the two together and it is one fact stated twice.** `0x40ff8080` is the value 733 read as CMD1's OCR
word. In this press it moved from word 0 being **empty** (with the same byte pattern sitting one word up,
as `0x0040ff80`/`0x80000000`) to word 0 holding it — while in rungs 19 and 20 it moved the other way, from
word 0 holding it to word 0 empty. **Same value, same register, opposite directions on two arms one flag
bit apart, and the direction correlates with nothing this document can name.**

**What is NOT claimed**: that anything answered. `_nidx_resp_is_arg = 0x00000000` says the word is not this
arm's own argument, and the arm's own caveat applies to its decode — `_nidx_state = 0`, `_nidx_ready = 0`,
`_nidx_illegal = 1` are a decode of `0x40ff8080`, the same word the ladder has been reading since 733, and
738's stale-word trap is exactly this shape. **The decode is published because the arm predicted it would be
of a leftover, and it is.** What moved the register is the open question, not what the register means.

## 5. What this does to the ladder's picture

**746's headline survives and gets narrower.** 746 said *the response demand was the stall*, from a
comparison between commands. This press removes the competing explanation that the rung-19/20 pair had left
open — that the `INDEX` bit, and not the demand, was what the block refused. **`0x030A` has no `INDEX` and
stalls exactly as `0x031A` does; `0x0300` has no `INDEX` and completes in 0.2554 ms.** The variable that
tracks the outcome across all six words is `RESP_PRESENT`, and it is now the only one standing.

**And the four-arm match is a matched set, not four samples.** `_rca_inhibit_seen = 0x400` and
`_nidx_inhibit_seen = 0x400`; `_rca_inhibit_last = 0x01f80001` and `_nidx_inhibit_last = 0x01f80001`;
`_rca_ticks = 0x015f92d7` and `_nidx_ticks = 0x015f91be` — 1.20004 s and 1.20002 s, **20 µs apart** on a
1.2 s quantity (a 1.7 ppm agreement). **A block that could tell `0x031A` from `0x030A` would not produce that.**

## 6. The question the press opened: why are CMD1 and CMD2 never started?

This is **not** a claim, and no rung is designed from it. It is the four-row table of §2 read as a
question, recorded so a later step does not have to re-derive it.

Four words in one capture. Two started, two did not:

| | started | not started |
| --- | --- | --- |
| opcode | 0, 3 | 1, 2 |
| `RESP_TYPE` (bits 1:0) | `00`, `10` | `10`, `01` |
| `CRC_CHECK` (bit 3) | 0, 1 | **0**, 1 |
| `CMD_INDEX` (bit 4) | 0, 0 | 0, 0 |
| argument | `0`, `0x00010000` | `0`, `0` |

**Every column splits the set somewhere except `CMD_INDEX`, and `CMD_INDEX` is the field the refuted rule
was about.** The opcode column splits it cleanly — `{0,3}` against `{1,2}` — and so does nothing else; but
an opcode is not a mechanism, and this document does not offer one. **The honest statement is that four
words in one boot separate into two behaviours by a field no other evidence names, and the ladder does not
yet know which.**

**Two facts bound the search, both measured.** (a) The gate that decides whether CMD2 runs at all
(`entry_storage.c:3742`: `c1.sent != 0u && (c1.resp & ST_MMC_CARD_BUSY) == 0u`) **passed** — `_cid_gated = 0`
with `_cid_sent = 1` and `_cmd1_resp = 0x40ff8080`, whose bit 31 is clear — so CMD2 was not skipped by the
ladder; its word is on the bus and readable. (b) `_cmd1_any_polls = 0` and `_cid_any_polls = 0`: neither
word's poll saw a single non-zero `INT_STATUS` sample, so this is the same *never started* the rung-20 press
measured, reproduced.

## 7. What this document does not say

- **It does not say the block is broken.** Everything measured here is consistent with a controller that
  latches a completion only into a register this ladder has not yet read at the right moment; 756 §1 named
  the one vendor write on the sampling path this ladder has never made, and the guess in §6 of that
  document is untouched by this press. **This press did not test it, and does not speak to it.**
- **It does not claim a mechanism for CMD1's and CMD2's silence.** §6 lists the columns and says the ladder
  does not know. A clean split is not an explanation.
- **It does not claim anything answered.** §4 says the opposite: the response word moved, and what moved it
  is unnamed.
- **It does not arm or design a rung.** No firer is armed, no new arm is built, and `out/` still holds the
  arm this press spent. **Any further press is the operator's decision.**
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. It spends the press the ladder
  was waiting on, refutes a rule from the ladder's own cells, and names the question the refutation leaves.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block starts a command and never
completes one — and 「让os可以正常启动并且挂载存储」 is not reached, so **TWRP-to-storage stays withheld.**
