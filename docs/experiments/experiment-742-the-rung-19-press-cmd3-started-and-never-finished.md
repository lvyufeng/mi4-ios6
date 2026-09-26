# 742: the rung-19 press — CMD3 was ACCEPTED and STARTED, the block held its own command line for the whole window, and the response register was EMPTIED rather than answered

**The press**: `armed-storage-rca-c333d09c` (`STAGE90_XNU_STORAGE_PROBE=18`, ordinal rung 19),
2026-09-26 **18:08:10–18:09:22 UTC**, **EXIT 0**. Readiness **5 of 5, exit 0** (`18:07:33`), **one**
`preflight_boot_check.sh --allow-xnu-entry` (exit 0, **587** lines, written `18:08:07`), **one**
`run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-rca-c333d09c`, fired by hand
(`18:08:10`). `fastboot boot` only, nothing flashed, no `POWER_CONTROL 0x29`, no GCC word, no
`core_mem` word, no byte of the medium. The neighbour `33e80afe` was absent from **both**
`fastboot devices` and `adb devices` at fire time (0 occurrences) and no stray runner, gate or watcher
was running. The runner's own line: *"the device came back 28s after this run called `fastboot boot`
(seen via: adb)"*; the interval from the gate log's own mtime to the captured log's is **74 s**
(`18:08:07.12` → `18:09:21.73`), measured here on the same footing as 738's 77 s and 740's 72 s.

Capture: `out/stage90/captures/rung19-rca-20260926-180922-last_kmsg.txt`, **630,486 B**, sha256
`990983a9477eb556a05fbf50e61f68f7a2625f8a8e3d3de03785e7af0a6c8944`, **archived by hand** (the runner
does not archive — 730 §6 — it writes `/tmp/cancro-last_kmsg.txt` and parks the previous run's log at
`.prev`; this press's own park is `/tmp/cancro-last_kmsg.txt.prev.5`, rung 18's capture). The three
press logs live under the job tmp and are named here so their shas are checkable: the readiness log
(23 lines, sha `01b4e68b593d67655710eca11fe7a24812b93f8bece44737ad993dd3fad1175b`), the gate log (**587**
lines, sha `3a16875d40d60cbe88e496b9bf93d8aa0924c2c00e1f12c74b5f8cd6bb7599e9`) and the combined run
log (1095 lines, sha `8f74372adc27dfd474af790e84b90f0ebacaac4c742997cfeb2242a05ae870e8`).

## 0. Two spellings of the rung number

The ladder counts the **value** of `STAGE90_XNU_STORAGE_PROBE` (this arm's clauses read `18 = 17 plus …`);
the commit subjects and the pre-registrations count **ordinal arms**; they part company at ladder value
9 (which has two arms), so **this arm's ladder value is 18 and its ordinal is rung 19**. 741 §0 is the
same sentence for the arm this press spent.

## 1. The answer: the block BEGAN CMD3, and it never finished it

The arm's own body, `st_set_relative_addr`, is `mmc.c:1409`'s `mmc_set_relative_addr(card)` transcribed
from `mmc_ops.c:194-210`: opcode 3, argument `card->rca << 16` with `card->rca = 1`, so the ladder's
**first non-zero argument `0x00010000`**, and flags R1 — the ladder's first command word with an `INDEX`
bit. It ran, and every cell 741 §3 and §4 pre-registered came back:

    _rca_gated         = 0x00000000    the gate (CMD2 SENT) opened; this is a real run, not a refusal
    _rca_calls         = 0x00000001
    _rca_op            = 0x00000003   )  the three constants, on the bus
    _rca_flags         = 0x00000015   )  MMC_RSP_R1 | MMC_CMD_AC
    _rca_arg           = 0x00010000   )  the FIRST non-zero argument this ladder has ever put out
    _rca_sent          = 0x00000001
    _rca_word          = 0x0000031a   )  opcode 3 << 8 | RESP_SHORT 0x02 | CRC 0x08 | INDEX 0x10
    _rca_word_read     = 0x0000031a   )  read back OUT of COMMAND 0x0E - the block held it
    _rca_arg_wrote     = 0x00010000   )  read back OUT of ARGUMENT 0x08
    _rca_complete      = 0x00000000   <== no completion
    _rca_err           = 0x00000000       and NO error bit either
    _rca_timeout       = 0x00000001       the 1.2 s bound inside st_send_command expired
    _rca_status_any    = 0x00000000   <== INT_STATUS 0x30 read ZERO at every sample
    _rca_any_polls     = 0x00000000
    _rca_polls         = 0x004da400      5,088,256 samples
    _rca_ticks         = 0x015f92d7      23,040,727 ticks = 1.2000 s at 19,200,000 Hz
    _rca_done          = 0x00000001

