# 519: the idle thread gets a stack of its own

Stage90, host-side. `STAGE90_XNU_IDLE_STACK=1` (default), `STAGE90_XNU_ISTACK_SEPARATE=0`,
`STAGE90_XNU_EXIT_POC_FLUSH=0`. Device `4a2fe00b` untouched; nothing flashed, nothing booted.

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
