# D13 launchd early-ABI study: the four dormant HD2-lineage gates

Method. Tree read-only: `external/xnu-hd2-darwin13/xnu` (no edits, no build, no git). Rendering in this
session corrupts long text, so every claim below was taken from a machine reading: `grep -c` on exact
sentinel strings, `grep -n` for line numbers, `python3 -I` `repr()` for structure; counts quoted are
grep counts. Board reality (by value): the C build defines only `-DBOARD_CONFIG_MSM8974_CANCRO=1`
(`tools/build_xnu_arm_kernel.sh:202`); the assembly layer receives **no** `BOARD_CONFIG_*` macro at all
(`tools/xnu_config/arm_asm_defines.sh` and `tools/assemble_arm_layer.sh`: 0 hits for `BOARD_CONFIG`).
So on Mi4, in both C and asm, `BOARD_CONFIG_ARMPBA8`/`BOARD_CONFIG_QSD8250_LEO` are undefined→0, and
`IOS7_COMPAT_PLATFORM` (= their disjunction, `osfmk/arm/ios7_compat.h:5`) is 0. Context: 991 pressed
`af099f4` — launchd[1] exec'd, dyld ran (pc/lr 0x2be5–0x2be7), then 231× `EXC_BAD_ACCESS` at
0xffff4090, zero kernel panics (`records/revert-set.txt:13857-13866`); 992 (six byte-identical gate
lines: `tools/patch_d13_commpage_alias.py:42-48`, pmap.c ×4 + commpage.c ×2) is pending press.

## 1. `bsd/kern/pthread_synch.c` — pthread thread-state ABI (USER-ABI, gated)

Guard sentinel `#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO` occurs **5×** in this file:
`228, 360, 370, 1082, 2308` (plus ARMPBA8-only at 381, 2313, 2328).

- `:228-232` inside `bsdthread_create` (def `:189`; syscall 360, `bsd/kern/syscalls.master:549`):
  `stackaddr = 0x20000000;` (HD2) else `stackaddr = 0x2FD00000;` — a *hint* only, consumed by
  `mach_vm_map(..., VM_FLAGS_ANYWHERE)` at `:248`. Same pattern at `:1082-1086`
  (workqueue thread alloc): `0x20000000` vs `0x2FE00000`.
- `:360-372` — exactly three things: (a) `bzero(ts, sizeof(*ts))`; (b) `ts->cpsr = 0x10U |
  ((p->p_threadstart & 1U) ? 0x20U : 0)`; (c) `ts->pc &= ~1U`. Enabling it therefore pins the
  ARM/Thumb pair (pc even, CPSR.T from the discarded bit). `p->p_threadstart` is a *userspace* value
  (`:498 p->p_threadstart = uap->threadstart;`, syscall 366), so bit 0 is whatever libsystem's
  trampoline symbol carries.
- Today on Mi4: the `arm_thread_state_t state;` local (`:358`) is **never** zeroed and `cpsr` is never
  set here. The sink partly rescues this: `thread_set_wq_state32` (`osfmk/arm/status.c:547`) bzeroes
  its own `saved_state` (`:564`) and applies `saved_state->cpsr = sanitise_cpsr(state->cpsr)` (`:575`);
  `sanitise_cpsr` (`status.c:112-123`) forces mode 0x10 and clears E/I/F but **keeps bit 5 (T)**. So
  today's first pthread gets a T bit derived from uninitialized stack memory, with `pc` unmasked.
- Related landmine in the same file: `:2308-2337` (`setup_wqthread`) — the ARM path is
  `#else panic("setup_wqthread");` (`:2336`, 1 hit). On Mi4 any workqueue-thread creation panics.

Caller path / timing: only reachable from userspace via syscall 360 (`pthread_create`); no in-tree
caller. Strictly **after** dyld's commpage read — 991 reached it only after exec.

## 2. `osfmk/arm/status.c:59-73` — default user stack (USER-ABI, gated)

`#define USRSTACK 0x2FE00000` (`:59`); `:62 #if IOS7_COMPAT_PLATFORM` → `ios7lab_user_stack()` (`:63-69`)
returns `0x2BE00000U` when `ios7_compat_vmabi_enabled()`, else `USRSTACK`; `#else` → `LAB_USER_STACK
USRSTACK` (`:72`). On Mi4: 0x2FE00000. When enabled, the selector is shared (`ios7_compat.h:11-19`:
LEO returns 1 always; ARMPBA8 reads boot-arg `ios7lab_vmabi`).

