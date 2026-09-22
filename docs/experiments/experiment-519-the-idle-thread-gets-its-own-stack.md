# 519: the idle thread gets a stack of its own

Stage90. Built host-side with `STAGE90_XNU_IDLE_STACK=1` (default), `STAGE90_XNU_ISTACK_SEPARATE=0`,
`STAGE90_XNU_EXIT_POC_FLUSH=0`, then **run once on hardware** (§9) through the standing gate, one
non-persistent `fastboot boot`, nothing flashed and nothing written to storage.

The image this step builds is `out/stage90/stage90-qcdt.img`, sha256
`40bf8a0bc8cccc33f7e1bd765304364d99807319c006ce774f9394ebc34b66bc`, 8,540,160 bytes (`.text`
5,305,256, `.bss` 379,696 — the 518 image's `.bss` was 363,324 and this one carries the 16 KB array).
The copied entry image is `xnu_arm_entry.bin`, still **5,519,996** bytes: the array lands in `.bss`
above the pinned `__bss_start` the payload's copy span ends at, so the span 516, 517 and 518 all
measured is unchanged and that is not an error.

Two edits after the first green build were comments only (the mirror-safety comments in
`tools/assemble_arm_layer.sh` and in `tools/patch_idle_stack.py`, and one word in a comment here), and
the rebuild that proves it is in the record: the same four steps, byte-identical output —
`40bf8a0b...` both times.

## 1. The instruction, and how 518's own run identified it

518's arm moved `cpu_data->istackptr` by 8 KB and changed nothing, because that one field has two
readers that both want the stack they are about to use: the exception vectors place a *handler's*
stack from it (`locore.s:1361-1362`) and `cswitch.s`'s `Idle_context` places the **idle body's** stack
from the same field. `SS_SP == istackptr - 16` before and after the move is that fact written as a
subtraction, and it is why 518's run could not test its hypothesis.

The way out is not a third value of the pointer but a second *stack*, and the place to say so is
`Idle_context`, which is where the idle body is given one:

```
LEXT(machine_idle)                 machine_routines_asm.s:49-58
        cpsid   if
        mov     ip, lr
        bl      EXT(Idle_context)  <-- the idle body is entered *here*
        mov     lr, ip
        cpsie   if
        bx      lr

LEXT(Idle_context)                 cswitch.s:200-213
        mrc     p15, 0, r9, c13, c0, 4          // TPIDRPRW
        add     r3, r9, ACT_KVFP                // (VFP save, if configured)
        ldr     r3, [r9, TH_KSTACKPTR]          // the *thread's* pcb ...
        add     r3, r3, SS_R4
        stmia   r3!, {r4-r14}                   // ... gets r4-r14 and the caller's lr
        ldr     r12, [r9, ACT_CPUDATAP]
        ldr     sp, [r12, CPU_ISTACKPTR]        <-- the instruction 519 rewrites
        LOAD_ADDR_PC(cpu_idle)                  // `b cpu_idle`; cpu_idle is noreturn
```

Two things follow, and both are facts of the image rather than of the source:

* **The idle body runs on the interrupt stack from this instruction until `Idle_load_context`
  restores the thread's own `sp`.** `Idle_load_context` (`cswitch.s:224+`) reloads `r4-r14`, `sp`
  included, from `TH_KSTACKPTR` (`0x800fd020`, `ldm r3!, {r4-r14}`) — so the *exit* of the idle body
  is on the idle thread's kernel stack whatever `Idle_context` does, and 519 leaves it alone. That is
  the correction to 518's doc §5, which proposed wrapping `__wrap_Idle_load_context`: that hook
  restores `sp` from the thread's pcb, so it cannot choose the stack the idle body runs on.
* **The interrupted `sp` in 516's and 518's runs is that instruction's, plus 16 bytes.**
  `Idle_context` sets `sp = istackptr`; `cpu_idle` opens with `sub sp, sp, #8`
  (`0x8000d460`, this build — 516's and 518's docs name `cpu_idle` as `0x8000d424`, and the 518 run's
  own `xnu_live_sip_site` reading is `0x8000d460`, which is `cpu_idle`'s entry here); the exit wrapper
  pushes its own 8-byte frame; and the frame's `SS_SP` is the address the real
  `platform_cache_idle_exit` saw on entry. `SS_SP = istackptr - 16` in 516 and in 518 is exactly that
  sum, and no other path produces it: the boot's own `sp` comes from `start.s:310-311`
  (`ldr sp, <intstack_top>` then `sub sp, sp, #80`, `0x800002d4`) and `machine_idle`'s caller runs on
  the idle *thread's* stack. So 519's premise is not that the idle body *might* be there — the run
  that failed to falsify 518 says which stack it was on, and this is the instruction that put it
  there.

## 2. The collision, in the code's own terms

`fleh_irq_kernel` (`locore.s:1347-1377`) builds the 360-byte saved state **below the interrupted
`sp`** and only then takes the handler's stack (`ldr sp, [r9, ACT_CPUDATAP]` /
`ldr sp, [sp, CPU_ISTACKPTR]`). With the idle body on the interrupt stack, both stacks have the same
top, so the handler's pushes from `istackptr` downward run into the idle body's live frames:

| handler's push | address (518's run) | what is there |
|---|---|---|
| 1st .. 4th | `0x80515ffc` .. `0x80515ff0` | below the frame's `SS_SP`; the 4th ends at it |
| 5th, 6th | `0x80515fec`, `0x80515fe8` | the idle exit's pushed `{fp, lr}` |

