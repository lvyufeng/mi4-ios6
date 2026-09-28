# 799: the CMD1 this ladder never sends — the driver calls it twice, and the two steps between the calls do not exist in the image

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER, NOTHING
BUILT.** `out/` was **not** touched: the spent rung-32 arm and every park are byte-identical before and
after this step. One press spent: **none**. **No press is authorized — rung 32 is spent and no arm is
armed.**

Everything below is read out of `external/android_kernel_xiaomi_cancro/drivers/mmc/core/{mmc.c,mmc_ops.c}`
or out of `src/entry/entry_storage.c` as it stands.

## 1. The driver calls CMD1 **twice**, and the two calls are not the same call

| | | |
| --- | --- | --- |
| **1** | `mmc.c:1923` | `err = mmc_send_op_cond(host, 0, &ocr);` — in `mmc_attach_mmc`, before `mmc_select_voltage`. **Argument 0.** |
| **2** | `mmc.c:1359` | `err = mmc_send_op_cond(host, ocr \| (1 << 30), &rocr);` — **argument with bit 30 set and the voltage window the first call returned.** |

**And the argument decides whether the loop runs at all.** `mmc_ops.c:148-150`:

```c
		/* if we're just probing, do a single pass */
		if (ocr == 0)
			break;
```

**So call 1 — argument 0 — is a SINGLE PASS by the driver's own design**, and call 2 is the one that
loops up to 100 times with `mmc_delay(10)` between passes, exiting when `resp[0] & MMC_CARD_BUSY` sets.

## 2. Between them the driver does something the image has no name for

`mmc.c:1951`: `host->ocr = mmc_select_voltage(host, ocr);` — the OCR windows the card claimed are
intersected with the host's available windows, and the result is **the argument of call 2**. The driver
also rejects a card whose OCR claims voltages below the defined range (`mmc.c:1943-1948`, `ocr &= ~0x7F`)
and errors out when no voltage is the intersection (`mmc.c:1956-1959`, `-EINVAL`).

**The image has neither step, and the check is a grep rather than an impression.** Anchored and
case-sensitive over `src/entry/`, `select_voltage` occurs **once and in a COMMENT** — the note at
`entry_storage.c:4724` explaining what it publishes — with **no define, no body and no call**. The
ladder's CMD1 argument is the literal `0u` (`entry_storage.c:4702`), and `1u << 30` occurs once in the
whole file as `ST_HC_DLL_RST` (`:258`, a different register), so **bit 30 is never set in any command word
this image sends**.

## 3. What the ladder actually does, stated against the driver's own order

| driver step | image |
| --- | --- |
| CMD1 argument 0, single pass | **yes** — `st_send_command(ST_CMD_OP_SEND_OP_COND, 0u, ST_MMC_RSP_PRESENT, &c1)`, `entry_storage.c:4702`, sent once |
| `mmc_select_voltage` on that response | **absent** — no define, no body, no call |
| CMD1 argument `ocr \| (1<<30)`, **looping until bit 31 sets** | **absent** — the image sends CMD1 once and never again |
| gate CMD2 on the result | **the image gates CMD2 on the FIRST call's response** (`:4785`), which the driver never does |

**So the ladder implements the driver's probe and then treats the probe's answer as the final one.** By
the driver's own code the probe's answer is an *input* to a later, looped call — it is not a gate.

**And the gate's direction is inverted against the very condition it cites** — 798 §5 measured it:
`mmc_ops.c:157` proceeds when bit 31 **sets**; `entry_storage.c:4785` proceeds when bit 31 is **clear**.

## 3a. And the ladder already publishes the driver's own test — it has simply never passed it

`entry_storage.c:4728-4729` publishes the response's two faces raw-adjacent, in the driver's own terms:

```c
    ST_LIVE("xnu_live_storage_cmd1_resp_busy", (c1.resp >> 31) & 1u);
    ST_LIVE("xnu_live_storage_cmd1_resp_voltage", c1.resp & 0x00FF8000u);
```

**`_cmd1_resp_busy` IS `mmc_ops.c:157`'s test, computed and logged since rung 16 — and over the WHOLE
ARCHIVE it has never read 1:**

| | |
| --- | --- |
| occurrences of `xnu_live_storage_cmd1_resp_busy` across every capture in `out/stage90/captures/` | **12** |
| distinct values | **`0x00000000`, all twelve** |

**So the card has never reported power-up complete on any arm this ladder has ever run** — and
`_cmd1_resp_voltage = 0x00ff8000` (OCR bits 23:15 all-ones) is on every one of those arms too, which is a
legal full voltage window and equally what a static pattern looks like (790 §5).

**That sharpens §4 two ways.** First, the arm needs **one** new cell and not two: **how many sends it
took**. `_cmd1_resp_busy` already exists, and on the row where the loop succeeds it becomes **`1` for the
first time in the archive** — a single bit flipping after twelve runs. Second, **"the bit never set" has
so far been read off ONE send per boot**, so the archive does not yet say the card cannot become ready; it
says only that one CMD1, sent with argument 0, never saw it. **The loop is the first arm that asks the
question more than once.**

