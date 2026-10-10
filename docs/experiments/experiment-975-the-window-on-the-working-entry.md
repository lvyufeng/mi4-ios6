# Experiment 975 — the 484 MiB window, on the entry line that actually boots

**Status:** 📍 **PRESSED 2026-10-10 — DID NOT RETURN (log not yet recovered).** The press was sent
(`fastboot boot`, image `1091566c…` unchanged across the send) and the runner's 180 s bounded wait
expired with **no adb entry and no new USB enumeration** — the same shape as 973. **975 is a resident
rung** (`POST_END_TICKS=0`), so a non-return is *expected* if the boot reached the idle/resident path;
**only the RAM console distinguishes residency (`wdt_pets > 0`) from a fault**, and it lives in the top
of DRAM. Recovery is owed (stock `boot.img` + `cat /proc/last_kmsg`, NOT a cold boot, NOT TWRP) — see §6.

969 named the wall: the real iOS 7.1.2
`/sbin/launchd` links `libSystem`/`libbsm` out of the **301 MiB** dyld shared cache, while XNU managed
only the **16 MiB** window — so it concluded iOS could not run without the whole-kernel 915-B pmap port.
970 designed the one-switch escape (`STAGE90_XNU_ENTRY_WINDOW=0x1e400000`, 484 MiB) and pressed it — but
**only on entry bin `7107b998`, which never booted** (970 §6c: that arm and its 16 MiB control failed
*identically*). So the window was never tested on a working entry. **975 is that missing measurement:**
the same window, on the entry line that boots (971) and whose console is now observable (974).
Entry bin `73475747` **unchanged**; payload `stage90-qcdt.img 1091566c…` (9351168 B); payload switch
record `6c2b6038` unchanged. `make check` 0. **THE PRESS IS THE OPERATOR'S.**

Continues `experiment-970-the-window-is-ram-not-just-address.md` (§6c left this measurement owed),
`experiment-974-the-board-pe-console-swallowed-the-boot.md`, and `experiment-969-the-16mb-window-is-the-wall.md`.

---

## 0. The arm

`armed-window-1091566c` is 974 (`armed-d13-73475747`) with **one** key changed:

| switch | 974 | 975 |
|---|---|---|
| `STAGE90_XNU_ENTRY_WINDOW` | `0x01000000` (16 MiB) | **`0x1e400000` (484 MiB)** |

Everything else is byte-for-byte 974. The window reaches XNU **only** through the payload's generated
header: `build_entry.sh` substitutes `@ENTRY_SIZE@` into `xnu_arm_entry.h` (`ENTRY_WINDOW_REQ` →
`ENTRY_SIZE`), and `src/xnu_entry_jump.c:150` reads it as `a->memSize = STAGE90_XNU_ENTRY_SIZE`. No
entry *source* reads it, so the **entry bin is unchanged** and the name comes from the qcdt — the
`armed-window-*` family 970 §4 established. `stage90-build-config.txt` is `6c2b6038`, identical to
968/970h/971/972/973/974, because the window is an *entry* build parameter the payload's own record
cannot see.

## 1. Why this rung exists, and why it was never run

970 §3 gave the arithmetic (the console is the ceiling):

