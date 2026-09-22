# 549: which arm the window runs is a boot argument, not a property of the code

A host-side reading, no device and no build, from the other session's cross-check of 546 and my own bytes.
It corrects one table in 546, and its consequence is that **533's log can say which arm ran by itself**
rather than by inference.

## 1. The two arms are exact complements, and the selector is in the boot-args

`platform_cache_idle_enter` and `_exit` each begin with the same two-way test on the same two globals:

| site | test | taken when |
| --- | --- | --- |
| `0x8004625c` (`beq 0x8004627c`) | `up_style_idle_exit == 0` | the **else** arm |
| `0x80046270` (`bne 0x8004627c`) | `real_ncpus != 1` | the **else** arm |
| `0x80046274` | `bl CleanPoU_Dcache` | `up_style_idle_exit != 0 && real_ncpus == 1` |
| `0x800462ec` (`beq 0x80046304`) | `up_style_idle_exit == 0` | the I-cache/TLB pair |
| `0x80046300` (`bcc 0x8004630c`) | `real_ncpus < 2` | **skips** the I-cache/TLB pair |

and the two selectors are not equalities of the code:

| global | where | initial value | written by |
| --- | --- | --- | --- |
| `up_style_idle_exit` | `.bss` `0x805511a4` (so **0**) | 0 | `PE_parse_boot_argn("up_style_idle_exit", &0x805511a4, 4)` in the real `arm_init`, at `0x8000d180`-`0x8000d198` (`movw r5,#0x11a4` / `movt r5,#0x8055` / `bl PE_parse_boot_argn`) |
| `real_ncpus` | `.data` `0x80520378` | **1** (read out of the image: `01000000`) | the CPU census |

