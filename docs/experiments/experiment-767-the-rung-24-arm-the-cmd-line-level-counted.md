# 767: the rung-24 arm — the CMD line's own level, counted; 766 §7's deferral reversed by measurement, and a clause relaxation drafted and deleted

**Host-side only. No device action, no press, no gate against a device, no runner, no firer.**
`out/` **was rebuilt** — the arm in it is now this one — and that is the whole of the device-side risk
this step carries: nothing was sent to the phone. The measurements are the two build logs, the
`classify_body` run against the built ELF, the two switch records, the clause `build_entry.sh` prints
when it accepts this rung, `tools/verify_press_ready.sh` **5 of 5 exit 0**, `tools/verify_revert_set.sh`
**11 of 11** on this arm **and 11 of 11 on rung 23's park**, and `make check` **exit 0**.

**One press spent by this step: none. No press is owed and none is authorized.**
`out/` now holds **`armed-storage-94de2ede`, ARMED AND NOT PRESSED**. `armed-storage-3a92aa52` — 766's
rung-23 arm, also never pressed — is **no longer in `out/`**, but its park is intact and unchanged
(11 members, verified against the record), so it can still be re-armed with
`tools/verify_revert_set.sh out/stage90/frozen/armed-storage-3a92aa52 --set=armed-storage-3a92aa52`. **Both arms are unspent
and the press remains the operator's decision.** Rung 24 strictly contains rung 23's window, so
pressing rung 24 answers rung 23's question *and* this one; rung 23 need never be pressed.

## 1. What this arm is: 766 §7's deferred cell, and it costs no device access at all

764 §4 reduced the frontier to three candidates and left **two of them indistinguishable from any log
this ladder has produced**: *(1)* the card is not powered and stays silent, and *(3)* the block never
actually put anything on the bus. Both produce the same picture — a command taken, `CMD_INHIBIT` held
1.2 s, no bit of `INT_STATUS`, no response.

**The one measurement that separates them is the CMD line's own signal level over the command's own
window.** `PRESENT_STATE 0x24` bit 24 is that level, and the ladder already reads it — but only as
`_nidx_inhibit_last`, **a SAMPLE AT THE END of the run**, and HIGH at the end is what an **idle line**
and a **finished transmission** both look like. One sample at the end cannot tell them apart. 765 §4
named the repair and 766 §7 **deferred** it:

> **It does not add the bit-24-LOW count** that 765 §4 named as *one new sample, not a new cell*. That
> count lives inside `st_send_command`'s poll — the one body **every** command in this ladder shares —
> so adding it would move rung 11's clause for every rung …

**This step reverses that deferral, and the reason is that the cost 766 §7 asserted is not there.**

**The change is one cell.** The poll at `entry_storage.c:2503-2506` already does

```c
if (r->polls <= ST_CMD_INHIBIT_SAMPLES) {
    r->inhibit_last = st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE);
    if ((r->inhibit_last & ST_SDHCI_CMD_INHIBIT) != 0u)
        r->inhibit_seen++;
```

so rung 24 adds, under `#if STAGE90_XNU_STORAGE_PROBE >= 23`, a second mask on **a value already in a
register**:

```c
    if ((r->inhibit_last & ST_SDHCI_CMD_LINE_LEVEL) == 0u)
        r->cmdlow_seen++;
```

with `ST_SDHCI_CMD_LINE_LEVEL = 0x01000000u`, one new field on the caller's stack struct, one
initializer, one `ST_LIVE`. **No new register, no new width, no new window, no new megabyte and NO NEW
STORE TO THE BLOCK** — and because it counts over the **same** first `ST_CMD_INHIBIT_SAMPLES`
iterations `inhibit_seen` counts, the two cells are read against each other:

> `_nidx_inhibit_seen = 0x400` with `_nidx_cmdlow_seen = 0` is a block that held its inhibit for all
> 1024 samples **while the line never moved at all.**

**The guard is `>= 23`, and that is deliberate**: every line of the change is excluded at value 22, so
**rung 23's own build is byte-identical** and its parked arm stays a build of this tree.