and 518's run caught exactly that: the closing `pop {fp, pc}` of `platform_cache_idle_exit` returned
into a timebase value (`0x04e1ae60` into `pc`, `0x04e1ae61` into `fp` = the panic's `r11`) where the
function had pushed `{fp, 0x8047c8dc}`, with the frame itself authentic. 519 removes the shared top:
the handler keeps the whole 16 KB interrupt stack and the idle body runs on a 16 KB array of this
image's own.

## 3. The second effect: `ml_at_interrupt_context()` stops answering "yes" for the idle code

This is not a side effect that was hoped for; it is a property of two listings.

`ml_at_interrupt_context()` (`machine_routines.c:669-677`) reads the **current** `sp` (`mov %0, sp`)
and tests it against `cpu_data->intstack_top`. And `prefabt_from_kernel` (`locore.s:900-963`) builds
its frame on the interrupted stack and calls `sleh_abort` **without switching stacks** — only the
user-mode path loads `TH_KSTACKPTR`, and only the IRQ path loads `istackptr`. So for a kernel-mode
abort the predicate is a test of the *faulting* code's stack, a few hundred bytes below the
interrupted `sp`:

* idle body on the interrupt stack → inside `[intstack_top - 16384, intstack_top)` → `trap.c:312-313`
  `panic_with_thread_kernel_state("sleh_abort at interrupt context", regs)` — the run 516, 517 and
  518 all ended in;
* idle body on `stage90_idle_stack` (this image: `0x8054bee0` .. `0x8054fee0`, both ends above
  `intstack_top` = `0x80518000`) → **false** → `sleh_abort` proceeds as an ordinary kernel fault, and
  for a wild jump that is `panic_context(EXC_BAD_ACCESS, ..., "sleh_abort: prefetch abort in kernel
  mode: fault_addr=0x%x")` with the register panel (`trap.c:383-403`) — a *diagnosed* panic, not
  Apple's flat "at interrupt context".

The failure mode therefore improves rather than merely changing: the same corruption that killed
three runs would now name itself. The collateral is two readers that also change answer for a fault
in the idle path: `ml_stack_remaining()` (`machine_routines.c:987`) switches to its
`current_thread()->kernel_stack` base — valid for the idle thread, which has one — and
`backtrace_interrupted()`'s `assert(ml_at_interrupt_context() == TRUE)` (`backtrace.c:209`) is the one
place written on the old assumption; it is a debug-path assertion, and the run either takes it or
does not.

## 4. What changed, and the two defects the change found

`tools/patch_idle_stack.py` rewrites the one line, in the object the assembler produced, under a
switch in `tools/assemble_arm_layer.sh` (`STAGE90_XNU_IDLE_STACK`, default 1; `..._SIZE`, default
16384, passed to the assembler as a define). The substitution is the kernel's own non-slidable idiom
written by hand, because `LOAD_ADDR(sp, EXT(x))` glues `L_` onto the whole string and LLVM rejects the
variant `L_EXT(x)`:

```
        ldr     sp, [pc]                        ; 800fd014 <L_stage90_idle_stack_top>
        b       cpu_idle
L_stage90_idle_stack_top:
        .word   0x8054fee0                      ; stage90_idle_stack + 16384
```

The literal goes after the tail branch (which `LOAD_ADDR_PC` makes unconditional; the patch refuses a
body whose last instruction is not a branch), the array is declared in `entry_stubs.c`
(`stage90_idle_stack`, 16 KB, `aligned(16)`), and the patch is **bounded to `Idle_context`**:
`Shutdown_context` keeps its own `ldr sp, [ip, #4]` (`0x800fcfd0`), because it wants the interrupt
stack on purpose.

Two defects were found on the way, and neither is in the arm:

* **The tool wrote XNU's tree, and my own comment said it could not.** `assemble_arm_layer.sh`'s
  mirror is directories that are real and files that are symlinks into the tree, so `open(dst, 'w')`
  on a path that is still a symlink writes `external/xnu-4570.1.46/osfmk/arm/cswitch.s`. It did, on
  the tool's first run. The write now goes through a temporary and `os.replace` (`rename(2)` replaces
  the symlink instead of following it) — the same mechanism that makes the translate step's `mv -f`
  safe, and the comment in `assemble_arm_layer.sh` that claimed the mirror was safe by construction
  now says where its safety actually comes from. The tree was restored and `git -C
  external/xnu-4570.1.46 status --short` is empty.
