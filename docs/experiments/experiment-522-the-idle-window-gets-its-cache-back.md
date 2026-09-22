# 522: the idle window gets its cache back

Stage90. Built host-side with `STAGE90_XNU_IDLE_STACK=1` (default), `STAGE90_XNU_ISTACK_SEPARATE=0`,
**`STAGE90_XNU_EXIT_POC_FLUSH=0`** - i.e. 521's exit-side flush is *out* of this image - and the one
state change 521's non-return left standing: **the D-cache is turned back on at the idle window's near
end**. Gate green. **The run has not happened**: the device has not been on the bus since 521's
non-return and needs a power press, so this step's hardware half is still owed. Everything below that is
not the run is a fact of the image and of its clause.

The image is `out/stage90/stage90-qcdt.img`, sha256
`13d771938336fe65ccaf3dee833c14c0a9280d5365eda3f5c1e74cc6e6948825`, 8,540,160 bytes - the same size as
521's, 520's and 519's. The copied entry image is `xnu_arm_entry.bin`, 5,519,996 bytes, sha256
`576ecb326706e1e104e056ef16f420b3fd3e97d2396fc8e4732ea022ea01b312`, the same size as the last three
because the copied span ends at a pinned `__bss_start`. `.text` is **5,306,504** (+96 against 521's
5,306,408: `entry_idle_cache_enable` at 24 bytes, `entry_window_note` at 144, and the 4-byte `bl` to the
exit-side flush that is now compiled out), `.data` 206,804, `.bss` **379,696** - byte-for-byte 519's,
520's and 521's again, because the seven key tables are initialised and live in `.data`.

## 1. What this arm is, and how it differs from what 521's section 5 predicted

521's section 5 named the next arm as **the enter side cleaning *and* invalidating** - `FlushPoC_Dcache`
where `__wrap_platform_cache_idle_enter` calls `CleanPoC_Dcache` today - with the exit-side flush back at
0. The second half of that is what this image does. The first half is not, and the reason is worth
writing down, because it is the same class of mistake the whole walk keeps finding:

* `CleanPoC_Dcache` (`0x8004575c`), which that call site uses today, is `DCCSW` - it **cleans** and leaves
  the line **valid**. `FlushPoC_Dcache` (`0x80045828`) is `DCCISW` over L1 **and** L2 - the right
  operation for a stale valid line. So "replace one with the other at that site" was, on its face, a
  one-token change that runs entirely outside the window. It was still not what this arm does, because
  **it repairs the copy and leaves the window's own arithmetic alone**: the enter wrapper's
  `strd r4, [sp, #-12]!` (the deadline) runs *before* the window, with the cache on, so a clean leaves a
  valid line in both levels that the window's cache-off `push` cannot update - and that is 520's
  mechanism exactly, reproduced by the repair as well as by the bug unless the invalidate lands between
  the `strd` and the window's first store. Fixing the *timing* of a maintenance operation inside a
  window whose semantics are "the cache is off" is a repair that has to be argued about; removing the
  need for it is not.
* **What this arm does instead is make the window's exit path run with the cache on**, so that the
  question "which copy does the `pop` read" stops having two answers. `entry_idle_cache_enable()`
  (`0x80007c30`, 24 bytes) reads `SCTLR`, sets bit 0 (`C`), writes it back, then `dsb sy` and `isb` -
  the exact inverse of the three instructions Apple's own `platform_cache_idle_enter` executes at
  `0x8004623c..0x80046244`, which the clause still finds in the image (`bic #4` at `0x80046240`), so
  this is a change of state and not a no-op.

The call is placed at the **near end of the window**, in `__wrap_platform_cache_idle_enter`
immediately after `__real_platform_cache_idle_enter()` returns and before the WFI: the clause pins it at
`0x8047c92c`, after the call that opens the window at `0x8047c924`. From there to the `pop` every access
is a normal cached access:

1. the WFI runs with the cache on;
2. the exit's `push {fp, lr}` **write-allocates** the line and stores into it - a dirty, valid L1 line,
   not a DRAM-only write behind a stale line;
