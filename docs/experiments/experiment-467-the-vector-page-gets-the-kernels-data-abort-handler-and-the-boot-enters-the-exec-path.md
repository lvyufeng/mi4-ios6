# Experiment 467 — the vector page gets the kernel's own data-abort handler, and the boot enters the exec path

**Status: on the device, one run, 483896 bytes of log, exit 0, device back on Android by itself (Android
10, `No errors detected`).** One change: the fourth slot of the entry image's exception vector page no
longer points at this project's `fleh_dataabt` — which records a data abort and leaves — but at the
renamed copy of Apple's own `locore.s:992` handler that 466 linked, which reads the fault, calls
`sleh_abort(regs, T_DATA_ABT)` and, when the page is paged in, **returns through `load_and_go_sys` and
retries the instruction**. The run's first data abort is byte for byte 466's (`dfar=0x1000`,
`dfsr=0x805`, the `strb` in `copyout` that `load_init_program_at_path` does before anything else), and
this time it is **serviced**: `sleh_abort` is entered three times and returns three times, `copyout`
succeeds on its second attempt, and the boot runs on into `execve`, `exec_activate_image` and the
image activators — a place no earlier run has reached. It ends at XNU's own named panic, not at an
instrument's report: `/sbin/launchd` opens, reads and is **not claimed by any activator**, and
`load_init_program`'s loop panics with `errno 8`. The trap's report survives that panic by the route
461 measured — `panic()` → `DebuggerTrapWithState`'s `udf`, slot 1, still this file's handler — and it
names the message (`panic_len = 0x1295`, `_dropped = 0`). **Not one stand-in was entered during the
whole boot.**

## The change, in two halves

**1. The slot.** `entry_vectors.s`'s eight trampolines each load a handler address from a literal in
the vector page. Seven of them load this image's `entry_stubs.c` handlers, whose design is to record
and leave; the eighth, slot 4, now loads `locore_fleh_dataabt`. The file says why, at the slot, and
the reason is the one thing about this instrument that has to change shape as the boot advances: while
the frontier *was* the first fault, recording it was the whole point; now that the fault is a demand
fault on a page the kernel has just created a map entry for, recording it **is** the stop. XNU's
handler is not a second instrument: `sleh_abort` (`osfmk/arm/trap.c:274`) is the kernel's own
page-fault path, and on the frame-of-reference check it either services the fault or panics.

**2. The record, kept on the path XNU would call fatal.** The instrument does not lose the trap, and
that is a property of XNU rather than of this file: a `sleh_abort` that cannot answer the fault panics
(`trap.c:313`, `:393`, `:464`), and `panic()` reaches `DebuggerTrapWithState`, whose `udf` is slot 1 —
still this image's `fleh_undef`, still writing the trap's own buffer. What the kernel *decided* is
measured on the other side by `--wrap=sleh_abort` (`entry_trace.c`), one record per entry and one per
return, and the wrapper's `__real` target is `trap.c`'s `sleh_abort` because the wrap is a link-time
rule and not a copy.

**The build checks it structurally, in both directions.** The check reads the eight
`vec_tramp_N_handler` literals out of the linked ELF (by index into `objdump -s`'s 16-byte lines,
since the literals are 4-byte and not 16-byte aligned), asserts each is inside the one 4 KB page that
`start.s` maps at `0xffff0000`, and compares each against `nm`:

```
  xnu_entry_467: the vector page carries the handlers it says it does: slot0=fleh_reset
  slot1=fleh_undef slot2=fleh_swi slot3=fleh_prefabt slot4=locore_fleh_dataabt
  slot5=fleh_addrexc slot6=fleh_irq slot7=fleh_decirq; slot 4 is Apple's locore_fleh_dataabt and
  not this image's fleh_dataabt (0x80006c18), and slot 1 (fleh_undef) is still this image's - so a
  fault the kernel cannot service still reaches the trap's own buffer through panic()'s udf
```