Consumers (verified): `thread_userstack` (`status.c:83-105`, uses `LAB_USER_STACK` at `:90`, `:98`) and
`thread_userstackdefault` (`status.c:161-167`, `:164`), called from `bsd/kern/mach_loader.c:1091`
(`load_main`, LC_MAIN: `result->user_stack = addr - slide`) and `:1263` (`load_threadstack`,
LC_UNIXTHREAD). The region is then allocated as `[user_stack - size, user_stack)`
(`bsd/kern/kern_exec.c:3604-3639`). Net change: the exec'd image's initial stack top moves
0x2FE00000 → 0x2BE00000.

Coupling (by value): the same selector also moves the shared region — `osfmk/vm/vm_shared_region.c:626-637`
sets `base_address = 0x2C000000U; size = 0x14000000U;` with a printf ending `"stack below 0x2be00000"`
(1 hit each); the ungated ARM values are `0x30000000/0x10000000`
(`osfmk/mach/shared_region.h:67-68`). Note 0x2BE00000 is also this tree's dyld canonical base
(`osfmk/arm/ios7lab_fault_witness.h:27`), consistent with 991's pc/lr. Timing: runs during launchd's
**exec** (before dyld) — and it already succeeded in 991, so this is a latent layout difference, not
the frontier. Do not enable the stack half without the shared-region half.

## 3. `bsd/kern/kern_fork.c:98,1098,1146` — early-PID reservation (KERNEL-BEHAVIOR, gated)

- `:98-100` `#if BOARD_CONFIG_ARMPBA8` → include `<pexpert/pexpert.h>` only.
- `:1098-1100` `#if IOS7_COMPAT_PLATFORM` → `boolean_t lab_reserve_early_pids =
  ios7_compat_vmabi_enabled();` (compiled out on Mi4).
- `:1146-1154` same gate: `if (lab_reserve_early_pids && nextpid > 1 && nextpid < 10) nextpid = 10;`
  (1 hit). Effect when on: after PID 1, children start at 10 instead of 2…9.
- launchd keeps real PID 1 either way: the first `forkproc` caller is `bsd_init.c:1115
  thread = cloneproc(TASK_NULL, kernproc, FALSE);` (1 hit) — `forkproc`'s `nextpid++` (`kern_fork.c:1145`)
  hands that child 1 (`bsd_init.c:1121 initproc = proc_find(1);`, 1 hit, confirms).

Timing: first exercised on launchd's second process (fork/posix_spawn of a job) — after launchd's main
runs, hence after the commpage wall. No in-tree consumer of the 2…9 range was found.

## 4. `osfmk/arm/traps_lo.s:116-119, 162-168, 260-268` — the −3 SWI timebase (USER-ABI, **negated twin**)

How r12 is set: `EnterARM(fleh_swi)` (`:115`) expands to label + alignment only
(`osfmk/arm/asm_help.h:53-57`), and the vector table is a bare `ldr pc, [pc, #24]`
(`osfmk/arm/exctramps.s:53-70`) — nothing extracts the SVC comment. The number is put in r12 **by
userspace**: `libsyscall/custom/SYS.h:245` "load the syscall number into r12 and trap", macro
`DO_SYSCALL` (`:246-255`) = `mov r12, #(num) … swi #SWI_SYSCALL`, `SWI_SYSCALL 0x80` (`:176`). The kernel
reads it from the saved state: `bsd/dev/arm/unix_syscalls.c:327 code = state->r[12];` (and `:545-546`).

- `:116-119` — guard is **negated** (`#if !(BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO)`, 1 hit),
  so Mi4 compiles it **in**: `cmn r12, #3` / `beq swi_trap_tb`, and `swi_trap_tb: /* Fast return */
  movs pc, lr` (`:260-262`). This runs *before* any register save: **today a user `r12 = -3` returns
  immediately with r0:r1 unchanged** (silently wrong, not loud).
- `:162-168` — Mi4 compiles it **out**: after the state save, `cmn r11, #3` (`mov r11, r12` at `:135`) /
  `beq ios7lab_timebase`; `:264-268 ios7lab_timebase: blx _mach_absolute_time / b swi_exit64`;
  `:195-196 swi_exit64: str r1, [r8,#4]` then `swi_exit` stores r0 — the only 64-bit (r0:r1) return.
- Why the special case exists at all: `mach_trap_table[3] = kern_invalid`
  (`osfmk/kern/syscall_sw.c:114`), so a plain dispatch of −3 would take the error path
  (`:232-240`), and `swi_exit` stores r0 only (`:199`).
