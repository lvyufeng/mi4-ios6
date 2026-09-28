# 798: the rung-32 press — the repair ran and the bound did not move, so `TIMEOUT_CONTROL 0x2E` is not what ends this ladder's commands; and CMD1 is sent once where the driver loops

**ONE PRESS SPENT, AND IT WAS AUTHORIZED BY THE OPERATOR BEFORE THE GATE WAS RUN.** One gate (exit 0),
one runner (EXIT 0), one `fastboot boot`, nothing flashed, nothing written to storage, no reboot.
`out/` moved: the arm in it was rung 32's and it is now **spent**. This step builds nothing and edits
no source.

| | |
| --- | --- |
| arm | `armed-storage-c1f89600`, `STAGE90_XNU_STORAGE_PROBE=31` (switch **value 31 = ordinal rung 32**) |
| boot image | `out/stage90/stage90-qcdt.img` sha256 `bda29ed1…` |
| fired | **2026-09-28 08:12–08:13 UTC**, EXIT 0 |
| gate | `scripts/preflight_boot_check.sh --allow-xnu-entry` — **exit 0, exactly one run, 608 lines** |
| runner | `scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-c1f89600` — **exactly one run** |
| capture | `out/stage90/captures/rung32-tout-20260928-081329-{last_kmsg.txt,gate.log,run.log}`, archived by hand, last_kmsg 633,759 B sha256 `0e33bbcd…` |
| `33e80afe` | absent from `adb devices` **and** `fastboot devices` at fire time |
| stayed up | `_post_end_calls = 0x7`, `_post_elapsed = 0x06df35b6` = **6004.8 ms**, `_post_cntfrq = 0x0124f800` = 19.2 MHz — the 6000 ms deadline **fired**, so the OS idled for the whole window |
| verdict | **7 PASS / 1 FAIL / 3 UNREAD counted per criterion** (14 PASS / 2 FAIL / 6 UNREAD *lines*, the block printed twice) — the single FAIL is the known `seam_sp … is not sleh_sp-8` criterion — **identical to rungs 29, 30 and 31, so this press added no failure** |

## 1. The repair ran, and the six cells say so

| key | value | reading |
| --- | --- | --- |
| `_tout_calls` | `0x1` | the open ran |
| `_tout_was` | **`0x0`** | **the register held 0** — the reset value, exactly as predicted; no rung and no earlier consumer had ever set it |
| `_tout_wrote` | `0x3` | the constant |
| `_tout_held` | **`0x3`** | **the register read back what was written** |
| `_tout_wrote_back` | `0x0` | the restore |
| `_tout_readback` | `0x0` | the readback after the restore |

**The window opened, the byte was raised from 0 to 3, the block held it, and it was put back.** The one
failure §4 of 796 said the rest of the log could not show — a store that never landed — **did not
happen**. The instrument worked.

## 2. The headline: the bound did not move by one tick-time

| | which bound CMD2 ran under | `_cid_ticks` | µs |
| --- | --- | --- | --- |
| rung 31 (`armed-storage-f9613649`) | `0x00` = **665.2 µs** | `0x31e2` = **12,770** | 665.2 |
| **rung 32** (`armed-storage-c1f89600`) | `0x03` = **5,321.6 µs** — **8×** | `0x31e4` = **12,772** | **665.3** |

**`TIMEOUT_CONTROL 0x2E` was raised eightfold, the block's own register provably held the new value
(§1), and the timeout fired at the same instant to within two ticks.**

`_cid_status_any = 0x00018000` (`ERR | TIMEOUT`) at `_cid_any_polls = 0x705` = 1,797 on both arms,
`_cid_err = 0x00010000`, `_cid_complete = 0`.

**So `TIMEOUT_CONTROL 0x2E` does not govern this event.** The SDHCI specification's timeout register
scales the **data** timeout; the command response timeout is fixed in the block. 795 §5 built the
arm on the assumption that the register sets the bound this ladder has been hitting, and **this press is
the measurement that says it does not**.

**This is the pre-registered refutation row and it arrived in the stronger form.** 796 §8 named
*"`ERR | TIMEOUT` again, at a LONGER tick count"* as the row that refutes 795's arithmetic. What came
back is `ERR | TIMEOUT` at the **same** tick count — not a longer one — so it refutes the **mechanism**
and not merely the arithmetic. **795 §5's extrapolation about where a 136-bit response would land is
untouched and untested**: the card is not answering at all, so no response ever raced the bound.

## 3. And it is a clean one-variable comparison, measured

Rungs 31 and 32 carry **481 and 487 storage keys**, and the six that exist only on rung 32 are §1's
`_tout_*`. Every other cell is identical to within timer jitter:

