# Experiment 517 — the step was two changes, and its first run did not come back

**One line.** 516's doc named two things owed: measure the interrupt frame slot, and repair the exit the
way 516 repaired the enter. Both were built into one image, that image was booted non-persistently, and
**the device did not return** — no `MACH Reboot` as in 516, no `panic()`, no watchdog bite, no log, just
the USB disconnect that is the handoff and then nothing, for ~15 minutes. It needed a power-button press,
and the cold boot took the ram_console with it. So there is **no reading**: the run cannot say which of
the two changes did it. One of them (`FlushPoC_Dcache()` on the idle exit) is a *state* change rather
than a reading, so it is the one the split isolates - but **the addendum below corrects how big that
difference is**: reading Apple's own idle code shows the kernel already does cache maintenance by set/way
with `SCTLR.C = 0` on both sides of the window, and 517's call is that same operation with one extra loop.
The more novel half, by the same reading, is the frame reader's *writes* - the first live record installs
L1 descriptors, and this is the first wrapper here on an interrupt path. The step is therefore
split, the state change now defaults **off** (`STAGE90_XNU_EXIT_POC_FLUSH=0`, asserted against the image
in the build), and the measurement — the frame read from inside the handler that owns it — is the arm
that has not yet had its run. What this step did land is the finding: **the recovery net's record is not
"it always brings the phone back"** (it recovered 506–515 and 516 returned on XNU's own path; 517 is the
first run it did not), and `preflight_boot_check.sh` and `docs/reference/recovery-and-rollback.md` §4a
now say so.

## What the step was for

516 ended with two sentences that are the shape of this step:

> **517 is owed the frame-slot measurement** — `fleh_irq_handler` stores the frame pointer at
> `[cpu_data + 0xb0]` (`locore.s:1403`) and `return_from_irq` clears it (`:1433`), so a wrapper on
> `ml_get_timebase`, which the handler calls at `0x8001aae4`, can publish that frame's own
> `SS_R0..SS_VADDR` beside the timebase it is returning — **and the exit-side mirror repair**:
> `FlushPoC_Dcache()` in `__wrap_platform_cache_idle_exit` **before** `__real_platform_cache_idle_exit()`,
> so the L1-only `FlushPoU_Dcache` at `caches.c:460` is no longer the only flush on the way out.

The first answers 516's own open number: `pc = far` in both of its runs was **the CPU's own timebase**
where a code address belonged, so `return_from_irq` → `load_and_go_sys` (`ldr lr, [sp, #60]` /
`ldm sp, {r0-r12}` / `movs pc, lr`) returned to whatever the interrupt frame's `SS_PC` slot held. The one
C caller that runs between the handler's dispatch and that return is the handler's own `ml_get_timebase`
call, and the handler's `locore.s:1421-1423` is `add r0, ...` / `bl ml_get_timebase` / `str` — an entropy
stir that stores `timebase` at `EntropyData.buffer[index_ptr]`. So the hypothesis the frame reading
exists to settle is narrow and checkable: **if the frame's `SS_PC` slot already holds the timebase at
that call, the writer is the stir, and the index arithmetic is the bug; if it is still an address, the
stir is excluded and the writer is something else in the handler.**

The second is 515's measurement read backwards: `platform_cache_idle_exit`'s own first act is an
**L1-only** clean-and-invalidate, the L2 keeps whatever the enter's window left it, and `caches.c:490`
sets `SCTLR.C` again at the end of that same function — the moment those copies become live.

## What the step changed

Two blocks, in two files, and the split between them is now a build flag.

**The frame reader — `__wrap_ml_get_timebase` → `entry_note_timebase_call(ret_lo, sctlr)`.** The wrapper
calls the real function once and passes its 64-bit return through unchanged, then records: the frame
pointer read out of `cpu_data->cpu_int_state` (`+176`) reached through `TPIDRPRW` → `[+1484]`
(`ACT_CPUDATAP`); the six frame words at `SS_SP/SS_LR/SS_PC/SS_CPSR/SS_STATUS/SS_VADDR`
(52/56/60/64/68/72); the entropy stir's own store address (`EntropyData.index_ptr + 4`, wrapped to
`EntropyData.buffer` at `+68`); and `_hit`, which is 1 exactly when that address lands inside the frame
just read — the one number that decides whether the stir can be the writer at all. Seventeen keys,
`xnu_live_tb_calls/_off/_seq/_frame/_pc/_lr/_sp/_cpsr/_status/_vaddr/_index/_target/_hit/_datap/_ret_lo/
_prev_lo/_sctlr`, and `_datap` is published beside `_frame` so 516's `_pcx_datap` is a comparison rather
than an inference.

**The dereference is guarded by the kernel's own bound, not by hope.** This function runs *inside* the
interrupt whose frame it reads, so a fault in it would be a fault inside the instrument that is reading
a fault (269). The candidate is dereferenced only when `cand ∈ (cpu_data->intstack_top −
INTSTACK_SIZE, cpu_data->intstack_top)` — `+8` and 16384 from this configuration's `assym.s` — and a
candidate that fails is published as `_frame = 0` with the six words zeroed. `_calls` and `_off` are
counted either way, so a run whose guard refused everything says that too.

**The exit flush — `FlushPoC_Dcache()` in `__wrap_platform_cache_idle_exit`.** `CleanPoC_Dcache` (516's,
`0x8004575c`) writes back and leaves the line valid; `FlushPoC_Dcache` (`0x80045828`) is the same two
loops with `cr7,cr14,{2}` — clean **and** invalidate, over the L1's geometry and then the L2's. On the
exit, with `SCTLR.C` still clear, the clean half is a no-op on lines the enter already cleaned and the
**invalidate** half is the whole of the work: it discards the L2's pre-window copies before `caches.c:490`
makes them live. It takes no reading — `_pcx`'s record is still taken after the real exit, with the cache
on — so 516's three-cache-state shape is unchanged.

