# 724: the rung-13 pre-registration — the register state at the instant of the command, the command's own return path, and three record defects found while checking

`STAGE90_XNU_STORAGE_PROBE=12`. The rung below it is [721/722/723](experiment-723-the-rung-12-press-the-command-went-out-and-nothing-latched.md),
whose press returned **EXIT 0** and whose answer was **31 cells and nothing latched**: `SDHCI_COMMAND 0x0E`
was written with the driver's own `0x0000` for CMD0, all three gates held, and `SDHCI_INT_STATUS 0x30` read
back **`0x00000000` on all 5,140,480 polls** for 1.200 s — not the completion bit, and not the block's own
command-timeout bit either.

**No device action in this step.** No build, no press; one press to follow, `fastboot boot` only.

## 1. 723 section 3's first half is refuted by 723's own log, and this is the correction

723 §3 ended with a two-part rung above it. Its **first** part read:

> "**the standard block's own `POWER_CONTROL 0x29` reads 0x00** (`_reg_power_control`,
> `_reg_power_control_after`) ... This ladder has never written that byte ... **The driver writes it, and it
> is the one part of its start-up this line has never ported.**"

**Every clause of that is wrong, and the correction is three lines out of the rung-12 log itself:**

| cell | value | what it is |
|---|---|---|
| `_reg_power_control` | `0x00` | the byte at `hc_mem + 0x29`, read by **rung 3**'s census (`entry_storage.c:630`) |
| `_reg_power_control_after` | `0x00` | the same byte, read by **rung 4**'s `st_driver_reset` stage-3 tail (`:783`) |
| `_pwr_before` | `0x00` | **rung 7**'s read of that byte, immediately before its store (`:1372`) |
| `_pwr_wrote` | `0x0b` | the value rung 7 stores (`:1383-1384`) |
| `_pwr_after` | **`0x0b`** | the same byte, read back after the store (`:1386`) |

**The register takes the write and holds it.** `0x00 → 0x0B`, measured, in the rung-12 log. And the two cells
723 §3 cited are *earlier in the same run*: rung 3 and rung 4 both run **before** rung 7, so both read the
byte while it is still `0x00`. 723 §3 read a **stale** value as the current one — one register, two readings at
two times, and the document used the earlier (the `mi4-one-value-two-definitions` class, and the same shape as
`m727`).

**And 723 §3 quoted the write set that refutes it while using it as evidence.** The sentence reads:

> "`build_entry.sh` asserts that `hc_mem`'s write set is unchanged at `47 44 44 41`, which is what makes a
> store to `POWER_CONTROL` — where writing 0 is a bus-off request on this SoC (531 §8) — unreachable by
> construction."

