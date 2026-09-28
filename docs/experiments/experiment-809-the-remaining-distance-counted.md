# 809: the remaining distance, counted — CMD9 needs no new mechanism, and the wall is CMD8

**A MEASUREMENT OF THE VENDOR DRIVER, NOT OF THE DEVICE.** No press, no device action, no runner, no
firer, no build, and **nothing in `out/` was written**: this step read the driver's own sources and
wrote a document. The arm `armed-storage-7341f5f6` is **ARMED AND NOT PRESSED** and the press is the
operator's.

The record currently states the distance as *"794 §1's breadth gap (four opcodes, **NO CMD9 DEFINE**,
no data path)"*. **That is right about the opcode and misleading about the mechanism**, and the
difference changes what the next rung costs. Measured here:

## 1. The driver's own initialization order, in its own words

From `mmc.c:1375` onward — one function, `mmc_init_card`, in file order:

| # | call | opcode | the driver's own flags | argument | ladder has it? |
| --- | --- | --- | --- | --- | --- |
| 1 | `mmc_all_send_cid` | CMD2 | bcr **R2** | `0` | **yes** |
| 2 | `mmc_set_relative_addr` | CMD3 | ac R1 | `RCA << 16` | **yes** |
| 3 | `mmc_set_bus_mode(PUSHPULL)` | — | — | — | **not a device act** (§3) |
| 4 | **`mmc_send_csd`** | **CMD9** | **ac R2** | **`card->rca << 16`** | **NO — this is the next rung** |
| 5 | `mmc_decode_csd` | — | arithmetic on the response | — | n/a |
| 6 | `mmc_decode_cid` | — | arithmetic | — | n/a |
| 7 | `mmc_select_card` | CMD7 | ac R1 | `RCA << 16` | no — **no new mechanism** (§4) |
| 8 | **`mmc_get_ext_csd`** | **CMD8** | **adtc** R1 | `0` | no — **512 BYTES ON THE DAT LINES** (§5) |
| 9 | `mmc_switch` ×several | CMD6 | ac **R1b** | — | no — **a second new mechanism** (§6) |

The opcode numbers are the driver's own header (`include/linux/mmc/mmc.h:29-52`), which prints the
access type beside each one: `MMC_SEND_CSD 9 /* ac [31:16] RCA R2 */`, `MMC_SELECT_CARD 7 /* ac
[31:16] RCA R1 */`, **`MMC_SEND_EXT_CSD 8 /* adtc R1 */`**, `MMC_SWITCH 6 /* ac [31:0] See below R1b */`.

## 2. CMD9's three ingredients already exist in this ladder, because CMD2 built them

`mmc_send_csd` (drivers/mmc/core/mmc_ops.c:296) is, for a native host —

```c
if (!mmc_host_is_spi(card->host))
    return mmc_send_cxd_native(card->host, card->rca << 16, csd, MMC_SEND_CSD);
```

— and `mmc_send_cxd_native` (`:213-233`) is the whole command:

```c
cmd.opcode = opcode;
cmd.arg    = arg;                     /* card->rca << 16, and mmc.c:1400 sets card->rca = 1 */
cmd.flags  = MMC_RSP_R2 | MMC_CMD_AC; /* 136-bit response, NO data lines */
mmc_wait_for_cmd(host, &cmd, MMC_CMD_RETRIES);
memcpy(cxd, cmd.resp, sizeof(u32) * 4);
```

**And every one of those three is already in `src/entry/entry_storage.c`:**

