# 525: pid 1 runs - the entry pc is the program's first instruction, and its faults are its own page's first touches

**This is a reading, not a step.** No image, no switch, no device. It re-reads the log experiment 520
left behind (`/tmp/cancro-last_kmsg.txt`, 596 665 bytes / 8 919 lines, ending `No errors detected`)
against the sources in this tree, and it corrects two sentences that are in circulation and that
matter, because both of them are load-bearing for what the next arm should be.

The log's provenance is checkable and was checked: it carries `xnu_live_slot_rtcpre_pop = 0x04b79075`
with `_calls = 1` and `_thr = 0xc0573df0`, which is 520's own reading; it has no `xnu_live_slot_cwe_*`
and no `xnu_live_slot_post_*` and no `_rin` key, so it cannot be from the 522 build; and its mtime
(02:55) is when 520 came back.

## 1. The finding, stated once

Two sentences are wrong, and each one points the phase at the wrong next step:

* **「pid 1 執行它自己的 Mach-O header」.** 523 §4 says *"`__TEXT` has fileoff 0, so pid 1 executes its
  own header and faults at `0x1118`/`0x1124`/`0x11a4`."* **`pc = 0x10e0` is `entry_code` - the
  program's first instruction.** The 28-byte `mach_header` and its three load commands occupy
  `0x1000`..`0x10E0`; the code begins exactly where they end, and the entry pc is defined in the
  assembler as an expression over that boundary, not as a number written down.
* **Those three addresses are not a failure.** `0x1118` and `0x1124` are the two first touches of the
  page `mmap` returned, serviced and retried like every other demand fault in this boot. And **pid 1's
  program ran to completion** - all seventy-nine instructions - with every syscall it was built to
  check returning the value the fixture was built to check it against, including a driver moving
  data into the process's own page.

What is left is smaller and more specific than either sentence allows: the boot dies at the idle
exit's `pop` (522's arm, unchanged), and separately the run contains exactly one user-mode prefetch
abort, at the instruction `fork` returns at.

## 2. `pc = 0x10e0` is `entry_code`

`stages/stage90/xnu_arm_boot/entry_ramdisk.s` builds the Mach-O by hand, and it never writes the two
numbers that decide this:

| definition | line | what it is |
| --- | --- | --- |
| `.equ TEXT_VMADDR, 0x1000` | `:938` | the segment's `vmaddr`, and the page the header lands on because `__TEXT` has `fileoff 0` |
| `.equ sizeofcmds_value, (load_commands_end - g_stage90_ramdisk) - 28` | `:1276` | the commands' own extent, asserted `== 0xC4` at `:1284` |
| `.equ entry_pc_value, TEXT_VMADDR + (entry_code - g_stage90_ramdisk)` | `:1277` | the entry point, as a file offset plus the segment base |
| `.if (entry_code_end - entry_code) != 316` | `:1318` | the program is 316 bytes = 79 instructions |
| `.zero RAMDISK_BYTES - (. - g_stage90_ramdisk)` | `:1326` | padded to `RAMDISK_BYTES` 0x2000, because `si_devsize` is the file's size |

28 (header) + 0xC4 (commands) = 224 = 0xE0, so `entry_code` is at file offset 0xE0 and
`entry_pc_value` = 0x1000 + 0xE0 = **0x10E0** - not a chosen address, the arithmetic of the layout.

The log agrees from the other side: `xnu_live_getpid_seq = 1`, `value = 0x00000001`, `error = 0`. The
instruction at 0x10e0 is the listing's `+0  svc #0x80` (getpid), and it returned the pid the kernel
gave the process. **An instruction inside the load commands cannot make a syscall.**

So the sentence to retire is not "there is a fixture" - it is the inference that a `fileoff 0`
`__TEXT` puts the *entry* inside the header. It puts the *header* there; the entry is one byte past
the commands by construction.

## 3. What pid 1 actually did

Every reading below is 520's own, from the live channel, and the keys are the fixture's own
instruments (`entry_stubs.c`). The program's annotated listing is `entry_ramdisk.s:120-` ; the
offsets in the third column are `pc - 0x10E0`.

