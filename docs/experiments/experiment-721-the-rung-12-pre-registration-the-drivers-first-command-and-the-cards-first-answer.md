# 721: the rung-12 pre-registration — the driver's own first command, and the card's first answer

**Date:** 2026-09-26. **Arm to build:** `STAGE90_XNU_STORAGE_PROBE=11` with every other switch exactly
where rung 10 left them (`STAGE90_XNU_PWR_WAIT_TICKS=384000` included) — *the rung-11 arm with one
act appended and no value moved*. **Purpose:** port `sdhci_send_command` (`sdhci.c:1076-1155`) and put
the driver's own first two commands on the bus — `mmc_go_idle`'s CMD0 and `mmc_attach_mmc`'s CMD1 —
so that for the first time in this line the question is not what the *controller* does but what the
**card** does. The cell that decides it is `_cmd1_resp`: the R3 response word, read out of
`SDHCI_RESPONSE 0x10`, which no image in this project has ever had a reason to read.

## 1. Why the command path is the next step, and not one of the three others

Rung 10 (experiment-720) closed the controller's own bring-up: the request (rung 7's power byte), the
interrupt (rung 8's client on intid 170) and the completion (rung 9/10/11's wait) now stand in one run
with the vendor's own predicate satisfied. Every one of those three acts a byte the *host* writes or a
bit the *controller* latches. `_writes` is 0→4 on the standard file, six reads in `core_mem`, and the
medium has never been addressed.

Four steps were owed (717 §6's list, unchanged): the card's rail, the RCG rate write, rung 6's two
stores to `hc_mem + 0x10C`, and the command path. Three of them are ruled out as *this* rung, and the
grounds are measured rather than preferred:

* **The card's rail** (`sdhci_msm_setup_vreg` / `setup_pins` / `set_vdd_io_vol`) is a PMIC request
  through the regulator framework and a TLMM pin-mux — not an SDHCI register at all. This image has no
  regulator and no TLMM client, and *the bus is already powered without it*: rung 11 measured the
  vendor's own power completion satisfied (`_wait_ctl_after = 0x01` = `BUS_SUCCESS` with
  `_pwr_irq_state = 0x02` = `BUS_ON`) and the I/O level raised (`_pwr_irq_io_level = 0x08` =
  `IO_HIGH`). A rail rung would be a rung about the PMIC's firmware, and its *premise* — that the card
  is unpowered — is one this ladder has already refuted.
* **The RCG rate write** is the *speed* reconcile (706 §2: the driver's bookkeeping believes the input
  clock is `sup_clk_table[0]` = 400 kHz while the register file is at `gpll4`/4), and it is reached
  only by `sdhci_msm_set_clock`'s `clk_set_rate` when `sup_clock != msm_host->clk_rate`. On the
  identification clock that is false, and the SDCLK the card actually sees is **200 kHz**:
  `CLOCK_CONTROL`'s divisor is computed against `host->max_clk` = the DT's last `qcom,clk-rates`
  entry, 384,000,000, giving `div = 960` (`_clk_set_div = 0x1e0` → `_clk_set_real_div = 0x3c0`), and
  192 MHz / 960 = 200 kHz. **The eMMC's own identification range is 100–400 kHz**, so the card is
  clocked legally and the rate write is not a prerequisite for it. (This is also why the rate write
  is *not* on the correctness path here: it is 2× off, not 480×.)
* **Rung 6's two stores to `hc_mem + 0x10C`** are `sdhci_msm_set_clock`'s `CORE_VENDOR_SPEC` writes
  *through the vendor's own accessor*, which translates the offset into the window at `0xf9824900`;
  rung 6 already makes them (through `core_mem`, at the same physical address — the two-addresses
  reading of 710 §1.5) and rung 7's press measured `_clk_set_vendor_after = 0` against
  `_clk_set_vendor_w1 = 0x00000200`, i.e. **the field did not take**. Re-running a store the hardware
  has already refused twice is not a rung; it is a re-press, and what it needs is a *hypothesis*, not
  another arm.

That leaves the command path, and it is where the goal's own sentence points: 「把基础驱动跑起来」 is
not satisfied by a controller that has been configured, and 「挂载存储」 cannot begin until something
on the bus answers. **This rung is the first act of this line that addresses the medium.**