**Two caveats the arm's own record carries, because a zero is only a reading if the sampler could have
seen a one.**

- **`bit 24` is the line's INPUT level**, so the block reading back its **own** drive over the pad
  counts as movement exactly as a card's answer does. The cell separates *nothing was ever on the line*
  from *something was*, **not** *the block* from *the card*.
- **The count's zero is a reading only if the sampler outran the bus.** The sampling period is
  `_nidx_ticks / _nidx_polls` over exactly the window the count covers, and both are published — so a
  zero is read **beside** the period and never instead of it.

## 2. The measurement that changed the plan: the clause did **not** move, and a drafted relaxation was deleted

766 §7 gave two reasons for deferring — *the clause would move* and *`_nidx_inhibit_last` already gives
one sample*. **The second reason is refuted by §1 above**: one sample at the end cannot discriminate.
**The first reason was checked rather than assumed, and it is false** — but it was nearly written into
the tree anyway.

**The counting cell needs a store through the caller's struct pointer**, and `st_send_command`'s
image-side clause (`build_entry.sh`) asserts that set **exactly**:

```bash
if [[ "$stb_cmd_img" == "UNK:ldr UNK:str" ]]; then stb_cmd_img_ok=1; fi
if [[ $STORAGE_PROBE -ge 12 && "$stb_cmd_img" == "UNK:ldr UNK:str UNK:strd" ]]; then stb_cmd_img_ok=1; fi
```

So a second per-iteration store looked like a guaranteed refusal, and the first draft of this step
**widened the clause for `>= 23`** — replacing the two-spelling enumeration with an all-`UNK:` pattern
plus a lower bound, justified by 741's lesson that the number and spelling of these accesses is the
compiler's merge and not the arm's.

**Then `build_entry.sh` accepted the arm, with the relaxation in place — and the arm was measured
instead of trusted.** `classify_body` extracted to `/tmp/cb.sh` and run standalone on the built ELF's
own `st_send_command`:

| reading | value |
| --- | --- |
| device accesses | `f9824924:ldr f9824930:ldr f9824930:str f9824908:str f982490e:strh f982490e:ldrh f9824910:ldr` |
| **image-side accesses** | **`UNK:ldr UNK:str`** |

**`UNK:ldr UNK:str` is the first spelling the clause already accepts, for every rung.** The classifier
reports a **set of distinct mnemonics**, not counts — so a second store through the same pointer
argument adds no entry, and the clause rung 11 wrote was already wide enough for this rung.
**`stb_cmd_img` is unchanged from rung 23, and so is the device set.**

**So the relaxation was reverted** (`git checkout -- src/entry/build_entry.sh`, byte-identical to HEAD)
and the entry image rebuilt: **same bin, sha `94de2ede…`, same 5,552,764 bytes, exit 0** — which is
itself the proof that the relaxation was pure dead text. **A check weakened for a reason that is not
true is worse than no change at all**, and the only thing that made this visible was running the
classifier rather than reasoning about it.

## 3. The rung-24 shape, and what did not move

`#if STAGE90_XNU_STORAGE_PROBE >= 23` guards, and nothing else in the ladder is touched:

