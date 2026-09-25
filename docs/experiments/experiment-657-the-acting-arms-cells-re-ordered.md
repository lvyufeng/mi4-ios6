# 657: the acting arm's three cells, re-ordered — and which one predicts the frontier moves

653 pre-registered the acting arm's readings as **`a1 ≠ b1` → `CHANGED`** (the row that selects 597's
candidate (A)) and **`a1 == rtcpre_pop` → `STALE LINE, WRITTEN OUT`**, marked *unreachable*. 656 withdrew
the unreachability. This step reads the acting arm's own instruction stream and the seam's own code order
and finds a second defect in that table: **on this arm the two rows are inverted.** An unequal pair will
read as `STALE LINE, WRITTEN OUT` — the cell 653 retired — and `CHANGED` is the *least* likely of the
three. §4.2 then measures the object 571 named as the frontier's next question — the four instructions
between `SCTLR.C` and the `pop` — and finds that the distance between them carries no maintenance and
cannot touch the slot's line (§5 states what that does and does not restore to the prediction).

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
pair by construction. **The frozen arm's own bytes say the same thing in the same order** — `ldr r9,[r5]`
/ `ldr r8,[r5,#4]` at `0x8047ca6c`/`0x8047ca70` precede `str r7,[r5]` / `str r6,[r5,#4]` at
`0x8047ca74`/`0x8047ca78` — so this is a property of the built arm and not only of the source (§4.2).

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

**The object 571 named is four instructions:** between the re-enable
(`0x80046320-24`) and the `pop` (`0x8004633c`) sit `mrc p15,0,r0,c13,c0,{4}` (the per-CPU thread
register, **not** `cpu_data` — §4.2 measures it), `mov r1,#1`, `ldr r0,[r0,#1484]` and
`str r1,[r0,#304]` — **two of them cacheable memory accesses taken
after `SCTLR.C` is set and before the `pop`**. Which of the two readings holds is exactly the question of
whether those accesses can put a stale copy of the slot's line back in front of the `pop`. Both outcomes
are pre-registered here so that neither one arrives as a surprise, and 571's is named first because it is
the older registration on the same arm. **§4.2 then reads that object out of the frozen ELF, and the
question turns out not to need the run.**

### 4.2 The four instructions, read out of the frozen arm — and the distance is not a mechanism

§4.1 ends by naming the four instructions between the re-enable and the `pop` as "the obvious next
object" and leaving the question to the run. **The object is host-side readable, so it was read.** From
`out/stage90/xnu_arm_entry.elf` (`a43304f2…`, sha256 `f4ef57fc…`), the exit's tail verbatim:

```
80046320  orr   r0, r0, #4            ; SCTLR.C = 1
80046324  mcr   15, 0, r0, cr1, cr0, {0}
80046328  isb   sy
8004632c  mrc   15, 0, r0, cr13, cr0, {4}   ; r0 = TPIDRPRW
80046330  mov   r1, #1
80046334  ldr   r0, [r0, #1484]             ; r0 = *(TPIDRPRW + 0x5cc)
80046338  str   r1, [r0, #304]              ; *(r0 + 0x130) = 1
8004633c  pop   {fp, pc}
```

Four facts, each a reading and not an inference:

* **There is no cache maintenance among the four.** `mrc`, `mov`, `ldr`, `str` — no `cr7` write of any
  kind. A mechanism that "does not survive the distance" has to live somewhere in the distance, and
  nothing there maintains a cache.
* **The two memory accesses are computable, and neither is the slot's line.** `TPIDRPRW` is published by
  this very arm — `xnu_live_pce_tpidrprw=0xc0558df0` in the 574 capture — so the load is at
  `0xc05593bc`; and the *loaded value* is pinned by the run's own register dump, where `r0 = 0x8051a000`
  at the abort (the abort is taken at the `pop`, after the `str`, so `r0` is the load's result — and
  `0x8051a000` is `cpu_data`, the same value `xnu_live_slot_rtcpre_datap` carries). So the store is at
  **`0x8051a130`**. The seam's slot line is `0x8054fec0`–`0x8054feff`:

  | access | address | its 64-byte line | L1 index (64 sets, 64 B lines) |
  | --- | --- | --- | --- |
  | the seam's slot (the word the `pop` loads) | `0x8054fec8` / `0x8054fecc` | `0x8054fec0`–`0x8054feff` | 59 |
  | the `ldr` | `0xc05593bc` | `0xc0559380`–`0xc05593bf` | — different line |
  | the `str` | `0x8051a130` | `0x8051a100`–`0x8051a13f` | 4 |

* **Neither access could put a stale copy of the slot's line in front of the `pop` even if it were in
  the same set.** A cacheable **load** can only fill a line *from the memory hierarchy*; by the time
  these two run, the seam's restore has already written both words back to DRAM with `C` clear and
  `dsb`'d them, so a fill is a fill of the repaired words. A cacheable **store** allocates and then
  writes `1` — and the store's data is `r1 = 1`, which is not the value the `pop` consumed
  (`0x05006e74`), nor even its shape. So the only thing the distance could reintroduce is the repaired
  value.
* **The four are bookkeeping, not repair.** `*(cpu_data + 0x130) = 1` after reading `cpu_data` out of
  the per-CPU structure is the idle path flagging state on its way out — the same shape the seam's own
  note calls out for the coherency-domain exit. It moves no frame word and maintains no cache.