Slot 4 is asserted **both ways** — it must be `locore_fleh_dataabt` and must not be this image's
`fleh_dataabt` — because the failure this step exists to remove is the second one, and a check that
only named the first would pass if the literal had been left alone. The wrap census goes from 40 to
**41** symbols (38 reached by a branch, unchanged otherwise), and the reachability is not taken on
trust: `objdump -d` finds four `bl __wrap_sleh_abort` sites, two inside `locore_fleh_prefabt` and two
inside `locore_fleh_dataabt`, which is exactly where `locore.s` calls `sleh_abort` (`T_PRE_ABT` and
`T_DATA_ABT`). The prefetch slot is still this image's, so only the data-abort pair is on a live path,
and the pair is the one this step is about.

## What the run says: three aborts, all serviced, and then the exec

The live channel records each entry and each return (`xnu_live_sleh_*`, `entry_live_write`):

```
 xnu_live_sleh_seq=0x00000001   type=0x00000004  dfsr=0x00000805  dfar=0x00001000  thr=0xc0672080
 xnu_live_sleh_back=0x00000001  at_back=0x00000001
 xnu_live_sleh_seq=0x00000002   type=0x00000004  dfsr=0x00000807  dfar=0xc82dd000  thr=0xc0672080
 xnu_live_sleh_back=0x00000002  at_back=0x00000002
 xnu_live_sleh_seq=0x00000003   type=0x00000004  dfsr=0x00000807  dfar=0xc831e000  thr=0xc0672080
 xnu_live_sleh_back=0x00000003  at_back=0x00000003
```

`type = 4` is `T_DATA_ABT`. The first fault is 466's, exactly — `dfsr=0x805` is `FSR_SFAULT` with
`DFSR_WRITE`, `dfar=0x1000` is the page `mach_vm_allocate_kernel` hands out first in process 1's empty
map. The second and third are `0x807` → `FSR_PFAULT` (4 KB-granular translation, `proc_reg.h:270`),
also writes, at kernel addresses. **All three returned**: `xnu_entry_sleh_back = 3` in the epilogue,
`xnu_entry_sleh_storm = 0`, and the epilogue's `seq` and `back` agree.

The count is a check in itself. The live channel's records go 859 → **880** (+21) and the new
instrument writes exactly 21 records for three aborts: `entry_note_sleh` writes five each
(`seq`, `type`, `dfsr`, `dfar`, `thr`) and `entry_note_sleh_back` two (`back`, `at_back`), 3 × 7 = 21 —
so the whole delta of the live channel is accounted for by the three faults and their returns, and
nothing else moved in it.

That the first fault was *answered* rather than only reported is what the OS console says. It gains
exactly one line over 466, and the line is two frames into the function the fault was in:

```
 466  22 lines   ... load_init_program: attempting to load /sbin/launchd
 467  23 lines   ... load_init_program: attempting to load /sbin/launchd
                   load_init_program: failed loading /sbin/launchd: errno 8
```

`kern_exec.c:5164` then `:5169`, the loop's two `printf`s. Between them,
`load_init_program_at_path` (`kern_exec.c:5001`) does three `copyout`s and calls `execve` — the first
of those three is the `strb` that 466 stopped on, so the boot is past it by two statements. The
tracer's own buffer agrees from the other side: `_dropped` (records refused by the 8 KB buffer, which
is full from the IOKit bring-up onward) goes 33017 → **33079**, i.e. **62 more recorded events than
466 reached**, and `stub_hit_count = 0` with every `stub_caller_*` key zero again says none of those
events was a stand-in.

## Where it stops: XNU's own panic, and its cause is a level lower than the fault

