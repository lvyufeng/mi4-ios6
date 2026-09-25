# 711: the rung-8 power-IRQ client pressed — the latch let go, the boot reached the ending, and the two `CORE_VENDOR_SPEC` addresses read DIFFERENTLY

710 pre-registered the rung that gives the driver's own power interrupt an owner: the vendor's
`sdhci_msm_pwr_irq` (`sdhci-msm.c:1990-2099`) registered as a client of this image's dispatcher on
**intid 170** (SPI 138, the `pwr_irq` `msm8974.dtsi:502` declares for `sdhc_1`), its line armed
*before* the card-power byte, so that the latch 709's press left standing has something to answer it.
The arm was built at `STAGE90_XNU_STORAGE_PROBE=8`, parked as **`armed-storage-pwrirq-df4d38e5`**
(entry bin `df4d38e5…`), and pressed once.

**Every cell the pre-registration named reads what section 2 said it would, one non-return did not
happen, the ending fired for the first time since rung 6 — and the press answered a question the
pre-registration only asked: the two `CORE_VENDOR_SPEC 0x10C` addresses are two different registers,
so rung 6's MCLK stores went to an address that reads zero.**

## 1. The press

| | |
| --- | --- |
| readiness | **5/5, exit 0** (23:41:42), the live arm found by hashing `stage90-qcdt.img` (`454acfe1…`), 11 files vs the park, every one byte-identical |
| gate | **exit 0, 576 stdout lines** (23:42:11), under `--allow-xnu-entry` — one line more than 709's 575, which is the rung-8 paragraph the arm's own ladder gained |
| runner | **EXIT 0** (23:43:21) — returned and captured, `--expect-arm=armed-storage-pwrirq-df4d38e5` |
| the device | `reboot bootloader` at **23:42:42** (adb's own transport teardown) and the first adb re-enumeration at **23:43:16** — the device was back **≈30 s** after the boot, the eleventh return-time measurement and the longest of the returning arms |
| capture | 621,267 B, sha256 `49574a9547a93b1210b426be00a8ea313df7c356ad73ef245cc2705a00a5abdc`, archived by hand under `out/stage90/captures/` as `rung8-pwrirq-20260925-234233-{last_kmsg.txt,gate.log,run.log,press.log}` (672's rule: the runner does not archive it) |
| the firer | `/tmp/g668/press-on-clear.v5.sh` (sha `7f23cae5…`), `armed-storage-pwrirq-df4d38e5 600`, neighbour `33e80afe` absent from both lists (the watcher found `4a2fe00b` on the bus after 10 s and never saw the neighbour), `done. gate=0 runner=0` — **no firer is armed** |
| the runner's own verdict | 2 `FAIL`s, both the `STAGE90_XNU_SLOT_NULL=1` `seam_sp` fingerprint 697/704/705/706 also printed (`seam_sp=0x80553ec8 is not sleh_sp-8`); `seam_lr=0x800482dc` PASSes against this image, which is the reading that says the seam did not move |

## 2. The twenty-one `_pwr_irq_*` cells, and the four that matter

| cell | pre-registered | read |
| --- | --- | --- |
| `_pwr_irq_reg_rc` / `_reg_intid` / `_reg_handler` / `_reg_refcon` | accepted, `170`, the handler's own address, `0` | `0x1` / `0x000000aa` / **`0x8000cff8`** / `0x0` |
| `_pwr_irq_arm_rc` | the line's arming accepted | `0x1` |
| `_pwr_irq_calls_once_before_byte` | **`0`** — the cell that makes the byte the cause | **`0x0`** |
| `_pwr_irq_calls` / `_calls_probe_end` | the client is called, once | **`0x1`** / **`0x0`** |
| `_pwr_irq_intid` / `_refcon` | `170` / the registration's own refCon | `0x000000aa` / `0x0` |
| **`_pwr_irq_status` / `_status32`** | `0x02` / `0x00000002` from one moment | **`0x00000002` / `0x00000002`** |
| **`_pwr_irq_ack`** | `0x01` (`BUS_SUCCESS`) | **`0x00000001`** |
| `_pwr_irq_ctl_before` → `_ctl_after` | `0` → `0x01` | `0x00000000` → **`0x00000001`** |
| **`_pwr_irq_status_after`** | **`0x00` — the latch cleared** | **`0x00000000`** |
| `_pwr_irq_io_level` | `REQ_IO_HIGH` (act 6 runs) | `0x00000008` |
| `_pwr_irq_pad_ctl_before` → `_pad_ctl_after` | bit 16 already clear means the RMW wrote what it read | `0x00000a1c` → `0x00000a1c` |
| **`_pwr_irq_vendor_core` / `_vendor_hc`** | §1.5's two addresses, read in one run | **`0x00000000` / `0x00000a1c`** |

Four of those deserve their own sentence, and the fourth is the one this press buys beyond its own
rung.

### 2.1 The line has an owner, and the run no longer ends on it

709's log carries `xnu_live_irq_other_count=0x00000001` with `_irq_other_iar=0x000000aa` and ends
there: an interrupt on a line the image handed to nobody. **This log carries no `_irq_other_*` key at
all.** The registry's own record says why, and it is a third client: `xnu_live_irq_cli_intid` now reads
`0x00000000`, `0x00000028` and **`0x000000aa`**, with `xnu_live_irq_cli_handler` reading
`0x8000cff8` beside that last one — the handler the build put at `st_pwr_irq`'s address, and the same
value `_pwr_irq_reg_handler` publishes. The line's own record agrees: `xnu_live_irq_line_intid=0xaa`
with `_line_isaddr=0x114` (= `ISENABLER[5]`, i.e. bit 10 of word 5 = **intid 170**),
`_line_isen_before=0x00000400` **and** `_line_isen_after=0x00000400` — the pre-registration predicted
exactly this (`_isen_before` already set by the bootloader, `rc = 1`) and it is confirmed: the GIC had
the line enabled before this image touched it, and arming it was a `rc=1` no-op on the bit.

### 2.2 The acknowledge is what clears the latch — measured on the latch

`_pwr_irq_status = 0x02` (`CORE_PWRCTL_BUS_ON`) read as a byte and as a word from one moment, the same
value the probe read *before* the handler ran (`_pwr_status_after = 0x02`, with
`_pwr_irq_calls_probe_end = 0` proving the client had not been called yet at the probe's tail). The
handler then ORs `BUS_SUCCESS` into `CORE_PWRCTL_CTL` (`_ctl_before = 0` → `_ctl_after = 0x01`,
`_pwr_irq_ack = 0x01`) — **and the status reads back `0x00`.** 709 measured the latch set and nothing
clearing it; this press measures the store that clears it, and it is the store the vendor's own
pre-acknowledge writes (`:2872-2889`). The arm was called **once**, which is the whole event: the line
de-asserted, so the level did not re-assert and `STAGE90_IRQ_CLIENT_CAP` was never approached (no
`_irq_cli_storm`; `xnu_live_irq_cli_calls_total` reaches `0x7` across all three clients, the same
ladder the timer walks).

### 2.3 The ending fired — the first run since rung 6 to reach the 690 clock

| cell | rung 6 (707) | **rung 8 (this press)** | rung 7 (709) |
| --- | --- | --- | --- |
| `_post_end_calls` | `0x00000007` | **`0x00000007`** | absent |
| `_post_elapsed` | `0x06e0953c` = 6,006.2 ms | **`0x06ded39b` = 6,003.5 ms** | `0x00000000` |
| `_slot_post_calls` | `0x00000004` | **`0x00000004`** | `0x00000001` |
| `_irq_other_count` | absent | **absent** | `0x00000001` |

The elapsed trace is complete (`0x001daf68`, `0x010dac59`, `0x06ded39b`) and the last value is
**3.5 ms past the armed 6,000 ms deadline** (`_post_end_ticks = 0x06ddd000` = 115,200,000 ticks at
`_post_cntfrq = 0x0124f800` = 19,200,000 Hz), i.e. the ending fired on the first idle pass after the
clock ran out, exactly as 690 designed it. **709's arm died inside the first idle pass because one
interrupt line had no owner; this arm — the same operation, the same ending, the same clock — runs
through the power event and past the deadline.** That is the reading the rung was built for, and it
also retires the shape of 709's conclusion: the two halves the vendor's sequence separates
(`CORE_PWRCTL_MASK` armed with the status latched) *are* separable, but only if someone answers the
line.

### 2.4 `core_mem + 0x10C` and `hc_mem + 0x10C` are two different registers — and the ladder has been writing one that reads zero

`_pwr_irq_vendor_core = 0x00000000` and `_pwr_irq_vendor_hc = 0x00000a1c`, read 32-bit in one run
before the byte. **710 §1.5's question is answered in the direction the vendor's source said it
would be**: `CORE_VENDOR_SPEC 0x10C` is used 23 times in `sdhci-msm.c`, all 23 through
`host->ioaddr` (= `hc_mem`, `0xf9824900`) and zero times through `msm_host->core_mem`
(= `0xf9824000`), so `0xf9824a0c` is the vendor's register and `0xf982410c` is a hole that reads
zero. Two consequences, both now measured and neither of them inferred:

* **706's MCLK select and its `HC_SELECT_IN` clear were written to `0xf982410c`** — an address that
  reads `0x00000000` whether or not it was written. That is the mechanism behind 707 §2's *"the field
  did not take"*: not a register that refused the value, but an address with nothing behind it. It is
  not a falsification of 706's cells (they read what they read) and it is a correction of what they
  mean.
* **the vendor's address is not zero and is not idle: it reads `0x00000a1c`** — bit 16
  (`CORE_IO_PAD_PWR_SWITCH`) clear, and every other bit the word carries already set. The handler's
  act 6 read-modify-write therefore wrote the word it read (`_pwr_irq_pad_ctl_before` =
  `_pwr_irq_pad_ctl_after` = `0xa1c`), which the pre-registration named in advance as a readable
  outcome and not a failure.

**So the next rung's first question is now a measurement rather than a re-baseline decision**: rung 6's
two stores moved to `hc_mem + 0x10C`, with the same readback cell, is a change with a predicted
direction — the readback should HOLD the value written, the way the power byte does, rather than read
`0` the way the hole did.

## 3. The one cell the pre-registration did not predict: `_pwr_cc_after` moved

`_pwr_cc_before`/`_pwr_cc_after` are `SDHCI_CLOCK_CONTROL 0x2C` read as a 16-bit word through `hc_mem`
either side of the power byte. 709 read **`0xE045` on both sides**, and its record says so in the same
words ("`_pwr_cc_after` is the same word, so powering did not disturb the clock register — a coupling
this ladder had not seen, and did not see"). This press reads **`0xE045` → `0xE047`**: bit 1, the
**SD clock enable**, came on between the store and the readback.

**The probe's code between the store and the read is byte-identical between the two arms** — the rung-8
change is a registration and two published readings *before* the byte and one publication at the
probe's tail — so the difference is not in the arm. Two candidate readings, and this press does not
choose between them:

1. **the controller's own power-up sequence is asynchronous to the driver's read**, and whether bit 1
   is visible at the readback is a race the driver happens to win on some boots. The controller asked
   for the bus (`_pwr_status_after = 0x02`) at the same moment, and the vendor's
   `sdhci_msm_check_power_status` polls for exactly this — which is why the wait exists and why this
   ladder does not take it;
2. **the machine's state differs between the two boots** in some way this log does not carry — the
   probe runs on the idle exit's first return, and nothing in either log says what the controller had
   been doing before it.

What can be said is the shape: **the same store, on the same code path, produced two different
`CLOCK_CONTROL` readbacks on two presses.** A driver that reads this register once and believes it is
a driver with a race; the cell to read it against next is a poll, and the number to record is that the
bit arrived *within the probe* on this boot and did not on 709's.

## 4. The inherited cells did not move

Every rung-7 key reads 709's value: `_pwr_wrote=0x0B`, `_pwr_before=0x00` → `_pwr_after=0x0B`,
`_pwr_cap=0x742dc8b2`, `_pwr_vdd=0x7`, `_pwr_voltage_bits=0x0A`, `_pwr_refused=0`,
`_pwr_status_before=0x00` → `_pwr_status_after=0x02`, `_pwr_mask=0x0f`, `_pwr_ctl=0`,
`_pwr_ps_before=_pwr_ps_after=0x01f80000`, `_pwr_cc_before=0xE045`, `_pwr_calls=_pwr_done=0x1` — and
so does every clock key (`_clk_set_*`, the twelve-word clock census with
`_clk_ahb_cbcr=0x2000cff1`), every standard-register census key, and `_mode_*`. The 690 clock, the
`STAGE90_XNU_SLOT_NULL=1` fingerprint, the seam pair and the two `FAIL`s beside it are the same
readings 704/705/706/709 carried.

**The goal is still NOT met**: no command, no sector, no partition table, no mount, no driver beyond
the fixture — what this press buys is that the driver's own power path now *completes* on this device,
which is the first rung at which that is true. **TWRP-to-storage stays withheld.**

## 5. What the build had already measured, carried here

The arm's own build falsified experiment-710's `hc_mem ... 268` prediction (the probe's census stays
`[47 44 44 41 ]` because the handler is its own symbol), the clause now prints its reading, the seam
did not move, and the arm differs from the pressed rung-7 park in the embedded entry blob plus the
boot header's 20-byte `id` and nothing else. All of it is in `stages/stage90/revert-set.txt` and in the
`f4fbe63` commit; the document and the README row were corrected in the same step.

## 6. Owed after this press

* **the completion itself** — a driver-side `curr_pwr_state` is a kernel object, and the vendor's wait
  is still not taken; the line now has an owner, so a bounded poll of `CORE_PWRCTL_STATUS` after the
  byte is a *reading* this image can take, which it could not before this press.
* **rung 6's two stores at `hc_mem + 0x10C`**, with the readback cell, now that the address question is
  measured (§2.4).
* **the `CLOCK_CONTROL` bit 1 race** (§3): one poll, or one more press, before any driver concludes
  the card clock is running.
* **the RCG rate write** and **the card's rail** (nothing in this image powers a supply).
* Carried unchanged: the width clause of the store census never fired on a WIDENED DEVICE store; the
  ending's first store faulting (`0x0fa0065c`, still unread); the 691 §5 `entry_note_wfi` readback;
  `entry_reset.h`'s false IMEM claim; 676 §6 / 677 §6; the 684-owed runner clause;
  `tools/xnu_dt_requirements.py` and the `"master"` value; the two peer-lane tripwire repairs; BIT(29)
  of `_clk_ahb_cbcr` published and unnamed; the seam address pinned in two files; and the gate's
  narration of `STAGE90_XNU_STORAGE_PROBE`, now **seven rungs short (2 through 8)** — peer lane, by
  message and never by edit.
