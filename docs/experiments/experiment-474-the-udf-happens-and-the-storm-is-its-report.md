# Experiment 474 — the `udf` happens, and the storm is its own report

**473 stopped on an abort it could not name.** Its record read `DFSR`/`DFAR` out of `cp15` and nothing
else, so the run knew *where* the fifth abort faulted (`DFAR` was cut by the `SLEH_LIVE_MAX` cap, so it
knew neither) and not *what instruction* did it. This step gives that record six words of the frame the
vector hands it, and the reading is the opposite of the one 473 assumed: **the fifth abort is not a
`copyin` of a user address at all. It is the fault handler itself — this image's own undefined-
instruction handler — running in UND mode and reading address 0.**

**The step's content is one argument and one check.** `__wrap_sleh_abort` already receives Apple's
`struct arm_saved_state *regs`; `entry_trace.c` now passes it to `entry_note_sleh`, and
`stages/stage90/xnu_arm_boot/entry_saved_state.h` holds the six offsets it is indexed by (`SS_SP` 52,
`SS_LR` 56, `SS_PC` 60, `SS_CPSR` 64, `SS_STATUS` 68, `SS_VADDR` 72). The live channel gains eight
per-entry keys (`xnu_live_sleh_pc/lr/sp/cpsr/fsr_frame/far_frame/frame_ok/user`) for the first eight
aborts, which is where the whole result below comes from.

