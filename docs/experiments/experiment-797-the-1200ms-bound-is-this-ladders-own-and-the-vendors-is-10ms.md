# 797: the rung-32 arithmetic is right and its citation is wrong — the 1.200 s the value is measured against is this ladder's own bound, not the driver's, and the driver's own bound is 10 ms

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER, NOTHING
BUILT.** `out/` was **not** touched: the armed rung-32 arm `armed-storage-c1f89600` and every park are
byte-identical before and after this step. One press spent: **none**. **No press is authorized by this
step — rung 32 is armed and unspent and the press is the operator's.**

Every number below is read either out of the vendor tree under
`external/android_kernel_xiaomi_cancro/` or out of `src/entry/entry_storage.c` as it stands. Nothing is
inferred.

## 1. The sentence under audit, and where it stands

The rung-32 arm's justification carries, in six places, the claim that `0x03` is the right size because
it stays under **"the driver's own 1.200 s poll bound"**, and one of those places spells the derivation
out as **"`sdhci.c`'s `timeout = 10` in 100 ms units"**.

| where | text |
| --- | --- |
| `src/entry/entry_storage.c:2323` | "`ST_CMD_DONE_TICK_BUDGET` = 23,040,000 ticks = **1.200 s**, so `0x03` is **225 times under** the …" |
| `src/entry/build_entry.sh:31071` | "stays under **the driver's own 1.200 s poll bound**" |
| `src/entry/build_entry.sh:31117` | the refusal message: "**at or above `0x0B` the bound passes the driver's own 1.200 s poll bound**" |
| `tools/verify_press_ready.sh:1120` | "225x UNDER **the driver's own 1.200 s poll bound**" |
| `tools/verify_press_ready.sh:1232` | the rung-32 paragraph, same phrase |
| `records/revert-set.txt:8045` | "**the driver's own 1.200 s poll bound** (`sdhci.c`'s `timeout = 10` in 100 ms units)" |
| `docs/experiments/experiment-796-…md:38` | the table row, with the same parenthetical |

## 2. There are three bounds in that neighbourhood and they are 10 ms, 100 ms and 10 s

| vendor site | what the source says | what it is |
| --- | --- | --- |
| `sdhci.c:1074` | `#define SDHCI_REQUEST_TIMEOUT 10` — *Default request timeout in seconds* | **10 s**, and it is used to arm a **timer** (`mod_timer(&host->timer, …)` at `:1109`), not a poll |
| `sdhci.c:1084-1085` | `/* Wait max 10 ms */` over `timeout = 10;`, with `timeout--; mdelay(1);` in the loop (`:1106`) | **10 ms** — the `SDHCI_CMD_INHIBIT` wait |
| `sdhci.c:251-252` | `/* Wait max 100 ms */` over `timeout = 100;`, with `mdelay(1)` at `:267` | **100 ms** — the reset poll |

**None of them is 1.200 s.** The parenthetical — *"`timeout = 10` in 100 ms units"* — is a **120×
overstatement** of the loop it names: that loop is 10 iterations of **1 ms**, and the line above it says
so.

## 3. The 1.200 s is this image's own, and the image already says so correctly in the same file

`src/entry/entry_storage.c:2236`:

```c
#define ST_CMD_DONE_TICK_BUDGET    23040000u    /* 1,200 ms at 19,200,000 Hz - just above the SDHCI
                                                 * specification's fixed ~1 s command timeout, so
                                                 * that the block's OWN timeout is the event this
                                                 * bound can see. If it is not, `_cmdN_timeout`
                                                 * firing with no error bit is a reading about the
                                                 * controller's timeout being longer than 1.2 s. */
```

**It is this ladder's poll bound**, deliberately placed just above the SDHCI specification's fixed ~1 s
command timeout so that the *block's* timeout is the event the bound can see. It is not the vendor's,
it was never the vendor's, and it belongs to the same file that carries the wrong sentence 87 lines
above it.

