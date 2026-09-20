# Experiment 466 — the ARM layer is assembled with the configuration, and the boot's first stop is a fault the kernel is built to service

**Status: on the device, one run, 478248 bytes of log, exit 0, device back on Android by itself (Android
10, `No errors detected`).** Two changes: the ARM assembly layer is now assembled with the kernel
configuration's own options (it never had been — `tools/xnu_config/arm_asm_defines.sh` is the one source
for them), and `osfmk/arm/locore.o` is linked into the entry image with its seventeen colliding globals
renamed, which retires the three names 465's run stopped at — `thread_bootstrap_return`,
`thread_exception_return`, `thread_syscall_return`. **The frontier that falls out is not a stand-in at
all.** With those three real, the boot runs `load_and_go_user` → `ast_taken_user` → `bsd_ast` →
`bsdinit_task` → `load_init_program`, prints a line of OS console text no earlier run could print
(`load_init_program: attempting to load /sbin/launchd`), and stops on its **first data abort**: a `strb`
in `copyout` to user address `0x1000`, which is the page `load_init_program` has just allocated in process
1's map and which the kernel's own `fleh_dataabt` → `sleh_abort` → `vm_fault` path exists to populate and
retry. The installed vectors are the instrument's, so the abort is reported instead of serviced. **Not one
stand-in was entered during the whole boot.**

## The two changes

**1. The configuration, into the assembly.** Every C object in this kernel gets
`make_defines.sh <CONFIG>` (108 flags for RELEASE). The seventeen `.s` files of the ARM manifest did not:
`tools/assemble_arm_layer.sh` and `stages/stage90/xnu_arm_assemble.sh` passed the *toolchain* flags
(`-DASSEMBLER=1`, `-DSLIDABLE=0`, `-D__ARM_L2CACHE_SIZE_LOG__=21`, …) and nothing else, so every
`#if CONFIG_*` in the tree's assembly was false while the same macro was 1 in every C object. One file
consults them: `osfmk/arm/locore.s`, whose return-to-user path was assembled to call
`timer_state_event_user_to_kernel` and `timer_state_event_kernel_to_user` — two functions
`osfmk/arm/machine_routines.c:1077-1107` does not compile, so they are undefined in **both** links
(`out/link/RELEASE-measure-undef.txt:42,43`) and no object in the pool could have supplied them. The
measured control: the whole-kernel link's undefined count falls **37 → 35**, and the two names that
disappear are exactly those two. With the options, `locore.o` calls `telemetry_needs_record` /
`telemetry_mark_curthread` instead (defined by `osfmk/kern/telemetry.c`, already linked). One exception is
named and checked rather than fixed: `SLIDABLE` is `1` in `config/MASTER.arm:77`, and `SLIDABLE=1` makes
`osfmk/arm/globals_asm.h:29` fail to assemble ("expected string in directive") because the tree's SLIDABLE
`LOAD_ADDR` is the Darwin `$non_lazy_ptr` idiom — so both scripts keep `-DSLIDABLE=0` against the C side
and `arm_asm_defines.sh --exceptions` prints the disagreement. Re-assembling moves four objects
(`locore` 12488→12560, `cswitch` 564→568, `caches_asm` 596→600, `start` 1040→1044 — the last three by one
literal-pool word) and leaves the other thirteen byte-identical.

**2. `locore.o` in the link, with its colliding names renamed.** The object defines seventeen globals; the
image already defines twelve of them (`ExceptionLowVectorsBase`, `ExceptionVectorsBase`,
`ExceptionVectorsTable`, the eight `fleh_*` handlers, and `ResetHandlerData`), and it is the pool's only
definer of the three this step is about. So the object is linked as a **renamed copy** — every global
except `thread_bootstrap_return`, `thread_exception_return` and `thread_syscall_return` gets a `locore_`
prefix:

