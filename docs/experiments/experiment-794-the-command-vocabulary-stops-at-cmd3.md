# 794: the row that said "the driver order then opens" — the image's whole command vocabulary is CMD0…CMD3 and there is no data path, so a perfect CID moves the ladder no closer to a mount

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER, NOTHING
BUILT.** `out/` was **not** touched: the armed rung-31 arm `armed-storage-f9613649` is byte-identical
before and after, its eleven-member park is intact, and no source under `src/` was edited. One press
spent: **none**. **No press is authorized.**

Every source claim below is the output of a `grep` against the working tree, quoted rather than
summarised, and each one is stated with the anchoring and case it was taken under (§7).

## 1. The claim, and it was written four times in two steps

792 §7 pre-registered rung 31's best row as:

> **`_cid_complete = 1`** | **THE CID IS TAKEN.** The driver order opens: CMD9 (CSD), CMD7 (select),
> CMD16 (block length), then data

793 §5 rewrote the row into two and kept the consequence verbatim — *"This is 792 §7's row 1 as it
should have been written, and it is the row that moves the ladder: the driver order then opens — CMD9
(CSD), CMD7 (select), CMD16 (block length), then data."* — and `tools/verify_press_ready.sh`'s
`entry_conseq` (line 1104) and `rung_para 30` (line 1216) carry it too, so it is **the text the next
press prints**.

**That sentence is true of the Linux MMC driver. It is false of this image, and this step measures
which.** It is the same defect as the one 793 corrected — a claim about *the ladder* written in the
vendor's terms — one level further out: 793's was about what a cell means, this one is about what the
image *contains*.

## 2. The image's whole command vocabulary is four opcodes

```
$ grep -cE '^#define ST_CMD_OP_' src/entry/entry_storage.c
4
$ grep -nE '^#define ST_CMD_OP_' src/entry/entry_storage.c
2198:#define ST_CMD_OP_ALL_SEND_CID     2u
2199:#define ST_CMD_OP_GO_IDLE_STATE    0u
2200:#define ST_CMD_OP_SEND_OP_COND     1u
2202:#define ST_CMD_OP_SET_RELATIVE_ADDR 3u
```

**Four defines, opcodes 0, 1, 2 and 3 — CMD0, CMD1, CMD2, CMD3.** There is no `ST_CMD_OP_SEND_CSD` (9),
no `ST_CMD_OP_SELECT_CARD` (7), no `ST_CMD_OP_SET_BLOCKLEN` (16), and no define for any other opcode.
`grep -rn 'ST_CMD_OP_' src/` over the whole payload returns definitions, six `_Static_assert`s and the
four send sites — every send in the image is one of those four.

**And CMD9 is not defined at all**, so a body that wanted to read the card's CSD would have to add the
constant before it could be written.

## 3. After CMD3 the storage path ENDS — the next call is the power-IRQ consumer, then return

`entry_storage_probe`'s ladder (`src/entry/entry_storage.c:5144` and following):

```c
        st_cmd_path();
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 8
    ...
    if (g_storage_mode_complete != 0u)
        st_pwr_irq_after();
#endif
}
```

`st_cmd_path()` is called **exactly once** (`grep -c 'st_cmd_path();'` = 1) and the only thing after it
is `st_pwr_irq_after()` — rung 8's power-IRQ consumer — and then the function returns. Inside
`st_cmd_path`, the sequence is:

| order | line | call | the command |
| --- | --- | --- | --- |
| 0 | `:4506` | `st_send_command(ST_CMD_OP_GO_IDLE_STATE, 0u, 0u, &c0)` | **CMD0** |
| 1 | `:4603` | `st_send_command(ST_CMD_OP_SEND_OP_COND, 0u, ST_MMC_RSP_PRESENT, &c1)` | **CMD1** |
| 2 | `:4678`/`:4680` | `st_all_send_cid(int_enable)` | **CMD2** |
| 3 | `:4742`/`:4775` | `st_cmd3_noresp` / `st_cmd3_noidx` | **CMD3** |

