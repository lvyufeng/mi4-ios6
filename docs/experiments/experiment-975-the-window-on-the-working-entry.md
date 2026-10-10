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

**Recovery owed (operator, at the device):** Vol-Down+Power → fastboot →
`fastboot boot xiaomi4-cancro-backup-20260604-112053/boot.img` (the STOCK image, never flashed) →
`cat /proc/last_kmsg`. This returns the *previous* boot's RAM console = 975's. A cold boot clears it;
TWRP has no `/proc/last_kmsg` and its own boot takes the slot. The recovery was attempted this session
but the device never re-enumerated (no fastboot, no adb, no USB), so the log is still owed.

**Next rung (once the log is read):** `xnu_entry_args_memSize=0x1e400000` proves the window rung ran;
`BSD root:`/launchd-past-`__TEXT` decides the wall; a fault's `far`/`fsr` or the last key before silence
names a hang. If the log is empty even after recovery, the non-return is *very* early (before the first
`xnu_live_*` probe) — the 484 MiB `_start` map would be the suspect, i.e. 915-B stands.

## 7. Provenance

*Provenance: 970 (design + §3 ceiling + §6c/§6d), 969 (the 301 MiB measurement), 971/973/974 (the
working entry line) read this session; the entry image and payload built and verified by value in
`out/`; `make check` 0 and `verify_press_ready` run this session; the press sent 2026-10-10 (device did
not return; log owed). Device unmodified by the host. Follows
[[mi4-970-the-window-is-ram]], [[mi4-969-the-16mb-window-is-the-wall]], [[mi4-974-board-pe-console-sink]].*