## 2. The driver's own code, read out before any of this file is written

`sdhci_send_command` (`sdhci.c:1076-1155`), for a command with no data, is four statements and two
waits, and every line below is quoted rather than paraphrased:

```
mask = SDHCI_CMD_INHIBIT;                                  :1086-1089   (no data, no MMC_RSP_BUSY)
while (sdhci_readl(host, SDHCI_PRESENT_STATE) & mask) {     :1096       the bounded inhibit wait
        if (timeout == 0) { ... cmd->error = -EIO; return; } :1097-1103  the bound is 10 ms,
        timeout--; mdelay(1); }                                            :1084 `timeout = 10`
mod_timer(&host->timer, ...);                              :1108       the driver's 10 s request timer
sdhci_prepare_data(host, cmd);                             :1117       (no data: returns early)
sdhci_writel(host, cmd->arg, SDHCI_ARGUMENT);              :1119       ARGUMENT 0x08, 32-bit
sdhci_set_transfer_mode(host, cmd);                        :1121       (no data: RETURNS, sdhci.c:985-986)
flags = <derived from cmd->flags>;                         :1130-1140
sdhci_writew(host, SDHCI_MAKE_CMD(cmd->opcode, flags), SDHCI_COMMAND);  :1153  COMMAND 0x0E, 16-bit
```

and the completion side, `sdhci_finish_command` (`:1158-1181`), reads the response **only when the
command asked for one**:

```
if (host->cmd->flags & MMC_RSP_PRESENT) {
        if (host->cmd->flags & MMC_RSP_136) { ...four words, shifted... }
        else host->cmd->resp[0] = sdhci_readl(host, SDHCI_RESPONSE);   :1174   RESPONSE 0x10, 32-bit
}
```

**The two commands, and their arguments, are the driver's own and not a choice.**

* `mmc_go_idle` (`mmc_ops.c:96-127`): `cmd.opcode = MMC_GO_IDLE_STATE` (0), `cmd.arg = 0`,
  `cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_NONE | MMC_CMD_BC`. It is called from
  `mmc_rescan_try_freq` (`core.c:3102`) on every rescan.
* `mmc_attach_mmc` (`mmc.c:1913-1923`): `err = mmc_send_op_cond(host, 0, &ocr)` — **argument 0**, i.e.
  the driver's own *single-pass* probe form (`mmc_ops.c:139-141`: `if (ocr == 0) break;`), and
  `cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R3 | MMC_CMD_BCR`, `MMC_RSP_R3 = MMC_RSP_PRESENT`
  (`core.h:54`) — a 48-bit response with **no CRC and no opcode check**, which is why its
  `SDHCI_COMMAND` word carries neither `SDHCI_CMD_CRC` nor `SDHCI_CMD_INDEX`.

The two derived `SDHCI_COMMAND` words, computed the way `sdhci.c:1130-1140` computes them
(`SDHCI_MAKE_CMD(c, f) = ((c & 0xff) << 8) | (f & 0xff)`, `sdhci.h:57`):

| command | `cmd->flags` | `SDHCI_CMD_RESP_*` | CRC | INDEX | word written |
|---|---|---|---|---|---|
| CMD0 `GO_IDLE_STATE`, arg 0 | `MMC_RSP_NONE` = 0 | `RESP_NONE` = `0x00` | no | no | **`0x0000`** |
| CMD1 `SEND_OP_COND`, arg 0 | `MMC_RSP_R3` = `MMC_RSP_PRESENT` = 1 | `RESP_SHORT` = `0x02` | no | no | **`0x0102`** |

**And the order is the driver's too**: `mmc_attach_mmc` calls `mmc_go_idle(host)` (`mmc.c:1356`) and
then `mmc_send_op_cond` (`:1359`) — CMD0 immediately before CMD1, in one function, for the reason its
own comment gives ("`mmc_go_idle` is needed for eMMC that are asleep"). What this rung does **not**
port is `mmc_rescan_try_freq`'s *sequence*: between its `mmc_go_idle` and the MMC attach are
`mmc_send_if_cond` (CMD8) and the SDIO and SD attach attempts (`core.c:3104-3111`), three probes an
eMMC answers with timeouts by design. They are omitted deliberately and named here so that no later
reader reads this arm as "the rescan's first commands".