`47 44 44 41` are offsets `0x2F`, `0x2C`, `0x2C`, **`0x29`** — and `0x29` **is** `POWER_CONTROL`. The arm's own
clause comment (`build_entry.sh:30353-30368`) says so in as many words: "*the driver's own FIRST POWER BYTE at
POWER_CONTROL 0x29 (sdhci.c:1368-1370, mmc_power_up's pass A)*". 723 §3 read the fourth offset of its own
quoted set as evidence of that offset's absence. That is `mi4-a-claim-in-a-comment-is-not-a-check` one step
further: a claim that quoted the artifact contradicting it.

What the byte actually differs by is **voltage, not presence**. Rung 7 derives `SDHCI_POWER_180` because
`_mode_capabilities = 0x742dc8b2` has `SDHCI_CAN_VDD_180` (`0x04000000`) set and `CAN_VDD_330` / `CAN_VDD_300`
clear, so `ocr_avail = MMC_VDD_165_195` (`0x80`) and `fls(0x80)-1 = 7` selects the 1.8 V arm of
`sdhci.c:1317-1334`. The driver's own byte on this variant is
`SDHCI_POWER_ON | SDHCI_POWER_330 = 0x0F` (`sdhci.h:88-91`, `SDHCI_QUIRK_SINGLE_POWER_WRITE` at
`sdhci-msm.c:2897`). **So the arm writes `0x0B` where the driver writes `0x0F`, and the difference is the
voltage field — not the bus-power bit, which both set.** The correction is recorded in 723 itself as a dated
section; this document does not restate it as a claim of its own.

### 1.1 Two more record defects, found while checking §1, and neither is a cause

**(a) `host->ioaddr` is `hc_mem`, and the base table proves it — but rung 6's wrong window is a no-op.**
`entry_storage.c:316-327` (710) asserts that every one of the vendor's 23 uses of `CORE_VENDOR_SPEC` goes
through `host->ioaddr` and that `host->ioaddr = hc_mem`. Read as a base table over the whole file, it holds
and it is *decidable from the source*: `host->ioaddr` is the base for `SDHCI_INT_STATUS 0x30` (`sdhci.c:2831`)
and `SDHCI_HOST_VERSION 0xFE` (`sdhci-msm.c:2909`) — the **standard** register file, whose offsets are inside
`hc_mem`'s declared window and outside everything else — while `msm_host->core_mem` is the base for
`CORE_MCI_VERSION 0x050`, `CORE_HC_MODE 0x78`, `CORE_TESTBUS_CONFIG 0x0CC` and the four `CORE_PWRCTL_*`
(`0xDC`/`0xE0`/`0xE4`/`0xE8`). The DT agrees: `hc_mem` is `<0xf9824900 0x11c>`, and `0x11C` is exactly
`CORE_VENDOR_SPEC 0x10C` + 4 — the window ends at the register. So `CORE_VENDOR_SPEC` is `0xf9824a0c` and
rung 6's stores to `ST_CORE_MEM_BASE + 0x10C` = `0xf982410c` are at the other window, which is why
`_clk_set_vendor_after = 0` while `_pwr_irq_vendor_hc` reads `0xa1c`.

**But the vendor's two stores are value-no-ops at the right window, and rung 12's own log already carries the
number that proves it.** `sdhci-msm.c:2496-2513` (the non-HS400 arm) computes

```
w1 = (readl(ioaddr + CORE_VENDOR_SPEC) & ~CORE_HC_MCLK_SEL_MASK) | CORE_HC_MCLK_SEL_DFLT
w2 =  readl(ioaddr + CORE_VENDOR_SPEC) & ~CORE_HC_SELECT_IN_EN & ~CORE_HC_SELECT_IN_MASK
```

and `_pwr_irq_vendor_hc = 0x00000a1c` has bits 9:8 = `0b10` = `CORE_HC_MCLK_SEL_DFLT` **already** and bit 18
(`CORE_HC_SELECT_IN_EN`) **already clear**. So `w1 = 0xa1c` and `w2 = 0xa1c` — **each store writes back the
word it read.** The window error is real and is a defect of the *record* (`_clk_set_vendor_before/after` are
about `0xf982410c`, a register the driver never addresses), and it is **not** a cause of the silence: moving
the two stores to `0xf9824a0c` would change no bit of the block's state. Rung 13 reads the vendor's window at
the clock set's own moment and gates the store on that arithmetic, so "value-neutral" is measured rather
than argued.

**(b) The SDCC1 apps clock is 200 MHz, so `ST_SET_MAX_CLK` names the wrong table — and the divider is 2x off.**
Rung 6 compiles `ST_SET_MAX_CLK = 384000000` from `msm8974pro.dtsi:1768` and publishes it as
`_clk_set_max_clk = 0x16e36000`; `msm8974.dtsi:507` (the same node, the base table) caps at `200000000`.
**The hardware answers this one without a press.** Rung 5's `_clk_rcg_cfg = 0x00000507` decodes to
`SRC_SEL = 5`, `DIV = 7`; the vendor's own `_rcg_clk_handoff`/`set_rate_mnd` pre-divider is
`(div_val + 1) >> 1 = 4`, and `clock-local2.c:290-299` treats `CMD_RCGR_ROOT_STATUS_BIT (1<<31)` as
"disabled" — `_clk_rcg_root_status = 0`, so **the root is enabled** (the `ROOT_ENABLE` bit 1 that
`clock-local2.c:61` defines is never written anywhere in that file; `_clk_rcg_root_en = 0` is the software
request bit, not the state). A parent of 800 MHz divided by 4 is **200 MHz**. So the block's own clock is
200 MHz, `ST_SET_MAX_CLK_STD` is the right constant, and `_clk_set_div = 0x1e0` (480) yields 208 kHz rather
than the 400 kHz intended — `_clk_set_divisor_alt = 0` records the arm's belief and the RCG refutes it.
208 kHz is a legal initialisation clock, so this too is a record defect and not a cause.

**The three defects share one shape and it is worth naming**: each is a value the arm *published* about the
device that is a fact about the arm — a stale reading, a store at a window the driver does not use, and a
table the hardware disagrees with. None of them is the missing answer, and all three would have been found by
a rung that reads the same quantity **at the moment it matters**.

## 2. What this rung is, and why it is a measurement and not another attempt

Rung 12 established the only thing it could establish by itself: the arm's command path matches the driver's
own beginning, and the block answered nothing. Five hypotheses remain, and **the arm cannot currently tell
three of them apart**:

1. **The command was never issued.** `SDHCI_CMD_INHIBIT` never rose, and `COMMAND 0x0E` may not even hold the
   word — the write landed in a register the block is not transmitting from.
2. **The command was issued, ran and completed, and the arm's poll read the wrong bit.** The arm polls
   `SDHCI_INT_RESPONSE` (bit 0) only. If the block latched a *different* bit, the poll ran its whole 1.2 s
   budget while a one-bit answer sat in the register the whole time.
3. **The command is in flight and the block is waiting.** `CMD_INHIBIT` rose and stayed — the block started
   the command and never finished it, which points at the clock or the card rather than at the register file.
4. **The bus is not powered at the instant of the command.** §1's correction says the byte is written and
   takes; it does not say the bit is still set *at the moment the command is issued*.
5. **The block's clock root is not running.** §1.1(b) turns this from a suspicion into a reading — the root
   is enabled and the tree is at 200 MHz — so the rung *re-takes* that reading at the command's own moment
   rather than trusting rung 5's.

**The rung's act is therefore: read every register the command depends on immediately before the command,
and read every register that could carry its answer immediately after it.** It makes **no new store**, so the
whole safety contract — the write set that has been `47 44 44 41` since 701 — is unchanged, and the rung is
the cheapest possible change to it.

## 3. The act in the source: three bodies, one of which is new

`,,xnu_arm_boot/entry_storage.c`, one `#if` block above `entry_storage_probe`:

- **`st_cmd_census`** — new, `noinline, noclone`, **reads only**, called immediately before `st_cmd_path`.
  It re-takes rung 5's clock-surface readings and rung 7's power byte **at the command's own moment**:
  `POWER_CONTROL 0x29` (byte), `CLOCK_CONTROL 0x2C` (halfword), `PRESENT_STATE 0x24`, `INT_ENABLE 0x34` and
  `SIGNAL_ENABLE 0x38`, and the GCC's `SDCC1_APPS_RCG` (`CMD_RCGR` and `CFG_RCGR`, decoded into
  root_en / root_status / src / div) plus `SDCC1_APPS_CBCR` and `SDCC1_AHB_CBCR` and the BCR. Its clause
  asserts a **non-empty, exact read set and an empty store set** — the reverse of the usual emphasis,
  because the failure this body could hide is a *store*, and rung 11's own lesson is that a body asserted
  only by its emptiness is a body nobody is checking.
