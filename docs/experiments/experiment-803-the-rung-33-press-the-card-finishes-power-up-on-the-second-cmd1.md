# 803: the rung-33 press — the card finishes power-up on the SECOND CMD1, and the CID comes back

**ONE PRESS SPENT, AND IT WAS AUTHORIZED BY THE OPERATOR BEFORE THE GATE WAS RUN.** One gate (exit 0),
one runner (EXIT 0), one `fastboot boot`, nothing flashed, nothing written to storage, no reboot. `out/`
moved: the arm in it was rung 33's and it is now **spent**. This step builds nothing and edits no source.

| | |
| --- | --- |
| arm | `armed-storage-ce2f589c`, `STAGE90_XNU_STORAGE_PROBE=32` (switch **value 32 = ordinal rung 33**) |
| boot image | `out/stage90/stage90-qcdt.img` sha256 `2c9c2492…`, 8,572,928 B |
| fired | **2026-09-28 12:30:03–12:31:20 UTC**, EXIT 0 (77 s) |
| gate | `scripts/preflight_boot_check.sh --allow-xnu-entry` — **exit 0, exactly one run, 609 lines** |
| runner | `scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-ce2f589c` — **exactly one run** |
| capture | `out/stage90/captures/rung33-opcond-20260928-123120-{last_kmsg.txt,gate.log,run.log}`, archived by hand, last_kmsg 637,213 B sha256 `6fe60a12…` |
| `33e80afe` | absent from `adb devices` **and** `fastboot devices` at fire time (measured before the gate) |
| verdict | **7 PASS / 1 FAIL / 3 UNREAD counted per criterion** — the single FAIL the known `seam_sp … is not sleh_sp-8` criterion — **identical to rungs 29–32, so this press added no failure** |

## 1. The headline, and it is row 1

| cell | value | reading |
| --- | --- | --- |
| `_opcond_calls` | `0x1` | the OCR-argument body ran |
| `_opcond_from` | `0x40ff8080` | the probe's own response — the word it derives from |
| `_opcond_arg` | `0x40ff8080` | **EQUAL to `_opcond_from`** — the derivation is a no-op, exactly as pre-registered |
| `_opcond_loop_calls` | `0x1` | the loop ran |
| **`_opcond_sends`** | **`0x2`** | **IT TOOK TWO SENDS** |
| **`_opcond_busy_seen`** | **`0x1`** | **BIT 31 SET — the card reported power-up complete** |
| `_opcond_last_resp` | `0xc0ff8080` | `0x40ff8080 \| 0x80000000` — **the same OCR the probe read, with the busy bit now set** |
| `_opcond_ticks` | `0x340ff` = 213,247 | **11.107 ms** |

**798 §6's row 1 fired.** `mmc_ops.c:157` is `if (cmd.resp[0] & MMC_CARD_BUSY) break;` — the driver's own
test — **and this is the first rung in the ladder's history to run it more than once in a boot, and the
first to see it pass.** The 100-send bound was not approached: the loop exited on **send 2**.

**And the arithmetic is exact.** The loop's 213,247 ticks are one send, then `mmc_ops.c:164`'s
`mmc_delay(10)` = 192,000 ticks, then a second send: **21,247 ticks = 1,106.6 µs for two commands**,
about **553 µs each** — each CMD1 inside the block's 665.2 µs window, which is why both completed. **The
cost the record named as its worst case was 1.06 s; what it actually spent was 1.107 ms — 958× less.**

## 2. And CMD2 answered: the CID comes back after nine arms of silence

