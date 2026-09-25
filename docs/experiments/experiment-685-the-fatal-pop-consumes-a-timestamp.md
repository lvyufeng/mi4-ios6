# 685: the frontier's fatal `pop` consumes a *timestamp* — the frame is clobbered, and it is not a cache

Host-side and read-only: two archived captures and the image's own disassembly. What it finds is that the seven
steps of cache work aimed at this `pop` — 546's mechanism, 571, 638, 652, 656, 657 and the L2 arm 676
prepared — have been aimed at a fault whose operands are **not stale, not dirty, and not even addresses**.

**No device was touched. Nothing was built, nothing was flashed, no firmer was armed.**

## 1. The seam reads exactly the words the pop eats — now proved from the disassembly

`platform_cache_idle_exit` (`0x800462d4`, read here in 678's image):

```
800462d4:  push {fp, lr}                 ; sp = S.  [S] = fp, [S+4] = lr
800462d8:  bl   __wrap_FlushPoU_Dcache   ; <-- THE SEAM IS CALLED HERE
...                                      ; (no sub sp / add sp anywhere in the function)
8004633c:  pop  {fp, pc}                 ; reads [S] -> fp, [S+4] -> pc
```

**There is no stack adjustment between the `push` and the `pop`**, so the `sp` at the seam's `bl` *is* the `sp`
at the pop: both are `S`. And `S` is measurable and identical in both captures — `xnu_live_seam_sp = 0x8054fec8`
in 684 *and* in 574-park.

The wrapper confirms the value independently:

```
8047c994 <__wrap_platform_cache_idle_exit>:
8047c994:  str r4, [sp, #-8]!      ; the wrapper's OWN frame is at S + 8
8047c998:  str lr, [sp, #4]
...
8047c9bc:  bl   platform_cache_idle_exit
8047c9c0:  ...                     ; <- lr = 0x8047c9c0, the byte after the bl
```

and `seam_b1 = 0x8047c9c0` in 684, `0x8047c990` in 574-park — **each the return address of that `bl` in its own
image**, and `seam_b0 = 0x800b2648` the pushed `fp`, the same value in both. So the seam is reading the frame's
own `{fp, lr}`, before anything in this arm runs. **656's join is right**, and it is now right by two routes
rather than one: the arithmetic of 684's own fault (`sleh_sp = 0x8054fea8 = S - 0x20`, which is exactly
`entry_seam_flush`'s frame below the slot it is passed) and the function's own body.

## 2. And the fatal `pop` is that pop, with the project's own instrument measuring its two operands

574-park's fatal record, read whole:

```
r4:  0x05006e74  r5: 0x8051a000  r6: 0x8051a0e0  r7: 0x00000000
r8:  0x80553520  r9: 0xc0558df0  r10: 0x800ba588  r11: 0x05006e74
sp:  0x8054fed0  lr: 0x001ffff3  pc: 0x05006e74
cpsr: 0x80000093   fsr: 0x00000005   far: 0x05006e74
```

and the abort-site instrument's own four words, taken at the abort:

```
xnu_live_slot_ab_sp = 0x8054fed0      (= S + 8)
xnu_live_slot_ab_m16 = 0x03000000     [S - 8]
xnu_live_slot_ab_m12 = 0x04fa5ef0     [S - 4]
xnu_live_slot_ab_m8  = 0x05006e74     [S]      <-- the word the seam read as b0 = 0x800b2648
xnu_live_slot_ab_m4  = 0x05006e74     [S + 4]  <-- the word the seam read as b1 = 0x8047c990
```

`ab_sp = S + 8` is the pop's post-writeback `sp` (`ldm sp!, {fp, pc}`), so its base was `S`, and
`ab_m8`/`ab_m4` **are the two words it consumed**. Both the frame's `fp` and the frame's `pc` slot had been
overwritten with the same value — and `r11` holds it too (the popped `fp`), which is what a `pop {fp, pc}` whose
second word is a bad address leaves behind.

`fsr = 0x5` with `far = pc` is a **section translation fault**, and `cpsr = 0x80000093` is **SVC mode**: the CPU
tried to *fetch* at `0x05006e74` in kernel mode and the address is not mapped. That is the same fault class 684
measured for `RESTART_REASON` one step earlier and in a different table.

## 3. The value is a timestamp, and that is what makes this the finding

`0x05006e74` is not a corrupted pointer. Read out of Apple's own source (`osfmk/arm/rtclock.c`):

```
rtclock.c:354   cdp->rtcPop        = delay_time + current_time;   /* setPop()      */
rtclock.c:382   cdp->cpu_idle_pop  = delay_time + current_time;   /* SetIdlePop()  */
rtclock.c:401   cdp->cpu_idle_pop  = 0x0ULL;                     /* ClearIdlePop() */
```

`rtcPop` is a **decrementer deadline** — `mach_absolute_time()` plus a delay — and `cpu.c:147-148` even guards a
re-plant with `if (cpu_data_ptr->rtcPop != lastPop) SetIdlePop();`. And the payload publishes that very field:
`entry_slot_rtc_note` reads `cpu_data->rtcPop` (`cpu_data + 0xe0`) and writes it as `xnu_live_slot_rtcpre_pop`
and `xnu_live_slot_rtcab_pop`.

**Measured, and it settles the interpretation:** `rtcpre_pop` in 574-park is `0x05006e74` — *exactly* the fatal
`pc`. And in **both** captures the abort-side readings are low, run-varying values of the same kind
(`0x04eae66f`, `0x04eb1a35`, `0x04fd3952` in 574-park; `0x0769abf2`, `0x0769df82`, `0x077d4025` in 684) — which
is what a timebase looks like and **not** what a clobbered code pointer looks like. Also identical across both
runs: `rtcab_datap = 0x8051a000` (`cpu_data`) and `rtcab_lr = 0x800b5f00`; different: `rtcab_pcb`, `rtcab_thr`,
`rtcab_sp`.

So the frame's `{fp, pc}` were overwritten with **the decrementer's deadline**, twice, with the same value.

### 3.1 And the numbers are a timebase, in order — which a clobbered pointer would not be

This device's timebase is 19.2 MHz (`cntfrq = 0x0124f800`), and every one of these values reads as a plausible
elapsed-since-boot when divided by it:

| run | key | value | / 19.2e6 |
| --- | --- | --- | --- |
| 574-park | `rtcab_pop` #1 / #2 / #3 (at the aborts) | `0x04eae66f` / `0x04eb1a35` / `0x04fd3952` | 4.297 / 4.298 / 4.360 s |
| 574-park | **the fatal `pc` = `rtcpre_pop`** | `0x05006e74` | **4.371 s** |
| 684 | `rtcab_pop` #1 / #2 / #3 | `0x0769abf2` / `0x0769df82` / `0x077d4025` | 6.477 / 6.478 / 6.544 s |
| 684 | `rtcpre_pop` (the exit wrapper's reading) | `0x077f3597` | 6.551 s |

Three things follow, and each is a check the "corrupted pointer" reading would fail:

* **The values ascend within each run**, and the reading taken *earliest* in the pass is the smallest — the
  `rtcab` readings (at the aborts) are all below the `rtcpre` reading (in the exit wrapper), which is what a
  monotone clock gives and what a clobber gives only by chance.
* **574-park's fatal `pc` is the largest of its four** — 4.371 s against a latest abort-time reading of
  4.360 s, i.e. about **11 ms newer than the freshest sample of the same field**. The value that killed the boot
  is a *later* deadline than any the instruments had seen, so the frame was not carrying an old copy of
  anything: it was carrying a value the timer had only just computed.
* **684's whole scale is ~2.2 s later than 574-park's**, and 684's boot genuinely ran longer (it reached the
  seam and ended deliberately instead of dying at the pop). One clock, two runs, ordered the way the runs
  happened.

**So the operand is a timestamp on this device's own clock, in the right order, at the right magnitude — and the
`pop` jumped to it.** That is not a stale cache line answering a lookup; it is a *timer deadline written into a
frame's `{fp, pc}`*.

## 4. What that does to the seven steps of cache work aimed at this `pop`

They are aimed at the wrong operands, and the arm's own text said so before any of this:

> `entry_trace.c:2244` — a clean half that finds a **dirty** stale line writes it *out*, so the two words come
> back **changed** and holding `cpu_data->rtcPop` … two equal pairs say the line was clean and **the pop's wrong
> value came from somewhere this arm has not touched**.

574-park measured `b0 == a0 == 0x800b2648` and `b1 == a1 == 0x8047c990`. 684 measured the same equality at
0x8047c9c0. **Two runs, two images, the same cell — and the arm's own sentence for it is the finding**: the slot's
line was clean, the operation changed nothing, and *the value that kills the boot was written to the frame by
something outside the slot's line entirely*. 652 read that as "candidate (A), the operation is inert"; it is
stronger than that. **The operands the operation protects are not the operands that are wrong**, and 546 §3's
mechanism — a stale line answering the pop — is out, because the line was never stale and the wrong value is not
a stale copy of anything: it is a value that never belonged in the frame.

**So the `pop` is not dying of a cache. It is dying because two words of its frame were replaced with a timer
deadline between the seam's read and the pop.**

## 5. Where the clobber can still be, and the one measurement that decides it

The idle pass, in the order this image's `cpu_idle` runs it (`0x8000d934`, read here), with the payload's own
wrappers marked:

```
8000d934  sub sp, sp, #8
8000d96c  bl __wrap_SetIdlePop            (returns a flag; cpu.c:125 branches on it)
8000d97c  b  __wrap_Idle_load_context     (taken when nothing changed)
8000d98c  ldm r6, {r4, r7}                r6 = cpu_data + 0xe0  -> r4 = cpu_data->rtcPop
8000d994  bl pmap_switch_user_ttb
8000d9d4  blx r3                          a cpu_idle handler, cpu_data + 0x88
8000da0c  ldrd r0, [r6] ... eor ... orrs  compare rtcPop with the saved pair
8000da20  bl __wrap_SetIdlePop            (re-plant when it changed)
8000da24  bl kpc_idle
8000da28  bl __wrap_platform_cache_idle_enter
8000da38  bl __wrap_cpu_idle_wfi          <-- already wrapped, and entry_note_wfi runs here
8000da3c  bl __wrap_platform_cache_idle_exit   <-- THE SEAM, and the push/pop in §1
8000da40  bl ClearIdlePop
```

**`cpu_idle` has no `pop {fp, pc}` of its own** — it ends `mov lr, pc` / `b cpu_idle_exit` — so §2's pop is the
only candidate, and it is the exit's. The clobber therefore happens **between 0x800462d8 (the seam) and
0x8004633c (the pop)**, and the code in that window is: two global reads, `InvalidatePoU_Icache`,
`flush_core_tlb`, two `mcr`s that turn `SCTLR.C` **on**, and a `TPIDRPRW`/`ldr`/`str` into `cpu_data + 304`
(0x800462dc–0x80046338 — the four instructions 657 measured, and 657 was right that **no cache maintenance is
among them**).

**The decisive measurement is one read at one site, and the site already exists**: `__wrap_cpu_idle_wfi`
(`0x8047c8b4`) is wrapped and already calls a note. Reading `[S]`, `[S+4]` and `cpu_data->rtcPop` **there** —
after the WFI, before the exit — and again inside the seam, brackets the whole interleaving between the two
known-good values and the fatal one. **It needs no new mapping, no new literal, and no new switch on the payload
side**: it is the `rtcpre` instrument, which is already built and already reads exactly this field, read at a
second site. Two readings of one address in one pass is what turns "somewhere in the idle path" into a step.

## 6. Why this is the most hopeful step the frontier has had

Three of the four hypotheses this project has spent arms on are now excluded by measurement rather than by
argument:

| hypothesis | verdict |
| --- | --- |
| the slot's line is **dirty** and the clean writes it out (546 §3) | **out** — the pair is equal, twice, and the arm's own text names that reading |
| the invalidate does not survive the distance to the pop (571) | **out** — nothing between the seam and the pop is a cache op (657, re-read here) |
| the operand is a **stale cache copy** of the right value | **out** — the value in the frame is a timestamp, and no copy of a timestamp belongs in `{fp, pc}` |
| **the frame is clobbered with the timer's deadline** | **the live one**, and it is the one no arm has ever read for |

And the clobber is not the *pop's* mechanism failing — it is a **write**, by something in a five-instruction
window, into a frame that two independent instruments agree was correct microseconds earlier. A write has a
writer, and the window is small.

## 7. The goal, honestly

**This does not enter the OS and it does not move the frontier**: the boot still dies at the exit's `pop`, and
nothing here was run on the device. What it does is take the project off a hypothesis that three arms have
already falsified and onto the mechanism the project's own instruments have been reporting for two runs —
**「起码要能进入操作系统，把基础驱动跑起来」 is not advanced**, no device was touched, nothing was flashed,
nothing was written to storage, and **TWRP-to-storage stays withheld**.

**One consequence for the next arm is worth stating now.** 684's ending faulted because it wrote to a low address
the payload's table maps and XNU's pmap does not; §2's fatal fault is of that same class. **Both faults are
section translation faults on addresses below XNU's window**, in a boot whose handoff handed XNU a table built
for a payload. That is now two independent instances of one shape, and it belongs beside this finding rather
than after it.
