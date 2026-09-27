# 776: the rung-28 arm — the first store this ladder makes to a second device, and it is guarded

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER.** `out/` was
rebuilt and now holds **`armed-storage-d5d98738`** (`STAGE90_XNU_STORAGE_PROBE=27`, ordinal rung 28),
**ARMED AND NOT PRESSED**. One press spent by this step: **none**. Rungs 27 (`armed-storage-8096cb2c`),
26 (`armed-storage-a1378a48`), 25 (`armed-storage-f72e9f18`) and 23 (`armed-storage-3a92aa52`) are all
intact and untouched.

## 1. What the arm is — one read, and at most one write

773 §4 pre-registered this design in the order 769 §6 pre-registered rung 25 and 770 built it; 773 §5
fixed its shape. The registered rung number moved by one because value 26 was spent on 775's eleven
publishes, and that is the only thing about the design that changed.

`st_pad_census` — the body rung 25 added to **read** the TLMM's SDC1 pad-control register at physical
`0xFD512044` — now ends in an `if`:

| the read | `_pad_raw` | what happens | keys |
| --- | --- | --- | --- |
| **already matches** `ST_TLMM_SDC1_EXPECT` = `0x00009F24` | `0x00009F24` | **NO STORE AT ALL** | `_pad_write_skipped = 1`, `_pad_writes = 0` |
| anything else | anything else | one masked read-modify-write, then a read-back | `_pad_write_skipped = 0`, `_pad_writes = 1`, `_pad_wrote`, `_pad_after`, `_pad_after_match` |

The value written is `(raw & ~ST_TLMM_SDC1_ALL_MASK) | ST_TLMM_SDC1_EXPECT` — **one read-modify-write over
the union of the seven fields**, `0x0001FFFF`. The bits *outside* the seven are the bits the read handed
back and are carried through unchanged, which is what makes this a field write and not a whole-word write
to a register a bootloader may have owned.

## 2. The store is the vendor's own, and that is not the same claim as the guard

`sdhci_msm_setup_pad` (`sdhci-msm.c:964`) walks **the same seven fields to the same two arrays**
(`msm8974pro-ac-pm8941-mtp-v5.dts:25-29`), through the same shift-and-mask. So the worst case of an
*unguarded* fire would still be a word Linux itself stores on this board at every `CORE_PWRCTL_BUS_ON`
(`sdhci-msm.c:2015-2032`).

**The guard is what makes the answer readable, not what makes the store safe.** 773 §5 said it plainly and
this arm is that sentence made real: the rung is a **fork**, and its more likely row is the one nothing is
written on.

`msm_tlmm_set_field` (`gpio-msm-common.c:481-496`) is the whole mechanism — a plain `__raw_readl` / mask /
`__raw_writel` / `mb()` under a spinlock the ladder does not need: **no unlock sequence, no
write-one-to-clear, no FIFO, no shadow register.** Because it is one masked RMW over the union, the
vendor's six separate calls **in whatever order** reduce to one store, and a single store has no order to
get wrong.

## 3. The guard is refused by the build, twice and independently

`src/entry/build_entry.sh`'s rung-25 clause family grew a value-dependent half at `>= 27`:

- the store **count** is `0` at values 24..26 and exactly `1` at 27 — one clause read against the rung,
  not two policies, so the read-only arm's own safety sentence survives untouched where it was written;
- the one `str` must be **PREDICATED or SPANNED BY A CONDITIONAL BRANCH** in the body's own disassembly.
  This arm's compiled form is `SPANNED-BY(8000ecdc)` — the `beq` at that address spans the `str` at
  `0x8000edb4`. That reading is **necessary and not sufficient**, and the clause says so;
- the **source** must carry exactly one `if (raw == ST_TLMM_SDC1_EXPECT) {` with the store in its **else
  arm**, read by line number (`3565` and `3571` of `src/entry/entry_storage.c`). That is the sufficient
  half, and it is a grep — a weaker instrument than a clause on the artifact, used here only because the
  guard is a C-level fact.

Two readings, neither of which can see the whole: a perturbed build that puts the store on the only path
is refused by the first, and a rewrite that moves the store out of the guard's else arm is refused by the
second.

## 4. Containment is measured, not argued

Every new line sits inside `#if STAGE90_XNU_STORAGE_PROBE >= 27`, and then it was measured: the value-26
build **from this same source** came out **byte-identical** to the parked `armed-storage-8096cb2c`
(`8096cb2c38f1ceb84183acd09bf31298302db244dc87fdea6e74fe07dc57d287`, 5552764 bytes, exit 0) — and
`cmp` against the park's own member agrees byte for byte.

**So one press answers the SDC1 pad question, the fix question, rung 27's eleven publishes, rung 26's
four cells and rung 23's window — and rungs 23, 25, 26 and 27 need never be pressed.**

## 5. The answer is a fork, and both rows name the next act

- **`_pad_raw = 0x00009F24` with `_pad_write_skipped = 1` and `_pad_writes = 0`** — the pads were
  **already configured**, by the bootloader, since no rung and no Linux on this path wrote them (773 §2's
  latch). **The pad candidate is CLOSED**, and the frontier is the PMIC rails alone — acts 1 and 3 of the
  vendor's BUS_ON branch, which 773 §3 counted as unreachable from `hc_mem`.
- **anything else with `_pad_write_skipped = 0`** — the vendor act was never applied on this path and the
  write **has been made**. `_pad_after = 0x00009F24` means it landed; read `_pad_wrote` beside `_pad_after`
  and never instead of it.

**The safety reading is a count and not a constant**: `_pad_writes` is published as what happened, so `0`
and `1` are the only two values a well-formed run can carry.

