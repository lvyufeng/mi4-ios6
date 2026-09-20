# Experiment 479 — the kernel answered process 1

**478's agenda was to retire `launchd_crashed_panic` by not killing process 1, and it named the right
route with the wrong syscall inside it.** The route — the vector page's slot 2, Apple's own
`locore_fleh_swi` — is what the fixture's `svc` needed and is what went in. The syscall the agenda
put on that route was `thread_switch`, and in this image the only option of `thread_switch` that does
anything at all (`SWITCH_OPTION_WAIT`, the one the 478-era draft's registers held) ends in
`assert_wait_timeout((event_t)assert_wait_timeout, THREAD_ABORTSAFE, 0, NSEC_PER_MSEC)`: the interval
is 0, the event is the *function's own address*, and the only thing that could fire the deadline is a
timer interrupt, which this image does not have and this step did not add. That run would have parked
process 1 in a wait nothing can signal, and the log would have read as a machine that stopped.

What went in instead is `getpid` — Unix syscall 20, `narg` 0 — a syscall that **returns a value**.
**The run answers with the number, and the number is 1.** `xnu_live_getpid_value=0x00000001`,
`_error=0x00000000`, `_caller=0x8027e248` = `unix_syscall+0x100`; the count climbs by powers of two to
`0x00800000`, every one of them carrying 1, and there is no `xnu_live_getpid_change_*` key at all. The
OS console is 478's text byte for byte except the `md0` address, with **one line removed**: no
`pid 1 exited`, no trap record, no panic — and no ` xnu_entry_*` report keys either (478 has 473 of
them), because the payload's report is written on the trap path and this run never trapped.

## The capture, and the number it found

The whole of this step's reading is in the live channel, in the channel's own order:

    xnu_live_getpid_seq     0x00000001     the first call
    xnu_live_getpid_value   0x00000001     the kernel's answer: this process is pid 1
    xnu_live_getpid_error   0x00000000     no error
    xnu_live_getpid_caller  0x8027e248     = unix_syscall+0x100, the `mov r4, r0` after the `blx r3`
    xnu_live_getpid_count   0x00000002 .. 0x00800000     the 23 powers of two the loop reached
    xnu_live_getpid_last    0x00000001     ... the value on every one of them
    (no xnu_live_getpid_change_seq, no _change_value, no _change_error)

`EXPECTED_PID` is 1 and the kernel said 1, so the `bne` behind `cmp r0, #1` is not taken 8 388 608
times and the `udf #1` behind it is never reached. The two halves of the reading are different in kind and both
are needed: the *value* says the fixture's question was answered, and the *absence of a change* says
the answer was not a coincidence of the first call. A fixture whose compare failed would have died on
SIGILL at pc `0x10e4` — one instruction past the `0x10e0` that 474/475/477 measured — and left a trap
record and a `pid 1 exited` line; neither exists anywhere in this log.

**The last record is a milestone, not a stop.** `0x00800000` is 2^23 and it is the *last* power of two
the wrapper writes, so the 8 388 608 calls are a floor: the loop was still running when the log ends.
What ended the log is the only reset source still enabled — the payload arms the SoC's own watchdog
before the jump (`hw_watchdog_bite_ticks_written=0x000dffac` at `hw_watchdog_hz=0x00007ffd`, 28.0 s,
with the bark at `0x000c7fb5`, 25.0 s) and nothing disarmed it, because the *disarm* lives in the
payload's exit path, which this run never reached. 8.39 million syscalls inside the ≤28 s that allowed
is a floor of about 3×10^5 syscalls per second, each with the whole BSD dispatch behind it.

The rest of the channel is the same shape as 478's and reads as the machine's state rather than as
this step's answer: `xnu_live_block_seq` runs to `0x46` (70 blocks entered) with `_returns = 0x0d` (13
returned), so 57 never come back — the drivers parked on a timer that does not exist; `xnu_live_sleh`
reaches seq 4 with `frame_ok = 1`, and **all four are `_user = 0`** (`cpsr 0x60000013`, SVC), i.e. the
kernel's own data aborts serviced and retried (`L64loop+0x8` at `far 0xc82de000` for the Mach-O
header, `Lcopyout_wordwise_loop+0x4` at `far 0x00101f28` for the exec's string copyout). The last
block the kernel records is seq `0x46`, `caller = 0x800f7efc` = `Call_continuation+0x1c`, thread
`0xc05fc1a0`, and process 1's first `getpid` record is the next record in the ring: on this image the
boot thread parks, and the ring's own order says the handover to process 1's user mode happened after
that park, not before it.

