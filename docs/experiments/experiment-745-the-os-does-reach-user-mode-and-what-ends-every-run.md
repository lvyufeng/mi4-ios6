# 745: the OS **does** reach user mode — what ends every run is the arm's own deadline, not the OS

**Read from an archived capture. No device action of any kind** — no `fastboot`, no `adb` to the device, no
press, no build. Everything below is read out of
`out/stage90/captures/rung19-rca-20260926-180922-last_kmsg.txt` (742's press, 630,486 B, sha
`990983a9…`), which is the most recent capture in the tree, and out of the sources it names.

**The one-line finding.** The standing record — the phase-status memory, the README rows, the closing
sentence of every experiment doc — carries *"no storage, no filesystem, the OS not observed reaching
userland"*. **The first two clauses are true and the third is false as written.** The OS reaches user mode
in this capture, a user process runs a complete syscall cycle, and a **driver answers a read from it** —
and the thing that ends the run **6.04 seconds in** is the arm's own clock, not the OS: `entry_post_clock`
calls `entry_seam_end_run`, its `RESTART_REASON` store faults, and the `kernel abort type 4` that reads
like a crash is the instrument doing what it was built to do.

## 0. Why this was worth reading at all

The goal's first clause is 「起码要能进入操作系统，把基础驱动跑起来」 and its second is
「让os可以正常启动并且挂载存储」. A ladder that has spent twenty rungs on an SDHCI controller is spending
them on the second clause, and the record has never separated the two. This reads the first clause out of
a capture that was already in the tree, and it answers a question nobody had asked the log: **what is it
that stops this boot?**

## 1. What the capture says, in the log's own words