3. Apple's own L1 flush inside the exit (`FlushPoU_Dcache`, `0x80045874`) is `DCCISW`, so it **writes
   that dirty line back to the Point of Unification - which on this CPU is the L2** - and then
   invalidates the L1 copy; the L2 therefore ends up holding the *pushed* value and not the pre-window
   one;
4. `caches.c:490` sets `SCTLR.C` again (a no-op now, which is why this arm does not disturb Apple's own
   sequence), and the `pop {fp, pc}` misses L1 and hits an L2 that has the right word in it.

That is the whole prediction, and it is falsifiable in one reading: the panic is absent, or it is not.

### 1.1 The exit's own disassembly, which turns the prediction into four addresses

Read off this image rather than assumed, because the whole arm turns on *where* `SCTLR.C` comes back
relative to the exit's `push` and its `pop` - and the two differ by three instructions that separate
520's death from this arm's prediction:

```
800462d4 <platform_cache_idle_exit>:
800462d4:	push	{fp, lr}                     <- 520: C is OFF here, so this store reached DRAM only
800462d8:	bl	80045874 <FlushPoU_Dcache>   <- L1-only DCCISW: cleans and invalidates, leaves the L2
800462dc:	...                                  (520's dump read this as lr out of the frame)
80046304:	bl	80045728 <InvalidatePoU_Icache>
80046308:	bl	800173c0 <flush_core_tlb>
8004631c:	mrc	15, 0, r0, cr1, cr0, {0}    <- caches.c:490's read
80046320:	orr	r0, r0, #4
80046324:	mcr	15, 0, r0, cr1, cr0, {0}    <- SCTLR.C comes back ON, here
80046328:	isb	sy
8004632c:	mrc	15, 0, r0, cr13, cr0, {4}   <- getCpuDatap(), read with the cache already on
80046338:	str	r1, [r0, #304]	; 0x130     <- cpu_CLW_active = 1 (caches.c:494)
8004633c:	pop	{fp, pc}                     <- the instruction 519 and 520 faulted on
```

Three things follow, and none of them was in the arm's design argument until this was read:

* **`SCTLR.C` is set at `0x80046324` - *after* the `push`, *before* the `pop`.** So 520's `pop` did not
  run with the cache off; it ran with the cache **on**, which is exactly why it *hit* the stale valid line
  instead of missing it. That is the mechanism, and it is three instructions wide: the window's
  `SCTLR.C`-clear is undone 100 bytes before the fatal load.
* **The `pop` misses L1 and reads the L2, and only one operation in the whole exit can change that.** The
  two `bl`s at `0x80046304`/`0x80046308` are `InvalidatePoU_Icache` and `flush_core_tlb`, and they are
  **not executed in this configuration**: `caches.c:466` guards them with
  `if (!up_style_idle_exit || (real_ncpus > 1))`, and 515's `up_style_idle_exit=1` with a uniprocessor
  makes that false - which the image says in its own two globals, the test at `0x800462dc..0x80046300`
  loading `0x805511a4` and `0x80520378` and the `bcc` at `0x80046300` skipping both calls. So between the
  `push` and the `pop` the **only** D-cache-affecting instruction is Apple's own `FlushPoU_Dcache` at
  `0x800462d8` - a `DCCISW` loop over L1 - and everything else in between (the ACTLR rejoin, the `SCTLR`
  write, `cpu_CLW_active = 1`) leaves the line alone. That is why the reading above is short enough to be
  a prediction rather than a hope.
* **This arm's only job is therefore the `push` at `0x800462d4`.** With `C` on there, the store hits the
  valid L1 line that holds the pre-window deadline, writes `lr` into it and marks it dirty; the very next
  instruction - `bl FlushPoU_Dcache`, `DCCISW` - cleans it to the Point of Unification, i.e. **into the
  L2**, and invalidates the L1 copy. The `pop` then misses L1 (that same invalidate) and reads the L2,
  which now holds `lr` instead of the deadline. Nothing in that chain is a maintenance operation
  performed *inside* the window by this image: the enable is at the window's near end and the flush is
  Apple's own.

