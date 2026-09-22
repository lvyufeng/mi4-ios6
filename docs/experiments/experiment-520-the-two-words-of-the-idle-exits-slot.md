# 520: the two words of the idle exit's `{fp, lr}` slot, watched

Stage90. Built host-side with `STAGE90_XNU_IDLE_STACK=1` (default), `STAGE90_XNU_ISTACK_SEPARATE=0`,
`STAGE90_XNU_EXIT_POC_FLUSH=0` — **519's switch set unchanged, on purpose** — then **run once on
hardware** (§7) through the standing gate, one non-persistent `fastboot boot`, nothing flashed and
nothing written to storage. The device came back on its own.

The image is `out/stage90/stage90-qcdt.img`, sha256
`25c4bc7d127da6605015c453d94a578394792f8e1941ed1a8418e6cdbe796e09`, 8,540,160 bytes. The copied entry
image is `xnu_arm_entry.bin`, 5,519,996 bytes, sha256 `9a5f8862a1d495f2ce56dc2fc07ee940c7a33f79165ccdd6554d8b86a3b60c6b`
— **the same size as 519's**, which is expected and is the linker's fill term rather than an error:
the copied span ends at a pinned `__bss_start`, so a `.text` that moved by −3,688 bytes against 519's
build is absorbed by the fill below `.data`. `.bss` is **379,696**, byte-for-byte 519's: 520's three
key tables are *initialised* globals and so land in `.data`, and the 41 key strings in `.rodata`
inside `.text`.

## 1. The question this step exists to answer

519's run ended in a diagnosed panic — `sleh_abort: prefetch abort in kernel mode:
fault_addr=0x7152a6c` — whose saved state matched the idle path instruction for instruction (`sp`,
`lr`, `r0`, `r1`, `r4`, `r5`), and 519 §10 then showed *by arithmetic* that no exception frame can
write above the `sp` it interrupted: `EXC_CTX_SIZE` is 360 (`genassym.c:188`), the VFP area is placed
at `align16(base + 80) + 16` and is 264 bytes, so it ends at or below the interrupted `sp`. So the
writers 519's own §10 table enumerates — the exit's `push`, the two wrappers' `strd`, `entry_note_pcx`'s
`strd`, the exit wrapper's one-word `str` — none of which writes two counter readings one tick apart.
The step's conclusion was therefore **not** a mechanism but a measurement: *the writer has to be
watched*, and 519 §11 wrote the list of readings that does the watching. This step is that list, and
nothing else. Every call it adds is a measurement: it changes no kernel state, and the only memory it
writes is this image's own data.

## 2. The five readings

| # | where | what |
|---|---|---|
| 1 | `__wrap_platform_cache_idle_exit`, before its `bl platform_cache_idle_exit` | the four words ending at the wrapper's own `sp`, i.e. `[sp-16, sp)` |
| 2 | the same wrapper, after that call returns | the same four words — the control: on a surviving pass the slot holds the pushed `{fp, lr}` |
| 3 | `entry_note_sleh` | the same four words of the **aborted** context, at the frame's own `SS_SP` |
| 4 | `__wrap_ml_get_timebase` | the counter read either side of the real function, plus its return |
| 5 | `__wrap_platform_cache_idle_exit` *and* `entry_note_sleh` | `cpu_data->rtcPop` (`+224` = `0xe0`) and the idle thread's saved `sp`/`lr` from its pcb |

Reading 1 is the centre of the arm and its address is derivable rather than guessed. The wrapper's
whole frame is one `str r4, [sp, #-8]!` (the build clause asserts it is the only stack movement in the
body), so the C body's own `sp` **is** the real exit's entry `sp`; and that is the same `sp` the fatal
panic holds, because the frame `fleh_irq_kernel`/`prefabt_from_kernel` builds is 360 bytes deep and
never reaches above it. So `[sp-8, sp)` is exactly the word pair `platform_cache_idle_exit`'s
`push {fp, lr}` writes and its `pop {fp, pc}` reads. Four words rather than two because 519 §11 asks
for `[sp-16, sp-8)` at the abort as well: the lower doubleword is what the frame reserves and leaves
alone, so a value found there rules one more writer out.