**And the file is already right about the vendor's real bound, three times** —
`entry_storage.c:2232` (`ST_CMD_INHIBIT_TICK_BUDGET 192000u`, *"the driver's `timeout = 10` ms
(sdhci.c:1084), 10 ms x 19,200 ticks/ms"*), `:2522`, `:2619`, `:2694`, `:4825`, and
`records/revert-set.txt:7476`. **So the tree contradicts itself**, and the wrong half is the newer one.

## 4. The arithmetic is not touched, and that is the point of separating the two

Everything the arm claims about the *number* survives unchanged:

| | |
| --- | --- |
| `0x03` = 8 × the 665.2 µs reset bound | **5.3216 ms** |
| against 1.200 s | **225.5× under** — the "225×" in the record is right |
| the first count that exceeds 1.200 s | `0x0B` = 2¹¹ × 665.2 µs = **1.3623 s** — the record's boundary claim is right |
| the last count under it | `0x0A` = 2¹⁰ × 665.2 µs = **681.2 ms** |
| 795 §5's extrapolated 136-bit arrival, 1,048.2 µs | `0x03` clears it by **5.08×** |

**So this is an attribution defect and not a measurement defect**: the bound the value is measured
against is real, is 1.200 s, and is in the image — it is simply **the image's own** and is described as
the vendor's, with a citation that is wrong by two orders of magnitude.

## 5. What the census turned up on the way, and it is the more useful finding

`sdhci_prepare_data` (`sdhci.c:827-828`) writes `TIMEOUT_CONTROL` with the value
`sdhci_calc_timeout` returns, and that function (`sdhci.c:740`) has this as its **second** statement:

```c
	/* Unspecified timeout, assume max */
	if (!data && !cmd->cmd_timeout_ms)
		return 0xE;
```

**For a data-less command with no `cmd_timeout_ms` — which is every command this ladder sends — the
vendor's own function would write `0x0E`, the register's maximum.** At `0x0E` the field is 2¹⁴ × the
reset bound = **10.899 s**, which is **9.08× past this ladder's own 1.200 s poll bound**.

**So the vendor's "assume max" is not a safe maximum here — it is a value that would put the device
timeout behind the ladder's own give-up and make it unobservable.** The arm's `0x03` is a value chosen
to sit between the reset bound and the poll bound, and the vendor has no such value in this code path:
its alternatives are `0x0E` (too long to see) or nothing at all (which is what the register holds now).

**That is the arm's whole justification restated correctly**: not *"under the driver's bound"* but
*"the only value available to this ladder is 0x00, the vendor's branch would write 0x0E which this
ladder's poll cannot see, and 0x03 is the choice between them."*

## 6. The data-path registers, and what the breadth gap actually costs

794 measured the image's command vocabulary stops at CMD3 and there is no data path. This step read the
vendor's data path to say what "no data path" means in registers:

| register | offset | in the image? |
| --- | --- | --- |
| `SDHCI_DMA_ADDRESS` / `SDHCI_ARGUMENT2` | `0x00` | **yes**, `ST_SDHCI_DMA_ADDRESS` — read twice as a version word, never as a transfer address |
| `SDHCI_BLOCK_SIZE` | `0x04` | **no** |
| `SDHCI_BLOCK_COUNT` | `0x06` | **no** |
| `SDHCI_ARGUMENT` | `0x08` | yes |
| `SDHCI_TRANSFER_MODE` | `0x0C` | **no** |
| `SDHCI_COMMAND` | `0x0E` | yes |
| `SDHCI_HOST_CONTROL` | `0x28` | yes (the DMA-select byte `sdhci_prepare_data` rewrites, `:951-960`) |
| `SDHCI_TIMEOUT_CONTROL` | `0x2E` | yes — and written by no rung until 32 |
| `SDHCI_ADMA_ADDRESS` | `0x58` | **no** |

**Three registers the vendor's data path writes unconditionally — `BLOCK_SIZE 0x04`, `BLOCK_COUNT
0x06`, `TRANSFER_MODE 0x0C` — do not exist in the image under any name, anchored and case-sensitive.**
`sdhci_set_transfer_mode` and the two `sdhci_writew` calls at the end of `sdhci_prepare_data`
(`:966-967`) are the act that has never happened here.

**And the next command after CMD3 is the same 136-bit demand the frontier is about.** `mmc.c:1425`
calls `mmc_send_csd`, which reaches `mmc_send_cxd_native` (`mmc_ops.c:214`) with

```c
	cmd.opcode = opcode;          /* MMC_SEND_CSD = 9 */
	cmd.arg = arg;                /* card->rca << 16 */
	cmd.flags = MMC_RSP_R2 | MMC_CMD_AC;
```

**CMD9 carries `MMC_RSP_R2` — the identical 136-bit response request as CMD2.** The image already
defines `ST_MMC_RSP_R2` (`entry_storage.c:2170`). So if rung 32's longer bound is what CMD2 needed,
**CMD9 rides the same mechanism**; and if it is not, CMD9 is blocked by the same thing. **CMD9 is not a
second frontier — it is the first one, one command further along.** CMD7 (`_mmc_select_card`,
`mmc_ops.c:23`) is `MMC_RSP_R1 | MMC_CMD_AC` with `arg = rca << 16`; the image has both constants.

## 7. What this changes, and what it deliberately does not

- **It corrects an attribution in seven places, of which six are live prose.** The seventh is
  `docs/experiments/experiment-796-…md` — written by the immediately preceding step and not yet a
  record of a press — and it is **corrected in place**, because it is this step's own subject.
- **The other six are COST-owed to a build and are NOT edited here**: `entry_storage.c:2323` and the
  `#error` message's tail, `build_entry.sh:31071` and `:31117`, and the two `verify_press_ready.sh`
  paragraphs. `entry_storage.c` and `build_entry.sh` are source, and **an armed arm is in `out/`** —
  editing them without rebuilding would leave the record and the image disagreeing, and rebuilding
  would spend the armed arm. **This is the same shape as the `0x40ff8080` comment at CMD1's head**,
  owed since 787 and carried forward through four steps for the same reason.
- **It does not change the arm.** `armed-storage-c1f89600` is arithmetically unaffected: §4 shows the
  number and the boundary are both right. Nothing in `out/` moved.
- **It does not decide anything about the press.** Rung 32 is armed and unspent.
- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made,
  so 「让os可以正常启动并且挂载存储」 is not reached and **TWRP-to-storage stays withheld**.

## 8. Owed, and named rather than left to be inferred

- **The six live-prose corrections of §1** — COST-owed to a build, and they must be carried by the step
  that next rebuilds, exactly as the `0x40ff8080` comment is.
- **`sdhci_calc_timeout`'s `0x0E` branch** (§5) — a fact about the vendor's data path that no rung has
  needed yet, and one that will matter the first time a rung reaches `sdhci_prepare_data` for a
  data-less `MMC_RSP_BUSY` command.
- **The three data-path registers of §6** — the concrete register cost of 794's breadth gap, named so
  that the step which crosses it is not also discovering them.
- **CMD9's `MMC_RSP_R2`** (§6) — the observation that the next command in the driver's order is the
  same 136-bit demand, which makes the rung-32 press's consequence wider than the CID alone.
- Unchanged from 787–796: the `0x40ff8080` "the card ANSWERED" comment at CMD1's head (COST); a
  `_cmd1_raw_pre*` / `_cmd1_raw_post*` pair (COST); `c3.inhibit_timeout` for the `nidx` family (COST);
  the `*_status_post` class (791 §5); the four `5,088,000`s and the mis-citation at
  `entry_storage.c:302-303` (COST); a check that counts `ST_LIVE` sites per key (789 §4); the
  `_Static_assert` message's bit map at `entry_storage.c:2360` (789 §5); `_cid_ps_after`'s second
  producer (789 §2, COST); the set-comparison pad repair (779 §7); the `rung_para` correction for
  values 12..23; the seam-address class; `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s
  lack of a synthetic FDT cell (782 §6); 784's `rail_name` cell; and 783's window-scope check.