| call | offsets | reading from 520's log |
| --- | --- | --- |
| `getpid()` | +0 | `seq 1`, `value 0x00000001`, `error 0` |
| `mmap(0, 0x1000, 3, 0x1002, -1, 0x5a5a pad, 0)` | +12..+48 | `seq 1`, `error 0`, **`value 0x00102000`**, `thread 0xc05aeb10`, `map 0xc00012a0`, `pmap 0xc056b130`, `arg5 0x00005a5a` |
| the read of that page | +56 = `0x1118` | data abort, `far 0x00102000`, serviced - §4 |
| the write of that page | +68 = `0x1124` | data abort, `far 0x00102000`, serviced - §4 |
| `poll(NULL, 0, 5)` then `poll(NULL, 0, 40)` | +88..+124 | `poll_seq 1` then `2`, `nfds 0`, `retval 0`, `before 0x04a20455`, `after 0x04af5dcb`, **`ticks 0x000d5976`** |
| `open("/dev/rmd0", 0, 0)` | +128 | `open_seq 1`, `path 0x0000121c`, **`fd 0`**, `error 0` |
| `read(0, 0x102000, 4)` | +148 | `read_seq 1`, `ret_lo 4`, `error 0`, **`word_before 0x00102000` -> `word_after 0xfeedface`**, `copy_before 0`, `copy_after 0` |
| `open("/dev/nosuch", 0, 0)` - the control | +164 | `open_seq 2`, `path 0x00001228`, **`error 0x00000002`** (ENOENT) |
| `fork()` | +184 | `fork_seq 1`, `ret_lo 0x00000002` |
| the child's `exit(3)` | +208 | `exit_seq 1`, `exit_pid 2`, `exit_rval 0x00000003` |
| `wait4(2, 0x102000, 0, NULL)` | +220 | `wait_seq 1`, `who 1`, `pid 2`, `status_ptr 0x00102000`, `rusage 0` |
| the kernel's SIGCHLD | - | `sigchld_seq 1`, `from 0`, `to 1`, `signal 0x00000014`, `psignal_calls 1` |
| the second `wait4` | +260 | `wait_seq 2` |

Three of these are worth naming separately because they are not "a syscall returned":

* **`word_before 0x00102000` is the fixture's own store, read back through the kernel.** The source
  says why this pair exists (`entry_stubs.c:4603-4613`): *"The fixture's buffer is the page 480's
  `mmap` returned and 480's own store wrote the page's address into (`str r0, [r0]`), so before the
  call the first word is a number this image knows - the mapping the kernel chose - and after it the
  word is whatever `mdevrw`'s `uiomove64` copied out of the RAM disk."* Both halves are present and
  both are the expected values: the kernel chose `0x102000`, the process's own `str` put it on the
  page, and `read` replaced it with `0xfeedface`, the RAM disk's `MH_MAGIC` - the same bytes the
  kernel loaded the program from.
* **`copy_before = 0` and `copy_after = 0`** are the instrument's statement that the kernel's own
  `copyin_word` read the page successfully; non-zero would have been an `EFAULT` and
  `word_*` would read `0xFFFFFFFF` = *not read*.
* **`mmap`'s `value` and the fault's `far` are the same number.** `0x102000` is returned as a mapped
  address and immediately faults once on read and once on write, then works. That is the demand-fault
  path, not a bad address - and §4 shows the source says so in as many words.

So the honest description of the far side of the idle exit is not "a fixture root with nothing to
run". It is **an OS that mounts a fixture root, execs a hand-built static ARMv7 Mach-O, and runs it
in user mode through mmap, poll, open, read, fork, wait4 and exit - with a driver moving bytes into
the process's own page.** What the goal's 「把基础驱动跑起来」 is still missing is the *console* and
any I/O Kit driver, which is 523 §2 / 524's item and not this one.

## 4. The two faults are the page's first touches, and the source says so

The run serves nine aborts. The first four are the boot's documented control, and they reproduce
`entry_stubs.c:1612-1615`'s list: `0x805/0x1000`, `0x807/0xc8215000`, `0x807/0xc8256000`,
`0x805/0x00101f28` against the file's `0x805/0x1000`, `0x807/0xc8105000`, `0x807/0xc8146000`,
`0x805/0x00101f28` - the first and fourth exactly, the middle two at different addresses in the same
shape, which is what a control taken from an earlier run should look like. `frame_ok = 1` on all four.

Then, in order:

