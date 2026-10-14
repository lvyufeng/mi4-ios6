# 1006 — evidence already present in the 1005 capture

READ-ONLY extraction from the 1005 run's recovered log. No source, script, or device was touched.

**Capture chosen:** `out/stage90/captures/recovered-unknown-arm-20261014-020404-last_kmsg.txt`
(1,396,170 B, 29,253 lines, sha256 `245417438a00d7e957f2c11b3ed7eae5312cee32b395a3da155965d706a6f9fa`).

**Why this file:** it is the only capture in the tree that carries `xnu_live_tick_sum_maxd=0x1be8bc01`
(the 1005 stamp), it carries all four cadence keys (`xnu_live_cadence_t/sync/tick/irq`, 106 tokens =
30 rows), and it carries the tick burst. `records/1005-press-read.txt:3` names it as *the* 1005 capture,
and `tools/check_1005_capture.py` on it returns `CADENCE CERTIFIED, 0 HARD ANOMALIES, EXIT=0`. The 1005
image itself is preserved at `out/stage90/frozen/armed-d13-ff937225/xnu_arm_entry.elf` (used below for
disassembly where a record's meaning needed disambiguating).

Authority for key meanings: `src/platform/darwin13/pe_msm8974.c:611-613` (`xnu_live_tick_sum_*`),
`src/entry/entry_stubs.c:2520-2540` (`entry_live_write`, the CAPPED channel),
`src/entry/entry_stubs.c:2627` (`entry_write_kv`, the DIRECT/uncapped channel),
`src/entry/entry_stubs.c:5296-5318` (cadence), `src/entry/entry_stubs.c:2722` (stall sampler).

**Two record channels, and this distinction is load-bearing for everything below.**
`entry_live_write()` counts against `ENTRY_LIVE_CAP = 24576` (`entry_stubs.c:2341`) and, once full,
suppresses every further record after writing one tombstone `xnu_live_capped`. `entry_write_kv()` is
uncapped and always appends. Which key rides which channel:

| key family | channel | consequence |
|---|---|---|
| `xnu_live_tick_irq`, `xnu_live_tick_ticks/hz/arm_*` | `entry_live_write` (CAPPED) | stops after the cap |
| `xnu_live_stall_*`, `xnu_live_tick_sum_*`, `xnu_live_cadence_*`, `xnu_live_usvc_*`, `xnu_live_wdt_pet_*`, `xnu_live_tick_anom_*` | `entry_write_kv` (DIRECT) | keep publishing past the cap |

The cap filled mid-run: `xnu_live_capped=0x00006000` (24576) at line **29072**, and no capped-channel
key is published after that point. This is why `xnu_live_tick_irq` (a capped key) stops at 128 while
`xnu_live_stall_n` (a direct key) keeps going to 15.

---

## 1. THE FREEZE INTERVAL

`xnu_live_tick_sum_maxd` is `g_msm8974_tick_max_d`, a **latched maximum**: at every timer delivery the
handler computes `d = CNTVCT.lo_now − CNTVCT.lo_at_last_arm` and keeps the largest ever seen
(`pe_msm8974.c:456-459`). It is a *max*, not a running gap, and it is never reset.

**Every publication, by value** (`entry_tick_summary`, called from the exec-done point and from the
spare pet gate):

| line | count | anom | maxd | maxd (ticks) | ~seconds @19.2 MHz |
|---|---|---|---|---|---|
| 8087 | — | — | `0x003057de` | 3,168,222 | ~0.165 s |
| 9584 | — | — | `0x003057de` | — | — |
| 12509 | — | — | `0x003057de` | — | — |
| 16493 | 0x78 (120) | 0x08 | `0x02f3e020` | 49,537,056 | ~2.58 s |
| 20192 | 0x78 | 0x08 | `0x02f3e020` | — | — |
| 21749 | 0x78 | 0x08 | `0x02f3e020` | — | — |
| 28857 | 0x81 (129) | 0x0e | `0x04ba1ff0` | 79,306,736 | ~4.13 s |
| 29081 | 0x81 | 0x0e | `0x04ba1ff0` | — | — |
| 29123 | 0x87 (135) | 0x14 | **`0x1be8bc01`** | 468,237,313 | **~24.39 s** |
| 29135 | 0x8b (139) | 0x18 | `0x1be8bc01` | — | — |
| 29147 | 0x93 (147) | 0x20 | `0x1be8bc01` | — | — |
| 29159 | 0xb1 (177) | 0x3e | `0x1be8bc01` | — | — |
| 29178 | 0x10d (269) | 0x9a | `0x1be8bc01` | — | — |
| 29190 | 0x1dd (477) | 0x16a | `0x1be8bc01` | — | — |

So `maxd` is **republished 14 times, but its value changed only four times** (→ 0x4ba1ff0 → pegged
0x1be8bc01). The ~24.4 s value is **one single worst gap** — a latched maximum can only ever name one
interval per distinct value.

**Is it "a single interval"?** `maxd` alone cannot say how many gaps there were — but `anom`
(`g_msm8974_tick_anom_count`, incremented per delivery whose `d > 2·period`, `pe_msm8974.c:462`) does.
At the last summary `anom=0x16a (362)`: **362 deliveries were individually late**, so the freeze is not
one delivery sitting in the gap — it is a window in which many deliveries each overshot, with the worst
single one at 24.39 s. The exact count of late deliveries beyond 362 is not recoverable (the counter's
last read is the last pet snapshot; see §2/§6).

**What is the LAST inter-tick gap, versus the max?** Not directly recorded. The last *raw* per-delivery
gap is not printed past the cnt-4 `xnu_live_tick_anom_d` ladder, whose last value is
`0x0002e527` (189,735 ticks, ~9.9 ms) at line 4689 — that is a **pre-freeze** healthy-ish reading, not
the tail. `maxd` stayed pegged at 24.39 s to the end; the per-delivery tail gaps during the burst are
not in the capture. **Stated plainly: the capture does not contain the last inter-tick gap by value.**

---

## 2. THE TICK BURST — and a correction to the 1005 read

The tick counter is `g_msm8974_tick_count`. **The 1005 read's "135 → 479" are PET SNAPSHOTS, not the
terminal count.** By value:

`xnu_live_tick_sum_count` (last pet snapshots): 0x81(129) @29079 → 0x87(135) @29123 → 0x8b(139)
@29135 → 0x93(147) @29147 → 0xb1(177) @29159 → 0x10d(269) @29178 → 0x1dd(477) @29190.

But the **uncapped** stall ladder (`xnu_live_stall_n`, one sample per tick-ladder doubling) reaches
`0x0000000f (15)` — the tick counter passed **2^14 = 16384**. Proof that the stall ladder *is* the tick
doubling ladder: in the 1005 image the call site is `msm8974_handle_interrupt` and the test guarding it
is the *same* `(count+1) & count == 0` test that guards `xnu_live_tick_irq` (disassembly of
`out/stage90/frozen/armed-d13-ff937225/xnu_arm_entry.elf` at `0x801ae548-0x801ae56c`: `tst r1,r2` on
`g_msm8974_tick_count` at `0x8084dc24`, both `entry_live_write` and `entry_note_stall_sample` on the
zero branch). The stall sampler fires **on every tick doubling**, and it is uncapped.

The two ladders reconcile only one way:
- `xnu_live_tick_irq` (capped) shows doublings 1,2,4,**8,16,32,64,128** — its last record
  `xnu_live_tick_irq=0x00000080` (128) @27879 is *before* the cap filled @29072.
- `xnu_live_stall_n` (uncapped) shows doublings 1…2^14 = **15 samples**, samples 9–15 (@29166, 29194,
  29217, 29224, 29231, 29238, 29245) being doublings 256,512,1024,2048,4096,8192,16384, all *after* the
  cap, so their `tick_irq` twin was swallowed.

**Reading:** the burst is a real catch-up, and it is far larger than the 344 the 1005 read inferred.
The counter climbed **from ≤135 to ≥16384** in the terminal records — the rate between pet snapshots
accelerates (135→139 over pet 3–4; 177→269 over pet 16–32; 269→477 over pet 32–64), which is the shape
of a catch-up draining a backlog, not a fresh steady cadence. `tick_sum_count=477` is just the last
**pet** snapshot; the tick had already passed 16384 by the last stall sample. (A ~24.4 s freeze at a
1 ms period would accumulate ~24,400 missed periods — a burst reaching ≥16384 is the right order.)

**Verdict for Q2:** catch-up of the missed expirations, not a fresh cadence — but the *size* is
"≥16384 ticks", **not** the "~344" the read reported. Answering it exactly needs the final
`tick_sum_count` after the last stall sample, which the capture does not contain (no pet snapshot fires
after @29190).

---

## 3. THE SYSCALL GAP

`xnu_live_cadence_sync` is `g_cadence_sync`, set from the USVC counter (`entry_stubs.c:5412`); it only
increments when an SVC reaches `fleh_swi`.

- Sampled window (healthy): `xnu_live_cadence_t` 1→16, `sync` 1→16, `tick` 0x7a→0x81 (122→129).
  (The coarse window verdict "NO COLLAPSE IN THE WINDOW" is correct for this early window.)
- Then **`_sync` goes FLAT at `0x00000014 (20)`** across every terminal summary:
  @29124, 29136, 29148, 29160, 29179, 29191 — six triples all `sync=0x14(20)` — while `tick` climbs
  0x87→0x1dd and `irq` climbs 0x8a→0x1e0.
- The USVC frontier (`xnu_live_usvc_n`, direct) stops at **`0x00000016 (22)`** (@29209). Calls 18, 19,
  20 @29088-29114; 21 @29201; 22 @29209. Last `_num=0xfffffffd` (−3 = `mach_absolute_time`),
  `_pid=1`, `_thr=0xc0bfa000`.

**So yes: `_sync` is flat (frozen at 20) across the entire burst window, then the SVC frontier jumping
16→20→22 lands only *after* the burst has begun.** The 1005 claim — "the pid-1 SVC path froze with the
tick" — is **supported**: `sync` is unchanged while `tick`/`irq` climb by hundreds. (Whether `sync` was
frozen *before* the burst started is not pinned to a record — the last pre-burst sample is 16 @28062 and
the first post-burst summary is 20 @29124; the counter's values between are unknown.)

---

## 4. STORAGE / PET CADENCE

**`xnu_live_storage_rd_ordinal`** reaches `0x00000207 (519)` @29054, on **LBA
`xnu_live_storage_rd_lba=0x004a3830`**. The read-blocks span the terminal records: ordinal 0x1e7–0x207
(@28133–29054) all read LBA `0x004a3810`…`0x004a3830`. This is the "media" region of the root
(`rootmedia_card_off` block-sizes appear up to `0x0455d000` here). Every read is clean:
`xnu_live_storage_rd_complete=0x1`, `_err=0x0`, `_timeout=0x0`.

**`xnu_live_wdt_pet_n`** (the published feed ladder, gated to powers of two of the pet ordinal) and
`xnu_live_wdt_pet_calls` (the caller's own pass count):

| pet_n (@line) | pet_calls | wdt_countdown | wdt_sts |
|---|---|---|---|
| 1 (@29073) | 592 | 0x0004a297 | 0x0009452e |
| 2 (@29085) | 1268 | 0x00074120 | — |
| 3 (@29115) | 1944 | 0x00011d75 | 0x00023aea |
| 4 (@29127) | 2619 | 0x00015a79 | 0x0002b4f2 |
| 8 (@29139) | 5322 | 0x0001c81d | 0x0003903a |
| 16 (@29151) | 10730 | 0x0002a393 | 0x00054726 |
| 32 (@29163) | 21546 | 0x00045ae9 | 0x0008b5d2 |
| 64 (@29182) | 43178 | 0x000083c0 | 0x00010780 |

The **feed record `_pet`/`_calls`/`_countdown`/`_sts` are also DIRECT** (uncapped); only the *gate*
condition (`calls & (calls−1) == 0`) samples them. The `_countdown` holds at ~475,000 and `_calls`
climbs from 592 to 43,178 — the idle/feed path ran continuously. But because the gate publishes only at
power-of-two *ordinals* (1,2,3,4,8,16,32,64 — **only 8 endpoints**), the samples are too sparse to
sandwich the ~24.4 s freeze to within an order of magnitude.

**Direct answer:** storage and pet do advance *through the tail* — by the time they publish, the burst
is underway (`pet_n=1` @29073 is already after the burst's early summaries `tick=129` @29079). The
capture **cannot** show whether the ordinal/pet moved *during* the 24.4 s freeze itself: there is no
record timestamped inside the freeze, and the free-running counter that could date them is not published
with these keys. This is exactly the question 1006's `xnu_live_frzbase_*`/`xnu_live_freezegic` witness is
built to answer. **Stated plainly: Q4 is not answerable from this capture.**

---

## 5. INTERRUPT / GIC EVIDENCE ALREADY PRESENT (predating 1006)

Keys present that reflect GIC/IRQ/CPSR state:

- **`xnu_live_irq_line_*`** — 3 sets (2 config sets @4118–4137 and one more; only 3 `_seq` records).
  All are **pre-freeze** GIC distributor config: `_intid=0x14`, `_isen_before=0x00007fff`,
  `_isen_after=0x00107fff` (bit 20 = the timer PPI enabled), `_pend_before=0x20400000`,
  `_group_before/after=0x0`, `_dist_ctlr=0x1`, `_dist_pmr=0xf0`, `_rc=0x1`. **No post-freeze
  `irq_line` record exists.**
- **`xnu_live_gicdrv_*`** — one set @15721–15766, all **pre-freeze**: `_en_before=_en_after=0x00107fff`,
  `_pend_before=0x20500000`, `_isr=0x801ae03c`, `_isr_calls=0`, `_sgir=0x02000000`, `_pends=0x1`.
- **`xnu_live_tick_irq`** — the capped tick ladder, last value `0x80 (128)` @27879 (capped; see §2).
  **Absence past 27879 is a cap artefact, not a reading.**
- **The two edge probes** (`xnu_live_edge_mlie_*`, `xnu_live_edge_texr_*`) — `mlie` @5544-5555 all
  `_tick=0x62 (98)`; `texr` @21767-21883 all `_tick=0x79 (121)`. Both are **well before** the freeze;
  their per-site cap of 4 burned out early, so they say nothing about the freeze.
- **The 1004 abort trace** (`xnu_live_sleh_*`) — the last abort `seq=40` @28083,
  `far=0x3f00c000`, `upc=0x804955c0`, pre-burst.
- **`xnu_live_stall_*`** — the ONLY IRQ/CPU-state history that straddles the burst. Samples 9–15
  (post-burst) all hold `pc=0x8001b32d`, `lr=0x8046b95c`, `cpsr=0x60000113`. Symbolized against the
  1005 image: `0x8001b32d = timer_switch+0x19`, `0x8046b95c = sleh_abort+0x468`. **`cpsr=0x60000113`
  has bit 7 (I) SET — interrupts masked — and is a supervisor-mode value.** This is a direct record
  showing the CPU is in kernel context with IRQ masked at every post-burst stall sample.

**Does any record show the state *during* the freeze?** No. Every GIC/IRQ-state record (`irq_line`,
`gicdrv`, both edge probes, the abort trace) is *before* the freeze; the only straddling records
(`stall_*`) are on the tick ladder and therefore sit *after* the interrupt path returned. There is **no
`sleh_cpsr`/GIC snapshot taken inside the 24.4 s window** — exactly the gap 1006's idle-path witness
fills.

Minor: `xnu_live_istack_cpsr1=0x60000193` (@3954), `xnu_live_ast_cpsr=0x0` (@21756),
`xnu_live_storage_pwr_wait_cpsr=0x80000113` / `_after=0x20000193` (@5465/5556) — all pre-freeze.

---

## 6. TIMELINE (record order — NOT any counter that wraps)

Method rules honored: `entry_counter()` (the pet's `calls` argument) wraps every ~3.7 min; the pet
publishes only the 8 gated endpoints; `xnu_live_tick_sum_maxd` is a latched max; the record stream
itself carries no clock. **No wall-clock dt is computed below, because none is recoverable from these
counters** — the only ordered quantity is line number.

The last ~40 direct key events, the burst, and the tail, in **record order**:

```
28062  cadence sample t=16: sync=16 tick=0x81(129) irq=0x84(132)   <- last sampled window = HEALTHY
28083  sleh seq=40 far=0x3f00c000 (last abort; NOT fatal)
28133..29054  storage_rd_ordinal 0x1e7..0x207, LBA 0x4a3810..0x4a3830 (all rd_complete=1,err=0)
28855  tick_sum: count=0x81(129) anom=0x0e maxd=0x04ba1ff0
29052  rootmedia served=0x3e refused boot (7,8)
29072  xnu_live_capped=0x00006000            <-- LIVE CHANNEL CAP FILLED HERE (tick_irq dies after this)
29073  wdt_pet_n=1 calls=592 down=0x4a297
29079  tick_sum: count=0x81(129) maxd=0x04ba1ff0
29085  wdt_pet_n=2 calls=1268
29088..29114  usvc_n=18(num=6) 19(num=0x152) 20(num=-3)
29115  wdt_pet_n=3 calls=1944
29121  ---------- BURST BEGINS (maxd pegs) ----------
29123  tick_sum: count=0x87(135) anom=0x14 maxd=0x1be8bc01(24.39s)
29127  wdt_pet_n=4 calls=2619
29133  tick_sum: count=0x8b(139) maxd=0x1be8bc01
29139  wdt_pet_n=8 calls=5322
29145  tick_sum: count=0x93(147) maxd=0x1be8bc01
29151  wdt_pet_n=16 calls=10730
29157  tick_sum: count=0xb1(177) maxd=0x1be8bc01
29163  wdt_pet_n=32 calls=21546
29166  stall_n=9  pc=0x8001b32d lr=0x8046b95c cpsr=0x60000113   (tick doubling 256)
29176  tick_sum: count=0x10d(269) maxd=0x1be8bc01
29182  wdt_pet_n=64 calls=43178
29188  tick_sum: count=0x1dd(477) maxd=0x1be8bc01             <-- LAST count SNAPSHOT
29194  stall_n=10 cpsr=0x60000113                              (tick doubling 512)
29201  usvc_n=21 num=-3  pc=0x2be5b328 lr=0x2be49af1
29209  usvc_n=22 num=-3  pc=0x2be5b328 lr=0x2be49b11          <-- LAST SVC
29217  stall_n=11 cpsr=0x60000113                              (tick doubling 1024)
29224  stall_n=12 cpsr=0x60000113                              (tick doubling 2048)
29231  stall_n=13 cpsr=0x60000113                              (tick doubling 4096)
29238  stall_n=14 cpsr=0x60000113                              (tick doubling 8192)
29245  stall_n=15 cpsr=0x60000113                              (tick doubling 16384) <-- LAST RECORD
29253  No errors detected
```

Interpretation of the tail, by record order (not by any counter):
1. Healthy sampled window ends (@28062), last abort @28083.
2. **Cap fills @29072** (a hard artefact boundary for every `entry_live_write` key).
3. The freeze has already happened by @29121 (maxd pegs to 24.39 s); the burst then runs.
4. The tick ladder keeps advancing past the last `tick_sum_count` snapshot (@29190 → count 477) all the
   way to doubling 16384 (@29245), visible **only** through the uncapped stall ladder.
5. The last SVC (@29209) sits inside the burst; the machine ends alive (`No errors detected`), never
   panics — the death gate is OPEN, so these absences count.

---

## WHAT THIS DOES NOT TELL US

- **No wall-clock, anywhere.** The record stream has no timestamp; `entry_counter()`, the tick count,
  and the cadence counters all wrap or are latched. Any "dt" derived from them is a wrap hazard
  (`mi4-measurement-defects`). The ~24.4 s is a *tick count ÷ 19.2 MHz*, inferred from `tick_hz`
  (`xnu_live_tick_hz=0x0124f800`), not a measured duration.
- **No state recorded inside the freeze.** Every GIC/IRQ/CPSR record predates it (edge probes at ticks
  98/121; `irq_line`/`gicdrv` config earlier still) or postdates it (stall samples on the tick ladder).
  Whether the GIC distributor, the CPU interface, or `cpsr.I` changed *during* the 24.4 s is unobserved.
  This is the hole 1006 (`STAGE90_XNU_FREEZE_WITNESS`, `xnu_live_frzbase_*` / GIC state on the idle
  path) exists to fill.
- **The tick burst's true size is not pinned.** The last `tick_sum_count` is 477, but the uncapped stall
  ladder proves the counter passed 16384; nothing records the counter *after* that. "≥16384" is a lower
  bound from ladder arithmetic, not a recorded value.
- **Whether storage/pet advanced during the freeze** is not answerable (Q4) — no record is orderable
  inside the window and the pet gate publishes only 8 endpoints.
- **The freeze's cause.** Nothing here identifies a masked critical section, a GIC re-mask, or an
  un-EOI'd ISR. The `cpsr=0x60000113` (I=1) stall samples are *after* the interrupt path returned, so
  they show the CPU reaches a kernel path with IRQ masked, but not that the mask *caused* the freeze.
- **A caution for the 1005 read:** its "tick climbs 135 → 479" understates the counter by ~35x; 479 was
  the last *pet snapshot*, not the terminal tick. Any 1006 comparison against "135→479" should be
  re-based on the stall ladder instead.