* **518's flag-off image had never been built, and did not build.** `STAGE90_XNU_ISTACK_SEPARATE=0`
  left `want` unused under `-Werror`, and 518's own clause then stopped the build for a body that was
  correct ("never materializes 0x80516000"). Both are the same claim — 518b's "`=0` still restores
  517b's arrangement" — that no build had tested. 519 is the first step that *needs* the flag-off
  image, because an arm that changes where the idle body runs has to be comparable with one that does
  not, which is how a two-step-old defect surfaced. `want` is now declared inside the flag and the
  value's absence in the flag-off body is asserted rather than assumed.

## 5. The reading, and the control

`entry_idle_stack_note()` (`entry_stubs.c`, called from `__wrap_platform_cache_idle_enter`, one site,
before 516's write-back and before the real enter disables the D-cache) is the first C on the stack
`Idle_context` chose. It reads `sp` from the register and publishes six live keys per pass of the
idle loop:

| key | what it is |
|---|---|
| `xnu_live_idlestack_sp` | the idle body's `sp` — the claim itself |
| `xnu_live_idlestack_top` | `stage90_idle_stack + 16384`, so `sp` can be placed by subtraction |
| `xnu_live_idlestack_istackptr` | `cpu_data->istackptr` — the handler's stack, read live |
| `xnu_live_idlestack_intstacktop` | `cpu_data->intstack_top` — the field the kernel's predicate reads |
| `xnu_live_idlestack_inwin` | `ml_at_interrupt_context()`'s predicate on this `sp`: 1 or 0 |
| `xnu_live_idlestack_calls` | how many passes have taken the reading |

**The switch is the control.** With `STAGE90_XNU_IDLE_STACK=0` the same six keys report the
arrangement 516, 517 and 518 ran — `sp` inside the interrupt stack and `inwin` equal to `calls`, one
per pass — which is a measurement of the collision that 518's arm could only infer. With it on, `sp`
is inside `stage90_idle_stack` and `inwin` should be 0 over every pass. The two fields are published
separately on purpose: 518's arm moved `istackptr` and not `intstack_top`, so the predicate kept
answering about the region that had not moved — a run that reads only one of the two cannot say that.

## 6. What the build asserts

The `xnu_entry_519` clause in `build_entry.sh` (and `519:` in `assemble_arm_layer.sh`) makes the
following facts of the artifact rather than claims in this document: the substitution is bounded
(`Shutdown_context` still loads `[ip, #4]` exactly once); the array's size in the linked image equals
`entry_stubs.c`'s macro and the assembler's define; `Idle_context` has no `ldr sp, [r12, #4]` and does
have a reference to `L_stage90_idle_stack_top` whose `.word` is exactly `stage90_idle_stack + size`
(compared as a *number*, after the first version of the check compared text and objdump's zero-padding
would have failed a correct image); the **whole array** is outside
`[intstack_top - INTSTACK_SIZE, intstack_top)`; both flags' off-shapes are asserted in the mirror
direction; the six record keys are in the entry image; `entry_idle_stack_note` exists, reads `sp`,
builds `BootCpuData`'s address, loads `[#4]` and `[#8]` from it (accepting the folded shape as well,
which is what the first version of that check got wrong in the opposite direction from 518b's), and
subtracts this configuration's `INTSTACK_SIZE`; and `__wrap_platform_cache_idle_enter` calls it
exactly once.

## 7. The prediction, written before the run

Two readings decide it, and they are independent:

1. `xnu_live_idlestack_sp` is inside `[stage90_idle_stack, +16384)` and `xnu_live_idlestack_inwin` is
   0 for every pass. (If `sp` is still inside the interrupt stack, the patch did not reach the
   instruction the boot takes — the arm did nothing.)
2. The run does **not** end in `panic: sleh_abort at interrupt context`. If the collision was the
   only thing stopping the idle path, the idle loop continues and the boot proceeds past it. If it
   ends in `sleh_abort: prefetch abort in kernel mode`, then the collision is gone and something
   *else* is also writing the idle body's saved `{fp, lr}` — and the run names the address it jumped
   to instead of leaving a frame that 518's arm could not distinguish from an authentic one.

The second branch is why this arm is worth running even if the first is all it shows: it converts a
verdict the project could not read for three steps into a message the next step can act on. The
first branch is the one that decides whether 519 was built at all.

## 8. Safety

Host-side only: no device step in this part of the work. The arm changes where a stack is, and the
arrangement it produces is one the function it patches was written to allow — `Idle_context`'s own
save/restore pair brackets exactly this region, and 16 KB is the size of the stack it borrowed. The
run, when it happens, goes through `stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`run_and_capture.sh --allow-xnu-entry`, one non-persistent `fastboot boot`, **nothing flashed and
nothing written to storage** — so the brick half of the standing constraint ("一定要保证不要让设备彻底
死机或者变砖") holds by construction, as it has for every run since 506.

## 9. The run

2026-09-22. Gate green (it printed `40bf8a0b...`, the file's own hash, and `found 4a2fe00b in adb`),
one non-persistent `fastboot boot` (`Sending 'boot.img' (8340 KB) OKAY` / `Booting OKAY`), the device
returned inside the capture window on its own, and `/tmp/cancro-last_kmsg.txt` came back at 596,958
bytes with 3,932 `MI4IOS6_STAGE90` lines and 4,264 `xnu_live_*` records, ending `No errors detected`.

**Prediction 1 obtained.** The six keys, verbatim:

```
xnu_live_idlestack_sp=0x8054fea8          inside [0x8054bee0, 0x8054fee0)
xnu_live_idlestack_top=0x8054fee0         the array's top, as entry_stubs.c computes it
xnu_live_idlestack_istackptr=0x80518000   the handler's stack, read live
xnu_live_idlestack_intstacktop=0x80518000 the field the kernel's predicate reads
xnu_live_idlestack_inwin=0x00000000       ml_at_interrupt_context() is false here
xnu_live_idlestack_calls=0x00000001       one pass in the whole boot
```

The idle body ran on the array, outside the window the predicate tests, and the arm reached the
instruction the boot takes. The reading also cross-checks the geometry from the live side: the note is
called from the enter wrapper with its body `sp`, so `sp = 0x8054fea8` puts the enter wrapper's frame at
`[0x8054fec0, 0x8054fed8)` and the array's top at `0x8054fee0` — hence `cpu_idle`'s `sub sp, sp, #8` at
`0x8000d460` lands at `0x8054fed8`, the exit wrapper's `str r4, [sp, #-8]!` puts the real exit's entry
`sp` at **`0x8054fed0`**, and that is exactly the `sp` the panic below prints. Two independent sources,
one number.

**Prediction 2 obtained, in its second branch.** The fatal abort is the *diagnosed* one:

```
panic(cpu 0 caller 0x80454584): sleh_abort: prefetch abort in kernel mode: fault_addr=0x7152a6c
r0:   0x8051a000  r1: 0x00000001  r2: 0xde500000  r3: 0x0008a400
r4:   0x07152a6d  r5: 0x8051a000  r6: 0x8051a0e0  r7: 0x00000000
r8:   0x80553520  r9: 0xc04990d0 r10: 0x800ba588 r11: 0x07152a6d
r12:  0xde58a40a  sp: 0x8054fed0  lr: 0x800462dc  pc: 0x07152a6c
cpsr: 0x800000b3 fsr: 0x00000005 far: 0x07152a6c
Attempting system restart...MACH Reboot
```

so `ml_at_interrupt_context()` answered **false** for the faulting stack and §3's second effect is a
property the hardware confirmed: 516, 517 and 518 all ended in Apple's flat
`panic: sleh_abort at interrupt context`; this one names the address and prints the panel. The frame is
authentic — `xnu_live_sleh_frame_ok = 1`, `_user = 0`, `_fsr_frame = 5`, `_storm = 9`, `_seen = 9` (the
fatal abort is the ninth of the boot; the boot had already reached `load_init_program` and `/sbin/launchd`
by then, so the earlier eight are the boot's own page-in faults, not idle-pass events).

**And the panel is the idle path's own context, one instruction before the `pop`** — which is new
information rather than a restatement, because every field can be traced to an instruction in this image:

* `sp = 0x8054fed0` is the exit's entry `sp` from the live chain above, which is also `pop {fp, pc}`'s
  *post*-pop value (the writeback is part of the instruction and the branch follows it);
* `lr = 0x800462dc` is the return address of the exit's own `bl FlushPoU_Dcache` at `0x800462d8`, and the
  exit never overwrites LR again — reachable only through the `bcc 0x8004630c` at `0x80046300`, which
  skips both later `bl`s;
* `r0 = 0x8051a000`, `r1 = 1` are the operands of the exit's last store, `str r1, [r0, #0x130]`
  (`0x8004632c`..`0x80046338`);
* `r5 = 0x8051a000` (`cpu_data`) and `r4 = 0x07152a6d` are `cpu_idle`'s own registers at the call:
  `ldr r5, [r1, #1484]` at `0x8000d470`, and `ldm r6, {r4, r7}` at `0x8000d4b8` with `r6 = r5 + 0xe0` from
  `0x8000d4b0`, i.e. `r4 = cpu_data->rtcPop`, the deadline `cpu_idle` compares against its own `lastPop`.

So the two words the `pop` read are `fp = 0x07152a6d` and `pc = 0x07152a6c`: two counter readings **one
tick apart**, the later one in the lower word — and the later one is the very value the panel's `r4`
holds, i.e. the deadline the idle loop was working with. The idle body's stack is no longer shared with
anything, and the corruption still lands in the exit's saved `{fp, lr}`.

## 10. The frame arithmetic, read off this image, and what it rules out

`EXC_CTX_SIZE` is not a guess: `genassym.c:188` defines it as
`sizeof(arm_saved_state) + sizeof(arm_vfpsaved_state) + VFPSAVE_ALIGN` = `80 + 264 + 16 = 360`, and the
image agrees — `fleh_irq_kernel` (`0x8001a9e8`) opens `cpsid i,#19` / `sub sp, sp, #360` /
`stm sp, {r0-r12}` / `str r0, [sp, #52]` (SS_SP = `sp + 360`) / `str lr, [sp, #56]`, and
`prefabt_from_kernel` (`0x8001a5e0`) builds the same 360 bytes on the interrupted stack. The VFP area is
placed by `add r0, sp, #80` / `bic r0, #15` / `add r0, #16` and is 264 bytes
(`arm_vfpsaved_state` = `uint32_t r[64]; uint32_t fpscr; uint32_t fpexc`, `thread.h:81`).

Two consequences, and both are arithmetic on the image rather than assumptions about it:

* **No exception frame can write above the `sp` it interrupted.** The VFP area ends at
  `base + align16(base + 80) + 16 + 264`, and `base = sp - 360`, so it ends at or below `base + 360 = sp`.
  Everything the vector writes is inside `[sp - 360, sp)`. 518's mechanism — the handler's pushes from
  `istackptr` downward reaching the idle code's saved `{fp, lr}` — is therefore *not reachable in this
  image even in principle*, and with `STAGE90_XNU_ISTACK_SEPARATE=0` the handler's top is
  `0x80518000`, 8 KB above the array's top, besides.
* **The top 8 bytes of a frame are slack.** When the alignment works out as it does for `sp = 0x8054fed0`,
  the VFP area ends 8 bytes below the frame's top — and the exit's `{fp, lr}` slot *is* those 8 bytes.
  The frame reserves that memory and leaves it alone, so a value found there is the interrupted code's
  own, which is what makes §9's reading of `r0`/`r1`/`r4`/`r5` an identification rather than an
  interpretation: there was nowhere for a spill to have come from.

That narrows the question the next arm has to answer. The writers the idle path in this image actually
contains, and what each writes into the two words in question:

| writer | where it stores | what |
|---|---|---|
| `platform_cache_idle_exit` `0x800462d4` | `push {fp, lr}` at `sp = 0x8054fed0` → `[0x8054fec8, 0x8054fed0)` | `{cpu_idle's fp, 0x8047c964}` — **the slot**, and the only writer of both words after the pass starts |
| `__wrap_cpu_idle_wfi` `0x8047c884` | `strd r4, [sp, #-12]!` at `sp = 0x8054fed8` → `[0x8054fecc, 0x8054fed4)` | `(cpu_idle's r4, cpu_idle's r5)` — the slot's **upper** word holds the deadline `0x07152a6d` |
| `__wrap_platform_cache_idle_enter` `0x8047c8d4` | same idiom, same address | same |
| `__wrap_platform_cache_idle_exit` `0x8047c958` | `str r4, [sp, #-8]!` → `[0x8054fed0, 0x8054fed8)` | one word, above the slot |
| `entry_note_pcx` `0x800071b8` (tail-called with `sp = 0x8054fed8`) | 32-byte frame `[0x8054feb8, 0x8054fed8)`; `strd r8, [sp, #16]` | `(sctlr, tpidrprw)` — **on the slot** |

Every one of them runs *before* the exit's `push` in a pass, and none of them writes two successive
counter readings (the two `strd`s store the deadline and `cpu_data`; `entry_note_pcx` stores two small
integers). So the enumeration that the disassembly supports does not produce the pair, and the geometry
says no exception frame can either. **The step's conclusion is therefore not a mechanism but a
measurement**: the writer has to be watched, not reasoned about — and the slot is narrow enough (two
words, one fixed address derived from the array's top) that watching it is a cheap instrument.

## 11. What the next arm publishes

Five two-word readings, live (the channel survives a fatal panic — that is why these keys are read
where they are, not from a console epilogue), each with the pass count and `sp`:

1. in `__wrap_platform_cache_idle_exit`, **before** its `bl platform_cache_idle_exit`, the two words at
   `[sp-8, sp)` — what the real exit's `push` is about to overwrite and its `pop` will read;
2. the same two words **after** the call returns, i.e. the control: on a surviving pass they are the
   pushed `{fp, lr}` (`0x8047c964`-shaped), and if the run dies in the `pop` this reading never arrives;
3. in `entry_note_sleh`, the two words at `[sp-16, sp-8)` and at `[sp-8, sp)` of the aborted context, so
   a fatal run reports what the `pop` read beside what the panel already says;
4. `__wrap_ml_get_timebase`'s own readings — 517's instrument is still in the image and is the only code
   on this path that could read the counter twice in a row, which is exactly the shape of the pair;
5. `cpu_data->rtcPop` (`+0xe0`, the value `cpu_idle` keeps in `r4`) and the idle thread's saved `sp`/`lr`
   from its pcb (`TPIDRPRW + 1480` = `TH_KSTACKPTR`, the saved state at `+16`), so the run can say whether
   the value that was jumped to is the deadline the idle loop computed.

The decision rule is written into the arm: if reading 1 is already the timebase pair, the writer wrote
*before* the exit ran and the exit's own `push` cannot be what the `pop` read — which would mean the
dying `pop` belongs to a different frame than the push that should have filled it. If reading 1 is clean
and the run still dies in the `pop`, the writer ran between the `push` and the `pop`, and §10 says the
only thing that can run there is an exception — whose frames are now bounded exactly.