- **`st_send_command`** — extended, same three stores in the same order, and its return path becomes the
  point of the rung:
  - `_cmd0_word_read`: `COMMAND 0x0E` read back with `st_read16`, immediately after the store.
  - `_cmd0_inhibit_after`: `PRESENT_STATE` read immediately after the `COMMAND` store.
  - `_cmd0_inhibit_seen` / `_cmd0_inhibit_last`: `PRESENT_STATE` sampled on the **first 1024** poll
    iterations only (CMD_INHIBIT's rise and fall are at the start of the transmission; sampling every
    iteration would double the poll's device traffic and halve the run's coverage for no reading). Seen
    counts the iterations in which bit 0 was set.
  - `_cmd0_status_any` / `_cmd0_status_polls_to_any`: the **first non-zero `INT_STATUS` of any kind** and
    the poll index at which it appeared. This is the cell that turns "nothing latched" into "exactly this
    bit latched, at this poll".
  - `_cmd0_resp_read`: `RESPONSE 0x10` read **unconditionally** (rung 11 read it only when
    `MMC_RSP_PRESENT`, which CMD0 never is — so for CMD0 the register has never been read at all).
  - The loop's break condition gains the driver's own second arm: `sdhci_cmd_irq` (`sdhci.c:2867-2876`)
    finishes the command on any `SDHCI_INT_CMD_MASK` bit, error or not, so the arm breaks on
    `(status & (RESPONSE | INT_ERROR)) != 0`. `_cmd0_status_after`, `_cmd0_complete`, `_cmd0_err`,
    `_cmd0_polls` and `_cmd0_ticks` keep their rung-11 meanings, and `_cmd0_status_any` is published beside
    them so the narrower poll's answer and the wider one's are two numbers under two names.