- Who issues −3: **no caller in the tree** (0 hits for `-3` as an assembler immediate in osfmk/bsd/arm;
  0 hits for `mach_absolute_time` under `libsyscall/`). The issuer is the iOS 7 rootfs's
  libsystem_kernel. In-tree evidence the target is `mach_absolute_time`: the arm's comment (`:163-165`,
  "Original iOS ARMv7's -3 fallback returns the actual 64-bit clock") and the callee symbol.
- Where the clock comes from today (tick path fixed by 985): `mach_absolute_time` =
  `osfmk/arm/rtclock.c:188-193` → `rtclock_timer_scale_convert` (`:82-93`) → `pe_arm_get_timebase`
  (`pexpert/arm/common/pe_armsupport.c:94-100`) → `gPESocDispatch.get_timebase` =
  `msm8974_get_timebase` (`src/platform/darwin13/pe_msm8974.c:407-424`, raw CNTVCT; registered `:464`),
  scaled by `tscFreq = gPEClockFrequencyInfo.timebase_frequency_hz = 19 200 000`
  (`pe_msm8974.c:402`; `rtclock.c:299-302`); `rtc_configure` unconditional (`pe_msm8974.c:403`) keeps
  `rtclock_sec_divisor` non-zero. So enabling the arm makes the SWI return that same real clock.

## Ranking (candidate next rungs)

1. **traps_lo.s −3 timebase** (`:116-119` off / `:162-168,264-268` on) — asm-layer only, no C/pcb
   change; the fast-return is on the same entry used by every syscall; today it is silent-wrong. Do it
   even if unproven on launchd's path: it is the cheapest gate and the only one whose *correct*
   implementation already exists verbatim in the tree.
2. **pthread_synch.c:360-372** — first `pthread_create` after the commpage wall now depends on
   `sanitise_cpsr` keeping a garbage T bit; enabling pins (pc&~1, T).
3. **pthread_synch.c:2308-2337** — `panic("setup_wqthread")` is a hard landmine for libdispatch
   workqueue threads; enabling the gated ARM path is the fix, but it is a bigger block (arg setup for
   `_start_wqthread`) — schedule after 2.
4. **status.c:62-73 / kern_fork.c:1098,1146** — latent: exec-time stack layout (must move with
   vm_shared_region.c:626-637) and the second fork's PID range; neither can be the next frontier.

## Checker sketch (top rung, modeled on 992)

`tools/patch_d13_timebase_swi.py` appends `|| (defined(STAGE90_XNU_TIMEBASE_SWI) &&
STAGE90_XNU_TIMEBASE_SWI)` to `traps_lo.s:116`'s **negated** guard and to `:162/:264` gates (off switch
leaves the file byte-identical, as `patch_d13_commpage_alias.py` does). `tools/check_timebase_swi.py`
then (a) refuses a staged tree whose `traps_lo.s` still has `cmn r12, #3` inside an active `#if`
(grep -c based, expect 0 staged / 1 unstaged), and (b) walks the **linked ELF** like
`tools/check_commpage_alias.py`: locate `_fleh_swi`, assert an ARM `bl`/`blx` to `_mach_absolute_time`
occurs after the state save and that `movs pc, lr` is not the first r12 check, and assert `swi_exit64`'s
`str r1, [r8, #4]` is reachable. Add a one-shot printf marker in `ios7lab_timebase` (the file has no
`kprintf` no-op problem) so the press log names the arm, and pre-register the falsification: a userspace
`mach_absolute_time` still returning 0/garbage, or `swi_trap_tb` still being taken.

## Uncertainties

- No in-tree caller of the −3 SWI: the path's exercise frequency on launchd is an inference from the
  asm comment, not a reading. 991's 231 aborts at commpage+0x90 show userspace used the *commpage*
  clock path, not (necessarily) the SWI fallback.
- status.c's 0x2BE00000 collides in value with the dyld canonical base (`ios7lab_fault_witness.h:27`);
  the shared-region move (`vm_shared_region.c:626-637`, printf text) is the only thing that makes it
  coherent. Treat 2+4 as one coupled change.
- `#if defined(BOARD_CONFIG_QSD8250_LEO)` (e.g. `rtclock.c:260`, traps_lo.s:137/202) is the
  "off option has two spellings" hazard: never define that macro as 0.
- Line-exact text for the asm blocks was cross-checked (`grep -c` of `cmn     r12, #3`=1, `beq
  swi_trap_tb`=1, `cmn     r11, #3`=1, `beq     ios7lab_timebase`=1, `blx     _mach_absolute_time`=1,
  `str     r1, [r8, #4]`=1, `movs    pc, lr`@262, `cmp     r11, #0x80000000`@170 — the latter encodable,
  imm8=0x08 ror 4).