## The console: 478's text with one line gone

The OS's own console is the project's only window onto the OS, and here it is 478's with a diffless
body:

    478: 25 lines + "pid 1 exited -- exit reason namespace 2 subcode 0x4, description none"
    479: 25 lines, ending at "load_init_program: attempting to load /sbin/launchd"

measured as bytes out of the console tank: **1289 bytes and 24 carriage returns through the launchd
line in both runs**, 1362 bytes in 478 once the death line and its terminator are added. The only other
difference in the whole captured region is the one word that had to move:

    Added memory device md0/rmd0 (02000000/0D000000) at 00000000804FD000 for 0000000000002000     (478)
    Added memory device md0/rmd0 (02000000/0D000000) at 0000000080501000 for 0000000000002000     (479)

`0x804FD000` -> `0x80501000` is `0x4000`, which is this build's `.bss` start moving by `0x4000`
(`0x8052fa80` -> `0x80533a80`) and the copied image growing by `0x4000` (5438068 -> 5454452 bytes) — the
RAM disk address is a *reading* of the image's layout and it agrees with the build's own three numbers.
And the live channel's `xnu_live_prop_w0=0x80501000` says the OS read the same new address out of
`/chosen/memory-map`'s `RAMDisk` property. Nothing else moved: no `xnu_live_undef_*` key (478 has 6),
no `xnu_entry_panic_*` key (478 has 14), no `pid 1 exited` (478 has 1).

**So the sentence this step exists to delete is deleted, and it was deleted by answering the syscall
rather than by removing the check.** `launchd_crashed_panic` was never reached because process 1 was
never killed: it was not killed because the `svc` reached the kernel's dispatcher, the dispatcher
reached a real function, and that function returned.

## Why the fixture asks `getpid` and not `thread_switch`

The fixture is five instructions, and they are the whole of what process 1 does:

    0x10e0  ef000080   svc     #0x80          ; r12 = +20 = SYS_getpid
    0x10e4  e3500001   cmp     r0, #1         ; the pid the kernel assigned this process?
    0x10e8  1a000000   bne     0x10f0         ; ... if not, to the udf
    0x10ec  eafffffb   b       0x10e0         ; ask again
    0x10f0  e7f000f1   udf     #1             ; the kernel answered something else

`0x10f0` is deliberate, not decorative: it is `udf #1`, one immediate away from the `udf #0` at the
entry point that 474/475/477 landed on, so a run that died in the compare would say *which* of the two
words fired. The run does not contain it.

`svc #0x80` with a positive `r12` is the *Unix* side of Apple's split, and this is worth stating
because the reverse is easy to assume — `osfmk/arm/locore.s:573`:

    	rsbs	r5, r11, #0				// make the syscall positive (if negative)
    	ble		fleh_swi_unix				// positive syscalls are unix (note reverse logic here)

