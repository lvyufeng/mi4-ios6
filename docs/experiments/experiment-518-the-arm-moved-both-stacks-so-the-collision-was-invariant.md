# 518's arm moved both stacks, so the collision came back unchanged — and the frame it was meant to convict turned out to be authentic

Stage90, the first hardware run of `STAGE90_XNU_ISTACK_SEPARATE`. Device `4a2fe00b`, one non-persistent
`fastboot boot` through the standing gate, nothing flashed. The image is
`stage90-qcdt.img` sha256 `32a513bc322f6db7465550039069fc93ed5ca4c9622fed95bfee0673b64ca519`
(`entry_istack_store` `0x80007710`, `entry_istack_separate` `0x80007718`, 268 bytes; `__wrap_ml_get_timebase`
`0x8047c904`; `platform_cache_idle_exit` `0x800462d4`, 0x6c bytes; `cpu_idle` `0x8000d424`).

Run: gate green and the image's hash printed by the gate matched the file, `found 4a2fe00b in adb`,
`Sending 'boot.img' (8340 KB) OKAY` / `Booting OKAY`, the device returned inside the 180 s window, and
`/tmp/cancro-last_kmsg.txt` came back at 599,995 bytes with 3,932 `MI4IOS6_STAGE90` lines and 4,263
`xnu_live_*` records. **This time the device came back on its own** — 517's run needed a power press and
lost its log; this one ends in `MACH Reboot`, as 516's did.

## 1. What the arm was for, and the one reading that says it could not test it

518's hypothesis: `fleh_irq_kernel` builds its 360-byte frame below the interrupted `sp` and then takes the
handler's stack from `cpu_data->istackptr` (`locore.s:1361-1362`), which the kernel sets `= intstack_top`
and never adjusts; this boot runs the OS *on* the interrupt stack, so the handler's spills grow down into
the frame, and 516's `SS_PC`/`SS_STATUS`/`SS_VADDR` — the top three fields, reached first — held a timebase
and a `5`. The arm pointed `istackptr` at `intstack + 8192`, expecting to leave the top half of the stack to
the boot and idle code and give the handler 8 KB below them.

**It moved both.** The idle code's stack top and the handler's stack top are the same register, so the arm
translated the whole arrangement down by 0x2000 and changed nothing about the geometry:

| reading | 516 (`istackptr = intstack_top = 0x80518000`) | 518 (arm on, `istackptr = 0x80516000`) |
|---|---|---|
| `xnu_live_irq_istackptr` | (not recorded) | **`0x80516000`** — the field moved |
| panic `saved state` | `0x80517e88` | `0x80515e88` (exactly −0x2000) |
| frame `SS_SP` / panic `sp` | `0x80517ff0` | `0x80515ff0` (exactly −0x2000) |
| `SS_SP − istackptr` | **−16** | **−16** |

`SS_SP == istackptr − 16` in both runs is the whole result: the frame's top word sits 16 bytes below the
handler's first push, before and after the change, so the arm could not have separated anything. The
identical panic that returned is therefore **predicted by the invariance and is not evidence about the
clobber hypothesis either way.** The hypothesis was not tested — the experiment was void, and the reading
that voids it is a subtraction of two numbers the log already had.

Why one field cannot separate them: `istackptr` has two readers that both want the same value. The vector
reads it to place the handler's stack (`locore.s:1304, 1377, 1514, 1587, 1687, 1755`), and the context
switch reads it to place the **idle thread's** stack (`cswitch.s:182, 205`). The image agrees: `cpu_idle`
(`0x8000d424`) begins `sub sp, sp, #8` and never sets `sp` from a stack pointer — it *inherits* it, and the
entry already has the hook that supplies it, `__wrap_Idle_load_context` `0x8047c794`, called from
`0x8000d46c`. So the idle thread's outermost frame and the handler's stack are the same memory by
construction, and any change to `istackptr` moves both. `start.s:310-311` (`ldr sp, <intstack_top>` /
`sub sp, sp, #80`, `0x800002d4`) sets the *boot's* sp — the idle thread's is the one that tracks the field.