## 3. The port, and the one substitution

`st_send_command(opcode, arg, mmc_flags, &r)` — one `noinline` function, so that its body is a window
a build clause can read (rung 9's `st_pwr_wait` precedent), called twice by the probe:

1. **the inhibit gate** — `PRESENT_STATE 0x24` polled for `SDHCI_CMD_INHIBIT` (`0x1`) to clear, with
   the driver's own 10 ms bound (`timeout = 10` × `mdelay(1)`), published as polls and ticks; the
   bound expiring **refuses the command** rather than issuing it into a busy controller.
2. **the enable gate** — `INT_ENABLE 0x34` and `SIGNAL_ENABLE 0x38`, read **before the first command
   only**, and the arm refuses if either is non-zero. This is not defensive: the SDHCI *hc_irq* is
   SPI 123 → intid 155, **a line this image hands to nobody**, and a command completion that raised it
   would arrive at the dispatcher as `_irq_other_count` and **end the run** (`entry_irq.c:549-556`).
   The enable registers are the only thing that can let that happen, so they are the gate.
3. **the latch clear** — `INT_STATUS 0x30` is read, the value read is written straight back
   (write-1-to-clear), and read again to confirm it is zero. Without this the poll's end condition is
   not a reading at all: `SDHCI_INT_RESPONSE` latches, so CMD1's poll would be satisfied by CMD0's
   completed command (`mi4-silence-is-a-reading-only-if-success-is-silent`'s shape, one register
   over). The clear is the driver's own semantics at one remove: `sdhci_irq` writes the bits it
   handled back to this register.
4. **the command** — `ARGUMENT 0x08` (32-bit, `sdhci.c:1119`) then `COMMAND 0x0E` (16-bit,
   `sdhci.c:1153`). `TRANSFER_MODE 0x0C` is **not written**, and that is a fact about the driver:
   `sdhci_set_transfer_mode` returns on its first line when `cmd->data == NULL` (`sdhci.c:985-986`).
5. **the completion poll** — `INT_STATUS & SDHCI_INT_RESPONSE` (`0x1`), bounded, with the ticks and
   the poll count published. **This is the one substitution**, and it is the same one rung 9 made for
   `wait_for_completion`: the driver's completion arrives through `sdhci_irq` and this image enables no
   SDHCI interrupt, so the block becomes a bounded poll of the register the handler would have read.
6. **the response** — `RESPONSE 0x10` read (32-bit) **iff the command asked for one**, exactly as
   `sdhci_finish_command` does. CMD0 reads no response; CMD1 reads `resp[0]`.

**The two bounds, and why they are source constants rather than switches.** Rung 9 introduced
`STAGE90_XNU_PWR_WAIT_TICKS` because *the bound was that rung's subject*; here the bound is the
failure path and the driver's own values are constants, so this rung follows
`ST_RST_TICK_BUDGET`/`ST_CC_TICK_BUDGET`'s precedent instead of adding a key — and a new key would
have to be added to the gate's `ENTRY_CFG_KEYS` in the peer lane's file, which is a cost with no
reading attached.

* the inhibit gate: **10 ms = 192,000 ticks** (the driver's own `timeout = 10`, one millisecond per
  step).
* the completion poll: **1,200 ms = 23,040,000 ticks**. The event it must be able to see is the
  controller's *own* command timeout (the SDHCI specification fixes it at 1 s), so the bound is just
  above it. If the assumption is wrong the arm says so rather than hiding it: our bound expiring first
  is `_cmd1_timeout = 1` beside a `_cmd1_status_after` that carries **no** error bit, which is a
  reading about the controller's timeout being longer than 1.2 s and not a silence.

## 4. What this rung writes, and the four floors it does not cross

**The store set is `ARGUMENT`(0x08, 32-bit), `COMMAND`(0x0E, 16-bit) and `INT_STATUS`(0x30, 32-bit
write-1-to-clear)** — five distinct offsets in `hc_mem` counting the reads, three of them stores, and
two calls of the function. Every one of them is reached by the vendor's own accessor:

| act | register | width | driver |
|---|---|---|---|
| clear the latch | `INT_STATUS 0x30` | 32-bit `writel` | `sdhci_irq`, `sdhci.c:2907` |
| the argument | `ARGUMENT 0x08` | 32-bit `writel` | `:1119` |
| the command | `COMMAND 0x0E` | 16-bit `writew` | `:1153` |
| the response | `RESPONSE 0x10` | 32-bit `readl` | `:1174` |
| the inhibit / the state | `PRESENT_STATE 0x24` | 32-bit `readl` | `:1096` |

Four floors, and each is a property of the artifact rather than a sentence:

1. **No interrupt is enabled and none is armed.** `INT_ENABLE 0x34` and `SIGNAL_ENABLE 0x38` appear in
   the function's access set as **loads only**, and the gate refuses the whole act if either reads
   non-zero — so the command completion cannot raise a line, and the arm cannot produce
   `_irq_other_*`. The only line this rung can cause is the one rung 8 already owns and rung 11
   measured working.
2. **`POWER_CONTROL 0x29` is still unreachable.** The probe's own `hc_mem` store set stays
   `47 44 44 41`; the command path's stores are in `st_send_command`'s own body, which is a second
   window with its own clause (rung 8's arrangement, one function over). Nothing here writes 0 to the
   power register, and nothing clears `CORE_PWRCTL_*`.
3. **The GCC, the RCG and `BCR 0x04C0` stay untouched** — the clock and `core_mem` store sets are
   rung 6's, unchanged.
4. **No byte of the medium is written, and no data path is entered.** `BLOCK_SIZE`, `BLOCK_COUNT`,
   `TRANSFER_MODE`, `BUFFER` and `ADMA_ADDRESS` are in no access set of this arm; CMD0 and CMD1 are
   `bc`/`bcr` commands with no data phase, so the controller has no transfer to run. CMD0 puts the
   card in idle state and CMD1 asks it for its operating conditions: **neither command changes one
   byte of what is stored**, which is what the goal's own constraint (「不要让设备彻底死机或者变砖」)
   requires of this rung.

**What the arm costs if the card does not answer:** at most one controller command timeout (~1 s) plus
the two inhibit gates, against a run whose ending is armed at 6,000 ms of idle-window time and whose
runner cap is 28 s. Rung 11's whole device act cost 73 µs.

## 5. The cells this rung pre-registers

Arm level, published once by the probe:

* **`_cmd_calls = 0x1`** — one command path.
* **`_cmd_int_enable = 0x0` and `_cmd_sig_enable = 0x0`** — **the gate's inputs and the outcome-4
  firewall.** A non-zero value here means the arm refuses (`_cmd_enabled_out = 1`, `_cmd_sent = 0`)
  and the run is void as a comparison — and that refusal is itself a finding, because it would mean
  the block can raise a line nobody owns.
* **`_cmd_sent`** — 0, 1 or 2: the number of `COMMAND` writes. 2 is the expected value.
* **`_cmd_gated = 0x0`** — 1 if CMD1 was withheld because CMD0 did not complete cleanly.
* `_cmd_ps_before`, `_cmd_ps_after` — `PRESENT_STATE` around the whole act. Rung 11 measured
  `0x01f80000` (both inhibit bits clear, every DAT and CMD line high), so `_cmd_ps_before` is expected
  to be the same word and its **bit 0 being set is the first refusal**.

Per command, `_cmd0_*` for CMD0 and `_cmd1_*` for CMD1:

* **`_cmdN_op`, `_cmdN_arg`** — 0/0 and 1/0. The arguments are the driver's (`mmc_ops.c:107`,
  `mmc.c:1923`).
* **`_cmdN_word`** — **`0x0000`** and **`0x0102`**. This is the cell that *is* the table in §2, and it
  is published from the register's input rather than from a derivation: a mismatch here is a
  transcription error in this document and not a fact about the hardware.
* **`_cmdN_inhibit_before = 0x0`**, `_cmdN_inhibit_polls`, `_cmdN_inhibit_ticks`,
  **`_cmdN_inhibit_timeout = 0x0`**. Rung 11's `_pwr_ps_after` says the controller is idle, so the
  wait is expected to pass on its **first** read (`_polls = 1`), as every branch halt check on rung 6
  did. A `1` here is the outcome-3 refusal.
* **`_cmdN_stale`** — `INT_STATUS` as found. Rung 4's reset cleared it and nothing since has written
  it, so **`0x00000000` is expected**; the *alternative* is the one that matters: **bit 0 set** means
  a completion is already latched and the clear below is what makes the poll readable, and any other
  bit set is a fact about the block that this ladder has not seen.
* **`_cmdN_clear_wrote`** — the W1C value, equal to `_cmdN_stale` by construction (this is the one
  cell whose value is fixed by the arm's own code, and it is published so that "the clear wrote what
  was read" is a reading and not an assumption — m723's rule: a cell fixed by construction predicts
  nothing, so it is pre-registered as a *consistency* cell and not as evidence).
* **`_cmdN_clear_after = 0x0`** — the clear confirmed. **A non-zero here refuses the command**
  (`_cmdN_sent = 0`), because a latch that will not clear makes the poll meaningless.
* **`_cmdN_sent = 0x1`**, `_cmdN_polls`, `_cmdN_ticks`, `_cmdN_status_after`, `_cmdN_timeout`.
* **`_cmdN_complete = 0x1`** — `status_after & SDHCI_INT_RESPONSE` (`0x1`).
* **`_cmdN_err = 0x00000000`** — `status_after & SDHCI_INT_ERROR_MASK` (`0xffff8000`). **This is the
  cell that separates the two failure modes**: `0x00010000` (`SDHCI_INT_TIMEOUT`) is "the command went
  out and the card did not answer", while any other error bit is about the controller.
* `_cmdN_ps_after` — `PRESENT_STATE` after the command.

And the two cells this press is actually for:

* **`_cmd0_resp` is not read at all** — and `_cmd0_rsp_present = 0x0` is the cell that says so, since
  CMD0's flags carry no `MMC_RSP_PRESENT` (`sdhci_finish_command`'s own guard). Its absence is the
  control for the next line.