```
 MI4IOS6_STAGE90_XNU real XNU entry: exception: undefined instruction
 xnu_entry_panic_entered=0x00000001   xnu_entry_panic_len=0x00001295   _dropped=0x00000000
 xnu_entry_undef_pc=0x800355ac        xnu_entry_undef_lr=0x800355b0
 xnu_entry_undef_spsr=0x60000093
 xnu_entry_why=0x80462a1c             (= "exception: undefined instruction", this image's banner)
 xnu_entry_trap_r9_fmt=0x804a0766     ("Process 1 exec of %s failed, errno %d")
 xnu_entry_panic_arg0=0x804a0919      ("/sbin/launchd")   _arg1=0x00000008
```

`pc = 0x800355ac` is `DebuggerTrapWithState`, and the instruction there is the `udf` the whole design
predicted:

```
800355a8:  eb00000f  bl   800355ec <DebuggerSaveState>
800355ac:  e7ffdefe  udf  #65006
```

So XNU's fatal path was taken, the trap was reported by the slot that is still this image's, and the
report is complete (`_dropped = 0`). The panic's format string and its first argument are read out of
the image, and they name the frontier in one line: **process 1's exec of `/sbin/launchd` failed with
errno 8**, from `kern_exec.c:5173` — the line after the `init_programs[]` loop runs out.

`errno 8` is `ENOEXEC`, and which `ENOEXEC` it is matters, because the obvious reading is wrong. The
loader's *malformed Mach-O* answer is not this one: `load_return_to_errno` maps `LOAD_BADMACHO` to
`EBADMACHO` (`kern_exec.c:5203`). `ENOEXEC` here comes from the flavor loop:

```c
  /* kern_exec.c:1473 */  error = -1;
  /* kern_exec.c:1474 */  for (i = 0; error == -1 && execsw[i].ex_imgact != NULL; i++) {
  /* kern_exec.c:1476 */      error = (*execsw[i].ex_imgact)(imgp);
  ...
  /* kern_exec.c:2803 */  } else if (error == -1) {
  /* kern_exec.c:2805 */      error = ENOEXEC;      /* "Image not claimed by any activator" */
```

and all three activators in `execsw[]` decline the file for the same reason — its first four bytes
are zero:

- `exec_mach_imgact` (`kern_exec.c:855`): `magic != MH_MAGIC && magic != MH_MAGIC_64` → `error = -1`;
- `exec_fat_imgact` (`kern_exec.c:643`): `OSSwapBigToHostInt32(magic) != FAT_MAGIC` → `error = -1`;
- `exec_shell_imgact` (`kern_exec.c:477`, `:482`): the first bytes are not `#!` → `error = -1`.

So the exec got all the way to `namei` on `/sbin/launchd` succeeding, `exec_check_permissions`
passing, and a page of the file being read — and then every activator looked at those bytes and said
"not mine". The bytes are the answer, and they are known without guessing: **the RAM disk is
`g_stage90_ramdisk`, 0x40000 bytes of `.bss`, and nothing in this build ever writes a byte of it**,
while mockfs serves that file node straight out of the memory device's physical pages:

```c
  /* bsd/miscfs/mockfs/mockfs_fsnode.c:333-342, for a memory-backed mount and a MOCKFS_FILE node */
  rvalue = pager_map_to_phys_contiguous(ubc_mem_object, 0,
              (mockfs_mnt->mockfs_memdev_base << PAGE_SHIFT), fsnp->size);
```

with the node's size taken from the device (`mockfs_fsnode.c:80`,
`mp->mnt_devvp->v_specinfo->si_devsize`). `/sbin/launchd` is therefore not a missing file and not an
unreadable one: it is 256 KB of zeros, and it resolves — `mockfs_lookup` maps `sbin` to the root node
and `launchd` to the file node (`mockfs_vnops.c:119-124`), which is why the errno is `ENOEXEC` and not
`ENOENT`.

## The two defects this step produced