Reading 3 is the half of the arm that does not need the run to survive: `SS_SP` is written by the
vector before any handler runs, and §10's arithmetic is what says nothing the handler does can move
those words.

## 3. Why the readings are published on a schedule

The idle loop can make thousands of passes. Six or seven records per pass would fill the 8192-record
live channel — 498's run already measured 97% of the old cap — and evict the trace this step exists to
read. So each site publishes while its count is at most 4 and thereafter at the **powers of two**
(`1, 2, 3, 4, 8, 16, …`): O(log n) records, and the last published reading is at least half way to the
run's last call. The count itself is always published with the reading, so a sample is never mistaken
for the last call.

**This is the design decision §11 of this doc has to correct: it is wrong for the abort site**, which
is a low-frequency site whose interesting reading is the *last* one. §11 records what that cost.

A reading the guard refuses is published as a zero with a refusal count (`_rej`) beside it, so "absent"
and "nothing was read here" stay different readings.

## 4. What the build asserts

`build_entry.sh`'s `xnu_entry_520` clause, on the linked image:

* `__wrap_platform_cache_idle_exit` moves `sp` down **once**, by 8, and back up once; reads `sp` off
  the register once, *after* that decrement; calls `entry_slot_note` exactly twice — once before its
  `bl platform_cache_idle_exit` and once after; calls `entry_slot_rtc_note` once, before the real call;
  and calls the real exit once. The first two are one property: a wrapper with a second stack movement
  would publish four words of its own frame under the name of the slot.
* `entry_note_sleh` reaches `entry_slot_note` and `entry_slot_rtc_note` once each.
* each of the three notes' bodies reaches `entry_live_write` exactly as many times as its key table has
  key pointers (7 / 8 / 4) — the "one value, two definitions" rule applied to a key *list*.
* the three tables are in the image at their stated sizes (40 / 44 / 24 = *n* pointers and three words),
  and the source spells exactly 7 / 7 / 7 / 8 / 8 / 4 distinct key literals for the six sites: a pointer
  dropped from a table, or a literal added without a pointer, stops the build.
* all 41 keys are in the entry image's strings (`grep -a`, and §11 says why `-a` is not decoration).
* `assym.s` and `entry_stubs.c` agree on `TH_KSTACKPTR` (1480) and `ACT_CPUDATAP` (1484), and on the
  `ACT_CPUDATAP` 517's clause already read; `STAGE90_CPU_RTCPOP` is 224 and **`cpu_idle`'s own body is
  what says so** — the clause requires an `add rX, rY, #224` inside `cpu_idle`, which is the instruction
  that loads the deadline into `r4`.

**Two shapes had to be accepted, and the first build of the clause is why.** It counted `bl` to
`entry_live_write` and came out one short on *every* note: gcc tail-branches the last one (`b
<entry_live_write>`), and `entry_note_sleh` likewise reaches `entry_slot_rtc_note` by `b` because it is
that function's last statement. A check for one encoding would have read a body that publishes every
key as a body that publishes all but one — the defect 517 recorded from the other side, where the shape
asserted was the compiler's choice rather than the property.

## 5. The prediction, written before the run

519 §11's decision rule, verbatim in its terms:

* If reading 1 is *already* the timebase pair, the writer wrote before the exit ran and the exit's own
  `push` cannot be what the `pop` read.
* If reading 1 is clean and the run still dies in the `pop`, the writer ran between the `push` and the
  `pop`, and §10 says the only thing that can run there is an exception — whose frames are now bounded
  exactly.

And what each reading would say:

* reading 2 **absent** ⇒ the real exit did not return; the pass died inside it.
* reading 5's `rtcPop` against the panic's `pc`: equal (modulo bit 0) means the value jumped to *is* the
  deadline the idle loop computed, which turns 519's `r4 = 0x07152a6d` from an identification of a
  register into an identity of a memory location.