* **`_cmd1_resp` — the R3 response word, and the arm's whole answer.** It is pre-registered as a
  **shape**, not one value, because the OCR is the card's to state: **bit 30 set** (access mode) with a
  **non-zero voltage window in bits 23:15** is the form Linux logs for an eMMC that has answered, and
  the two reference words are `0x40ff8080` and `0xc0ff8080`. The alternatives are all different
  findings: **`0xffffffff` or `0x00000000`** with `_cmd1_complete = 1` is a response the controller
  read off a line nothing drove — a fact about the *bus*, which is what the pull-ups and the card's
  absence would look like; **`_cmd1_resp` present with `_cmd1_complete = 0`** is a stale word and not
  an answer (`sdhci_finish_command` is only reached on completion); and **`_cmd1_err = 0x00010000`** is
  the card's silence, stated by the controller.
* **`_cmd1_resp_busy = (resp >> 31) & 1`** and **`_cmd1_resp_voltage = resp & 0x00ff8000`** — the two
  derived faces of it that the driver itself uses: `mmc_send_op_cond`'s loop tests
  `cmd.resp[0] & MMC_CARD_BUSY` (`mmc_ops.c:156`, `0x80000000`) and `mmc_select_voltage` masks the
  window against `host->ocr_avail`. **Both are published raw-adjacent on purpose**: the bit's *meaning*
  is the driver's convention and not this document's, so the record quotes the code that tests it and
  does not translate it.