**Three things are true of CMD3 that were true of no command before it**, and they are the answer:

1. **The block was still holding its own command line when the sampling window ended.**
   `_rca_inhibit_after = 1` (its first sample), **`_rca_inhibit_seen = 0x400`** — all 1024 samples —
   and **`_rca_inhibit_last = 0x01f80001`, whose bit 0 is STILL SET**: `PRESENT_STATE`'s
   `SDHCI_CMD_INHIBIT` was raised on the read after the store and had not cleared 1.2 s later.
   `_rca_ps_after = 0x01f80001` is the same word read once more, at the far end, with bit 0 set — the
   two cells agree, and they are the first reading in this ladder of a command **in progress at the end
   of the window**.
2. **The response register was EMPTIED.** `_rca_resp_pre = 0x40ff8080` (CMD1's own word, still sitting
   there after CMD2 — the same word 740 proved CMD1 put there) → the command path's own read
   `_rca_resp = 0x40ff8080` → **`_rca_resp_post = 0x00000000`**. So `_rca_resp_moved = 1` by the letter
   (§3 is the reading that matters) and the register is empty at the end of the window.
3. **Nothing was latched.** `_rca_status_any = 0` over the whole register across 5,088,256 samples,
   `_rca_complete = 0`, `_rca_err = 0`, `_rca_timeout = 1`. This is 726's sixth hypothesis — *this
   controller completes a command and does not set its interrupt-status register* — measured a third
   time, on a command whose answer is one word wide.

**What the run therefore says, in one sentence**: CMD3 went out with the driver's own word and argument
(both read back out of the block), **the block took it and started it** — the response registers were
cleared and the command line was held — and then it neither finished nor faulted: no status bit, no
error bit, no response, and the command line still held at the end of a 1.2 s window.

## 2. The pair landed on an outcome the pair did not name

741 §3 registered `_rca_resp_moved` as a boolean with two readings and named one alternative producer:
**1** = CMD3 moved the register (the sequencer took the command), **0** = it did not; and
`_rca_resp_is_arg` = 1 iff the post word equals the argument, so a register echoing `ARGUMENT 0x08`
would be a published reading rather than a coincidence.

The press landed **`_rca_resp_moved = 1` with `_rca_resp_post = 0x00000000` and
`_rca_resp_is_arg = 0`** — the register changed, and it changed **to zero**. That is a third producer
the pair's two readings do not distinguish: not *the block wrote a status there*, and not *the block
echoed the argument*, but **the response registers were cleared and nothing was written into them**.
An SDHCI host clears `RESPONSE` when it begins a response-expecting command; the value then waits for
the card. **A zero response behind a moved register is the signature of a command that was started and
never answered**, and the two cells that make it unambiguous are on either side of it in the same log:
CMD2 (rung 17) left the register at CMD1's word — `_cid_resp0 = 0x40ff8080` — so **CMD2 did not clear
it, and CMD3 did**.

This is worth stating plainly because it is a defect of the *pre-registration* and not of the run: a
derived boolean over a quantity with three outcomes collapses two of them, and the press landed the
collapsed one. `_rca_resp_moved` is a reading of *change*; the thing the next rung needs is a reading
of *what is there*, and that is `_rca_resp_post` itself. The boolean did its job — it said the register
is not inert — and it is the reason the run is a diagnosis rather than a blank.

## 3. The R1 decode is a decode of CMD1's leftover word

`_rca_resp` (the word the command path read, at the moment it read it) is `0x40ff8080`, and its three
R1 fields decode off **that** word:

    _rca_state   = 0x00000000   (resp >> 9) & 0xF   CURRENT_STATE   -> 0: idle
    _rca_ready   = 0x00000000   bit  8              READY_FOR_DATA  -> clear
    _rca_illegal = 0x00000001   bit 22              ILLEGAL_COMMAND -> SET