## 4. The next arm, now specified rather than sketched

**Port the driver's two missing steps: derive an OCR window from the first response, then send CMD1 with
`ocr | (1 << 30)` in a loop, exiting when bit 31 sets.** Two new cells, the same two 798 §6 named:

- **how many sends it took** (1..N) — the only cell the ladder does not already carry — and
- **whether bit 31 ever set**, which §3a reads out of `_cmd1_resp_busy`, a key already published.

Its answer space is unchanged from 798 §6 and is now tighter, because the argument is the driver's:

| row | reading |
| --- | --- |
| bit 31 sets on some send `k ≤ N` | **the card finishes power-up and the ladder has been sending CMD2 since rung 16 without ever asking the card whether it was ready.** CMD2 then goes on the bus, and 793 §1's row 1a becomes live. |
| bit 31 never sets over N sends | the card does not complete power-up on this path — the subject is **power and clock**, not the command sequence (784's rails are RPM resources with no `reg`). |
| the response **changes** between sends | the response registers are reading something that moves with each CMD1, which separates a real OCR from a static pattern **without** needing to know which it is. |
| the response is **byte-identical** every send | `0x40ff8080` is a static pattern, and 790 §5's discount extends to every arm. |

**The safety surface is unchanged and small**: the same command, the same opcode, the same register class,
the same `st_send_command` with its two existing per-command guards; what is new is the **delay** between
sends (the driver's own `mmc_delay(10)`) and the count. No new device, no new address, no new width.

## 5. One observation this step made and deliberately does not conclude

Rung 32's capture carries **both** of CMD3's raw pairs, and they are not equal:

| word | `_nidx_raw_pre*` (before CMD3) | `_nidx_raw_post*` (after CMD3) |
| --- | --- | --- |
| 0 | `0x0040ff80` | `0x00000000` |
| 1 | `0x80000000` | `0x00000000` |
| 2 | `0x00000000` | `0x00000000` |
| 3 | `0x00000000` | **`0x40ff8080`** |

`_nidx_resp_moved = 1` and `_nidx_resp_is_arg = 0` — the word is not CMD3's argument (`0x00010000`).
**The register bank's content moved between two reads taken around a command that timed out.** What it
moved *to* is a value whose relationship to CMD1's `0x40ff8080` is not established here, and **this step
does not claim one**: the two pre-words are `0x0040ff80` and `0x80000000`, whose concatenation is the same
eight bytes CMD1's response word sits beside, and a shift, a rotation and a coincidence are all
admissible. **Named as an observation, not a reading** — it needs a cell the ladder does not have (a
pre/post pair around CMD1's own `RESPONSE` registers, owed since 787 §6).

## 6. What this does not do

- **It spends no press, authorizes none, and moves no byte of `out/` or of any park.**
- **It builds nothing.** The arm of §4 is pre-registered here and **not built**.
- **It does not decide §5.**
- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made, so
  「让os可以正常启动并且挂载存储」 is not reached and **TWRP-to-storage stays withheld**.

## 7. Owed, and named rather than left to be inferred

- **The arm of §4** — derive the OCR, then send CMD1 looped with `ocr | (1 << 30)`. The next build.
- **`mmc_select_voltage`'s window** (§2) — the image has no representation of the host's available OCR
  windows at all, so §4's arm must decide whether to port the intersection or to pass the card's own OCR
  through. **That decision is the arm's own design question and is named here rather than hidden.**
- **A `_cmd1_raw_pre*` / `_cmd1_raw_post*` pair** around CMD1's own `RESPONSE` registers (§5) — owed
  since 787 §6 and now the cell that would make §5's observation a reading.
- **`entry_storage.c:3328`'s `>= 30` guard** (798 §4) — COST-owed to a build.
- **796 §1/§3/§8's sentences about `0x0209`** (798 §4) — false; COST-owed to a build.
- **The six live-prose corrections of 797 §1** — the 1.200 s attribution, COST-owed to a build.
- **`TIMEOUT_CONTROL`'s real scope** (798 §2) — unmeasured on this ladder.
- **The effective bit rate, 171.5 kHz against rung 6's configured 400 kHz** (795 §10) — untouched.
- Unchanged from 787–798: the `0x40ff8080` "the card ANSWERED" comment at CMD1's head (COST);
  `c3.inhibit_timeout` for the `nidx` family (COST); the `*_status_post` class (791 §5); the four
  `5,088,000`s and the mis-citation at `entry_storage.c:302-303` (COST); a check that counts `ST_LIVE`
  sites per key (789 §4); the `_Static_assert` message's bit map at `entry_storage.c:2360` (789 §5);
  `_cid_ps_after`'s second producer (789 §2, COST); the set-comparison pad repair (779 §7); the
  `rung_para` correction for values 12..23; the seam-address class; `run_and_capture.sh`'s
  `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic FDT cell (782 §6); 784's `rail_name` cell;
  and 783's window-scope check.