### 1.2 The coherence domain, the flag, and what this arm lengthens

The two ends of the idle window are not symmetric, and reading them out of `caches.c` changes what this
arm can be said to be safe against:

* **Apple's exit rejoins the coherence domain *before* it re-enables the cache.** `platform_cache_idle_exit`
  is `FlushPoU_Dcache` (`caches.c:458`) -> the guarded I-cache/TLB pair -> "Rejoin the coherency domain"
  (`ACTLR` bit `0x40` set, `caches.c:475-491`, in the image at `0x8004630c..0x80046318` with the
  `dsb`/`isb`/`dsb` dance) -> **then** `SCTLR.C |= SCTLR_DCACHE` (`caches.c:493-497`, `0x8004631c..0x80046328`)
  -> then `cpu_CLW_active = 1` (`caches.c:495`). The enter is the mirror with the same ordering: clear `C`
  (`platform_cache_disable`, `caches.c:385-395`), clean, and leave the domain at the end.
* **So 522 re-enables the cache while the CPU is still out of the coherence domain** - the reverse of
  Apple's order, inside the window instead of at its exit. On this machine that is not observable:
  `real_ncpus` is 1, so the only agent that could miss a dirty L1 line allocated outside the domain is
  the core itself, which is exactly the core that allocated it; the exit's own `FlushPoU_Dcache` cleans
  those lines to the Point of Unification one call later, the rejoin happens immediately after, and the
  exit's own `SCTLR` write is then a no-op. **It becomes a real hazard the moment a second CPU or a live
  DMA master exists** - an L1 line dirtied outside the domain is invisible to both - which is precisely
  the "get the drivers running" milestone of this project. That is recorded here as an obligation of the
  drivers step, not as a defect of this arm.
* **`cpu_CLW_active` is not cleared during this window at all**, because the write that clears it
  (`caches.c:421`) is in the *else* branch of `caches.c:414` and `up_style_idle_exit && (real_ncpus == 1)`
  is true. The cross-CPU `cache_xcall` protocol therefore never sees this CPU as "in the low-power window"
  - it stays `1` throughout, which is also the state the exit writes - so the enable does not mislead it.
  The same branch choice is *why* the enter's own clean is `CleanPoU_Dcache()` (L1 only, `caches.c:415`),
  which is the piece 516 was about: on this CPU only `CleanPoC_Dcache` - what the enter *wrapper* calls
  before Apple's function - reaches DRAM, and neither it nor Apple's `CleanPoU_Dcache` invalidates
  anything, so the pre-window line survives **valid in the L2** for the trap above to read.

## 2. The build, and the defect that the build did *not* catch

The clause `xnu_entry_522` asserts the enable's whole body by disassembly rather than by source order -
one `SCTLR` read, one `orr ..., #4`, one `SCTLR` write, in that address order, with a `dsb` and an `isb`
after - the wrapper's single call to it, positioned between the real enter and the WFI, the two `SCTLR`
reads that bracket it (`_win` before, `_set` after) with the note published between the second read and
`entry_note_pce_after`, Apple's own `bic ..., #4` still present inside `platform_cache_idle_enter`, and
the 20-byte `g_slot_cwe` table with its three keys and three write sites. The shared clause lists were
extended with it: seven tables, **44 keys**, and `entry_window_note:3` in the per-note counts.

### 2.1 The clause that makes the arm's central claim structural

The arm rests on one sentence - *this image has no store of its own inside the cache-off window* - and a
sentence in a comment is not a check, so it is now two assertions added to `xnu_entry_522` after the run
order was read off the image (section 1.1):

