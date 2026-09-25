# 657: the acting arm's three cells, re-ordered — and which one predicts the frontier moves

653 pre-registered the acting arm's readings as **`a1 ≠ b1` → `CHANGED`** (the row that selects 597's
candidate (A)) and **`a1 == rtcpre_pop` → `STALE LINE, WRITTEN OUT`**, marked *unreachable*. 656 withdrew
the unreachability. This step reads the acting arm's own instruction stream and the seam's own code order
and finds a second defect in that table: **on this arm the two rows are inverted.** An unequal pair will
read as `STALE LINE, WRITTEN OUT` — the cell 653 retired — and `CHANGED` is the *least* likely of the
three. It also identifies which of the cells predicts that the boot gets past the `pop`.

All of it is read out of the frozen acting arm (`out/stage90/xnu_arm_entry.elf`, entry `a43304f2…`) and
out of `entry_trace.c`. Nothing was built, nothing was run, and no file the press fires was touched.

## 1. The seam's own order: the `a`-reads come **before** the restore

`entry_trace.c:2254-2300`, read in order:

| # | statement | cache state |
| --- | --- | --- |
| 1 | `b0 = *(slot); b1 = *(slot+4)` | `C` clear → DRAM |
| 2 | `__real_FlushPoU_Dcache()` | Apple's own `cleanflush` |
| 3 | `FlushPoC_DcacheRegion(slot, 8u)` | the operation, `C` clear |
| 4 | `dsb sy` | |
| 5 | **`a0 = *(slot); a1 = *(slot+4)`** | `C` clear → DRAM |
| 6 | **`*(slot) = b0; *(slot+4) = b1`** — the restore | `C` clear → DRAM |
| 7 | `dsb sy`, `entry_sctlr()`, publish | |

**The `a`-reads are step 5 and the restore is step 6**, so the published pair is a reading of what the
*operation* did to DRAM, taken before anything repairs it. That ordering is what the rest of this step
turns on, and it is worth stating because the opposite order would have made every arm publish an equal
pair by construction.

## 2. What the operation is, in this arm's own bytes

`entry_seam_flush` (`0x8047c9c4`) calls the operation at `0x8047ca64`:

```
8047ca58:  bl  80045874 <FlushPoU_Dcache>        <- Apple's own, the routine the seam intercepts
8047ca64:  bl  8004589c <FlushPoC_DcacheRegion>  <- the operation, POC=1 only
```

and `FlushPoC_DcacheRegion` is a **64-byte-line walk over the address range** (`and r2, r0, #63` /
`bic r0, r0, #63` / `lsr r1, r1, #6`) whose maintenance instruction is at `fmdr_loop`, `0x80045710`:

```
80045710:  ee070f3e   mcr  15, 0, r0, cr7, cr14, {1}
```

which is the MVA form — *clean & invalidate D-cache line by MVA* — the same encoding 638 §1's table lists
for the POC routine. `FlushPoC_DcacheRegion(slot, 8u)` therefore covers **one whole line** containing
`slot`, hence both `slot` and `slot+4` at once.

## 3. The line is dirty, and the dead run already measured its contents

The arm that ran (`574`, `SEAM_MEASURE=1`) left both sides of one load in the log:

| | |
| --- | --- |
| `b1` — `slot+4` read **non-cacheably** | `0x8047c990` |
| the `pop`'s load of `pc`, **cacheably**, from the same address | `0x05006e74` |

So the address holds `lr` in DRAM and the loop's own deadline (`rtcpre_pop`) in the cache line, which is
597's stale line stated as two values with both of them measured. **A clean writes the whole line to
DRAM**, so a working operation would put that line's contents — the deadline — into DRAM at `slot+4`, and
the step-5 read would then see it:

> `a1 == (that boot's own xnu_live_slot_rtcpre_pop)`

## 4. The reader's cell order makes that sentence `STALE LINE, WRITTEN OUT`, not `CHANGED`

`run_and_capture.sh:2069-2088`, the `seam_op == 0x1` branch, in order:

| test | cell |
| --- | --- |
| `a0 == b0` **and** `a1 == b1` | `CLEAN LINE` |
| **elif** `a1 == rtcpre_pop` | `STALE LINE, WRITTEN OUT` |
| else | `CHANGED` |

The first cell requires **both** words equal, so an unequal pair never reaches it; and the second test is
an exact equality on `a1`. **`a1 == rtcpre_pop` is therefore matched before `CHANGED` can be**, and §3 says
that is the value a working operation writes. So the row that fires is the one 653 filed as unreachable,
and the row it filed as the (A)-selecting one needs `a1` to be *neither* the deadline *nor* the pushed
`lr` — the least likely of the three.

**So the corrected table for this arm is:**