```
80012000 T locore_ExceptionLowVectorsBase   8001453c T locore_fleh_prefabt
80012258 T locore_ResetHandlerData          800146d0 T locore_fleh_dataabt
80012264 T locore_ExceptionLowVectorsEnd    80014950 T locore_fleh_addrexc
80013000 T locore_ExceptionVectorsBase      80014954 T locore_fleh_irq
80014000 T locore_ExceptionVectorsEnd       80014b8c T locore_fleh_decirq
80014000 T locore_ExceptionVectorsTable     80014d0c T locore_fleh_fiq_generic
80014020 T locore_fleh_reset                80014d38 T locore_fleh_dec
80014024 T locore_fleh_undef                800150d0 T locore_ExceptionVectorPanic
800141c0 T locore_fleh_swi
```

The rename is a rule, not a list, and both ends of the rule are checked: the three kept names must be
**defined by this object** (or the rule would be skipping names it does not have) and, after pass 1's link
array is complete, **referenced by some other object in this link** (or the object would be defining a
frontier symbol on no live path) — two measurements neither of which can be taken from the other's output.
`tools/xnu_entry_callwalk.py --root bsdinit_task` says "reached no stub on the straight-line path", and the
renamed set is asserted name for name against the seventeen, so a new collision fails the build instead of
being renamed by the rule that happens to cover it.

The three retired names, in the built image:

```
80014f68 T thread_syscall_return
80014f98 T thread_bootstrap_return      ( = thread_exception_return = load_and_go_user )
8010913c T ast_taken_user               ( the next real code, unchanged )
```

Pass 1's undefined set is **24 names, was 27**; the three that left it are those three.

## What the run says: the boot reaches `load_init_program`

`xnu_entry_stub_hit_count = 0` and `xnu_entry_stub_caller_v = 0`, the live records
`xnu_live_stub_hit_*` are absent, and no `locore_` name appears in pass 1's undefined list — **no stand-in
was entered at any point in the boot**. The static walk agrees from the other side (above). XNU's own OS
console block grows by one line and that line is the frontier:

```
 465  21 lines   ... mbinit: done [0 MB total pool size, (0/0) split]
              Added memory device md0/rmd0 (01000000/0B000000) at <ptr> for 0000000000040000
              BSD root: md0, major 1, minor 0
 466  22 lines   ... the same twenty-one lines, then
              load_init_program: attempting to load /sbin/launchd
```

`kern_exec.c:5164`'s `printf`, in the `init_programs[]` loop, three lines before
`load_init_program_at_path`. The chain into it is 465's own frontier completed: `thread_bootstrap_return`
(now real) → `load_and_go_user` → `ast_taken_user` → `bsd_ast` (`kern_sig.c:3445`, `bsdinit_task()`) →
`bsd_init.c:1079` `load_init_program(p)`. Every function on it is real in this image, and the run says so
twice — by the console text and by the fact that nothing on it is a stub.

## The stop: the first data abort, and what it is

```
 xnu_entry_abort_entries=0x00000001
 xnu_entry_abort_first_dfar=0x00001000
 xnu_entry_abort_first_dfsr=0x00000805
 xnu_entry_abort_first_pc=0x80010fc0
 xnu_entry_abort_first_lr=0x80010fc8
 xnu_entry_abort_first_insn=0xe4c13001
 xnu_entry_abort_first_spsr=0x20000013
 xnu_entry_abort_first_ttbr0=0x807a404a  _ttbr1=0x8070404a  _ttbcr=0x00000002
 xnu_entry_abort_first_sp=0x80514fe0      _kv_len=0x00001fea
```