## 2. The frame is authentic, and 516's "the handler's spill is in `SS_PC`" was wrong

The step's own abort instrument (`entry_irq.c`'s `xnu_live_sleh_*`) reads the frame at the abort and
published, for the fatal record (sequence 9 of 9): `sleh_pc = 0x04e1ae60`, `sleh_lr = 0x800462dc`,
`sleh_sp = 0x80515ff0`, `sleh_cpsr = 0x800000b3`, `sleh_fsr_frame = 0x00000005`,
`sleh_far_frame = 0x04e1ae60`, `sleh_frame_ok = 0x00000001`, `sleh_user = 0x00000000`.

Every one of those is what the vector writes for a prefetch abort, not what a spill leaves:

* `SS_SP = frame + 360` exactly, which is the identity the vector itself stores (`locore.s:1344-1349`).
* `SS_PC == SS_VADDR` — for a prefetch abort the faulting instruction address and FAR are the same number
  by construction, and the panic's own live panel agrees (`fsr: 5  far: 0x04e1ae60`).
* `SS_STATUS = 5` is a real IFSR (translation fault, section) and `SS_CPSR = 0x800000b3` is a real CPSR.
* The three fields a spill would reach *first* — `SS_VADDR` (frame+72), `SS_STATUS` (+68), `SS_CPSR` (+64) —
  are the three that are intact and mutually consistent.

So the readings that 516 was explained by are the abort's own, and the explanation in 516/517 ("the handler's
stack overwrote the top three fields, highest address first") does not hold. That is a correction, not a
refutation of the collision: what follows shows the collision is real and lands **one word higher than the
frame**.

## 3. Where the damage actually is: the idle code's own saved return address

Two facts from the image, both checked by disassembly of the arm that ran:

* The interrupted code was inside `platform_cache_idle_exit`: the frame's `SS_LR = 0x800462dc`, which is
  `0x800462d8 + 4` — the return address of that function's `bl FlushPoU_Dcache`. (`LR` is unchanged for the
  rest of the function, so this places the interrupted code anywhere in `0x800462dc..0x8004633c`; 517's doc
  read it as "the flush itself", which is one address in that range among many and was not established.)
* **`platform_cache_idle_exit` has exactly one indirect transfer in its whole 0x6c bytes: the closing
  `pop {fp, pc}` at `0x8004633c`** (`push {fp, lr}` at `:62d4`, `bl FlushPoU_Dcache` at `:62d8`, `bl
  InvalidatePoU_Icache`/`bl flush_core_tlb` at `:6304`/`:6308`, `mrc`/`mcr` of c1 at `:630c`..`:6328`,
  `mrc TPIDRPRW` + `str r1, [cpu_data + 304]` at `:632c`..`:6338`, `pop {fp, pc}` at `:633c`).

So the PC came off that function's own stack, and `sp` arithmetic pins the slot exactly. `pop {fp, pc}`
is `ldmia sp!, {fp, pc}`: `sp` is written back as part of the instruction and the branch happens after, so
the abort's saved `sp` is the *post-pop* value — and that is the frame's own `SS_SP`:

    slot = [SS_SP - 8, SS_SP) = [0x80515fe8, 0x80515ff0) = [istackptr - 24, istackptr - 16)