| `seq` | `type` | `fsr`/`far` | `pc` | `user` | `recover` |
| --- | --- | --- | --- | --- | --- |
| 1 | 4 `T_DATA_ABT` | `0x805` / `0x00001000` | `0x800176c0` | 0 | `0x800177cc` |
| 2 | 4 | `0x807` / `0xc8215000` | `0x8000d6cc` | 0 | 0 |
| 3 | 4 | `0x807` / `0xc8256000` | `0x8000d398` | 0 | 0 |
| 4 | 4 | `0x805` / `0x00101f28` | `0x800176c0` | 0 | `0x800177cc` |
| 5 | 4 | `0x007` / `0x00102000` | **`0x00001118`** | **1** | 0 |
| 6 | 4 | `0x80f` / `0x00102000` | **`0x00001124`** | **1** | 0 |
| 7 | **3 `T_PREFETCH_ABT`** | `0x005` / `0x000011a4` | **`0x000011a4`** | **1** | 0 |
| 8 | 4 | `0x80f` / `0x00102000` | `0x800176e0` | 0 | `0x800177cc` |
| 9 | storm | `0x005` / `0x04b79074` | `0x04b79074` | 0 | - |

`0x1118` = `entry_code + 56` = `ldr r3, [r0]`; `0x1124` = `entry_code + 68` = `str r0, [r0]`. Both
`far`s are `0x102000`, which is `xnu_live_mmap_value`. `recover = 0` means only that no recovery
address was armed - not that the fault was fatal, because `recover` is non-zero only on a
`copyin`/`copyout` path (`entry_stubs.c:1638-1641`).

The register values decode against the tree's own table, not against a recollection of the ARM ARM.
`osfmk/arm/proc_reg.h:263-283` gives `FSR_SFAULT 0x5` = *Translation Section*, `FSR_PFAULT 0x7` =
*Translation Page*, `FSR_PPERM 0xF` = *Permission Page*, and `DFSR_WRITE 0x800` = *write data abort
fault*. Read that way the two user entries are the two halves of one access:

* **`seq 5`, `fsr 0x007` = `FSR_PFAULT` with no write bit** - a translation fault on a page, taken on
  a *read*, at `pc = 0x1118` = `ldr r3, [r0]`. The first touch of a page that has no translation yet.
* **`seq 6`, `fsr 0x80f` = `DFSR_WRITE | FSR_PPERM`** - a *write*, permission fault on a page, at
  `pc = 0x1124` = `str r0, [r0]`. The same page, now translated but not yet writable, so the store
  faults and is retried after the mapping is upgraded.

So the instrument's `DFSR_WRITE` bit and the listing's `ldr`/`str` agree independently of the two
`pc` values: **the fault register says one read and one write, and the disassembly says one load at
`+56` and one store at `+68`.** That the two `far`s are identical, at the address `mmap` returned, is
what makes it the page's own demand faults rather than anything else in the process.

**They were serviced, and the run proves it twice over.** First, the program continues: `poll_seq = 1`
is on the live channel after `sleh_seq = 6`, and so is every syscall in §3. Second, and independently,
`xnu_live_read_word_before = 0x00102000` is the value `str r0, [r0]` left behind, read back through
the kernel's own copy path with `copy_before = 0`. A store that faulted and was not retried could not
have left that value there.

This is also not a new shape - it is the shape the file was written for. `entry_stubs.c:1545-1546`:
*"466's run produced the other kind: `copyout`'s first store to the page `load_init_program` had just
allocated (`dfar = 0x1000`, `dfsr = 0x805`), i.e. the demand fault XNU pages in and retries."*

**What `seq 7` is, and what it is not.** It is the run's only user-mode prefetch abort, and its `pc`
equals its `far` equals `0x11a4` = `entry_code + 196` = the `cmp r1, #CHILD_FLAG` that **both**
processes resume at after the `fork` `svc`. Its thread (`0xc05ae480`) is not the thread the other
records carry (`0xc05aeb10`), it has `lr = 0` and `sp = 0x101efc`, and it is immediately followed by
the child's `exit_rval 3` and the SIGCHLD. **Whether it was serviced or fatal is not decided by these
readings**, and this log does not claim it: `g_sleh_back` reaches 5 against `g_sleh_seen` at 9, and
the meaning of that gap on a serviced-then-retried fault is a question about the instrument, not a
conclusion available from the numbers. It is named here because it is the one event on the user path
that a later instrument should read directly.

The ninth entry is the known death and it is unchanged: `pc 0x04b79074`, `lr 0x800462dc`,
`sp 0x8054fed0`, `cpsr 0x800000b3`, `user 0`, and `xnu_live_slot_rtcpre_pop = 0x04b79075` read
immediately before it - the idle exit's own `pop {fp, pc}`, which 522 is built to close.

## 5. The linker wall, stated correctly

523 §4 proposes *"a real static ARMv7 Mach-O program as `/sbin/launchd`, built by this tree's own
toolchain"* as the arm that follows. Two facts make that a different statement than it looks:

