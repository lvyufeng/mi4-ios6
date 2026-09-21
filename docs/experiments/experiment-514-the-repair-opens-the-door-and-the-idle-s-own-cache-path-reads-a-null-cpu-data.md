# Experiment 514 — the repair opens the door, and the idle's own cache path reads a null `cpu_data`

**One line.** 513 located the idle's door — `cpu_idle`'s first test returns early because the per-CPU
`cpu_signal` still carries `SIGPdisabled`, a bit that only `cpu_signal_handler_internal(FALSE)`
(`osfmk/arm/cpu_common.c:386`) clears, and that function is reachable only through the platform's IPI
handler slot (`machine_routines.c:605`), which this port never fills. 514 takes the first of the three
repairs 513 named: **one call to the kernel's own function**, made from the park's own syscall, before the
park's real `poll`, so the window that follows is the first in this walk in which the door can be open.
It also wraps `cpu_idle_wfi` to turn 513's *bound* on the halt into a count, and adds two console lines
that report the park's own window instead of a cumulative total. **The repair took**, and it is visible in
the census rather than in a claim: the idle's exit series stops at `0x8000` where 513's ran to `0x01000000`,
and `SetIdlePop` — entered **0 times** in every 512/513 run — is entered **once** and answers TRUE. And the
boot then stops four instructions short of the `wfi`, inside `platform_cache_idle_enter`, on a store to
address **`0x130`**: the function turns the D-cache off, *then* reads `getCpuDatap()`, and that read returns
NULL — so `cpu_data_ptr->cpu_CLW_active = 0` (`caches.c:421`) writes to `0 + 0x130`. The wall is named, and
it is one line of Apple's own idle-cache path plus one boot argument this port never passes.

The OS's own console block, as both runs of this image leave it — the three lines 513 already had, and
*none* of the lines that follow them, because every one of those is printed after the park's real call
returns (`entry_trace.c:1025-1048`) and in this image the park's real call never returns:

    load_init_program: attempting to load /sbin/launchd
    mini4: the OS starts the process at 0x10e0 (the thread's user pc was 0x10e0)
    mini4: the OS's own init load returned, so pid 1 has the init image (caller 0x80048eb8)
    mini4: the AST is done -- pid 1's thread is at 0x10e0 for user mode (sp 0x101efc)

513's block held six `mini4:` lines (821 characters from the first one to the space fill that ends the
block); this one holds three (250 characters), and the three are the same three, byte for byte. The
absent ones are the park's own line, 513's two door lines, and this step's two new ones — five lines, all
of them in the same `printf` group below `__real_poll`.

## What the step was for

