# Experiment 504 — the first value a driver hands back, and the load that had to become a copy

**One line.** Process 1 opens `/dev/rmd0` - the character node `mdevadd` made before process 1 existed -
and then **reads four bytes out of it**: the call returns 4, and the word the driver copied into the
page is **`0xfeedface`** while the word that was there before it is **`0x00102000`**, the address
`mmap` had returned. So this is the first reading in this walk whose value came out of a *device* rather
than out of the kernel's own tables, and the two words either side of it are a before/after this image
can predict from its own other records. Getting there took two runs, because the first one showed the
instrument where *it* was wrong: the wrapper read the fixture's buffer with a plain kernel load
(`out[0]`), the kernel took a data abort at exactly that instruction - `far = 0x00102000`, `DFSR = 0x5`,
`recover = 0`, `pc = __wrap_read+0xb8` - and the entry counter ran to its cap of **64 at one repeated
`pc`**, so run A's log holds **no `xnu_live_read_*` key at all**. The fault is two instructions
before `bl __real_read`: the `read` never reached the driver, the control `open` never ran, and the
spin was never reached, all of which the log says by omission. Run B reads the buffer with
`copyin_word` (`osfmk/arm/machine_routines_asm.s:719`) - the kernel's own single-instruction user copy,
the same call `kern_event.c:2095` and `sys_ulock.c:455` use - which arms `TH_RECOVER` and returns
`EFAULT` instead of faulting.

    ./tools/host_ramdisk_macho_check.py --selftest out/stage90/xnu_arm_entry.elf        # 93 refused
    ./tools/check_timer_sources.py --image out/stage90/xnu_arm_entry.elf --selftest   # 48 refused
    ./tools/check_sysent_table.py --elf out/stage90/xnu_arm_entry.elf --selftest      # 18 refused, 3 and 5
    ./tools/check_timer_line.py --selftest --verbose                                 # 44 citations, 20 refused
    ./tools/check_os_entry.py --image out/stage90/xnu_arm_entry.elf --selftest        # 30 refused
    ./tools/check_irq_routing.py --image out/stage90/xnu_arm_entry.elf --selftest     # 57 refused
    ./tools/check_driver_catalogue.py --selftest                                      # 18 claims / 131 refused
    ./tools/check_experiment_index.py                                                 # 481 rows

## What the step was for

503's document ended with the ceiling in plain words: the fixture "can ask for a page, block on a
deadline and fault, and the *kernel* answers all three. What it cannot do is be an init - **it has no
way to open a file, and there is nothing for it to open**." 504 is the step that removes both halves of
that sentence. The program gains an `open` and a `read`; and the thing it opens is the node the boot's
own memory device made four lines into the OS console:

    Added memory device md0/rmd0 (02000000/0D000000) at 000000008050D000 for 0000000000002000

`mdevadd` is `bsd/dev/memdev.c` and it makes **two** nodes per device - `devfs_make_node(..., DEVFS_BLOCK,
UID_ROOT, GID_OPERATOR, 0600, "md%d", devid)` at `:607` and the same with `DEVFS_CHAR` and `"rmd%d"` at
`:616` - and the two nodes are not two spellings of one thing. The block node's `read` is
`spec_read`, whose last resort is `mdevstrategy`: buffer-cache pages, and a `panic("mdevstrategy:
source address %016llX not mapped")` at `:322` if the address it computed is not the one it meant. The
**character** node's `read` is `mdevrw` (`:204`), whose whole body from the driver's side is
`status = uiomove64(mdata, uio_resid(uio), uio)` at `:232`. One copy, to a user address, with a length
the caller chose - which is a step's worth of kernel and no more.