| ingredient | already there | since |
| --- | --- | --- |
| the flag set | **`ST_MMC_RSP_R2`** at `:2170`, `= PRESENT\|136\|CRC` | CMD2's own flags |
| the mapping to the command word | `ST_SDHCI_CMD_FLAGS`'s `ST_MMC_RSP_136 → ST_SDHCI_CMD_RESP_LONG` at `:2271`, asserted by `_Static_assert(ST_SDHCI_CMD_WORD(CMD2, R2) == 0x0209u)` at `:2287` | CMD2 |
| **the 136-bit assembler** | `:3531-3544` and `:3639-3654` — the REVERSED word order `+12, +8, +4, +0`, each `<< 8`, with the trailing byte OR'd in from the next word down | CMD2 (this is how rung 33 read a real SanDisk CID) |
| **the argument** | **`ST_MMC_RCA_1 = 0x00010000u`** at `:2206`, `_Static_assert`ed at `:2356`, already sent as CMD3's argument at three sites | CMD3 |

**So rung 35 is one opcode define, one call, and its cells** — not a mechanism. The assembler is the
non-obvious part and it is *already written*, because CMD2 is also `bcr ... R2`.

**The trap the assembler poses, and it is why this is written down rather than assumed**: the 136-bit
response is **not** four words read in order from `0x10`. The driver reads `+12, +8, +4, +0`
(`sdhci.c:1165-1172`, under the comment **"CRC is stripped so we need to do some shifting"**) and
shifts each left by 8. A builder who read the four words in order would get **a wrong 128-bit value
that still looks like a CSD** — no error, no fault, just a number that decodes to nonsense. The
ladder's own copy is at `:3531` and a new arm should **call it, not re-derive it**.

## 3. `mmc_set_bus_mode(PUSHPULL)` between CMD3 and CMD9 is not a device act on this host

The driver calls it (`mmc.c:1412`) with the comment *"set card RCA and quit open drain mode"* — and
**neither `sdhci.c` nor `sdhci-msm.c` references `ios->bus_mode` anywhere.** Checked by name over both
files: zero occurrences. `mmc_set_bus_mode` (`core/core.c`) sets `host->ios.bus_mode` and calls
`mmc_set_ios` → `sdhci_do_set_ios`, whose body reads clock, power mode, bus **width** and vdd — never
the bus mode.

**So the open-drain-to-push-pull transition writes no register on this controller**, and rung 35 must
not invent one. (What *that* means for the card's own open-drain state is a question for a press, not
for this document — and it is named here rather than assumed away.)

## 4. CMD7 is the shape of CMD3, which the ladder sends three times already

`_mmc_select_card` (`mmc_ops.c`): `cmd.opcode = MMC_SELECT_CARD; cmd.arg = card->rca << 16;
cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;` — **an R1 command addressed by the same `ST_MMC_RCA_1` value
CMD3 already uses.** The ladder has four R1-sending sites (`st_set_relative_addr`, `st_cmd3_noresp`,
`st_cmd3_noidx`, `st_cmd_path`), so this needs a new opcode and no new mechanism.

## 5. THE WALL IS CMD8, AND IT IS THE FIRST DATA COMMAND

`mmc_send_ext_csd` (`mmc_ops.c`) is `mmc_send_cxd_data(card, card->host, MMC_SEND_EXT_CSD, ext_csd,
**512**)` — and `mmc_send_cxd_data` builds a full `struct mmc_request` with `struct mmc_data`:
`data.blksz = len; data.blocks = 1; data.flags = MMC_DATA_READ;` and a scatterlist. `mmc.h:37` prints
the access type in the header itself: **`MMC_SEND_EXT_CSD 8 /* adtc R1 */`** — *adtc*, the one access
type this ladder has never sent.

**And the ladder says so in its own source** (`src/entry/entry_storage.c:2076`, written long before
this step): *"no data-path register at all (`BLOCK_SIZE`, `BLOCK_COUNT`, `TRANSFER_MODE` and …)"*.
`:2038` adds that `TRANSFER_MODE 0x0C` is **not** written because `sdhci_send_command` returns on its
first line for a data-less command.

So the distance to anything that *transfers* is: **CMD9, CMD7, and then the data path** — with
`TRANSFER_MODE 0x0C` (the `DATA_PRESENT_SELECT` bit is what makes the controller drive the DAT lines),
`BLOCK_SIZE 0x04`, `BLOCK_COUNT 0x06`, a data timeout, `INT_DATA_END` in the interrupt status, and a
buffer to read into. That work is real and it is **not** a rung-35 problem.