**And it is off by default, because of this step's own first run.**

    STAGE90_XNU_EXIT_POC_FLUSH=0   (default)  the measurement alone; this wrapper is 516's
    STAGE90_XNU_EXIT_POC_FLUSH=1             restores exactly the image that ran on 2026-09-21

The `xnu_entry_517` clause asserts the wrapper's `FlushPoC_Dcache` **count against this same variable**,
so a flag-off build cannot publish a say line claiming a write-back it does not contain, and a flag-on
build cannot silently omit one. A build with neither, or with both, fails.

**A defect the review found before it could cost a run.** `ml_get_timebase` is the first wrapper in this
project on a **per-interrupt** path, and `entry_live_write` spends one of `ENTRY_LIVE_CAP` (8192) records
for the *whole* run — 516's log already carries **4258** `xnu_live_*` records. Two writes per interrupt
for the ~2500 interrupts of a 25 s run would have spent most of that budget inside a diagnostic that needs
eight records of its own, pushing the tail — the `wfi`, `pcx` and `pce` keys a run *stops* in — behind
it. That is 445's and 461's defect ("the report path's buffer was the tracer's and was full when the
report was written") arriving from the other direction. The instrument now writes the per-call keys for
the first `STAGE90_TB_LIVE_MAX` (8) recorded calls and refreshes the two counters once per
`STAGE90_TB_LIVE_EVERY` (256) calls: bounded at `8 × 17 + calls/256` ≈ 150 records for a 25 s run,
healthy boot or storm. The cost is stated rather than hidden — `_calls`/`_off` are current to the last
multiple of 256 — and the console line says so.

## What the build checks

`build_entry.sh`'s `xnu_entry_517` clause is green (0 failures, exit 0) and asserts, from the linked
image rather than from the source:

- `FlushPoC_Dcache` is the kernel's **own** function — one definition, not in pass 1's undefined set —
  and its body holds **2** clean-and-invalidate loops (`cr7,cr14,{2}`), against `CleanPoU_Dcache`'s 1 and
  `CleanPoC_Dcache`'s 2 `DCCSW` (`cr7,cr10,{2}`); the loop count *is* the difference between the L1-only
  form and the Point-of-Coherency one.
- `platform_cache_idle_exit` still calls `FlushPoU_Dcache` exactly once (`caches.c:460` — this step adds
  to Apple's instruction, it does not rewrite it), and the exit wrapper's flush count equals
  `STAGE90_XNU_EXIT_POC_FLUSH`.
- `ml_get_timebase` is the kernel's own and **every reference to it in the linked pool is a call**
  (`R_ARM_CALL R_ARM_JUMP24`) — a value-taken reference would let `--wrap` rewrite an address that is
  later branched through, and the record would then be missing the calls it exists to make.
- The compiled `entry_note_timebase_call` loads `[.., #176]`, `[.., #8]`, `[.., #1484]`, uses 16384 and
  68, and loads **all six** of the frame's offsets.
- This configuration's `assym.s` says `CPU_INT_STATE=176`, `CPU_INTSTACK_TOP=8`, `INTSTACK_SIZE=16384`,
  `ACT_CPUDATAP=1484` **and** `SS_SP/SS_LR/SS_PC/SS_CPSR/SS_STATUS/SS_VADDR/SS_SIZE = 52/56/60/64/68/72/80`
  — the second group against `entry_saved_state.h`, which is the *other* place those offsets are written
  down, i.e. this project's oldest defect class (one value, two definitions) closed for the six numbers
  this step's reading is made of.
- The seventeen keys are literals in the linked image's `.text`, and `entry_note_timebase_call` holds
  17–19 `entry_live_write` call sites — the cadence above is a property of the compiled body, so a later
  edit that hoists a write out of its guard stops the build.
- **The flag reaches the file that reads it, and the clause is what found out that it did not.**
  `STAGE90_XNU_EXIT_POC_FLUSH` is added to `entry_trace.c`'s own compile line as well as to
  `STUB_DEFINES` (which only reaches `entry_stubs.c` and `entry_timebase.c`). The first flag-on build
  after the split compiled `#if STAGE90_XNU_EXIT_POC_FLUSH` to its **default**, i.e. it would have
  produced the flag-off image while reporting the flag as on — and the build stopped, because this clause
  reads the *wrapper's* call count out of the linked image and compares it against the variable. Same
  defect class as `#define X 0` not being "off" to `#ifdef X`; shorter fuse, because here the check and
  the flag live in the same clause.
- **The four offsets are read from the two files that state them, not written here a third time.**
  `CPU_INT_STATE`/`CPU_INTSTACK_TOP`/`INTSTACK_SIZE`/`ACT_CPUDATAP` come from `entry_stubs.c`'s macros and
  from `assym.s` (genassym's answer), and the clause's only job is to require them to be **equal**; the
  six frame offsets come from `entry_saved_state.h` and are also used, at those values, to build the
  matcher for the compiled body — so the header↔binary tie is checked without this file restating
  52/56/60/64/68/72 anywhere. (The header↔genassym agreement is separately gated, by
  `tools/check_saved_state_offsets.py`, which this build runs a hundred lines earlier.) An earlier
  revision of the clause did hard-code all of those numbers, and that is precisely the defect class the
  checker exists to refuse — a third definition of one value, with nothing comparing it.

**Two of the three build failures this clause produced were the check, not the image**, and both are
recorded in `memory/mi4-measurement-defects.md` (175, 176):

- The operand class was written `r[0-9]+`. objdump spells r12 `ip`, r14 `lr`, r13 `sp`, r15 `pc` — **per
  instruction**, not per function — so five of the ladders read 0 on a body that loads every field
  (`80007574: e59c30b0 ldr r3, [ip, #176]`). It failed with "does not load `[#176]` from cpu_data", a
  true-sounding sentence about a false premise. **516's own two clauses had the same narrow matcher and
  passed only because gcc happened to pick `rN` there** — so this is a latent defect 516 shipped with.
- One read was written `<<<"$(awk '...' <<<"$twbod"))"`, and bash appends the stray `)` to the last field:
  `tfrom` became `2152187904)`. Every test used it in a *string* comparison, so the build was green and
  the say line published the wrong number. Found by diffing the say template's literal segments against
  the log line the build produced. Every extracted value now has its **shape** asserted
  (`^[0-9]+$` / `^0x[0-9a-f]+$`), skipped only where the value is legitimately absent.
- The third was 516's `xbod` extent using `sym_next` where the body carries *local* labels
  (`clean_dcacheline`), so the range ended four bytes in; fixed with `next_global`, whose boundary must be
  *strictly* greater than the start because `clean_mmu_dcache` and `CleanPoC_Dcache` share `0x8004575c`.

## The run

**One run, and it is a negative reading. Non-persistent `fastboot boot`, nothing flashed.**

    18:45  preflight_boot_check.sh --allow-xnu-entry          all checks passed, exit 0
    18:46  run_and_capture.sh --allow-xnu-entry
           Sending 'boot.img' (8340 KB) OKAY [0.263s] / Booting OKAY [0.011s]

The USB record for the phone's port (`usb 3-10`, serial `4a2fe00b`), read from the host kernel log:

    18:46:06  USB disconnect, device number 41        <- adb reboot bootloader
    18:46:13  new device 50: idVendor=18d1 idProduct=d00d  Product: Android  <- fastboot mode
    18:46:16  USB disconnect, device number 50        <- fastboot boot handed off to the payload

and then **nothing**: no further enumeration on that port in ~15 minutes, `adb devices` and
`fastboot devices` both empty throughout, and no other Xiaomi/Google device on the bus. (The
`05c6:9008` QDL device that appears in `lsusb` is **not** the phone — it is an unrelated Halium unit on
port 3-3 that entered QDL mode minutes earlier. Section 4a's own warning about unrelated devices is
there because of exactly this.)

No log was captured: `/tmp/cancro-last_kmsg.txt` still holds 516's run 3 (596731 bytes, 18:04). The
power press is the long hold, i.e. a PMIC reset, so DRAM was reinitialised and whatever the payload
wrote into the ram_console is gone rather than merely unread — §4a's step 5.

**What the run does and does not say.** It says the image is not one this device recovers from by itself,
which is new. It does **not** say which change did it, and it does **not** say the exit flush is
dangerous: a compound step whose failure is unattributable is the defect, whatever the cause turns out to
be. The two halves have different risk shapes, and that is the whole basis of the split:

- the **frame reader** adds one call and six bounded loads inside an interrupt that already exists, and
  publishes numbers into the tracer's own channel — the risk shape of every other instrument in this
  image, on a hotter path than most but a *reading*;
- the **exit flush** changes the state of the CPU's caches on a path whose last act is to switch the
  D-cache back on — the risk shape of 516's enter-side repair, which 516's two runs returned from, but
  this step's doc cannot pretend that is proof, because 516's runs did not also carry a new wrapper on
  the interrupt path.

## Addendum (same day) — reading Apple's own idle code changed the risk story in both directions

The first version of this doc called the exit flush "the only thing in this image that can change what the
kernel *does*", and treated the frame reader as the tame half. Both halves of that are wrong, and the
correction comes from `osfmk/arm/caches.c` and from the disassembly rather than from a run.

**The exit flush is a smaller delta than this doc said.** Apple's own `platform_cache_idle_enter`
(`caches.c:404`) calls **`platform_cache_disable()` first** — which clears `SCTLR_DCACHE` — and only then
does its cache maintenance (`CleanPoU_Dcache` with one CPU, else `FlushPoU_Dcache`). So *cache maintenance
by set/way with `SCTLR.C = 0` is Apple's own idiom on both sides of the idle*, not an invention of this
step; the exit's `FlushPoU_Dcache` runs with the cache already off. And the two functions are the same
instruction — `Flush` is clean **and** invalidate in Apple's naming:

    FlushPoU_Dcache 0x80045874   DCCISW (cr7,cr14,{2}) over 128 sets x 4 ways   dsb sy  bx lr
    FlushPoC_Dcache 0x80045828   the same loop, then DCCISW over 4096 sets x 8 ways, dsb sy, bx lr

and the geometry decodes to exactly this part: the first loop covers 32 KB 4-way (this L1D) and the second
covers 2 MB 8-way (this L2), so 517's call differs from the kernel's own exit flush by **one extra loop of
an operation the kernel performs three instructions later** — 16384 `DCCISW` and a `dsb`. It is a
wholesale L2 clean-and-invalidate at a moment when the CPU has left the coherency domain, which is worth
saying; it is not a new *kind* of operation on this path, and the honest statement is "one more loop, at a
moment the kernel already does this", not "the one risky change".

**The frame reader is the more novel half, and the novelty is in its writes rather than its reads.** It is
the first wrapper in this project on a per-*interrupt* path, and that has a consequence this doc missed:
`entry_live_write`'s **first call is not a write — it is `entry_live_init`, which installs three L1 page
descriptors** for the console's section (`entry_live_map`). Every other live-writing site in the image runs
in thread context, and 516's log shows the channel coming up at `attempts = 1` from the timer-registration
wrapper (`xnu_live_tmr_setup_seq = 1`, `xnu_live_timebase_seq = 2`), in thread context immediately after the
jump. So the *ordering* is safe today — but nothing in the code said an interrupt-context call was not
allowed to be the first, and editing the live translation table from inside an exception, on the interrupt
stack, with the D-cache off, is the one thing on this path that cannot be retried.

So the instrument now **asks before it writes**: `entry_live_ready()` (one `noinline` predicate over
`g_live_state`) is called at the top of the write block, and an interrupt-context call (`_frame != 0`) holds
its records while the channel is not yet up instead of bringing it up from there. The held count is
published as `xnu_live_tb_held` and printed on the console line, because a refusal that is invisible is how
441's defect class starts. In a healthy run this changes nothing — the channel is up long before any
interrupt — and that is a reading rather than an assumption: it is why 516's log was worth reading again.

`entry_live_ready` is `__attribute__((noinline))` deliberately: gcc folds a two-instruction predicate into
its caller, which made the guard read as *absent* to the build clause — the third time in two steps that a
check failed on the shape of the code rather than on the property (`168`'s class). Keeping it a call costs
one branch per interrupt and makes "the interrupt path asks first" a thing in the image.

    xnu_entry_517: ... this instrument spends at most 8 per-call records and then one counter refresh per
    256 calls (18 write sites in the compiled body), and it never brings the live channel up from the
    interrupt path (1 call to entry_live_ready, whose body really reads g_live_state) ...

Both arms rebuilt green after this change, and the flag-off image is now
`83cd02d7e08f45a0396081c8efec0ed74da92f2105fd9a41eb176adc8b38df79` (`stage90-qcdt.img`, `.text`
5,298,976, `.bss` 363,248 bytes to the same end address as before `g_tb_held` was added - the new word fitted in the
section's existing tail slack, which is the observation and not an explanation); the flag-on arm is
`3faee37183974b6774c95513ad3444f9f1cbb04709a18f47ac6bf95cd7b44458`. The image that ran on 2026-09-21 and
did not come back is still `59618b02…`, and the reason it cannot be attributed is unchanged — it carried
both halves, and neither this addendum nor anything else here says which one stopped it.

## Addendum 2 (host-side, before the run) — the frame has two forms, and the guard was written for neither

The frame reader was reviewed against `locore.s` before another device run, and it would have read
nothing. **The vector puts the saved state in one of two places, and the interrupt stack is not one of
them:**

- **From user mode it is in the current thread's PCB** — `locore.s:1300-1307`: `mrc p15,0,sp,c13,c0,4`
  (TPIDRPRW), `add sp, sp, ACT_PCBDATA` (848 here), `stmia sp, {r0-r12, sp, lr}^`. So
  `cpu_int_state == TPIDRPRW + 848` exactly, and no read is needed to establish it.
- **From kernel mode it is on the interrupted SVC stack** — `:1344-1349`: `sub sp, sp, EXC_CTX_SIZE`
  then `add r0, sp, EXC_CTX_SIZE` / `str r0, [sp, SS_SP]`. The vector therefore writes the identity
  **`frame[SS_SP] == frame + 360`** into the frame itself, and which stack that is depends on the code
  the interrupt took: a thread's kernel stack, or the interrupt stack when the interrupted code is
  already on it (this kernel's boot and idle loops are — below).

The guard as built accepted a candidate only inside
`[cpu_data->intstack_top − INTSTACK_SIZE, cpu_data->intstack_top)`, i.e. 16 KB of one specific stack.
516's frame happens to be inside it — **and it is the identity, to the byte, that says so**: the frame
is at `0x80517e88`, its `SS_SP` is `0x80517ff0`, and `0x80517ff0 − 0x80517e88 = 0x168 = 360`. A frame on
a thread's kernel stack is outside that window, so every ordinary call of a healthy run would have
published `_frame = 0`, and the run would have spent a wedge — or a power press — to learn nothing. That
is the *second* time this instrument's own check and code had to be corrected before it flew (the first
was the write budget, §"What the step changed"), and both were found by reading it rather than by
running it.

**What changed.** The guard is now the two facts instead of a window: `cand == TPIDRPRW + ACT_PCBDATA`
(the user form, exact, no read) or `cand` word-aligned, inside one of the two windows this
configuration's kernel data is known to live in (`0x80000000`–`0x80800000`, the image's own data;
`0xc0000000`–`0xc1000000`, the heap the thread and registry pointers sit in), **and** carrying the
identity in its own `SS_SP` word. The windows are a fault-guard — 269's rule is why there is one at all
— and the identity is the test. Two keys were added: `xnu_live_tb_kind` (1 kernel stack, 2 user PCB) and
`xnu_live_tb_cand`, which is what `cpu_int_state` held whether the guard accepted it or refused it —
because `_frame = 0` had three different meanings and only one of them was "no interrupt in service".
The build's clause was rewritten with it: it now requires the compiled body to use `ACT_PCBDATA` and
`EXC_CTX_SIZE`, compares all four offsets against `assym.s`, and reads the two header-named offsets
(`ACT_PCBDATA`, `SS_SP`) from the header the C includes rather than restating them — the build's own
first run refused the duplicate macros, which is the check doing its job on the clause that had just
been written.

### And what the same reading turned up in 516's log

516's two runs did not come back through XNU's own path — both took `panics` at the same site — and this
step's subject was the frame those runs stopped on. The host-side work did not have to wait for a run to
read that frame: `sleh_abort at interrupt context (saved state:0x80517e88)` with the dump below it is
complete enough to decode, and the reading is that **the code in progress when it faulted was the idle
exit's own L1 flush, the very call this step's flag-on arm duplicates one line earlier.**

- `lr: 0x800462dc`. In this image `platform_cache_idle_exit` is `0x800462d4`, and its second instruction
  is `0x800462d8 bl 80045874 <FlushPoU_Dcache>` — so `+8` is the return address of exactly that call.
  The abort was taken inside `FlushPoU_Dcache`, called from `platform_cache_idle_exit` at `caches.c:460`.
- `pc: 0x0723c1f8` and `far: 0x0723c1f8`, `fsr: 0x5` — an `IFSR` translation fault on a **section**, i.e.
  the *instruction fetch* at that address faulted. `0x0723c1f8` is not code in any image this project
  builds; it is a **timebase**. The same log's `wfi` records, written immediately before the abort, read
  `0x071cd428` → `0x0723c215` over `0x6eded` ticks, so the value the machine tried to fetch is 29 ticks
  below the timebase it read straight after the `WFI` it had just completed.
- `cpsr: 0x80000093` — SVC mode, I and F set. `sp: 0x80517ff0`, and the frame is at `0x80517e88 = sp − 360`,
  so the kernel form of the frame is confirmed from both ends.
- **"at interrupt context" is a statement about `sp`, not about an interrupt.** `ml_at_interrupt_context()`
  (`machine_routines.c:671`) is literally `sp < cpu_data->intstack_top && sp > intstack_top − INTSTACK_SIZE`,
  and `intstack`/`intstack_top` are `0x80514000`/`0x80518000` in this image — so `0x80517ff0` is 16 bytes
  below the top of the interrupt stack and the label was earned by *where the stack pointer was*. The
  reason it was there: the only `ldr sp, <intstack_top>` in the whole image is at `0x800002d4`, which is
  `start.s:310`'s `LOAD_ADDR(sp, intstack_top) // Get interrupt stack top` — **the boot runs on the
  interrupt stack**, and so does the idle loop it becomes, which is why a *boot-time* abort is reported
  as an interrupt-context one.
- `xnu_live_sleh_frame_ok = 1` in the same log: 516's own instrument compares the frame's
  `SS_STATUS`/`SS_VADDR` against the live `cp15` reads, so the frame the panic reported is the abort's own
  and not a stale interrupt frame left in `cpu_int_state`.

**What this reading does not settle, and the arm about to run is what separates the two.** Which
instruction put a timebase where a PC belongs. The return path is the one place in the kernel where that
becomes a fetch: `load_and_go_sys` ends `0x8001a944 ldr lr, [sp, #60]` (the frame's `SS_PC`) →
`0x8001a948 ldm sp, {r0-r12}` → `0x8001a94c movs pc, lr`, and that `movs pc, lr` runs **in Abort mode**
(`0x8001a920 cpsid if,#23`, with `msr SPSR_fsxc` set at `:0x8001a92c` from the frame's `SS_CPSR`) — so a
frame whose `SS_PC` (frame + 60) held a timebase is one instruction away from exactly this fault, and
because the mode is Abort an abort taken there is a **double fault**, which is UNPREDICTABLE on ARMv7.
That is also a candidate shape for 517's own silent death, worth naming as a hypothesis and not as a
reading: a run that locks up instead of taking the second abort leaves no log, no `MACH Reboot` and no
USB, which is what 517's run did. The one number in this reading the return-path story does **not**
account for is the dump's `lr`: the sequence sets `lr` from the frame's `SS_LR` at `0x8001a914` and then
replaces it with `SS_PC` at `0x8001a944`, so a jump made by `movs pc, lr` would leave the timebase in `lr`
and the dump shows `0x800462dc` (the frame's `SS_LR`). So the mechanism is a fact about the code and
*which* instruction jumped is not established by the dump.

The other candidate the doc named first, the entropy stir, is now the weaker one: its store address is
bounded by its own wrap test to `[EntropyData+4, EntropyData+68)` = `0x80526504`–`0x80526544`, which is
59 KB from this frame, so it cannot be the writer unless the `index_ptr` it reads was itself already
corrupt — and that is a reading (`_index`) rather than an assumption. `_pc` against `_lr` in the first
record, with `_kind` and `_cand` saying which frame it read, is the arm's whole question.

Still owed, and unchanged by the above: the `r4 = r11 = pc + 2` anomaly in 516's dumps (both runs). The
return-path reading sharpens it rather than explaining it: `ldm sp, {r0-r12}` at `0x8001a948` restores
both registers out of the frame's own slots, so `r4` and `r11` were already equal in the frame the return
path was spending, and no instruction in that path writes either.

## Addendum 3 (host-side, no device) — the handler's stack is where the frame is, and 518 is the arm

The frame reader and 516's dumps are enough to name a mechanism without another run, and it is a
property of how this kernel is arranged rather than of any instrument.

**Where the frame goes, and where the handler's stack goes.** `fleh_irq_kernel` builds its 360-byte
saved state *below the interrupted `sp`* (`locore.s:1344`, `sub sp, sp, EXC_CTX_SIZE`) and then **throws
that `sp` away**: the stack the handler body runs on comes from `cpu_data->istackptr`
(`locore.s:1361-1362`, `ldr sp, [r9, ACT_CPUDATAP]` / `ldr sp, [sp, CPU_ISTACKPTR]`). `istackptr` is
written **twice in the whole kernel and never per interrupt** — `arm_init.c:227` and `cpu.c:294`, both
`= intstack_top`. So the handler's stack always starts at the **top** of the 16 KB interrupt stack,
while the interrupted code's frame sits just below the interrupted `sp`.

**And in this boot the interrupted code is on that same stack.** `start.s:310-311` is the image's only
`ldr sp, <intstack_top>` (`LOAD_ADDR(sp, intstack_top)` / `sub sp, sp, SS_SIZE`), and 514's reading of
the idle path's own `sp` (`0x80517ff0` = `intstack_top − 16`) says the idle loop it becomes never left
it. So on 516's runs:

| what | where |
|---|---|
| the vector's frame | `[0x80517e88, 0x80517ff0)` — 360 bytes below the interrupted `sp` |
| the handler's stack top | `0x80518000` = `intstack_top`, i.e. **16 bytes above the frame's top word** |
| the six recorded words | frame+52..72 = `0x80517ebc`..`0x80517ed0` |

The handler's stack grows down *into* the frame from above. It reaches `SS_VADDR` (frame+72) after
**304** bytes of use, `SS_PC` (frame+60) after **316**, `SS_LR` after 320 and `SS_SP` after 324 — and the
handler does real work at that depth (`interrupt_stats`, the driver's handler through `blx r5`, then
`ml_get_timebase`). **Which is exactly the shape 516's three runs measured**: `SS_LR` intact and
plausible (`0x800462dc`, the return address of `platform_cache_idle_exit`'s `bl FlushPoU_Dcache`) while
`SS_PC`/`SS_STATUS`/`SS_VADDR` held **a timebase** and a `5` — and the fields are reached in exactly that
order, highest address first, so "the top three are the handler's spills and the next three are still
the vector's" is what a 316-to-319-byte handler produces. The values fit too: the handler calls
`ml_get_timebase`, and the pair `r4 = r11 = SS_PC + 2` that **both** of 516's dumps carry is two spilled
words a tick apart, not a coincidence of two runs.

**This also reads 517's silent death the only way the evidence allows, as a hypothesis and not a
reading.** The instrument 517 added to that path — the `ml_get_timebase` wrapper and
`entry_note_timebase_call` — *adds stack use to the very handler whose depth is the problem*. If the
collision is what the three runs were dying of, then a deeper handler overwrites more of the frame:
316 bytes took `SS_PC`, and past 324 it takes `SS_SP`, after which `load_and_go_sys`'s
`ldr sp, [ip, #52]` restores a wild stack and the device dies with no log, no `MACH Reboot` and no USB —
which is what 517's run did. That is a hypothesis; it is also a reason not to run the same image again.

### 518: the arm, and why it is the safe one

`cpu_data->istackptr` names where the handler's stack *starts*. Pointing it at the **middle** of the
16 KB interrupt stack leaves the top half to the boot and idle code whose frames are built there and
gives the handler 8 KB below them. It is one store, it is idempotent, it is inside a dedicated stack
region, and it cannot change any other behaviour of the kernel: nothing else in `osfmk/arm` reads the
field, and `ml_at_interrupt_context()` (`machine_routines.c:671`) tests `sp` against
`[intstack_top − INTSTACK_SIZE, intstack_top)`, which the middle still satisfies.

It is done *before* another measurement rather than after one because it is the **safer** arm: it
removes the interaction it is testing for, where the instrument that would measure the collision adds
to it. And both outcomes are decisive:

- the frame comes back clean and the boot gets further than 516's did → the collision was the writer,
  and the OS's death in the idle path is explained;
- the frame still holds a timebase in `SS_PC` with the separation on → the writer is something else,
  and 517's records say which of the two forms of frame it read.

Built as `STAGE90_XNU_ISTACK_SEPARATE`, **default 1**; `=0` is 517b's arrangement (the same
measurement, no state change), and the build asserts the store's call site's *presence or absence*
against the same variable.

**Where the store lands is part of the arm, and the first version of it would have been undone by the
kernel.** `arm_init` calls `PE_init_platform` three times, and the first is the `(FALSE, args)` call at
`arm_init.c:159` — *before* `arm_init.c:226` writes `BootCpuData.istackptr = intstack_top`, so a store
made from that wrapper is overwritten by the kernel's own assignment. The call that matters is the
`(TRUE, &BootCpuData)` one at `:383`, and its wrapper runs before `__real_PE_init_platform` brings the
interrupt controller up; it re-stores because the condition re-tests the *field* rather than a remembered
value, which is why the field is re-read on every call and why the build asserts that it is.
(`__real_PE_init_platform` compiles to a **tail branch** here — `b <PE_init_platform>`, not a `bl` — so
the clause matches both; its first version matched only the `bl` and called a correct body wrong.)

**And the run says whether it was early enough rather than assuming it.** Both stores' `CPSR` values are
recorded and printed: store 1 is the one `arm_init` overwrites, store 2 is the one that precedes the
platform bring-up, and the I bit at store 2 is the difference between "the handler's stack was moved
before any interrupt could be taken on the boot's own stack" and "it was moved somewhere in the boot and
the window between `:226` and `:383` was open". Flag-on `stage90-qcdt.img`
`32a513bc322f6db7465550039069fc93ed5ca4c9622fed95bfee0673b64ca519` (`.text` 5,299,904, `.bss` 363,324,
entry image 5,519,996 — the frame reader and the exit flush are unchanged:
`STAGE90_XNU_EXIT_POC_FLUSH` is still 0); the flag-off arm is
`37c94910…` from the previous build, whose arrangement `=0` restores. Two of the clause's failures were
the check rather than the code: it first looked for `#8192` in the compiled body and the compiler had
folded `intstack + 8192` into one pool word (`0x80516000`) — so the test is now the *computed* address
from this image's own `intstack` and this configuration's `INTSTACK_SIZE` — and the store is its own
symbol (`entry_istack_store`) precisely so that "is the change in the image" is a fact the build can look
up rather than match by shape.

### What the arm does not break, checked rather than assumed

`istackptr` is written exactly twice in this kernel (`arm_init.c:227`, `cpu.c:294`, both
`= intstack_top`) and **read by eight sites** in `osfmk/arm/locore.s` and `cswitch.s` — the IRQ, FIQ,
data-abort and prefetch-abort kernel paths, and the two context-switch paths — every one of which reads
it as *"where to put this stack"*. That is precisely the field 518 changes, so the change is the field's
meaning and not a side effect on it.

The two places that compare against the *window* rather than the field still hold: `ml_at_interrupt_context()`
(`machine_routines.c:671`) and KDP's backtracer (`kdp/ml/arm/kdp_machdep.c:498` and `:669`, the two
`prevfp >= intstack_top - INTSTACK_SIZE && prevfp < intstack_top` tests) both test membership in
`[intstack_top − 16384, intstack_top)`, and the middle of that range is in it. `ml_stack_remaining()`
(`:992`) measures from `intstack_top − INTSTACK_SIZE` whatever `sp` is, so a handler running in the lower
half is measured the same way.

And the overflow boundary is better, not worse: below `intstack`'s 16 KB sits `excepstack`'s 4 KB
(`data.s:48-52`), whose pointer `excepstackptr` (`arm_init.c:232`) **nothing in this tree reads** — so a
handler that overran its 8 KB would land in unused storage, where before the arm it landed in the frame
and said nothing.

### Why the net may not have fired, as a hypothesis

The watchdog's arming is proved at arm time and 506–515 all came back on its bite, so "517's run needed a
power press" wants an explanation rather than a shrug. The collision supplies a candidate one: the same
overwrite that put a timebase in `SS_PC` takes `SS_SP` a few tens of bytes deeper, and
`load_and_go_sys` restores `sp` from `[ip, #52]` — so the CPU returns with a wild stack, and from there
into a state this project cannot describe: a second abort taken in Abort mode, or a `cpsid`-masked loop
with the vectors no longer where the exception needs them. **A secure-mode watchdog interrupt cannot be
delivered to a core whose exception state is UNPREDICTABLE, and `hw_watchdog.c`'s own header already says
the bite is TrustZone-mediated** rather than independent of all software. That also fits the two runs
that *did* return: 516's handler was shallow enough to leave `SS_LR` and `SS_SP` intact (it panicked and
`MACH Reboot`ed), and a deeper one in 517 would not be. This is a hypothesis with a mechanism, not a
reading, and the arm is the test: 518 removes the collision, so if the net's silence was the collision's
consequence, the next run either returns or comes back on the bite.

### Where the boot actually was when it died, read from the console rather than from the frontier

516's log ends with the OS's own console, in order (`/tmp/516-run3-kmsg.txt`, the same in run 2):

```
load_init_program: attempting to load /sbin/launchd
mini4: the OS starts the process at 0x10e0 (the thread's user pc was 0x10e0)
mini4: the OS's own init load returned, so pid 1 has the init image (caller 0x80049eb8)
mini4: the AST is done -- pid 1's thread is at 0x10e0 for user mode (sp 0x101efc)
panic(cpu 0 caller 0x804542dc): sleh_abort at interrupt context (saved state:0x80517e88)
```

So the OS did not die *on the way in*. `bsd_init` ran, `IOCatalogue`'s matching records are in the same
log (`xnu_live_cls_calls = 0x3e`, the registry walk's `xnu_live_walk_*`), the memory device was added and
the RAM disk mounted (`BSD root: md0`), `load_init_program` reached the end of Apple's own list, and
**pid 1's thread was set up to run in user mode at `0x10e0`** (the project's own 4 KB fixture in the RAM
disk, not a real `launchd` — which is why `load_init_program` printed its failure for
`launchd.development` and then succeeded with the fixture). The abort came *after* all of that, in the
interrupt taken while the boot thread — which by then **is** the idle thread — sat at
`intstack_top − 16`.

Two things follow, and they are the reason the arm is worth its risk rather than a shot in the dark:

1. **What dies is the idle path, not the OS's bring-up.** The kernel's own driver and registry work has
   already run by the time of the panic; the machine stops seconds after entering user mode, in the loop
   that is supposed to keep running while pid 1 does its work.
2. **So the collision is a candidate for exactly the death that is left**, and removing it is a candidate
   for the difference between "dies right after entering user mode" and "keeps running" — which is where
   the goal's driver clause lives.

**The prediction, written before the run, so the reading is not a story told afterwards.** With the arm in
(`32a513bc…`, `STAGE90_XNU_ISTACK_SEPARATE=1`):

- **if the collision is the writer**: the next log has **no** `sleh_abort at interrupt context`, the idle
  and `wfi` records continue past the point where 516's stopped, and the console goes on with the OS's own
  activity instead of a panic — and the device comes back on its own with a log;
- **if it is not**: the same panic returns, and the record says which case it is without ambiguity —
  `xnu_live_tb_kind` is 1 (a kernel-stack frame) with `_pc` clean, `_cand` equal to the frame, and
  `SS_LR`/`SS_SP` intact, i.e. the frame was **not** the handler's spill and the writer is something else
  that then has a name and a moment.

## Addendum 4 (the run, 2026-09-22) — the arm flew, and the run it flew cannot answer the question

`STAGE90_XNU_ISTACK_SEPARATE=1` (`stage90-qcdt.img` sha256 `32a513bc322f6db7465550039069fc93ed5ca4c9622fed95bfee0673b64ca519`)
ran on device `4a2fe00b` through the gate: green, `Sending 'boot.img' (8340 KB) OKAY` / `Booting OKAY`, the
device returned on its own inside the capture window, and the log came back at 599,995 bytes. This doc's
prediction (§3) named two branches. **The second one obtained — the same panic, byte-for-byte in kind** —
and the run's own records then say the first branch was never testable, because **the arm moved the
interrupted stack as well as the handler's**: `xnu_live_irq_istackptr = 0x80516000` and every stack address
in the panic exactly `0x2000` lower than 516's, with `SS_SP == istackptr - 16` in both runs.

So this addendum exists to correct the prediction rather than to report it confirmed. The prediction assumed
that moving `istackptr` gives the handler a *different* stack from the one the interrupted code is on. It
does not: the field is read by the vector to place the handler's stack and by `cswitch.s` to place the idle
thread's, so one store moves both and the 16-byte gap between the handler's first push and the frame's top
word is invariant. The hypothesis was not tested, and it is not refuted.

Everything else the run produced — the frame shown to be authentic, the `pop {fp, pc}` in
`platform_cache_idle_exit` and the two counter readings in the handler's 5th and 6th stack words that it
popped, the two readings that were unpublishable by construction, and the arm that the next step has to be
instead — is written up as **experiment 518**
(`experiment-518-the-arm-moved-both-stacks-so-the-collision-was-invariant.md`), which is also where 516's
reading of `SS_PC`/`SS_STATUS`/`SS_VADDR` as handler spills is corrected: they are the vector's own words,
and the collision lands one word higher, on the idle code's saved return address.

What this changes in the list below: **item 1 is spent** (the measurement arm's run happened, and its
instrument recorded nothing — the gate), and **item 4 is answered** (`r4 = r11 = pc + 2` in both of 516's
dumps is the half of the popped `{fp, pc}` pair that did not become the PC, i.e. the first of two adjacent
curves on the counter — not a frame slot).

## What is owed

1. ~~**The measurement arm's run**~~ — **spent, 2026-09-22** (`67075d64…` was superseded by 518's
   `32a513bc…`, which carried the same frame reader). The run happened and the instrument published
   nothing: `entry_note_timebase_call` records only while `SCTLR.C` is clear and prints its totals only from
   the console epilogue, and a fatal run reaches neither. Publishing from a site that always runs is owed
   in its place, and the frame is now read from the abort path instead (`xnu_live_sleh_*`, which is what
   showed it authentic).
2. **The exit flush's own run** (`=1`) once the measurement arm has returned, so a failure has one
   candidate.
3. **The watchdog's non-recovery, understood rather than noted.** The bone is worth stating precisely:
   the watchdog's *arming* is proved at arm time (`en_after`, `counter_running`, `countdown_plausible`),
   and a *bite* is proved by nothing but a device that came back. Every run from 506 to 515 came back on
   the 25 s bark / 28 s bite — including 512/513 with the OS parked in the kernel's own `WFI` idle path
   and 514/515 in abort storms — and 516 twice on the kernel's own `MACH Reboot`. So the net has fired on
   this device many times and this run is the counterexample. `hw_watchdog.c`'s own header already says
   the honest thing and the docs did not: the bite is a **secure-mode interrupt mediated by TrustZone**,
   so "does not depend on the *payload's* state" was always the claim, never "does not depend on any
   software". A run that can leave the device needing a power press is a cost this project should know
   before it spends it.
4. ~~Still owed from 516: the `r4 = r11 = pc + 2` anomaly.~~ **Answered by 518's run**: the two
   registers are the halves of the `{fp, pc}` pair that `platform_cache_idle_exit`'s closing
   `pop {fp, pc}` loaded — one of them became the PC and the other stayed in `fp`/`r11`, which is why they
   differ by the tick or two between two adjacent counter readings, and why the pair tracks the timebase
   rather than a fixed frame slot. See experiment 518 §3.

## The image

`.text` 5,297,664 → **5,298,816** (+1152: the frame reader's body and the seventeen key literals), entry
image **5,519,996** bytes — unchanged and *not* an error, for 516's reason: the copied span is
`[0x80000000, __bss_start)` and `__bss_start` is pinned at `0x80543a80` in both images, so growth in
`.text` eats the fill below `.data` rather than moving the end. `.bss` `0x80543a80` … `0x8059c570` =
**363,248** bytes (516: 363,184). The seventeen new words are 4 bytes each — measured, `nm -S` — so 68
bytes of new `.bss` and a 64-byte section delta, a four-byte difference this doc does not explain by hand
and does not need to: the checkable claims are that the seventeen symbols exist at size 4 and that the
section grew 64 bytes. Entry point `0x80000074`, tree at `0x806e0000` + `0x744c`, boot args at
`0x8059e000`, `topOfKernelData` `0x80700000`, headroom 1,456,784 bytes. **Wrap census 76** (516: 75; the
new one is `--wrap=ml_get_timebase`). 129 fixture mutations refused, `xnu_entry_failures = 0`.

The arm that has not run, as rebuilt with the interrupt-path guard: `stage90-qcdt.img` sha256
`83cd02d7e08f45a0396081c8efec0ed74da92f2105fd9a41eb176adc8b38df79`, `stage90.bin` `2e0b0fb1…`,
`stage90.elf` `734960f1…`, `xnu_arm_entry.bin` `b4e3291a…`, `stage90_fixture.macho` `52bc9c35…`; the
flag-on counterpart is `3faee371…`. Two earlier hashes of this same arm appear above and are worth keeping
straight: `67075d64…` was it before `entry_live_ready` was added, and it is **byte-identical to the build
before the clause refactor** — which is how a check-only change is proved to be one, since an edit that
touched the artifact would not be a check.

The arm that **did** run, and did not come back: `stage90-qcdt.img` sha256
`59618b026ff6978a384ce2d0d2ab8c0a96e30753eae3458df0eadb83c55e3664`, `stage90.bin` `3b0425aa…`,
`xnu_arm_entry.bin` `e4359cae…`. It was built before the split, so its `FlushPoC_Dcache` call was
unconditional — which is what "the image that ran carried both changes" means concretely. The symbols this
step reads: `FlushPoC_Dcache` `0x80045828`, `CleanPoC_Dcache` `0x8004575c`, `CleanPoU_Dcache` `0x800457a8`,
`FlushPoU_Dcache` `0x80045874`, `platform_cache_idle_exit` `0x800462d4`, `ml_get_timebase` `0x80017474`,
`entry_note_timebase_call` `0x80007390`, `__wrap_ml_get_timebase` `0x8047c7f8`, `EntropyData`
`0x805264fc`.

## Safety

One run, non-persistent `fastboot boot` through `stages/stage90/preflight_boot_check.sh
--allow-xnu-entry` and `stages/stage90/run_and_capture.sh --allow-xnu-entry`, **nothing flashed, and
nothing in this project is ever written to storage** — so the brick half of the standing constraint
("一定要保证不要让设备彻底死机或者变砖") is satisfied by construction and no run can change that. The
hard-hang half was **not** satisfied by this run: the device needed a power-button press, the nets did
not recover it, and the log is lost with the cold boot. That is the finding this doc has to carry, and it
is why the gate's text and §4a of the recovery reference have both been corrected rather than left
claiming a net that holds always. A four-byte difference in `.bss` and a run with no reading are the
price of the step; the device is intact.

    python3 tools/check_experiment_index.py    # ok: 494 row(s) across 91 stage column(s)
