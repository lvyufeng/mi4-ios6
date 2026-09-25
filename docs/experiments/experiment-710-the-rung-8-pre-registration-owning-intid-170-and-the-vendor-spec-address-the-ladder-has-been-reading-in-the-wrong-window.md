# 710: the rung-8 pre-registration — owning intid 170, the handler whose three arms cannot run in exception context, and the `CORE_VENDOR_SPEC` address the ladder has been reading in the wrong window

709 pressed rung 7 and the press answered a question the pre-registration did not ask: the power byte
took and its readback held, the controller's answer was `CORE_PWRCTL_STATUS = 0x02` (a bus-on
request), and that request arrived as **an interrupt on intid 170** — the line this image hands to
nobody, which is what ended the run (`_irq_other_iar = 0xaa`). 709 §3 left the next rung as a
**decision**, not a reading: mask the power events (`CORE_PWRCTL_MASK <- 0`) or own intid 170 and
clear the latch. **This rung takes the second.** The reason is not taste: the registry
`entry_irq_register_client` and the per-line arming `entry_irq_enable_line` already exist, were
built and measured at 496/498/500, and this rung is their first caller in the ladder — and masking
the events would leave the driver's own power handshake permanently incomplete, because
`sdhci_msm_check_power_status`' completion is written by **the handler this rung installs**.

Reading the same path for the handler's body also produced a finding about the ladder itself, and it
is §1.5 rather than a footnote: **`CORE_VENDOR_SPEC 0x10C` is written through `host->ioaddr`, 23
times out of 23, and this image has read and written it through `core_mem`.** Rung 6's
`_clk_set_vendor_after = 0` is a reading of the wrong window, and 707 §2's "the field did not take"
is an address error. The rung's reading half is placed here because this rung's handler is the first
thing in the ladder that writes that register at the vendor's own address.

## 1. The path, read out of the vendor's own source

### 1.1 `sdhci_msm_pwr_irq`'s five acts

`sdhci_msm_pwr_irq` (`sdhci-msm.c:1990-2099`) is one `readb`, one `writeb`, then a decode, then a
`writeb`, then a second read-modify-write — and the whole of the ladder's IRQ debt is in the order:

| # | line | the act | in this image |
| --- | --- | --- | --- |
| 1 | `:2001` | `irq_status = readb_relaxed(core_mem + CORE_PWRCTL_STATUS 0xDC)` | **the rung's read** |
| 2 | `:2006` | `writeb_relaxed(irq_status, core_mem + CORE_PWRCTL_CLEAR 0xE4)` — the latch's acknowledge | **the rung's first store** |
| 3 | `:2012` | `mb()` — the vendor's own comment: `core_mem` and `hc_mem` "do not fall within 1KB region", so a later `hc_mem` update needs it | taken, and see §1.2's note: this image's `st_write8`/`st_read8` each carry `dsb sy` |
| 4 | `:2016-2067` | the decode: `BUS_ON`/`BUS_OFF` → `sdhci_msm_setup_vreg` + `sdhci_msm_setup_pins` + `sdhci_msm_set_vdd_io_vol`, `IO_LOW`/`IO_HIGH` → `set_vdd_io_vol`; `irq_ack` gets `BUS_SUCCESS`/`BUS_FAIL` or `IO_SUCCESS`/`IO_FAIL` | **the three PMIC arms are absent — §1.2** |
| 5 | `:2069` | `writeb_relaxed(irq_ack, core_mem + CORE_PWRCTL_CTL 0xE8)` — the answer to the controller | **the rung's second store** |
| 6 | `:2074-2085` | `mb()`, then **`host->ioaddr + CORE_VENDOR_SPEC`**: `IO_HIGH` clears `CORE_IO_PAD_PWR_SWITCH` (bit 16), `IO_LOW` sets it — a read-modify-write **in `hc_mem`** | **the rung's third store, and §1.5's subject** |
| 7 | `:2092-2096` | `curr_pwr_state`/`curr_io_level` ← the decode's own values, then `complete(&pwr_irq_completion)` under `host->lock` | absent, and named in §4 |