**`_rca_illegal = 1` is a property of `0x40ff8080`, and `0x40ff8080` is not CMD3's answer** — it is
CMD1's own word (740: the register was empty before any command and CMD1 put that value there, and 738
found `_cid_resp0` bit-for-bit equal to `_cmd1_resp`). So the decode is the ladder doing exactly what
741 §2 built it to do — *a word that comes back as CMD1's status again is the register not answering
CMD3 at all* — and the reading it returns is that **the register did not hold CMD3's status when the
command path read it**. Bit 22 being set is therefore not "the card rejected CMD3"; it is bit 22 of a
stale OCR-shaped word. Read the three fields together with `_rca_resp_post = 0`: the stale word was
still there during the command, and the register was emptied by the end of it.

## 4. Four commands' own inhibit readings, side by side in one log

741 §2 published the inhibit triplet for CMD3 as it is for the three below it, which is 740 §6's next
question made answerable rather than asked. Here they are, all four in this one capture:

| command | word | `_inhibit_after` | `_inhibit_seen` | `_inhibit_last` | bit 0 at the end | `_complete` | `_status_any` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| CMD0 `GO_IDLE_STATE` | `0x0000` | `1` | `0x219` = 537 | `0x01f80000` | **clear** | **1** | **1** |
| CMD1 `SEND_OP_COND`     | `0x0102` | `0` | `0x000` = 0   | `0x00f80000` | clear | 0 | 0 |
| CMD2 `ALL_SEND_CID`     | `0x0209` | *not published* | `0x000` = 0 | `0x01f80000` | clear | 0 | 0 |
| CMD3 `SET_RELATIVE_ADDR` | `0x031A` | `1` | **`0x400` = 1024** | **`0x01f80001`** | **SET** | 0 | 0 |

Three readings come out of the table and none of them needed a new register:

- **CMD0 is the only command in this ladder whose `CMD_INHIBIT` had cleared by the last sample**, and
  CMD0 is the only command whose flags ask for no response at all (`RESP_NONE`, word `0x0000`). It is
  also the only command with `_complete = 1` and `_status_any = 1`, and its poll ended at
  `_cmd0_polls = 0x21b` (539) / `_cmd0_ticks = 0x0000132a` (4,906 ticks, **0.26 ms**) instead of running
  to the 1.2 s bound.
- **CMD1 and CMD2 never showed inhibit at all** — `_cmd1_inhibit_seen = _cid_inhibit_seen = 0` over
  1024 samples — where CMD0's was seen 537 times. 738 read that as *a command whose word reads back
  never once showed `CMD_INHIBIT`*, and rung 19 sharpens it: it is not a property of "a word that reads
  back", because CMD3's word read back too.
- **CMD3's inhibit was seen in every sample and was still set at the last one**, and its
  `_cid`-side counterpart did not clear either — the response register was emptied. The two readings
  agree: **CMD3 was started; CMD1 and CMD2 were not.**

**One difference recorded rather than smoothed over**: `_cid_inhibit_after` does not exist in this
image. CMD2's body publishes the pair (`_cid_inhibit_seen`, `_cid_inhibit_last`) and not the third
cell, so the table's CMD2 row has a hole in it that is a property of the arm's own body and not of
this run. The hole does not affect the reading above, which is carried by `_cid_inhibit_seen = 0`, but
a reader comparing four rows should know why one cell is missing.

## 5. The window's cells, and why no new register class was touched

    _rca_ena_wrote   = 0x00000001   the gate's own value, not a constant
    _rca_ena_held    = 0x00008001   bit 15 (SDHCI_INT_ERROR) riding along with a write to 0x34
    _rca_sig_enable  = 0x00000000   READ AND NEVER WRITTEN, at every rung - the register whose
                                    conjunction with INT_ENABLE raises this block's SPI 123 -> intid 155
    _rca_wrote_back  = 0x00000000   the restore wrote the gate's value back
    _rca_status_post = 0x00000000
    _rca_readback    = 0x00008000   the PARTIAL restore 730/733/736/738/740 all measured
    _rca_ps_after    = 0x01f80001   PRESENT_STATE 0x24: bit 0 CMD_INHIBIT still SET; bit 16
                                    (SDHCI_CARD_PRESENT) clear, as it is in every reading here

Two stores, both to `INT_ENABLE 0x34`, exactly as pre-registered: the one-bit window
`(read | SDHCI_INT_RESPONSE)` immediately before CMD3, and its restore on one unconditional line
immediately after the publishes. `SIGNAL_ENABLE 0x38` was read and never written — the SPI 123 → intid
155 line this block shares with nobody in this project did not rise, and **there is not one
`_irq_other_*` key in this capture** against 1 in 709's run that ended that way. The pre-registered
hazard — a delivery ending the run at the dispatcher as `_irq_other_count = 1` / `iat = 155` — did not
occur, on a third arm in a row.