Three properties make it the *safe* one, and all three are readings rather than hopes:

  - **`mdevopen` cannot refuse this call.** `:191` returns `ENXIO` for a minor past `NB_MAX_MDEVICES`,
    `EACCES` for a write open of an `mdRO` device, and `0` otherwise - and the fixture opens
    `O_RDONLY`, which is 0 in `bsd/sys/fcntl.h`, so `flags & FWRITE` is 0 and the answer is 0.
  - **The copy goes to the process's own virtual address, not to a physical one.** `mdevrw` rewrites
    `uio->uio_segflg` to one of the `UIO_PHYS_*` forms only when `mdFlags & mdPhys` is set, and the
    payload calls `mdevadd` with `phys = 0` - the run's own `xnu_live_mdevadd_phys` is 0 - so the
    `uiomove64` is an ordinary user copy and `0x00102000` is a virtual address the process owns. A
    physical interpretation of a user VA is the panic at `:322`/`:296`, and it is not reachable here.
  - **The address is one the process already faulted in and wrote.** 480's `str r0, [r0]` put the
    mapping's own address into the page's first word, and the two faults that made that work are
    entries 5 and 6 of the same run's abort record - unchanged in `pc`, `far` and `fsr` from 502's and
    503's runs.

## The program is a path, a control, and a read that keeps its page

`entry_ramdisk.s` goes from thirty-seven instructions to **fifty-two**: the two asks, then two `open`s
and a `read`, then 479's loop again. The page address moves into `r9` on the way, because `read`'s
return is a `user_ssize_t` and `arm_prepare_u32_syscall_return` (`bsd/dev/arm/systemcalls.c:289-298`)
therefore writes `save_r1` as well as `save_r0` - so `r0` is not the only register a call destroys and
the page has to live somewhere the kernel does not write back.

The two paths are `adr`-loaded rather than written as literals, and that is a change of kind:

    adr r0, path_rmd0       /* "/dev/rmd0"  - 0x001011b0 in run B */      the device
    mov r1, #OPEN_RDONLY    /* flags = 0, so mdevopen cannot answer EACCES */
    mov r2, #0              /* mode, which open reads only with O_CREAT */
    mov r12, #SYS_OPEN      /* 5 */
    svc #0x80
    mov r1, r9              /* cbuf = the page, whose first word is the address */
    mov r2, #READ_BYTES     /* nbyte = 4, so the copy is one word */
    mov r12, #SYS_READ      /* 3 */
    svc #0x80
    adr r0, path_missing    /* "/dev/nosuch" - the control, 0x001011bc in run B */
    ... the same four instructions with open's number ...

**`adr` is PC-relative and the assembler resolves it here and once**, so each path address is a value
this image *computes* and never a value it *states*: the host check decodes the two `adr`s out of the
instruction stream and requires the run's two `path` words to be their targets, which is what makes
`xnu_live_open_path = 0x000011b0` a reading rather than a coincidence. The alternative - writing the
addresses as `.word` literals - is this project's oldest defect in its plainest form: one value with two
definitions, and nothing comparing them.

**And the second `open` is the control, without which the first proves nothing.** A kernel whose `namei`
answers everything, or a wrapper that published its own return, would report a small fd for `/dev/rmd0`
too. The control is the *same* syscall with the *same* three arguments and a name no driver can have
made a node for, so the two records differ in exactly one word - the path - and the two `error` words
have to differ, one of them `0` and the other `ENOENT` = 2. That 2 is not chosen here: XNU's own loader
printed it for its own missing path three lines earlier in the same console block
(`load_init_program: failed loading ...: errno 2`).

