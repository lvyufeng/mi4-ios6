# 746: the rung-20 press — **the response demand was the stall**

**The arm under test is 743's** `armed-storage-b7e5a18f` (`STAGE90_XNU_STORAGE_PROBE=19`, ordinal rung 20):
the driver's own CMD3 `SET_RELATIVE_ADDR`, opcode 3, argument `0x00010000`, sent through rung 17's one-bit
`INT_ENABLE 0x34` window with `SIGNAL_ENABLE 0x38` at zero — **the same command as rung 19's with one
constant moved**, `MMC_RSP_R1` replaced by `MMC_RSP_NONE` (`core.h:50`, the value 0), so the command word
goes `0x031A` → `0x0300` and the `INDEX` bit goes with the response.

**It answered, and the answer is the first branch of the threeway 743 §7 pre-registered:**
**`_nrsp_complete = 0x00000001` beside `_nrsp_status_any = 0x00000001`** — *the response demand is what
stalled CMD3.* **And it was not a marginal reading: the command finished in 0.255 ms where rung 19's burned
the full 1.2 s bound, and it is the second command this ladder has ever driven to a completion and the
second that asked for nothing back.**

## 1. The press, and its safety contract

| | |
| --- | --- |
| readiness | **5 of 5, exit 0** — flags `--allow-xnu-entry`, set `armed-storage-b7e5a18f`, found by hashing the live `stage90-qcdt.img` (`86c9e5d7…`) |
| the gate | **exactly one** `./scripts/preflight_boot_check.sh --allow-xnu-entry`, **exit 0, 596 lines** |
| the runner | **exactly one** `./scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-b7e5a18f` |
| fired | 2026-09-27 **01:21:21 UTC**, `fastboot boot` only, **nothing flashed**, no firer armed |
| returned | **01:22:35 UTC — `RUNNER EXIT=0`**, 74 s; the device back **28 s** after the send (seen via `adb`) |
| neighbour `33e80afe` | **0 occurrences in both lists at fire time**, and no stray runner, gate or watcher |
| capture | `out/stage90/captures/rung20-nrsp-20260927-012235-last_kmsg.txt`, **630,462 B**, sha256 `9f4473862d313ccc3ae16dd7af7e2da3eae9f14b79199c56f55f5ba6e3f606a5`, archived by hand with its run and gate logs |

**The arm's own safety reading held, and every cell of it is in the log.** Two stores, both to
`INT_ENABLE 0x34`: `_nrsp_ena_wrote = 0x00000001` with `_nrsp_ena_held = 0x00008001` (the partial readback
rung 14 measured — bit 15 is set by a write and not cleared by one), and the restore
`_nrsp_wrote_back = 0x00000000` reading back as `_nrsp_readback = 0x00008000`.
**`_nrsp_sig_enable = 0` — `SIGNAL_ENABLE 0x38` was READ and never written**, which is what keeps this
block's SPI 123 → intid 155 off a line nobody here owns. No data path, no `POWER_CONTROL 0x29`, no GCC
word, no `core_mem` word, no byte of the medium. **There is no `_irq_other_*` key anywhere in the capture
(0 occurrences)**, so the pre-registered delivery did not occur — fourth arm in a row.

## 2. The answer

     xnu_live_storage_nrsp_gated=0x00000000        xnu_live_storage_nrsp_calls=0x00000001
     xnu_live_storage_nrsp_op=0x00000003           xnu_live_storage_nrsp_flags=0x00000000
     xnu_live_storage_nrsp_arg=0x00010000          xnu_live_storage_nrsp_arg_wrote=0x00010000
     xnu_live_storage_nrsp_word=0x00000300         xnu_live_storage_nrsp_word_read=0x00000300
     xnu_live_storage_nrsp_sent=0x00000001
     xnu_live_storage_nrsp_inhibit_after=0x00000001   xnu_live_storage_nrsp_inhibit_seen=0x0000021a
     xnu_live_storage_nrsp_inhibit_last=0x01f80000    xnu_live_storage_nrsp_polls=0x0000021b
     xnu_live_storage_nrsp_ticks=0x00001327           xnu_live_storage_nrsp_any_polls=0x0000021b
     xnu_live_storage_nrsp_complete=0x00000001        xnu_live_storage_nrsp_status_any=0x00000001
     xnu_live_storage_nrsp_err=0x00000000             xnu_live_storage_nrsp_timeout=0x00000000
     xnu_live_storage_nrsp_clear_after=0x00000000     xnu_live_storage_nrsp_done=0x00000001