* **A real static ARMv7 Mach-O already exists and already runs.** It is `entry_ramdisk.s` - 79
  instructions, a hand-built `mach_header` with `flags 0`, and §3's table is its execution. The arm
  is not "write one"; it is "why does the run contain a user-mode prefetch abort at `0x11a4`".
* **The toolchain wall the notes carry is mis-stated, and the mis-statement is in the tree.**
  `tools/build_xnu_arm_macho.sh:203-218` refuses with *"no Mach-O linker on this host"* and *"neither
  is installed: `/usr/lib/llvm-14/bin` has llvm-nm, llvm-size, llvm-objdump and llvm-ar, but no
  ld64.lld"*. Both sentences are false here:

  ```
  $ ls -l /usr/bin/ld64.lld-14
  /usr/bin/ld64.lld-14 -> ../lib/llvm-14/bin/ld64.lld
  $ /usr/lib/llvm-14/bin/ld64.lld --version
  Ubuntu LLD 14.0.0
  ```

  and it links armv7 - for a trivial object: `clang --target=armv7-apple-darwin -c` on a one-instruction
  `_start` then `ld64.lld -arch armv7 -e __start -platform_version ios 0.0 0.0` produced exit 0 and a
  `Mach-O armv7 executable`, `MH_EXECUTE`, `ARM/V7`. The script's **own discovery loop** (verbatim,
  `:56`) resolves `MACHO_LD` to `/usr/lib/llvm-14/bin/ld64.lld`, so the refusal branch is dead code
  and the `exit 3` at `:217` is unreachable on this host.

  **The wall moves as soon as the object has a reference in it, and this is where the message is wrong
  rather than merely stale.** The same two lld binaries, given an object with one `bl` to an external
  and a literal-pool reference (`extern int g(void); int f(int a){ return g()+a+(int)(long)s; }`),
  refuse:

  ```
  ld64.lld: error: VANILLA relocation must be extern at offset -1191182320 of __TEXT,__text in r.o
  ld64.lld: error: VANILLA relocation has width 1 bytes, but must be 0 bytes at offset ... in r.o
  ```

  exit 74, on **both** 14.0.0 and 15.0.7. That reproduces experiment 150's finding -
  *"`ld64.lld` links arm64 Mach-O and CANNOT link armv7 Mach-O"* - whose text names the message as
  `unhandled relocation type`; the message differs, the conclusion does not, and 150 is right. So the
  honest statement is **two-part, and the tree's text has neither part**: no linker is *missing* (it is
  installed, and the script would find it), and armv7 linking still fails on any object with an external
  reference. My own first pass at this section said "it links armv7" from the one-instruction test, which
  is the same over-generalisation this document is about - a positive from the narrowest possible input,
  read as a property of the tool.

  The second limitation is independent and it is the one that decides the pid 1 question: **lld's Mach-O
  driver cannot emit a *static* executable.** `-static` prints
  `Option '-static' is not yet implemented. Stay tuned...` and the output carries `DYLDLINK TWOLEVEL PIE`
  with `LC_DYLD_INFO_ONLY`, `LC_DYSYMTAB` and `LC_LOAD_DYLINKER` regardless. A pid 1 must be static -
  `mach_loader.c:623-637` refuses a `MH_DYLDLINK`-less binary only when `DEVELOPMENT || DEBUG`, and the
  *dynamic* branch needs `CS_DYLD_PLATFORM`, which is not obtainable (523 §4). **So the reason
  `entry_ramdisk.s` builds its header by hand is real, and it is the *static* requirement plus armv7
  relocations - neither of which the tree's refusal text names.**

  A stale message is the `mi4-a-claim-in-a-comment-is-not-a-check` family, and this one has a cost
  beyond being wrong: it says the Mach-O path stops at a missing tool, which is the kind of claim a
  later step would plan around - and 523 §4 did, by proposing "a real static ARMv7 Mach-O" as the arm,
  when that program already exists and already runs (§3).

## 6. Open: where pid 1's stack came from

This one is left open on purpose, because the log does not decide it and the arithmetic is specific.

* The Mach-O writes `sp = 0` (`entry_ramdisk.s:1014`), so `thread_userstack`
  (`osfmk/arm/status.c:585-593`) takes its `else` branch and returns `USRSTACK`, and `bsd/arm/vmparam.h:10`
  says `USRSTACK = 0x27E00000`. The built image carries exactly one little-endian `0x27E00000`, which
  is consistent with that constant reaching `thread_userstack` and `thread_userstackdefault` through a
  shared literal pool.