512 and 513 left one question open and one repair list: *why is this idle a spin, and what happens if it
stops being one?* 513 answered the first with three independent witnesses (`SetIdlePop` entered 0 times;
2,198,110 door exits at one site; 483's decrementer census at ≤64 for the whole boot) and named three
candidate repairs, all of them behaviour changes whose falsifier it stated: (a) clear `SIGPdisabled` with
`cpu_signal_handler_internal(FALSE)`, (b) install an IPI handler into the slot at `machine_routines.c:605`,
(c) accept the spin. 514 does (a), and does it where the kernel's own answer is the one that counts:

* **The call is made in `__wrap_poll`**, on pid 1's thread, in a syscall, with interrupts as the kernel
  left them — not from the payload. `cpu_common.c:386` and its `hw_atomic_and(&cpu_signal, ~SIGPdisabled)`
  at `:402` are the kernel's own clearing of the bit, so `getCpuDatap()` inside it is the kernel's answer
  about the CPU this process is running on and not this file's guess. It is made **before** the park's real
  call, and the counter is read either side of it, so the repair's own cost is on the record.
* **The window is the park's, not the boot's.** 513's console lines are cumulative, and its document had to
  give the park's share as a *bound* because the published series is sparse (powers of two). 514 snapshots
  the same counters at the park's start and reports differences.
* **The halt gets a count.** `--wrap=cpu_idle_wfi` publishes the fast flag, the word at `wfi_inst`, the
  ticks either side of the call, and the running total — so a run's `xnu_live_wfi_*` record is a halt the
  CPU was actually given up for, and its `_ticks` is how long for. The record is written **after** the
  call and not around it, which is the one place this step's instrument can be silent about the thing it
  measures: a run that stops inside the `wfi` leaves no `_after`, and this step says so in the clause
  rather than leaving it to be inferred.

## What the build checks

    xnu_entry_514: cpu_signal_handler_internal (0x800121cc) is the kernel's own (defined in
    osfmk/arm/cpu_common.c, not in pass 1's undefined set, one definition in the image), the pool reaches
    it by 1 call (this step's, inside __wrap_poll 0x8047ad0c..0x8047b224 at 1 site) and by the kernel's own
    tail branches [R_ARM_CALL R_ARM_JUMP24 ], and its compiled body in osfmk_arm_cpu_common.o clears the
    bit: of 8 hw_atomic_and call(s) exactly 1 is preceded by ~SIGPdisabled, of 3 hw_atomic_or call(s) 1 is
    preceded by SIGPdisabled and 2 read the live word with a zero operand — and the platform's own entry
    point cpu_signal_handler (0x800121c4) is 'mov r0, #0; b cpu_signal_handler_internal', the same call
    with the same argument, whose address ml_processor_register takes for the IPI slot; and cpu_idle_wfi
    (0x8001669c, reached from inside cpu_idle only, 1 site) is wrapped at 0x8047b4b4, with Apple's own
    wfi_inst (0x800166cc) the first symbol after it and the word there 'e320f003 wfi' — so a run's
    xnu_live_wfi_* record is a halt the CPU was actually given up for, and its ticks are how long for

The clause is the same shape 513's was, for the same reason: the instrument's own claim is checked
against the object it will read, and five things that would have made a record a *label* rather than a
reading are refused at build time — the callee not being the kernel's (it is in the pool, one definition),
this step's call not being the only call from the instrument (one site inside `__wrap_poll` and none
outside), the compiled body not being the one that clears the bit (the `and` whose operand is
`~SIGPdisabled` and the `or` that sets it, both located in the instruction window rather than by name),
`cpu_signal_handler` not being the same call with the same argument, and `wfi_inst` not being the first
symbol after `cpu_idle_wfi` with a real `wfi` in it. Two defects of this step's own reasoning were fixed
this way and are recorded below.

## The readings

Both runs are of the one image (`out/stage90/stage90-qcdt.img`, sha256 `febd4b50…`), non-persistent
`fastboot boot` through the gate and the capture script.

| reading | run 1 | run 2 |
|---|---|---|
| `xnu_live_repair_seq` / `_caller` | 1 / `0x80285898` (`unix_syscall+0x100`) | 1 / `0x80285898` |
| the repair's ticks | `0x06f3a2d9` → `0x06f3a2e5` (**12**) | `0x04c280c9` → `0x04c280d6` (**13**) |
| `xnu_live_sip_seq` | **1** (513: none, ever) | **1** |
| `_site` / `_ret` / `_en` | `0x8000cce0` / **1** / 1 | `0x8000cce0` / **1** / 1 |
| `_now` after the repair | `0x06f3a650` (+`0x36b`) | `0x04c2844b` (+`0x375`) |
| idle / door series | 16 records each, last `0x8000` (32768) | 16, last `0x8000` |
| `xnu_live_poll_*` records | seq 1 (5 ms, `0x1ef4c` = 6.604 ms), seq 2 (40 ms, `0xd48d5` = 45.344 ms) | seq 1 (5 ms, `0x240e0` = 7.692 ms), seq 2 (40 ms, `0xd8afb` = 46.226 ms) |
| `xnu_live_poll_over` | **absent** (513: `0x8`) | **absent** |
| `xnu_live_wfi_*` | **none** | **none** |
| the first fault | `pc 0x80045290`, `lr 0x80045288`, `fsr 0x805`, `far 0x130`, `thr 0xc04dd830`, `sp 0x80517ff0`, `cpsr 0x60000093`, `user 0`, `recover 0`, `frame_ok 1` | same `pc`/`lr`/`fsr`/`far`, `thr 0xc046bb10` |
| the storm | `sleh_seen` to its cap **64**, `sleh_storm 9`, `pc 0x80011664` × N, `far 8`, `sp` descending `0x248` a record | identical |
| `dec_count` / `setpop_count` / `irq_timer_count` | `0x20` / `0x20` / `0x4` (513: `0x40`/`0x40`/`0x10`) | last published the same |
| `ostext_chars` / `_heals` / `_at` | `0x3ee` (1006) / 1 / `0x48410` | `0x3ee` / 1 / `0x48410` |
| `mini4:` lines in the block | 3 (250 chars) | 3 (250 chars) |
| log | 573203 bytes, `No errors detected` | 571795 bytes, `No errors detected` |
| gate / run exit | 0 / 0, device back on its own | 0 / 0, device back on its own |
| `xnu_entry_failures` | `0x00000000`, no `trap record:`, no `pid 1 exited`, no panic | the same |

`thr` is the *idle thread* in both runs — it equals `xnu_live_idle_thread` (`0xc04dd830` in run 1,
`0xc046bb10` in run 2), which is why `xnu_live_sleh_thr` on the storm records names the thread the idle
was running on rather than some other thread's.

### The repair took, and the census is the witness

Three things moved together, and they only move together if the door stopped being taken:

1. **The exit series stops at `0x8000`.** The published records are at powers of two, so the total is
   `[32768, 65536)` in both runs — against 513's `0x01000000` = 16,777,216. The idle is entered the same
   number of times *before* the repair (both runs publish `0x4000` and `0x8000` and neither publishes
   `0x10000`), so the collapse is entirely after the repair: the loop that could only leave by its door is
   no longer leaving by it.
2. **`SetIdlePop` is entered once and answers TRUE.** It was entered 0 times in every 512 and 513 run — not
   0 per window, 0 in the boot. `_ret = 1` means `rtclock.c:365`'s `SetIdlePop` ran to its `ml_set_decrementer`
   arm, i.e. the decrementer was given the pop's remainder. The site it is entered from, `0x8000cce0`, is the
   `cmp r0, #0` after `bl __wrap_SetIdlePop` at `0x8000ccdc` — this step's own call site, which is what the
   wrapper's `_site` describes.
3. **`xnu_live_sip_en = 1`** on that record, as on every door record: the first test's left operand is still
   TRUE, so the right operand is what changed.

The door's `_lr` is `0x8000ccf0` on all 16 records in both runs. That is `cpu_idle+0x4c` — the `mov lr, pc`
at `0x8000cce8`, which this image reaches by **falling through** from the `bne` at `0x8000cce4`, because
inserting `--wrap=SetIdlePop` changed the layout so that all three of `cpu_idle`'s doors exit through one
site instead of 513's two. The rule 513 fixed still reads correctly (`0x8000cce8 + 8 = 0x8000ccf0`), and the
consequence for reading a run is recorded here rather than discovered later: **in this image the exit site
no longer separates the doors, so the door a run left by comes from `SetIdlePop`'s own census and not from
`_lr`.**

### The halt was four instructions away, and `far = 0x130` is where the boot stopped instead

`cpu_idle`'s continuation after a TRUE `SetIdlePop` is short. In this image:

    0x8000cd04  bl  pmap_switch_user_ttb          (kernel_pmap)
    0x8000cd10  str r0, [r5, #24]                 cpu_active_thread = current_thread()
    0x8000cd1c  bl  arm_debug_set                 (only if cpu_user_debug)
    0x8000cd38  ldr r0, [r5, #36] / blx r3        cpu_idle_notify(cpu_id, TRUE, &ticks)
    0x8000cd48  ldr r0, [r5, #200]                idle_timer_notify
    0x8000cd94  bl  kpc_idle
    0x8000cd98  bl  platform_cache_idle_enter     <-- the fault is in here
    0x8000cda8  bl  __wrap_cpu_idle_wfi           <-- the halt, never reached
    0x8000cdac  bl  platform_cache_idle_exit
    0x8000cdb4  bl  ClearIdlePop(TRUE)
    0x8000cdbc  b   cpu_idle_exit

The faulting record is `pc = 0x80045290` = `platform_cache_idle_enter+0x58`, `fsr = 0x805` (write,
translation fault, section) and `far = 0x130`. The instruction there is

    8004527c: mrc  p15, 0, r0, cr13, cr0, {4}       getCpuDatap()
    80045280: ldr  r4, [r0, #1484]                  r4 = *(... + 0x5cc)
    80045284: bl   FlushPoU_Dcache
    80045288: mov  r0, #0
    8004528c: add  r2, r4, #312
    80045290: str  r0, [r4, #304]                   cpu_data_ptr->cpu_CLW_active = 0   <-- fault

so `far = 0x130` says **`r4 = 0`**: `getCpuDatap()` returned NULL. That is Apple's own code —
`caches.c:402-430`, `runtime_smp_idle` path — and its `cpu_data_ptr->cpu_CLW_active = 0` is `caches.c:421`.

**`r4` is `getCpuDatap()` and the expression is not in doubt.** `cpu_data.h:79` defines
`getCpuDatap()` as `current_thread()->machine.CpuDatap`, `current_thread()` is `[TPIDRPRW]` because
`machine_set_current_thread` writes the thread pointer into TPIDRPRW (`machine_routines_asm.s:37-38`), and
`0x1484` is that field's offset. The *same* expression, read 300 instructions earlier in `cpu_idle`
(`0x8000ccb4`: `mrc r1, c13, c0, 4` / `ldr r5, [r1, #1484]`), returned a **non-NULL** pointer. That is
proved by absence, not by an assertion: if `r5` had been 0, `0x8000ccd0` (`ldr r0, [r5, #40]`, address
`0x28`), `0x8000cd0c` (`ldr r1, [r5, #284]`, address `0x11c`) and `0x8000cd10` (`str r0, [r5, #24]`,
address `0x18`) would each have faulted, and none of them did — and the door test itself is a read out of a
real `cpu_data` (a door taken 32768 times is `cpu_signal & SIGPdisabled` answering TRUE).

**What changed between the two reads is one thing, and it is upstream of the read.** 514's fault is in
`platform_cache_idle_enter`, and the first thing that function does is clear `SCTLR.C`:

    8004523c: mrc  p15, 0, r0, c1, c0, 0     read SCTLR (0x30c5787d in this run, C = 1)
    80045240: bic  r0, r0, #4                SCTLR_DCACHE (proc_reg.h:498)
    80045244: mcr  p15, 0, r0, c1, c0, 0
    80045248: isb  sy
    ...  then the flags, then the read at 80045280, then FlushPoU_Dcache at 80045284

`getCpuDatap()`'s load is therefore made with the D-cache **off** and `FlushPoU_Dcache()` — which is what
would put the line back into memory — runs **after** it. On this Cortex-A15 an access with C = 0 does not
answer the L1/L2, so that load reads DRAM; and DRAM holds a `0`, because `0` is what the field is
initialised to (`pcb.c:130`, `machine_thread_create`: `thread->machine.CpuDatap = (cpu_data_t *)0` for any
thread that is not current) and the value `cpu_idle` saw was written into the *cache* by the switch
(`pcb.c:103`, `machine_switch_context`: `new->machine.CpuDatap = cpu_data_ptr`) — which is the most recent
writer of that line whenever this thread is the one running. So the SMP idle-cache branch reads a NULL
`cpu_data`, and its next store faults at `0x130`.

**Every other writer of that field is excluded**, which is why the read is the only variable left: the five
writers in the tree are `pcb.c:103` (a context switch), `pcb.c:130` (a new thread), `pcb.c:272` (a dup),
`cpu.c:441` (the first thread) and `arm_init.c:250` (`BootCpuData`), and none of them runs inside
`cpu_idle`; TPIDRPRW is written only by `machine_set_current_thread` (`machine_routines_asm.s:38`),
`machine_load_context` (`cswitch.s:84`) and `Switch_context` (`cswitch.s:145`), i.e. only by a context
switch, and this path takes none. Two registers, one computed address, one value written in one place: the
only thing that differs between the two reads is the cache state.

### The storm is the handler faulting on its own first load

The second record is `pc = 0x80011664` = `ml_at_interrupt_context+0x14`, `lr = 0x804531d8` =
`sleh_abort+0x70`, `far = 0x8`. `ml_at_interrupt_context` (`machine_routines.c:669-677`) is

    80011650: mov  r0, sp
    80011654: mrc  r1, c13, c0, 4
    80011660: ldr  r1, [r1, #1484]           getCpuDatap()
    80011664: ldr  r1, [r1, #8]              -> intstack_top, at address 0 + 8   <-- fault

so the abort handler's first `getCpuDatap()` returns the same NULL, and its `ldr` faults at **address 8**.
Because the CPU is already in abort mode, the second abort re-enters the handler instead of being reported:
each entry pushes a full frame on the same stack, `sp` descends `0x248` (584 bytes) a record, and
`xnu_live_sleh_seen` runs to its cap of **64** with `pc` pinned at `0x80011664`. `xnu_live_sleh_storm = 9`
is the flag the instrument raises one past its per-epoch record cap (`entry_stubs.c:1700-1702`), and the
log's last payload record is `xnu_live_sleh_seen=0x00000040` in both runs. That the *first* fault is the
`str` and not a recovered fault at the `ldr` above it is again an absence: there is no `sleh_pc =
0x80045280` anywhere in either log.

## The wall is named, and it is a uniprocessor being told it is not one

`platform_cache_idle_enter`'s whole shape is

    platform_cache_disable();                                 /* SCTLR.C = 0, isb */
    if (up_style_idle_exit && (real_ncpus == 1))
            CleanPoU_Dcache();                                /* reads nothing */
    else {
            cpu_data_t *cpu_data_ptr = getCpuDatap();         /* reads with C = 0  <-- here */
            FlushPoU_Dcache();
            cpu_data_ptr->cpu_CLW_active = 0;                 /* the fault */
            ...
    }

(`caches.c:402-430`.) Both of its operands are globals in this very image, and the disassembly reads them
at exactly those addresses: `up_style_idle_exit` is `0x8054d0c4` (a `.bss` word, `arm_init.c:103`) and
`real_ncpus` is `0x80520378` (`cpu_common.c:66`, initialised to 1). And they are such that the **else**
branch is taken on a machine with one CPU:

* **`real_ncpus` is 1.** Its only incrementer is `cpu_data_register` (`cpu.c:408`), and `ml_processor_register`
  calls it only for a non-boot CPU (`machine_routines.c:570-573`: `if (!is_boot_cpu)`). This port registers
  the boot CPU, whose `cpu_data` is `BootCpuData`, so the count never moves.
* **`up_style_idle_exit` is 0.** `arm_init.c:103` initialises it to 0 and the only thing that ever sets it
  is the boot argument of the same name (`arm_init.c:287-289`), which the payload's `CommandLine`
  (`boot_args.c`) does not carry.

So Apple's *uniprocessor* configuration is the one that never runs the read, and its *SMP* configuration —
which is what this port selects by omission — reads a `cpu_data_t` with the cache disabled. That is the
whole of the step's product: 514 did not find a bug in this port's code, it found the branch Apple's own
idle-cache path takes when a uniprocessor does not say so, four instructions before the halt, and named the
boot argument that selects the other one.

## What is owed

* **515: declare the uniprocessor idle, and measure the read instead of eliminating it.** The change is
  `up_style_idle_exit=1` in the payload's `CommandLine` (and in the device tree's `/chosen/boot-args` copy,
  which this port keeps in agreement with it). The instrument that turns the paragraph above into a reading
  is a `--wrap=platform_cache_idle_enter` that publishes `up_style_idle_exit` and `real_ncpus` **and**
  `getCpuDatap()` — read while the cache is still on, i.e. one instruction before the real call — plus
  `--wrap`s on `CleanPoU_Dcache` and `FlushPoU_Dcache` gated on that window, so the branch is a *counted*
  one and not an inference from which fault did not happen. The falsifier is the halt: `xnu_live_wfi_seq`
  with `_inst = 0xe320f003`, `_ticks` greater than zero, then `platform_cache_idle_exit`, `ClearIdlePop`,
  `cpu_idle_exit` — and the park's `poll` returning, which is `xnu_live_poll_seq` 3 and `xnu_live_poll_over`
  and the five console lines that this image never printed.
* **The other two repairs 513 named are still open** and are now the alternatives if the flag turns out not
  to be enough: installing a handler in the IPI slot at `machine_routines.c:605`, and accepting the spin.
* **Carried from 514's own design**: the two new console lines and the `wfi` record are all taken on a path
  that this run did not reach, so their *content* is unmeasured — the park's window counters (`w_calls`,
  `w_exits`, `w_sip`, `w_wfi`, `w_sleep`) have no values in either log, and the totals they were meant to
  be differenced from are `0x8000`, `0x8000`, 1, 0 and 0. The first entry in that list is the step's own
  answer to 513's bound and it is still owed.
* **Carried from 512/513 and unchanged**: the telemetry copy loop on pid 1's own thread; `_cpsr = 0x10`
  on the AST records; 508's a-record-that-cannot-be-lost; 507/506's `p->p_xstat`, the empty `xnu_entry_why`
  and the `trap record:` gate; 505's corpse-path slot `0x802933b4`; 504's `mdevadd_base`/`mdevopen`/second
  `read`; 503's leeway row and third ask; 502's long list; and the captured console's own census question,
  which this step advances without closing: `xnu_live_ostext_chars` is 1006 with `_heals = 1` in both runs
  where 513 published 1006 *and* 2022 — and `xnu_live_ostext_at` is `0x00048410` in all three logs, so the
  OS's own console text is the same length to the byte and the difference is the heal count and not the text.
* **The OS's own reboot path** is still in the image and still unwired: `reboot_kernel` → `host_reboot` →
  `halt_all_cpus` → `PEHaltRestart(kPEHaltCPU)`, with `PE_halt_restart` an unfilled `.bss` slot.

## The image

`.text` 5,290,560 → **5,292,096** (+1536, this step's wrapper and its records), entry image 5,503,612 →
**5,519,996** bytes (`xnu_entry_copied_bytes = 0x00543a7c`, copied to `0x80000000`), `.bss`
`0x80543a80` … `0x8059c4f0` = **363,120** bytes (`xnu_entry_bss_bytes = 0x00058a70`), entry point
`0x80000074`, device tree at `0x806e0000` + `0x7490`, `boot_args` at `0x8059e000`, `topOfKernelData`
`0x80700000`, headroom 1,456,912 bytes. Wrap census **73** (61 reached by a branch / 1 same-object /
1 never called / 10 by address — this step's `cpu_idle_wfi` is the 61st), and the two symbols this step
reads: `idle_enable` `0x8054d830` (one 4-byte B, loaded inside `cpu_idle` and stored inside
`cpu_machine_idle_init`), `real_ncpus` `0x80520378`, `up_style_idle_exit` `0x8054d0c4`. Payload sha256
`febd4b50…` (`stage90-qcdt.img`), `817fc699…` (`stage90.bin`), `fc70a881…` (`stage90.elf`), `52bc9c35…`
(`stage90_fixture.macho`). 129 fixture mutations refused; falsifiers all negative
(`xnu_entry_failures = 0`, no `trap record:`, no `pid 1 exited`, no panic, `No errors detected`).
One thing changed in the OS's own console text between 513 and this image and it is a word: the RAM disk,
`Added memory device md0/rmd0 (02000000/0D000000) at 000000008050D000 for 0000000000002000` in 513's log
against `0000000080511000` here — the port's own `g_stage90_ramdisk` moving with the image, 1307 characters
of OS text otherwise byte-identical, and byte-identical between this step's two runs.

## Safety

Two runs, both non-persistent `fastboot boot` through `stages/stage90/preflight_boot_check.sh
--allow-xnu-entry` and `stages/stage90/run_and_capture.sh --allow-xnu-entry`, nothing flashed, the gate and
the run both exiting 0, and **the device back on Android on its own both times** (`MI 4LTE`, release 10) —
which is the property that matters for a step whose run now ends in a fault storm rather than in a
`platform_reboot`: the storm is in the kernel's abort path, and the hardware watchdog is the net that
covers it, exactly as the gate's own text says. Run 1's log is 573203 bytes and run 2's 571795, at
`/tmp/514-run1-kmsg.txt` and `/tmp/514-run2-kmsg.txt`; both end in the kernel's own `No errors detected`.

    python3 tools/check_experiment_index.py    # ok: 491 row(s) across 91 stage column(s)
