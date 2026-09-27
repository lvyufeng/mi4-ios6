# 786: the rung-29 press — the widened window names an error, and refutes a claim its own step made about scope

**ONE PRESS SPENT. THE ARM IS SPENT.** `armed-storage-5fca6210` (`STAGE90_XNU_STORAGE_PROBE=28`)
was pressed **2026-09-27 15:18:47–15:20:01 UTC, EXIT 0 (returned and captured)**, the device back
**29 s** after `fastboot boot` (seen via adb). Readiness **5 of 5** exit 0; **one** gate, exit 0 /
**605** lines at 15:18:11; **one** runner; no firer; `fastboot boot` only; nothing flashed; serial
`4a2fe00b`; `33e80afe` absent from **both** lists. Capture archived by hand:
`out/stage90/captures/rung29-2win-20260927-152001-{last_kmsg.txt,gate.log,run.log}`, the log
633,827 B sha256 `f9012bf6…`. `out/` is byte-identical before and after this step — this step is a
**reading**, not a build.

## 1. The act is confirmed at both stores — and one bound on what an enable can be

| key | value | what it is |
| --- | --- | --- |
| `_ena_wrote` | `0x000f0001` | the five-bit word, at `st_cmd_path`'s window |
| `_cid_ena_wrote` | `0x000f0001` | the five-bit word, at `st_all_send_cid`'s window |
| `_ena_wrote_back` | `0x00000000` | the restore stores **zero** |
| `_ena_readback` | `0x00008000` | and it reads back `0x8000` |

So `SDHCI_INT_ERROR` (bit 15) is a bit of `INT_ENABLE` that **no write clears** — 736 measured it
with a press, and every enable readback in this capture carries it. The arm's own act reached the
block, out of the log, at both sites.

## 2. The news: the CMD2 window produced a *named* error