- D13's managed map is **fixed-base** — `l2_cache_to_range(managedCachePA, MANAGED_BASE = 0xC0000000,
  TRUE, gMemSize)` spans `[0xC0000000, 0xC0000000 + gMemSize)`, **no clamp** (4570 clamps; D13 does not).
- The entry's RAM console lives at VA `0xde500000`, installed into XNU's live L1 by
  `entry_section_install`, which **refuses an occupied slot** (`(before & LIVE_TTE_TYPE_MASK) != 0`).
  A window that *reaches* the console occupies that slot → the install is refused → **silent boot, no
  log at all**.
- Ceiling = `RAM_CONSOLE_BASE − MANAGED_BASE = 0xde500000 − 0xC0000000 = 0x1e500000` (**485 MiB**); the
  safe max is `0x1e400000` (484 MiB), one 16 MiB step below, with the console and every MMIO window
  above it (GIC `0xf9000000`, USB `0xf9a55000`, WDT, SMCC, GCC, TLMM, SMEM `0xe0000000`) left intact.
- Free after `topOfKernelData 0x80A00000`: `0x9E400000 − 0x80A00000` = **~470 MiB** — enough for the
  301 MiB shared cache *plus* the rest of iOS userspace, the number 969 said needed 915-B.

`build_entry.sh` already **refuses** any D13 window `>= 0x1e500000` (970 §3's guard). So the arm is a
build parameter, not new code.

**Why it was never run.** 970 §6c: the 484 MiB arm and its 16 MiB control both failed on entry bin
`7107b998`, and that failure travelled with the bin — no D13 entry had ever booted. The bin's cause was
found later (970g/970h) and fixed; 971 confirmed the boot crosses `set_mmu_ttb`; 973 linked the board PE;
974 made the board PE's console observable. **The window is the one variable of 970's design that was
never re-measured on the now-working line.**

## 2. The build (host-side, press-free)

1. Entry image rebuilt from 974's own record (`out/stage90/xnu_arm_entry-config.txt`, `(unset)` markers
   skipped) + `XNU_TREE=<d13>` + `STAGE90_XNU_ENTRY_WINDOW=0x1e400000` + `STAGE90_ENTRY_ARM_CHANGE=1`.
   **The entry bin is byte-identical to 974's** (`73475747`); only the generated
   `xnu_arm_entry.h` / `-config.txt` moved. This is 970 §4's prediction, confirmed by value.
2. Payload rebuilt (`XNU_TREE=… STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh`), which
   embeds the bin and recompiles `xnu_entry_jump.o` against the new header.

## 3. By-value verification

- The generated header: `out/stage90/xnu_arm_entry.h` → `#define STAGE90_XNU_ENTRY_SIZE 0x1e400000`.
- The linked payload: `out/stage90/stage90.elf` carries **exactly one** `0xe3a08579`
  (`mov r8, #0x1e400000`) and zero occurrences of the old 16 MiB immediate.
- The entry record: `STAGE90_XNU_ENTRY_WINDOW=0x1e400000`; the entry bin hash is still `73475747`.
- `make check` 0; `verify_press_ready` 4/5 (the 5th row is the physical device, which is dark).

## 4. What the press decides

**If the premise holds**, real iOS userspace (launchd, then SpringBoard) maps its shared cache and iOS
**RUNS — with no 915-B port**. The runner already has a window block that reads the value from the log
(`check_runner_window_family`):

- `xnu_entry_args_memSize = 0x1e400000` — XNU was handed 484 MiB (the rung ran).
- `BSD root:` naming the card's HFSX volume; the COW block; then **launchd past its `__TEXT`** and a
  userspace that does not die on an unmapped owned page.
- **Falsification:** XNU panics early (the 484 MiB `_start` map faults) → the window's *size* was not
  the wall, and 915-B is back on the table. Either way this press is the cheap next measurement.

