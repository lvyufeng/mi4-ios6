# 522: the idle window gets its cache back

Stage90. Built host-side with `STAGE90_XNU_IDLE_STACK=1` (default), `STAGE90_XNU_ISTACK_SEPARATE=0`,
**`STAGE90_XNU_EXIT_POC_FLUSH=0`** - i.e. 521's exit-side flush is *out* of this image - and the one
state change 521's non-return left standing: **the D-cache is turned back on at the idle window's near
end**. Gate green, and **run on 2026-09-22: the device did not come back** - the project's third
non-return, with no log and a power press owed. Section 3.3 is the run and what it does and does not say;
sections 1 to 3.2 are the pre-run reading of the image and of its clause and are unchanged by it.

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

### 1.3 Does anything *read* the bit this arm sets early? A census, because 521's non-return cost a log

522 sets `SCTLR.C` at a moment Apple does not choose, and the thing that could make that different from a
harmless reordering is some *other* code testing that bit and taking a different branch. So the census was
taken across Apple's whole ARM layer, and it is short enough to state in full:

* **The bit is written in exactly two places**: cleared at `caches.c:393` (`platform_cache_disable`, inside
  `platform_cache_idle_enter`, the window) and set at `caches.c:490` (the exit). 522 adds a *third* writer
  and changes neither of Apple's.
