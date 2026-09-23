# 590: our own wrapper writes the word the `pop` reads, and the `pc` in the dump is a fetch address

Two corrections and one new lead. The corrections are mine (585's and a claim I made to the user), and
the lead is the first since 535 that puts a *writer* at the slot the `pop` reads, with a measured
address behind it.

Host-side only. No device action, no build, no arm: disassembly of the frozen entry image
(`151425c4…`) plus the archived 520 capture. The arm in `out/` is byte-identical and unrun.

## 1. The three idle wrappers share one `sp`, and `bl` does not move it

Read out of `out/stage90/xnu_arm_entry.elf` - `cpu_idle` at `0x8000d934`:

```
8000da24:  bl kpc_idle
8000da28:  bl 8047c8d0 <__wrap_platform_cache_idle_enter>
8000da2c:  movw r0, #0
8000da30:  movt r0, #32850
8000da34:  ldr  r0, [r0]
8000da38:  bl 8047c884 <__wrap_cpu_idle_wfi>
8000da3c:  bl 8047c964 <__wrap_platform_cache_idle_exit>
```

The three instructions between the first and the second call are `movw`/`movt`/`ldr` - **none of them
writes `sp`** - and `bl` does not write `sp` either. So all three wrappers are entered with the same
`sp`, which the rest of this document calls `S`. (The `wfi` wrapper's epilogue returns it: `add sp, sp,
#12` / `ldrd r4, [sp]` / `add sp, sp, #8` / `pop {pc}` = `S-24 -> S-12 -> S-4 -> S`.)

`cpu_idle` also has `sub sp, sp, #8` at `0x8000d934`, so `S` sits 8 bytes below `cpu_idle`'s own entry
`sp`; nothing between the three calls changes that.

## 2. `r4` is `cpu_idle`'s `rtcPop` at both wrappers, at the instruction level

```
8000d98c:  add  r6, r5, #224          ; r6 = cpu_data + 0xe0
8000d98c:  ldm  r6, {r4, r7}          ; r4 = [cpu_data + 0xe0]
```

`r5` is `cpu_data` (`0x8000d944`: `ldr r5, [r1, #1484]` where `r1 = TPIDRPRW`). `224 = 0xe0` is
`STAGE90_CPU_RTCPOP` (`entry_stubs.c:6220`), the offset the rtc note reads the deadline from - so **`r4`
is the low word of that 8-byte pair, i.e. `rtcPop`**, the value 585's table prints as `rtcpre_pop`.

**And `r4` is not written again between there and the `bl` to the `wfi` wrapper.** The intervening
instructions write `r0`, `r1`, `r2`, `r3`, `ip` and `lr` only; the calls (`pmap_switch_user_ttb`,
`arm_debug_set`, `blx r3`, `clock_absolutetime_interval_to_deadline`, `timer_resync_deadlines`,
`__wrap_SetIdlePop`, `kpc_idle`) are AAPCS, under which `r4` is callee-saved and therefore preserved by
the compiler's own contract - which is exactly what `0x8000da14` relies on, comparing the freshly read
pair against `r4`/`r7` and calling `SetIdlePop` only when they differ.

So at the enter wrapper's first instruction, and again at the `wfi` wrapper's, **`r4` holds the idle
deadline**.

## 3. Both wrappers store that doubleword at `S-12` - and `S-12` is the word the `pop` reads as `pc`

```
8047c8d4 <__wrap_platform_cache_idle_enter>:
8047c8d4:  strd r4, [sp, #-12]!       ; sp = S-12;  [S-12] = r4, [S-8] = r5
8047c8d8:  str  lr, [sp, #8]          ; [S-4] = lr
8047c8dc:  sub  sp, sp, #12           ; sp = S-24

8047c884 <__wrap_cpu_idle_wfi>:
8047c884:  strd r4, [sp, #-12]!       ; same three statements
8047c88c:  str  lr, [sp, #8]
8047c890:  sub  sp, sp, #12
```

and the exit wrapper (583 §1) leaves `sp = S-8` and hands the real exit `sp = S-8`, whose
`push {fp, lr}` writes `[S-16] = fp` and **`[S-12] = lr`** - the same address the enter and `wfi`
wrappers wrote `r4` into, twice, at the top of the same pass.

**So two of this project's own instructions store the idle deadline into the exact word the `pop` loads
as `pc`, in every pass, before Apple's `push` overwrites it.** That is a *writer* at the slot, named by
address, in the image - not a cache hypothesis.

There are four `strd r4, [sp, #-12]!` sites in the whole image (`8047b504`, `8047c884`, `8047c8d4`,
`8047d3d8`); the two above are the ones whose `sp` is `S`.

## 4. Correction to 585: the `pc` in the dump is a fetch address, not a second memory word

585 §2 read 520's abort as **two memory words one apart** - "`fp` is exactly `rtcpre_pop`", "`pc` is that
value minus one" - and concluded that "what the `pop` read was not stale stack content at all, but a pair
of `cpu_data`-derived values one apart".

The dump is:

```
r11: 0x04b79075 ... pc: 0x04b79074
cpsr: 0x800000b3  fsr: 0x00000005  far: 0x04b79074
```

`pc` in a **prefetch-abort** dump is the address the CPU was *fetching* when the abort was taken, and
`far` is that same address. If the `pop` loaded `pc` = `0x04b79075` - the value with **bit 0 set**, i.e.
a Thumb target - the CPU clears bit 0 and fetches `0x04b79074`, which is exactly what the dump reports
for `pc` and `far`. So `0x04b79074` is `[S-12] & ~1`, **not** `[S-12]`; the word itself is
`0x04b79074` *or* `0x04b79075`, and the second is `r4`.

`r11 (fp)` is a plain register value after the pop, so `[S-16] = 0x04b79075 = rtcPop` is a real memory
reading - and it is `r4`'s value too.

The project had this relationship in hand and then read past it: 519's record says the pair is "`pc =
0x04b79074 = rtcPop - 1`, the Thumb-address relationship 519 read off its own `r4`/`far` pair". A
Thumb-address relationship is a statement about **one** value in two spellings. 585 turned it into two
values in two words.

## 5. The new lead: the store is unaligned, and ARMv7 makes that UNPREDICTABLE

`strd r4, [sp, #-12]!` with an 8-byte-aligned `S` targets `S-12`, which is `4 mod 8` - **an unaligned
doubleword store**. `LDRD`/`STRD` require doubleword alignment on ARMv7; an unaligned one is
UNPREDICTABLE (it is not in the list of instructions that may be unaligned, unlike `LDR`/`STR`/`LDM`).
Two implementations are therefore both legal:

- **store at `S-12`/`S-8`** (the offset as written): `[S-12] = r4 = rtcPop`;
- **align down to `S-16` and store there**: `[S-16] = r4 = rtcPop`, `[S-12] = r5`.

Under the first, the `pop`'s `pc` word is the deadline only if the push's `lr` store fails to reach the
line - 546's cache story. **Under the second, `r4` lands at `[S-16]` - the word the `pop` loads as
`fp` - and 520 measured `fp = 0x04b79075`, which is `r4`.** That is one of the two measured words
explained *without* a cache theory at all, by an unaligned store our own wrapper issues twice a pass.

**This is a hypothesis and is marked as one.** The part that is measured is: `r4 = rtcPop`; both wrappers
issue that store; `S` is shared; `[S-16] = rtcPop` and the `pc` word is `rtcPop` or `rtcPop-1`. What it
does not yet say is which of the two legal behaviours this part has, and that is decidable **on the
bench**: a two-instruction arm that issues the same `strd` and reads both words back says it, and costs
one boot of a payload that need only print two numbers.

## 5a. Addendum (590a): the align-down reading is largely dead, and the reason is the store's second register

Checked before it reached an arm, which is the only reason this is a paragraph and not a lost boot.

`strd r4, [sp, #-12]!` stores **`r4` and `r5`** - `STRD` takes consecutive registers. And in `cpu_idle`,
`r5` is the **`cpu_data` pointer**, not a second deadline word:

```
8000d944:  ldr  r5, [r1, #1484]      ; r5 = cpu_data   (r1 = TPIDRPRW)
8000d984:  add  r6, r5, #224         ; r6 = cpu_data + 0xe0
8000d98c:  ldm  r6, {r4, r7}         ; r4 = [cpu_data+0xe0] = rtcPop;  r7 = the high word
```

So under the **align-down** reading (`r4` at `S-16`, `r5` at `S-12`), the word the `pop` loads as `pc`
would be `cpu_data` = **`0x8051a000` in 520's run** - a perfectly valid kernel address, and the pop would
have returned to it instead of taking a prefetch abort at `0x04b79074`. **It aborted.** So align-down
cannot explain the `pc` word, and §5's "one of the two measured words explained with no cache theory" is
withdrawn for the `pc` word. It survives only as a caveat about what ARMv7 permits for `LDRD`/`STRD`,
not as an explanation of this death.

**And the aligned-as-written reading is now the one the measurement fits**, which is the sharper half:
it puts `r4 = rtcPop` at **`S-12`** - exactly the word the `pop` loads as `pc`, whose measured value is
`0x04b79074`/`0x04b79075` = `rtcPop - 1`/`rtcPop`. 546's cache story is what the push's `lr` store then
has to lose to, which is where this project already was.

### 5b. The abort's register file carries the instrument's own values, and one of them does not fit

520's log publishes `rtcpre_datap = 0x8051a000`, `rtcpre_thr = 0xc0573df0`, `rtcpre_pop = 0x04b79075`,
and the abort's dump reads **`r5 = 0x8051a000`, `r9 = 0xc0573df0`, `r11 = 0x04b79075`** - the same three
values, in registers. `r6 = 0x8051a0e0` is `cpu_data + 0xe0`, i.e. `cpu_idle`'s own `r6` from
`0x8000d984`. So the dump's registers are the idle pass's, and `r11` holding `rtcPop` is consistent with
the `pop` having already committed its `fp` load (`LDM sp!, {r11, pc}` writes `r11` before `pc`) - 585's
"`[S-16] = rtcPop`" reading, which stands.

**What does not fit is `r4`.** `cpu_idle` sets `r4 = [cpu_data + 0xe0] = rtcPop` at `0x8000d98c`, nothing
in `cpu_idle` or in any of the three idle wrappers writes `r4` again (`__wrap_platform_cache_idle_enter`
and `__wrap_cpu_idle_wfi` save it with the same `strd` and restore it with `ldrd r4, [sp]`; the exit
wrapper only stores it), so `r4` should still be `rtcPop` at the abort. **It is `0x8054fed0`, which is
`sp`** - the frame slot's own address, not a deadline. Either the abort handler's own prologue touches
`r4` before the frame it prints is saved, or something in the pass clobbers it; **this document does not
decide which, and it is recorded as open rather than resolved.** It matters because 519's chain ("the
writer is the `strd` because `r4` holds the deadline") is a statement about `r4` at the *store site*,
which the disassembly above confirms, and not about `r4` at the *abort*, which it does not.

## 6. What this does to the next arm

535's candidate was a *cache operation* at the seam, and 535's run did not come back. This is a different
axis: **the writer**, not the maintenance. The candidate repairs are all in our own code, and each is
smaller than 535's:

- do not store at that offset at all in the two idle wrappers (a frame layout that keeps the deadline
  out of the exit's `{fp, lr}` slot);
- or, if the store stays, have the exit wrapper **invalidate** that one line after the push (invalidate,
  not clean - a clean leaves the line valid and the `pop` still hits it).

The unaligned-store reading that §5 raised is withdrawn as an explanation by §5a; what is left of it is a
note about the instruction, not a lead about this death.

Either way the first thing to settle is which behaviour the part implements, because under the align-down
reading there is nothing for a cache repair to fix.

The phase still runs one arm at a time, so **nothing here is built**: 574 is frozen and its result is
what chooses between this axis and 535's. This step exists so that if 574's `b1` comes back as anything
other than `0x8047c990` - i.e. the push's store did not reach the word the `pop` reads - the next arm is
already designed and grounded in addresses rather than in a mechanism.

## 7. Correction to a claim I made to the user

Told to the user while reading the 520 doc, and it was stronger than the evidence: *"that wrong value is
not Apple's, it is our own wrapper's prologue spilling `r4`"*. What the artifacts support is the
*mechanism* (our wrapper stores `r4 = rtcPop` at exactly the word the `pop` reads as `pc`, in the image,
by address), and that 520's `fp` word **is** `rtcPop`. What they did not support, and what I stated as
settled, is that this *is* the explanation of the measured value - 585 explicitly left that provenance
open, and §5 above is a hypothesis rather than a finding. Recorded here rather than only in the
transcript because the same sentence will be quoted back when the next arm is chosen.

## 8. Safety

No device action, no `fastboot`, no `adb`, nothing written to storage, no build input, no arm: two
`objdump` runs over the frozen entry image, one `readelf`, one read of an archived capture, and one
`grep` of the source for the `RTCPOP` offset. **The arm in `out/` is byte-identical and unrun.**

**The boot still waits only on the user's power press** (Vol-Down + Power, into fastboot, because this
phone's Android does not reliably settle). TWRP stays withheld:
「如果os已经能进去了的话」 is unmet.