**One consequence of the decode is worth a sentence before the table is used to design anything.**
For `irq_status = BUS_ON` the vendor sets **both** `pwr_state = REQ_BUS_ON` *and*
`io_level = REQ_IO_HIGH` (`:2028-2029`, after the ack bits and outside the `ret` test). So act 6 runs
on the very status 709 measured: `IO_HIGH` clears the pad bit. The handler for this press is
therefore a **three-store** handler — `CLEAR`, `CTL`, and `hc_mem`'s `0x10C` — and not the two-store
one a reader would predict from the `readb`/`writeb` pair at the top.

### 1.2 Why three arms cannot be in this image, and the vendor says so itself

`devm_request_threaded_irq(..., NULL, sdhci_msm_pwr_irq, IRQF_ONESHOT, ...)` (`:2937-2939`): a
**NULL primary handler over `IRQF_ONESHOT`** makes `sdhci_msm_pwr_irq` a **threaded** handler — it
runs in a kthread, not in exception context. The reason is in the arms it contains:
`sdhci_msm_setup_vreg` touches the regulator framework, `sdhci_msm_setup_pins` calls
`pinctrl_select_state`, `sdhci_msm_set_vdd_io_vol` sets a regulator voltage — all three may sleep,
and none of them may run in an exception.

**So the three absent arms are absent for a structural reason and not for convenience**: this image's
client is called from `entry_irq_handler`, i.e. from `fleh_irq_kernel`'s frame, with interrupts
masked and no thread to sleep in. A rung that wanted them would need a kernel thread first. What
remains of the handler *is* the register handshake — which is exactly the part the controller needs
and the part 709 measured the controller asking for.

**And the ack value it writes is the vendor's own, on a path with no regulators at all.** The probe's
pre-acknowledge (`:2872-2889`) reads `CORE_PWRCTL_STATUS`, writes it to `CORE_PWRCTL_CLEAR`, ORs
`CORE_PWRCTL_BUS_SUCCESS` into `CORE_PWRCTL_CTL` **because the status said `BUS_ON`** — with no vreg
call and no card present — and writes `CTL` back, then `mb()`. Its comment says why: *"CORE_SW_RST
above may trigger power irq if previous status of PWRCTL was either BUS_ON or IO_HIGH_V. So before we
enable the power irq interrupt in GIC... we need to ensure that any pending power irq interrupt
status is acknowledged."* **This rung's handler is that preamble's own code, moved from probe time
to the moment the line is raised, plus the decode that names which event it was.** A reader who
objects that the image acks success without having powered anything is objecting to the vendor's own
probe, three thousand lines earlier in the same file.

### 1.3 The line, and what "completable" would mean

`check_power_status` (`:2179-2209`) early-returns when `req_type & msm_host->curr_pwr_state` and
otherwise `wait_for_completion`s. Both fields are written **only** by act 7 (`:2092-2096`) — so the
driver's power-up needs a handler that runs, and 709's press is the evidence that the request the
handler would answer does arrive. Rung 8 installs the handler. It **does not** emulate the
completion and it does not call `check_power_status`: `curr_pwr_state` is a field of a `struct
sdhci_msm_host` this image does not have, and a cell computed from *this image's own* reading of
`STATUS` would be a second definition wearing the driver's name
([[mi4-one-value-two-definitions]] m717/m718). What the rung publishes instead is what it can
support: the status as read (`_pwr_irq_status`), the latch cleared (`_pwr_irq_status_after`), and
the ack written (`_pwr_irq_ack`).

### 1.4 The intid, and the sibling this rung does not take

`msm8974.dtsi:502`'s `sdhc_1: sdhci@f9824900` declares `interrupts = <0 123 0>, <0 138 0>` with
`interrupt-names = "hc_irq", "pwr_irq"`. **SPI 138 + 32 = intid 170** — the `0xaa` 709's log carries
— and the sibling `hc_irq` is SPI 123 = **intid 155**, the SDHCI controller's own line, which is the
line `entry_irq.c`'s registry was measured on at 498/500 (intid 40, the timer). This rung registers
**170 and only 170**; 155 is the controller's transfer line and belongs to a rung that submits a
command.

### 1.5 The finding: `CORE_VENDOR_SPEC 0x10C` has two addresses and the ladder has been using one

