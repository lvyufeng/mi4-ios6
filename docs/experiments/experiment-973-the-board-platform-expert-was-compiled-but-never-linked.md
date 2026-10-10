# Experiment 973 — the 914 board platform expert was compiled but never linked

**Status:** ✅ **BUILT, PARKED (`armed-d13-12427611`).** The 972 press advanced one rung past
`_disable_preemption` and stopped on `stub_hit=PE_init_SocSupport_stub` — the *generated* stub. The
Mi 4's PE methods exist (`src/platform/darwin13/pe_msm8974.c`, the 914 board PE) but were compiled
into `out/xnu_arm_obj_d13/`, a pool **no entry link list reads**. 973 wires that object into the
link. Entry image `12427611` (6331476 B), payload `stage90-qcdt.img 7d12735f…` (9351168 B), payload
switch record `6c2b6038` **unchanged** (the fix is a *link*, not an arm switch). `verify_press_ready`
5/5; `make check` 0. **THE PRESS IS THE OPERATOR'S.**

Continues `experiment-972-the-de-underscore-step-strips-a-c-facing-name.md` (whose press this
follows, one rung up) and `experiment-914-*.md` (which *measured* the board PE but never linked it).
Supersedes nothing.

---

## 0. The arm

`armed-d13-12427611` is `armed-d13-0184b928` (972's de-underscore keep-set) **plus one build-tool
change**: the 914 board PE object is now *linked*. No `STAGE90_XNU_*` switch moves, so the payload's
own switch record `6c2b6038` is byte-identical to 968/970h/971/972 and the arm is named by the entry
bin alone. Medium unchanged: 968's real iOS 7.1.2 HFSX rootfs over `hfs_rmd0`; `HDD_WRITE=0`, the
base is never written — fully reversible.

## 1. The 972 press — what it confirmed and what it stopped on

Capture `out/stage90/captures/972-press-armed-d13-0184b928-20261010-last_kmsg.txt` (451354 B, 4532
lines, sha256 `a02b66cf…`), arm `armed-d13-0184b928`:

- **972 CONFIRMED.** `stub_hit=_disable_preemption` is **absent**; 971 also holds
  (`arm_vm_init: setting up segment information...` after the switch).
- The boot then stops on the **next** missing symbol — `xnu_live_stub_hit_seq=0x00000001`,
  `xnu_live_stub_hit_caller=0x8048bd50` (`caller - 4 = 0x8048bd4c`, the `bl`), runner line
  `stub_hit=PE_init_SocSupport_stub`, and `PE_init_platform` is `+0x2c` from that call site.

The run returned clean in ~17 s (`No errors detected`), no brick.

## 2. The root cause — a definition that existed and was not linked

The reported `PE_init_SocSupport_stub` was the **generated** stub (`0x8049bdc4`, body
`movw r0,#name; movt r0,#0x8056; b entry_stub_hit`). The **real** body was in
`out/xnu_arm_obj_d13/pe_msm8974.o` — the 914 experiment compiled it and measured `1 of 1` — but
`src/entry/build_entry.sh`'s pool glob iterates only `$XNU_KERNEL_OBJ_OUT/*.o` and
`$XNU_ASM_OBJ_OUT/*.o`; **`$XNU_ARM_OBJ_OUT` (= `out/xnu_arm_obj_d13`) is in no link list at all**.
So `PE_init_SocSupport_stub` stayed undefined, and the stub generator fabricated the one the run
stopped on. This is [[mi4-one-value-two-definitions]] with the value being *"the board PE is
compiled"* — compiled is not linked.

The Mi 4's PE methods (`msm8974_putc`/`getc`/`uart_init`, the QTimer timebase, the GIC
`msm8974_interrupt_init`/`handle_interrupt`) were absent from the final ELF even though the msm8974
**I/O Kit classes** were present (from `xnu_platform_obj_d13` via `STAGE90_PSEUDO_INITS`). That one
missing object was the stop.

## 3. The edit (a build-tool change; no tree edit)

Four links, each a thing that if reverted re-arms the generating stub **silently** (the record would
be unchanged):