**The epilogue's constant pool, again, and by 4096 exactly.** Appending twenty-five `entry_write_kv`
calls to `entry_epilogue` stopped the build with one error, `bad immediate value for offset (4096)`
against `/tmp/cc36Ep2d.s:3127`. The message names a line of compiler output and not the function that
grew, and reading it back is what identifies it: the instruction is `ldr r0, .L277` inside
`entry_epilogue`, `.L277` is that function's own constant pool emitted after its body, and the two are
**exactly 4096 bytes apart, one byte past the ±4095 PC-relative range** — the same edge 455 hit, found
by the same means. Compiling the file with `-S` alone succeeds, which is why the failure is only ever
in the build. The fix is the one 455 chose and recorded as the categorical problem's stopgap: the
twenty-five keys and their comment move into `entry_write_269_kv()`, called at exactly the position
they occupied, so the key order in the buffer is unchanged. `.text` 5118848 → **5119200** (+352: the
new function and one `bl`).

**A check that stopped the build for a reason about the tool.** The new 467 check's first run failed
with `arm-none-eabi-objdump: --stop-address: bad number: 80001f10` — its own address, printed back
without the `0x` that the `--start-address` beside it had, so `objdump` read it as decimal and refused
it. It is a one-character defect and it is recorded rather than quietly fixed, because the check exists
to be the thing that fails when the vector page is wrong and instead it failed on the shape of its own
invocation. The guard that makes it a *reported* failure rather than a silent one is in the helper: it
returns non-zero when it cannot read the word, and the caller turns that into
`layout_fail "the 4 bytes at vec_tramp_N_handler could not be read out of the ELF"` rather than
comparing against an empty string.

## The build, and the run

466's sequence, unchanged: platform objects
(`XNU_KERNEL_CONFIG=STAGE90_XNU XNU_MASTER_LOCAL=$PWD/tools/xnu_config/boot/STAGE90_XNU.local
./tools/build_xnu_arm_kernel.sh --platform-only`), then `build_entry.sh` with
`STAGE90_ENTRY_REAL_ARM_INIT=1 STAGE90_ENTRY_TRACE=1`, then `build.sh` with
`STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1'`.

Entry image `.text` 5118848 → **5119200**, image bytes **5323784 (unchanged** — the 352 bytes fit in
the same page-rounded image), `.bss` `0x80514000..0x805a9f58` (**614232 bytes, byte-identical to
466's** — the six new globals at `0x80515068` are added before `g_stage90_ramdisk`, whose 4096-byte
alignment absorbs them and leaves everything from the disk onward where it was), `headroom` **1401000**
(unchanged), pass 1 **24** undefined (unchanged), 41 wraps, `g_kv_buf` still `0x8055a000`,
`ExceptionVectorsTable` still `0x8055c000`, `g_panic_buf` still `0x80558000`. Entry `.bin` 5323784
bytes, sha256 `fe3d9ac551a3e1d8bc682b20d15021905ce18f75ad0a7de68c44cc6dffb704b3`. Payload
`stage90-qcdt.img` **8343552** bytes (unchanged size), md5 `b2176ce70f9be60a862c3d9b3ad0e311`, sha256
`a3fcb097e206a877fae3fa6fd9f6652c01936628b4d863b28360b3ce5d5c1de1`. One `fastboot boot` through
`preflight_boot_check.sh --allow-xnu-entry` and `run_and_capture.sh --allow-xnu-entry`; log 483896
bytes; `No errors detected`; exit 0; device back on Android 10 by itself.

The instrument's other counters: `kv_written = _in_dram = 0x1fea` (8170, the same last whole record as
466 — the buffer still fills during the IOKit bring-up, and the kalloc tracer's 8 KB buffer is still
the reason the report cannot hold what this step writes), `live_records = 0x370` (880) with
`live_refusals = 0`, `block_count = 0x44` with `block_returned = 0x0d` (unchanged),
`ostext_chars = _total = 0x479` (1145), `_lines = 0x17` (23), `_heals = 1`, `_limited = 0`,
`zone_map_min = 0xc05f6000`, `zone_map_max = 0xc0834000`.

## What is not measured

