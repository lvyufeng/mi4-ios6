# Experiment 516 — the write-back reaches the Point of Coherency, and the halt is reached

**One line.** 515 asked for the window to be measured from inside it with instructions that do not need
the cache, and named the discriminator: read the *same* expression on both sides of the real
`platform_cache_idle_enter`. 516 does that, and its answer is the one that says the repair is sound —
`xnu_live_pce_after_datap = 0x8051a000`, read with `SCTLR.C = 0` (`xnu_live_pce_after_sctlr =
0x30c57879`), is **the same value the cache-on read gave**. But the step's own build check found the
reason the first attempt at it could not work: on this CPU the Point of Unification is the **L2**, and a
`CleanPoU_Dcache` — Apple's own up-arm clean at `caches.c:415` — writes back only as far as the L2, so a
read taken with the D-cache off still gets DRAM. The wrapper therefore calls **`CleanPoC_Dcache`**, the
kernel's own function, whose only difference is the second loop over the L2's geometry. With that,
`platform_cache_idle_enter` *returns*, **the halt is reached for the first time in this walk** —
`xnu_live_wfi_seq = 1`, `_inst = 0xe320f003`, `_ticks = 0x0005aefc` and `0x0006eded` in two runs, i.e.
the CPU was really given up for **19.4 ms** and **23.4 ms** — and the run then ends in Apple's own panic
rather than in 515's 64-record storm:

    panic(cpu 0 caller 0x804542dc): sleh_abort at interrupt context (saved state:0x80517e88)
    r0:   0x8051a000  r1: 0x00000001  r2: 0xde500000  r3: 0x0008a31d
    r4:   0x0723c1fa  r5: 0x8051a000  r6: 0x8051a0e0  r7: 0x00000000
    r8:   0x8054f4a0  r9: 0xc05feb10 r10: 0x800ba588 r11: 0x0723c1fa
    r12:  0xde58a327  sp: 0x80517ff0  lr: 0x800462dc  pc: 0x0723c1f8
    cpsr: 0x80000093 fsr: 0x00000005 far: 0x0723c1f8

`lr` is `0x800462dc` = **`platform_cache_idle_exit + 8`**, the instruction right after that function's
own `bl FlushPoU_Dcache` — the exit is entered, and its first act completes, in **both** runs. `sp` is
`0x80517ff0`, the **interrupt stack** (`L_intstack_top` is `0x80518000` in this same image), and
`ml_at_interrupt_context()` — which is `sp ∈ (intstack_top − INTSTACK_SIZE, intstack_top)`
(`machine_routines.c:669-677`) — is what turned the abort into a panic: the fault was taken **inside the
interrupt**, where XNU's own trap code refuses to handle it (`trap.c:312-313`). And `pc = far` is **the
CPU's own timebase**: `0x0723c1f8` is 29 ticks below the same run's own post-`wfi` reading
(`xnu_live_wfi_after = 0x0723c215`), and run 2's is 28 ticks below its own — a value that moves with
uptime and sits where a code address belongs. The site is Apple's exception return:
`return_from_irq` (`0x8001ab10`) → `load_and_go_sys` (`0x8001a860`), whose tail is
`ldr lr, [sp, #60]` / `ldm sp, {r0-r12}` / `movs pc, lr` (`0x8001a944-0x8001a94c`) — so the `pc` it
returned to is **whatever the interrupt frame's `SS_PC` slot held**, and the register dump above is
exactly that frame's `r0..r12`.

## What the step was for

515's own "what is owed" named three readings of one expression in three cache states, and said what
each answer would mean:

    (a) the enter wrapper's, before the real call, cache on      0x8051a000   measured by 515
    (b) right after the real enter returns, cache still off      ?            this step's discriminator
    (c) the exit wrapper's, after caches.c:490 sets C again      ?            this step's other end

and the alternatives: "(b) is the discriminator the step does not have: if it is `0x8051a000`, the
clean does restore the field and the fault above is a race with a window a few instructions wide; if it
is 0, the clean does not, and the port's `CleanPoU_Dcache` is the thing to look at." Alongside it: read
`TPIDRPRW` as well, because "that is the alternative explanation this step cannot exclude: that the read
is 0 because `TPIDRPRW` no longer points at the thread whose field the wrapper read."