| site | rung 23 (value 22) | rung 24 (value 23) |
| --- | --- | --- |
| bound | `> 22` | **`> 23`** |
| the `#error` rung list | ends at 22 | **+ a rung-24 clause naming the cell, the two readings and the two caveats** |
| `ST_SDHCI_CMD_LINE_LEVEL` | absent | **`#define … 0x01000000u`** with the bit map and its two differences from `CMD_INHIBIT` |
| `_Static_assert`s | six (rung 23's window) | **+ four**: the bit is `0x01000000`; it is **not** `ST_SDHCI_CMD_INHIBIT`; it lives in the byte the spec puts the line levels in; the window is still **1024** |
| `struct st_cmd_result` | — | **`cmdlow_seen`, inside `#if >= 23`** so no lower rung's stack frame shifts |
| `st_cmd3_noidx` | rung 23's window | **unchanged**, plus the publish |
| publish | … | **`xnu_live_storage_nidx_cmdlow_seen`** under `#if >= 23` |

**Verified, not asserted:**

| reading | value |
| --- | --- |
| `entry_epilogue` | **`0x80004948`** — unmoved |
| `STAGE90_XNU_SEAM_LR` | **`0x800492dc`** — unmoved (the build's own seam clause, exit 0) |
| `xnu_arm_entry.bin` size | **5,552,764** — unchanged, the growth fitted the padding |
| payload switch set | **byte-identical** to rung 23's (`6c2b6038…`) and carries `STAGE90_XNU_ENTRY 1` |
| `stage90_fixture.macho` | `52bc9c35…` — unmoved |

Arm hashes: `xnu_arm_entry.bin` **`94de2ede…`** (the set name is its own sha256 prefix),
`xnu_arm_entry.elf` `8275bee3…`, `xnu_arm_entry-config.txt` `36343423…`,
`xnu_arm_entry-sources.txt` `809dac58…`, `stage90.bin` `486113b7…` (6,048,260 B),
`stage90.elf` `5be1d75d…` (6,110,332 B), `stage90.img` `34f795cb…` (6,051,840 B),
`stage90-qcdt.img` `e15c1e03…` (8,572,928 B), `stage90-build-config.txt` `6c2b6038…` (unchanged),
`stage90_fixture.macho` `52bc9c35…` (unchanged), `SHA256SUMS.txt` `3033dd4a…`.

**Verified host-side:** the park is `out/stage90/frozen/armed-storage-94de2ede`, **11 members**, every
one `cmp`-identical to `out/stage90/`, and `tools/verify_revert_set.sh out/stage90/frozen/armed-storage-94de2ede
--set=armed-storage-94de2ede` returns **11 ok / 0 failed**; rung 23's park verifies **11 ok / 0 failed**
beside it; `tools/verify_press_ready.sh` **5 of 5, exit 0**, resolving the live arm to this set by
hashing the live `stage90-qcdt.img`; `check_set_name_rule` reads **33 sets** (32 named by a member's
hash, 1 label); `check_payload_config_entry` reads `STAGE90_XNU_ENTRY 1` across **33** parked arms;
`check_backtick_messages` ok across 73 files; the docs index still reports the **same 7 pre-existing
violations**; `make check` **exit 0**. The two commands readiness prints, and the only ones that may be
run, are

    ./scripts/preflight_boot_check.sh --allow-xnu-entry
    ./scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-94de2ede

**Nothing has been pressed, and no firer is armed.** The runner does not archive the capture; the
archive into `out/stage90/captures/` is by hand.

## 4. The answer space, and the negative is the valuable one

| `_nidx_cmdlow_seen` | reads as | the next act |
| --- | --- | --- |
| **> 0** | **the line MOVED** — something drove it. 764 §4's candidate 3 (*the block never put anything on the bus*) is **retired**, and the card's own silence is the subject | the card and the bus: bus width, the CMD/DAT pads, the clock at the card's pins, the board file's `qcom,pad-*` settings |
| **= 0**, with `_nidx_inhibit_seen = 0x400` | the **strong form**: over 1024 samples the line never went LOW while the inhibit was held — **nothing was ever driven onto the bus**, and the subject is the block's own command path | `_nidx_tout_ctl`'s value, `CLOCK_CONTROL`'s divider at the moment of the command, and the sampling period read against the count |

**And the arm still carries rung 23's four-row enable table unchanged**, so one press answers both
questions: `_nidx_status_any = 0x00010000` alone still means the card is silent and the controller
knows it; a CRC/END_BIT/INDEX bit still means the card answered and the word was malformed; nothing
with the enables up is still the strong negative about the block's own timeout; bit 15 alone is still
the sticky-bit reading.

**The failure mode is unchanged and is a diagnosis, not a lost device**: a delivery on this block's
shared SPI 123 line ends the run at the dispatcher as `_irq_other_count = 1` / `iat = 155`, an ending
this image already reads and survives (709 ended that way on intid 170). `SIGNAL_ENABLE 0x38` is still
never written, so no line can rise.

## 5. The host-side finding that rides along: the pads are not a missing act

Rung 23's row 1 named *the board file's `qcom,pad-*` settings* as the next act if the card turns out to
be silent. **Checked host-side so the next reader does not spend a press on it.** The properties exist
and the vendor acts on them: `msm8974-mtp.dtsi:437-446` gives `&sdhc_1` `qcom,pad-drv-on = <0x7 0x4
0x4>` (16 mA CLK, 10 mA CMD/DAT) on top of the node's own `qcom,pad-pull-on = <0x0 0x3 0x3>` /
`pad-drv-on = <0x4 0x4 0x4>`, and `sdhci_msm_dt_get_pad_pull_info` / `…_pad_drv_info`
(`sdhci-msm.c:1113,1189`) feed `sdhci_msm_setup_pad` (`:966`), which calls `msm_tlmm_set_hdrive` /
`msm_tlmm_set_pull` — **writes into the TLMM block, a second device's megabyte**, which is why 692's
`addr >> 20` interlock would apply if this arm ever wanted them.

**And it is not the missing act**, for a reason that does not need a press: **both properties are
drive strength and pull, neither is a function mux.** At identification speed they are signal-integrity
settings, not the condition for a card to answer — and **the pull is measurably present already**:
`_nidx_inhibit_last` reads bit 24 **HIGH** in every arm on record (758/742/730), so the CMD line is
pulled up and a card that answered could pull it down. **The pads are therefore a candidate the
`_nidx_cmdlow_seen` cell can only confirm, not one that needs its own arm.**

## 6. What this document does not say

- **It does not spend the press.** `out/` holds this arm **armed and not pressed**; the two commands
  above are the whole path to it, and the press is the operator's decision.
- **It does not claim the count will answer.** It claims the count is the only cell that *can*
  separate 764 §4's candidates 1 and 3, and that it costs no device access.
- **It does not re-open 764.** The DLL answer stands and its path stays closed; the rung-21 `0x030A`
  stall is still the frontier.
- **It does not move any guard on a rung that has been pressed.** Rung 23's `>= 22` and rung 22's
  `>= 21` are untouched; every line of this step is inside `#if >= 23`.
- **It does not change `build_entry.sh` at all.** The relaxation was reverted; `git diff HEAD` on that
  file is empty, which is what makes the rebuilt bin byte-identical to the first build.
- **It does not retire rung 23 as an artifact.** Its park is measured intact in §3 and it is
  reproducible from this tree at value 22, because every rung-24 line is guarded out there.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. What it buys is that the
  next press — whenever it is authorized — is spent on a question the ladder has never asked *and* on
  the question 766 built the arm for, in one boot.

## 7. New instance — **m775: a check relaxed for a reason that was never measured**

The first draft of this step widened `st_send_command`'s image-side clause — an exact two-spelling
enumeration that 741 had already made deliberate — into a pattern plus a lower bound, because a second
per-iteration store through the caller's struct pointer *looked* like a guaranteed widening. **The
classifier reports distinct mnemonics, not counts**, so the set did not change at all: the arm built and
passed with the relaxation in place, and building it again after the revert produced **the same bytes**.
The weakening was dead text, and the only thing that caught it was reading the classifier's output
instead of reasoning about the store.

**Shape to suspect first: an edit made to accommodate a predicted failure rather than a measured one.**
The prediction here was about a *tool's* behaviour under an edit (how `classify_body` counts accesses)
and the tool was one command away. **The test: before widening a check, produce the input that the
widened check is for and run the un-widened one on it** — and if the un-widened check passes, the
widening is the defect. A special case of the project's standing rule that a claim about a tool is a
claim that reading the tool settles, and it is the mirror of **m757** (a clause whose *message* was an
over-claim) — there the text lied about a check that fired, here the text would have weakened a check
that never needed it. Related: [[mi4-a-claim-in-comment-is-not-a-check]], [[mi4-measurement-defects]],
[[mi4-self-written-record-is-not-a-constraint]] (a clause is only a constraint where it can refuse
something, and a relaxation nobody needed removes exactly that).

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block starts a
response-demanding command and never completes one — and 「让os可以正常启动并且挂载存储」 is not reached, so
**TWRP-to-storage stays withheld.**