**Read the four cells that are the answer, and read them as a sequence rather than as four numbers.**

| cell | value | what it says |
| --- | --- | --- |
| `_nrsp_word` / `_nrsp_word_read` | `0x0300` / `0x0300` | the block holds **the driver's own word**, read back out of `COMMAND 0x0E` — the demand really is gone, and this is what `0x031A` minus `RESP_SHORT`/`CRC`/`INDEX` is |
| `_nrsp_inhibit_after` | `0x00000001` | `CMD_INHIBIT` **rose on the read after the store** — the block took it |
| `_nrsp_inhibit_seen` | `0x0000021a` (538 of 539 samples) | it was **in flight** on the CMD line, like CMD0's 537 |
| `_nrsp_inhibit_last` | `0x01f80000` — **bit 0 clear** | **it finished.** Rung 19's was `0x01f80001`, bit 0 **still set** after the same window |
| `_nrsp_complete` | `0x00000001` | the completion bit latched |
| `_nrsp_status_any` | `0x00000001` | and `INT_STATUS 0x30` was **non-zero** — `SDHCI_INT_RESPONSE` (bit 0), the one bit the window enabled |
| `_nrsp_polls` / `_nrsp_ticks` | `0x21b` (539) / `0x1327` (4,903) | **≈ 0.255 ms** against rung 19's `0x4da400` samples and `0x015f92d7` ticks = **1.2000 s**, the full bound |

**So the three-way answer is branch 1**, and 743 §7 named its consequence before the press: *the subject
becomes the response path* — `RESPONSE 0x10`, `SDHCI_INT_RESPONSE`, and the `INT_ENABLE` bit this ladder has
been carrying since rung 13.

## 3. The ladder now holds one table it could not build until this rung existed

**Five commands' own inhibit and completion readings, four of them in this capture and the fifth in 742's**
— and the split is total: **the commands that ask for nothing back complete, and the commands that ask for
a response never do.**

| command | word | `inhibit_seen` | `inhibit_last` | `polls` | `ticks` | `complete` | `status_any` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **CMD0** `GO_IDLE_STATE` | `0x0000` | `0x219` (537) | `0x01f80000` — **clear** | `0x21a` | `0x1326` | **1** | **1** |
| CMD1 `SEND_OP_COND` (R3) | `0x0102` | **0** | `0x00f80000` | `0x4db000` | `0x015f9067` | 0 | 0 |
| CMD2 `ALL_SEND_CID` (R2) | `0x0209` | **0** | `0x01f80000` | `0x4dac00` | `0x015f9ceb` | 0 | 0 |
| CMD3 rung 19 (**R1**) | `0x031a` | `0x400` (1024) | `0x01f80001` — **SET** | `0x4da400` | `0x015f92d7` | 0 | 0 |
| **CMD3 rung 20 (NONE)** | `0x0300` | `0x21a` (538) | `0x01f80000` — **clear** | `0x21b` | `0x1327` | **1** | **1** |

**The only two rows with a completion are the only two rows whose flags are zero.** That is the whole
finding, and it is a property of the pair rather than of either rung alone — which is why the rung was worth
a press. **Read §4b before using this sentence: two of the three response commands in this table were never
taken by the block at all**, so the table has three rows for what this paragraph calls one outcome.