**Inherited and expected unchanged:** everything rung 10's record lists — `_wait_timeout = 0`,
`_wait_ctl_after = 0x01`, `_wait_polls = 1`, `_wait_ticks ≈ 1,403`, `_wait_done = 1`,
`_wait_cpsr = 0x80000013`, `_wait_cpsr_after = 0x20000093`, `_wait_cc_before = _wait_cc_after =
0xe047`, `_pwr_irq_calls_probe_end = 1`, `_storage_calls = 1`, `_loads = 6`, `_writes` 0→4,
`_rst_stores = 1`, `_mode_bit_after = 1`, `_gate = 1`, `_bcr = 0x0`, `_cbcr = 0x4ff1`, `_gcc_map = 1`,
`_map = 1`, `_pwr_before = 0x00` → `_pwr_after = 0x0b`, `_pwr_vdd = 7`, `_clk_set_writes = 0x8`,
`_clk_set_rate_writes = 0x0`, `_clk_set_cc_card = 0x0000e045`, and the ending firing
(`_post_end_calls = 0x7` or `0x8`, `_post_elapsed` past 6,000 ms at `_post_cntfrq = 0x0124f800`).

**And the write set is the whole safety contract, restated as a delta:** the rung adds **three
stores per command call and nothing else** — `INT_STATUS` (the W1C clear), `ARGUMENT`, `COMMAND`. No
store is added to any window but `hc_mem`, no store to the GCC, no store to `core_mem`, no store to
the medium, and no data-path register is touched.