## 6. CMD6 is R1b, whose mapping exists and has never been exercised

`MMC_SWITCH 6 /* ac [31:0] See below R1b */`. R1b means **the card drives DAT0 busy after the
response**, so the controller must be told to use `SDHCI_CMD_RESP_SHORT_BUSY`. The ladder **has that
constant and its mapping** (`:2167`, and `ST_SDHCI_CMD_FLAGS`'s `ST_MMC_RSP_BUSY` arm at `:2273-2274`)
— **and no call site has ever passed it.** Measured: zero uses of `ST_MMC_RSP_BUSY` outside the
mapping itself.

**A value nothing reads is this project's `m719` shape**, and it is stated here as a *boundary* rather
than a defect: the mapping was written when the flags were, and no rung has needed it. The rung that
sends CMD6 is the rung that exercises it — and a busy response is a second new mechanism, in the same
family as the data path and not smaller.

## 7. The honest distance, counted rather than asserted

From rung 34 (armed and unspent) to 「让os可以正常启动并且挂载存储」:

1. **CMD9** — rung 35. No new mechanism; one opcode, one argument that already exists, one assembler
   that already exists.
2. **CMD7** — the shape of CMD3. No new mechanism.
3. **THE DATA PATH** — begins at CMD8: `TRANSFER_MODE`, `BLOCK_SIZE`, `BLOCK_COUNT`, a data timeout,
   `INT_DATA_END`, and a 512-byte buffer read over the DAT lines. **This is the wall**, and it is the
   first rung that needs more than a command word.
4. **CMD6 ×several** — **R1b**, the busy response, mapped but never passed.
5. **Bus width and bus speed** — more CMD6 against EXT_CSD, which needs the EXT_CSD read of step 3.
6. **The block layer** — CMD16 `SET_BLOCKLEN`, then CMD17/CMD18, both `adtc` again.
7. **And then a filesystem above the block device**, which is a further body of work and not a
   storage-controller rung at all.

**Two consequences the operator should have in writing.** First, **the next two rungs are cheap and
the third is not** — so the press now owed is worth taking before the wall is approached, because
rungs 35 and 36 are the cheapest rungs this ladder has left. Second, **「进入操作系统」 and
「挂载存储」 are gated by different things**: nothing in this list is what puts the OS on the machine —
this ladder is the path to a *mountable* volume, and the recorded blockers for entering the OS are
elsewhere. This document measures one of the two and does not claim the other.

## 8. What this does not do

- **No device action of any kind, and no build.** Nothing under `out/` was written; the eleven members
  of `armed-storage-7341f5f6` are untouched and their hashes still agree with the record.
- **No claim about the card.** Every line above is read out of the vendor sources and this ladder's own
  source. Whether the card answers CMD9 is a press's answer, not this document's.
- **It does not say CMD9 will work.** It says the *mechanism* exists, which is a statement about the
  image and not about the device.
- **The goal is not reached**: no transfer completes, no filesystem is reached, no mount is made, and
  **TWRP-to-storage stays withheld.**
- **The arm is ARMED AND NOT PRESSED and the press is the operator's.**

## 9. The citations in this document, and the check that now reads them

Every citation above was written with its **directory** (`sdhci.c:1165-1172`,
`drivers/mmc/core/mmc_ops.c:296`, `include/linux/mmc/mmc.h:29-52`) because
`tools/check_line_citations.py` refuses a bare basename that two live roots hold — and because the
vendor files are **not in this repository at all**, which is exactly the case that check passes over
**by name**. Their line numbers are cited against the vendor checkout and cannot be checked by
anything in this repo; the ladder's own citations (`src/entry/entry_storage.c:2170`, `:3531`, …) can
be, and the check reports that they have not moved.