* **It is never tested.** `SCTLR` is read for modification only: `caches.c:390` (whose result is `&= ~`'d)
  and `caches.c:490` (whose result is `|=`'d). No comparison against `SCTLR_DCACHE` exists anywhere in
  `osfmk/arm` or `pexpert/arm` — the only other mentions are the `#define` (`proc_reg.h:498`) and the
  boot-time default mask (`proc_reg.h:511`), which is `start.s:356`'s and runs once, before `arm_init`.
* **The one accessor that hands the register out, `get_mmu_control`** (`machine_routines_asm.s:396`,
  read-only), has six callers and every one of them is in `arm_init` (`arm_init.c:312/380/417/443/501/521`)
  reading-modifying-writing a *different* bit (`SCTLR_PAN_UNCHANGED`, `SCTLR_PREDIC`) — never `C`.
* **And the body that runs inside the window touches no memory at all**: `cpu_idle_wfi`
  (`0x800172dc`..`0x80017330`) is a `dsb` and then the `wfi` (`0x8001730c`) with `add`/`subs`/`bne` on
  registers only — which is why "the WFI now runs with the cache on" is not by itself a statement about
  memory. What *can* run halted under it is an interrupt handler, and a handler reads Device memory
  (uncached **by mapping attribute**, which `SCTLR.C` does not touch) and Normal kernel memory (correctly
  cached); the only Normal-cacheable locations on this path that any agent other than the CPU writes are
  the `cpu_data` fields of the cross-CPU protocol, and with `real_ncpus = 1` there is no such agent.

**So the arm's change cannot alter a decision anywhere in Apple's code**, including in a handler taken
during the halt: there is no branch in the kernel whose outcome depends on `SCTLR.C`. What it *can* alter is
timing and the coherence of this image's own stores - both of which is section 3's argument, and both of
which the two readings measure directly.

### 1.4 The same window is opened in a second place - and Apple stores inside it there

The census above is about *reading* `SCTLR.C`. A census of the other thing - *who opens the window* - turns
up a second opener, and it is worth writing down both because it scopes this arm's claim honestly and
because it names the same class of hazard in the path this project has not reached yet.

`platform_cache_disable` (the `bic #4` in a function of its own, `0x80046224`) has **three** call sites in
the image, not one:

* `0x8047c924` - inside `platform_cache_idle_enter`. This is the window 522 closes early.
* `0x8000dfe8` and `0x8000e088` - inside **`ml_arm_sleep`** (`0x8000dfc0`), Apple's suspend path. The first
  is the **non-boot CPU** branch (never taken on a uniprocessor); the second is the boot CPU's, and the
  instructions after it read:

  ```
  8000e088:  bl  platform_cache_disable     <- the window opens, and nothing closes it early
  8000e08c:  bl  platform_cache_shutdown    (CleanPoC_Dcache, caches.c:373-381)
  8000e090:  ... bcopy(suspend_signature /*0x80487c48*/, [0x805511b8]+128 /*IOS_STATE*/, 8)
  8000e0b0:  dsb sy
  8000e0b4:  b   0x8000e0b4                <- an infinite spin, noreturn
  ```

  **So Apple's own code stores with `SCTLR.C` clear on that path** - the 8-byte suspend signature - which is
  the very semantics this whole walk has been assuming (a store with the cache off reaches DRAM) and which
  `ml_arm_sleep` *depends* on, since the signature is what the wake path reads. It is also the same shape of
  store 520 died in, in a path where nothing does the equivalent of 522's enable.

* **Reachability, checked rather than assumed**: `ml_arm_sleep` has **no caller** in the linked image - no
  `bl`/`b` to `0x8000dfc0` anywhere, no pointer to it in `.data`/`.rodata`, and its name occurs only in the
  ELF `.strtab`, which the payload does not copy (the copied span ends at the pinned `__bss_start`). It is
  `T`/global because a platform kext could call it, and this boot loads no such kext. **So the second
  window is dead code today**, and this arm's claim holds as written: *the image* has no store of its own
  inside *the idle* window, and the idle window is the only one this boot reaches.

**Where that lands as an obligation**: the moment a platform driver that can suspend exists - i.e. the
"get the drivers running" milestone, which is also where 522's coherence-domain note (section 1.2) points -
`ml_arm_sleep` becomes reachable with the cache off and Apple's own signature store inside the window. That
step will have to answer the same question this one did, in a path where the answer cannot be "the cache is
off for 140 bytes and all of them are Apple's".

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

## 2.2 What is actually on the far side of the `pop`, read from 520's own log

This is worth stating before the run, because it changes what the run is *for*. 520's captured log
(`/tmp/cancro-last_kmsg.txt`, 596,665 bytes) is a complete boot, and its own records order it:

| file line | record | value |
|---|---|---|
| 3999–4001 | console | `the OS starts the process at 0x10e0 (the thread's user pc was 0x10e0)` / `the OS's own init load returned, so pid 1 has the init image (caller 0x80049eb8)` / `the AST is done -- pid 1's thread is at 0x10e0 for user mode (sp 0x101efc)` |
| 7643–7781 | `sleh_seq` 1–4 | `user = 0` - ordinary kernel-mode aborts (`far` 0x1000, 0xc8215000, 0xc8256000, 0x101f28) |
| 7803–7821 | `ast_seq`/`ast_done_seq` 1, 2 | the second AST returns with `ast_sp = 0x00101efc` and `ast_pid = 0x00000001` - **pid 1's thread, with a user stack** |
| 7842–7871 | `sleh_seq` 5, 6 | `far = 0x00102000`, `pc = 0x00001118` and `0x00001124`, **`user = 1`** |
| 8188–8202 | `sleh_seq` 7 | `far = pc = 0x000011a4`, **`user = 1`** |
| 8228–8242 | `sleh_seq` 8 | `far = 0x00102000`, `pc = 0x800176e0`, `user = 0` - the kernel handling it |
| 8339–8348 | `sleh_storm = 9` | `pc = 0x04b79074`, `user = 0` - the idle exit's `pop` |

**So pid 1 executes in user mode in that run**: aborts taken with `CPSR` mode = user at user-space
addresses `0x1118`, `0x1124` and `0x11a4` — 0x38, 0x44 and 0xC4 bytes past the entry point the console line
names — on the user stack `0x00101efc` the AST record names. The root MD device is attached, BSD init has
run, `launchd` was loaded twice (the developer path failing with `errno 2`, then `/sbin/launchd`), and the
platform, cache, timer, console and exception paths are all demonstrably working. (The same faults appear in
510's run, and the header of `entry_note_ast_returned` in `entry_stubs.c` cites them - so this is a standing
reading of these images, not a new one.)

**What follows for 522 is therefore not "reach the OS" - it is "survive the first idle pass that the OS
reaches *after* its first user-mode seconds".** The distance between these images and a boot that keeps
running is the 140-byte window this arm closes, and the frontier beyond it is what pid 1 does next: the
run should show the boot going *past* the idle loop - further idle passes, more `sleh_user` records, no
panic - rather than stopping one instruction into the idle exit. A run whose only difference from 520 is
that the `pop` returns is already the answer to this step; a run that goes on to more user-mode activity is
the answer to the next question, which is whether the OS can be said to be *running*.

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

## 3.1 The verdict is now read mechanically, and the reading was tested before the run

A run can cost a power press and a log, so the four criteria above are no longer something an operator holds
in their head while looking at a fresh failure: `run_and_capture.sh --summarise FILE` now prints 522's verdict
for itself. The block is **self-selecting** - it fires only when the log carries `xnu_live_slot_cwe_*`, which
exists in exactly one image built in this project, so it appears for 522's run and never for another step's -
and it prints **PASS / FAIL / UNREAD** as three separate states, because "the key is absent" and "the value
is zero" are different readings about different things.

It was run against four logs before being trusted, as this repository requires of a new check:

| log | expected | got |
|---|---|---|
| 520's real capture (596,665 bytes, no 522 keys) | the block does **not** appear | absent, summary unchanged |
| a log shaped like a pass (`win` with `C` clear, `set` with it set, both counts 1, 4 user-mode records) | all three checks PASS and the "idle exit completed" line | exactly that |
| a log shaped like 520's death (a `sleh_abort` panic, `post_calls = 0`) | verdict FAIL, exit FAIL | exactly that |
| a sparse log carrying one key only | UNREAD branches, no crash, exit 0 | exactly that |

Two defects were found by that exercise and both are in the *reading*: the extraction's `grep` status ends
the caller under `set -e`/`pipefail` when a key is absent (fixed with a load-bearing `|| true`, and it is the
same shape as a defect documented four lines above it in the same file), and the shape tests were written in
decimal while the live channel writes `0x%08x`, so every key that *was* present read as UNREAD. Both are
recorded in this project's measurement-defect table, and neither could have reached a hardware run.

## 3.2 What "the OS" is in these images: a fixture root with one file, whose entry point is its own header

While the device is off the bus there is time to ask the question the goal actually turns on - *how much of
an OS is on the far side of the `pop`?* - and the answer is not a matter of opinion. It is in the image and
in 520's log, and it says that 522 is necessary but nowhere near sufficient for the user's stated milestone.

### 3.2.1 The console, and the drivers that are not there

520's boot output names exactly one console and it is not an I/O Kit one:

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
[os-console-459]
Darwin Kernel Version ###not-built-by-apple###
...
MAC Framework successfully initialized
using 80 buffer headers and 80 cluster IO buffer headers
mcache: 1 CPU(s), 128 bytes CPU cache line size
Added memory device md0/rmd0 ... at 0000000080511000 for 0000000000002000
BSD root: md0, major 2, minor 0
VM_TEST_DEVICE_PAGER_TRANSPOSE: PASS
load_init_program: attempting to load /usr/local/sbin/launchd.development
load_init_program: failed loading /usr/local/sbin/launchd.development: errno 2
load_init_program: attempting to load /sbin/launchd
```

`[os-console-459]` is **the payload's own marker** (`os-console-459` is a cmdline token in
`stages/stage90/build.sh`'s list), so every kernel `printf` below it travels through this stage's console
shim and not through an OS-visible device. `bsd_init` therefore **completed without a console device**:
there is no `IOConsole`/`AppleARMPL011`/UART service, no `IOService` matching that produced `/dev/console`,
and nothing in the image is a driver - `nm` shows `devfs_vfsops`, `mockfs_vfsops`, `routefs_vfsops` and a
large I/O Kit **core** (`IOService`, `IOBSD`, `IOBufferMemoryDescriptor`, the catalogue and registry
machinery), which is a framework for drivers rather than any driver. The two things the kernel does *not*
need a device for are the ones it did: `md0` comes from the platform shim's `RAMDisk` path, and pid 1 comes
from a fixture. **"把基础驱动跑起来" has not started**; the console is the first item of it, because every
service's own diagnostics go there.

### 3.2.2 The root device and the filesystem

**The root device is a fixture.** `rd=md0` with the payload's `RAMDisk` entry in `/chosen/memory-map` makes
XNU attach the 8 KB array `g_stage90_ramdisk` (`0x80511000`, `RAMDISK_BYTES` 0x2000, in `.data`) as `md0` -
520's log says so itself: `Added memory device md0/rmd0 (02000000/0D000000) at 0000000080511000 for
0000000000002000`, then `BSD root: md0, major 2, minor 0`. It is not a filesystem. It is a **32-bit Mach-O
executable**, parsed out of the linked image byte by byte:

```
magic=0xfeedface (MH_MAGIC)  cputype=12 (ARM)  cpusubtype=12 (ARM_V7)  filetype=2 (MH_EXECUTE)
  LC_SEGMENT __PAGEZERO: vmaddr=0x0    vmsize=0x1000 fileoff=0x0    filesize=0x0
  LC_SEGMENT __TEXT:     vmaddr=0x1000 vmsize=0x1000 fileoff=0x0    filesize=0x1000 initprot=5 (r-x)
  LC_UNIXTHREAD flavor=1 (ARM_THREAD_STATE) count=17
      r0-r12 = 0, r12 = 0x14, sp = 0x0, lr = 0x0, pc = 0x10e0, cpsr = 0x10 (user mode)
```

**And that `pc = 0x10e0` is pid 1's user pc in 520's console line** - `the OS starts the process at 0x10e0
(the thread's user pc was 0x10e0)`. So the first process's entry point comes from this stub's own
`LC_UNIXTHREAD`, and `__TEXT` maps file offset 0, which means the bytes pid 1 begins executing at `0x10e0`
are **inside the Mach-O header and its load commands**: `__TEXT`'s 4 KB is the header (196 bytes of load
commands) and nothing else. That is why the run's user-mode faults are at `0x1118`, `0x1124` and `0x11a4` -
0x38, 0x44 and 0xC4 past that entry, i.e. still inside the header - and why the last of them faults at `far
= 0x102000`, the page just past `__TEXT`'s 4 KB: the "program" read its own header as instructions until it
ran off the end of its segment. **It is a stub for the kernel to exec, not a program.**

**The filesystem that serves it is a test fixture, by its own documentation.** `STAGE90_XNU` is
`[ RELEASE mockfs development ]`, and of the filesystems in that configuration only **mockfs** has a
`vfc_mountroot` - `build_entry.sh`'s 459 clause checks exactly that, and that devfs and routefs have no
`mountroot` at the same word, so `vfs_mountroot` mounts mockfs. `mockfs_mountroot`'s own comment describes
what it builds: *"three nodes; a directory node (to serve as a mountpoint for devfs), a file node meant to
serve as an executable frontend for rootvp (*we will assume that rootvp is an executable, that the kernel
can subsequently run*), and the root node."* Its `vnop_lookup` answers the name `launchd` under `sbin` with
that one file node. So when `load_init_program` walks its list - `/usr/local/sbin/launchd.development`
(ENOENT, as `development` is in the config) and then `/sbin/launchd` - the second one **succeeds**, which is
520's second console line: `the OS's own init load returned, so pid 1 has the init image`.

**So the honest statement of where the goal stands**, independent of any cache question:

1. The kernel boots, brings up the platform/cache/timer/console/exception paths, attaches a root device,
   execs pid 1 into user mode and schedules it - all of which 520's log shows, and all of which is real.
2. The root device is an 8 KB Mach-O **stub** and the root filesystem is a **fixture with one file**. The OS
   has nothing to run and nowhere to run it from: no init, no userland, no shell, no on-disk filesystem.
3. **Mounting the storage - the purpose of the TWRP clause - is therefore a phase that has not begun.** This
   configuration has no HFS/APFS/UFS/ISO9660 (`nm` finds `devfs_vfsops`, `mockfs_vfsops` and
   `routefs_vfsops` and nothing that can read Android's or Apple's partitions), so even a correct idle exit
   leaves the OS with no filesystem to mount a real root from.

The ordering consequence is the useful part: **522 remains the right next run** - the idle exit is on the
path to everything, and it is built, gated and frozen - but what comes after it is no longer "one more cache
fix". It is a decision about what the OS's root is going to be: a real in-memory filesystem image, or the
device's own storage with a filesystem this kernel can actually read, plus a first process that is a program
rather than a header. That is a new phase, and it should be planned as one rather than discovered as a
surprise on the next run.

> **Superseded, 2026-09-22:** 522 has now run and did not come back (section 3.3), so "522 remains the
> right next run" is spent - the run happened. What survives of this paragraph is the half that is not
> about ordering: the OS still has no root worth the name and nothing to run from it, and closing the idle
> exit is still the gate in front of every one of those choices. Section 3.3.1 names what comes first now:
> a null instrument, not a cache fix.

## 3.3 The run

Device `4a2fe00b`. The phone came back onto the bus on 2026-09-22 at 14:13:49 as `2717:0368` and then
`18d1:4ee7` at 14:14:09 - Android, `adb devices` listing `4a2fe00b	device` - on a human power press: that
is 521's owed press having happened and the phone being healthy. Gate green (`All checks passed`, exit 0,
and it printed `13d771938336fe65...` computed from the file itself two screens up). One non-persistent
`fastboot boot` through `run_and_capture.sh --allow-xnu-entry`: `Sending 'boot.img' (8340 KB) OKAY [
0.262s]`, `Booting OKAY [ 0.011s]`, `Finished. Total time: 0.291s`. Nothing was flashed and nothing was
written to storage.

**The device did not come back within the 180 s window, and has not come back since.**

The host's own USB log (`/var/log/kern.log`, every line naming `usb 3-10`) carries the whole signature, and
it is 521's signature to the second:

| time | dev | id | what it is |
| --- | --- | --- | --- |
| 14:13:49 | 77 | `2717:0368` | Android, after 521's power press |
| 14:14:09 | 78 | `18d1:4ee7` | Android up; `adb devices` = `4a2fe00b device` |
| 14:14:38 | 78 | - | disconnect: `adb reboot bootloader` |
| **14:14:45** | **88** | **`18d1:d00d`** | **fastboot - the bootloader took the boot command** |
| **14:14:46** | **88** | **-** | **disconnect: the payload took the core** |
| - | - | - | **nothing on `usb 3-10` after that** |

The disconnect at 14:14:46 is *normal*: it is the bootloader handing the core to the payload, which is what
`Booting OKAY` means, and it is what 520's returning runs look like at the same point. What is not normal is
the absence of anything after it - a return is `2717:0368` and then `18d1:4ee7` inside ~20 s, and 520's was
inside the window. The only later event on the bus is an unrelated device on `usb 3-3` (a LeEco LEX727) at
14:15:11, which is not this phone.

No log: `/tmp/cancro-last_kmsg.txt` is untouched (mtime 02:55, 596,665 bytes - 520's capture), because there
was no device to read one from. The phone is off the bus now and needs a **power press** (hold Power
~10-15 s, release, press Power normally); the payload's log lives in the top of DRAM, so that press loses
whatever the run produced.

## 3.3.1 What the non-return says, and what it does not

**It retires the flush-in-the-window as *the* cause.** 521's section 4 named the full L1 *and* L2
clean-and-invalidate executed inside the window with `SCTLR.C = 0` as the prime suspect, on two grounds:
that it was the one operation in that image this hardware had never run, and that it was the only thing the
project's two non-returns shared. This image **removes** it and turns the cache back on at the window's near
end - a state change that runs *outside* the window's arithmetic, which is the property the arm was built
for - and the run did not come back either. A suspect that was present in both non-returns, and absent here,
is no longer the explanation for this one.

**What both non-returns share, and what nothing that has ever come back has, is the capture/publish path
521 introduced**: 8 loads and 8 stores per pass into `.data`, executed *inside* the window, where 520's
capture ran in the callee. 521's section 5 and this log's section 3 both named the arm that follows from
that before either run: a **null instrument** - the same wrapper with the readings replaced by a counter -
to separate "the measurements cost something" from "the state change costs something". That naming is now
the strongest thing this arm produced, and the choice between those two is what the next build is for.

**What it does not say** is which instruction wedged the core, and this run cannot say: there is no log. It
does not even say the death is at the same `pc`. Section 3 of this log worked through exactly this case
*before* the run and its argument stands unchanged: with the enable in place, a run that does not come back
can no longer distinguish "the enable never ran" from "the enable ran and the reading did not reach DRAM" -
and with no log at all it cannot separate either of those from a death anywhere else on the path. So **none
of the four readings section 3 names exists**: `xnu_live_slot_cwe_win`, `xnu_live_slot_cwe_set`,
`xnu_live_slot_cwe_calls` and `xnu_live_slot_post_calls` have no value from this arm, and the mechanical
verdict block section 3.1 added and tested never had a log to fire on.

**One thing it does say that is worth keeping**, because it is now a repeatable observation rather than a
single one: the *shape* of the hang is the same across two images with different state changes and the same
measurement path - the bootloader acknowledges, sends the image, reports `Booting OKAY`, the device leaves
USB, and nothing ever comes back. The mechanism that 520 identified is still the reason this arm exists, and
this run neither confirms nor refutes it. It only removes the flush as the explanation for *this* failure,
which is what the ledger below is for.

The ledger, from the runs rather than from any gate's prose:

| run | window state | capture | returned? |
| --- | --- | --- | --- |
| 519, 520 | cache off, no flush | in the callee (4 loads) | yes, inside 180 s, dying at the `pop` |
| 521 | cache off, **+ L1&L2 flush in the window** | **in the wrapper (8 loads, 8 stores)** | **no** |
| 522 | cache **on** at the window's near end, flush out | in the wrapper (8 loads, 8 stores) | **no** |

**The ledger this run changes, and a reading of it that this run breaks.** 521's section 4 read the record as
*flag-off runs come back, flag-on runs do not* - it was true when it was written, and it is what made the
exit-side flush the prime suspect. **This run falsifies it**: 522 is flag-**off** by construction
(`STAGE90_XNU_EXIT_POC_FLUSH=0` is what makes it this arm) and it did not come back either. So the flag was
never the discriminator. What the three non-returns share is a *state change* the returning runs did not
have - 517's first run and 521 carry the exit-side flush, 522 carries the cache enable - and the one change
521 and 522 share, and 520 does not, is the capture/publish path 521 introduced. The next arm is the null
instrument, and the case for it is the table's third column.

## 4. Safety

Non-persistent `fastboot boot` only, and the run was a single one through
`preflight_boot_check.sh --allow-xnu-entry` then `run_and_capture.sh --allow-xnu-entry`; nothing was
flashed and nothing was ever written to storage, so a brick is impossible by construction and the failure
mode is a hang that needs a power press. **That is what happened** (section 3.3): the phone is off the bus,
the log is lost with the top of DRAM, and a power press is owed before anything else can run. The arm's own
premise - that the ledger's two non-returns both had cache maintenance *inside* the cache-off window and
neither had a maintenance operation *outside* it - is what moved this change out of the window; section
3.3.1 records that the move did not bring the phone back, and what that retires.