`#define CORE_VENDOR_SPEC 0x10C` (`:92`) is used **23 times in the file, every one through
`host->ioaddr`** — `:587`, `:597`, `:646`, `:653`, `:2079`, `:2081`, `:2083`, `:2085`, `:2416`,
`:2417`, `:2431`, `:2433`, and the two read-modify-writes 706 quoted (`:2497-2499`, `:2510-2512`) —
and **zero times through `msm_host->core_mem`**. `host->ioaddr` is `hc_mem` (`0xf9824900`,
`msm8974.dtsi:503`), so the register the driver's MCLK select lives in is at **`0xF9824A0C`**, 16
bytes inside `hc_mem`'s declared `0x11c` length — the SDHCI standard's own vendor-specific area,
which is where the standard puts it.

This image reads and writes `ST_CORE_MEM_BASE + ST_CORE_VENDOR_SPEC` = **`0xF982410C`**
(`entry_storage.c:876`, `:1108-1120`), i.e. the *other* window of the same controller, 0x900 bytes
away. Consequences, stated plainly because they change an earlier rung's reading:

* **Rung 6's central finding is an address error.** 707 §2 read `_clk_set_vendor_after = 0` after two
  stores to `0xF982410C` and concluded *"the register read back `0` after a store the artifact shows
  writing `0x00000200`... the field did not take"*. The measurement is what it is; the sentence
  attached to it is not supported, because **the vendor's code never writes that address**. Whether
  the MCLK field took is **unmeasured**, and 706/707's "the vendor's non-HS400 path ran and the
  register says no" becomes "the vendor's non-HS400 path's *register* was not the one this arm
  wrote".
* **The ladder has two readings of one offset and no reading of the other window.** 705's
  `_clk_vendor_*` and rung 6's `_clk_set_vendor_*` are all `core_mem + 0x10C`.
* **The alternative is named rather than excluded.** The two windows are 0x900 apart in one 1 MB
  section; nothing in this project has shown them to be aliases, and the DT declares them as two
  regions. Rung 8's handler writes **`hc_mem + 0x10C`** (act 6) — so this rung puts a *read of both
  addresses in one run* beside that store, and the reading settles it: two different values means two
  registers and 23-vs-0 decides which one the driver meant; one value means the windows alias and
  rung 6 was reading the right register through the wrong name.
