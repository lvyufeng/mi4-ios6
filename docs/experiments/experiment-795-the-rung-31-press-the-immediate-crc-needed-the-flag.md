# 795: the rung-31 press — the immediate CRC needed the CRC FLAG as well as the 136-bit request, and what replaced it is the block's own 665.2 µs timeout, which is an unset register

**ONE PRESS SPENT, AND IT WAS AUTHORIZED BY THE OPERATOR BEFORE THE GATE WAS RUN.** One gate (exit 0,
607 lines), one runner (EXIT 0), one `fastboot boot`, nothing flashed, nothing written to storage, no
reboot. `out/` moved: the arm in it is rung 31's and it is now **spent**. This step builds nothing and
edits no source under `src/`.

| | |
| --- | --- |
| arm | `armed-storage-f9613649`, `STAGE90_XNU_STORAGE_PROBE=30` (value 30 = ordinal rung 31) |
| boot image | `out/stage90/stage90-qcdt.img` sha256 `782c846f…` (8,572,928 B) |
| fired | **2026-09-28 03:43:44 UTC**, EXIT 0 |
| gate | `scripts/preflight_boot_check.sh --allow-xnu-entry` — **exit 0, exactly one run, 607 lines** |
| runner | `scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-f9613649` — **exactly one run** |
| capture | `out/stage90/captures/rung31-nocrc-20260928-034344-last_kmsg.txt`, 633,586 B, sha256 `9c0baf80…` |
| archived by hand | `…-last_kmsg.txt`, `…-gate.log`, `…-run.log` (the runner does not archive) |
| `4a2fe00b` / `33e80afe` | `4a2fe00b` present as `device`; **`33e80afe` absent from BOTH `adb devices` and `fastboot devices` at fire time** |

