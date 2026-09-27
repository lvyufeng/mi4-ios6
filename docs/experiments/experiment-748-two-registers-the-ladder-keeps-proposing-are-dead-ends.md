# 748: two registers the ladder keeps proposing are dead ends, and a third carries no information at all — settled from the archive and the vendor's own sources

**Read from the archived captures and the vendor tree. No device action of any kind** — no `fastboot`,
no `adb` to the device, no press, no gate, no runner, no build, and `out/` untouched.

**The one-line finding.** Three readings have been carried in the record as *things worth a rung*:
`SLOT_INT_STATUS 0xFC` and `CORE_PWRCTL_STATUS 0xDC` (named by 743 §7 and again by 746 §6 as *cheaper
new readings*) and `PRESENT_STATE` bit 16 `CARD_PRESENT` (which 746 §6 called *a bit that cannot be read
either way*). **The archive has already answered the first and the third — sixteen and sixteen times
respectively — and the vendor's own driver says why.** `SLOT_INT_STATUS` is read in exactly one place in
the whole vendor kernel and that place is a **debug dump**; `CARD_PRESENT` is a bit the driver's command
path **never consults on this board by construction**. And `CORE_PWRCTL_STATUS` is the one of the three
that is *live* — and it reads zero, which is a reading with content.

## 0. Why this was worth reading out of a capture rather than spending a press on

746 §6 wrote two readings down as owed: *"**which words move**, i.e. the four raw `RESPONSE` words"* —
rung 21 carries that — and *"**the card's own presence**"*, with this caution:

> `PRESENT_STATE 0x24` reads `0x01f80000` at every one of the twelve moments this capture published it,
> with bit 16 (`SDHCI_CARD_PRESENT`) clear — **and this document does not read that as "no card"**,
> because the arm's own source says at `entry_storage.c:218` that the bit *"must NOT be read as 'no card'
> here"*. **A bit that cannot be read either way is a bit that needs a second reading beside it, not a
> verdict.**

743 §7 and 746 §6 both also kept `SLOT_INT_STATUS 0xFC` on the "cheaper new reads" list, and 726 §3's
retired sixth hypothesis had already put `CORE_PWRCTL_STATUS 0xDC` beside it. **All three are answerable
without touching the device**, and the answer for two of them is that there is nothing there to find.

## 1. What the board is, in the vendor's own words

    sdhci-msm.c:2891   /*
    sdhci-msm.c:2892    * Following are the deviations from SDHC spec v3.0 -
    sdhci-msm.c:2893    * 1. Card detection is handled using separate GPIO.
    sdhci-msm.c:2894    * 2. Bus power control is handled by interacting with PMIC.
    sdhci-msm.c:2895    */
    sdhci-msm.c:2896   host->quirks |= SDHCI_QUIRK_BROKEN_CARD_DETECTION;

and the device tree describes the part as **soldered**:

    msm8974.dtsi:500   sdhc_1: sdhci@f9824900 {
    msm8974.dtsi:501       qcom,bus-width = <8>;
    msm8974.dtsi:503       reg = <0xf9824900 0x11c>, <0xf9824000 0x800>;

**So this is an eMMC on an 8-bit bus, with card detection on a GPIO, and the controller's own detection
declared broken by the vendor.** Every claim below follows from that one paragraph, and each one is
independently checkable.

## 2. `SLOT_INT_STATUS 0xFC` is a dead end, and the record has already spent sixteen presses proving it

**Three independent reasons, and the third is the one that settles it.**

**(a) The register is read in exactly one place in the entire vendor kernel, and that place is a debug
dump.**

    $ grep -rn 'SDHCI_SLOT_INT_STATUS' drivers/mmc/host/
    sdhci-dove.c:34:	case SDHCI_SLOT_INT_STATUS:
    sdhci.c:122:		sdhci_readw(host, SDHCI_SLOT_INT_STATUS));
    sdhci.h:239:#define SDHCI_SLOT_INT_STATUS	0xFC

Line 122 is inside `sdhci_dumpregs`, whose whole body is `pr_info` — and `sdhci_dove.c:34` is another
platform's dump. **Nothing writes it, nothing tests it, nothing is decided by it.** A register that only
a debugging function reads is a register whose value cannot be a fact about this ladder's question.

**(b) The mechanism that would set it is switched off.** `sdhci.c:205`:

    if ((host->quirks & SDHCI_QUIRK_BROKEN_CARD_DETECTION) ||
        (host->mmc->caps & MMC_CAP_NONREMOVABLE))
        return;

`sdhci_set_card_detection` returns before it can unmask `SDHCI_INT_CARD_INSERT`/`SDHCI_INT_CARD_REMOVE`
— so on a board with the quirk set, the slot's insert/remove interrupts are never enabled, and there is
no insert event to latch on a soldered part anyway.

**(c) The archive has read it on every storage rung since rung 3, and it is zero every time.**