**And it retires 726 §2's sixth hypothesis.** 726 wrote that *"this controller completes a command and does
not set its interrupt-status register"*, on a log where `_cmd0_status_any = 0` over 5,088,256 polls of the
**whole** register. This capture has a command that completed **in the same image, on the same block,
through the same window** and `INT_STATUS` bit 0 latched — so the register does report a completion, and
what rung 13/14/15 were reading was a block whose commands never finished. (Rung 14's `_int_status_after =
0x00000001` across one store to `0x34` was the same fact seen from the enable's side; this is it seen from
the command's side, and the two now agree.)

## 4. The one prediction that was refuted, and the reason is a defect in the pre-registration

743 §7 predicted: *"with the demand removed nothing should clear `RESPONSE 0x10` during the command, so
`_nrsp_resp_post` should still hold whatever CMD2 left there (`0x40ff8080` …) — and if the R1 command
emptied the register while the no-response command does not, the CLEARING is tied to the response demand
and 742 §1's reading is confirmed rather than inferred."*

**Measured: `_nrsp_resp_post = 0x00000000`.** The no-response command's post-reading is zero, exactly as
rung 19's was. **So the prediction is refuted: the clearing is not tied to the response demand** — and 742
§1's *"the response registers were cleared because a response-expecting command began"* cannot be inferred
from this pair.

**But the refutation is not the interesting part; that the three readings were compared at all is.**

     _nrsp_resp_pre  = 0x40ff8080   <- (readl(RESPONSE + 0x1C) << 8) | readb(RESPONSE + 0x1B)
     _nrsp_resp      = 0x40ff8080   <- readl(RESPONSE + 0x10)          <-- A DIFFERENT WORD
     _nrsp_resp_post = 0x00000000   <- (readl(RESPONSE + 0x1C) << 8) | readb(RESPONSE + 0x1B)

**743 §7 says the three are "one arithmetic at three times, the driver's own
`(readl(RESPONSE + 0x1C) << 8) | readb(RESPONSE + 0x1B)`". The body does not do that.** In
`st_send_command` the middle reading is `r->resp = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE)`, i.e. a
bare `readl(+0x10)` — word 0 — while the outer two are the driver's word-0 derivation over `+0x1C`/`+0x1B`
(`st_cmd3_noresp`, the `before`/`after` pair). **So the pair `0x40ff8080 → 0x00000000` is not one quantity
changing: it is word 0 and words 2/3, and the middle reading agreeing with the first is a coincidence of
CMD1's `0x40ff8080` still sitting in word 0 while CMD2's leftover still sat in words 2/3.**

**This document does not say the response register was cleared**, because the pair this arm published does
not license it: what moved between `_nrsp_resp_pre` and `_nrsp_resp_post` is the derivation's two source
words (`+0x1C` and the byte at `+0x1B`), and `readl(+0x10)` was never read again. **The discriminating
reading is owed and it is cheap: the four raw words at the four offsets, taken at the same three moments**
— which is the shape rung 17's and rung 18's bodies already publish (`_cid_raw0..3`, `_rb_raw0..3`) and
which this arm's CMD3 bodies do not.

**Recorded as an instance of the project's most-repeated class** ([[mi4-one-value-two-definitions]]): one
quantity — *what the RESPONSE block holds after CMD3* — with **two readings**, and a pre-registration that
wrote them as one. Its cost is not a wrong number in a cell; it is that the ceremony built for a three-time
comparison compared two different words, and the number that came back **agreed** with the middle one for a
reason nobody chose.

## 4b. The correction §3 needs: two of the three response commands were **never taken by the block at all**

**Written after §3, reading the same capture again, and it changes the next subject.** §3 says *"the commands
that ask for nothing back complete, and the commands that ask for a response never do"*. **True, and it hides
a distinction the ladder already has the vocabulary for.** The full reading needs a third column — **did the
block START the command at all** — and `_inhibit_after` / `_inhibit_seen` are exactly the two cells that
answer it, which is why 743 §7 named *"the block never started it at all"* as a branch of its own.

| command | word | flags | `inhibit_after` | `inhibit_seen` | outcome |
| --- | --- | --- | --- | --- | --- |
| CMD0 `GO_IDLE_STATE` | `0x0000` | `0x00` | **1** | `0x219` (537 of 538) | **started → completed** |
| CMD1 `SEND_OP_COND` | `0x0102` | `0x02` | **0** | **0** of 5,088,256 | **NEVER STARTED** |
| CMD2 `ALL_SEND_CID` | `0x0209` | `0x09` | (cell absent — §4b's note) | **0** of 5,088,000 | **NEVER STARTED** |
| CMD3 rung 19 | `0x031a` | `0x1A` | **1** | `0x400` (all 1024) | started → never finished |
| CMD3 rung 20 | `0x0300` | `0x00` | **1** | `0x21a` (538 of 539) | **started → completed** |

**So there are three behaviours and not two**, and `inhibit_seen = 0` is not "no inhibit was observed" — it
is **the block declined the command**, and it is a distinction with more than one producer (the sequencer
refused it; or it ran so briefly no sample caught it; or the poll's own sampling missed it — the third is
retired here because CMD1's poll ran **5,088,256** samples and saw nothing while CMD0's saw the bit 537 times
in 538, so the sampler demonstrably can catch this bit).

**And it is not a one-run artefact: all three of the last three presses carry it identically.**
`xnu_live_storage_cmd1_inhibit_after = 0x00000000` with `_cmd1_inhibit_seen = 0x00000000` and
`_cmd1_word_read = 0x00000102` in **rung 18's, rung 19's and rung 20's** captures — the same three values in
the same three logs — while CMD0's `inhibit_after = 1` and `inhibit_seen = 0x219` are equally constant. **The
word is in the register and the block did not take it**, three times.

**What this does and does not change.** It does **not** touch the headline: rung 19's CMD3 and rung 20's are
a matched pair — same opcode, same argument, same window, same gate, one flag word different — and they
differ in outcome, so *the response demand is what the block stalls on* stands **for the one command it was
measured on**. What it changes is the reach: **the ladder cannot yet say that a response demand makes the
block refuse a command, because two of the three response commands were never taken, and the only
response-demanding command that WAS taken is CMD3.** CMD1 and CMD2 were declined with the **same enable
window open as CMD0** (rung 17 opens one around CMD2: `_cid_ena_wrote = 1`, `_cid_ena_held = 0x8001`), so the
window is not the discriminator either.

**So the next subject is narrower than §6 says, and it has two candidate first questions, both
one-constant moves:**

1. **is the flag word what the block declines?** Send **opcode 1 or 2 with `MMC_RSP_NONE`** — the same opcode
   and the same argument as a command that was never taken, with the flag word changed to the value that
   demonstrably starts (`CMD0`'s `0x00`). Starts ⇒ the response-demand bits are the refusal. Does not start
   ⇒ the flag word is exonerated and the subject is the block's own state at that moment;
2. **or is it the shape of the demand?** Send **CMD3 with `RESP_SHORT` alone** (word `0x0302`) — exactly
   CMD1's flag shape on the opcode that demonstrably starts. Never starts ⇒ the *shape* is the refusal and
   `0x0302`/`0x0209` are one class; starts and stalls ⇒ the `CRC`/`INDEX` bits are what let it start.

**Neither is designed here and neither is armed.** This section records a reading, not a plan — and the
reading is that **the ladder has been calling two different outcomes by one name for three presses.**

**A note on the absent cell, so the table is not read as complete.** CMD2's `_inhibit_after` **does not
exist**: `st_all_send_cid` publishes `_cid_inhibit_last` and `_cid_inhibit_seen` and not the third cell, a
gap rung 19's record already names. So CMD2's row rests on `inhibit_seen = 0` and `inhibit_last =
0x01f80000` (bit 0 clear at the window's end), which is the same evidence CMD1's row rests on plus one more
cell — not the same evidence CMD1's row has. **The row is still a "never started" reading, and it is a
slightly weaker one, and that is worth one line rather than a footnote.**

## 5. Everything else, unmoved

**The ending is 690's clock and it did not move.** `_seam_post_end_ticks = 0x06ddd000`, `_post_cntfrq =
0x0124f800` (19,200,000 Hz), `_post_elapsed` climbing `0 → 0x001db763 → 0x024e1686 → 0x06e94a31`
(= 116,083,761 ticks = **6.0460 s**, crossing the 6.0000 s deadline), **`_post_end_calls = 0x00000006`**,
with `_seam_end_run = 0` and `_seam_post_end_run = 0` — so the deadline is the one that fired. `_seam_lr =
0x800492dc` (unmoved), `_seam_sp = 0x80557ec8`, `_seam_sctlr = 0x30c57879`, `_sleh_storm = 0x9`.

**The runner's criterion block is unchanged**: the known storage-arm FAIL (`seam_sp = 0x80557ec8 is not
sleh_sp-8`) and the two `SLOT_NULL` UNREADs — **7 PASS / 1 FAIL / 2 UNREAD counted per criterion**, and the
block appears twice in the run log (the gate's reading of the log as it stood at gate time, and the
runner's of this run's) for the reason m751 records: **count per artefact, not per line.**

**The goal's floor is met and no further** — *"user mode reached, and a driver answering"* PASSes in this
log exactly as in 745 §1, and the block's own sentence stands beside it: **it is a floor, not progress.**

## 6. What the next rung is for, and what it is not

**The subject is now the response path — with §4b's correction, and the correction is what makes the next
question askable**: the block declined CMD1 and CMD2 outright and took CMD3, so *why a response-demanding
command is refused* and *why a response-demanding command that IS taken never finishes* are **two questions
in one name**, and §4b names a one-constant move for each. What the ladder's next question is, is narrower
than any it has asked:
**a command that asks for a 48-bit response and gets none never completes and never times out** — `_rca_err
= 0`, the whole `INT_STATUS` register zero over 5,088,000 polls, and (per 726's census) the block's own
`TIMEOUT` never set either. That is not the SDHCI timeout behaviour, and it is not the card's: **it is this
block waiting for a CMD-line reply that nothing on the other end is sending.**

**Two readings are therefore owed before any new command class, and neither needs a new rung's worth of
ceremony:**

1. **which words move**, i.e. the four raw `RESPONSE` words at the four offsets, at the three moments §4
   says the arm compared through two arithmetics — the reading 743 §7 thought it was publishing;
2. **the card's own presence**, since `PRESENT_STATE 0x24` reads `0x01f80000` at every one of the twelve
   moments this capture published it, with bit 16 (`SDHCI_CARD_PRESENT`) clear — **and this document does
   not read that as "no card"**, because the arm's own source says at `entry_storage.c:218` that the bit
   *"must NOT be read as 'no card' here"*. A bit that cannot be read either way is a bit that needs a
   second reading beside it, not a verdict.

**And this document arms nothing.** Rung 20 is pressed and spent, no firer is armed, **no press is owed**,
and `out/` still holds `armed-storage-b7e5a18f` as the last arm — the next arm is a host-side act that this
press has now made worth designing.

## 7. What this document does not say

- **It does not say a card answered.** No response was read, decoded or attributed anywhere in this press;
  the command that completed asked for nothing and got nothing.
- **It does not say the storage driver works.** Four commands have been put on this block's bus in the whole
  ladder; there is no sector, no partition table and no mount, and 「挂载存储」 is not nearer by a cell.
- **It does not say the block is healthy.** The reading is *one command with its response demand removed
  completes in 0.255 ms*, and nothing here separates a block that is working from one that reports a
  completion for a command it never really put on a card.
- **It does not say the clearing in §4 happened**, for the reason §4 gives.
- **It does not change the goal's status.** No storage, no filesystem, and the machine never observed
  staying up — **TWRP-to-storage stays withheld.**