**(b) is `0x8051a000`, and `TPIDRPRW` is unchanged** — `xnu_live_pce_tpidrprw ==
xnu_live_pce_after_tpidrprw == 0xc04750d0` in run 2 and `== 0xc05feb10` in run 3, i.e. the cache-off
read and the cache-on read agree about *both* halves of the expression: the thread pointer the CPU
reads with `mrc p15, 0, rX, c13, c0, 4` and the `cpu_data` it reaches through it at
`ACT_CPUDATAP = 1484`. So the window's own write-back does put the field in DRAM, a read with `SCTLR.C =
0` sees it, and the fault 515 stopped on is not a race with a window a few instructions wide.

## What the step changed

**The write-back, and the reason it cannot be Apple's own clean.** `entry_trace.c`'s
`__wrap_platform_cache_idle_enter` gains one call, between the record and the call that opens the window:

    entry_note_pce(caller, up, ncpu, datap, entry_tpidrprw(), before);
    CleanPoC_Dcache();                       /* 516: the write-back, cache still on */
    __real_platform_cache_idle_enter();
    entry_note_pce_after(entry_tpidrprw(), entry_cpu_datap(), up_style_idle_exit, real_ncpus,
                         entry_sctlr());

The first version of this step used `CleanPoU_Dcache`, which is the obvious mirror of what the kernel
itself calls at `caches.c:415` — and run 1 of this image is what says that is the wrong function, so the
wrong-arm run is kept rather than discarded:

* run 1 (`CleanPoU_Dcache`): `sleh_pc = 0x20400000`, `sleh_far = 0x20400000`, `sleh_lr = 0x80046278`
  = `platform_cache_idle_enter + 0x40`, i.e. the `b` after `bl CleanPoU_Dcache` **inside the enter** —
  a *data*-shaped stop whose `far` is a literal that the **return-address slot itself** had been
  written with, before the halt was ever reached;
* run 2 and run 3 (`CleanPoC_Dcache`): the enter returns, the `wfi` runs, the exit's own `bl` completes,
  and the stop is the exception return above.

The two functions are the same loop with one difference, which is the whole of this step:

    CleanPoU_Dcache (0x800457a8)   mov r0,#0; mcr p15,0,r0,cr7,cr10,{2} ... bx lr
    CleanPoC_Dcache (0x8004575c)   ... the same loop ...; mov r0,#2; mcr p15,0,r0,cr7,cr10,{2}
                                   ... the same loop again, with the L2's geometry (0x40000 / 0x20000000)
                                   ...; dsb sy; bx lr

`clr7,cr10,{2}` is `DCCSW` — clean, by set/way, **no invalidate** — so `CleanPoU_Dcache` leaves the L1
holding clean copies of everything it wrote back, and on a CPU whose Point of Unification is the L2 the
data it moved has reached the L2 and no further. `CleanPoC_Dcache` is the same instruction over the L1's
geometry and then over the L2's. **That is why the first attempt made the stop worse rather than
better**: the L1-only clean converted the field's newest value into a clean L1/L2 line and left DRAM
holding the older one, and the window's own cache-off read then got the *older* one — which is 515's
"counter that recedes", now arranged deliberately by the step that was trying to fix it.

**Three more readings, all taken with a coprocessor read or a value, never with the cache.**
`entry_note_pce_after(tpidrprw, datap, up, ncpu, sctlr)` is published from the far side of the window's
own `platform_cache_disable()`, and `entry_note_pcx` now also carries `tpidrprw`, `datap` and `sctlr`
from the exit. All three of the new left-hand values are things a cache cannot answer wrongly:
`mrc p15, 0, rX, c13, c0, 4` (TPIDRPRW), `ldr rX, [rX, #1484]` (the field the thread points at, which
the write-back is *about*), and `mrc p15, 0, rX, c1, c0, 0` (SCTLR, whose bit 2 is the statement "the
cache is off right now"). `pce_after_sctlr` is the reading that dates the window: it is `0x30c57879`
with bit 2 clear, so the reading was taken while the D-cache really was disabled — which is what makes
`pce_after_datap` a statement about what a cache-off read gets.

## What the build checks

`build_entry.sh`'s `xnu_entry_516` clause asserts, from the linked image rather than from the sources:

* `CleanPoC_Dcache` (`0x8004575c`) is the kernel's **own** function — one definition in the image, not
  in pass 1's undefined set, and *not itself wrapped* — and it is called **exactly once**, at
  `2152187504`, **before** the call that opens the window at `2152187508`, i.e. while `SCTLR.C` is still
  set;
* the difference is measured rather than asserted: `CleanPoC_Dcache`'s own body contains **2** `DCCSW`
  loops against `CleanPoU_Dcache` (`0x800457a8`)'s **1**. Counting a function body's instructions needs
  the next *global* label, and this check's first version got that wrong twice — `sym_next` landed on a
  *local* label four bytes past the function's start, so the range was empty and the count was 0, and the
  fix (`next_global`) then returned the start address itself, because without `-S` `nm` prints
  `address type name` and the boundary must be **strictly greater** than the start (`clean_mmu_dcache`
  shares `0x8004575c` with `CleanPoC_Dcache`). Verified by hand: `CleanPoC_Dcache
  0x8004575c..0x800457a8 loops=2`, `CleanPoU_Dcache 0x800457a8..0x800457d0 loops=1`;
* the two functions `platform_cache_idle_enter` (`0x80046238..0x800462d4`) still calls were not touched:
  **exactly one** `CleanPoU_Dcache` and **exactly one** `FlushPoU_Dcache` inside it, so the entry-side
  repair is additive and Apple's own arm is still there to be read;
* the wrapper reads `getCpuDatap()` twice — `mrc cr13,cr0,{4}` followed by `ldr [rX, #1484]` — and
  `SCTLR` once (`mrc ..., cr1, cr0, {0}`), and the exit wrapper reads `SCTLR` **after** its call to the
  real exit (`2152187596`), which is the reading that says the cache is back on by then;
* the eight keys that carry the two sides of the comparison (`pce_tpidrprw`, `pce_after_tpidrprw`,
  `pce_after_datap`, `pce_after_up`, `pce_after_ncpu`, `pce_after_sctlr`, `pcx_datap`, `pcx_sctlr`) are
  present in the image's own `.text` — `objcopy -O binary --only-section=.text` then `strings`, because
  a whole-ELF count also sees `.debug_str` and `.strtab` copies that are not in the image.

Two further defects this step's checks found are worth recording because both were silent: objdump
spells coprocessor registers `cr1, cr0`, not `c1, c0`, so a matcher written for `mrc 15, 0, r0, c1, c0,
0` matched nothing and the check "the wrapper never reads SCTLR" was a fact about the regular
expression; and `--wrap` renames a symbol **for the linker only**, so the disassembly still names the
real function `<platform_cache_idle_enter>` and a matcher written for `<__real_...>` fails on the image
that is correct.

No new `--wrap` is added: the census stays at **75** — 63 reached by a branch, 1 same-object-only
(`_ZN9IOService12matchPassiveEP12OSDictionaryj`), 1 never called here (`sleep`), 10 by address only.
129 fixture mutations refused; `xnu_entry_failures = 0`.

## The readings

Two runs of this image, `/tmp/516-run2-kmsg.txt` (596867 bytes) and `/tmp/516-run3-kmsg.txt` (596731).
Run 1 (`/tmp/516-run1-kmsg.txt`, 571903) is the `CleanPoU_Dcache` image and is cited above as the
discarded arm.

                                  run 2                     run 3                    meaning
    xnu_live_pce_seq              1                         1                        the enter wrapper ran once
    xnu_live_pce_caller           0x8000d07c                0x8000d07c               = cpu_idle+248, the bl at +244
    xnu_live_pce_up               1                         1                        up_style_idle_exit, cache ON
    xnu_live_pce_ncpu             1                         1                        real_ncpus, cache ON
    xnu_live_pce_datap            0x8051a000                0x8051a000               (a) getCpuDatap(), cache ON
    xnu_live_pce_tpidrprw         0xc04750d0                0xc05feb10               the thread, cache ON
    xnu_live_pce_after_seq        1                         1                        the far side of the window
    xnu_live_pce_after_tpidrprw   0xc04750d0                0xc05feb10               unchanged: no thread switch
    xnu_live_pce_after_datap      0x8051a000                0x8051a000               **(b) the same value**
    xnu_live_pce_after_up         1                         1                        read with C = 0
    xnu_live_pce_after_ncpu       1                         1                        read with C = 0
    xnu_live_pce_after_sctlr      0x30c57879                0x30c57879               bit 2 clear: cache really off
    xnu_live_wfi_seq              1                         1                        the halt was reached
    xnu_live_wfi_fast             1                         1                        the wrapper's own fast path
    xnu_live_wfi_inst             0xe320f003                0xe320f003               `wfi`, from the image
    xnu_live_wfi_before           0x062da8bc                0x071cd428               the clock, before
    xnu_live_wfi_after            0x063357b8                0x0723c215               the clock, after
    xnu_live_wfi_ticks            0x0005aefc                0x0006eded               19.4 ms / 23.4 ms asleep
    xnu_live_pcx_seq              absent                    absent                   the exit wrapper never returned
    xnu_live_sleh_storm           9                         9                        64 in 515: the panic path
    xnu_live_sleh_seen            9                         9                        64 in 515
    xnu_live_sleh_lr              0x800462dc                0x800462dc               **identical: the exit+8**
    xnu_live_sleh_sp              0x80517ff0                0x80517ff0               **identical: the istack**
    xnu_live_sleh_cpsr            0x80000093                0x80000093               **identical: SVC, I and F**
    xnu_live_sleh_fsr_frame       0x5                       0x5                      translation fault, section
    xnu_live_sleh_frame_ok        1                         1                        the frame agrees with the registers
    xnu_live_sleh_user            0                         0                        kernel mode
    xnu_live_sleh_pc              0x0633579c                0x0723c1f8               **the timebase, at the fault**

**The stop is a reading and not a guess, because three of its fields are identical to the byte across
two runs and the fourth moves exactly as uptime moves.** `lr`, `sp`, `cpsr` and `fsr` are the same
numbers in both logs; `pc` is `0x0633579c` at 5.41 s and `0x0723c1f8` at 6.24 s, and in each run it is
**28 and 29 ticks below that same run's own `xnu_live_wfi_after`** — 1.5 µs, at 19.2 MHz. A code address
that a fault could land on would not scale with elapsed time; a timebase does, by construction.

### The panic names the site, and Apple's own test is what decides it

`panic(cpu 0 caller 0x804542dc): sleh_abort at interrupt context (saved state:0x80517e88)` is Apple's
own text and Apple's own test:

    osfmk/arm/machine_routines.c:669   ml_at_interrupt_context()
    osfmk/arm/machine_routines.c:674       __asm__ volatile("mov %0, sp" : "=r"(stack_ptr));
    osfmk/arm/machine_routines.c:675       intstack_top_ptr = getCpuDatap()->intstack_top;
    osfmk/arm/machine_routines.c:676       return ((stack_ptr < intstack_top_ptr) && (stack_ptr > intstack_top_ptr - INTSTACK_SIZE));
    osfmk/arm/trap.c:312               if (ml_at_interrupt_context())
    osfmk/arm/trap.c:313                       panic_with_thread_kernel_state("sleh_abort at interrupt context", regs);

so the panic is a statement about a register, not an interpretation: the abort handler was running with
`sp` inside the boot CPU's interrupt stack. The frame agrees from three directions — `saved state
0x80517e88`, the recorded `sp` `0x80517ff0`, and this step's own epilogue keys
`xnu_entry_panic_args_page = 0x80517000` and `xnu_entry_panic_ap = 0x80517cb4` — against a stack whose
top the image itself declares as `L_intstack_top = 0x80518000` (`ml_get_timebase`'s neighbour at
`0x8001720c`, which is also where `SS_SP`'s `0x80517ff0` sits: 16 bytes below the top).

The register dump is therefore the interrupt frame's own `r0..r12`, and it is the same frame
`load_and_go_sys` had just loaded from:

    8001a914:  ldr  lr, [sp, #56]     ; SS_LR  - the interrupted lr
    8001a918:  mov  ip, sp
    8001a91c:  ldr  sp, [ip, #52]     ; SS_SP  - so the return lands on the interrupted stack
    8001a920:  cpsid if, #23          ; abort mode, so the msr below writes the SPSR the return will use
    8001a928:  ldr  r4, [sp, #64]     ; SS_CPSR
    8001a92c:  msr  SPSR_fsxc, r4
    8001a940:  bl   vfp_load
    8001a944:  ldr  lr, [sp, #60]     ; **SS_PC - this is the value that becomes the pc**
    8001a948:  ldm  sp, {r0-r12}
    8001a94c:  movs pc, lr

`lr`, `sp` and `cpsr` in the panic dump are not the frame's saved values at all — they are the *banks*
the return switched back into (`movs pc, lr` restores the CPSR from the SPSR, and SVC's `lr` bank still
holds the instruction the interrupt was taken at, while SVC's `sp` bank still holds the interrupt
stack it was running on). That is why the dump shows `lr = 0x800462dc` — an address inside
`platform_cache_idle_exit` — next to `sp = 0x80517ff0` — an address on the interrupt stack: the frame's
`SS_PC` slot is the one word that was wrong, and everything else about the state is what it should be.

**What the log says about where the interrupt was taken is exactly consistent with that.** `cpu_idle`
calls its three stubs at `0x8000d07c` (the enter), then `__wrap_cpu_idle_wfi`, then
`__wrap_platform_cache_idle_exit`, so a run that publishes `pce`, `pce_after` and `wfi` and no `pcx` has
taken its fault inside the exit call — and `lr = 0x800462dc` = `platform_cache_idle_exit + 8` places it
at the instruction *after* that function's first act, `bl FlushPoU_Dcache` (`caches.c:460`). The exit's
own flush is the last thing that provably completed; the interrupt arrived at the next instruction, and
the value it left in the frame's pc slot is a timebase.

### The rest of the run is unchanged, and that is the point

`xnu_live_poll_seq` is 1 and 2 (515's park at 3 and 4 is still not reached, so the park's real call has
still not returned), the captured console still holds **three** `mini4:` lines and none of the five that
are printed after it, `xnu_live_ostext_chars` is `0x3ee` (1006) with `_heals = 1` and `_at =
0x000483cc`, and `xnu_entry_panic_len = 0x13fa` (5114) with `_dropped = 0` — 461's separate panic buffer
holds the whole message, which is why this step has a register dump to read at all.

**But the run's character has changed, and the change is the good kind.** 515's runs ended in the abort
storm running to its cap of 64 and the OS never finding out; 516's runs end in an *ordered* stop: one
abort, nine records, Apple's own `panic()`, the register dump, `Attempting system restart...MACH Reboot`,
and the device back on Android. That is `sleh_abort` doing what it is written to do with a fault taken
at interrupt context, and it is the first time in this walk that the OS has had anything to say about
the failure it stopped on.

## What is owed

* **517: measure the frame slot, and repair the exit the way 516 repaired the enter.** The one word that
  is wrong is the interrupt frame's `SS_PC`. It can be read from where it matters: `fleh_irq_handler`
  stores the frame pointer into `cpu_data->cpu_int_state` (`[cpu_data + 0xb0]`, `locore.s:1403`) and
  `return_from_irq` clears it (`locore.s:1433`) — so a wrapper on `ml_get_timebase`, which the handler
  calls at `0x8001aae4` *after* the dispatch and *before* the return, can publish `[getCpuDatap() + 0xb0]`
  and then that frame's own `SS_R0..SS_VADDR` words together with the timebase the wrapper is returning.
  **If the frame's `SS_PC` slot equals the timebase the wrapper just read, the writer is named** — the
  entropy stir, which stores `r0` (the timebase, xored with the pool word) through the *pointer* held in
  `EntropyData[0]`, and reads both with the cache off. And the repair to try first is the exact mirror of
  this step's: `FlushPoC_Dcache()` (L1 **and** L2, clean and invalidate) in
  `__wrap_platform_cache_idle_exit` **before** `__real_platform_cache_idle_exit()`, so the exit's own
  L1-only `FlushPoU_Dcache` at `caches.c:460` is no longer the only flush on the way out. Its falsifier
  is sharp: if the same `lr = 0x800462dc` / `sp = 0x80517ff0` / timebase-`pc` signature survives it, the
  L2 story is dead and the next candidate is the frame write itself.
* **The register dump has numbers this step cannot yet explain, and they are owed an explanation.**
  `r4 = r11 = pc + 2` in **both** runs, `r0 = r5 = 0x8051a000` and `r6 = 0x8051a0e0` (i.e. `cpu_data` and
  `cpu_data + 0xe0`), `r2 = 0xde500000`, `r8 = 0x8054f4a0` and `r10 = 0x800ba588` identical across two
  runs, and `r3`/`r12` differing by a few hundred (`0x0008a3a5`/`0x0008a31d`,
  `0xde58a327`/`0xde58a3af`). A pair of callee-saved registers holding the faulting address **+2** is a
  shape worth naming before it is explained.
* **Carried, unchanged**: the park's `w_calls`/`w_exits`/`w_sip`/`w_wfi`/`w_sleep` totals are still
  unmeasured (514's own content); the OS's own reboot path (`reboot_kernel` → `host_reboot` →
  `halt_all_cpus` → `PEHaltRestart`) is still unwired *as a path this port can take on purpose*, though
  this run shows the panic's way out reaching `MACH Reboot` on its own; 513's other two repairs (install
  a handler in the IPI slot at `machine_routines.c:605`, or accept the spin); the captured console's
  census question (1006 chars, `_heals` 1, `_at` 0x000483cc); 512's list (the telemetry copy loop on
  pid 1's own thread, `_cpsr = 0x10` on the AST records, `_entry_hi`), 508's a-record-that-cannot-be-lost,
  507/506's `p->p_xstat`/empty `xnu_entry_why`/`trap record:` gate, 505's corpse-path slot `0x802933b4`,
  504's `mdevadd_base`/`mdevopen`/second `read`, 503's leeway row, 502's long list; the driver clause and
  a non-watchdog self-sustaining OS.

## The image

`.text` 5,296,832 → **5,297,664** (+832: the write-back call, the six `pce_after_*` readers, the three
`pcx_*` readers and their keys), entry image **5,519,996** bytes — unchanged, and *not* an error: the
copied span is `[0x80000000, __bss_start)` and `__bss_start` is pinned at **0x80543a80** in both images,
so growth in `.text` eats the fill below `.data` rather than moving the end. `.bss` `0x80543a80` …
`0x8059c530` = **363184** bytes, entry point `0x80000074`, tree at `0x806e0000` + `0x744c`, boot args at
`0x8059e000`, `topOfKernelData` `0x80700000`, headroom 1,456,848 bytes, 3129 MB of image-alias headroom.
Wrap census **75**. The symbols this step reads: `CleanPoC_Dcache` `0x8004575c`, `CleanPoU_Dcache`
`0x800457a8`, `FlushPoU_Dcache` `0x80045874`, `platform_cache_idle_enter` `0x80046238`,
`platform_cache_idle_exit` `0x800462d4`, `cpu_idle` `0x8000cf84`, `ml_get_timebase` `0x80017174`,
`fleh_irq_handler` `0x8001aa70`, `return_from_irq` `0x8001ab10`, `load_and_go_sys` `0x8001a860`,
`fleh_irq` `0x8000a6f8`, `L_intstack_top` `0x80518000`, `EntropyData` `0x805264fc`; the three wrappers at
`0x8047c628`, `0x8047c6ac`, `0x8047c5dc`. Payload sha256
`75f182b867e1b4afc3bc6f8f5fcbed8a9fcd9da5622af8a828aff1a94948b6db` (`stage90-qcdt.img`), `a15ef093…`
(`stage90.bin`), `1b9d357e…` (`stage90.elf`), `52bc9c35…` (`stage90_fixture.macho`). 129 fixture
mutations refused; `xnu_entry_failures = 0` (`xnu_entry_panic_len` `0x13fa`, `_dropped` 0, `_caller`
`0x804542dc`, `_args_ok` 1).

## Safety

Two runs, both non-persistent `fastboot boot` through `stages/stage90/preflight_boot_check.sh
--allow-xnu-entry` and `stages/stage90/run_and_capture.sh --allow-xnu-entry`, nothing flashed, gate and
run both exiting 0, and **the device back on Android on its own both times** (`MI 4LTE`, release 10,
`adb devices` reporting `4a2fe00b device`). This step is a better test of that property than the last
several: its run reaches a real `panic()` inside the kernel's interrupt path, and the OS's own panic path
answers it — `Attempting system restart...MACH Reboot` is in the log — so the net that matters here was
the kernel's own, with the hardware watchdog behind it exactly as the gate's own text says.

    python3 tools/check_experiment_index.py    # ok: 493 row(s) across 91 stage column(s)