**Four commands, and the fourth is the last.** There is no fifth body and no call site for one.

## 4. And there is no data path at all — measured, not read off the prose

The three registers a transfer needs are **`BLOCK_SIZE` (`0x04`), `BLOCK_COUNT` (`0x06`) and
`TRANSFER_MODE` (`0x0C`)**, plus a DMA address register for any DMA mode. Anchored, case-sensitive, over
all of `src/`:

```
$ grep -rnE '\b(BLOCK_SIZE|BLOCK_COUNT|TRANSFER_MODE|ADMA|SDHCI_DATA|SDHCI_BLOCK|DMA_ADDRESS)\b' \
      src/ --include=*.c --include=*.h
src/entry/entry_storage.c:2038: *   RETURNS on its first line for a data-less command (`:985-986`, so `TRANSFER_MODE 0x0C` is NOT
src/entry/entry_storage.c:2076: * promise here: no data-path register at all** (`BLOCK_SIZE`, `BLOCK_COUNT`, `TRANSFER_MODE` and
```

**Two hits, and both are comments** — the `:2038` one explaining that the driver's
`sdhci_set_transfer_mode` returns on its first line for a data-less command, and the `:2076` one that
states the absence directly: *"no data-path register at all"*. There is no `ST_SDHCI_BLOCK_SIZE`
define, no store to `0x04`/`0x06`/`0x0C`, and no `ADMA` anywhere in the tree.

**`ST_SDHCI_DMA_ADDRESS` (`0x00`) is defined and is not a data path.** It appears three times: its
define, a rung-2 read published as `_mode_dma_address` (`:695`), and a rung-3 read into `hci_version`
(`:4932`). **Both reads are the register used as what the vendor's own code also uses offset `0x00`
for at bring-up — a version/identity read — and neither is a transfer address.** The name is in the
tree; the *path* is not.

## 5. What this changes, and it is the size of the remaining distance

**A perfect CID at rung 31 does not move this ladder one command closer to 「挂载存储」.** Row 1a is
still worth the press — it is the last item the ladder's *existing* four-command sequence asks for, and
it would settle the 136-bit question the last four rungs have been circling — but its stated consequence
was wrong, and the real distance is larger than the row implied:

| what the row said would open | what the tree has |
| --- | --- |
| CMD9 `SEND_CSD` — read the card's CSD | **no define, no body, no call site** |
| CMD7 `SELECT_CARD` — select it by RCA | **no define, no body, no call site** |
| CMD16 `SET_BLOCKLEN` — set the block length | **no define, no body, no call site** |
| "then data" | **no `BLOCK_SIZE`, `BLOCK_COUNT`, `TRANSFER_MODE` define or store; no ADMA; no DMA address used** |

