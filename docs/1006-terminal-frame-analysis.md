# 1006 terminal-frame analysis — `(pc, lr) = (0x8001b32d, 0x8046b95c)`

READ-ONLY, host-side. Resolves BY VALUE what the 1005 terminal stall frame's two addresses are,
using the exact linked image the 1005 press ran.

## 0. The image the capture came from

| item | value |
|---|---|
| capture | `out/stage90/captures/recovered-unknown-arm-20261014-020404-last_kmsg.txt` (the 1005 press read, `records/1005-press-read.txt`) |
| arm | `armed-d13-ff937225` |
| image | `out/stage90/frozen/armed-d13-ff937225/xnu_arm_entry.elf` |
| entry / segment | entry `0x80000000`, one LOAD `0x80000000` RWE |

The live `out/stage90/xnu_arm_entry.elf` has already moved on to 1006's `armed-d13-31bdebf3`, so the
**frozen** elf is the correct resolve base. (1005 was a pure measurement rung; the `enable_interrupts`
/ `sleh_abort` bodies are byte-identical between the two arms for the addresses below, but every
number here is read from the frozen `ff937225` elf as the load-bearing source.)

Debug info: **absent** — `addr2line -f` yields the symbol only and `??:?` for the file:line. Symbol
names from `nm` are the resolution; no line numbers exist for these objects.

## 1. `pc` — normalise bit0 first (a measurement-defect trap)

The D13 `pc` field arrives with **bit0 set** (Thumb marker) / a `+1` bias. Evidence: sample 5's
`pc=0x8000e821` disassembles to one byte *into* `st_pwr_wait+0x238`'s `add r4,r4,#1` (`e2844001` at
`0x8000e820`, next instruction `0x8000e824`); `0x8000e821` is not an instruction boundary. Every odd
sampled pc in the ladder is `real_addr | 1`. **Strip bit0 before naming.**

`0x8001b32d & ~1 = 0x8001b32c`.

## 2. `0x8001b32c` — inside `enable_interrupts`, the instruction right after `cpsie i`

`nm -n`: `8001b328 t enable_interrupts`, `8001b344 t disable_interrupts`, `8001b2dc t astloop`,
`8001b34c t out` — all local labels inside the single global `8001b2d0 T ml_set_interrupts_enabled`.

```
8001b328 <enable_interrupts>:
8001b328:  f1080080   cpsie   i            <- bytes 80 00 08 f1
8001b32c:  e92d4187   push    {r0, r1, r2, r7, r8, lr}   <- bytes 87 41 2d e9  = the sampled pc
8001b330:  e3a00001   mov     r0, #1
8001b334:  e10f1000   mrs     r1, CPSR
8001b338:  eb064bca   bl      801ae268 <entry_timer_edge_probe>
8001b33c:  e8bd4187   pop     {r0, r1, r2, r7, r8, lr}
8001b340:  ea000001   b       8001b34c <out>
8001b344 <disable_interrupts>:
8001b344:  f10c0080   cpsid   i
8001b348:  eaffffff   b       8001b34c <out>
8001b34c <out>:
8001b34c:  e3a00001   mov     r0, #1
8001b350:  e1c003a2   bic     r0, r0, r2, lsr #7
8001b354:  e12fff1e   bx      lr
```

**Verified:** the sampled pc is the `push` at `enable_interrupts+4`, i.e. the instruction
**immediately after the `cpsie i`**. The capture's label `enable_interrupts+5` was the raw `+1`
bit — the by-value symbol is `enable_interrupts+4`.

## 3. `0x8046b95c` — inside `sleh_abort`, a *forward branch*, not a return

`nm -n`: `8046b4f4 T sleh_abort` (next global `8046bbc8 irq_handler`). The capture's
`sleh_abort+0x468` is **correct by value**: `0x8046b95c - 0x8046b4f4 = 0x468`.

```
8006f298 <vm_fault>          (called from sleh_abort)
...
8046b944:  mov  r6, r0
8046b948:  cmp  r0, #0
8046b94c:  cmpne r6, #14
8046b950:  bne  8046b9b0 <sleh_abort+0x4bc>
8046b954:  e3a00001   mov      r0, #1             ; enable=true
8046b958:  ebeebe5c   bl       8001b2d0 <ml_set_interrupts_enabled>   ; 5c be ee eb
8046b95c:  ea000058   b        8046bac4 <sleh_abort+0x5d0>   ; 58 00 00 ea  = the sampled lr
8046b960:  ...                              (the other arm; NX-breakpoint dump path)
```

**Verified:** `lr=0x8046b95c` is exactly the return address of the `bl` at `0x8046b958`, and the
instruction there is `b 8046bac4` — an unconditional **forward branch**. There is **no**
`bx lr`/`mov pc, lr` at `0x8046b95c`; control cannot go back to the caller from here.