* **The source comment that carries the false claim is corrected in this step's commit**
  (`entry_storage.c:275-281` says 0x10C is "the fifth `core_mem` offset and the one register
  `sdhci_msm_set_clock` writes"). A comment-only edit changes no code, and per
  [[mi4-linker-fill-term]] it is proved by rebuilding and `cmp`ing the image — done in this step,
  with the result recorded in §5.

## 2. Pre-registered: what rung 8 is

**Rung 8 = rungs 1–7 unchanged, plus the driver's own power IRQ client**: `entry_irq_register_client(170,
st_pwr_irq, 0)` and `entry_irq_enable_line(170, 0x01)` **before** rung 7's power byte (§3), plus the
handler `st_pwr_irq` — acts 1, 2, 4-ack, 5 and 6 of §1.1 in the vendor's order — plus two readings
this rung adds for §1.5 (`hc_mem + 0x10C` and `core_mem + 0x10C` side by side).

**The registration** — the registry's own keys are the record, and this rung's cells say what it asked
for:

| key | what a reading means, and what the alternative would be |
| --- | --- |
| `_pwr_irq_reg_rc` | `1` — a slot was taken. `0` means the registry refused (`_irq_cli_refused` then names which: a repeat intid, a zero handler, or a full table of 4) |
| `_pwr_irq_reg_intid` | **`170`** — the number `msm8974.dtsi:502`'s `pwr_irq` gives, and the value 709's `_irq_other_iar` carried |
| `_pwr_irq_reg_handler` | `st_pwr_irq`'s own address, read live: the cell that says the filed handler is *this* image's function and not a stub |
| `_irq_cli_seq` / `_irq_cli_slot` / `_irq_cli_capacity` | `1` / **`0`** / `4` — the facility's own keys, and slot 0 is predicted because this ladder build registers nothing else. **Slot 0 is where `_irq_cli_calls0` publishes from**, so the facility's power-of-two key is this client's count and not another line's |
| `_irq_cli_refused` | absent (or `0`) — a refusal would mean the registration is not the one the record describes |

**The line's own distributor state** — `entry_irq_enable_line(170, 0x01)`'s keys, and this is the
**first** call of that function in the ladder (it has no caller today), so `xnu_live_irq_line_*` has
exactly one producer and §5 makes that structural:

| key | expected | the alternative, named |
| --- | --- | --- |
| `_irq_line_isen_before` | bit 170 set — the predecessor's kernel enables this line at its own boot, which is why 709's IRQ was delivered at all | bit clear would mean the delivery came from somewhere else and the arm is now enabling a line that was never heard from |
| `_irq_line_target_before` | `0x01010101` (all four bytes of the word CPU 0) | a zero target byte would be a line the distributor drops whatever the handler is |
| `_irq_line_group_before` | `0` | a set bit would be a line in Group 1, which `GICC_CTLR = 1` (EnableGrp0 only) does not deliver |
| `_irq_line_pend_before` / `_pend_after` | `0` / `0` — this rung's pending-clear runs **before** the byte, so it clears nothing and the pair is the evidence | a set bit before would mean the line was already asserted and the store is *not* what raised it |
| `_irq_line_icfgr_field` | **`0`** (level-sensitive, the GIC's own reset value for an SPI, and what a latch-and-clear handshake needs) | `1`/`2`/`3` (edge) means the predecessor programmed it edge and the vendor's STATUS/CLEAR handshake is running on a line that does not re-assert — a finding about the machine, not about the arm |
| `_irq_line_rc` | `1` — the enable read back | `0` means the write vanished, which is the same gate every clock and reset rung uses |
| `_irq_line_prio` / `_dist_pmr` | the readings, published for the reason 500 gives: "the line was never delivered" and "the line's priority is outside the mask" must be different findings |

**The handler's readings** — published on **every** call and not only the last (497's rule: a counter
written only at the end is a counter that is never read):

| key | pre-registered | what would falsify it |
| --- | --- | --- |
| `_pwr_irq_calls_once_before_byte` | **`0`** — the count read from the probe *before* the power byte | anything else would mean the line was already asserting and this rung's byte is not the cause (the direct falsifier of §3's whole ordering) |
| `_pwr_irq_calls` | **`≥ 1`** — the client was called | `0` with `_irq_other_iar = 0xaa` present is rung 7's ending repeating: the registration did not take effect, and the log says which by whether `_irq_cli_seq` is `1` |
| `_pwr_irq_intid` | `170` — the intid the dispatcher passed | any other intid means another line was routed here |
| `_pwr_irq_status` | **`0x02`** (`CORE_PWRCTL_BUS_ON`, byte width — the vendor's `readb`) | `0x00` means the handler was called for a status that had already gone; `0x01` would be a busy-off request and `0x08` IO_HIGH |
| `_pwr_irq_status32` | **`0x00000002`** — the *same* moment read at the probe's own 32-bit width, published beside the byte | **a disagreement between the two is a reading about the register's width behaviour and is exactly the defect class this project keeps meeting**: one quantity, two widths. The pair is taken because both widths are the vendor's own (`readb_relaxed` here, `writel_relaxed` in the probe's preamble) |
| `_pwr_irq_ctl_before` / `_pwr_irq_ack` | `0x00` / **`0x01`** (`CORE_PWRCTL_BUS_SUCCESS`, the probe's own preamble value for a `BUS_ON` status) | `0x03` would mean the decode saw a `BUS_OFF` bit too, `0x04` an IO event, and `0x00` would mean the byte was zero |
| **`_pwr_irq_status_after`** | **`0x00`** — the latch read back after the `CLEAR` write | **`0x02` means the status did not clear and the line will re-assert — §2's third outcome below** |
| `_pwr_irq_ctl_after` | `0x01` — the `CTL` readback that says the ack took (706's lesson: the store's own readback is the cell) | a `CTL` that reads back `0` would be a second register whose write does not hold |
| `_pwr_irq_pad_ctl_before` / `_after` | `hc_mem + 0x10C` read 32-bit either side of act 6: the field is a read-modify-write, so the expected movement is **bit 16 cleared** (`CORE_IO_PAD_PWR_SWITCH`, `:97`, `:2079-2080` for `IO_HIGH`) and every other bit unchanged | bit 16 already clear means the read-modify-write wrote the word it read — a store that changes nothing, which is readable and is not a failure |
| `_pwr_irq_vendor_core` / `_pwr_irq_vendor_hc` | **§1.5's two addresses, read in one run**, both *before* the byte: `core_mem + 0x10C` (the ladder's, expected `0x00000000` — rung 6's own `_clk_set_vendor_before`) and `hc_mem + 0x10C` (the **vendor's**, never read by this project) | **two different values means two registers and the vendor meant `hc_mem`**; one value means the windows alias. This is the cheapest cell in the rung and it retires an owed item whichever way it reads |

**Three outcomes, and every one of them is a line in the log** — 709's ending, and what replaces it:

1. **the handler clears the latch** → the run goes on. Predicted cells: `_irq_other_count` **absent**
   (rung 7's `1` gone), `_irq_cli_storm` absent, `_post_end_calls` **present** (the 690 ending is
   reached again, and its cells are read for the first time since rung 6) at
   `_post_elapsed ≈ 0x06e0953c` = 6,006 ms, the panic at `0x0fa0065c`, `_slot_post_calls` reaching 4.
2. **the handler runs but the clear does not take** (`_pwr_irq_status_after = 0x02`) → the level line
   re-asserts, the dispatcher calls the client again, and `STAGE90_IRQ_CLIENT_CAP` (**64**,
   `entry_gic.h:267`) is the bound: `_irq_cli_storm = 1`, `_pwr_irq_calls = 65`, and
   `entry_epilogue("exception: irq client storm")`. **This is a *different* stop with different
   keys, and telling it from outcome 1 is the point of publishing `_pwr_irq_status_after` inside the
   handler rather than after it.**
3. **the client is never called** → `_irq_other_count = 1` with `_irq_other_iar = 0xaa`, exactly
   rung 7's log: the registration did not route the line.

**The inherited cells do not move.** Every rung-7 key reads rung 7's value (`_pwr_wrote = 0x0B`,
`_pwr_after = 0x0B`, `_pwr_cc_before = 0xE045`, the nineteen `_pwr_*`), and the store census changes
in exactly one window for one reason (§5): `hc_mem` gains the handler's `0x10C`.

## 3. The placement, and why the first rung to move is not a reordering

708 §3 states the ladder's rule — *"every rung appends to the probe so that the cells earlier rungs
already answered keep reading the same values"* — and **rung 8 is the first rung that breaks the
letter of it**, so the reason has to be in the record rather than in the diff:

* **The driver's order is registration-then-power, and it is not close.** `devm_request_threaded_irq`
  is at `:2937` in the probe; `mmc_power_up` runs from `mmc_rescan`, thousands of lines of call graph
  later. An arm that registered *after* the byte would send the byte into a line owned by nobody —
  which is rung 7's press, i.e. the rung would be unrunnable.
* **What the rule protects is the cells, and no cell moves.** The registration writes this image's
  own table (`g_irq_cli_*`) and no device register; the arming writes four GIC distributor words that
  **no earlier rung reads** (`xnu_live_irq_line_*` has no producer below rung 8 — §5 makes that a
  clause). Nothing rungs 1–7 read is touched by either, so their cells are the same readings they
  were, which is what the rule is for.
* **The byte still runs last among the device acts**, and `_pwr_irq_calls_once_before_byte = 0` is
  the cell that states it: at the moment the power byte is written, this rung's client has never been
  called. Without that cell, "the store raised the line" would be an inference from the log's order
  rather than a reading of it.

## 4. What rung 8 will **not** do

* **It does not mask the power events.** No write to `CORE_PWRCTL_MASK 0xE0`; the mask stays `0x0F`
  (the vendor's own `INT_MASK`, `:2946`). 709 §3's first option is *rejected and named here*: it
  would answer "is the rest of the sequence reachable with the line quiet" at the price of the
  driver's own handshake, and this rung is the one that makes that handshake completable.
* **It does not run the three arms that sleep** — `sdhci_msm_setup_vreg`, `sdhci_msm_setup_pins`,
  `sdhci_msm_set_vdd_io_vol` (§1.2). **The claim is structural and §5 checks it as a shape rather
  than a count**: `st_pwr_irq`'s body makes no call but the live-writer that publishes its readings.
  A build clause over the handler's own disassembly asserts exactly that, because a count of absent
  arms is a table of things not to count (m693).
* **It does not touch `CORE_PWRCTL_STATUS`' latch for any event it did not see**, and it does not
  write `CLEAR` with anything but the byte it read — the vendor's own line, and the one place where a
  cleverer value would be a different handler than the one this record claims.
* **It does not emulate the completion and does not call `check_power_status`** (§1.3), and it does
  not write `curr_pwr_state`/`curr_io_level` — those are fields of a struct this image does not have.
* **It writes no regulator, no rail, no level shifter** — and this is the rung's honest boundary: it
  makes the **controller's** power handshake complete, and the card's rail is still whatever the
  bootloader left. A controller told `BUS_SUCCESS` believes the bus is on; nothing in this rung moves
  a supply.
* **It registers 170 and only 170** (§1.4): `hc_irq` (intid 155) stays unowned, and a delivery on it
  is still a stop — which is the property that keeps a driver's mistake from being a storm.
* **No command, no sector, no partition table, no mount**, and no driver beyond the fixture.
  **TWRP-to-storage stays withheld**: the OS is not observed entering and *staying*.

## 5. The build clauses rung 8 needs

1. **`core_mem` is rung 6's set, unchanged** (`120 0 120 120 268 268`) — the handler writes no vendor
   mode word and no `core_mem` offset the record does not name. **This is also the clause that keeps
   §1.5 honest**: the *reading* of the two addresses adds no store, so the set that the clause
   asserts is the same set it asserted before, and a reader who expected the correction to move a
   store can see that it did not.
2. **`hc_mem` gains exactly one offset**: `47 44 44 41 268` — the reset's byte, the clock set's two
   halfwords, the power byte, and **the handler's `CORE_VENDOR_SPEC 0x10C` read-modify-write**. The
   width pairing (`strb strh strh strb str`) is asserted with the messengers, and the clause must
   refuse a *second* store at 268 and a store at 268 on any rung below 8 — the same shape rung 7's
   clause was rewritten into, because 268 is now the third offset this window has gained and the two
   before it each had to say which is which.
3. **the handler's own body is classified, in its own clause.** `entry_storage_probe`'s clause
   disassembles **that symbol only** (`--start-address`/`--stop-address` on `next_global`), so a store
   in `st_pwr_irq` is *outside its scope* — a scope claim about the extractor and not about the arm
   (m671/m672/m693). The new clause classifies `st_pwr_irq` the same way and asserts exactly: one
   `ldrb` at **220** (`CORE_PWRCTL_STATUS 0xDC`), one `ldrb` at **232** and one `strb` at **232**
   (`CORE_PWRCTL_CTL 0xE8`), one `strb` at **228** (`CORE_PWRCTL_CLEAR 0xE4`), one `ldr`/`str` pair
   at **268** in the `hc_mem` window, and **no other device access at all**. The `CLEAR` store is the
   one the whole rung turns on and it is asserted by offset, not by count.
4. **`bl entry_irq_enable_line` occurs exactly once in the linked image, and `bl
   entry_irq_register_client` exactly once**, both from `entry_storage_probe` — asserted from the
   disassembly. This is not bookkeeping: 500 wrote `xnu_live_irq_line_*` as *the* record of a line,
   and a second caller would make those keys a value with two producers. The clause is the reason
   the record can read them as one line's.
5. **the false comment is gone and the rebuild is a `cmp`.** `entry_storage.c:275-281`'s claim that
   0x10C is a `core_mem` offset is corrected in this step's commit; because the edit is a comment,
   the image must come back **byte-identical** ([[mi4-linker-fill-term]]), and the sha256 is recorded
   in the press's own document beside the parked arm's. A comment that changes the image would mean
   the comment was inside a `#if` that is not a comment.

