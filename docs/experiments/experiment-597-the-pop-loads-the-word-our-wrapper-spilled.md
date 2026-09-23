# 597: the word the `pop` loads is the one our own wrapper spilled, and it is one address

Two facts about the death at `platform_cache_idle_exit`'s `pop {fp, pc}` had been established
separately, by different steps, and never joined. Joining them is mechanical — both are claims about
instruction addresses, so both can be *computed from the image* rather than argued — and the joined
fact is what makes the cache account of the death a measurement instead of a plausible story:

> **The address the two idle wrappers spill `r4` to and the address the exit's `push {fp, lr}` saves
> `lr` to are the same address** — `sp-12`, measured from `cpu_idle`'s own `sp` at its three `bl`s. And
> `pc` in both archived runs equals exactly the value of that spilled register.

The second half of this step is a correction to the *reasoning* the seam's arm rests on: the comment
inside `entry_seam_flush` says the enter path "cleaned the whole L1 earlier in this same window", so
its own clean half "has nothing to write back". Neither half holds, and the census that shows why is
now a check.

Nothing is built, nothing is run, and the payload on disk is untouched — the sleepless arm stays the
image the next power press sends. **TWRP stays withheld**: 「如果os已经能进去了的话」 is unmet.

## 1. The two facts, and why the value alone proves nothing

**(1) The value is ours.** The exit wrapper publishes `xnu_live_slot_rtcpre_pop` by spilling `r4` and
reading it back, and `r4` was loaded at `0x8000d98c` (`ldm r6, {r4, r7}` with `r6 = cpu_data + 224`) —
a deadline word, `cpu_data->rtcPop`. The panic's `pc` is that value with bit 0 masked off, because a
prefetch abort's `pc` is the *fetch address*:

| run | `xnu_live_slot_rtcpre_pop` | fatal `pc` |
| --- | --- | --- |
| 520 | `0x04b79075` | `0x04b79074` |
| 533 | `0x33f1c1b5` | `0x33f1c1b4` |

**(2) A value in a register is not an address in memory.** That equality says the *value* came from
somewhere our instrumentation put it; it says nothing about which memory the `pop` read. If the
wrappers' spill and the exit's frame slot were **different addresses**, the equality would still hold
and the cache story would be unnecessary — the pop would simply be reading a slot our own code wrote.
So the whole account turns on the addresses, and those are decidable from the ELF.

## 2. The addresses, computed

`tools/check_idle_frame_slot.py` walks `cpu_idle` and the four functions on the path as a running `sp`
offset from one base — `cpu_idle`'s `sp` at its three `bl`s — and records every store's offset and
every `pop`/`ldm sp!` that loads `pc`. On the frozen image:

```
== the three idle calls out of cpu_idle ==
  __wrap_platform_cache_idle_enter       sp offset from cpu_idle's entry: +8
  __wrap_cpu_idle_wfi                    sp offset from cpu_idle's entry: +8
  __wrap_platform_cache_idle_exit        sp offset from cpu_idle's entry: +8
== __wrap_platform_cache_idle_enter ==
  sp-12  r4   (strd r4, [sp, #-12]!)
  sp-8   r5   (strd r4, [sp, #-12]!)
  sp-4   lr   (str lr, [sp, #8])
== __wrap_cpu_idle_wfi ==
  sp-12  r4   (strd r4, [sp, #-12]!)
  sp-8   r5   (strd r4, [sp, #-12]!)
  sp-4   lr   (str lr, [sp, #8])
== __wrap_platform_cache_idle_exit ==
  sp-8   r4   (str r4, [sp, #-8]!)
  sp-4   lr   (str lr, [sp, #4])
  -> it calls platform_cache_idle_exit with sp at -8 (from the one base)
== platform_cache_idle_exit ==
  sp-16  fp   (push {fp, lr})
  sp-12  lr   (push {fp, lr})
  -> the pop takes pc from:  sp-12  (pop {fp, pc})

ok: the two idle wrappers' r4 spill and this exit's saved lr are the SAME address
```

**Step 1 is what makes the base well-defined, and it is asserted rather than assumed**: the three
`bl`s out of `cpu_idle` carry the same `sp` offset, so a wrapper's own entry `sp` *is* the base and
each function's offsets are directly comparable. `cpu_idle` writes its own frame words (`str r1, [sp]`
/ `[sp, #4]`) and that is fine — what the argument needs is only that `sp` does not *move* between the
three calls.

So the layout, in one base:

| address | written by | with |
| --- | --- | --- |
| `sp-12` | `__wrap_platform_cache_idle_enter`'s `strd` (first instruction) | `C = 1` |
| `sp-12` | `__wrap_cpu_idle_wfi`'s `strd` | `C = 0` |
| `sp-12` | `__wrap_platform_cache_idle_exit` → `platform_cache_idle_exit`'s `push` — the `lr` the `pop` loads | `C = 0` |
| `sp-4` | the exit wrapper's `str lr, [sp, #4]`, read back at `0x8047c9b8` after the return | `C = 0` store, `C = 1` load |