## 6. The outcomes, each a different next rung

1. **`_cmd1_complete = 1`, `_cmd1_err = 0`, `_cmd1_resp` of the answered-card shape.** The card
   answered. The push is `mmc_attach_mmc`'s next statements — CMD2 (`mmc_all_send_cid`, a 136-bit
   response, and the first `MMC_RSP_136` read: four words with the driver's shift), CMD3
   (`mmc_set_relative_addr`), CMD9 (`SEND_CSD`), CMD7 (`SELECT_CARD`) — and then the first *data*
   command, CMD17 at LBA 1, where 531 §9's self-verifying reading lives: the eight bytes `EFI PART`
   or an MBR ending `55 aa`. The rung above this one is the 136-bit response; the one that answers the
   goal's other half is the block read.
2. **`_cmd1_complete = 0` with `_cmd1_err = 0x00010000`** (`SDHCI_INT_TIMEOUT`). The command path
   works and **nothing answered**. This is the outcome that re-opens the three steps §1 talked out of
   this rung, and it re-opens them *in a measured order*: the CMD line's level in `_cmd1_ps_after`
   (if it is low, the card is holding it), the SDCLK actually reaching the card (the RCG rate write),
   and the card's rail (the vendor's three sleeping arms). **A timeout is not a wasted press** — it is
   the first statement this line has ever had about the card rather than the controller.
3. **`_cmd0_err` non-zero (and so `_cmd_gated = 1`, `_cmd_sent = 1`).** The command path itself is
   wrong. The next reading is the controller's error decode: a CRC or index error with
   `RESP_NONE` is impossible, so CRC/INDEX here would mean the *word* in `_cmd0_word` is not the word
   written, and the first thing to re-read is `st_send_command`'s own disassembly.
4. **`_cmd_enabled_out = 1`, `_cmd_sent = 0`.** `INT_ENABLE` or `SIGNAL_ENABLE` was non-zero before any
   command: the block can raise SPI 123, which this image hands to nobody and whose delivery would
   *end the run* at the dispatcher. The arm is void as a comparison and the finding is a real one —
   the next rung is a client for that line (rung 8's shape, intid 155), or a read of the distributor's
   `ISENABLER1` to see whether the line is even enabled at boot.
5. **`_cmd0_inhibit_timeout = 1` or `_cmdN_clear_after != 0`.** The controller is busy or its latch
   will not clear: no command was issued, `_cmd_sent = 0`, and the arm's own gates did their job.
   What it costs is the press and what it buys is the register state that says so.
6. **No return (exit 2).** This arm's own hazard, named rather than discovered: **the first act of
   this line that puts a transaction on the bus**. A card that drives the line into a state the
   controller waits on with its own ~1 s timeout is bounded by §3's budget, the inhibit gate is
   bounded by 10 ms, the watchdog is armed, nothing is flashed, and the never-brick constraint is
   intact — but a non-return costs the press and refutes none of the cells above.

## 7. What this arm does NOT do

No data command, no block, no sector, no partition table, no filesystem, no mount, no DMA, no data
path register at all, no interrupt enabled, no store outside `hc_mem`, no write to the medium, and no
byte of the card's storage touched. It issues two `bc`/`bcr` commands, one of which is answered or is
not, and it reads the answer.

**The goal is still not met by this rung, and this rung's own answer does not meet it either** — the
card answering is a *precondition* of reading the medium and not the reading. **TWRP-to-storage stays
withheld**: the goal's condition (「如果os已经能进去了的话」) is still unmet, and a card that answers
CMD1 is not an OS that is in. What the press buys, whichever way it comes back, is that for the first
time in this line the *medium* is addressed, so every rung above it is about the card instead of
about the controller.