### Why `bl ml_set_interrupts_enabled` lands at `enable_interrupts`

`ml_set_interrupts_enabled(enable)` = `mrs r2,CPSR; cmp r0,#0` then, for `enable!=0`, it **falls
through with no branch** into `astloop` (`0x8001b2dc`), which eventually reaches
`enable_interrupts` (`bne`/`beq 0x8001b328`) — so **`enable_interrupts` never executes an epilogue
of its own on this path**:

```
8001b2d0:  mrs  r2, CPSR
8001b2d4:  cmp  r0, #0
8001b2d8:  beq  8001b344 <disable_interrupts>
8001b2dc <astloop>:
  ...
8001b2f8:  bne  8001b328 <enable_interrupts>
8001b304:  beq  8001b328 <enable_interrupts>
8001b328 <enable_interrupts>:
8001b328:  cpsie i
8001b32c:  push {r0,r1,r2,r7,r8,lr}      <- sampled
  ...
8001b340:  b    8001b34c <out>
8001b34c <out>: mov r0,#1; bic r0,r0,r2,lsr#7; bx lr
```

`out` (the single `bx lr`) is the shared tail reached from *both* `enable_interrupts` and
`disable_interrupts`; the `bx lr` there is `ml_set_interrupts_enabled`'s real return to
`sleh_abort`. So the pc/lr pair is exactly the synchronously-determined state of pid 1's thread the
instant **`cpsie i` completed**: the pending timer fires there, the CPU takes the IRQ **before** the
`push` at `+4` retires, and the frozen saved-state records pc=`+4`, lr=the return address of the
`bl` that is still outstanding.

## 4. THE KEY QUESTION — fault-return / RSR-shuffle loop?

**Verdict: REFUTED.**