The kernel gets to BSD, mounts a root, and runs process 1:

    BSD root: md0, major 2, minor 0
    load_init_program: attempting to load /usr/local/sbin/launchd.development
    load_init_program: failed loading /usr/local/sbin/launchd.development: errno 2
    load_init_program: attempting to load /sbin/launchd
    mini4: the OS starts the process at 0x10e0 (the thread's user pc was 0x10e0)
    mini4: the OS's own init load returned, so pid 1 has the init image (caller 0x8004ceb8)
    mini4: the AST is done -- pid 1's thread is at 0x10e0 for user mode (sp 0x101efc)

and the runner's own **goal criterion** — *"user mode reached, and a driver answering"* — reads PASSed in
this log, from keys the image publishes around each call:

| the fixture's call | the cell | the reading |
| --- | --- | --- |
| `open("/dev/rmd0")` | `_open_seq=0x1`, `_open_error=0x0`, `_open_fd=0x0` | **the driver answered** |
| `open(<a name devfs has no node for>)` | `_open_seq=0x2`, `_open_error=0x2` | the control answered `ENOENT` |
| `read` | `_read_nbytes=0x4`, `_read_ret_lo=0x4` | the read returned 4 bytes |
| the read's buffer | `_read_word_before=0x00102000` → **`_read_word_after=0xfeedface`** | **a driver's `uiomove64` moved `MH_MAGIC` into a user page** |
| `getpid` | `_getpid_value=0x1` | pid 1 |
| `exit` (the child) | `_exit_pid=0x2`, `_exit_rval=0x3` | a **second** process exited |
| `wait` | `_wait_pid=0x2`, `_wait_status=0x300` | pid 1 **reaped** it, status = exit code 3 |
| the AST record | `_ast_calls=0x2`, `_ast_done_seq=0x2`, `_ast_sp=0x00101efc` | the kernel took a user thread's AST and put it back |

**A `fork`/`exec`/`exit`/`wait` cycle completed in user mode, and a device's read crossed into a user
page.** And the boot went on living: *"the kernel's own idle path was entered 60037 time(s)"*, the idle's
exits are counted at two sites (`0x80010aa4` ×60,036 and `0x80010bfc` ×1), and *"the idle's cache window —
`platform_cache_idle_enter` entered 1 time(s) … and returned 1"*.

**So the two readings the goal's first clause asks for are both in the log**: user mode is reached, and a
driver answers a call made from it.

## 2. What ends the run, and it is the arm's own clock

The capture's last four lines before the console goes quiet are a panic:

    panic(cpu 0 caller 0x80457668): kernel abort type 4: fault_type=0x3, fault_addr=0xfa0065c
    r2: 0x0fa00000   sp: 0x80557ec8
    cpsr: 0x80000093 fsr: 0x00000805 far: 0x0fa0065c
    Attempting system restart...MACH Reboot

and every address in it resolves, in this arm's own entry ELF, to the run's deliberate ending:

| address | symbol | what it is |
| --- | --- | --- |
| `pc 0x8047e488` | **`entry_seam_end_run`** | the function whose whole job is to end the run |
| `lr 0x8047e5bc` | **`entry_post_clock`** | the deadline that decides which idle pass ends it |
| `caller 0x80457668` | `sleh_abort` | the abort handler the fault reached |
| `sp 0x80557ec8` | `_seam_sp` | the seam's own stack — this is the entry image's frame |

**The faulting store is `entry_seam_end_run`'s `RESTART_REASON` write at `0x0fa0065c`** — the address
`[[mi4-the-run-ends-at-entry-epilogue]]` already records as one of the two stores that *take a translation
fault in the context they run in*. **The disassembly and the panic dump agree instruction for instruction**,
which is why this is a measurement and not a reading of a symbol table:

    8047e5b8:  bl  8047e47c <entry_seam_end_run>     <- entry_post_clock's call; lr becomes 0x8047e5bc
    8047e47c:  movw r3, #21761  ; 0x5501            ) r3 <- 0x78665501 = STAGE90_ENTRY_RESET_REASON_NORMAL
    8047e480:  mov  r2, #262144000 ; 0xfa00000       ) r2 <- 0x0fa00000 = MSM_IMEM_BASE_PHYS
    8047e484:  movt r3, #30822  ; 0x7866            )
    8047e488:  str  r3, [r2, #1628] ; 0x65c         <- FAR 0x0fa0065c, FSR 0x00000805

| the panic's own register | the value | the disassembly |
| --- | --- | --- |
| `pc: 0x8047e488` | the `str` | `str r3, [r2, #0x65c]` |
| `lr: 0x8047e5bc` | the instruction after the `bl` | `8047e5b8: bl <entry_seam_end_run>` |
| `r2: 0x0fa00000` | the base | `mov r2, #262144000` |
| `r3: 0x78665501` | the value | `movw #0x5501` / `movt #0x7866` |
| `far: 0x0fa0065c` | the address | `r2 + 0x65c` = `STAGE90_ENTRY_RESET_REASON_ADDR` (`entry_reset.h:33`) |
| `fsr: 0x00000805` | bit 11 set = a **write**; bits[3:0] = 5 = **translation fault, section** | the `0x0fa` megabyte is unmapped here |

`r3`'s value is `STAGE90_ENTRY_RESET_REASON_NORMAL` (`entry_reset.h:35`), so the store that faults is the
one that writes **"the boot got to its normal end"** — into a megabyte that is not mapped, from the
context the entry image's reset tail runs in. That is the finding the epilogue memory already carries; what
this document adds is that **in this capture the caller is the arm's own clock, which is why the run ends
when it does.**

**And the deadline it fired on is published in the same log, and the arithmetic closes exactly.**

| cell | value | |
| --- | --- | --- |
| `_seam_post_end_ticks` | `0x06ddd000` = 115,200,000 | the deadline |
| `_post_cntfrq` | `0x0124f800` = 19,200,000 | the hardware's own rate, read once |
| the deadline in seconds | **6.0000 s** | 115,200,000 / 19,200,000 |
| `_post_t0` | 190,155,762 | the baseline at the wrapper's first return |
| `_post_elapsed` ×4 | 0 → 1,947,393 → 38,717,341 → **116,090,109** | 0.0000 s → 0.1014 s → 2.0165 s → **6.0464 s** |
| the last one crosses the deadline | **6.0464 ≥ 6.0000** | **the ending fires on that pass** |
| `_post_end_calls` | `0x00000006` | **published — the ending fired** |

**And the two *other* ending switches are off in this arm**, which is how the deadline is known to be the
one responsible rather than inferred: `_seam_end_run = 0` and `_seam_post_end_run = 0`, against
`_seam_post_end_ticks = 0x06ddd000`.

**So the sequence is: boot → user mode → the fixture's syscalls → ~60,000 idle passes → the arm's own
6-second deadline → `entry_seam_end_run` writes `RESTART_REASON` → that store faults → `sleh_abort` →
panic → `MACH Reboot`.** The `kernel abort type 4` is the instrument, and reading it as an OS failure is
reading the arm's own ending as a crash.

## 3. Which driver answers, and it is not the one the ladder is building

The fixture opens **`/dev/rmd0`** — `entry_trace.c:1726` names what serves it: *"`open("/dev/rmd0")` ends in
`bsd/dev/memdev.c` — in a `bdevsw`/`cdevsw` entry a driver installed before process 1 existed"*. And the
root is `md0`: *"Added memory device md0/rmd0 … BSD root: md0, major 2, minor 0"*.

**So the driver that answers is the memory-disk driver, and the root filesystem is a RAM disk.** This is a
real driver answering a real call from a real user process — the goal's second clause in its weakest true
form — and it is **not** the SDHCI controller the twenty-rung ladder is aimed at. That is exactly why the
record's *"no storage, no filesystem"* stands: there is no mounted real storage and no filesystem on it.

**And `/sbin/launchd` is not what runs.** The log shows the OS *attempting* it, failing (`errno 2`), and
then running the project's own fixture at `0x10e0` instead — `stage90_fixture.macho`, 1,744 B, generated by
`tools/mkmacho_fixture.py`, whose own header says it *"contains no Apple binary code and is never
executed"* (which this boot falsifies in the interesting direction: it **is** executed, as pid 1). So pid 1
runs a test program, and the honest statement of the first clause is **"the kernel reaches user mode and
runs a user process"**, not "the OS boots".

## 4. Why the record said otherwise, and what part of it is right

**The record is not wrong by carelessness; it is using a different definition, and the difference has never
been written down where the goal is scored.** The runner's own goal block states both halves of it:

> **And what it prints is a FLOOR, not progress.** All of it is in 520 and 533 — the whole userland phase
> happens before the death — so a run has to *lose* these readings to be a regression, and having them is
> not evidence that the boot got further than any boot has. The sentence that has to survive into the
> report is the one naming the ceiling: what nobody has yet observed is the machine *staying* up.

and, at the foot of the same block:

> ---- and this criterion does not decide whether the machine stayed up. Every reading above is a floor
> that 520 and 533 also meet; **the ceiling is the arm's own clause, and the run that matters is the one
> whose log has both.**

**So the project's definition of the goal is "user mode **and** the machine stays up", and it is the second
half that is unmet — not the first.** Where the closing sentence of a document says *"the OS not observed
reaching userland"*, it is stating the second half's absence in the first half's vocabulary, and an
operator reading it will look for a failure in the wrong place. **The precise sentence is: user mode is
reached and a driver answers, in every capture since 520; what has never been observed is the machine
staying up, and §2 says why.**

## 5. The tension, and it is not a rung

**Every run is ended by one of two instruments, and neither of them is the OS.**

1. **the entry-side deadline** — `STAGE90_XNU_POST_END_TICKS`, which this arm carries as `115200000`
   (6.0000 s) and which §2 measures firing;
2. **the payload's own hardware watchdog** — armed before the jump and, per the record's own 606 clause,
   the net that caps a run of this arm at **28 s** (25 s bark + 3 s bite gap) *"on a successful run too"*.

**And the second one exists because the operator's standing constraint requires it**: 「一定要保证不要让设备
彻底死机或者变砖」 is implemented, in this tree, by the payload arming a watchdog it cannot later disarm. So
observing *"the OS stays up"* needs a run in which both instruments are off — and the instrument that would
have to come off is **the exact net that makes the no-brick property true.** That is a **design question
for the operator and not a rung to climb**: it is the one place in this project where the goal and the
safety constraint are in direct tension, and it has never been stated in one sentence.

**It is also measurable without a press, and this is the shape of the answer.** The two instruments are
separately switchable — `STAGE90_XNU_POST_END_TICKS`, `STAGE90_XNU_SEAM_END_RUN`,
`STAGE90_XNU_POST_END_RUN` on the entry side, and the watchdog's own arm/disarm on the payload side — so a
future arm could carry **the entry deadline off and the hardware watchdog on**, which would buy the longest
window the constraint allows: the OS unbounded by the project and the machine still guaranteed to come
back. What that arm would read is not *"the OS stays up"* — the watchdog still ends it, at 28 s — but
**how far past the deadline the boot gets, and whether anything after it is a mechanism of the OS's rather
than of the arm's.** No such arm exists, and this document does not arm one.

## 6. What the goal actually needs, in two clauses

| the goal's clause | where it stands | what it is waiting on |
| --- | --- | --- |
| 「进入操作系统」 | **reached** — user mode, a user process, a full `fork`/`exec`/`exit`/`wait`, a driver answering a read into a user page | nothing on this clause, except that pid 1 is the fixture and not `/sbin/launchd` |
| 「把基础驱动跑起来」 | **partial** — `memdev.c` answers `/dev/rmd0`; the SDHCI storage driver does not exist | the ladder, which is at rung 20 |
| 「正常启动并且挂载存储」 | **not reached** | the ladder for *mounting*, and §5's design question for *正常启动* |

**So the twenty-rung ladder is spending itself on the right clause and the record's headline hides it.**
The ladder's own next answer (rung 20, armed, not pressed) is the response-demand question; the storage
that 「挂载存储」 needs is still several commands away at best (a completed CMD3, then CMD9 `SEND_CSD`,
CMD7 `SELECT_CARD`, and a data phase at CMD17 whose `EFI PART` reading 531 §9 registered).

## 7. What this document does not say

- **It does not say the goal is met, or nearer than the record claims.** The record's *"no storage, no
  filesystem"* is true, *"TWRP-to-storage stays withheld"* is right, and the ladder's next step is unmoved.
  What is corrected is one clause of one sentence, and the direction it points is not "closer" but "look at
  the right thing".
- **It does not say the ending is a defect.** `entry_seam_end_run` faulting on its own store is
  documented (`[[mi4-the-run-ends-at-entry-epilogue]]`, measured by 684), and the ending is a **designed**
  instrument: the arm ends the run on purpose so that the readings in §1 can be collected without the
  machine being left running.
- **It does not say the boot is a success in the goal's sense.** pid 1 is the project's own fixture; the
  root is a RAM disk; `/sbin/launchd` failed with `errno 2`. A user process ran, and the OS is not
  *usable*.
- **It does not attribute the return.** The device came back ~24 s after the send in this press; §2
  explains the run's *end* and not the machine's *return*, which
  `[[mi4-the-run-ends-at-entry-epilogue]]` records as unattributed.
- **It arms nothing.** No press is owed, no firer is armed, `out/` is untouched, and rung 20 remains
  **ARMED AND NOT PRESSED**.

**The goal is still NOT met.** This document changes what the next action should be aimed at; it does not
take it.