## 6. What a single masked RMW to this register cannot do

Stated because a first store to a second device is exactly where an unstated risk lives — 773 §6's list,
carried here because this is the build that realises it:

- **cannot reach any other register** — one word, no indexed or block access;
- **cannot reach another pad bank** — `0x2044` is not SDC2's `0x2048`, and the seven masks are disjoint,
  so their union is their arithmetic sum (rung 25's own `_Static_assert`, and 776 adds
  `ST_TLMM_SDC1_ALL_MASK == 0x0001FFFF`);
- **cannot change a FUNCTION** — every field of that register is drive strength or pull; 769 §2
  established by register layout that there is no function-select field to write;
- **does not put the bus at risk** — no transfer is in flight at that moment;
- **does not touch the PMIC, the rails or any clock** — acts 1 and 3 are the two 773 says are unreachable,
  and this arm does not reach them;
- **is not the DLL path and not the clock register**, so 764 §1 and 768 §4 stay closed.

## 7. And it does not say the pads are the cause

A register already holding `0x9F24` is the **more likely** row — a board that boots from this eMMC to
`fastboot` has had *something* drive that bus successfully. And a register that now holds it does not say
the card will answer: `PULL SDC1_CMD` is **necessary and not sufficient**. 768 §6's caveat stands — the
idle line reads HIGH on every arm on record, which is what a pull-up produces *and* what a floating line
that happens to sit high looks like.

## 8. A cost already on record is paid in this same build

771 §7 refuted the **737 window paragraph** in `src/entry/entry_storage.c` and left it standing for a
**cost** reason and not a lane reason: correcting a comment forces a build, and the next build should be
an arm that carries the correction rather than a comment that spends one. **This is that build.** The
paragraph's rationale *as a prediction* — *a CMD2 sent the way CMD1 was would answer the same way* — is
withdrawn in place, with 771's measurement named beside it: CMD2 **was** sent inside that window and
gained nothing, because `_cid_inhibit_seen = 0` over all 1024 samples, i.e. the block never raised
`CMD_INHIBIT` and the window was open above a command that was never issued. The window itself stays, and
the corrected paragraph says why (one read-modify-write pair, still below the gate, and it removes a
competing explanation rather than making a claim about CMD2's outcome).

## 9. Two defects this step hit, recorded because a diff does not show either

**A backtick inside a `layout_fail` message is a command substitution (m779).** The guard clause's prose
said *the two shapes a compiled `if` takes here*, and the refusal printed *the two shapes a compiled
takes here*: the shell ran the `` `if` `` substitution, took its empty answer, and spliced the emptiness
into the sentence. The build did not fail. `make check`'s `check_backtick_messages` refused it —
**"2 unescaped backtick(s) inside a double-quoted string"** — which is the check doing exactly what it
was written for: a refusal that fires with a hole in its explanation is worse than no refusal. The class
is the same shape as m778 one step over: a defect in a *message*, invisible in a diff and in every
artifact hash, whose only readers are the compiler (m778) and this check (m779).

**And the same class has a second cost, which this step paid.** Fixing that message edits
`src/entry/build_entry.sh`, and the entry image's `xnu_arm_entry-sources.txt` records **every regular file
in `src/entry/`, by content** — so the record went stale while the *artifact* did not move at all. The
entry was rebuilt and came out **`d5d98738…` a third time**: the arm is reproducible byte for byte across
three builds, and the only thing that moved was the builder's own recorded hash. The payload was rebuilt
and every payload artifact was likewise unchanged (`93026ca1…`, `babe9364…`, `092f94d4…`), so the park
differs from its first recording in exactly one member.

## 10. Verification

| gate | reading |
| --- | --- |
| entry build | exit 0, `xnu_arm_entry.bin` `d5d98738…`, 5552764 B, `STAGE90_XNU_STORAGE_PROBE=27` — **rebuilt three times, same hash each time**, so the arm is reproducible byte for byte |
| the guard clause | `[fd512044:ldr fd512044:str ]`, counts `[fd512044:ldr=2 fd512044:str=1 ]`, order `[fd512044:ldr fd512044:str ]`, EMPTY image side, compiled as `SPANNED-BY(8000ecdc)` |
| the source clause | exactly one guard at line 3565 and one store at line 3571, in the guard's else arm |
| containment | value-26 build from the same source → `8096cb2c…`, **byte-identical** to the parked rung-27 arm, and `cmp` against its member |
| payload build | exit 0, `stage90-qcdt.img` `93026ca1…`, switch record `6c2b6038…`, **byte-identical to rungs 23/24/25/26/27** |
| the park | `out/stage90/frozen/armed-storage-d5d98738`, 11 members, `tools/verify_revert_set.sh` **11 ok / 0 failed** |
| `make check` | exit 0 — 37 sets |
| readiness | **5 of 5**, exit 0 |

**Owed, unchanged and named rather than left to be inferred:** the `rung_para` correction for values 12..23
(**partly paid here**: `tools/verify_press_ready.sh`'s shared storage paragraph carried rung 1's
`_loads=6`/`_writes=0` sentence with the correction attached only inside a `2..11` guard, so every arm from
12 up carried it uncorrected and three `rung_para` calls had to disclaim it in their own words; the guard
now has an `else` and the correction travels with the sentence for **every** value); the seam-address class
itself — *a kernel address pinned in an entry source* — whose repair is a link order;
`run_and_capture.sh`'s `EXIT_POP_LR_LITERAL` structural repair.

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist and 「让os可以正常启动并且挂载存储」 is
not reached, so **TWRP-to-storage stays withheld.**