| key | value |
| --- | --- |
| `_cid_status_any` | **`0x00028000`** = `SDHCI_INT_ERR \| SDHCI_INT_CRC` |
| `_cid_err` | `0x00020000` (CRC alone, out of the driver's own `SDHCI_INT_CMD_MASK`) |
| `_cid_any_polls` | `0x00000001` — it stood at the **first** poll |
| `_cid_complete` / `_cid_timeout` / `_cid_polls` | `0` / `0` / `1` |

No arm before this one could have reported either bit. Rung 16's pressed arm read
`_cid_status_any = 0`, and every arm since carried that as the CMD2 row. **So the widening bought
exactly the class of reading it was built for — at one of the two windows.**

## 3. And it refutes 785's own claim about the other one — by the log's line order

785 said `st_cmd_path`'s window “carries CMD0 and CMD1”. **It is CMD0 alone.** The capture says so
without any reading of the source: `_ena_wrote_back = 0x00000000` — the restore — stands at
capture line **8789**, *before* `_cmd1_status_any` at line **8832**. So CMD1 ran with
`INT_ENABLE = 0x00008000`, and

- `_cmd1_status_any = 0x00000000` over `_cmd1_polls = 0x004da000` = **5,087,232**
  (*not* 5,088,256 — this step's first draft wrote that and `make check` refused the pair, which is
  778's class and is the same conflation `records/revert-set.txt:5349` had already corrected)
- `_cmd1_timeout = 1`

is a reading under a **narrow** mask and **not** under the widened word — the same masked reading
rung 28 would have produced.

The source had said it all along. The `>= 14` window's own comment reads *“the interval it was open
for is exactly CMD0's own send and poll, and there is no path out of `st_cmd_path` that reaches the
gate, CMD1, or a `return` with the enable still set.”*

**783 read the SCOPE by counting which COMMAND each FUNCTION carries. A window is an INTERVAL, and
the two are not the same question.** The false sentence is in five places; corrected in
`records/revert-set.txt` and `tools/verify_press_ready.sh`, and **COST-owed to the next build** in
the entry source's own `>= 28` comment (correcting a comment forces a build — 771's rule — and this
arm is already spent). The 785 experiment log is a historical record and is **not** rewritten; this
log supersedes it.

## 4. And it narrows 765's premise, which is the sharper of the two findings

765 established that `INT_STATUS` here is a **gated view**, and every arm since has read
`_status_any = 0` as a reading about a one-bit **mask**. But `SDHCI_INT_ERROR` (bit 15) is enabled
on **every** arm (§1), and on this block it **tracks** the error bits rather than standing beside
them:

| key | value | reading |
| --- | --- | --- |
| `_nidx_status_any` | `0x00018000` | `ERR \| TIMEOUT` — CMD3's timeout, rung 23's cell reproduced |
| `_cid_status_any` | `0x00028000` | `ERR \| CRC` — the CMD2 row above |

So a zero `_status_any` on the narrow arms was **not blind to the existence of an error**. What a
one-bit command window could not show is the error's **identity**; what it could not show at all is
a non-error bit other than the completion. **765's premise is true and narrower than this ladder has
been stating it: the widening buys identity, not existence** — which is exactly what §2 shows, and
why the CMD2 row moved from `0` to a named CRC while the CMD1 row would not have moved at all.

## 5. Rung 28's pad question is answered in the same press — and the answer is the row nothing is written on

| key | value |
| --- | --- |
| `_pad_raw` | `0x00009f24` — **equal to `_pad_expect`** |
| `_pad_match` | `1` |
| `_pad_write_skipped` | `1` |
| `_pad_writes` | **`0`** — nothing was stored |
| `_pad_cmd_pull` / `_pad_data_pull` / `_pad_rclk_pull` | `3` / `3` / `1` |
| `_pad_clk_hdrv` / `_pad_cmd_hdrv` / `_pad_data_hdrv` | `4` / `4` / `4` |

The TLMM's SDC1 pad register was **already at the vendor's `CORE_PWRCTL_BUS_ON` state** when the arm
read it — the bootloader put it there, since no rung and no Linux on this path wrote it. **768 §5's
other half is retired**, and `PULL SDC1_CMD` is present as 769 §2's mechanism requires. **The guard
held: the arm's first row is the one on which its ONE STORE does not happen**, and `_pad_writes = 0`
is that fact read out of the log. 776 §3's `0x9E00` row (pulls present, drive off) did not occur.

## 6. The CMD line moved during three of the four commands

| command | `_cmdlow_seen` | window |
| --- | --- | --- |
| CMD0 | `0x1bc` = 444 of the first 1024 samples | `_cmd0_polls = 0x219` |
| CMD1 | `0x28a` = 650 of 1024 | `_cmd1_polls = 0x004da000` |
| CMD2 | `0x0` | `_cid_polls = 1` — **a window too short to sample the line**, not a reading that the line was still |
| CMD3 | `0x176` = 374 of 1024 | `_nidx_polls = 0x000005d5` |

**764 §4's candidate 3 (“nothing was ever driven onto the bus”) is RETIRED**: the block drives the
line. What stays open is what it was driven *with*, and what the card did about it.

## 7. What this press does not decide, named rather than left

The CRC is a **command-level** error bit and the block raises it **at the first poll after the
command word was written**. Rung 6 set the card clock to 400 kHz, at which a 48-bit command and its
response take ~240 µs — and CMD2's own window lasted `_cid_ticks = 0x11`, far too short for a card's
answer to have been clocked in. `_cid_stale = 1` with `_cid_clear_after = 0` says the latch was
**clean** before the command, so the bit did not stand there from an earlier window. But whether it
is

- **(a)** the card answering and the block refusing the answer,
- **(b)** the block signalling an **illegal command** because CMD1's 1.2 s timeout left its command
  state machine wedged (rung 19 measured exactly that wedge: `_rca_inhibit_seen = 0x400` with bit 0
  still set at the window's end), or
- **(c)** the bit re-asserting for a reason this capture does not carry,

**is not decided by this press.** `_nidx_status_pre = 0x00028000` — the same two bits still set at
CMD3's window, one command later — is the cell that says the pair is **sticky** across at least one
further command, and it is the cheapest next reading there is: **it is already in this capture.**

## 8. What this does not do

- **It does not make the card answer.** It names an error on CMD2 and completes no transfer.
- **It spends no new arm**: one press, one arm, and the arm is spent. Rung 28 need never be pressed
  on its own — its question was answered here off the containment claim.
- **It does not correct the entry source.** That correction is COST-owed and named.
- **No filesystem is reached and no mount is made**, so **TWRP-to-storage stays withheld.**

## 9. Owed

- **The entry source's `>= 28` comment** at `st_cmd_path`'s window still says the window “carries
  CMD0 and CMD1”. It is wrong; the next build owes the correction (771's COST rule).
- 783's **window-scope clause as a clause** — a check that reads each enable window's OPEN and CLOSE
  sites and reports which command each *interval* contains, rather than which command each function
  mentions. §3 is the hand reading; the clause is still owed, and this press is the measurement that
  would have caught it.
- Unchanged: `entry_storage.c:302-303`'s mis-citation and the four `5,088,000`s; the set-comparison
  pad repair (779 §7); the `rung_para` correction for values 12..23; the seam-address class;
  `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic FDT cell (782 §6);
  784's `rail_name` cell.
- **The mechanism of §7** — (a)/(b)/(c) — with `_nidx_status_pre` in this capture as the first
  cheap step.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture); the SDHCI storage driver does not exist — the block names an error on CMD2 and
still completes no transfer — and 「让os可以正常启动并且挂载存储」 is not reached, so **TWRP-to-storage
stays withheld.**