**And this image sets the boot argument.** The boot-args string is built in two places
(`stages/stage90/boot_args.c:48`, `stage90_main.c:797`) and **ends with `up_style_idle_exit=1`** - the
literal is present twice in `out/stage90/stage90.bin` - with a `_Static_assert` guarding the fit because
`cmd_len` silently takes the smaller of the two sizes and a 256-byte field would have dropped *the tail*,
which is exactly this token (515's note, `boot_args.c:31-52`). `arm_init` parses the string at
`0x80487b93` and writes the result to `0x805511a4`.

**So in this image:** the enter takes the `CleanPoU_Dcache` arm, the exit takes its `FlushPoU_Dcache` and
**skips** `InvalidatePoU_Icache`/`flush_core_tlb`, and `real_ncpus` is 1. The window's whole cache
inventory is two L1 D-side operations.

## 2. The correction to 546's table

546 §2 listed six calls as "every cache operation called inside the window" and said they invalidate "L1
lines only". Four of the six are on branches this image does not take, and two of the six are mutually
exclusive alternatives. The corrected table is in 546 §2 now, with a "runs in this image?" column; the
conclusion is unchanged and stronger - **the only lines invalidated in the window are L1 lines, and there
is no PoC flush in the window at all, taken or not** - and one detail sharpens: the enter's arm in this
configuration is a **clean**, not a flush, so the single invalidation in the window is the exit's.

Also corrected by the cross-check: `0x800457fc` is reached as `CleanPoC_DcacheRegion`, and
`LEXT(CleanPoC_DcacheRegion)` / `LEXT(CleanPoC_DcacheRegion_Force)` are **the same symbol**
(`caches_asm.s:216-219`), so a reader who knows only the `_Force` spelling would think two routines were
being called. Its body is a clean-by-VA to PoC, not a set/way sweep, as 546 already said - one correction
to the correction: the two immediates are the **image's**, `and r2, r0, #63` / `lsr r1, r1, #6`
(64-byte lines, i.e. `MMU_CLINE` 6), not `#31`/`#5`.

## 3. What this buys: 533 reports its own arm

`__wrap_platform_cache_idle_enter` loads **both** selectors and hands them to the note functions, whose
fields publish **by name** through `entry_live_write(name, value)`:

| site | what it reads | where it goes | keys |
| --- | --- | --- | --- |
| `0x8047c8f4` / `0x8047c8f8` | `[0x805511a4]`, `[0x80520378]` | `entry_note_pce` → `0x80006e40` | **`xnu_live_pce_up`**, **`xnu_live_pce_ncpu`** (with `_seq`, `_caller`, `_datap`, `_tpidrprw`) |
| `0x8047c948` / `0x8047c94c` | the same two words again | `entry_note_pce_after` → `0x8000703c` | **`xnu_live_pce_after_up`**, **`xnu_live_pce_after_ncpu`** (+ `_after_seq`, `_after_datap`, `_after_sctlr`, …) |

**And the returning run's own log already carries them, with the values §1 predicts.** Grepped out of
`/tmp/cancro-last_kmsg.txt` (520's run, lines 8300-8317):

```
xnu_live_pce_seq=0x00000001       xnu_live_pce_after_seq=0x00000001
xnu_live_pce_caller=0x8000d8cc    xnu_live_pce_after_tpidrprw=0xc0573df0
xnu_live_pce_up=0x00000001        xnu_live_pce_after_datap=0x8051a000
xnu_live_pce_ncpu=0x00000001      xnu_live_pce_after_up=0x00000001
xnu_live_pce_datap=0x8051a000     xnu_live_pce_after_ncpu=0x00000001
xnu_live_pce_tpidrprw=0xc0573df0  xnu_live_pce_after_sctlr=0x30c57879
```

**`_up = 1` and `_ncpu = 1` on both sides of the enter**, so 520's run took the single-CPU
`CleanPoU_Dcache` arm and skipped the exit's I-cache/TLB pair - the state 546's mechanism assumes, now
**measured rather than inferred from the boot-arg string**. (`_after_sctlr = 0x30c57879` is the same
in-window value 548 decoded: `C` clear, `TRE` set.) Three facts agree: the token is in the string,
`arm_init` writes it, and the run that came back reports it.

`_seq` publishes on the schedule `n == 1 || (n & (n-1)) == 0`, so a printed `1` means *the first* enter and
a printed `4` would mean *at least* four - measurement defect 406's tell, and nothing here uses the number
as a total.

**So the reading 533 needs is already being published, and its arm is the same tree with one wrapper switch
changed** - so the prediction is `_up = 1`, `_ncpu = 1` again. If the log shows `_up = 0` the enter took the
SMP arm and the mechanism under test is a different one; **if the keys are absent altogether that is itself
the answer**, because it puts the death before the enter wrapper's first `entry_live_write` rather than
inside the exit. That is the check that catches the arm being wrong for a reason nobody chose, which is this
project's most-repeated failure ([[mi4-off-option-two-spellings]],
[[mi4-self-written-record-is-not-a-constraint]]).

## 4. What it does not change, and one thing it does

**Does not change:** 546's mechanism. The push is still uncacheable, the pop is still cacheable 24 bytes
after the re-enable, and the L2 still receives nothing in the window. 548's attribute finding is untouched.

**Does change one candidate remedy, by removing it.** Flipping the boot-arg - dropping
`up_style_idle_exit=1` - switches the arms and so buys `InvalidatePoU_Icache` and `flush_core_tlb` back,
and picks up the enter's `FlushPoU_Dcache` + `CleanPoC_DcacheRegion` pair. **None of those is a D-side L2
operation**, so the boot-arg is not a lever on the mechanism: the L2 path stays unmaintained either way,
and the fix remains 535's PoC *flush* behind the exit's call site. Worth stating because a one-token
change to a string is the cheapest-looking lever in this phase, and it is the wrong one.

## 5. An instrument note, for the next reader

`llvm-objdump` without a triple decodes `xnu_arm_entry.elf` as **pre-ARMv7** - the ELF declares no
architecture in `e_flags` and its BuildAttributes block is empty - so every `movw`, `movt`, `ubfx` and
`dmb` prints as `.word` (118,621 of 1.38 M words; `movw`/`movt` count 0). Use `--triple=armv7-none-eabi`,
or `arm-linux-gnueabihf-objdump`, which is what the readings above used. This is
[[mi4-silence-is-a-reading-only-if-success-is-silent]] in the disassembler: a decoder that fails by
printing `.word` looks like an instruction that is not in the image.

## 6. Safety

No device action in this step. `objdump`, `grep` and one section dump over
`out/stage90/xnu_arm_entry.elf`, `out/stage90/stage90.bin` and the stage90 sources. Nothing written outside
`docs/` (and the two files 546's correction touches), no build run, no file under `out/` touched, `flash`
not used, frozen pair untouched (`1daaf44e624563694e…` / `f202f2465886aba6…`). The device is off the bus
and owes a power press before 533 can run.