**Neither call branches on its answer**, for 503's reason: this program *is* `/sbin/launchd`, and
`udf #1` here kills `initproc` (478's run measured what follows). A `read` from a device that answered
an errno is a fact about the driver, not a reason to end the boot.

## The readings

**Run A** - the build as first written - reached the first `open` and then stopped making records:

    getpid  seq 1   value 1  error 0  caller 0x80285848
    mmap    seq 1   value 0x00102000  arg5 0x00005a5a  caller 0x80285848
    poll    seq 1   timeout   5 ms  before 0x071097f4  after 0x07131d3c  ticks 0x00028548 = 165256
    poll    seq 2   timeout  40 ms  before 0x07131e12  after 0x0720e085  ticks 0x000dc273 = 901747
    open    seq 1   path 0x000011b0  flags 0  mode 0  error 0  fd 0     <- /dev/rmd0, and it answered
    (no xnu_live_read_* key, no second open, and no xnu_live_getpid_count: the spin never ran)

and with it, this:

    sleh seq 7  fsr 0x00000005  far 0x00102000  pc 0x8047aea0  cpsr 0x20000013  recover 0x00000000
    sleh seq 8  fsr 0x00000005  far 0x00102000  pc 0x8047aea0  cpsr 0x20000013  recover 0x00000000
    seen 0x40 (the cap)  storm 9  back 6  at_back 8  armed 2  redirected (absent, i.e. 0)

`0x8047aea0` is `__wrap_read+0xb8`, and the same image's `objdump` makes it `ldr fp, [r8]` - the
wrapper's own `word_before = out[0]`, two instructions before `bl 802066c0 <read>`. `0x00102000` is the
buffer the fixture asked for. `FSR = 0b00101` is a *section translation* fault, i.e. the L1 descriptor
for that address is invalid - the address has no translation at all in the tables in force, which is not
the same event as a permission fault on a page that is there.

**Run B** - the same step with the wrapper's read replaced by the kernel's own - makes every record the
step was written for:

    open  seq 1   path 0x000011b0  flags 0  mode 0  error 0          fd 0     /dev/rmd0
    read  seq 1   fd 0  buf 0x00102000  nbytes 4  error 0  ret_lo 4  ret_hi 0
                  word_before 0x00102000   word_after 0xfeedface
                  copy_before 0         copy_after 0
    open  seq 2   path 0x000011bc  flags 0  mode 0  error 2          fd 0     /dev/nosuch, ENOENT

and the rest of the program: `xnu_live_getpid_count` runs to `0x800000`, so the spin after the driver
was read from is alive, and the two asks reproduce 503's shape a run later (165256 / 901747 in run A
and **152554 / 891390** in run B, ratios 5.49 and 5.84 against an 8:1 ask).