| | |
| --- | --- |
| captures carrying `xnu_live_storage_reg_slot_int_status` | **16** |
| distinct values across those 16 | **`0x00000000`** — one value |

That is `698` (rung 3, the standard register file census) onward through `rung20`, on sixteen separate
boots. **The reading 743 §7 and 746 §6 both proposed as *new and cheap* has been taken on every rung
since the census existed, and it is a constant.** Nothing is owed here, and **no future rung should
spend a press on it.**

## 3. `CARD_PRESENT` carries no information on this board, and it is not two witnesses

**746 §6 was right to refuse the verdict, and this says why the refusal is stronger than a caution: the
bit is gated out by the driver's own construction.**

`sdhci.c:1539`, inside `sdhci_send_command` — the function the ladder's `st_send_command` is a
transcription of:

    /* If polling, assume that the card is always present. */
    if (host->quirks & SDHCI_QUIRK_BROKEN_CARD_DETECTION)
            present = true;
    else
            present = sdhci_readl(host, SDHCI_PRESENT_STATE) &
                            SDHCI_CARD_PRESENT;

    if (!present || host->flags & SDHCI_DEVICE_DEAD) {
            host->mrq->cmd->error = -ENOMEDIUM;

**On this board the first branch is the one taken**, so the generic core's card gate is hard-wired to
`present = true` and `CARD_PRESENT` is *never read in the command path at all*. `sdhci.c:3368` adds
`MMC_CAP_NEEDS_POLL` under the same quirk, which is the polling that replaces the interrupt. And
`sdhci.c:205` (above) keeps the detection interrupts off.

**And the ladder's own port has no such branch at all**, which is the correct transcription: the only
gate in `st_send_command` is the `CMD_INHIBIT` wait (`sdhci.c:1096`'s loop, `timeout = 10`), and the
only other early return is the `INT_STATUS` latch being unreadable. **Neither one consults
`PRESENT_STATE`'s card bit.**

**The archive agrees, and its agreement is a constant rather than a corroboration:**

| cell | captures | distinct values |
| --- | --- | --- |
| `_reg_present_state` | **16** | **`0x01f80000`** — one value |
| `_reg_card_present` | **16** | **`0x00000000`** — one value |

Sixteen independent boots, one value each. **A reading with no variance can be compared with nothing**,
which is the second reason the bit cannot carry a verdict.

**And the two keys are one read.** `entry_storage.c:648`:

    ST_LIVE("xnu_live_storage_reg_card_present", present_state & ST_SDHCI_CARD_PRESENT);

`_reg_card_present` is a **mask of the word `_reg_present_state` already published**, from the same
single `st_read32` at `0x24`. **A reader who sees two keys has read one register twice.** That is
[[mi4-one-value-two-definitions]] in its benign form — no wrong number anywhere, and a shape that
invites a reader to count two witnesses where there is one — and it is worth naming here rather than in
a footnote, because *sixteen captures × two keys* looks like a great deal of agreement.

**The word, decoded, for the reader who wants it:**

    PRESENT_STATE = 0x01f80000   (the same value in all 16 captures)
      bit 16  CARD_PRESENT        0     <- gated out by the vendor's quirk, see above
      bit 17  CARD_STABLE         0
      bit 18  CARD_DETECT_PIN     0     <- detection is a GPIO on this board
      bit 19  WRITE_PROTECT_PIN   1
      bits 20-23  DAT[3:0]        1 1 1 1   <- all four data lines idle HIGH
      bit 24  CMD line            1         <- and the command line too
      bits 0/1  CMD_INHIBIT/DATA_INHIBIT   0  (at census time, before any command)

**All five of the standard's bus lines idle high.** On a bus whose lines are pulled up, that is what
*nothing driving them* looks like — and **this document does not read it as "no card" either**: an eMMC
that is powered down drives nothing, and rung 7's power byte is the first rung that could change it.
**The bits are published; that is all that is claimed for them.**

## 4. `CORE_PWRCTL_STATUS 0xDC` is a *live* register, unlike `0xFC` — and it reads zero

**The contrast with §2 is the point, and it is why this one is not dismissed the same way.** The vendor
reads this register in **real logic**, twice:

    sdhci-msm.c:2001   irq_status = readb_relaxed(msm_host->core_mem + CORE_PWRCTL_STATUS);
    sdhci-msm.c:2877   irq_status = readl_relaxed(msm_host->core_mem + CORE_PWRCTL_STATUS);

`:2001` is inside `sdhci_msm_pwr_irq` — the handler the ladder **already ports as rung 8's intid 170
client** — and the value it reads decides which `CORE_PWRCTL_CTL` success bits get written back. So this
is a register on the ladder's own path.

**And the archive has read it after the power byte on fifteen boots, all zero:**

| | |
| --- | --- |
| captures carrying `_reg_pwrctl_status_after` | **15** (`rung6` … `rung20`, i.e. every rung from the first clock set) |
| distinct values | **`0x00000000`** — one value |

**That zero is an informative reading, not a dead register**: it says **no bus-on or IO-high event is
pending** on this block. And it is exactly what the ladder's own history predicts — 743's record of rung
7 says the vendor's `REQ_BUS_ON` handshake was **deliberately not taken** (*"sdhci-msm.c:2179-2209 would
wait_for_completion on an IRQ this image cannot deliver"*). A block that was never asked to power a bus
has nothing to report about powering one.

**So `0xDC` is a dead end for the ladder's next question for the opposite reason `0xFC` is**: it is
informative and the information says *nothing pending*. It is worth a rung only if a future rung does
the power handshake — and that rung would be about the power path, not about the command path this
ladder is on.

## 5. What this does to 746 §4b, and it is the finding that matters for rung 21

746 §4b split one outcome into three and said `inhibit_seen = 0` is **the block declining the command**,
listing its producers and retiring one of them:

> ... the sequencer refused it; or it ran so briefly no sample caught it; or the poll's own sampling
> missed it — the third is retired here because CMD1's poll ran **5,088,256** samples and saw nothing
> while CMD0's saw the bit 537 times in 538, so the sampler demonstrably can catch this bit.

**A fourth candidate producer was not on that list, and it is now eliminated by source reading: the
core's card-present gate.** It cannot be the cause of CMD1's and CMD2's silence, for two independent
reasons:

1. **on this board the generic core hard-wires `present = true`** (§3, `sdhci.c:1539-1541`), so *the
   driver the ladder transcribes does not have a card gate to refuse with*;
2. **and the ladder's own port does not either** — and if the *only* gate it does have, the
   `CMD_INHIBIT` wait, had refused, the store to `COMMAND 0x0E` would never have happened, because that
   store comes **after** the loop. **`_cmd1_word_read = 0x00000102` is therefore proof that the gate
   passed and the word reached the register** — which is the same fact 746 §4b already used, now read
   as a statement about the gate rather than only about the word.

**So the block was handed the driver's own word for CMD1 and did not raise `CMD_INHIBIT`** — and that is
exactly the reading **rung 21 tests one bit away from**, on a command this ladder has already driven to a
start. The rule rung 21 tests (*a word that asks for a response and carries no `INDEX` is declined*) is
now the **only** surviving explanation of CMD1 and CMD2 among the four candidates, and §2 has removed
`SLOT_INT_STATUS` as the innocent bystander a rung might have blamed instead.

## 6. The one register in this family that is still owed, and it is a different kind of thing

**`HOST_VERSION 0xFE`.** 697 left it owed and **it is still owed: no archived capture carries
`xnu_live_storage_reg_host_version` at all.** It is the one of the three that the archive has never
answered.

| | `SLOT_INT_STATUS 0xFC` | `CORE_PWRCTL_STATUS 0xDC` | `HOST_VERSION 0xFE` |
| --- | --- | --- | --- |
| read by vendor logic? | **no** (dump only) | **yes** (`:2001`, `:2877`) | **yes** in the generic core (`sdhci.c:3177`, `host->version = …`); **print only** in `sdhci-msm.c:2909` |
| archived? | **16×**, all zero | **15×**, all zero | **never** |
| worth a rung? | **no** — nothing to find | only with a power-handshake rung | **open** |

**It is named here as open and not as owed-to-this-rung.** The ladder's image does not run
`sdhci_add_host`, which is the only caller in the generic core, and the vendor's msm path reads it to
`dev_dbg` — so a reading of `0xFE` would be *descriptive* unless a future rung needs to know which host
version the block reports in order to justify a timeout or a quirk the ladder simulates. **That is a
question for whichever rung needs it, not a gap this document proposes filling.**

## 7. What this document does not say

- **It does not say the card is absent.** §3 decodes `PRESENT_STATE` and refuses that verdict
  explicitly, for 746 §6's reason and one more: on this board the bit is not evidence either way,
  because the driver never reads it and detection is a GPIO. **Nothing here is a card-presence
  witness**, and this document adds none.
- **It does not say the block is healthy or unhealthy.** §4's zero is a reading about the *power*
  handshake; §2's zero is vacuous. Neither touches the command path.
- **It does not change 746's headline, except by narrowing it.** The response demand was the stall for
  the one command it was measured on; §5 removes a candidate explanation for the *other* two and leaves
  rung 21's rule as the one standing.
- **It does not arm anything.** No press is owed, no firer is armed, `out/` is untouched, and **no
  press may be spent without the operator's authorization.** Rung 21 remains **ARMED AND NOT PRESSED**.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. It removes two candidate
  readings from the ladder's backlog so that the press after rung 21 is chosen from a **smaller and
  better-founded set** — and that is the whole of what it claims.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist; 「让os可以正常启动并且挂载存储」 is
not reached, so **TWRP-to-storage stays withheld.**