The design doc §6 (`docs/1006-interrupt-freeze-witness-design.md`, "The candidate the terminal frame
favours") hypothesised a **fault-return loop**:

> "The natural hypothesis is a **fault-return loop** — the CPU re-entering `sleh_abort`'s return
> path, enabling interrupts and immediately faulting again… `pc=0x8001b32d` is `enable_interrupts+5`
> — the instruction AFTER `cpsie i`… `lr=0x8046b95c` (`sleh_abort`)."

Instructions read by value refute the loop:

1. **`0x8046b95c` is `b 8046bac4` (`ea000058`), a forward branch — not a return.** After
   `ml_set_interrupts_enabled` returns, `sleh_abort` proceeds forward to the
   `sleh_abort+0x5d0` epilogue (`0x8046bac4`: `cmp r6,#0; cmpne r6,#14; bne …; add sp,sp,#124;
   pop {…,pc}`). There is **no instruction at `0x8046b95c` (or any point that leads back to
   `enable_interrupts`) that sends control to `0x8001b32c`.** The exceptional return to user is the
   `pop {…,pc}` at `0x8046bad4` / `0x8046bb08`, which restores the *interrupted user* pc — not a
   kernel re-entry into `enable_interrupts`.
2. **`0x8001b32c` is only reachable by the fall-through path of `ml_set_interrupts_enabled`'s
   `enable` branch** (`astloop`) or by an IRQ preemption of it. It is not a re-entry target of
   `sleh_abort`.
3. **The lr is a genuine pendable return.** `0x8046b958: bl 8001b2d0` → return `0x8046b95c`. The
   `bl`-to-`enable_interrupts` **has a normal return** (`out`'s `bx lr`). It is a normal call that
   is **caught mid-flight by the timer IRQ at the `cpsie i`** — not a closed loop.

There is **no `bx lr`/`mov pc,...` at `0x8046b95c`** that would send control BACK to `0x8001b32c`;
the next instruction proceeds normally (forward branch to the epilogue). The repetition of the
*identical* frame across samples 9–15 is the sampler seeing **the same one interrupted instant**
each tick (the thread is parked at this pc across the freeze, and the powers-of-two ladder's last
samples sit closest together), **not** a per-iteration loop re-taking the fault.

Additional consistency check: the ppc-ladder is heterogeneous (`enable_interrupts`,
`st_pwr_wait` (the `wait_for_power`/idle usec-poll loop), `OSMetaClass`, `thread_continue`,
`kernel_bootstrap_thread`) — a fault-return loop would show one repeated kernel pc, not this spread.

*Limit stated honestly:* the pc/lr pair alone cannot distinguish "parked at this pc" from "cycling
through this pc"; the `sp` constant at `0xcfdbbf00` and the `b`-forward target are what make the
loop reading implausible, but the `d`/GIC reads of 1006 still carry the positive classification.

## 5. What this means for 1006

If the terminal frame were the §6 fault-return loop, 1006 would predict **`cpsr` I-bit 0, `ien` 1,
`pend` 1** (enabled, pending, CPU too busy faulting to take it). Because the loop is **refuted**, the
by-value picture inverts:

- The **A-bit is 1** in the sampled `cpsr=0x60000113` (`A=(0x113>>8)&1=1`). The `mrs r1,CPSR` inside
  `ml_set_interrupts_enabled` (after the `cpsie i`) reads back `A=1`, i.e. `ml_set_interrupts_enabled`
  was called **from inside the `sleh_abort` tree where aborts are masked** — consistent with the
  `A=1` `sleh_*` entry convention preserved through this call.
- So the freeze is **not** a self-sustaining fault loop. The machine enabling IRQs at `sleh_abort`
  as it returns to user **is the honest end of the abort**; the freeze begins only after this frame.
- The terminal frame is therefore the boundary **where the abort path hands back to the interrupted
  context**; the ~24.4 s whole-path freeze must be sought **past** it (the frame `sleh_abort` returns
  to), not inside it.

### 1006's `xnu_live_freeze_*` prediction at the press

The witness samples at the **first idle pass after a large `d`**, i.e. *after* the freeze has ended
and a fresh tick has fired (`ml_set_interrupts_enabled(1)` → tail call → this same
`enable_interrupts`, then `sleh_abort` returns and the idle path is reached). At that edge, because
there is **no loop and no hidden mask**:

| key | predicted | basis |
|---|---|---|
| `xnu_live_freeze_cpsr` | I-bit **0** (e.g. `0x60000113` / `0x20000113`), A-bit 1 if still in the abort tree | the frame read back by `mrs` after `cpsie i`; A=1 is the `sleh_abort`-entry mask |
| `xnu_live_freeze_ien` | **1** (timer line still enabled) | nobody disabled it: `cpsie i` ran, `GICD_ISENABLER0` untouched through `msm8974_tick_arm` |
| `xnu_live_freeze_pend` | **1** (irq in `GICD_ISPENDR0`) | the line asserted across the whole gap and was serviced by the post-$d$ tick |
| `xnu_live_freeze_act` | **1** (in `GICD_ISACTIVER0`) at the sampling instant | the tick handler is running when the sampler fires (`entry_note_stall_sample` is called from inside `pe_msm8974_irq`) |
| `xnu_live_freeze_ctlr` | **1** (distributor enabled) | the tick eventually ran → `CTLR.Enable` was set |
| `xnu_live_freeze_d` | large (tens of millions of counts, ~24.4 s at 19.2 MHz) | the freeze interval |

That triple — **I-bit 0, `ien` 1, `pend` 1, `d` large, `ctlr` 1** — is design doc §5's
**"a long non-IRQ critical path / fault loop"** row *in its non-loop reading*: the line was enabled
and pending while the CPU was busy in non-idle kernel code, but the code was a **long non-interruptible
stretch**, not a self-loop at this pc. §6's positive classification (`ien=1, pend=1`) stands; only
its *loop* mechanism is falsified. Candidate (i) — a long IRQ-masked critical section held across a
blocking path — is **not** supported either, because I-bit 0 at the edge with A-bit 1 says the mask
that mattered was the **abort mask (A)**, not I.

### Sharpened frontier for 1006 / 1007

The freeze is bounded by an **abort path that took ~24.4 s to return** — the frame at
`sleh_abort+0x468` is the call to `ml_set_interrupts_enabled` inside the abort tree, and the machine
sits there without the tick running. The next question is **what `sleh_abort`'s forward path
(`sleh_abort+0x5d0` and its epilogue) blocks on for 24.4 s**, and whether the abort source is the
1004 `vm_fault` WALK (39/40 `sleh_back` missing). This supersedes §6's fault-return-loop hypothesis.

## 6. Citations

- `docs/1006-interrupt-freeze-witness-design.md` §6 ("The candidate the terminal frame favours"),
  lines 213–241 — the hypothesised **fault-return loop**; §5 (classification table) row
  "`freeze_d` large, `ien` 1, `pend` 1, `cpsr` I-bit 0"; §9.1 (the "OWED" note that this exact
  instruction was not yet disassembled — **now discharged here**).
- `records/1005-press-read.txt` lines 85–89, 110 — the frontier frame and candidate (i).
- Image: `out/stage90/frozen/armed-d13-ff937225/xnu_arm_entry.elf` (arm `armed-d13-ff937225`).
- `arm-none-eabi-nm -n`, `arm-none-eabi-objdump -d/-s`, `arm-none-eabi-addr2line -f` — all reads.