* reading 4's own pair: the only code on that path that reads the counter twice in a row, so
  `after - before` says whether a 1-tick pair can come from there at all.

## 6. Safety

Non-persistent `fastboot boot` only, one run, through `preflight_boot_check.sh --allow-xnu-entry` and
`run_and_capture.sh --allow-xnu-entry`; nothing flashed, nothing written to storage. 520 adds no state
change at all — the only new memory traffic is seven `entry_live_write` calls into the console's own
live channel, published O(log n) times per site, and four `.data` writes per call in the notes. The
hardware watchdog is the same net it has been since 506 and it recovered this run.

**One gate defect was found and fixed before the run, and it is this project's oldest class.**
519c's edit put the literal `40bf8a0b...` into the gate's XNU-entry prose, where it was true of 519's
image — and then 520's build produced `25c4bc7d...` and the gate went on telling the operator that the
image it was checking was 519's. The manifest check does not catch it: nothing compares a hash written
in a comment. The gate now computes `sha256` of the image and prints *that*; the literal is gone.

## 7. The run

Device `4a2fe00b`, gate green (it printed `25c4bc7d127da660...`, computed from the file), one
non-persistent `fastboot boot`, the device back on its own. `/tmp/cancro-last_kmsg.txt` 596,665 bytes,
8,919 lines, 139 `xnu_live_slot_*` records, the epilogue's `No errors detected`.

**The run reproduced 519's failure at the same instruction.** The fatal record is
`xnu_live_sleh_storm = 9` (the ninth abort of the boot), and its frame is:

    xnu_live_sleh_pc    = 0x04b79074
    xnu_live_sleh_lr    = 0x800462dc
    xnu_live_sleh_sp    = 0x8054fed0
    xnu_live_sleh_user  = 0
    xnu_live_sleh_frame_ok = 1

`sp` and `lr` are **numerically identical to 519's run** (`0x8054fed0`, `0x800462dc`), and the panic is
again `sleh_abort: prefetch abort in kernel mode`. So this is the same death, at the same address, in
the same instruction — which is what makes the readings below readable as instruction-level statements
rather than as correlations.

## 8. What the run answers: the `pop` read `cpu_data->rtcPop`

The three numbers fit together with no free parameter:

* **`xnu_live_slot_rtcpre_pop = 0x04b79075`.** Reading 5, taken in the exit wrapper *immediately before*
  the call that never returned (`_calls = 1`, `_rej = 0`), so it is `cpu_data->rtcPop` at the moment the
  fatal pass entered the idle exit. `cpu_idle` loads exactly this value — `lastPop = cpu_data_ptr->rtcPop`
  — into `r4`, and 519's own panel has that register.
* **`pc = 0x04b79074` = `rtcPop - 1`.** Bit 0 of `rtcPop` is set, so `0x04b79075` is a Thumb address; an
  instruction fetch there is reported by the fault as `0x04b79074`, the halfword-aligned address. This
  is exactly the relationship 519 §9 read off its own run (`r4 = 0x07152a6d`, `pc = far = 0x7152a6c`)
  and it is the whole reason reading 5 exists.
* **`lr = 0x800462dc` is the return address of `platform_cache_idle_exit`'s `bl FlushPoU_Dcache`
  (`0x800462d8`), and the faulting instruction is that function's own `pop {fp, pc}` at
  `0x8004633c`.** The image says so: `0x800462d4 push {fp, lr}` / `:62d8 bl FlushPoU_Dcache` /
  `:62dc movw r0, #4516` … `:6300 bcc 0x8004630c` (taken, which is why `lr` keeps the flush's return
  address and not one of the two later `bl`s) … `:633c pop {fp, pc}`. A `pop` does not touch `lr`, so
  `lr = 0x800462dc` with `pc` far away is precisely a `pop {fp, pc}` whose loaded `pc` was the deadline.

So: **the `pop {fp, pc}` at `0x8004633c` loaded `0x04b79075`, and `0x04b79075` is `cpu_data->rtcPop`.**

### 8.1 Who puts the deadline in that word