* `create_unix_stack` (`bsd/kern/kern_exec.c:4904-4970`) then tries
  `mach_vm_allocate_kernel(map, &addr, size, VM_FLAGS_FIXED)` at `USRSTACK - size`, and **only if that
  fails** falls back to `VM_FLAGS_ANYWHERE` and sets `user_stack = addr + size`.
* The log says the fallback's answer is what happened: pid 1's user `sp` is `0x00101efc`
  (`xnu_live_ast_sp` and `xnu_live_sleh_sp` both), the kernel's `copyout` of the initial stack lands
  at `far 0x00101f28`, and the page above both is `0x102000`. **`0x102000 = 0x2000 + 0x100000`** - the
  first free region above `__TEXT` (which is `[0x1000, 0x2000)`), plus exactly `MAXSSIZ` (1 MB,
  `bsd/arm/vmparam.h:24`), which is the size that makes the top land on 0x102000.

  If that arithmetic is right, the *fixed* allocation at `0x27D00000` failed, and the constants alone
  do not explain it: `osfmk/mach/arm/vm_param.h:143-144` gives `VM_MIN_ADDRESS 0` and
  `VM_MAX_ADDRESS 0x80000000`, so `0x27D00000 + 0x100000` is inside the map's range by a wide margin.
  **Nothing in this log records `load_result->user_stack_alloc_size`, the map's own bounds, or the
  `kern_return_t` of that allocation**, and `grep` over the 596 KB log finds no `0x27D...` or `0x27E...`
  address at all - the region was never instrumented. So this is named as an open question with a
  named next reading rather than answered.

  The alternative reading is that `USRSTACK` is not the constant the tree's `bsd/arm/vmparam.h`
  declares for the configuration that was built. Both readings are one instrument away from decided,
  and neither changes §1-§5.

## 7. The measurement defect this reading caught

The claim in §1 - 「pid 1 executes its own Mach-O header」 - is one I carried forward rather than read.
It came from taking `__TEXT fileoff 0` plus `pc = 0x10e0` and inferring *inside the header*, when the
two numbers that decide it (`sizeofcmds` 0xC4 and the entry pc being an expression over the commands'
own end) are in the same file, four lines apart, with an assembler assertion between them.

Its shape is worth adding beside the ones already listed, because it is not an extraction error and
not a shape error - it is an error of **arithmetic performed on a layout instead of read off its
definition**. `0x10e0` looks like an offset into a header; it is a file offset plus a segment base,
and the file says so. The rule: **when a layout's boundary is an expression in the source, read the
expression; do not re-derive the boundary from the numbers it happens to produce.** The second thing
that would have caught it is cheaper still - the log records `getpid` returning 1, and an instruction
inside the load commands cannot make a syscall.

The same section had a second instance, and it is the same shape in the other direction. My first pass
wrote *"it links armv7"* on the strength of a one-instruction `_start` with no references - exit 0 on the
narrowest possible input, read as a property of the tool. An object with one `bl` in it fails. Both
measurements are in §5 now, and the two together are the rule: **a positive from the smallest possible
input is an upper bound on what the tool can do, not a statement about what it will do** - which is the
same mistake as 404's false zero, taken as a positive instead of a negative.

## 8. What this does not decide

* It does not decide `seq 7` - whether the prefetch abort at `0x11a4` was serviced or fatal.
* It does not decide §6 - where the stack came from, or whether the fixed allocation failed.
* It does not change 522. 522 is built, gated and frozen, its run is still owed, and its four
  readings are still `xnu_live_slot_cwe_win`/`_set`/`_calls` and `xnu_live_slot_post_calls`. The
  verdict is still the panic's **absence**, and the boot going *past* the idle loop.
* It does not reorder the phase. The console (523 §2, 524) is still the first item of the drivers
  milestone; this log changes what the *pid 1* item is, not whether it comes before or after.
* It does not decide whether the run's `poll` readings mean the timer path is correct. `ticks
  0x000d5976` is a fact about two `microuptime` samples around a 40 ms timeout; the fixture
  deliberately does not branch on it (`entry_ramdisk.s:148-149`), and neither does this reading.

## 9. Safety

Unchanged, and this log touches none of it: `fastboot boot` only, never flash, gate first, the log
captured before anything else touches the device. Nothing here is an image, a switch or a build - it
is a reading of a log already on this host and of files already in the tree, and it was done with the
device absent. `522` remains built, gated, frozen and unrun, and the device has been off the bus since
521's non-return.