## 3. What the two together prove, and what they do not

The exit's `push` stores `lr` to `sp-12` while `SCTLR.C` is clear — `platform_cache_idle_enter`
clears it at `0x80046240-48` and the exit restores it at `0x80046320-24` — so that store goes to DRAM
and *is* there. The wrappers' store to the same address is the only other writer in the boot. So:

* if the `pop`'s lookup finds **no** cache line for `sp-12`, it reads DRAM and gets the `push`'s `lr`;
* the `pop` in both archived runs got the **spill's** value instead.

**Therefore a cache line older than the push answered the lookup.** That is the conclusion, and it is
forced rather than chosen: the only other way to see the spill's value at that address is for the
push not to have landed, and a `C = 0` store lands.

What it does **not** prove: *why* the line was stale, which is 534's question, and — the part this
step adds — **what made it dirty in the first place**. The line's stale content is not anonymous: it
is this project's own wrapper's registration save, at exactly the address the death names.

## 4. The correction: the enter path did not clean the L1, and the seam's comment says it did

`entry_seam_flush`'s own note reasons that its clean half is harmless:

> Its clean half has nothing to write back - the enter's `CleanPoU_Dcache` cleaned the whole L1 earlier
> in this same window - but that is a fact about the run, not an assumption the arm makes.

**Both halves of that sentence are wrong, and each is checkable in the image.**

`CleanPoU_Dcache` does exist and is called in this window — `platform_cache_idle_enter` calls it at
`0x80046274`, on the single-CPU arm — but:

1. **It runs after `SCTLR.C` was cleared.** The real enter's first four instructions are
   `mrc r0, cr1, cr0, {0}` / `bic r0, r0, #4` / `mcr r0, cr1, cr0, {0}` / `isb sy` at `0x8004623c-48`,
   and the `bl CleanPoU_Dcache` is at `0x80046274`. Whatever that call does, it does with the cache
   off.
2. **No set/way sweep in this image can select a level.** That is not a property of Apple's loop; it
   is a property of the architecture, and the image's own census is the check:

```
== every CSSELR writer in this image (mcr 15, 2, ...) ==
  0x8000497c  in entry_epilogue
  0x80022eb0  in machine_write_csselr
  -> neither is a cache routine, so no set/way sweep in this image selects its own level:
     each acts on whatever CSSELR holds, and XNU's `do_cacheid` leaves it at the L2.
```

   `CleanPoU_Dcache`'s whole body is one `mcr 15, 0, r0, cr7, cr10, {2}` loop — `DCCSW` by set/way —
   and `CleanPoC_Dcache`'s is two, with different geometry for each pass. Neither writes CSSELR. And
   XNU's only CSSELR writer is reached from `do_cacheid` (`osfmk/arm/cpuid.c:214`), which selects
   **L1**, then **L3 or L2**, and **never selects L1 back** (the source is quoted in 534 §2 and the
   ordering is `:224` then `:262`/`:265`).

**So the enter wrapper's `strd` at `0x8047c8d4` — its first instruction, executed with `C = 1` — is
the only thing in this window that dirties the line, and nothing in the window cleans or invalidates
the L1.** The seam's arm therefore does not face a clean line. It faces a **dirty** one, which changes
the direction of the operation: for the two words the pop reads, the *invalidate* is what repairs them
and a *clean* writes the stale spill over the pushed `lr`. For the line's other words the reverse is
true. The arm's design already knows the first half (step 4 restores the two words); what it did not
know is that the line is dirty at all.

### And a second live word in the same line, which the arm does not restore

`__wrap_platform_cache_idle_exit` stores its own `lr` at `sp-4` with `C = 0` and reads it back at
`0x8047c9b8` with `C = 1` — that read is *after* the real exit restored the cache. `sp-4` is in the
same 64-byte line as `sp-12`, and the wrappers dirtied that word too (`str lr, [sp, #8]`, with `C = 1`,
in the enter and wfi wrappers, writing *their* `lr`s). So a clean-and-invalidate at the seam writes
whichever of those the cache holds over the exit wrapper's own return address before the line is
dropped.

**That is a candidate for 535's non-return, and it has a shape.** The enter wrapper's `lr` is
`0x8000da2c` and the exit wrapper's is `0x8000da40` — the return addresses of the `bl`s at
`0x8000da28` and `0x8000da3c`. If the exit wrapper's `ldr lr, [sp, #4]` loads the **enter's** return
address, `b entry_note_pcx` sends control back into the `wfi` path: enter → wfi → exit → enter, a
window loop that never reaches `ClearIdlePop` and never returns. Marked as a **candidate, not a
measurement**: 572 §1 records that 535's run left no log, and 574's measurement arm reads only the two
words of the real exit's frame — `b0`/`b1` and `a0`/`a1` — so its pair is **blind to `sp-4`**, which is
the one word this candidate turns on. That blindness is the thing to fix before the arm is read as a
verdict on the operation.