* **Adjacency, as addresses rather than a count.** The `SCTLR` read that produces `_win` must be the
  instruction immediately after the call to `platform_cache_idle_enter`, and the call to
  `entry_idle_cache_enable` the instruction immediately after that: `cwebefore == realaddr + 4` and
  `cweaddr == realaddr + 8`, in this image `0x8047c928` and `0x8047c92c` against `realaddr = 0x8047c924`.
  With that, the wrapper's whole presence inside the window is one `mrc` - which writes no memory - and
  the count of the image's instructions in the window is two, without the clause having to count them.
  It also says in its own failure message that it assumes a single-instruction `bl`, because an
  out-of-range target would be a `movw`/`movt`/`blx` triple and the refusal would then be about the
  addresses rather than the property - the direction that costs a build and not a run.
* **No store in `entry_idle_cache_enable`.** The body's mnemonics are counted for `st*`/`push`/`vst*` and
  that count must be **0**. The function's whole contribution to the window is six bytes of
  control-register write.

The clause now also *names the window* in its say line, computed rather than asserted: Apple's tail from
its own `SCTLR` write at `0x80046244` to the function's `pop` at `0x800462d0`, **140 bytes, all of it
Apple's** - code that has run in every run since 506 - followed by the wrapper's one read and the
enable's write. It is worth stating what that number replaces: without this arm the cache-off window spans
the WFI, the WFI wrapper's stores, the whole exit wrapper's capture and publishers, and Apple's exit up to
`0x80046324` - i.e. essentially the entire idle path, which is where 519, 520 and 521 all failed. After it,
nothing this project wrote executes with the cache off.

**Both assertions were run against the state they are meant to refuse** before the build was trusted, as
this repository's rule requires: a store spliced into the enable's body counts as 1 and the check fails,
and every displacement of the two addresses - `+8`, `+4`, `0`, and the empty case - is refused while the
true pair passes. The rebuild then came out **byte-identical** in all three artifacts (`stage90-qcdt.img`,
`xnu_arm_entry.bin`, `xnu_arm_entry.elf`), which is what a clause-only edit should do and is itself the
evidence that the image the gate approved is the image the clause describes.

Two defects were found while building this, and one of them is the kind that only a reader catches:

* **The objdump operand field.** The first build refused with "`entry_idle_cache_enable` holds 1 SCTLR
  read(s), 1 SCTLR write(s) and 0 `orr ..., #4`". In `orr r3, r3, #4` objdump's immediate is field 6, not
  field 5 - the mnemonic occupies a field, which `ldr`/`str` do not have. Fixed in the three places the
  clause reads a `#4` immediate from (`orr #4`, `orr != #4`, `bic #4`). This is 517's operand-class
  defect one step further along: not the register's spelling but the *field index*, and it failed the
  build rather than passing quietly, which is the direction that costs nothing.
* **The arm that was built first was not this arm.** `/tmp/stage90-521-resume.sh` exports
  `STAGE90_XNU_EXIT_POC_FLUSH=${STAGE90_XNU_EXIT_POC_FLUSH:-1}` - 521's value, kept as the default when
  the script was written - so the first 522 image carried the exit-side flush *and* the cache enable,
  i.e. two state changes, one of them the operation that had just failed to come back. **The build was
  green, and the 517 clause passed**: that clause asserts the image agrees with the *flag*, and
  `STAGE90_XNU_EXIT_POC_FLUSH=1` is a perfectly self-consistent image. Nothing in the build compares the
  flag against **the arm the step intends**, and the only reason it was caught is that the wrapper's
  disassembly was read by hand and `bl 80045828 <FlushPoC_Dcache>` was sitting at `0x8047c97c`, the first
  call in the exit wrapper. The image was rebuilt with the flag explicitly at 0. This is filed as a
  measurement defect: **a resume script's retained default is a decision nobody re-made**, and the tell
  is that the build log quotes the switch *and* the clause's own say line describes the arm - both were
  read as "the switch is set the way the step wants" when the only thing they establish is internal
  consistency.

The rebuilt image's clause output is the one quoted in section 1, and the 517 clause now reads
`STAGE90_XNU_EXIT_POC_FLUSH=0 - ... calls FlushPoC_Dcache ... 0 time`, which is the image this arm is.

## 3. What the run is to be read for, written before it happens