so `r12 = +20` becomes `r5 = -20` and the *signed* `ble` takes the Unix branch, while a negative `r12`
(what 478's draft carried: `0xffffffc3` = −61 = `thread_switch`) falls through to `fleh_swi_mach` and
`mach_trap_table`. `fleh_swi_unix` (`:669`) loads the uthread and the proc and does
`bl EXT(unix_syscall)` followed by `b .` — **the Unix path never returns to the fixture through this
code**; the return to user mode is `arm_prepare_syscall_return`'s `regs->save_r0 = uthread->uu_rval[0]`
(`bsd/dev/arm/systemcalls.c:297`) and `thread_exception_return`.

Two things follow, and they are the reason for the syscall's shape rather than its name:

  - **It has to return.** `thread_switch` is a *mach trap*, but that was not the objection: with
    `SWITCH_OPTION_NONE` it calls `thread_block_reason(..., AST_YIELD)` with no wait asserted, and
    `thread_select`'s `still_running` test (`osfmk/kern/sched_prim.c:1840`) is true for a thread whose
    state is exactly `TH_RUN`, so in a single-CPU image with one runnable thread it re-selects itself
    and returns — a no-op that measures nothing. With `SWITCH_OPTION_WAIT` it asserts the wait and
    then blocks, and the wakeup is a timer. Either the safe option proves nothing or the proving
    option parks the machine; neither of them is a *reading*.
  - **It has to take no arguments.** `arm_get_syscall_args` is called only when `callp->sy_narg != 0`,
    and a Unix syscall's arguments come off the **user's stack**. `getpid`'s `narg` is 0
    (`{ int getpid(void); }` in `bsd/kern/syscalls.master`), so the fixture is self-contained in
    registers and needs no stack. That is a real limitation of this step and not a property of the
    design: **the argument-fetch path has still never run in this image**, and giving the fixture a
    stack with six words at `sp` is part of what the next step owes.

478's defect was in the *dispatcher's table*, so the instrument has to sit in the table's slot and not
at a call site, and the fixture had to ask for the syscall the wrapper occupies.

## Where the instrument sits: a data word in `sysent`, not a call site

`--wrap=getpid` rewrites the *initialiser* in `bsd/kern/init_sysent.c` exactly as it rewrites a call —
the fact 458 read out of `cons_ops[1].putc` and 463 out of `IOService::getState` — so `sysent[20]`
holds `__wrap_getpid` and the kernel's indirect call goes through the wrapper:

    8027e23c  mov  r1, r7          ; uap     = &uthread->uu_arg[0]
    8027e240  mov  r2, r4          ; retval  = &uthread->uu_rval[0]
    8027e244  blx  r3              ; r3 = [fp] = callp->sy_call  <- the table's word
    8027e248  mov  r4, r0          ; <- the caller this run recorded

`0x8027e248` is `unix_syscall+0x100` in this image (`unix_syscall` is `0x8027e148`), and the `r3` it
called through was loaded `ldr r3, [fp]` one instruction earlier — `(*(callp->sy_call)) (proc,
&uthread->uu_arg[0], &(uthread->uu_rval[0]))`, `systemcalls.c:160`. The wrapper's three-argument
prototype is that call and not a convenience: `int __wrap_getpid(void *proc, void *uap, int *retval)`,
which the 478 defect (a `--wrap` whose declared shape was `void`) is the reason to spell out.

**And the census cannot see any of this.** In this image the only reference to `getpid` is that
initialiser, so `build_entry.sh`'s reachability census counts it **by address** the way it counts
`vcputc` (458) — "45 `--wrap`ped symbols: 41 reached by a branch, 1 same-object-only, 1 never called
here, 2 by address only (vcputc getpid)". A slot holding `getpid` instead of `__wrap_getpid` defines
exactly the same symbols, moves no count, and produces a run with no records — indistinguishable, in
the log, from a wrapper that is not there.

## The two checks

`tools/check_sysent_table.py` (new) makes the mapping structural, and it compares two *independent*
claims:

  - the **index** — `sysent[20]` is `getpid` — against `bsd/kern/syscalls.master`, Apple's own
    source of the numbering, at eight witnesses spread from 0 to 128 (`nosys`, `exit`, `fork`, `read`,
    `open`, `enosys`, `getpid`, `rename`), which is what makes the 16-byte stride a *reading* and not
    an assumption: no other stride satisfies eight of them at once;
  - the **word in the slot** — `0x804733e0` = `__wrap_getpid`, and *not* `0x80293154` = `getpid` —
    which is the fact the whole step depends on and the one no other check can see.

`--selftest` mutates a copy of the table in memory (the slot zeroed, the real `getpid` put back, the
wrapper one entry late, 20 and 24 swapped, the table shifted a word, `nsysent` at 20, `nsysent` at 1)
and requires all 7 to be refused. Measured output:

    sysent at 0x804f064c, nsysent at 0x8052b078 = 530, stride 16, 8 witnesses
    sysent[20] = 0x804733e0 = __wrap_getpid (getpid is at 0x80293154), so the kernel dispatches
    the fixture's syscall into the wrapper

`tools/host_ramdisk_macho_check.py` grew the other end: the program is now read as **five words** at
the Mach-O's entry point — `svc #0x80`, `cmp r0, #1` with the mask `0xFFFFF000` (which leaves only
`imm12` free, so neither the register nor the condition can move), `bne` to the `udf`, `b` back to the
`svc`, and `udf #1` — with `r12` compared against `SYS_GETPID = 20` read out of `syscalls.master`, and
`EXPECTED_PID = 1` read out of `bsd_init.c`'s own `initproc = proc_find(1)` rather than written into
the checker. Its mutation list is now 31 entries, and both checks' numbers were confirmed against the
payload itself: the 20 bytes at `0x10e0` in `stage90-qcdt.img` (`0x57a294` in the payload's file) are
`80 00 00 ef  01 00 50 e3  00 00 00 1a  fb ff ff ea  f1 00 f0 e7`.

## What the step's own checks caught

Three defects, all of them in the checks rather than in the image, and the first two are the reason
the checks are not decoration:

  - **The sysent check called the image wrong for being right.** Its first version compared
    `sysent[20]` with the symbol `getpid`; the image's slot holds `__wrap_getpid`, so the check exited
    1 on a correct build. This is the 478 lesson — a `--wrap` is an ABI substitution and it renames
    *data* references too — applied one level up: the fact was already in the project's notes about
    `cons_ops[1].putc`, and the check that exists to assert it did not apply it to itself. The fix is
    two-sided: expect `__wrap_<name>` when `nm` has that symbol, and state the *negative* explicitly
    for index 20 (`got == real` is its own failure message, because that is the silent one).
  - **The fixture check's mask forgot `Rn`.** `0xFFF00000` covers `cond`, the opcode and `S`, and
    leaves bits 19:16 — the register — free, so the mutation `cmp r1, #1` (`0xE3510001`, "the
    compare's register") was *accepted*: the selftest found a hole in the check rather than in the
    fixture. `0xFFFFF000` leaves only the 12 immediate bits free, and the immediate's read is now
    `0xFF` rather than `0xFFF`. This is "a measurement can be the thing that is wrong" in its
    self-testing form: the only reason the hole was found is that the mutation was run.
  - **A success line that named the wrong thing.** `check_sysent_table.py` printed `notes[-1]` as its
    `ok:` line, and the witness read-back is appended after the wrapper's slot — so a correct build
    reported `ok: sysent[128] = 0x801deb3c = rename`, and with `--selftest` it reported only the
    selftest. The `ok:` line is now `notes[1]`, the wrapper's slot, whatever else ran.

## What 480 has to do

**The frontier is no longer a stop; it is the absence of one.** Every previous step's agenda was the
name the last run died on. This run dies on nothing: the kernel's console has reached
`load_init_program: attempting to load /sbin/launchd`, process 1 runs and is answered, and the
machine is otherwise where it was — the boot thread parked at a block, 57 threads parked behind it,
and no interrupt source to unpark any of them. The two things to do next are different in kind and
should not be confused with each other:

  - **Give process 1 a syscall whose effect is on the machine, not in a register.** The cheapest one
    that is a real reading is `mmap` of an anonymous page followed by a store to it: the store is the
    first **user-mode** fault this walk would ever produce — all four `xnu_live_sleh` records this run
    has are `_user = 0` — serviced by `fleh_dataabt` -> `vm_fault` and retried in the payload's own
    pmap, and it makes the fixture need what no fixture has needed yet, a **stack**, because a Unix
    syscall's arguments come from `sp`. The alternative (`open`/`write` on the console, so that the
    *bytes* land in the console tank) is larger than it looks: process 1's descriptor table is empty —
    `load_init_program_at_path` calls `execve` and opens nothing (`kern_exec.c:4979`) — so it is
    really "mount devfs at `/dev`, then open, then write", which is a filesystem step as well.
  - **The timer.** `ml_init_timebase` plus an MSM8974 `tbd_ops_t` over the GPT at `0xf9020000`
    (19.2 MHz, IRQ 19) and `IOCPUInterruptController` were owed before this step and are owed after
    it; what is new is that they are now the *only* thing between this run and the OS's own drivers
    making progress — those are the 57 parked blocks, and they are parked on a wakeup that needs a
    clock, not on a missing symbol. That is a kernel-source step with an interrupt path in it, not a
    fixture edit, and it is what "把基础驱动跑起来" needs.

480 should do the first, and carry the second as the plan of record.

**Measured:** gate passed, exit 0, device back on Android on its own, log 458822 bytes / 4986 lines /
1029 `xnu_live` records with no `_capped` (478: 488475 / 5716 / 1003), `No errors detected`, no
`pid 1 exited`, no `xnu_live_undef_*`, no `xnu_entry_panic_*` and **no ` xnu_entry_*` report key at
all** (478: 473) — the payload's report is written on the trap path, which this run never took, so
every number in this document comes from the live channel and the console tank. `.text` 5230176 (478:
5225568), `.bss` 0x80533a80..0x8058b548, image 5454452 bytes (478: 5438068), payload
`stage90-qcdt.img` 8474624 (478: 8458240) sha256
`51d1e7b4def68509ed42c5a221bc0b6f1405f182f1877a72dd39483b9f8ac970`, undefined 26, 45 `--wrap`ped
symbols (41 by branch, 1 same-object-only, 1 never called, 2 by address only), and the entry image
rebuilt byte-identical (`xnu_arm_entry.bin` sha256
`753723b3fc0ac0dd48f1fcae4f038fd303573a74cd8015e55d50420900830619`) after the check-message fix, so
the run above is the run the committed tree produces.