**And the offsets are not transcribed from Apple's header — they are checked against it.** The image
reads `struct arm_saved_state` in three places that must agree: Apple's
`osfmk/mach/arm/thread_status.h`, this configuration's *generated* `assym.s`
(`out/xnu_assym/STAGE90_XNU/assym.s`, which is Apple's own `genassym.c` compiled against that header),
and this project's `entry_saved_state.h` — plus a fourth: `stages/stage90/xnu_arm_boot/assym.s`, a
hand-written stand-in that `start.s` assembles against and that is **first on the entry build's include
path**, i.e. a second copy of the same layout in the one place a drift would be invisible.
`tools/check_saved_state_offsets.py` parses all four, plus `proc_reg.h` for the mode constants, and
refuses the link if any disagrees — including the *cross-derivation* `ACT_PCBDATA_PC - ACT_PCBDATA ==
SS_PC`, which is the same statement reached from the thread's PCB instead of from a stack frame.
`--selftest` mutates each source in memory and requires the refusal. Measured in the build:

    xnu_entry_474: struct arm_saved_state is read at the offsets Apple declares, this configuration's
    assym.s gives and entry_saved_state.h writes (SS_PC = 60, SS_CPSR = 64, SS_STATUS = 68,
    SS_VADDR = 72, and the PCB's own +PC and +R0 agree), so the record's pc is the faulting
    instruction and its cpsr is the mode it ran in

**The control is what makes the frame readable rather than merely plausible.** `regs->fsr` and
`regs->far` are filled by `mrc p15,0,.,c5,c0`/`c6,c0` in the vector — the same two coprocessor registers
`__wrap_sleh_abort` reads for itself — so on any abort whose `DFSR`/`DFAR` are already known, the frame
must report the same pair. Entries 1 to 4 are exactly those aborts, and all four read `frame_ok =
0x00000001`:

    seq 1  dfsr 0x805  dfar 0x00001000  pc 0x800119c0  lr 0x80288ccc  sp 0xc80d3e0c  cpsr 0x60000013
    seq 2  dfsr 0x807  dfar 0xc80fd000  pc 0x800079c4  lr 0x8028a680  sp 0xc80d3d48  cpsr 0x20000013
    seq 3  dfsr 0x807  dfar 0xc813e000  pc 0x80007690  lr 0x8003f4a0  sp 0xc80d3844  cpsr 0x60000013
    seq 4  dfsr 0x805  dfar 0x00101f28  pc 0x800119c0  lr 0x8028a03c  sp 0xc80d3c04  cpsr 0x60000013

The four pairs are this run's, not 473's — the addresses move between runs (473's third was
`0xc8146000`, this run's is `0xc813e000`) and *that is the point*: the control is the two numbers
agreeing **inside one run**, not any particular value. The four `pc`/`lr` pairs are the exec's own
copyout path — `Lcopyout_wordwise_loop` (`machine_routines_asm.o`) with `load_init_program_at_path`
above it, `L_64loop` (`bzero.o`) with `exec_save_path`, `L64loop` (`bcopy.o`) with `copypv`, and
`Lcopyout_wordwise_loop` again with `exec_copyout_strings` — with `cpsr` = SVC in all four, i.e. the
kernel path. (**Corrected in 477**: this sentence first listed the two middle pairs one position apart
from the table above it. Which loop goes with which caller was right and which *entry* each pair belongs
to was not; the table's own `lr`s — 0x8028a680 = `exec_save_path` on seq 2, 0x8003f4a0 = `copypv` on
seq 3 — and the two loops' source files (`bzero.s:104` is `L_64loop`, `bcopy.s:88` is `L64loop`) settle
it, and 476's and 477's runs read the same two pairs in that order.)

**One derived detail is what makes `pc` mean "the faulting instruction".** `dataabt_from_kernel`
stores `lr` twice: once into `SS_LR` while still in SVC mode (the aborting context's `lr`, which is why
entries 1-4 name the *callers* above), and once into `SS_PC` after `cpsid i, #23` has switched to ABT
mode — where `lr` is `lr_abt`, i.e. the faulting instruction, because the vector's `sub lr, lr, #8` has
already been applied. Read as straight-line code the second store looks like it must be the return
address of the `bl vfp_save` that sits between them; the `cpsid` is the whole difference, and the four
`pc` values above are the falsifier: none of them is `0x800147b8`.

## Entry 5 is the trap's own handler, and it read address 0

    xnu_live_sleh_pc        = 0x80002088   = entry_word_at, its first instruction: ldrb r2, [r0]
    xnu_live_sleh_lr        = 0x000010e0
    xnu_live_sleh_sp        = 0xc045ec50   = the thread's kernel stack (not sp_usr)
    xnu_live_sleh_cpsr      = 0xa000009b   = mode 0x1b = UND, I set
    xnu_live_sleh_fsr_frame = 0x00000007   = iDFSR: translation fault, page, read
    xnu_live_sleh_far_frame = 0x00000000   = address 0
    xnu_live_sleh_frame_ok  = 0x00000001   = the control holds on the frontier entry too

`far = 0` says the pointer handed to `entry_word_at` was **0**. `cpsr = 0x9b` says the abort was taken
while the CPU was in UND mode — so it was taken inside the undefined-instruction handler, which this
image owns (vector slot 1 is still `fleh_undef` at `0x80006638`, not `locore_fleh_undef` at
`0x80014024`), and that handler runs in the mode it was entered in and on the UND bank's stack.
`pc = entry_word_at` says the read is the handler's own.

**`lr = 0x000010e0` is the second witness, and it is the answer to 473's question.** The kernel path
stores the SVC bank's `lr`, and `return_to_user_now` (`osfmk/arm/locore.s`) loads the user's `pc` into
`lr` before the exception return: `ldr lr, [sp, #60]` (`0x80015038`), then `ldm sp, {r0-r12, sp, lr}^`
— the `^` form, which writes `lr_usr` and leaves `lr_svc` alone — then `movs pc, lr` (`0x80015044`). So
after every return to user mode `lr_svc` **is the user's `pc`**, and a fault taken from user mode's
successor reports it. `0x10e0` is the RAM disk's own entry: `__TEXT`'s file `[0, 4096)` maps to
`[0x1000, 0x2000)`, `pc 0x10e0` is file offset `0xe0`, and the word there is `udf #0` — the marker the
RAM disk Mach-O was built with. **So process 1 did enter user mode and execute its first instruction,
and 473's predicted event is measured after all — from the SVC bank rather than from the trap report.**

**Why the handler read 0.** The instrument's `fleh_undef` is the vector entry for *every* undefined
instruction, and its argument walk was written for one of them: `panic()`'s. `r_args` is the trapped
context's `r8` (the compiler treats `r8` as callee-saved and the exception mode banks no `r0`-`r12`, so
the C function reads the interrupted `r8` — `mov r7, r8` at `0x800066ec`, straight into the guard), and
for panic's own `udf` that word holds `panic`'s live `va_list *`. For process 1's `udf` it holds *the
user's `r8`*, which was 0. The guard then does what a guard that tests *shape* does with a zero:

    800067c0: tst  r7, #3                  ; 0 is 4-aligned          -> pass
    800067e0: tst  r3, r1   (r1=0xFFFFF000)  ; (0+31)^0 = 31, page-fit -> pass
    800067e8: and  r1, r1, r7                ; publishes args & ~0xFFF = 0
    800067f8: cmp  r6, r7   (r6=0x80000000)  ; entry_image_ptr(0)      -> no
    80006800: mov  r0, r7                    ; r0 = 0
    80006804: bl   80002088 <entry_word_at>  ; <- the fault

and reads address 0.

**The sentence that is the defect is in the guard's own comment**, at
`entry_stubs.c:4580-4583`: *"`xnu_entry_panic_args_page` is the page base this window was accepted on,
and zero there means the reads below did not happen."* It does not: `entry_panic_args_page(0)` returns 1
— the window `[0, 32)` is inside one page — so an **accepted** NULL publishes the same `0` as a
refusal, and the key cannot tell the two apart. That is one more instance of the shape this project
keeps paying for: a *statement about what a number means* that the reader is expected to accept
instead of a reading that distinguishes the cases (`mi4-measurement-defects.md`). The rule the
guard needed is the one it says out loud one paragraph earlier — *"the address is not a guess: `r_args`
is `panic`'s live frame address"* — and 474 falsifies the premise: the pointer can come from a
**user**-mode `udf`, where the CPU is not executing on that stack at all.

## Entries 6 to 8: Apple's own `sleh_abort`, recursing on a NULL map

That single fault in UND mode goes to `locore_fleh_dataabt` (slot 4, Apple's, 467) and reaches
`sleh_abort` at `0x8044ab18`, which is where the storm comes from:

    seq 6  pc 0x8044b0b4  lr 0x8044ad80  sp 0xc045ea08  cpsr 0x93  fsr 0x05  far 0x00000028
    seq 7  pc 0x8044b0b4  lr 0x8044ad80  sp 0xc045e7b8  cpsr 0x93  fsr 0x05  far 0x00000028
    seq 8  pc 0x8044b0b4  lr 0x8044ad80  sp 0xc045e568  cpsr 0x93  fsr 0x05  far 0x00000028

`fsr = 7` is a VM fault, so `sleh_abort` takes its `COPYIN((user_addr_t)(regs->pc), &ins, 4)` arm — and
`lr = 0x8044ad80` is precisely the instruction after `bl copyin_kern` at `0x8044ad7c`, so the record
names the exact statement that was executing. `ins` is not a SWP arm, so the next thing the function
does is choose a map: `cmn r9, #0x10000` with `r9` = the fault address, `addge r0, r1, #692` where
`r1` is the saved `thread` — `ACT_MAP`, this configuration's `offsetof(struct thread, map)`, in the
generated `assym.s` — and then `ldr r5, [r0]`. **`r5` was 0**, so `ldr r0, [r5, #40]` at `0x8044b0b4`
faulted at `0x28` — `MAP_PMAP` is 40 in the same generated file — and that fault is another data abort
in SVC mode, which re-enters `sleh_abort` at the same place, 0x250 bytes further down the kernel stack
each time (0xc045ea08 → 0xc045e7b8 → 0xc045e568). The `kernel_map` arm cannot be the zero: it would
have loaded the global's own value and `kernel_map` is not NULL at this point in the boot. So
**`current_thread()->map` is 0** for the thread that ran the exec (TPIDRPRW = `0xc0495480`, the same
thread as entries 1-4).

**That is recorded and not explained.** `thread_create_internal` sets `new_thread->map =
parent_task->map` (`osfmk/kern/thread.c:1280`) and `thread_template.map = VM_MAP_NULL`
(`:333`); which of those the boot thread should have been through, and what should have written a
non-NULL map by the time this fault is taken, is 475's second question and is not answered here. What
the run does say is what it costs: the four entries that returned have `xnu_live_sleh_back` = 1..4 and
**entries 5 to 8 have no `_back` record at all**, and `xnu_live_sleh_seen` then counts 9 → 0x40 and
stops. The machine did not loop on one fault; it *stopped asking the handler* — the stack ran out
inside the recursion and nothing wrote a report, which is why this log has 77 `xnu_entry_` keys against
a completed run's 385 and not one of them from the `undef`/`panic`/`trap`/`abort` groups.

**Measured:** gate passed, device run exit 0, device back on Android by itself (`No errors detected`
after the payload's output — the hardware watchdog, unchanged), log 459628 bytes / 5018 lines / 3930
payload lines. The OS console is byte-for-byte the shape 473 left (`Added memory device md0/rmd0 …`,
`BSD root: md0, major 2, minor 0`, `attempting to load /usr/local/sbin/launchd.development`,
`failed loading … errno 2`, `attempting to load /sbin/launchd`) and **still ends there, with no failure
line for `/sbin/launchd` and no `panic`** — the exec succeeded, and the boot is one instruction into
process 1. Entry image 5438068 bytes (`.text` 5224192, `.bss` 0x8052fa80..0x80587500); payload
`stage90-qcdt.img` 8458240 bytes, sha256 `badbca2e…00eb6f`.

**What would have made this wrong, and did not.** If the offsets were wrong, entries 1-4 would have
read `frame_ok = 0`, and the frontier entry would have been unreadable — that is the control's job. If
the kernel path's `pc` store were the return address of `bl vfp_save` after all, `pc` would have been
`0x800147b8` on every kernel-mode abort; it is not. If `far = 0` had been a *different* small number,
the pointer would not have been NULL and the guard would not be the cause. And if the handler had
simply kept running, entries 5-8 would have `_back` records — they do not, which is what says the
recursion is where the run ended.

## What 475 has to do

**One place, two changes, both in the undefined-instruction handler, and the first is one comparison.**
(a) **Do not read a pointer this handler was not handed**: `r_args` = 0 means there is no panic frame,
so refuse it — and make the refusal *distinguishable* from an accepted NULL, which means publishing the
decision rather than the page (`args & ~0xFFF` is 0 in both cases; the key needs a sense of its own, or
the page key needs a value that cannot be a page). (b) **Decide what a user `udf` means before walking
anything.** Apple's own `locore_fleh_undef` makes that decision as its *first* test — `mrs sp, SPSR`,
`tst sp, #15`, `bne undef_from_kernel` — i.e. user mode goes one way and kernel mode the other, and the
user path saves the frame into the thread's PCB and switches to SVC on the thread's kernel stack. This
handler has the same number already (`mrs r7, SPSR` at `0x80006658`) and never tests it. Everything
after that follows from the same question: what the handler should *do* with a user `udf` so that the
instrument does not change the kernel's behavior — forward to Apple's handler, or report and let the
thread take its signal — and a report that is finally written will be the first log in this project
with `xnu_entry_undef_pc = 0x10e0` in it, which is the milestone 473 predicted and 474 measured from the
other side.