1. **`tools/build_xnu_arm_kernel.sh`** — a `PLATFORM_SOC_SOURCES` loop compiles
   `src/platform/darwin13/pe_msm8974.c` with `-DBOARD_CONFIG_MSM8974=1` (the file's own `#if` gate)
   into `$XNU_PLATFORM_OBJ_OUT/pe_msm8974.o`. (The loop uses `PL_INCLUDES`, not `INCLUDES`, which
   carries `OPTION_FIRST_PLACEHOLDER`/`COMP_FIRST_PLACEHOLDER` sentinels.)
2. **`src/entry/build_entry.sh`** — declares `STAGE90_PE_MSM8974_OBJ`
   (default `$XNU_PLATFORM_OBJ_OUT/pe_msm8974.o`), `require`s it, freshness-checks it, and **names it
   in `LINK_OBJS`**. The link is the whole point: a `require` alone proves the file exists, not that
   it is handed to the linker — the 914 failure exactly.
3. **a linked-image clause** after the `xnu_entry_888` door clause — reads
   `PE_init_SocSupport_stub` **by value** (bounds via `nm -S -n`, disassembly via `objdump`),
   refuses the generated stub (`grep -qE 'bl.*<entry_stub_hit>'`), requires `bl.*<PE_early_puts>`,
   and requires `PE_init_SocSupport_msm8974` is `T`.
4. **`tools/check_board_pe_wired.sh`** (new) — re-derives all four links from the **sources** and
   refuses drift; `--selftest` mutates each of the five (compile gate, source gate, pool default,
   link entry, by-value test) and asserts each is refused. Wired into `make check`.

## 4. By-value verification

In the linked image `out/stage90/xnu_arm_entry.elf`:

```
801ae3ec T PE_init_SocSupport_stub       ; body: bl 8048bcd4 <PE_early_puts>   (the REAL board PE)
801ae2f0 T PE_init_SocSupport_msm8974    ; the MSM8974 init it runs
801ae290 T msm8974_putc                  ; the Mi 4 console method
8001ab60 T _disable_preemption           ; 972's fix, still present
```

The generated stub it replaced was at `0x8049bdc4`. The `xnu_entry_973` clause prints exactly this
from the built artifact (not from a comment).

## 5. What the press must show

- **`stub_hit` does not recur at `PE_init_SocSupport_stub`.** The next stop, if any, is a *further*
  missing symbol (one rung up the closure) or a real fault — recorded, not papered over.
- `PE_init_platform` runs **past** the SocSupport call — the Mi 4 PE methods (console/QTimer
  timebase/GIC) are now the ones `PE_init_platform` reaches.
- **Falsification:** if `stub_hit=PE_init_SocSupport_stub` reappears, the board PE was defined
  elsewhere (or the object was not the one linked) — record and re-derive.

## 6. Books — the arm, the manifest, and a stale-park correction

The first park (`armed-d13-c879f31a`, entry `c879f31a`, built 13:34) was made from a
`build_entry.sh` **`8901262e`** that the 973 edits then replaced at 13:48. `verify_press_ready`'s
"gate accepts this tree" row caught it — the source manifest bound `build_entry.sh 8901262e` while
the file on disk was `826f4982`. Rebuilding the entry image from the current tree (the gate's own
remedy, `src/entry/build_entry.sh` is byte-for-byte reproducible) produced a **different** binary
(`12427611`) whose `.text` differs from `c879f31a` by ~3.5 MB — the shape of a tree under a stale
generated-input read, not of a comment edit. Built twice from the current tree, byte-identical. The
arm is **`12427611`**; the stale `c879f31a` park was removed and `records/revert-set.txt` now names
the reproducible arm.

## 7. Scope / non-goals

- **The 3 GB** is untouched (958's report; 915-B's low-bank pmap port is separate).
- **The USB ladder** (959–962) is untouched.
- 973 is a **link**, not a switch: no `STAGE90_XNU_*` key moved, so the payload record `6c2b6038` is
  unchanged and the arm is named by the entry bin hash.
---

## 8. THE PRESS (2026-10-10) — 973 DID NOT RETURN

Pressed `armed-d13-12427611` (payload `stage90-qcdt.img 7d12735f…`, the gate's own bytes, unchanged
across the send) via `scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-d13-12427611`.
Gate exit 0; the run sent `fastboot boot` only, nothing flashed.

**The device did NOT come back.** The runner's bounded 180 s wait expired with:
```
adb:      serial 4a2fe00b not listed
host log: 34 -> 34 enumeration(s) of SerialNumber: 4a2fe00b
port 3-10: 34 -> 34 enumeration(s), any id
```
`lsusb` then showed no Mi 4 at all (only the host's own hubs/peripherals). No log was captured
(`/tmp/cancro-last_kmsg.txt` is gone; the previous run's `a02b66cf…` was parked to `.prev.60`).

**This is a BEHAVIOURAL CHANGE, and it is the run's whole result.** 972 (the same arm one rung
below) returned clean in ~17 s; 973 (the board PE now linked and running) did not return. The
board PE's body is now reached (`PE_init_platform` → the real `PE_init_SocSupport_msm8974` →
`msm8974_uart_init` / QTimer timebase / GIC `msm8974_interrupt_init` / `msm8974_putc`), so the stop
moved from a *missing symbol* (`stub_hit=PE_init_SocSupport_stub`) to a *hang or fault inside the
Mi 4 PE methods*. Which of the four is not yet known — the log is what would say, and the log is in
the top of DRAM (lost on a cold power transition).

**The device is dark, not bricked.** It needs an operator power press. Two routes:

0. **`scripts/recover_last_kmsg.sh` (the one command).** Boots the stock image non-persistently,
   waits for adb, reads `/proc/last_kmsg`, stores a dated capture, and prints the keys the open
   experiments wait on (`wdt_pets`, `xnu_entry_args_memSize`, the USB ladder). Nothing is written to
   storage; it is not a press. `--read-only` reads from an already-running Android.
1. **Recover the log by hand (preferred).** VolDown+Power → fastboot, then
   `fastboot boot xiaomi4-cancro-backup-20260604-112053/boot.img` (the STOCK Android image, never
   flashed), and in the Android shell `cat /proc/last_kmsg`. That returns the *previous* boot's RAM
   console — 973's. **Do NOT do a normal power-on first** (a cold boot clears the buffer), and do
   NOT boot TWRP (its kernel has no `/proc/last_kmsg` and its boot takes the slot the log is in).
2. **Plain recovery.** Hold Power ~10–15 s, release, press Power normally → back to Android. This
   loses 973's log.

**Next rung (once the log is read).** The board PE's four methods are the suspects; the log's last
key before silence names which (`msm8974_putc` output, the QTimer read, the GIC init, or a fault's
`far`/`fsr`). A build that links only *part* of the board PE (e.g. the console method alone) would
narrow it — but that is the next experiment, not this one.

### 8.1 A non-return is NOT automatically a wedge — check `wdt_pets`

**The project has seen this exact shape before and it was residency, not a hang.** 911's arm
(`21086959`, `IDLE_NO_SLEEP=1`) also did not return and the log was lost;
`[[mi4-911-resident-nonreturn-is-not-a-wedge]]` records the resolution: **`wdt_pets > 0` means the
kernel is resident** (it entered the idle loop and is staying in XNU) — which is the goal's own
"保持在 xnu 里" state — not a fault. So the 973 non-return has **two readings the log distinguishes,
and the build cannot**:

- **residency** (`wdt_pets > 0`): the boot advanced PAST the last missing symbol
  (`PE_init_SocSupport_stub`, now the real board PE) far enough to enter the idle/resident path — i.e.
  **progress, possibly a first**, not a defect. The 972 record already carries
  `STAGE90_XNU_RESIDENT=1`; a run that reaches the resident path does not return by design.
- **a hang/fault** (`wdt_pets == 0`, or a fault's `far`/`fsr`): the board PE (or what follows it)
  blocks. That would be the defect the narrow-the-PE rung is for.

**Which one it is is the recovered log's first question, and it is not answerable from the host** —
the entry image's controller `PE_init_SocSupport_stub` is a plain table-fill with no blocking call
(read line by line), so a hang would be *after* it, in the generic PE or the boot tail; a residency
would be the run reaching the idle path. The log (§8 recovery) settles it.

### 8.2 Why 972 returned and 973 did not — the terminal epilogue (read from the source, 2026-10-10)

The behavioral change is not just "the boot ran further." It is a change in *what stops the run*,
and the source says which. `entry_stub_hit` (`src/entry/entry_stubs.c:8061`) ends at `:8139` with

```c
entry_epilogue("a symbol this image does not provide was called");
```

and `entry_epilogue` is `__attribute__((noreturn, noinline))` (`:3565`): after writing the whole
`MI4IOS6_STAGE90_XNU real XNU entry …` report it **resets the run deliberately** —

```c
*(volatile uint32_t *)STAGE90_ENTRY_RESET_REASON_ADDR = STAGE90_ENTRY_RESET_REASON_NORMAL;  /* 0x0fa0065c */
*(volatile uint32_t *)STAGE90_ENTRY_PSHOLD_ADDR      = 0u;                                   /* 0xfc4ab000 */
for (;;) { asm volatile ("wfe"); }                                                           /* :4275–4282 */
```

So a boot that hits a missing symbol is *ended on purpose and returns*. That is exactly what 972's
capture shows: the report ends `stub_hit=PE_init_SocSupport_stub`, and the log's very next line is
the stock kernel's clean boot — **`No errors detected`** (`out/stage90/captures/972-press-armed-d13-0184b928-20261010-last_kmsg.txt`).

**973 removed that ending.** It linked the real board PE, so `PE_init_SocSupport` no longer reaches
the generated stub → `entry_stub_hit` never fires → `entry_epilogue` never runs → **nothing resets
the device and the boot continues into the kernel (the 507 record's "a run with nothing left to stop
it ends on the hardware watchdog instead of on the epilogue").** A continuing boot produces **no host
enumeration by construction** when the USB ladder is off — the same space as 911's resident
non-return ([[mi4-911-resident-nonreturn-is-not-a-wedge]]). So 973/974/975's darkness is the
*expected* consequence of retiring the last generated stub, **not by itself a regression**; whether
the continuing boot is resident (the goal's 「保持在 xnu 里」) or blocked is still only the recovered
log's `wdt_pets` that says — §8.1 stands, and this sharpens *why* the log is owed rather than guessed.

### 8.3 The board PE is non-blocking by value — no method on the boot path blocks (source, 2026-10-10)

§8.1 asserted the controller is "a plain table-fill with no blocking call (read line by line)." That is
now *checked*, method by method, in `src/platform/darwin13/pe_msm8974.c` (195 lines, the whole file):

- `PE_init_SocSupport_stub` → `PE_early_puts` (our sink) → `PE_init_SocSupport_msm8974`. The latter is
  **pointer stores into `gPESocDispatch`** (no call), plus **one** call: `msm8974_timebase_init()`.
- `msm8974_timebase_init` — `mrc p15,0,%0,c14,c0,0` (read CNTFRQ) + a conditional register write. **CP15
  c14, which the entry image already uses** (`src/entry/entry_timebase.h`); not a device load.
- `msm8974_uart_init` / `msm8974_interrupt_init` — **empty bodies**. `msm8974_getc` — `return -1`.
- `msm8974_handle_interrupt` is the only **device load** (`*gicc_iar`) but it is a **table pointer the
  stub only stores** — not called during `PE_init_SocSupport`. `msm8974_timer_enabled` is a no-op.
- `msm8974_putc` (thus every XNU `PE_kputc` char after line 151) → `entry_os_console_char`
  (`entry_stubs.c:2696`), which is **bounded**: before the live tables install it appends to a fixed
  `.bss` tank `g_os_tank[ENTRY_OS_TANK]` (overflow counted in `g_os_tank_dropped`); after, it is bounded
  by `g_os_end` with a one-time `xnu_live_ostext_limited` marker. No unbounded loop, no block.

**Consequence.** A non-return is therefore **not** a hang inside the board PE (or inside
`PE_init_platform`'s call to it) on this image. So the log's `wdt_pets` asks a *narrow* question: the
boot is either **resident** (past `PE_init_platform` into the scheduler/idle — the goal's 「保持在 xnu
里」) or **blocked later**, in the *generic* PE or the boot tail — never here. This also says 974 was the
right rung: the console fix is exactly what makes that reading recoverable.