**The proof, measured.** The corrected comment was rebuilt at the pressed arm's own switches
(`STAGE90_XNU_STORAGE_PROBE=7` with the sixteen entry switches `out/xnu_arm_entry-config.txt` records,
including `STAGE90_ENTRY_TRACE=1` and `STAGE90_XNU_ISTACK_SEPARATE=0` — a blind invocation is refused by
name, and two of the refusals this step took were the build being right: the first said
`STAGE90_XNU_STORAGE_PROBE=7` with `STAGE90_ENTRY_TRACE=0` "would shape nothing while the record said
it did" (692's clause, the m720 shape a second time), the second printed both switch lists and refused
because `ISTACK_SEPARATE` defaults to 1 where the arm has 0). The build then exited **0**, its census
line reads

```
xnu_entry_698: the probe's stores, classified by window and offset -
  core_mem [120 0 120 120 268 268 ] through [r4 ],
  hc_mem   [47 44 44 41 ](strb strh strh strb ) through [sl r7 r3 r6 ],
  gcc      [1224:str 1220:str 1256:str 1252:str ], …
```

and **all seven of the arm's files came back byte-for-byte identical** — `stage90.img` `9cacee09…`,
`stage90-qcdt.img` `f0ad4405…`, `xnu_arm_entry.bin` `7b1f3799…`, `xnu_arm_entry.elf` `ad9cac90…`,
`stage90.elf` `00c28a18…`, `stage90.bin` `71086f45…`, `stage90_fixture.macho` `52bc9c35…` — so the
comment changed no code, and `out/` still holds the pressed rung-7 arm with the same identity
readiness finds by hashing its `stage90-qcdt.img`.