Four of those numbers were predictions written into the code before the build, and all four hold:

  - **`word_before = 0x00102000` is `mmap`'s own return.** The page's first word is the mapping's
    address because 480's store put it there, and the run's `xnu_live_mmap_value`,
    `xnu_live_read_buf` and `xnu_live_read_word_before` are the same word in three records.
  - **`word_after = 0xfeedface` is the fixture's own `MH_MAGIC`.** The RAM disk *is* this Mach-O:
    `mdBase << 12` is `0x8050D000`, the console line says so, `uio_offset` is 0, and the file's first
    four bytes are `ce fa ed fe`. So the value says a driver moved data, and the data says which driver
    over which memory - neither of which a wrapper publishing only the call's return could produce.
  - **`ret_lo = 4`.** `read`'s return is 64-bit, and `ret_hi = 0` beside it is what says the low word is
    the whole of it (the same pair 503's `poll` does not need and this call does).
  - **The control's `error = 2`**, devfs's answer for a name its tree does not have, and the same number
    the OS's own loader printed for its own missing path.

## The instruction that had to become the kernel's

Run A's fault is not a mystery about the device; it is a fact about this kernel, and the image states it
in its own `copyout`:

    80015d74: add  r3, pc, #336              ; COPYIO_SET_RECOVER: arm the recovery (source: adr)
    80015d80: str  r3, [ip, #664]            ; thread->recover = copyio_error
    80015d8c: ldr  r3, [ip, #1468]           ; COPYIO_MAP_USER: the thread's user TTB
    80015d90: mcr  p15, 0, r3, c2, c0, 0     ; TTBR0 = the user's tables
    80015d98: ldr  r3, [ip, #1476]           ; ... and its ASID
    80015d9c: mcr  p15, 0, r3, c13, c0, 1    ; CONTEXTIDR = the user's
    80015dc0: stmia r1!, {r3, r5, r6, ip}    ; the copy itself, now that the user is mapped
    80015df4: ldr  r3, [ip, #1472]           ; COPYIO_UNMAP_USER: the kernel's TTB back
    80015df8: mcr  p15, 0, r3, c2, c0, 0     ; TTBR0 = the kernel's tables again
    80015e08: str  r4, [ip, #664]            ; COPYIO_RESTORE_RECOVER

`COPYIO_MAP_USER`/`COPYIO_UNMAP_USER` are `osfmk/arm/machine_routines_asm.s:548-561` and `:601-610`,
they are compiled only `#if __ARM_USER_PROTECT__`, and the four `mcr`s are *in this build* - so the
statement "while the kernel runs, `TTBR0` holds the kernel's tables" is a reading of the linked image
rather than of a `#define`. A wrapper that loads a user address without that dance is not copying user
memory; it is dereferencing an address that happens to belong to a process. The abort handler then has
nothing to work with: `sleh_abort` reaches its recovery arm only when `thread->recover` is non-zero
(`osfmk/arm/trap.c:456-461`), and a fault with nothing armed is *retried* - which is exactly what the
record shows, `pc` and `far` repeating to the 64-record cap with `recover = 0` on every one.

The fix is not to teach the instrument that dance. It is to call the function that already does it:

    int copyin_word(const user_addr_t user_addr, uint64_t *kernel_addr, vm_size_t nbytes)
    /* "Move an aligned 32 or 64-bit word from user space to kernel space using a single read
     *  instruction ... think `*kernel_addr = *(uint32_t *)user_addr`"  kern/misc_protos.h:101 */

`osfmk/arm/machine_routines_asm.s:719-752` is the whole of it: the header's range check, the recovery
arm, `COPYIO_MAP_USER`, **one** `ldr` (or `ldrd` for a 64-bit read, 0-extended), the store to the kernel
buffer, and the unmap. Its own two rules are visible in the record rather than assumed - `nbytes` must be
4 or 8 and the user address must be aligned to it (`:725-730`, `L_copyin_invalid` at `:746` returning
`EINVAL`) - and both hold for a page-aligned word. The widths are this target's: `user_addr_t` and
`user_size_t` are `u_int32_t` with `__arm64__` undefined (`bsd/arm/types.h:82-83`), so all three
arguments are 4-byte words.

So the wrapper now publishes **`copy_before`/`copy_after` beside the two words** - `0` means the read is
a read, non-zero is the `EFAULT`/`EINVAL` the kernel returned instead. That is the key run A could not
print, and its absence is why run A's three missing records are missing. And `copyin_word` is *not* a
`--wrap`: `build_entry.sh` gained a `xnu_entry_504` check that the name is not in pass 1's undefined
set, because a name the stub generator had to stand in for would make the two words facts about the
stand-in - 471's rule, for 504's reason.

## The claims, and what the step's own code had to be told

  - `tools/host_ramdisk_macho_check.py`: the program is **52 words** against 37, and the check's decode
    grew two shapes to read it - `mov rd, rm` (masked with `Rn` *included*, because `mov` is `add` with
    `Rn = 0` and a mask that leaves `Rn` free reports a correct instruction as a wrong one) and `adr`,
    which the assembler resolves into an `add`/`sub` of `pc` and which the check now reads the *target*
    out of, so the two path addresses are cross-checked between the instruction stream and the strings.
    Six mutations are new (the read's length above the mapping's, the read's length below a word, the
    control opening the device rather than the missing name, and three byte-valued mutations of the two
    path strings), and the loop now accepts `bytes` values for the same reason. **93 mutations refused**,
    against 73 in 503.
  - `tools/check_sysent_table.py`: `WRAPPED` and `WITNESSES` gain 3 and 5, `MUNGERS` gains
    `3: "munge_www"` and `5: "munge_www"` - the same munger as `poll`, because all three slots take
    three 4-byte arguments - and four mutations are new: the real `read` and the real `open` in their
    slots (either would leave a fixture that reads a driver and publishes *nothing*, which is run A's
    symptom arrived at from the other side), the two new slots swapped, and `poll`'s wrapper in `open`'s
    slot. **10 witnesses, 18 mutations refused**, against 14.
  - `tools/check_os_entry.py`: its fault sites are at `entry+0x38` and `entry+0x44`, in 480's block,
    which did not move; the wording around them went from thirty-seven to fifty-two words. **30 refused.**
  - `build_entry.sh`: `TRACE_LDFLAGS` gains `--wrap=open --wrap=read` and now counts **62** `--wrap`ped
    symbols with 53 reached by a branch, 1 same-object-only, 1 never called (`sleep`) and **7 by address
    only** (`vcputc getpid mmap poll open read thread_quantum_expire`) - both new names are in that list
    for `poll`'s reason: their only reference in the whole image is the word in `sysent[3].sy_call` /
    `sysent[5].sy_call`, which is why `check_sysent_table.py` reads the address out of the image rather
    than trusting the flag.
  - `entry_stubs.c` and `entry_trace.c` are the instrument: two note functions (`entry_note_open`,
    `entry_note_read`) and two wrappers, live-channel only, with the four-record band and the
    powers-of-two overflow counter the two 503 keys use.

One defect of this step's own instrument is worth naming as a defect and not as a finding: the
`.balign 4` that was going to pad the two path strings is a frag whose size the assembler does not know
until the section is finished, and an `.if` over a difference that spans one is a **non-constant
expression** - so the two assertions tying the strings to `__TEXT`'s file range could not be written at
all. `.zero 2` takes "/dev/rmd0" to the next word, and two explicit `% 4` assertions say so.

## The image

  - `.text` **5280960 -> 5285824** for the step as first written (+4864: 52 words of program, two
    wrappers, two note functions, the `by_address` list entry) and **5285824 -> 5285984** for the fix
    (+160: the two `copyin_word` calls, two spill words, and two more keys in the note).
  - `.bss` **362824 -> 362888** (+64) and `bss_end` `0x805983c8 -> 0x80598408`; the four `uint32_t`
    counters are the reason, and the payload zeroes the same range.
  - **the entry image's file is 5503612 bytes in all four builds** - 502's, 503's and both of 504's -
    and this is 503's band lesson read a second time: `image_bytes` is `__bss_start` = `0x8053fa7c`,
    which is `.init_array`'s end, and `.data` begins at the 16 KB-aligned `0x8050c000`, so `.text` can
    grow by kilobytes without moving a single byte of the image the payload copies. `xnu_entry_checksum`
    moves instead, `0x90702371` -> `0x907024b1`.
  - `kernel_size` 5999040, `stage90.img` 6002688 (`5edce8c1...` in run B), `stage90-qcdt.img` 8523776
    (`20f3db41...`), the RAM disk still `0x8050d000 + 0x2000`, `sizeofcmds` still `0xC4`.

## The rest of the boot is unmoved

  - **The OS console block is byte-identical across all four runs** - 502, 503, and both of 504's: 25
    text lines, 1289 bytes, sha256 prefix `424e1b747e8bafb2`, and the same 129794-byte pad line after
    them. The frontier line is still `load_init_program: attempting to load /sbin/launchd`, and the only
    `failed loading` line in the log is the loader's own for `/usr/local/sbin/launchd.development`.
  - **Run B's abort record is 502's and 503's, entry for entry.** Entries 1..4 are the boot's own demand
    faults, 5 and 6 are the fixture's own page (a read fault then a permission fault, `pc = 0x00001118`
    and `0x00001124` - the same two words in all three runs), and 7 and 8 are
    `Lcopyin_wordwise_loop + 0x0` at `far = 0` with `recover = 0x80015eec = copyio_error`: **the kernel's
    own copy path faulting on a null user address and being redirected to `EFAULT`**. `armed = 0x1c`,
    `redirected = 0x1a`, `seen = 0x20`, `storm 9`, `back 6`, `at_back 8` - 28 armed and 26 redirected,
    the same four numbers 502 and 503a print. **The storm is not a failure and never was**: the 26
    entries past the first six are `copyin` armed and spent, which is the mechanism working.
  - run A is the exception that proves the reading: its `seen` ran to the cap of 64 and its `armed` was
    2, because the thread stopped taking those 26 faults at all - it was looping inside the instrument
    instead. A run whose instrument faults loses the boot's own tail as well as its own records.
  - the timers are unmoved: `_irq_timer_count` `0x800`, `_irq_late_count` 0, and 502's three deliveries
    reproduce on `_isr_intid` 0x28.
  - both logs end `No errors detected`, both exits are 0, and the device came back on its own inside the
    capture window in both runs - 567625 bytes for run A and 570316 for run B.

## What is owed

  - **The step's own read, one level further in.** The word the driver wrote is now a reading; what is
    *not* is which bytes `mdevrw` moved them out of - `mdBase << 12` and `uio_offset` are still derived
    here from the console line and the C. The instrument is a `--wrap` on `mdevadd` that publishes
    `base`/`size`/`phys` (it exists and the run's `xnu_live_mdevadd_*` already carries them) *beside* the
    read's record, so the source address is a record and not an inference - and `xnu_live_mdevadd_base`
    reads `0x0008050d`, which is a **page number**, so the shift is a claim the reader has to know.
  - **The `open` reached `mdevopen`, and the run says so only by its return.** `error 0` with a device
    node name is the strongest statement the log can make, because `devfs_open` propagates the driver's
    answer - but it is an argument, not a record. A `--wrap` on `MDEV_*`'s open hook, or the same
    record from the driver's side, would turn "a driver answered" into a number the driver wrote.
  - **A second read at an offset**, so `uio_offset` stops being 0 by construction: `read(fd, page, 4)`
    twice, with the fixture advancing the buffer by four, would make the second word the Mach-O's
    `cputype` (`0x0000000c`) and prove the offset is carried rather than ignored.
  - **Still owed from 503, unchanged**: which row of `timer_compute_leeway`'s table the `poll` thread
    took and what `timer_user_idle_level` was (a `--wrap` on `timer_call_slop` publishing its own
    answer); the ~1.8 ms `microuptime`-vs-`mach_absolute_time` difference measured directly in
    `__wrap_poll`; a third ask (5/40/400 ms); and from 502: the frame's `0x038`/`0x03C` pair, intid 39,
    the `AckC`/EOI question, the registry's 4-slot capacity stop, claim 16's one-level derivation
    reader, the citation rule over the other cited files, the unstamped `out/xnu_asm_obj`, 497's
    conditional-clause mutation, the unregister guard's asymmetry, `/timer`'s second definition, the
    other device nodes, `MSM8974RootResource`'s `state0 = 0`, a name/class reader wider than eight
    characters, the release as a reading, `vm_fault`, 488's flag-list, 490's frames band,
    `xnu_live_dec_same`.
  - **And the one that matters for the goal.** User mode can now ask the kernel for time, for a page,
    for a vnode by name, and for bytes out of a device, and the kernel answers all four. What it still
    cannot do is *be* init: the fixture reads one word and spins, and `load_init_program` prints
    `attempting to load /sbin/launchd` and nothing else, so the OS is one `exec` away from a program
    that will not return to this loop. The next step that makes the OS *proceed* has to give the image
    an `exit` - or a second fixture that runs after the spin and ends in `thread_terminate` instead of
    `b spin`, so that `launchd_crashed_panic` is not the only way off the end.

## Safety

Every device touch went through `stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; the boot is a non-persistent `fastboot boot` and
nothing was flashed - the payload lives in RAM and the device was handed back to Android twice. Two runs
were spent on this step: the build as first written, whose wrapper faulted inside its own probe and left
the fixture's thread in a 64-entry retry loop, and the build with the two buffer reads made with
`copyin_word`. **Both returned `No errors detected` with the device back on its own inside the capture
window** - the first one is worth saying out loud, because a fault loop inside the instrument is exactly
the shape that could have held the machine, and what kept it a run rather than a hang is that the loop
was in one thread's kernel context with the timer and the watchdog still live above it.