`pc = lr - 8` for a data abort (the instrument's own arithmetic, `entry_stubs.c:5039`), so the aborted
instruction is at `0x80010fc0`, and `insn` is read back from the image at that address:

```
80010fb4 <Lcopyout_bytewise>:
80010fb4:  e2522002  subs r2, r2, #2
80010fb8:  e4d03001  ldrb r3, [r0], #1
80010fbc:  54d0c001  ldrbpl ip, [r0], #1
80010fc0:  e4c13001  strb r3, [r1], #1        <- the fault, and the FIRST store of the loop
80010fc4:  54c1c001  strbpl ip, [r1], #1
```

`copyout` is `osfmk/arm/machine_routines_asm.s:706`; the bytewise path stores with r1 as the destination,
so `dfar = 0x1000` is the destination of that `strb`. `spsr = 0x20000013` is SVC with C set: the faulting
code is **kernel** code. `dfsr = 0x805`: `& FSR_MASK (0x40F)` = `0x005` = `FSR_SFAULT` ("translation
fault, section", `osfmk/arm/proc_reg.h:269`) with `DFSR_WRITE` (bit 11) set — a write to an address whose
1 MB section has no first-level descriptor. `ttbcr = 2` puts `0x1000` under `ttbr0`.

Which `copyout` call it is comes from one function:

```c
  /* bsd/kern/kern_exec.c:5001 load_init_program_at_path(proc_t p, user_addr_t scratch_addr, const char *path) */
      size_t path_length = strlen(path) + 1;            /* 15 for "/sbin/launchd" */
      argv0 = scratch_addr;
      error = copyout(path, argv0, path_length);        /* kern_exec.c:5020 -- the first thing it does */
```

and `scratch_addr` is the page `load_init_program` allocates one statement earlier
(`bsd/kern/kern_exec.c:5127`):

```c
      mach_vm_offset_t scratch_addr = 0;
      (void) mach_vm_allocate_kernel(map, &scratch_addr, map_page_size, VM_FLAGS_ANYWHERE, VM_KERN_MEMORY_NONE);
```

**`0x1000` is the value XNU's own allocator produces here, and that is a source-level fact rather than an
inference** — `osfmk/vm/vm_user.c:182-184`, `mach_vm_allocate_kernel`'s ANYWHERE case:

```c
		map_addr = vm_map_min(map);
		if (map_addr == 0)
			map_addr += VM_MAP_PAGE_SIZE(map);
```

with `osfmk/mach/arm/vm_param.h:143` `VM_MIN_ADDRESS = 0` for ARM32: process 1's map is empty, `vm_map_min`
is 0, and the first page handed out is `0x1000` — which is exactly `dfar`. So the fault is not a bad
pointer, a truncated one, or a driver's; it is the demand fault of a page the kernel has just created a map
entry for and has not yet populated.

## Why this is the instrument's stop, and not the kernel's

XNU services this fault. The first-level handler is `osfmk/arm/locore.s:992` `fleh_dataabt`, whose
kernel-mode branch (`dataabt_from_kernel`, entered because `spsr`'s mode is not `PSR_USER_MODE`) saves the
state, reads DFSR/FAR, and calls the second-level handler at `locore.s:1140`:

```
	mov		r1, T_DATA_ABT
	bl		EXT(sleh_abort)
```

`osfmk/arm/trap.c:274` `sleh_abort` then does exactly three things that matter here: `status = 0x005` is
`FSR_SFAULT`, which `TEST_FSR_VMFAULT` accepts (`proc_reg.h:286`); the fault is in kernel mode, so it takes
the `(spsr & PSR_MODE_MASK) != PSR_USER_MODE` branch; and with `vaddr = 0x1000` not a kernel address it
picks `map = thread->map` and calls `vm_fault(map, 0x1000, VM_PROT_READ | VM_PROT_WRITE, …)`. On success it
`goto exit`s, `locore.s` returns through `load_and_go_sys`, and **the `strb` is retried** — the page is
there on the second attempt. `copyout` into a freshly `vm_map_enter`ed user page is the case this path
exists for.

All of that code is in this image and none of it ran:

```
80006a3c T fleh_dataabt          <- installed: entry_stubs.c:5030, records and leaves
800146d0 T locore_fleh_dataabt   <- XNU's, locore.s:992, renamed and referenced by nothing
80437528 T sleh_abort            800936e4 T vm_fault            8004fadc T vm_map_enter
8008a520 T mach_vm_allocate_kernel
```

The vectors are the instrument's by construction: `start.s` patches the vector page from the image's own
`ExceptionVectorsTable` (`0x8055c000`, `.bss`), which `entry_vectors.s` fills with the `entry_stubs.c`
handlers. So a data abort in this image is always a report and never a retry — which is what the same
design was for while the boot was still in `bsd_init`, and is now the thing standing between the boot and
`execve`.

`xnu_entry_why = 0x80462a20` is the string `"exception: data abort"` in the image, i.e. the banner and the
report are one record; `_abort_first_kv_len = 0x00001fea` equals `xnu_entry_kv_written`, so nothing was
recorded between the fault and the report. `panic_entered = 0`: XNU's own `panic()` did not run.

## The two defects this step produced, and the checks they replaced

**The pool adopted an object no tool in the tree writes.** The first build after the config change failed
in the *link*, three steps from the cause: `multiple definition of '_start'` between
`out/xnu_asm_obj/xnu_arm_start.o` and `out/stage90/xnu_arm_start.o`. The first of those was a scratch
object from 09:36 that morning, hand-assembled during this step's own investigation (it lacks
`L_telemetry_needs_record`, which is how it is identified as pre-change), sitting in the directory the
pool globs — and the pool had counted **435** objects where every other build counts 434. The glob cannot
tell a kernel object from a file that happens to be there, so the check is now structural and part of the
compilation it selects from: `tools/assemble_arm_layer.sh` writes `<name>.log` beside every `<name>.o` it
builds, success or failure, so an `.o` in that directory with no `.log` beside it was written by something
else and is reported and skipped:

```
  the whole kernel: 434 object(s) added, 286 already named above,
  refused by name: iokit_KernelConfigTables.o locore.o start.o; in the assembly directory but not written
  by the assembler (no .log beside it): xnu_arm_start.o
```

Both directions were measured: with the scratch object present the guard fires, the pool is 434 and the
link succeeds; with it removed the line is absent and the pool is 434 again (`.text` 5118848, image bytes
5323784, identical both ways).

**A check whose input is its own output.** The keep-name check as first written asked whether each of the
three names was in `out/stage90/xnu_arm_entry_undef.txt` — the file *this build writes*, and which on a
second run already contains the previous run's renaming. It passed once and failed on its own success
(`'thread_bootstrap_return' is not in the previous build's undefined list`). It is replaced by the two
measurements named above, neither of which reads anything the build produced. The same shape is recorded in
`tools/assemble_arm_layer.sh`'s history — a de-underscore rule keyed on the linker's undefined list renamed
2 symbols on a clean tree and 22 on the next run — and this is the second time it has been paid for.

## The build, and the run

465's sequence, unchanged: platform objects
(`XNU_KERNEL_CONFIG=STAGE90_XNU XNU_MASTER_LOCAL=$PWD/tools/xnu_config/boot/STAGE90_XNU.local
./tools/build_xnu_arm_kernel.sh --platform-only`), then `build_entry.sh` with
`STAGE90_ENTRY_REAL_ARM_INIT=1 STAGE90_ENTRY_TRACE=1`, then `build.sh` with
`STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1'`.

Entry image `.text` 5103168 → **5118848** (+15680: locore.o's text less the four vector-table names the
image already had), image bytes 5307400 → **5323784** (+16384), `.bss` `0x80510000..0x805a5f58` →
`0x80514000..0x805a9f58` (**614232 bytes, unchanged** — the image moved, the bss did not grow), `headroom`
1417384 → **1401000**, pass 1 **24** undefined, 40 wraps (37 reached, 1 same-object, 1 never called, 1 by
address, none dead — unchanged), `g_kv_buf` still `0x8055a000`; entry `.bin` 5323784 bytes, sha256
`c23bcee4809590091f620cc6251371daef62d3d2cb7d1f6e283a68a732dbd76f`. Payload `stage90-qcdt.img`
**8343552** bytes (8327168 + 16384), md5 `fd1dff405a01033822e9d3a844a6a7fd`, sha256
`6631d5ae46b4293250038656c7b025dfac9216b9bc3592b5af11bf39c094a301`. One `fastboot boot` through
`preflight_boot_check.sh --allow-xnu-entry` and `run_and_capture.sh --allow-xnu-entry`; log 478248 bytes;
`No errors detected`; exit 0; device back on Android 10 by itself.

The instrument's own counters: `xnu_entry_kv_written = _in_dram = 0x1fea` (8170 — the last whole record
that fit under 466's `+2u` bound, 21 bytes of the 8192-byte buffer unused), `_dropped = 0x80f9` (33017),
`live_records = 0x35b` (859) with `live_refusals = 0`, `block_count = 0x44` (68) with `block_returned =
0x0d` (13). The last live record before the fault is a *background* thread, not the faulting one: thread
`0xc05a1000` returning from `lck_mtx_sleep+0x84` entered at `ipc_mqueue_receive+0x6c` (the ux handler's
receive). `ostext_chars = _total = 0x43f` (1087), `_lines = 0x16` (22), `_heals = 1`, `_limited = 0`.

## What is not measured

1. **That the faulting thread is the one that ran `load_init_program`.** `spsr`'s mode is SVC, `dfar` is the
   scratch page, and the console's last line is that function's own `printf`; the report records no thread
   and no `r0`/`r1`, so "the current thread here is process 1's" is the source chain plus `thread->map`
   being the map the allocation went into, not a reading.
2. **That `vm_fault` would succeed.** The claim that XNU recovers from this fault is an argument from
   `sleh_abort`'s source and from the fact that this is the demand-fault path for a fresh map entry; it is
   not measured, because the only way to measure it is to install XNU's handlers — which is the next step,
   and its outcome is the measurement.
3. **The 8 characters.** `ostext_chars` is 1034 → 1087 while the added line is 45 characters; the capture's
   one heal (`_heals = 1` in both runs) rewrites one pointer-shaped token, which is the only other thing
   that moves that total between runs. This document does not claim to have accounted for the remaining 8.
4. **Whether the boot returned to user mode at all.** It did not need to: `load_init_program` is reached in
   kernel mode along the AST path, and `spsr = 0x20000013` says the fault was in kernel code. Nothing in
   this run is evidence about `load_and_go_user`'s own return to user mode, which is still unmeasured.
5. **The 24 remaining stand-ins.** None was entered, so their correctness is exactly as unmeasured as it was
   in 465 — `_kprintf`, `_hashLookupTable_new`, the five `chudxnu_*`, `mach_msg_destroy_from_kernel`, the
   `UND*_rpc` group, `osrelease`/`ostype`/`version_major`/`version_minor`, `_enable_kernel_vfp_context`,
   `_lastkerneldataconst`(+`_padsize`), the two `__llvm_profile_*`, and the three `upl_get_internal_*`.

## Next: 467 — the vectors, so that a page fault can be a page fault

1. **The object of the step is the vector table.** `locore.s:992`'s `fleh_dataabt` and its second-level
   `sleh_abort` are in this image and are unreachable by design; the installed `fleh_dataabt` is the
   instrument's. 467's job is to install the kernel's own first-level handlers for the aborts the boot now
   takes on purpose — which means the ten `locore_fleh_*` names have to become reachable by the thing that
   patches the vector page, and `ExceptionVectorsTable` (the image's, `0x8055c000`) has to say so. The
   instrument does not have to lose the trap: the honest form is XNU's handler as the vector with the
   report kept on the path that XNU itself would panic or debugger-trap on.
2. **The three things XNU's handlers need, and which of them this boot has.** The first-level handlers run
   in ABT/UND/IRQ mode and take their stack from the per-mode stacks `start.s` sets up; they read
   `ACT_CPUDATAP` through `TPIDRPRW`, which `arm_init` writes; and they need `thread->recover` and
   `thread->map`, which exist because the boot got this far. Whether all four are in the state
   `fleh_dataabt` expects is the step's first measurement, and it is checkable statically against
   `start.s`/`arm_init` before the run.
3. **Then the exec, and the first user thread.** With the fault retried, the remaining path is
   `copyout` → `execve` → `exec_mach_imgact` → the Mach-O loader on a RAM disk that has no launchd in it;
   the `panic("Process 1 exec of /sbin/launchd failed, errno %d")` after the loop is a foreseeable end, and
   a named one. Entering user mode is the step after that, and it is where `load_and_go_user`'s own
   unmeasured half sits.
4. Still owed from 461-465: 448's `_bad` slots as a pair of keys naming each one's sense; the timer
   (`ml_init_timebase` + an MSM8974 `tbd_ops_t` over the GPT at `0xf9020000`, 19.2 MHz, IRQ 19);
   `IOCPUInterruptController`; the pthread table's other 37 slots; `osfmk/kperf/kperfbsd.c`; making the
   epilogue's key list a data table; and the kalloc tracer's 8 KB buffer, which is full from the IOKit
   bring-up onward and therefore costs the report every `entry_kv` record written after it.