| key | rung 31 | rung 32 |
| --- | --- | --- |
| `_cid_ticks` | `0x31e2` | `0x31e4` |
| `_cid_status_any` | `0x00018000` | `0x00018000` |
| `_cid_word` / `_cid_flags` | `0x0201` / `0x03` | `0x0201` / `0x03` |
| `_cid_raw0..3` | `0x0040ff80 / 0x80000000 / 0 / 0` | **identical** |
| `_cid_resp0..3` | `0x40ff8080 / 0 / 0 / 0` | **identical** |
| `_cmd0_ticks` | `0x1327` = 4,903 | `0x1325` = 4,901 |
| `_cmd1_any_polls` | `0x4d4` = 1,236 | `0x4d6` = 1,238 (**CMD1 completed again**) |
| `_nidx_ticks` | `0x31e4` | `0x31e2` |

**So the press isolated exactly one variable — the timeout window — and the variable did nothing.**

## 4. A defect this step found in its own predecessor, and it is a guard that leaked

**796 §1 and §3 state that value 31 sends CMD2's `ST_MMC_RSP_R2` — word `0x0209` — and that "rung 31 sent
`0x0201` and this arm does NOT". That is false.** The capture reads `_cid_word = _cid_word_read =
0x00000201` and `_cid_flags = 0x00000003`: **rung 32 sent the CRC-dropped word.**

The source is unambiguous:

```
entry_storage.c:3328:  #if STAGE90_XNU_STORAGE_PROBE >= 30
entry_storage.c:3348:      ST_LIVE("xnu_live_storage_cid_flags", ST_MMC_RSP_R2_NOCRC);
entry_storage.c:3349:      st_send_command(ST_CMD_OP_ALL_SEND_CID, 0u, ST_MMC_RSP_R2_NOCRC, &c2);
entry_storage.c:3352:  #else
entry_storage.c:3353:      ST_LIVE("xnu_live_storage_cid_flags", ST_MMC_RSP_R2);
```

**The guard is `>= 30`, so every value at or above 30 inherits rung 31's constant.** Rung 32 is a
strict superset of rung 31's act — the NOCRC flag **and** the timeout window — and 796's prose described
it as though it were the timeout window alone.

**And the clause family did not catch it, because it was never asked to.** `xnu_entry_792` asserts the
FLAGS register is `#3` and is itself guarded `>= 30`, so at value 31 it asserts `#3` — which the source
also builds — and **passes**. The check was right about the image and the prose was wrong about the arm.
**This is the m-class *a claim in a document no check reads* again, and the tell is that the check and
the sentence disagreed and nothing compared them.**

**The reading of §2 is not weakened by this.** The leak made the comparison *better*, not worse: rungs 31
and 32 differ in the timeout window and nothing else, which is what §3 measures. **What is weakened is
796's answer space** — its row 4, *"`ERR | CRC` returning refutes 795 §2"*, could not have fired,
because the CRC flag was not carried.

**COST-owed and not repaired here**: `entry_storage.c:3328`'s guard should be `== 30` (or the record must
say the rung is a containment), and 796's §1/§3/§8 sentences are wrong. `entry_storage.c` is source and
the fix belongs to the next build.

## 5. CMD1 is sent ONCE, and the gate below it reads the driver's own condition backwards

This is the finding the press made available, and it is not about the timeout register at all.

**The driver loops.** `mmc_send_op_cond` (`mmc_ops.c:141-166`):

```c
	for (i = 100; i; i--) {
		err = mmc_wait_for_cmd(host, &cmd, 0);
		if (err) break;
		if (ocr == 0) break;                     /* a probe: one pass */
		if (mmc_host_is_spi(host)) { … }
		else {
			if (cmd.resp[0] & MMC_CARD_BUSY)     /* :157 — PROCEED when bit 31 SETS */
				break;
		}
		err = -ETIMEDOUT;
		mmc_delay(10);
	}
```

with the comment `/* otherwise wait until reset completes */`. **Up to 100 sends, 10 ms apart, exiting
when `resp[0] & 0x80000000` becomes TRUE — the card has finished power-up.**

**The ladder sends once and tests the opposite.** `entry_storage.c:4702`:

```c
    st_send_command(ST_CMD_OP_SEND_OP_COND, 0u, ST_MMC_RSP_PRESENT, &c1);
```

— one call, no loop — and `entry_storage.c:4785`:

```c
    if (c1.sent != 0u && (c1.resp & ST_MMC_CARD_BUSY) == 0u)   /* bit 31 CLEAR => send CMD2 */
```

**The driver proceeds when bit 31 is SET; the ladder proceeds when bit 31 is CLEAR.** The constant's own
comment at `:2197` cites `mmc_ops.c:158`'s condition, and the source comment above the gate says *"the
condition is the DRIVER's, read in the direction the driver reads it"*. **Both sentences are false
against `mmc_ops.c:157`**, and they are m-class claims in comments no check reads.

**And the measured values put the ladder on the wrong side of that test.** `_cmd1_resp = 0x40ff8080`;
`0x40ff8080 & 0x80000000 = 0`, so the busy bit is CLEAR, so the **driver's** loop would not have exited
— it would have retried 99 more times. The ladder proceeded to CMD2. `_cid_gated = 0` says it proceeded.