1. **The instruction behind the second and third faults.** The wrapper reads DFSR, DFAR and
   `TPIDRPRW` and not the PC out of `regs`, so the two `FSR_PFAULT` writes at `0xc82dd000` and
   `0xc831e000` are recorded by address and not by site. Their neighbourhood is suggestive — the
   panic's own varargs page is `0xc82b3000` (`_panic_args_page`, `_panic_ap = 0xc82b3ed4`) — but
   "these are the same allocation path" is a reading of nearby numbers, not a measurement, and it is
   the first thing the next instrument on this path should add.
2. **That the faulting thread is process 1's.** `thr = 0xc0672080` is the same thread for all three
   faults, and the console line before the first one is `load_init_program`'s own `printf`, but the
   report records a thread pointer and the claim that it is process 1's is the source chain plus
   `thread->map`, not a reading.
3. **That `vm_fault` answered the first fault.** What is measured is that `sleh_abort` was entered
   with `FSR_SFAULT` and returned, and that the `copyout` after it completed — the second is strong
   and indirect. The retry itself is inside `load_and_go_sys` and is not instrumented.
4. **62 recorded events, unenumerated.** `_dropped` says the boot produced 62 more records than 466
   reached; the buffer they were refused from is full, so which 62 they were is not in this run's log.
5. **Whether the boot returned to user mode at all.** It still has not: every abort so far is in
   kernel mode (`spsr` SVC), and `load_and_go_user`'s own return to user mode remains unmeasured.
   The exec path is the last thing between the boot and that measurement.
6. **The 24 remaining stand-ins.** None was entered, so their correctness is exactly as unmeasured
   as in 466.

## Next: 468 — a Mach-O for process 1, so that `execve` can succeed

1. **The object of the step is the RAM disk's first bytes.** Nothing else has to change for the
   frontier to move: mockfs's file node is a window on the memory device's physical pages
   (`mockfs_fsnode.c:333-342`) and its size is the device's size (`:80`), so whatever occupies offset 0
   of `g_stage90_ramdisk` **is** `/sbin/launchd`, byte for byte, with no filesystem in between. A
   `MH_MAGIC`/`MH_EXECUTE`/`CPU_TYPE_ARM` header there is the difference between `error = -1` at
   `kern_exec.c:855` and `exec_mach_imgact` claiming the image.
2. **What that reaches, and what it needs.** Claiming the image runs `load_machfile` →
   `load_segment` → `pmap_create` for the new task's map, `vm_map_enter` for each segment, the
   `LC_UNIXTHREAD` state, and then `thread_bootstrap_return` — which 466 made real — for the first
   time with a user PC. A static ARMv7 executable with no `LC_LOAD_DYLINKER` is the honest first
   target: the dylinker path needs a second Mach-O out of the same 256 KB. Whatever it is, it should
   prove user mode where it can: a `svc` (`osfmk/arm/locore.s`'s `fleh_swi`, slot 2, is this image's
   handler) or a write to a user address the kernel is watching is worth more than a `b .`.
3. **The instrument this step wants first.** The `sleh_abort` wrapper should record the faulting PC
   out of `regs` (item 1 above) before the exec path adds more faults to the same record; and the
   exec path's own record — which activator declined and why — is a `--wrap` on
   `exec_activate_image`'s three activators, or on `load_machfile`, rather than a guessing game from
   `errno 8`.
4. Still owed from 461-466: 448's `_bad` slots as a pair of keys naming each one's sense; the timer
   (`ml_init_timebase` + an MSM8974 `tbd_ops_t` over the GPT at `0xf9020000`, 19.2 MHz, IRQ 19);
   `IOCPUInterruptController`; the pthread table's other 37 slots; `osfmk/kperf/kperfbsd.c`; making
   the epilogue's key list a data table (three times now the reason a build stopped); and the kalloc
   tracer's 8 KB buffer, which is full from the IOKit bring-up onward and therefore costs the report
   every `entry_kv` record written after it.