Only one instruction in the image stores `cpu_idle`'s `r4` — i.e. `lastPop`, the deadline — into the
word the `pop` reads as `pc`, and it is in the idle path's own entry:

    __wrap_platform_cache_idle_enter   0x8047c8d4:  strd r4, [sp, #-12]!
    __wrap_cpu_idle_wfi                0x8047c888:  strd r4, [sp, #-12]!

`strd r4, [sp, #-12]!` is pre-indexed with writeback: `sp -= 12`, then `r4` at `[sp]` and `r5` at
`[sp+4]`. Both wrappers are entered from `cpu_idle` at the same `sp` — call it `X` — and the exit
wrapper's frame is `X - 8`, which §7's own reading confirms: `xnu_live_slot_pre_sp = 0x8054fed0 = E`,
and `X = 0x8054fed8` is `stage90_idle_stack + 16384 - 8`, i.e. the array's top less `cpu_idle`'s own
`sub sp, sp, #8`. So the `strd` writes `r4` at `X - 12 = E - 4` — **the upper word of the slot**, which
is the word `pop {fp, pc}` reads as `pc`. 519 §10's table already says this ("the slot's *upper* word
holds the deadline"), and the run now says it with a number.

The exit's own `push {fp, lr}` writes that same word with its `lr`, and it runs first. So the question
is not *who* writes the deadline there but **why the `push`'s write is not what the `pop` reads**.

### 8.2 Why the `push` does not win

The window. The idle body is entered with the D-cache on, `platform_cache_idle_enter` clears `SCTLR.C`
(`caches.c:406`) and `platform_cache_idle_exit` sets it again at the end (`caches.c:490`). Everything
between runs with the cache **off**, including the exit's `push {fp, lr}` — a store with `SCTLR.C = 0`
reaches DRAM and does not update, nor invalidate, whatever the cache still holds for that address. And
the cache *does* still hold a valid line for the slot's upper word, from before the window: the `strd`
ran with the cache on, so the line was dirty; 516's enter-side `CleanPoC_Dcache()` — a set/way loop of
`mcr p15, 0, r0, c7, c10, 2`, `DCCSW` — **cleans** it, which writes the deadline to DRAM and leaves the
line **valid**. Then the window's two stores (`strd` again, then `push`) write DRAM only, and
`caches.c:490` sets `SCTLR.C` again, and the `pop`'s load **hits** the still-valid line and reads the
pre-window value: the deadline.

Apple's own exit-side flush does not repair this either: `FlushPoU_Dcache` (the `bl` at `0x800462d8`) is
`DCCISW` — clean **and invalidate**, which would be the right operation — but only over the **L1**'s
geometry, and the line is also in the L2.

**The operation that repairs it is a clean-and-invalidate at the Point of Coherency, and this project
already has one.** `FlushPoC_Dcache` is a set/way loop of `mcr p15, 0, r0, c7, c14, 2` — `DCCISW`, run
once for the L1 and once for the L2 (the 517 clause counts both loops in the linked body) — and it is
the function **517 added behind `STAGE90_XNU_EXIT_POC_FLUSH`, one call before the real exit, and it is
off in every image that has been run since.** So:

> **Prediction for the next run.** With `STAGE90_XNU_EXIT_POC_FLUSH=1`, the exit wrapper cleans and
> invalidates both cache levels *before* `platform_cache_idle_exit` runs. The `push` then writes the
> only copy of `{fp, lr}` that exists — in DRAM, with the line invalid — and the `pop`'s load misses the
> cache entirely and reads DRAM. The idle exit returns to `0x8047c964`, and the idle path no longer
> dies in its own epilogue.

This is a *state change* and it is one change, which is 517's own lesson applied: 517's first image
carried this flush **and** the frame reader and did not come back, so neither could be attributed. The
next arm carries the flush and the readings that watch it.

## 9. What reading 4 answered

`ml_get_timebase` was called **at least 1024 times** in this run (the last published `_calls`), and
**517's instrument published no `xnu_live_tb_*` record at all** — which its own clause says can only
happen when `SCTLR.C` is set, because it returns early in that case. So: every one of those calls had
the cache **on**, and `ml_get_timebase` never ran inside the WFI window. And its own pair spans
**exactly 24 ticks** every time (`before` and `after` around a body that is one `mrrc`), with the
returned value sitting strictly between them (`0x048cd50b` between `0x048cd4ff` and `0x048cd517`).

So the answer to 519 §11's reading 4 is **no**: the one piece of code on this path that reads the
counter twice in a row reads it 24 ticks apart, never inside the window, and cannot produce the 1-tick
pair the slot held.

## 10. The instrument's own defects, found by the run

Three, and all three are the "a measurement can be the thing that is wrong" class.

**1. Reading 1 and 2 were read through the reader's own frame.** `entry_slot_note` is a C function
called at `sp = E`, so its own prologue saves registers in `[E-24, E)` — and the four words the reading
is about are `[E-16, E)`. The run says so in its own numbers: `xnu_live_slot_pre_m4 = 0x8047c974`,
which is the instruction immediately after the `bl <entry_slot_note>` at `0x8047c970` — i.e. the
note's **own saved `lr`** — and `pre_m8 = 0x80553520` is one of its saved general registers. The slot's
real contents are not in that record. This is defect 394 from the other side (a value read at an
address the reader's own call has already overwritten), and the repair is to take the four loads in
the **wrapper**, as a single inline-asm block (`mov` plus four `ldr` with negative offsets, no stack
use at all), and pass the *values* to the publisher — values in registers cannot be spoiled by the
callee's frame, and the call that follows may then use as much stack as it likes.

**2. The publishing schedule skipped the fatal abort.** The abort site's readings are sampled at
`calls = 1, 2, 3, 4, 8, …` and the fatal abort is `seq 9`. Its `SS_SP = 0x8054fed0` is inside the
guard's window, so a reading taken there *would* have been published — the one reading the whole arm
exists for was thrown away by the sampler. A low-frequency site whose interesting reading is the last
one must publish every call up to a bound rather than a geometric subsequence.

**3. The guard's windows are too narrow for the stacks this run runs on, and the refusal is not
visible where it matters.** The boot's ordinary aborts have `SS_SP` on stacks above `0xc1000000`
(`0xc820bf14`), so all of them were refused and published as zeros — correct behaviour, but the abort
record then says nothing about them. Worse, `entry_slot_rtc_note`'s *inner* dereferences are bounded by
the same two windows without counting their refusals, so the pcb read
(`__wrap_...`'s `TH_KSTACKPTR` → `0xc820bfa0`) silently published `sp = 0` and `lr = 0` at every abort
while `_rej` stayed 0. A reading that is zero and a reading that was never taken have to be different
numbers.

`xnu_live_slot_ab_*`'s five published samples are therefore all zeros; the three user-mode aborts
(`seq 5, 6, 7` at `pc = 0x00101118 / 0x00101124 / 0x001011a4`, `sp = 0x00101efc`) were correctly refused
by the same guard.

## 11. What the next arm does

One state change and the instrument repairs, with the flush as the step:

1. `STAGE90_XNU_EXIT_POC_FLUSH=1` — 517's exit-side clean-and-invalidate, unchanged and un-reordered,
   as §8.2's prediction names it. The verdict is the panic's absence: if the idle exit returns, this
   boot's death is retired and the frontier moves.
2. The four loads for readings 1 and 2 move into the wrapper's inline-asm block (§10.1), so the two
   readings that name the slot are the slot's.
3. The abort site publishes every abort up to a bound (§10.2), so the fatal one is never sampled away,
   and its `_rej` distinguishes refused from zero.
4. The read windows take this run's stacks — the aborts are on `0xc8xxxxxx` — and the rtc note counts
   its inner refusals (§10.3).

Readings 1 and 2 become load-bearing in the same run: with the flush on, `_post_calls ≥ 1` says the
exit returned, and the post reading says what a surviving pass has in the slot — which is the
assertion that the repair did what §8.2 says it does, rather than the run merely getting further.