- **`st_cmd_path`** — unchanged in its gates, with the census called first and its outcome table gaining the
  cell rung 12's §2 identified as missing.

`struct st_cmd_result` gains four `uint32_t` fields. It is a **stack** struct in `st_cmd_path`, so `.data`
and `.bss` are unchanged — the "this rung adds no instrument state" claim survives, and the clause re-asserts
it rather than assuming it.

## 4. The safety contract, unchanged, and the gates re-read at the moment of the command

1. **Write set.** `hc_mem`'s four-offset set is still `47 44 44 41`, and `st_send_command`'s three stores are
   still `INT_STATUS 0x30` (W1C, 32-bit), `ARGUMENT 0x08` (32-bit), `COMMAND 0x0E` (16-bit), in that order.
   **The rung adds no store anywhere**, which `st_cmd_census`'s clause asserts directly.
2. **`INT_ENABLE 0x34` and `SIGNAL_ENABLE 0x38` read 0 — now twice.** Rung 12 read them once before the
   command. The census reads them **immediately before it**, because the run-protecting gate is the one thing
   a rung above must not assume from a rung below. Non-zero at the census ⇒ the command is not issued.
3. **`POWER_CONTROL`'s bus-power bit is set** at the census. If it is clear, the command is not issued and
   the cell says so. This is 531 §8's hazard read in the *safe* direction: the arm refuses rather than
   writing.
4. **The inhibit gate and the latch gate** are rung 11's, unchanged: `CMD_INHIBIT` must clear inside the
   driver's own 10 ms, and `INT_STATUS` is read, written straight back (W1C) and read again.
5. **CMD1 is still issued only if CMD0 completed with no error.** The gate stays, and the outcome table now
   *names* its refusal as an outcome, because rung 12 §2's whole cost was a cell the table lacked.