| the pair in this run | the reader prints | what it establishes |
| --- | --- | --- |
| `a1 == rtcpre_pop` | **`STALE LINE, WRITTEN OUT`** | **the operation acted with `C` clear.** The clean wrote the dirty line — the idle loop's own deadline — into the frame's word in DRAM, i.e. the line was stale exactly as 597 said and an MVA maintenance operation *does* reach it with the cache disabled (638 §5's open question, answered "reachable") |
| `a1 == b1` | `CLEAN LINE` | **the operation was inert.** With `C` clear Apple's MVA clean-and-invalidate wrote nothing, the line stayed stale, and the `pop` fetches the deadline again — the falsifier, and 638 §3's second row for a second time |
| `a1` is neither | `CHANGED` | the line's word at that offset is not this boot's deadline: the value is the line's pre-`pop` content and is the number to compare by hand against the death's `pc`/`r11`. Informative, but **not** the cell 653 aimed at |

### 4.1 This is not a new prediction — 571 registered it, and 571 registered the *opposite* consequence

**571 is the step that built this arm, and its pre-registration already contains §3 and §4's first row.**
Its per-key table reads:

> `xnu_live_seam_a1` | **= this pass's `xnu_live_slot_rtcpre_pop`** (`0x33f1c1b5` on 568's run) | **546 section 3's
> mechanism seen before the `pop`**: the dirty stale line's copy of the slot's `lr` word is the deadline the exit
> published, written out by the clean half

and its rehearsal table records variant **A** — *"the predicted reading (`b1` a text address,
`a1 == rtcpre_pop`, `sp` = the abort's sp)"* → **`STALE LINE, WRITTEN OUT`** — as measured **"as
designed"** on 568's real log. So the cell has been known to be reachable since 571, one day before 653
called it unreachable; 656's fixture is a re-measurement, not the discovery. **What is new here is only
the mapping**: 653 filed the unequal pair under `CHANGED` when the reader's order sends it to this row.

**And 571 registered a different consequence for it, which §3/§4's last paragraph must not overwrite.**
Its frontier table:

> dies at the `pop` (`sleh_lr == 0x800462dc`), and the pair came back **changed** to the `rtcpre_pop` value |
> the mechanism is confirmed and the repair is **incomplete**: memory was restored and the line discarded, so a
> `pop` that still reads a stale word says **the invalidate did not survive the distance to the `pop`** (the four
> instructions between Apple's re-enable and the `pop` are the obvious next object)

**So there are two competing predictions for the same pair, and the pair does not decide between them:**

* **656/657's reading (§3):** the restore rewrote both words in DRAM and the line was just invalidated, so the
  `pop` — the first cacheable load after the re-enable — misses and fetches the repaired DRAM ⇒ the pass
  survives, `slot_post_calls` is published, the frontier moves.
* **571's reading:** the invalidate **does not survive the distance** to the `pop`, so the `pop` still reads a
  stale word ⇒ the mechanism is confirmed and the repair is incomplete.

**The object 571 named is now measured, and it is four instructions:** between the re-enable
(`0x80046320-24`) and the `pop` (`0x8004633c`) sit `mrc p15,0,r0,c13,c0,{4}` (the per-CPU base),
`mov r1,#1`, `ldr r0,[r0,#1484]` and `str r1,[r0,#304]` — **two of them cacheable memory accesses taken
after `SCTLR.C` is set and before the `pop`**. Which of the two readings holds is exactly the question of
whether those accesses can put a stale copy of the slot's line back in front of the `pop`, and this
document does not have the answer: **the run does.** Both outcomes are pre-registered here so that neither
one arrives as a surprise, and 571's is named first because it is the older registration on the same arm.

## 5. What this changes, and what it does not

* **It corrects the pre-registration before the run rather than after it.** 653's table would have had a
  reader score `STALE LINE, WRITTEN OUT` as an unregistered surprise — when it is the expected outcome —
  and read `CHANGED`'s absence as the arm failing to reach the line.
* **It does not claim the frontier moves, and its first draft did.** §4.1 is the correction: the pair
  confirms the *mechanism*, and whether the boot gets past the `pop` is a separate question about the four
  cacheable instructions between the re-enable and the `pop`, on which 571 registered the pessimistic
  answer. The honest pre-registration is two readings and a run that decides between them.
* **It does not weaken the falsifier.** `a1 == b1` still means the operation did nothing, and it is now a
  *sharper* statement than before: the same routine Apple calls at the seam wrote nothing with `C` clear,
  so candidate (A) as built would be inert for a reason that has nothing to do with the level arithmetic.
* **It does not measure anything about the coming boot.** §3's contents are the *previous* boot's, and this
  step's prediction uses that boot's deadline value; a new boot publishes its own, and the reader compares
  against the log in hand rather than against `0x05006e74`. The prediction is falsifiable in one line of
  the coming log either way.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** It makes the one press that can
  advance it read correctly. `poll_seq` still stops at 2 and `slot_post_calls` is still absent.
* **It does not edit the runner, and the runner needs no edit.** The three cells and their order are
  right; only the record's mapping of the pair to the cells was wrong. That is the same conclusion 656
  reached about the same reader, one cell over.

## 6. Safety

No device action: no `fastboot`, no `adb`, nothing sent anywhere, **nothing written to storage**. Every
reading is `arm-none-eabi-objdump`/`nm` on the frozen acting-arm ELF, `entry_trace.c`, or the archived
652 capture. The only files changed are this document, a corrigendum in `experiment-653`, the index row
and memory — **no source, no image, no byte of `out/`**, and deliberately so: a press watcher is armed
(`/tmp/r654/press-on-clear.sh`, waiting for the neighbour `33e80afe` to leave the bus), and an armed
watcher fires the runner by path. The arm still verifies against the `armed-seam-poc-a43304f2` park.
`fastboot boot` only, never `flash`.