| cell | rung 32 (and every arm since 16) | **rung 33** |
| --- | --- | --- |
| `_cid_complete` | `0x0` | **`0x1`** |
| `_cid_status_any` | `0x00018000` (`ERR \| TIMEOUT`) | **`0x00000001`** — bit 0 `SDHCI_INT_RESPONSE`, **no error bit of any kind** |
| `_cid_err` | `0x00010000` | **`0x0`** |
| `_cid_ticks` | `0x31e4` = 12,772 = 665.2 µs (the block's own timeout) | **`0x49e2` = 18,914 = 985.1 µs** (the command really ran) |
| `_cid_inhibit_after` | `0x1` (wedged) | **`0x0`** |
| `_cid_resp0..3` | `0x40ff8080 / 0 / 0 / 0` | **`0x45010053 / 0x44573136 / 0x47014a2f / 0xe00bb100`** |
| `_cid_raw0..3` | `0x0040ff80 / 0x80000000 / 0 / 0` | **`0x00450100 / 0x53445731 / 0x3647014a / 0x2fe00bb1`** |

**The four-word 136-bit read is the nine-arm pattern in neither word, and it decodes as a real CID:**

| field | bits | value |
| --- | --- | --- |
| MID | 127:120 | **`0x45` = 69 — SanDisk** |
| OID | 119:104 | `0x01 0x00` |
| **PNM** | 103:56 | **`SDW16G`** — six ASCII characters, the spec's product-name field |
| PRV | 55:48 | `0x01` |
| PSN | 47:16 | `0x4a2fe00b` |
| MDT | 15:8 | `0xb1` |

**And the second, independent reading one byte off agrees**: the `_nidx_raw_*` family holds the same
sixteen bytes shifted by one (`00450100534457313647014a2fe00bb1` against
`450100534457313647014a2fe00bb100`), and **both contain `SDW16G`**. **793 §1's row 1a is met and 793 §5's
fifth row is refuted in the same event**: the completion latch fired *and* the response file filled.

**This also retires the caveat 790 §5 and 787 §6 left standing**: `0x40ff8080` was "a static pattern, not
evidence the card answered" — **it was the card's OCR all along, with bit 31 clear because the card had
not finished power-up.** The word did not change between rungs; its *meaning* did.

## 3. And CMD3 answered too, with the RCA

| cell | rung 32 | **rung 33** |
| --- | --- | --- |
| `_nidx_complete` | `0x0` | **`0x1`** |
| `_nidx_status_any` | `0x00018000` | **`0x00000001`** |
| `_nidx_err` | `0x00010000` | **`0x0`** |
| `_nidx_ticks` | `0x31e4` = 12,772 = 665.3 µs | **`0x2b22` = 11,042 = 575.1 µs** |
| `_nidx_resp` | `0x0` | **`0x00000500`** — **RCA = `0x0500`** |
| `_nidx_resp_pre` → `_nidx_resp_post` | `0x40ff8080` → `0x0` | **`0x2fe00bb1` → `0x00000500`** (the CID's tail replaced by the RCA) |

**The card accepted CMD3 and was assigned RCA `0x0500`.** That is 795 §6's protocol claim tested on its
own terms and **confirmed in the direction it predicted**: a card enters IDENT only by accepting CMD2,
and CMD3 is only accepted in IDENT — so CMD3 failing was always a *consequence* of CMD2 failing, and the
consequence of CMD2 succeeding is CMD3 succeeding. **795 §6 reached the right shape from the wrong
direction and this press supplies the missing cause: neither command was the problem — the card was not
ready, and CMD1 is where readiness is asked for.**

## 4. The reading of the ladder, and it is one sentence

**Every arm from rung 16 through rung 32 was sending CMD2 and CMD3 to a card that had not finished
power-up, and the ladder's own gate is why.** `entry_storage.c`'s CMD2 gate tests **the probe's**
response — `c1.resp`, the word from the single CMD1 with argument 0 — where the driver tests **the loop's**
(`mmc_ops.c:157`). **The gate is not merely inverted; it is reading the wrong word.** 798 §5 said the
direction was backwards; this press shows what the direction was backwards *about*: the probe is a single
pass by the driver's own design (`mmc_ops.c:148-150`, `if (ocr == 0) break;`), so its response says
nothing about readiness, and the loop that does say is the one the image never had.

**So the repair is one line and it is now measured rather than argued**: gate CMD2 on the loop's result
rather than on the probe's. Nothing else in the sequence needs to change — CMD0, the probe, the derived
argument and the loop are all correct as built, and the 665.2 µs "timeout" that nine rungs chased was
never a timeout at all: it is what a correctly configured block reports when the card is not listening.

## 5. One difference this step does not explain, named rather than glossed

| | rung 32 | rung 33 |
| --- | --- | --- |
| `_post_end_calls` | `0x7` | **`0x8`** |
| `_post_elapsed` (last) | `0x06df35b6` = 115,291,574 = **6.005 s** | **`0x092a857e` = 153,781,630 = 8.009 s** |

The run stayed up for **one more post-end call and about two more seconds** than every arm since rung 24,
and **nothing in this arm's record predicts it**. The storage probe's own delta is 11.107 ms and cannot
account for 2 s. **Two readings are admissible and this step does not decide between them**: the deadline
is a per-call bound rather than a hard stop, so a longer-lived idle loop simply takes more calls; or the
completed commands left the controller in a state the OS's idle path behaves differently under. **It is
owed a cell, not a sentence.**

## 6. The safety contract, cell by cell

- **`fastboot boot` only — nothing flashed, nothing written to storage, no reboot.** The gate's own last
  line: `booted, never flashed, so no outcome of this run can write to storage.`
- **`xnu_live_storage_writes` 0 → 4** — the same four inherited mode-sequence stores every arm from rung
  3 carries (published per store), and **no new store of any kind**: rung 33's own two bodies have an
  **empty device set**, asserted out of the linked image by `xnu_entry_799` before the press.
- **The 11.107 ms of bus time is the whole of this arm's new cost**, and `_opcond_ticks` is the cell that
  measures it. **Nothing was left powered, driven or half-configured**: the loop's only act is the command
  it sends, and both sends completed inside the block's own window.
- **The machine stayed up** — `_post_cntfrq = 0x0124f800` = 19.2 MHz, `_post_end_calls = 0x8`, and the OS
  reached pid 1's syscalls (the runner's goal criterion, unchanged from 504).

## 7. What this does not do

- **It does not reach the goal.** **No transfer completes, no filesystem is reached, no mount is made.**
  794 §1's breadth gap is untouched and is now **the whole distance**: the image's command vocabulary is
  still four opcodes (CMD0–CMD3, **no CMD9 define at all**), `st_cmd_path()` is still called once, and
  there is still **no data path** (`BLOCK_SIZE 0x04`, `BLOCK_COUNT 0x06`, `TRANSFER_MODE 0x0C` and
  `ADMA_ADDRESS 0x58` do not exist in the image under any name). **「挂载存储」 is not reached and
  TWRP-to-storage stays withheld.**
- **It does not repair the CMD2 gate.** The gate ran, `_cid_gated = 0`, and CMD2 went on the bus and
  succeeded — **the inverted gate did not block this run**, which is precisely why the repair must be made
  deliberately rather than discovered: it is wrong about a question the loop now answers correctly.
- **It does not port `mmc_select_voltage`** (799 §2). The card's own OCR passes through the driver's mask
  and bit 30 with no intersection against the host's windows, and on this card the two derivations
  coincide — confirmed here, since `_opcond_arg` equals `_opcond_from` exactly.
- **It does not move `entry_storage.c` by one byte.** No source was edited; the arm is exactly the bytes
  that were parked.
- **No press is authorized by this step.** Rung 33 is spent and no arm is armed.

## 8. Owed, and named rather than left to be inferred

- **The gate repair**: `entry_storage.c`'s CMD2 gate must test the **loop's** result. This is now the
  next build's first item, and it is the rung-33 press's own finding.
- **`entry_storage.c:3328`'s `>= 30` guard** (798 §4) — it should be `== 30`. COST-owed to a build.
- **The stale line-number citations** — the gate is at `:4911` (cited as `:4785` in 798 §5, 799 §3) and
  the single CMD1 call at `:4808` (cited as `:4702`). **802 §7 named this; it is now this build's item.**
- **§5's two unexplained readings** — `_post_end_calls` 7 → 8 and 6.005 s → 8.009 s, with no cell for it.
- **796 §1/§3/§8's sentences about `0x0209`** and **797 §1's six 1.200 s attributions** — COST-owed.
- **`TIMEOUT_CONTROL`'s real scope** (798 §2) — still unmeasured, and now less urgent: the 665.2 µs it was
  thought to bound was not a timeout.
- **The effective bit rate, 171.5 kHz against rung 6's configured 400 kHz** (795 §10) — untouched, and
  now better posed: the CID's own timing gives a second, independent handle on it.
- **CMD9's `MMC_RSP_R2`** (797 §6) — the next command in the driver's order, and the breadth gap's first
  body, which the repaired gate makes reachable.
- Unchanged from 787–802: the `0x40ff8080` "the card ANSWERED" comment at CMD1's head (**now vindicated,
  and it is a comment, so the check is still owed**); `c3.inhibit_timeout` for the `nidx` family (COST);
  the `*_status_post` class (791 §5); the four `5,088,000`s and the mis-citation at
  `entry_storage.c:302-303` (COST); a check that counts `ST_LIVE` sites per key (789 §4); the
  `_Static_assert` message's bit map at `entry_storage.c:2360` (789 §5); `_cid_ps_after`'s second producer
  (789 §2, COST); the set-comparison pad repair (779 §7); the `rung_para` correction for values 12..23;
  the seam-address class; `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic
  FDT cell (782 §6); 784's `rail_name` cell; and 783's window-scope check.