## 5. 534's conclusion stands; its quoted measurement is from the wrong moment

534 §2 quoted `xnu_entry_csselr_before=0x00000002` from 520's capture as its measured evidence that
CSSELR is 2. It is a real reading and the value is right — but it is an **`xnu_entry_*` key**, i.e. it
is published during the **entry phase**, before XNU's `_start`, and it cannot speak to the level at the
idle exit. What carries 534's conclusion is its *structural* half: `do_cacheid` is the only CSSELR
writer XNU has, it ends on L2/L3, and nothing in the idle path writes the register. That half is
verified above and now checked.

This is the project's most-repeated defect in miniature — **a number from one moment read as a reading
of another** — inside the step that established the level. The conclusion survives; the citation is
corrected so the next reader does not re-derive the conclusion from a number that cannot support it.

## 6. The check, and how it is shown to fail

`tools/check_idle_frame_slot.py` asserts §2's equality and §4's census. It is deliberately not an ARM
decoder: it understands the forms these five functions use and **refuses** on anything else, so a
compile that changes shape is a refusal rather than a silent mis-parse. Since a check whose success is
silent cannot be told from one that never ran, it was exercised in both directions on doctored copies
of the frozen image (patched in `/tmp`, never in place — the park is a byte-exact record):

| image | what was changed | verdict |
| --- | --- | --- |
| the frozen arm | — | **EXIT=0**, prints the derivation above |
| the sleepless arm in `out/` | — | **EXIT=0**, same geometry |
| `/tmp/dA.elf` | the enter wrapper's `strd` to `#-16` | **EXIT=1**: "spills r4 at -16, the pop reads pc from -12" |
| `/tmp/dB.elf` | the exit wrapper's `str r4, [sp, #-4]!` | **EXIT=1**: "the pop reads pc from -4" |
| `/tmp/dC.elf` | a CSSELR write planted in `CleanPoC_Dcache` | **EXIT=1**: "a cache routine writes CSSELR at 0x80045760" |

`dC` is the one worth keeping: its first draft fired on the *count* of writers rather than on the
property, so a doctored cache routine was caught for the wrong reason. The predicate is now the name's
family (`Clean*`, `Flush*`, `clean_`, `flush_`, `Invalidate`, `platform_cache_idle…`), because the
disassembly labels these routines' inner loops with their own symbols (`clean_dcacheway`, `cudr_loop`)
and an exact list would miss exactly the file the check is about.

The tool also had two defects of its own while being written, both caught by running it: the
`cpu_idle` walk refused on `str r1, [sp, #4]` (a write to `cpu_idle`'s own frame, which moves nothing),
and the base accounting added `cpu_idle`'s `sub sp, sp, #8` twice, printing `-4` for an address at
`-12`. The second is the one worth recording: **the printed derivation is what made it visible**, and a
checker that prints its arithmetic can be read instead of trusted.

## 7. What this does not do

* **It does not fix the boot and it does not build an arm.** The two candidate repairs it sharpens —
  have the enter wrapper leave the line clean (so the seam's clean half is a pure invalidate), or make
  the seam restore every word of the line it cleaned rather than the two the real exit pushed — are
  *changes to `entry_trace.c`*, which is a build input, and a rebuild now would replace the sleepless
  arm the next power press is for (595 §1). So they are named and not built.
* **`entry_trace.c`'s comment is not edited either, and that is deliberate.** That file is covered by
  `xnu_arm_entry-sources.txt`, so editing it makes the entry image stale and the gate refuses at its
  freshness clause — **a comment edit would block the boot the parked arm is waiting for.** The
  correction lives here and is carried into the next arm as a requirement; the file is one rebuild
  away and the rebuild is the user's call.
* **It does not answer 572 §5's question** — whether the operation or the interception cost 535's
  return — beyond sharpening it: 574's arm is blind to the word the candidate turns on.
* **It is not a claim about the distance between `pc` and `rtcPop`.** The two readings agree to the
  mask bit and no further; `pc`'s bit 0 is set by the load, not by the stored value.

## 8. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. Reads of
the frozen and sleepless entry ELFs, `nm`/`objdump`/`readelf` over them, reads of two archived
captures, three doctored *copies* under `/tmp`, and one new host-side tool under `tools/`. The parked
frozen payload and the sleepless payload are unmodified, byte for byte — verified by re-running the
checker and the gate against them, not by assumption. `fastboot boot` only — never `flash` — so no
outcome of any of this can write to storage.
