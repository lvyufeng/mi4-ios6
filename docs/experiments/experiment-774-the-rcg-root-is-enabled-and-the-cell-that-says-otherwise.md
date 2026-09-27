# 774: the RCG's root is ENABLED — and the cell that says otherwise reads a bit no code in this tree writes

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO ARM BUILT, NO GATE AGAINST A DEVICE.** `out/` still holds
**`armed-storage-a1378a48`** (`STAGE90_XNU_STORAGE_PROBE=25`, ordinal rung 26), **ARMED AND NOT PRESSED**.
This is a re-reading of a register the rung-24 press already carried and of the vendor clock driver it comes
from, with every `file:line` quoted so it can be checked the same way.

## 1. What this document is for

771 §7 concluded that the clock is not a candidate for the card's silence, and it argued from **the divider
arithmetic**: `clk_set_max_clk = 0x16E36000` = 384 MHz against `clk_set_real_div = 0x3C0` = 960, i.e.
**400,000 Hz exactly**, with `SD_CLOCK_CARD_EN` set in `cmd2_clock_control = 0x0000E047`.

That argument is about **whether the ladder programmed the right number**. It does not answer whether the
root clock generator *produces* anything — and this ladder publishes **two cells** about exactly that, both
reading `0`, which a reader could take as "the root is off":

```
xnu_live_storage_clk_rcg_root_en     = 0x00000000
xnu_live_storage_clk_rcg_root_status = 0x00000000
xnu_live_storage_cmd2_rcg_root_en    = 0x00000000
xnu_live_storage_cmd2_rcg_root_status= 0x00000000
```

**Read against the vendor's own driver, one of those two readings says the opposite of what its name
suggests, and the other says nothing at all.**

## 2. Which bit is which, and which one the vendor believes

`clock-local2.c:61-65`:

```c
#define CMD_RCGR_ROOT_ENABLE_BIT    BIT(1)    /* what you WRITE to turn the root on */
#define CMD_RCGR_CONFIG_UPDATE_BIT  BIT(0)
#define CMD_RCGR_ROOT_STATUS_BIT    BIT(31)   /* what the hardware REPORTS */
```

And the vendor reads the **status** bit in three places, always the same way —
`clock-local2.c:299` (`_rcg_clk_handoff`), `:727` and `:804` (the two `set_rate` variants):

```c
	if (readl_relaxed(CMD_RCGR_REG(rcg)) & CMD_RCGR_ROOT_STATUS_BIT)
		return HANDOFF_DISABLED_CLK;
	return HANDOFF_ENABLED_CLK;
```

**So `CMD_RCGR_ROOT_STATUS_BIT` set means DISABLED, and clear means ENABLED.** The capture's
`_rcg_root_status = 0` is therefore **a positive reading that the SDC1 apps root is enabled**, in the
vendor's own terms and by the vendor's own test.

## 3. And the `_rcg_root_en` cell reads a bit nothing in this tree ever writes

```
$ grep -rn 'ROOT_ENABLE_BIT\|ROOT_ENABLE' arch/arm/mach-msm/ drivers/clk/
arch/arm/mach-msm/clock-local2.c:61:#define CMD_RCGR_ROOT_ENABLE_BIT	BIT(1)
```

**One hit in the whole tree, and it is the `#define`.** No code writes that bit. `rcg_clk_prepare`
(`clock-local2.c:152-161`) is a no-op that only `WARN`s if the rate was never set, and
`__clk_pre_reparent` (`clock.c:247`) recurses into the *parent's* prepare/enable — which for an rcg is that
same no-op. **And the one path that does write `CMD_RCGR` does not write that bit**: `set_rate_mnd`
(`:127-150`) writes `M_REG`/`N_REG`/`D_REG`, then `CFG_RCGR`'s `DIV_MASK`/`SRC_SEL_MASK` and `MND_MODE_MASK`,
then calls `rcg_update_config` — **five words and the update bit, and never `CMD_RCGR`'s root enable.**
**So `_rcg_root_en = 0` is a reading of a bit that the vendor path never asserts, and it carries
no information about whether the root is running.**

**This is m737's shape and m776's shape one more time**: a cell whose NAME states a direction — `root_en`,
0 — read as though the 0 were the finding. It is not. The finding is the neighbouring cell, and its sign is
the opposite of the one the name invites.

## 4. What this does and does not change

**It strengthens 771 §7's conclusion with a second, independent reading.** "The clock is not a candidate"
now rests on two measurements that do not share a mechanism:
1. **the arithmetic** — 384 MHz ÷ 960 = 400,000 Hz exactly, `SD_CLOCK_CARD_EN` set (§1), and
2. **the hardware's own report** — `CMD_RCGR_ROOT_STATUS_BIT` clear, i.e. the vendor's own
   `HANDOFF_ENABLED_CLK` (§2).

**It does not disturb any other rung.** Nothing in the press path reads `_rcg_root_en` as evidence of a
disabled clock: `rung_para 5` calls the RCG's five keys *"the apps root's current configuration, i.e. the
words `set_rate_mnd` (`:126-146`) will write"* — which is fair for `src`/`div`/`m`/`n`/`d`, the four fields a
`set_rate` actually writes, and the sentence is not made false here. What it invites is a reader's inference,
and that inference is the thing this document removes.

**It does not say a correct root and a correct divider put a clock at the pads.** Rung 5's own narration
already drew the distinction two levels up: `CBCR_BRANCH_ENABLE_BIT` (BIT(0)) is the *request* and
`CBCR_BRANCH_OFF_BIT` (BIT(31)) is *what the framework believes* — a branch can be requested while the root
above it is off. This document closes the *root* half of that question and leaves the **pad** half where rung
25 put it, which is the register the armed arm reads.

**It does not open a rung.** No store is proposed, no arm is built, and the ladder's clock write set is
unchanged. `_clk_rcg_root_en` stays published exactly as it is — the correction is to its *meaning*, and the
cell's own value is a reading whether or not its name is a good one.

## 5. What this document does not say

- **It does not say the card will answer.** The clock is now doubly exonerated and the pads are still the
  open question; §4's second reading is about the root, not about the pins.
- **It does not re-open 764 §1 or 768 §4.** The DLL path stays closed.
- **It does not spend a press and it does not authorize one.**
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount, no driver.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist and 「让os可以正常启动并且挂载存储」 is
not reached, so **TWRP-to-storage stays withheld.**