**So CMD2 is put on the bus for a card that has never reported power-up complete, and the card does not
answer it.** That is what the 665.2 µs is: the block's fixed response window expiring over a silent card
— not a bound that a register can move, which is exactly what §2 measured with the register.

**And it explains 795 §6 without needing the protocol claim at all.** CMD1 is answered because a card
answers CMD1 *while* it initializes — that is what the retry loop is for. CMD2 is not answered because
the card is not in READY. **795 §6's account (a card enters IDENT only by accepting CMD2) reached the
right shape from the wrong direction: the reason CMD3 also fails is one step earlier than it said, at
CMD1.**

**The one caveat, named rather than left**: `0x40ff8080` may not be a real OCR at all — 790 §5 and 787 §6
established it is byte-identical on arms whose window was closed and is not evidence the card answered.
**The next arm is sound either way**: if it is a real OCR, the loop is what the card needs; if it is not,
the loop is the measurement that says so.

## 6. The next arm, and it is the first time this ladder has ported the driver's own control flow

**Port `mmc_send_op_cond`'s loop**: send CMD1 up to N times with the driver's own 10 ms delay between
them, exit when `resp[0] & MMC_CARD_BUSY` **sets**, and publish two cells the ladder does not have —
**how many sends it took**, and **whether the bit ever set at all**.

Its answer space, and the first row is the one that would open the ladder:

| row | reading |
| --- | --- |
| the busy bit **sets** on some send `k` | **the card finishes power-up, and the ladder has been sending CMD2 too early since rung 16.** CMD2 then goes on the bus under the same window, and 793 §1's row 1a becomes live. |
| the bit **never sets** over the full count | the card is not powering up on this path at all — which puts the subject on **power and clock** (784's rails are RPM resources with no `reg`) and **not** on the command sequence. |
| the response word **changes between sends** | the block is reading something that moves with each CMD1, which separates a real OCR from a static pattern **without** knowing which it is. |
| the response is **byte-identical every send** | `0x40ff8080` is a static pattern and 790 §5's discount extends to every arm. |

**This is a driver port and not a register experiment**, and it is the first rung whose act is the
driver's own control flow rather than one of its constants — which is what 794 §4 and 797 §6 named as the
breadth gap.

## 7. What this does not do

- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made, so
  「让os可以正常启动并且挂载存储」 is not reached and **TWRP-to-storage stays withheld**.
- **It does not take the CID.** `_cid_complete = 0` and the eight `RESPONSE` words are the nine-arm
  pattern — 793 §1's row 1a did **not** fire.
- **It does not touch the PMIC, the rails, the clocks, the DLL or the pad register.** The arm's own
  safety surface was one byte in `TIMEOUT_CONTROL 0x2E`, and §1's six cells show it was applied and
  undone.
- **It does not decide §5's caveat.** Whether `0x40ff8080` is a real OCR is not decided here.
- **It does not repair §4's guard.** That is source, and the next build owes it.
- **No press is authorized by this step.** Rung 32 is spent and no arm is armed.

## 8. Owed, and named rather than left to be inferred

- **`entry_storage.c:3328`'s `>= 30` guard** (§4) — it should be `== 30`, or the record must state that
  value 31 is a strict containment of value 30. COST-owed to a build.
- **796 §1, §3 and §8's sentences about `0x0209`** (§4) — false; COST-owed to a build.
- **The CMD1 loop of §6** — the next arm, pre-registered here and not built.
- **`TIMEOUT_CONTROL`'s real scope** (§2) — the register does not move the command response timeout. What
  it does govern has not been measured on this ladder.
- **The effective bit rate, 171.5 kHz against rung 6's configured 400 kHz** (795 §10) — untouched, and now
  more pointed: if the card never answers, the rate this ladder inferred from CMD1's one response is the
  only measurement of it there is.
- **CMD9's `MMC_RSP_R2`** (797 §6) — the next command in the driver's order is the same 136-bit demand.
- **The six live-prose corrections of 797 §1** — the 1.200 s attribution, COST-owed to a build.
- Unchanged from 787–797: the `0x40ff8080` "the card ANSWERED" comment at CMD1's head (COST); a
  `_cmd1_raw_pre*` / `_cmd1_raw_post*` pair (COST); `c3.inhibit_timeout` for the `nidx` family (COST);
  the `*_status_post` class (791 §5); the four `5,088,000`s and the mis-citation at
  `entry_storage.c:302-303` (COST); a check that counts `ST_LIVE` sites per key (789 §4); the
  `_Static_assert` message's bit map at `entry_storage.c:2360` (789 §5); `_cid_ps_after`'s second
  producer (789 §2, COST); the set-comparison pad repair (779 §7); the `rung_para` correction for values
  12..23; the seam-address class; `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a
  synthetic FDT cell (782 §6); 784's `rail_name` cell; and 783's window-scope check.