**The runner's verdict is 7 PASS / 1 FAIL / 3 UNREAD — eleven criteria, counted per criterion and not
per line** (the block prints twice in one log, the gate's reading of the previous capture and the
runner's of this one). The single FAIL is the known `seam_sp … is not sleh_sp-8` criterion and the three
UNREADs are the known absent-key ones — **identical to rungs 29's and 30's, so this press added no
failure.**

## 1. The arm took effect, and the two registers say so

| key | rung 30 (word `0x0209`) | **rung 31 (word `0x0201`)** |
| --- | --- | --- |
| `_cid_flags` | `0x00000007` | **`0x00000003`** |
| `_cid_word` / `_cid_word_read` | `0x00000209` / `0x00000209` | **`0x00000201` / `0x00000201`** |
| `_cid_op` | `0x00000002` | `0x00000002` |

**The block's own copy of the command word is `0x0201`, read back from `COMMAND 0x0E`.** So the one
constant moved exactly as built and the arm is the arm the record says it is.

## 2. The headline: `ERR | CRC` at poll 1 became `ERR | TIMEOUT` at poll 1,797 — and the latch moved 751×

| key | rung 30 | **rung 31** |
| --- | --- | --- |
| `_cid_status_any` | `0x00028000` = **`ERR | CRC`** | **`0x00018000` = `ERR | TIMEOUT`** |
| `_cid_err` | `0x00020000` (CRC) | **`0x00010000`** (TIMEOUT) |
| `_cid_timeout` (the driver's own flag) | `0x00000000` | `0x00000000` |
| `_cid_any_polls` / `_cid_polls` | **`0x00000001`** | **`0x00000705`** = **1,797** |
| `_cid_ticks` | **`0x00000011`** = **17** | **`0x000031e2`** = **12,770** |
| `_cid_complete` | `0x00000000` | `0x00000000` |
| `_cid_inhibit_seen` | `0x00000001` | **`0x00000000`** |
| `_cid_inhibit_after` | `0x00000001` | **`0x00000000`** |
| `_cid_cmdlow_seen` | **`0x00000000`** | **`0x000001bd`** = **445** of 1024 |
| `_cmd0_ticks` (the control) | `0x132a` = 4,906 | `0x1327` = **4,903** |

**Row 2 of 792 §7's table — `ERR | TIMEOUT` — is the row that landed.** With the CRC check removed from
the command word, the immediate CRC is **gone**, and what takes its place is the block's own response
timeout, fired after **12,770 ticks** instead of **17**.

**And the two companion cells are what make this a reading rather than a different failure:**

- **`_cid_cmdlow_seen` moved `0 → 445 of 1024`.** On rung 30 the CMD line was never seen LOW during the
  window — the immediate CRC landed **before the command went on the wire**. On rung 31 the block
  **drove the bus** and then waited. **A 136-bit command was actually issued this time.**
- **`_cid_inhibit_after` moved `1 → 0`.** On rung 30, CMD2 wedged and CMD3 was refused by the driver's
  own `CMD_INHIBIT` guard (`_nidx_sent = 0`). Here CMD2 released the block, **CMD3 was sent**
  (`_nidx_sent = 0x00000001`) and its window ran.

## 3. 12,770 ticks is 665.2 µs, and that is a CONSTANT this ladder has measured before

`_cid_ticks = 12,770` at this device's own 19,200,000 Hz is **665.2 µs**. And rung 31's own CMD3 —
sent in the same boot, for the first time since rung 19 — reads:

| cell | value | = |
| --- | --- | --- |
| `_nidx_polls` | `0x00000701` = **1,793** | (CMD2: 1,797) |
| `_nidx_ticks` | `0x000031e4` = **12,772** | **665.3 µs** |
| `_nidx_status_any` | `0x00018000` = **`ERR | TIMEOUT`** | (CMD2: `0x00018000`) |
| `_nidx_err` | `0x00010000` | (CMD2: `0x00010000`) |
| `_nidx_cmdlow_seen` | `0x00000187` = 391 of 1024 | the block drove the bus |

**Two different commands, two different windows, one bound to within 0.02 %.** And it is the bound 769
already measured at rung 24: *"a break at poll 1789 in **665.2 us** = `TIMEOUT_CONTROL 0x00`'s `2^13`
TMCLK cycles."* **So 665.2 µs is not a property of CMD2 or of CMD3 — it is this block's timeout, and
both commands hit it exactly.**

## 4. And the register that sets that bound has NEVER BEEN WRITTEN — it sits at its reset value

```
$ grep -n 'ST_SDHCI_TIMEOUT_CONTROL' src/entry/entry_storage.c
212:#define ST_SDHCI_TIMEOUT_CONTROL 0x2Eu  /* sdhci.h:111 - a BYTE, and **READ ONLY UNTIL RUNG 23 …**
4233:            (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_TIMEOUT_CONTROL));
```

**Two hits: the define and one read.** There is no store to `0x2E` anywhere in the image, and rung 31's
own capture agrees — **`_nidx_tout_ctl = 0x00000000`**, the reset value, which is the **shortest**
timeout the register can express.

**And the source says why, in its own words**: the vendor writes it only from `sdhci_prepare_data`
(`sdhci.c:827-828`), inside `if (data || (cmd->flags & MMC_RSP_BUSY))` — **so a data-less command with
no `MMC_RSP_BUSY` leaves it at the reset value**, on purpose, because a data-less command is not
expected to need a long wait.

**This ladder's commands are all data-less.** So every response demand in this ladder has been racing a
`665.2 µs` bound that no rung has ever set.

## 5. The timing ladder, and the arithmetic it implies

Five commands, one boot, one counter:

| command | flags | word | response | ticks | = | outcome |
| --- | --- | --- | --- | --- | --- | --- |
| CMD0 `GO_IDLE_STATE` | none | `0x0000` | none | 4,903 | 255.4 µs | completed |
| CMD1 `SEND_OP_COND` | `PRESENT` | `0x0102` | 48-bit, no CRC | **10,276** | **535.2 µs** | **COMPLETED** |
| CMD2 `ALL_SEND_CID` (rung 31) | `PRESENT | 136` | `0x0201` | **136-bit**, no CRC | **12,770** | **665.2 µs** | `ERR | TIMEOUT` |
| CMD2 `ALL_SEND_CID` (rung 30) | `PRESENT | 136 | CRC` | `0x0209` | 136-bit, CRC | **17** | 0.9 µs | `ERR | CRC` |
| CMD3 `SET_RELATIVE_ADDR` | `R1_NOIDX` | `0x030a` | 48-bit, CRC | 12,772 | 665.3 µs | `ERR | TIMEOUT` |

**CMD1's 48-bit response arrives at 535.2 µs — INSIDE the 665.2 µs bound, by 130 µs.** That is the
narrowest margin in the ladder and it is why CMD1 is the one command that completes.

**And the arithmetic that follows, with its assumption named:** adding one 48-bit response to a 48-bit
command added **5,373 ticks** to the window (10,276 − 4,903). A 136-bit response is **88 bits more**. At
the same measured per-bit rate — **and this is the assumption; the rate is not separately measured** —
CMD2's response would arrive 88 × (5,373/48) = **9,850 ticks** later than CMD1's, at **20,126 ticks =
1,048.2 µs**.

**The bound is 665.2 µs.** The response would need **1,048.2 µs**. **The bound is 63 per cent of what a
136-bit response needs**, and CMD2 therefore times out before its answer could possibly have arrived.

**The model's own weakness, named rather than hidden**: the per-bit rate it implies is 5,373/48 = 111.9
ticks/bit = 5.83 µs/bit ≈ **171.5 kHz**, and rung 6's own clock set is **400 kHz** (2.5 µs/bit). The
measured rate is 2.33× slower than the configured one and **this step does not explain that**. The
model's *conclusion* — that a 136-bit response cannot fit in 665.2 µs — does not depend on the
discrepancy: at the configured 400 kHz a 136-bit response still needs 340 µs of shifting on top of CMD1's
measured 535.2 µs, which is 875 µs, still past the bound. **Both rates put the answer out of reach.**

## 6. And why CMD3 fails is a different question, answered by a protocol claim rather than by this window

**CMD3's 48-bit response should arrive at about the same 535.2 µs CMD1's does** — the same response
length, the same clock, the same window shape — and it does not; it hits the bound. **So CMD3's failure
is not a timing failure and §5 does not explain it.**

The explanation this record offers is a **claim about the card's state machine, named as a claim and not
measured here**: in the MMC protocol a card enters IDENT state only by accepting `ALL_SEND_CID` (CMD2),
and `SET_RELATIVE_ADDR` (CMD3) is **only accepted in IDENT state**. On this ladder **CMD2 has never
completed**, so the card has never entered IDENT, so CMD3 gets no answer — and it hits the bound for the
same reason a command sent to a card that is not listening does.

**That reading makes CMD2 the single root of everything downstream**, and it is the one claim in this
document a future arm can falsify cheaply: if CMD2 is made to complete and CMD3 still times out, the
state-machine account is wrong.

## 7. What the next arm is, and it is the first FIX this ladder has had

**One byte store to `TIMEOUT_CONTROL 0x2E`, to a value above 0, before CMD2.** `0x01` doubles the bound
to 1,330.4 µs; `0x02` gives 2,660.8 µs; the register's maximum is `0x0E`.

**Every rung since rung 1 has been a measurement. This one is a repair, and its prediction is numeric**:
if §5's arithmetic is right, CMD2 with a bound at or above ~1,050 µs **completes**, the 136-bit response
lands in the four `RESPONSE` words, and — on §6's claim — CMD3 then answers too.

**The rows, and the first two are the ones that matter:**

| row | reading |
| --- | --- |
| **`_cid_complete = 1` AND the four words CHANGED** | **THE CID IS TAKEN**, §5's arithmetic is confirmed, and the ladder has its first real card answer on a 136-bit request. The four words are already read and already published — `_cid_raw0..3` and `_cid_resp0..3` — so the reading needs no new cell. |
| **`_cid_complete = 1` and the words unchanged** | §5's timing is right and the response path is still not delivering: the fifth row 793 §5 named, now on an arm where the bound is not the suspect. |
| `ERR | TIMEOUT` again, at a LONGER tick count | the bound was raised and the command still ran out of it: §5's arithmetic is **refuted** and the 136-bit request needs more than the model says. The tick count says how much. |
| `ERR | CRC` returns | the CRC check is not about the command word's flag after all, and §2's reading is wrong. |

**The safety surface of that arm is one byte, one register, no new address and no new device** — and
`TIMEOUT_CONTROL` is a register the driver itself writes on the data path, so writing it is the vendor's
own act rather than a new one.

## 8. What this changes about the ladder's record

- **791 §4 is REFINED, not confirmed and not refuted.** It said *"the correlate is the response LENGTH,
  not the CRC enable"* and named the 136-bit request as the one thing CMD2 has that no other command
  has. Rung 31 removes the CRC flag from that same 136-bit request and the immediate CRC **disappears**.
  So the immediate CRC requires **both** conjuncts — a 136-bit request **and** the CRC check in the word
  — and 791 named one and missed that the other is necessary. **Neither alone produces it**: CMD3 has
  the CRC flag and never latched the CRC bit, and CMD2 without the flag does not either.
- **786 §7's and 788 §7's anomaly is EXPLAINED.** *"The CRC is reported at poll 1 of a window whose
  136-bit response cannot have been shifted"* — the answer is that the block raises that CRC **without a
  response**, as a property of being handed a 136-bit request with its CRC check enabled. It is not a
  report about the card at all, and it never was.
- **790 §5's `0x40ff8080` discount is UNCHANGED and is *strengthened*.** The word is identical on rungs
  19, 29, 30 and 31 — including rung 31, where the block drove the bus and timed out. So it is present
  both when CMD1's window was closed and when it was open, and it is not evidence the card answered.
- **The frontier moved from CMD2 to the TIMEOUT REGISTER.** The subject is no longer *what is wrong with
  the 136-bit path*; it is *the bound this ladder has been racing since rung 1, which no rung has ever
  set*.

## 9. What this does not do

- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made, so
  「让os可以正常启动并且挂载存储」 is not reached and **TWRP-to-storage stays withheld.**
- **It does not prove §5's arithmetic.** The model is an extrapolation from two measured points with its
  assumption named; §7's third row is the arm that would refute it.
- **It does not establish §6's state-machine claim.** That is protocol knowledge offered as a claim, and
  the arm that would falsify it is named.
- **It does not touch the PMIC, the rails, the clocks, the DLL, the pad register, or any address outside
  the block's own register file.** 784's rail reading stands.
- **It explains nothing about the 171.5 kHz-versus-400 kHz discrepancy** (§5), which is named and left.
- **No press is authorized.** Rung 31 is spent and no further arm is armed.

## 10. Owed, and named rather than left to be inferred

- **The `TIMEOUT_CONTROL 0x2E` arm of §7** — one byte, one register, the first repair rather than a
  measurement. **This is the ladder's next build.**
- **The effective bit rate** (§5) — 5.83 µs/bit measured against rung 6's 400 kHz configured. Unexplained
  and possibly a second bound the ladder has been sitting on.
- **A `_cid_resp_changed` bit** (793 §8, COST) — §7's first row is still an eye comparison across five
  cells.
- Unchanged from 787–794: the `0x40ff8080` "the card ANSWERED" comment at CMD1's head (COST); a
  `_cmd1_raw_pre*` / `_cmd1_raw_post*` pair around CMD1's own `RESPONSE` registers (COST, and §5 makes it
  more valuable); `c3.inhibit_timeout` unpublished for the `nidx` family (COST); the four `*_status_post`
  masked cells (791 §5, COST); the four `5,088,000`s and the mis-citation at `entry_storage.c:302-303`
  (COST); a check that counts `ST_LIVE` sites per key (789 §4); the `_Static_assert` message's bit map at
  `entry_storage.c:2360` (789 §5); `_cid_ps_after`'s second producer (789 §2, COST); the set-comparison
  pad repair (779 §7); the `rung_para` correction for values 12..23; the seam-address class;
  `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic FDT cell (782 §6);
  784's `rail_name` cell; and 783's window-scope check — landed at rung 30's window and not at the
  ladder's other windows.