The two words in it were `0x04e1ae61` (popped into `fp`, and the panic's `r11` is exactly that) and
`0x04e1ae60` (popped into `pc`). `platform_cache_idle_exit` pushed `{the wrapper's fp, 0x8047c8dc}` there —
a *valid* return address, since the entry's `__wrap_platform_cache_idle_exit` (`0x8047c8d0`) `bl`s the real
function at `0x8047c8d8` — so the slot was **overwritten between the push and the pop**, and it was
overwritten with two values **one tick apart in the timebase**.

`[istackptr - 24, istackptr - 16)` is exactly the handler's **5th and 6th pushed words**: the handler's stack
top is `istackptr`, so its words land at `istackptr - 4`, `-8`, `-12`, `-16`, `-20`, `-24` … The 4th word
(`-16`) ends exactly at the frame's top word and the 5th and 6th (`-20`, `-24`) land in the idle code's saved
`{fp, lr}`. **That is 518's collision, drawn correctly**: the frame is one word too low to be touched by a
shallow handler, and the idle code's return address is one word too high to escape one.

And the bad value is *fresh*, which says the words came from a handler that had just read the counter: the
run's own halt instrument recorded `xnu_live_wfi_after = 0x04e1ae7b` immediately before the panic, so the
fetch address `0x04e1ae60` is the timebase **27 ticks before the post-`wfi` reading** — "now", not a stale
carried value. 516's dump has the same shape (`pc` 28/29 ticks below its own post-`wfi` reading, `r4 = r11 =
pc + 2`), so the two runs agree on the mechanism and differ only in which two counter readings were in the
slot that time.

## 4. Two readings this step was built to publish, and neither was publishable

Both are the same defect class as 461's ("the report path's buffer was the tracer's and was full when the
report was written"): *a reading that exists only on a path the death never reaches.*

* **The 517 frame reader recorded nothing at all.** No `xnu_live_tb_*` key appears in the log — not even the
  totals. `entry_note_timebase_call` opens with `if ((sctlr & 4u) != 0u) return;` (`SCTLR.C` set: "outside
  the window, counted, not recorded"), and the per-call records are behind that gate; the totals
  (`g_tb_calls`, `g_tb_off`, `g_tb_seen`, `g_tb_held`) are `.bss` and are printed **only** by the console
  epilogue. A run that ends in a panic never reaches the epilogue, so the instrument that the step was
  built around reports nothing in exactly the runs it is needed for.
* **`xnu_live_istack_before` is unwritable by construction.** It is written when `g_istack_moved == 1`, i.e.
  at the *first* store — and 518b's own design puts that store before the live channel is initialised, so
  the field's initial value never reaches the log either. The values that did arrive
  (`xnu_live_istack_moved = 2`, `_after = 0x80516000`, `_cpsr1 = 0xc0000093`, `_cpsr2 = 0x40000093`) confirm
  the move happened twice and that both stores ran with `A`/`I` clear and `F` clear in SVC mode — i.e. with
  interrupts *enabled* — so 518b's "the I bit at store 2" question was answered: not early enough to be
  before interrupts could be taken, which is consistent with the collision having happened.

Both are fixable the same way: publish on a site that always runs, not on a path the death can skip.

## 5. What the next arm has to be

The arm cannot be another value of `istackptr`. It has to make the idle thread's stack and the handler's
stack **different memory at the same time**, and there is already a hook that can do it:
`__wrap_Idle_load_context` (`0x8047c794`), called from `cpu_idle` at `0x8000d46c`. Point `istackptr` at a
separate region while the real `Idle_load_context` runs (so the idle thread's `sp` is taken from there), then
restore `istackptr = intstack_top` — the vector then keeps the whole 16 KB interrupt stack for handlers while
the idle code runs elsewhere. Two effects, and the second is worth more than the first: a stack that is
**outside** `[intstack_top - 16384, intstack_top)` also makes `ml_at_interrupt_context()`
(`machine_routines.c:671`) answer *false* for a fault in the idle code, so a recurrence would be a
recoverable abort the entry's own `sleh` instrument already knows how to take, instead of Apple's
`panic: sleh_abort at interrupt context`.

That is the next step's subject. It is stated here and not built here because it is a stack change with a
mechanism to check first (`Load_context`'s other readers, and how far the idle path actually descends below
its own frame), and because this run's job was to make the mechanism concrete — which it did.

## 6. Safety

One run, non-persistent `fastboot boot` through `stages/stage90/preflight_boot_check.sh --allow-xnu-entry`
and `stages/stage90/run_and_capture.sh --allow-xnu-entry`; **nothing flashed and nothing written to
storage**, so the brick half of the standing constraint ("一定要保证不要让设备彻底死机或者变砖") holds by
construction. The hard-hang half held this time: the device panicked, printed `Attempting system
restart...MACH Reboot`, and re-enumerated on its own inside the capture window, and the log was captured —
unlike 517's first run, which needed a power press and lost its log. 517's silent image
(`59618b02f6...`) is still unexplained and is still not run again as it stands.