## 6. Owed by rung 8, and what it does not answer
* **`CORE_VENDOR_SPEC`'s address is retired as a *decision* and re-opened as a *reading*** (§1.5).
  If the two addresses read differently, the rung after this one moves rung 6's two stores to
  `hc_mem + 0x10C` and takes the MCLK field's readback at the vendor's own address — a re-baseline of
  rung 6's cells, which is why it is not folded in here.
* **The completion itself** (§1.3): a driver-side `curr_pwr_state` is a kernel object. The first
  thing that can honestly take the wait is the kernel's own thread.
* **The RCG rate write** (the speed-change rung) with `_clk_rcg_update = 0` / `_clk_rcg_cmd = 0` as
  before-values and the bounded update poll.
* **The MCLK-select field's readback at the vendor's address** (§1.5's follow-up), and with it 706's
  question — write-through-to-nowhere, or a state the block needs — re-asked at the right register.
* **The card's rail**: nothing in this image powers a supply, and the byte 709 measured reading back
  is a request to the controller.
* Carried unchanged: what actually returns a run (8/17/24/27/24/27/25/28/**18** s); the ending's first
  store faulting (`0x0fa0065c`, rung 6's measurement, UNREAD in 709); the width clause of the store
  census never observed firing on a **widened device store**; the 691 §5 `entry_note_wfi` readback;
  `entry_reset.h`'s false IMEM claim; 676 §6 / 677 §6; the 684-owed runner clause;
  `tools/xnu_dt_requirements.py` and the `"master"` value; the two peer-lane tripwire repairs;
  BIT(29) of `_clk_ahb_cbcr` published and unnamed; the seam address pinned in two files.
* **Peer lane, by message and never by edit**: the gate's narration of `STAGE90_XNU_STORAGE_PROBE` is
  **seven rungs short** (2–7 are not described in it), and 710's message to `run-experiment-526`
  carries the rung list rather than asking the peer to read it out of the source.

The pre-registration rung 7 was, and the press that filled it:
`docs/experiments/experiment-708-the-rung-7-pre-registration-the-card-power-byte-and-the-wait-that-is-not-taken.md`
and `docs/experiments/experiment-709-the-card-power-byte-pressed-the-store-took-and-the-power-irq-that-ended-the-run.md`.

## 7. One thing this step measured about itself

**Two builds at once produce a false verdict about the artifact.** This step's first three rebuild
attempts were run as background jobs and two of them overlapped: the second of the pair exited 1 with

```
FAIL: ast_taken_user is not in the linked image - 511's clause needs the function the AST is
delivered from to read the call site out of
```

— a sentence about the *artifact*, produced while the other build was mid-link rewriting the same
object files. Run alone, the same switches exit 0 and that clause passes. So the message was a reading
of a *half-written* link, which is this project's recurring defect class arriving in its own build:
a check whose input another writer is rewriting reports a property of the race
([[mi4-measurement-defects]], and [[mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one]] one
level down). **The rule the gate already states for its firer — one gate, one runner — has a third
member: one build.** Nothing about the arm was harmed (the files are written at the end and the
hashes above are the proof), but a *failed* build whose failure is not about the arm is exactly the
kind of record a later reader would use to conclude something false.