**So the ladder's frontier is two frontiers, and only one of them was named.** (i) The command-level
question the last five steps have been on — what CMD2 does with a 136-bit word. (ii) **The breadth
question, which no step has named: the image's storage path is an initialization sequence and stops
there.** A mount needs bodies that do not exist, and the cheapest of them (CMD9, a 136-bit read the
ladder's own `_cid_raw*` machinery already performs against exactly those four registers) still needs
CMD7 and CMD16 and a data path behind it.

**This does not make rung 31 a worse press, and it does not make it a better one.** It makes the row
honest, and it says what the next *build* is about: **the first body past CMD3, not another bit at CMD2.**

## 6. The correction, and where it lands

Per this project's standing rule, `docs/experiments/**` is a historical record and is **not** rewritten:
792's row and 793's row stand as written, and this document supersedes them.

The **live** narration is corrected in place, appended beside the original sentence rather than
replacing it: `tools/verify_press_ready.sh`'s `entry_conseq` rung-30 branch and `rung_para 30`, the two
places whose text the next press prints. Plus a `# 794:` block in `records/revert-set.txt` and a row in
`docs/experiments/README.md`.

**Lane, stated rather than left to be inferred.** That file is not `preflight_boot_check.sh`, the one
file this session (`run-experiment-526`, the gate owner) owns; `ListAgents` names two peers
(`zl1-bb10-10`, `xing4-decode-launch-profile`) and **neither is `mi4-ios6-1a`**. So this is the
**LANE-ownerless** case and not the COST case — the same reason 793 recorded, and the second time in two
steps that the same file had no live owner to report a wrong press-facing sentence to.

## 7. A defect this step made itself, recorded because the selector is the whole lesson

The first pass of §4's search was written `grep -rniE 'BLOCK_SIZE|BLOCK_COUNT|TRANSFER_MODE|ADMA|...'`
— **case-insensitive and unanchored** — and returned fifteen lines. **Fourteen of them were
`deadman`**: `stage90_disarm_deADMAn_timer`, `stage90_arm_deADMAn_reset`, `g_deADMAn_armed`. The
selector matched a substring of a word it was never about, and a table built from that output would have
said the tree contains ADMA machinery.

**Anchored (`\b…\b`) and case-sensitive, it is two hits and both are comments.** The class is the
m-class *a selector looser than its claim*, whose instance 789 §1 recorded as `apps_before` containing
`ps_before`; this one is the same defect with the case flag as the un-tightened dimension, and it is
recorded rather than quietly fixed because the wrong table looked exactly like a measurement.

## 8. What this does not do

- **It builds nothing, edits no source under `src/`, and does not move `out/` or any park.** The armed
  arm is 792's, unchanged and unspent.
- **It spends no press and authorizes none.**
- **It does not answer the CMD2 question**, and it does not say rung 31's rows are wrong: rows 1a, 1b, 2,
  3 and 4 are unchanged and their readings stand. What changes is what row 1a *buys*.
- **It does not name the next arm's shape beyond its subject** (§5): the first body past CMD3. Whether
  that is CMD9 or CMD7 first, and what it may safely store, is not decided here.
- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made, so
  「让os可以正常启动并且挂载存储」 is not reached and **TWRP-to-storage stays withheld.**

## 9. Owed, and named rather than left to be inferred

- **The bodies past CMD3** (§5) — CMD9, CMD7, CMD16 — and behind them a data path. **This is the
  ladder's largest named gap and it is COST-owed to builds, not to a press.** The first of them is
  cheap in this image's own terms: a 136-bit read against the four `RESPONSE` registers the ladder
  already reads.
- **`ST_CMD_OP_SEND_CSD` and its `_Static_assert` pair** — the word and the response length pinned the
  way CMD0/1/2/3 are, before any body is written. COST-owed.
- **The `0x40ff8080` / rung-31 row-1a consequence, in the two live narration sites** — corrected by this
  step; the printed text is what a press reader sees.
- Unchanged from 787–793: the `0x40ff8080` "the card ANSWERED" comment at CMD1's head (COST); a
  `_cmd1_raw_pre*` / `_cmd1_raw_post*` pair around CMD1's own `RESPONSE` registers (COST); a
  `_cid_resp_changed` bit computed from the four raw words (793 §8, COST); `c3.inhibit_timeout`
  unpublished for the `nidx` family (COST); the four `*_status_post` masked cells (791 §5, COST); the
  four `5,088,000`s and the mis-citation at `entry_storage.c:302-303` (COST); a check that counts
  `ST_LIVE` sites per key (789 §4); the `_Static_assert` message's bit map at `entry_storage.c:2360`
  (789 §5); `_cid_ps_after`'s second producer (789 §2, COST); the set-comparison pad repair (779 §7);
  the `rung_para` correction for values 12..23; the seam-address class; `run_and_capture.sh`'s
  `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic FDT cell (782 §6); 784's `rail_name` cell;
  and 783's window-scope check — landed at rung 30's window and not at the ladder's other windows.