**On `_rca_ps_after`**: bit 16 is `SDHCI_CARD_PRESENT` in this tree's own vendor header
(`external/android_kernel_xiaomi_cancro/drivers/mmc/host/sdhci.h:71`), and the ladder's constant carries
its own warning — *"must NOT be read as 'no card' here, see below"*. It reads clear here **and it reads
clear in every reading this ladder has ever taken**: `_rb_present = 0x01f80000` (rung 18, before any
command) and every command's `_inhibit_last` baseline in this table carry the same bits 19–24 and a
clear bit 16. So this press adds nothing about the card-detect line, and it must not be read as if it
did — what rung 19 added to `PRESENT_STATE` is **bit 0**, and nothing else.

**Bit 0 is worth the same care, and it is unambiguous here because the ladder holds four readings of
`PRESENT_STATE 0x24` in this one capture and only one of them has it set.** In the capture's own order:
`_rb_present = 0x01f80000` (before any command) → `_cmd_ps_before = 0x01f80000` → `_cmd_ps_after =
0x01f80000` (the command path's own census, bracketing CMD0 and CMD1) → **`_rca_ps_after =
0x01f80001`**, taken after CMD3. Three readings with the bit clear — one of them bracketing the only
command that ever completed — and one with it set, on the command that was still in flight when the
window closed. `_cmd_ps_before`/`_cmd_ps_after` are what make the contrast a measurement rather than an
inference: the *same register, read by the same arm in the same phase*, saying the command path left
the block with its command line idle.

## 6. The rungs below are inherited unchanged, and the exclusion held

    _rb_resp_zero     = 0x00000001   <== 740's answer, reproduced one press later
    _rb_int_enable    = 0x00000000   bit 15 clear before the gate, so nothing above it wrote 0x34
    _rb_present       = 0x01f80000
    _cmd_int_enable   = 0x00000000   )  736's four-place exclusion, all four holding
    _cmd_gate_kind    = 0x00000000   )
    _cmd_refused      = 0x00000000   )
    _cmd_sent         = 0x00000002   )
    _cmd_ps_before    = 0x01f80000   )  PRESENT_STATE, bracketing CMD0/CMD1: bit 0 clear, both times
    _cmd_ps_after     = 0x01f80000   )
    _cmd_enabled_out  = 0x00000000       and cmd_sig_enable = 0
    _cid_word = _cid_word_read = 0x00000209
    _cid_resp0        = 0x40ff8080
    _cmd1_resp        = 0x40ff8080   with _cmd1_resp_read = 1, _cmd1_resp_busy = 0, _cmd1_resp_voltage = 0x00ff8000
    _cmd1_word_read   = 0x00000102
    _int_status_before = 0x00000000 )  730's pair, unmoved
    _int_status_after  = 0x00000001 )
    _ena_host_version = 0x00001102

Every one of the seventeen `_rb_*` cells reproduces 740, so **rung 18's answer is a reproduced property
of this image and not a one-off** — and `_rb_resp_zero = 1` is the cell this whole rung was built on
(741 §1). 736's exclusion is intact in the bytes: the command path ran, nothing above the gate had
written `0x34`, and **there is no `_quiet_*` key at all** in the capture, so rung 15's body — compiled
for the ladder value 15 and no other — is not in this image. A log from this arm carrying any `_quiet_*`
key would be a log from an arm that is not this one.

## 7. The ending, and what the runner said

    _seam_lr            = 0x800492dc     the exit's own call's return address, unmoved
    _seam_sctlr         = 0x30c57879     C clear at the seam, so these words are DRAM readings
    _seam_sp            = 0x80557ec8
    _seam_post_end_ticks = 0x06ddd000
    _sleh_storm         = 9   episodes_seen = 9
    _slot_post_calls    = published 0x1, 0x2, 0x3, 0x4 - a running counter, last reading 4
    No errors detected   - 1 occurrence, the kernel's own line the capture carries

The ending is unmoved from 730/733/736/738/740: the same seam constant, the same storm count, the same
`_seam_post_end_ticks`, and the log carries the kernel's `No errors detected`.

**The runner's criterion block, read exactly.** It is **ten lines — 7 PASS, 1 FAIL, 2 UNREAD** — and
the one FAIL is the known storage-arm shape criterion that has failed on every arm of this family:

    FAIL  seam_sp=0x80557ec8 is not sleh_sp-8 (the abort's sp is 0x80557ec8, so the slot is
          0x80557ec0): the arm's address and the pop's are different words, and nothing below
          this line joins up
    UNREAD xnu_live_slot_pre_calls=0x00000004 is published and xnu_live_slot_pre_sp is not

The two UNREAD lines are the SLOT_NULL image (`STAGE90_XNU_SLOT_NULL=1`), which writes a count and no
words — **an absence produced by a switch in the image, not an agreement** — and the FAIL is about the
idle-exit seam, not about storage. Nothing in the block is a verdict about CMD3.

**And a correction to how this press was first counted, because the count was wrong and the shape of
the mistake is one this project has on file.** The same criterion block appears **twice** in the run
log: once inside the gate's `== payload output ==` section, which reads *the log those instructions
name as it stands at gate time* — and at gate time that log was the **previous** run's, rung 18's
capture — and once in the runner's own `== reading the log this run captured ==` section, over this
run's capture. The two blocks are identical except for two lines, and those two lines are what
identifies them: `xnu_live_post_elapsed` / `xnu_live_post_t0` read `0x06ea65db` / `0x07c78b7a` in the
first block, **which are rung 18's own values, checkable in `/tmp/cancro-last_kmsg.txt.prev.5`**, and
`0x06eb64fd` / `0x0b558bf2` in the second, which are this capture's. Counting the verdict **lines**
across the whole file gives `14 PASS / 2 FAIL`; counting the **criteria** in the block the runner
produced for this run gives `7 PASS / 1 FAIL / 2 UNREAD`. The first form was quoted as
`16 PASS / 2 FAIL` at one point in this step's own notes, which is wrong twice: it double-counts the
block, and it reads the same criterion twice as two different failures. **A predicate that the tool
prints once per artefact it was applied to must be counted per artefact and not per line.** Recorded as
**m751** in `mi4-measurement-defects.md`.

## 8. What the next rung is

Rung 19's own answer retires the question the ladder has been carrying since rung 13. 726 §2's five
hypotheses were refuted by 724/725/726; 726's sixth — *this controller completes a command and does not
set its interrupt-status register* — is now **wrong in its first half for the response-expecting
commands**: CMD3 was not completed at all. What is left is a smaller and sharper statement:

> **A response-expecting command is accepted — the word and the argument are read back out of the
> block, the response registers are cleared, and the command line is held — and it never completes,
> never times out and never errors.**

The one command that did complete is the one that asked for no response. That gives the next rung a
single variable to move rather than a plan to invent: **the same opcode and the same argument with the
flag word carrying no response demand** — `RESP_NONE` on opcode 3, word `0x0300`, argument
`0x00010000` — issued through the same window, publishing the same triplet. If the block completes
*that*, the demand for a response is what stalls it and the subject becomes the response path
(`RESPONSE 0x10` and the inhibit line); if it does not, the opcode or the block's own state is the
subject, and the census has to move to the registers that have never been read with a command in
flight (`SLOT_INT_STATUS 0xFC`, `CORE_PWRCTL_STATUS 0xDC`). Either way the failure mode stays a
diagnosis: the 1.2 s bound is inside `st_send_command` (measured four times now), the ending is
unmoved, and a delivery on the shared SPI 123 line ends the run at the dispatcher — an ending this
image already reads and survives.

**Two things are named here and must not be read into the press**: this rung is a *controller* question
and not the driver's next statement (`mmc.c` would say CMD9 `SEND_CSD`; arming a further
response-expecting command on a block whose command line has been held for 1.2 s would stack the same
unknown on itself), and `_rca_ps_after`'s clear `CARD_PRESENT` bit is **not** evidence about a card —
§5 says why.

## 9. What the press was worth, in one line

**It converted the ladder's oldest open question from "does the block answer?" into "why does a command
that asks for an answer never finish?", and it did it with two cells it had never seen before — an
inhibit still standing at the end of the window, and a response register emptied rather than filled —
without touching a register class, a byte of the medium, or a rung below it.** The goal is still NOT
met: no storage, no filesystem, the OS not observed reaching userland, so **TWRP-to-storage stays
withheld**. No firer is armed, no press is owed, and the press path is the two commands readiness
prints, run by hand once each.