The gate's own text carries these four, so a run is read the same way whichever surface is opened first:

1. **No `sleh_abort` panic and no `xnu_live_sleh_storm` growth past 520's 9.** The idle exit retires its
   own epilogue - this is the verdict, and everything else is context for it.
2. **`xnu_live_slot_cwe_win` shows `C` clear and `xnu_live_slot_cwe_set` shows it set, with `_calls`
   >= 1.** The enable ran inside the window and really took. A `_win` with `C` already set means
   `platform_cache_idle_enter` did not clear it where the image thinks it does, and the whole arm is
   then about a window that was not open.
3. **`xnu_live_slot_post_calls >= 1`** - the exit *returned* through the wrapper, which 520's run never
   did (its pass died inside the call). This is the reading that says the `pop` went somewhere legal,
   independently of where the boot goes next.
4. **Any later ending than 520's is progress**: a different `pc`, or the boot moving on to whatever it
   does after the first idle pass. The same death at the same `pc` says the mechanism is not the cache
   state at all but something the window itself does - and the arm after that is a **null instrument**:
   the same wrapper with the readings replaced by a counter, to separate the cost of the measurements
   from the cost of the state change.

One known consequence of this arm, recorded here so that it is not misread as a broken instrument:
`entry_note_timebase_call` increments its counters and then returns early when it sees `SCTLR.C` **set**
(`entry_stubs.c:5768`), so once this enable has run the `xnu_live_tb_*` records stop. **That is not
silence, and reading it as silence would be a mistake**: the same function publishes
`xnu_live_tb_calls` and `xnu_live_tb_off` (`entry_stubs.c:5897-5898`), so what the log shows after the
enable is both counters rising together with `xnu_live_tb_seq` frozen - the instrument stating *why* it
has nothing to add, which is a reading rather than a gap. The one C-gated publisher in the image is that
one; the three the verdict rests on (`xnu_live_sleh_*`, `xnu_live_slot_cwe_*`, `xnu_live_slot_pre/post_*`)
never test `C`, so this arm cannot blind its own evidence.

**And one consequence of the enable for the readings' *durability*, worked through rather than assumed.**
The `push` that loses in 520 is not the only thing the window does with `SCTLR.C = 0`: the exit wrapper's
own capture and `entry_slot_note(&g_slot_pre, ...)` run *before* the real exit, i.e. **inside** the
window, so in 520 those stores went straight to DRAM - which is why 520's `pre` reading was there to be
read at all. With 522's enable, every one of those stores runs with the cache **on** and lands in L1,
where it stays until something cleans it. There is no case in which that loses a reading, and the
argument is short: either the run comes back - and then Apple's own reboot path cleans the cache to the
Point of Coherency (`platform_cache_shutdown`, `caches.c:373-381`, `CleanPoC_Dcache` plus the dispatch
hook) before the payload runs again, which 520's own run demonstrates empirically, since its `post`
readings were written after the real exit with the cache on and were readable - or the run does not come
back, and then *the whole log is gone*, cached or not, which is 521. What this does change is the shape of
a partial answer: a run that dies at the same `pop` can no longer distinguish "the enable never ran" from
"the enable ran and the reading did not reach DRAM", so the verdict for that outcome rests on the panic's
presence and on `xnu_live_slot_cwe_calls` in the *previous* pass's data rather than on the new keys alone.

## 4. Safety

Non-persistent `fastboot boot` only, and the run is a single one through
`preflight_boot_check.sh --allow-xnu-entry` then `run_and_capture.sh --allow-xnu-entry`; nothing is
flashed and nothing is ever written to storage, so a brick is impossible by construction and the failure
mode is a hang that needs a power press. **The device is currently in exactly that state from 521 and the
run cannot be made until it is back** - this step's gate is green and its image is frozen, and the run is
owed rather than pending on anything in this repository. That the ledger now has two non-returns, both
with cache maintenance inside the cache-off window and neither with a maintenance operation *outside* it,
is the reason this arm moves the change out of the window rather than into it.