**What this does to §4.1's two competing readings, and it is asymmetric.** 571's mechanism was *"the
invalidate did not survive the distance to the `pop`"*; the distance contains nothing that could undo
it, so **the distance is out as that mechanism.** What is *not* out is the question 571's mechanism was
reaching for, and 652 already opened it: **whether the word the `pop` consumes is the word the seam
invalidated.** If the pop's word is served from a *different* level — the L2, which the set/way form
that Apple's `FlushPoU_Dcache` uses at the seam does not necessarily reach, and which is exactly the
600-series "one cache level off" account — then an L1-and-PoC invalidate at the slot is the right
address and the wrong level. That candidate is *named and different* from the four instructions.

**And the operation's two halves are one instruction, which makes the pair stronger than §3 claimed.**
`FlushPoC_DcacheRegion`'s loop contains exactly **one** maintenance instruction —
`mcr 15, 0, r0, cr7, cr14, {1}`, `DCCIMVAC`, *clean **and** invalidate by MVA* — and the built call is
`FlushPoC_DcacheRegion(slot, 8u)`, whose loop arithmetic (`and r2,r0,#63`; `bic r0,r0,#63`;
`r1 = (8 + 8 - 1) >> 6 = 0`) makes that single instruction execute **once, at `0x8054fec0`**, the line
containing the slot. So the cell that reads `a1 == rtcpre_pop` — the clean having written the dirty
line out — is at the same moment the cell that says **the invalidate ran on that same line**, because
there is no clean-only path to read from. §3 argued the frontier then moves from the *restore*; the
sharper argument is from the **instruction's identity**, and it did not need the restore at all.

**But the pair cannot say which case it is in, and this is the correction to make before the run.**

| the pair in this run | the reader prints | what it does **not** establish |
| --- | --- | --- |
| `a1 == rtcpre_pop` | `STALE LINE, WRITTEN OUT` | — this one *does* establish the invalidate (§ above), hence a `pop` that misses at L1 |
| `a1 == b1` | `CLEAN LINE` | **two readings, and they differ in outcome.** Either the operation was inert, **or it ran on a line that was already clean** — in which case there was nothing to write out, the invalidate still happened, and the boot should still get past the `pop`. 653's cell label ("the operation did nothing") is only one of the two |
| `a1` neither | `CHANGED` | the word at that offset is not this boot's deadline; it is a number to compare by hand |

So the **frontier reading beside the pair is what separates `CLEAN LINE`'s two readings**, and the four
combinations are pre-registered here rather than discovered after the run:

| pair | frontier (`slot_post_calls`, `poll_seq`) | the joint reading |
| --- | --- | --- |
| `STALE LINE, WRITTEN OUT` | past the `pop` | the mechanism is confirmed **and repaired**: the dirty stale line was written out and invalidated by one instruction, and the `pop` missed |
| `STALE LINE, WRITTEN OUT` | still dies at the `pop` | the clean reached the line but the `pop`'s word came from below it: **the L2 candidate**, now the first object to look at |
| `CLEAN LINE` | past the `pop` | the line was clean; there was nothing to write out; **the invalidate was the repair** — `CLEAN LINE` would have been misread as failure |
| `CLEAN LINE` | still dies at the `pop` | the operation was inert: 638 §3's second row again, and candidate (A) as built does not act |
| `CHANGED` | either | a word at that offset that is neither — compare it by hand against the death's `pc`/`r11` |

## 5. What this changes, and what it does not

* **It corrects the pre-registration before the run rather than after it.** 653's table would have had a
  reader score `STALE LINE, WRITTEN OUT` as an unregistered surprise — when it is the expected outcome —
  and read `CHANGED`'s absence as the arm failing to reach the line.
* **Its first draft claimed the frontier moves; §4.1 withdrew that on 571's older registration; §4.2
  puts a narrower claim back, and the difference matters.** 571 registered the pessimistic consequence
  for the same pair, and the honest response to two competing registrations was to pre-register both —
  which §4.1 did. §4.2 then *measured 571's named object*: the four instructions carry no maintenance
  and cannot touch the slot's line, so the distance is not a mechanism for undoing the invalidate. The
  claim restored is therefore not §3's ("the restore repaired DRAM") but the instruction's identity: a
  clean write-back and an invalidate are the *same* `mcr`, so the cell that shows the write-back shows
  the invalidate — **and only in that cell**. `CLEAN LINE` stays ambiguous, which is why the joint table
  in §4.2 is the pre-registration and not a single predicted cell.
* **It does not weaken the falsifier — it names its second reading.** `a1 == b1` still covers "the
  operation did nothing", and it now also covers "the line was already clean, the operation ran, and the
  repair worked". Reading the cell alone as failure would be this project's oldest defect in a new
  place; the frontier reading beside it is the discriminator, and both rows are registered above. The
  first of the two is still the *sharper* statement 656 made: the same routine Apple calls at the seam
  wrote nothing with `C` clear, so candidate (A) as built would be inert for a reason that has nothing to
  do with the level arithmetic.
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
652 capture — §4.2's addresses come from that capture's register dump and from the arm's own published
keys (`xnu_live_pce_tpidrprw`, `xnu_live_slot_rtcpre_datap`), read with the same extractor the reader
uses. The only files changed are this document, a corrigendum in `experiment-653`, the index row
and memory — **no source, no image, no byte of `out/`**, and deliberately so: a press watcher is armed
(`/tmp/r654/press-on-clear.sh`, waiting for the neighbour `33e80afe` to leave the bus), and an armed
watcher fires the runner by path. Every address in §4.2 was read from the frozen ELF, not recomputed
from source, so the numbers are the built arm's. `fastboot boot` only, never `flash`.