6. **`no store to `core_mem`, no store to the GCC, no `POWER_CONTROL` store, no data-path register, no byte
   of the medium.** The `core_mem` window's clause is unchanged and the GCC's stays empty below rung 6 and
   asserted-empty for stores at this rung.

## 5. The cell table, and the outcomes it must be able to report

| cell | reads | the two answers it separates |
|---|---|---|
| `_cmd2_power_control` | `hc_mem+0x29` (byte) | bit 0 set ⇒ hypothesis 4 is dead, by a reading taken at the moment |
| `_cmd2_clock_control` | `hc_mem+0x2C` (halfword) | `INT_STABLE`+`CARD_EN` both set, one register earlier than rung 12 took it |
| `_cmd2_int_enable`, `_cmd2_sig_enable` | `hc_mem+0x34`, `0x38` | both 0 ⇒ the run-protecting gate still holds at this instant |
| `_cmd2_present_state` | `hc_mem+0x24` | the block is idle before the command (bit 0 and bit 2 clear) |
| `_cmd2_rcg_root_status`, `_cmd2_rcg_root_en`, `_cmd2_rcg_src`, `_cmd2_rcg_div`, `_cmd2_rcg_cfg` | GCC `SDCC1_APPS_RCG` | hypothesis 5: status bit 31 clear ⇒ the root runs; `SRC_SEL=5`,`DIV=7` ⇒ 200 MHz, not 384 |
| `_cmd2_apps_cbcr`, `_cmd2_ahb_cbcr`, `_cmd2_bcr` | GCC `0x04C4`, `0x04C8`, BCR | the two branches are still enabled at this instant |
| `_cmd0_word_read` | `hc_mem+0x0E` (halfword) | `0x0000` ⇒ the block holds the driver's word; anything else ⇒ the store did not land where the arm thinks |
| `_cmd0_inhibit_after`, `_cmd0_inhibit_seen` | `hc_mem+0x24` | **hypotheses 1 / 2 / 3 separated**: seen = 0 ⇒ never started; seen > 0 and the command completed ⇒ ran; inhibit still set at the end ⇒ in flight |
| `_cmd0_status_any`, `_cmd0_status_polls_to_any` | `hc_mem+0x30` | **hypothesis 2**: a non-zero value says the arm's rung-11 poll was reading the wrong bit |
| `_cmd0_resp_read` | `hc_mem+0x10` | the response register, read for the first time on a no-response command |
| `_cmd0_status_after`, `_cmd0_complete`, `_cmd0_err`, `_cmd0_polls`, `_cmd0_ticks`, `_cmd0_timeout` | unchanged | rung 11's own cells, so the two runs' answers are comparable cell for cell |

The outcomes the table must be able to report, named before the press:

1. **`_cmd0_inhibit_seen = 0`** and `_cmd0_word_read = 0x0000`: the block holds the word and never
   transmitted it. The missing thing is *before* the command register — the clock path or the block's mode.
2. **`_cmd0_inhibit_seen > 0`, `_cmd0_status_any != 0`**: the command ran, and the arm's rung-11 poll was
   reading a bit the block does not set for a no-response command. The answer to rung 12 is then *in rung
   12's own log*, one bit over.
3. **`_cmd0_inhibit_seen > 0`, `_cmd0_status_any = 0`, `_cmd0_inhibit_last` bit 0 set**: the command is in
   flight and stuck. The next rung is the card's rail and the SDCLK, not the register file.
4. **`_cmd0_word_read != 0x0000`**: the store did not land where the arm believes, and every cell above it
   is about a register that is not the command register.
5. **`_cmd2_power_control` bit 0 clear, or `_cmd2_int_enable`/`_cmd2_sig_enable` non-zero**: the census
   refuses, the command is not issued, and the rung's answer is *that* — a run that bought one reading and
   spent no command, which is the correct trade.
6. **`_cmd2_rcg_root_status` bit 31 set**: hypothesis 5 was live after all and §1.1(b) is wrong about the
   hardware; the next rung is the clock tree and nothing else.
7. **`_cmd0_complete = 1`**: CMD0 completed and the run continues into CMD1 — the first time this line has
   ever issued the second command, and the first statement about the *card* rather than the controller.
8. **CMD0 issued and CMD1 refused** (`_cmd_gated = 1`): a named outcome, so a repeat of rung 12's shape is
   read as the rung's contract working rather than as a surprise.

## 6. What the rung is not

No data command, no block, no sector, no partition table, no filesystem, no mount, no DMA, no data-path
register, no interrupt enabled, **no new store anywhere**, no `core_mem` store, no GCC store, no
`POWER_CONTROL` store, no write to any RCG word, and no byte of the medium. **TWRP-to-storage stays
withheld**: 「如果os已经能进去了的话」 is unmet, and nothing in this rung can meet it — the rung's whole value
is that the *next* one will know which of five things it is about.