**This is a resident rung** (`POST_END_TICKS=0`): budget a black screen + a power-cycle capture, not a
clean return. **But 974 makes the console observable *if 975 does not return*** — the board PE's
`uart_putc` text now lands in the same captured RAM console, and the arm also carries the full USB
ladder (974's record), so the console ring is handed to EP1-IN.

## 5. Scope / non-goals

- **No 915-B pmap port.** 975 tests whether the window *alone* removes the wall; if it does, the
  whole-kernel port is not needed for SpringBoard. If it does not, 915-B stands.
- **The 3 GB clause** is untouched (958 reports it; the low bank's *allocatability* is 915-B).
- **The medium is unchanged** — 968's real iOS 7.1.2 HFSX rootfs over `hfs_rmd0`; `CARD_COW=1`,
  `HDD_WRITE=0`, the base is never written — fully reversible. `fastboot boot` only, never flash.

## 6. THE PRESS (2026-10-10) — 975 DID NOT RETURN

Pressed `armed-window-1091566c` via `scripts/press_975.sh` → gate rc=0 → runner
`--expect-arm=armed-window-1091566c`. The card head was written to `userdata`
(`mmcblk0p25`, first 8 MiB read-back `9bcef3ff…` = the built image) and the runner sent
`fastboot boot` only (image `1091566c…`, unchanged across the send; nothing flashed).

**The device did NOT come back.** The runner's 180 s bounded wait expired with:
```
adb:      serial 4a2fe00b not listed
host log: 38 -> 38 enumeration(s) of SerialNumber: 4a2fe00b
port 3-10: 38 -> 38 enumeration(s), any id
```
`lsusb` then showed no phone at all. **Same shape as 973** — a resident-rung non-return.

**This is NOT yet a verdict.** 975's record carries `POST_END_TICKS=0` (`STAGE90_XNU_RESIDENT=1`), so a
non-return is the *expected* form if the boot reached the idle/resident path — the goal's 「保持在 xnu
里」 — not necessarily a hang ([[mi4-911-resident-nonreturn-is-not-a-wedge]]). **The RAM console
distinguishes them (`wdt_pets > 0` = resident), and it is in the top of DRAM.** 974 makes that console
observable *if the recovery reads the buffer*.

**And the non-return is now *expected by construction*, not merely possible** (973 §8.2, read from the
source 2026-10-10): `entry_stub_hit` ends in the `noreturn` `entry_epilogue`, which **resets the run on
purpose** (PS_HOLD/`RESET_REASON_NORMAL` + `wfe` loop, `entry_stubs.c:4275–4282`). 972 returned clean
precisely *because* it hit a missing symbol (`stub_hit=PE_init_SocSupport_stub` → epilogue → reset).
973 linked the real board PE, retiring that last generated stub, so the epilogue no longer runs and the
boot continues into the kernel — where, with the USB ladder off, **nothing enumerates**. So darkness is
what a boot past the diagnostic endpoint looks like; the log's `wdt_pets` alone divides residency from a
hang.

**Recovery owed (operator, at the device):** `scripts/recover_last_kmsg.sh` — the one command. It
reads `/proc/last_kmsg` (the *previous* boot's RAM console = 975's), stores a dated capture, and prints
the keys this experiment waits on. Equivalent by hand: get back to the **installed ROM** (a normal
power-on, or `fastboot reboot`) and `cat /proc/last_kmsg`.

**⚠️ CARRIER CORRECTED 2026-10-10 (measured).** This section previously prescribed
`fastboot boot xiaomi4-cancro-backup-20260604-112053/boot.img`. That is wrong: measured on the device,
`/proc/last_kmsg` is exposed by the **installed ROM** after a plain reboot (a full ~2 MB = the previous
boot), while `fastboot boot <golden>` exposes **no** `/proc/last_kmsg` — it installs a different kernel
*and* burns the single-slot record. The console the payload left is read by the **very next** boot; every
boot after that overwrites the slot. **Recovery was attempted this session** (the operator had the device
in fastboot): a non-persistent `fastboot boot <golden>` returned *no* `/proc/last_kmsg` (the carrier had
been confused, and the fastboot-boot itself burned the slot); a plain `adb reboot` then exposed
`/proc/last_kmsg`, but by then **975's console had been overwritten** by earlier boots (the 2 MB capture
held 0 `MI4IOS6` / 0 `xnu_live` lines — it was the stock Android kernel's own previous boot). So 975's
log is **gone**: too many boots elapsed for the single-slot ring. The lesson is the carrier and the
one-boot window, now fixed in the script.

**Next rung (the log is gone; the escape is the ladder, not recovery).** `xnu_entry_args_memSize=0x1e400000`
would prove the window rung ran; `BSD root:`/launchd-past-`__TEXT` decides the wall; a fault's `far`/`fsr`
or the last key before silence names a hang. None of that is readable now — 975's console is lost (§6).

**Why this recurs, structurally (read from `build_entry.sh`, 2026-10-10):** a **resident** arm has **no
ending by construction**. `build_entry.sh:1094-1103` **refuses** `STAGE90_XNU_POST_END_RUN>0` and
`STAGE90_XNU_POST_END_TICKS>0` whenever `STAGE90_XNU_RESIDENT=1` ("POST_END_TICKS is the deliberate ending
still in the image, so the run would end at the countdown… Set POST_END_TICKS=0"). So every resident arm —
which is every arm that reaches the goal's 「保持在 xnu 里」 — ends only on the hardware watchdog, which
does **not** preserve the ram console record the way a clean partial-boot (epilogue or panic) does. Its log
therefore survives at most the **one** boot after it, and any boot after that overwrites the single-slot
record. **975 lost its log this way** (the ~2 MB `/proc/last_kmsg` this session held 0 `MI4IOS6` lines).

**So the escape is not another recovery — it is to stop needing one.** 975's record already carries the
full USB ladder (`STAGE90_XNU_USB_PROBE/DEV/ENUM/STREAM=1`), which fires from `__wrap_machine_idle` (951):
if it reaches the idle path, it **streams the console to the host live**, and the host sees it *while* the
device stays resident — no RAM console, no recovery window. But the ladder (959–962) is **PARKED, never
pressed** (`experiment-962…:42`), so 975's "no new USB enumeration" cannot be read: it is equally consistent
with (a) the boot faulting **before** the idle path, or (b) the ladder code running but being buggy.

**⟹ The next rung is the USB ladder itself (959–962), pressed alone.** Validate that the D13 resident path
streams its console to EP1-IN; once it does, a resident run is observable live and the "non-returning run
loses its log" problem is closed for good — which is also the goal's 「可以通过 usb 进行调试」. Only after
that does a 975 re-press carry a readable outcome. **PRESS IS THE OPERATOR'S.**

**§6a — VERIFIED 2026-10-10: the ladder IS in 975's image, at a live site. The next press is decisive.**
Disassembled the built `out/stage90/xnu_arm_entry.elf` by value: `__wrap_machine_idle` (951's D13 site)
carries all four calls contiguously — `bl entry_usb_probe` / `entry_usb_dev_init` / `entry_usb_enum_poll`
/ `entry_usb_stream_poll` (`8049c8b8+0x60…0x6c`), tail-`b machine_idle` — and the four symbols are in the
image (`entry_usb_stream_poll T 0x80015388`). So the ladder is **not** a dead site and **not** compiled
out on D13: on any arm that *reaches* the idle path it runs on every pass. This retires reading (c) from
§6's dichotomy — "the ladder code is absent" is false. An **empty** capture on the next press therefore
means EITHER (a) the run faulted before the idle path, OR (b) the ladder reaches the idle path and is
**buggy**; the two are divided by 974's board-PE console, which now lands in the same RAM console and is
handed to EP1-IN. A **non-empty** capture proves (b) false and closes the live-read problem outright.

**§6b — ONE PRESS OF THIS ARM ANSWERS FOUR GOAL CLAUSES (from the arm's own record, 2026-10-10).**
`out/stage90/xnu_arm_entry-config.txt` for `armed-window-1091566c` carries, together:
`STAGE90_XNU_MEM_TOTAL=1` (958 — 「正确识别 3 GB」), `STAGE90_XNU_CARD_TOTAL=1` (911d — 「16/32 GB」),
the four `STAGE90_XNU_USB_{PROBE,DEV,ENUM,STREAM}=1` (959–962 — 「可以通过 usb 进行调试」), and
`STAGE90_XNU_RESIDENT=1` (「保持在 xnu 里」), on the real iOS 7.1.2 HFSX rootfs (`CARD_COW=1`,
`HDD_WRITE=0`, base never written) at the 484 MiB window. So the single owed press is not one measurement
— it is the goal's clause-2/3/5a/5b observation AND the window/wall decision at once. Nothing about the
arm is missing; only the press is.


## 7. Provenance

*Provenance: 970 (design + §3 ceiling + §6c/§6d), 969 (the 301 MiB measurement), 971/973/974 (the
working entry line) read this session; the entry image and payload built and verified by value in
`out/`; `make check` 0 and `verify_press_ready` run this session; the press sent 2026-10-10 (device did
not return; log owed). Device unmodified by the host. Follows
[[mi4-970-the-window-is-ram]], [[mi4-969-the-16mb-window-is-the-wall]], [[mi4-974-board-pe-console-sink]].*