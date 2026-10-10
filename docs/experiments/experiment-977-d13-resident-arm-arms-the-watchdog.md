# Experiment 977 — the D13 resident arm arms the watchdog and cannot pet it

**Status:** ✅ **ROOT CAUSE ESTABLISHED AND FIXED (2026-10-10).** The fix is built and parked as
`armed-d13-d693864f` (`scripts/park_977.sh`), press-ready (`verify_press_ready` 4/5, the 5th the dark
device). The D13 entry line **cannot**
pet the SoC watchdog, because the resident signature the whole **cancro bring-up is built on** —
`arm_vm_init.c` ends → the boot spins in **`machine_idle`** (`osfmk/kern/sched_prim.c:4532`) — is only
true for the **bounded** build; a **resident** D13 build carries **no pet at all**, while the payload
arms the watchdog ~28 s before the jump. A resident rung therefore resets **mid-boot**, and its only
possible live channel (the 959–962 USB stream) has not started. This is a **design gap inside the
resident arm**, recorded by `test_resident_guard.py:62-71`'s deliberate D13 skip but never measured as
the observability trap 975's empty capture demonstrates.

Follows [[mi4-975-window-on-the-working-entry]] §6d (this session's press) and 960/961/962.

---

## 1. What was pressed, and what it showed

`armed-window-1091566c` (entry bin `73475747`, the working D13 entry line) was pressed 2026-10-10:
`RUN_RC=2` (*the payload ran and the device did NOT come back*), **USB console capture EMPTY**
(`idProduct=0910` 0 times in the host dmesg). The empty capture cannot be read on its own — §6a retired
"the ladder code is absent", so it is either (a) the run faulted before the idle path or (b) the ladder
reached it and is buggy. This experiment establishes a **third** reading that dominates both: the run
**reset** before a device ever started.

## 2. The chain, each link checked by value

1. **The consumer's signature (memory: 507).** The D13 line was accepted because it "ends on the
   watchdog instead of `entry_epilogue`" — i.e. the **bounded** boot survives into XNU and spins in
   `machine_idle`, where 951 places the USB ladder. That is true **only for a build with an ending**.
2. **A resident build has no ending.** `src/entry/build_entry.sh:1094-1130` REFUSES `POST_END_TICKS`,
   `POST_END_RUN`, `SEAM_END_RUN`, `HW_WATCHDOG=0`, and `ENTRY_TRACE=0` when `STAGE90_XNU_RESIDENT=1`.
   Its own comment (`:1114`): *"Residence removes the image's own ending, so the armed SoC watchdog is
   the ONLY net left; **the pet this arm adds resets the countdown**."*
3. **On D13 there is no pet.** `entry_trace.c:2241` declares `entry_wdt_pet` under
   `#if STAGE90_XNU_RESIDENT && !STAGE90_ENTRY_D13`; both call sites (`:2380`, `:3030`) sit inside the
   **`#if !STAGE90_ENTRY_D13`** idle block (937's "513-535 is 4570-only" gate, opened `:2251`, closed
   `:3475`); the pet's **definition** (`:2701-2866`) is inside that same gate. So a D13 resident image
   links **no** `entry_wdt_pet`.
   - **Measured:** `arm-none-eabi-objdump -t out/stage90/xnu_arm_entry.elf` has **no**
     `entry_wdt_pet`/`wdt_pet`/`wdt_` symbol; the only `wdt`/`watchdog` symbols are Mach's
     `mbuf_watchdog`/`mb_watchdog`/`IOWatchDogTimer` — not ours.
4. **The payload arms the watchdog ~28 s before the jump.** `stage90_main.c:1247`
   `stage90_hw_watchdog_arm(25)`; `hw_watchdog.c:230` writes `WDT0_EN=1`, bark 25 s, bite +3 s. It is
   never disarmed (`stage90.h:4110`).
5. **D13 XNU never pets the SoC WDT.** D13 ships no MSM watchdog driver — `find external/xnu-hd2-darwin13
   -iname '*watchdog*'` returns only `IOWatchDogTimer` (a Mach software timer letting kernel
   extensions watch the *kernel*; it does **not** store to `0xf9017004`). 4570 is the same. So nothing
   in the handed-off kernel feeds `WDT0`.

**Conclusion.** Bounded D13 arm: payload arms WDT → D13 XNU boots → idles in `machine_idle` → resets in
~28 s → **`fastboot boot` does not persist → next power-on is Android** → `/proc/last_kmsg` readable →
the log is captured (the working 507 line). Resident D13 arm: identical, **except** the boot reaches the
idle spin with **no pet**, so between ~28 s and the ladder's first pass the watchdog **resets the SoC** —
and because the ladder had not started, the USB stream is **empty**. This is the empty capture.

## 3. Why this was invisible

- `scripts/recover_last_kmsg.sh` assumes a **reboot** returns the device (resident → not a wedge). It
  does **not**: `fastboot boot` does not persist, so the reset lands to a **powered-off** phone, not to
  Android.
- `tools/test_resident_guard.py` `configure()` (`:62-71`) **skips D13** deliberately, "because the pet
  is compiled out" — it publishes the skip but does not name the consequence (no net on a tree whose
  resident run is dark by construction).
- [[mi4-goal-is-press-gated]] reads a non-return as the *expected* form of "`保持在 xnu 里`" and defers
  to the RAM console — which a reset-and-power-off does not leave.

## 4. The fix (BUILT, PARKED `armed-d13-d693864f`, 2026-10-10)

**Give the D13 resident arm a pet at the one D13-live site.** The pet body is already
tree-independent: it touches only `entry_mmio_section`, `entry_live_write`, `entry_live_ready`, and
`STAGE90_WDT_*`, none of which is 4570-only. Steps 1–3 below are the fix that landed (commit
`de6d9f1`); step 4's build-side refusal is `tools/test_resident_guard.py` run from
`build_entry.sh:38003` for any `RESIDENT=1` image (the preflight half is the peer lane's file, not
touched here). **Verified by value:** `arm-none-eabi-nm` shows `entry_wdt_pet` defined and `objdump`
shows exactly one `bl … <entry_wdt_pet>` inside `__wrap_machine_idle`; the rebuilt payload embeds
entry blob `d693864f`; `stage90.elf` still carries one `e3a08579` (`mov r8,#0x1e400000`); the
resident guard and its `--selftest` both exit 0; `make check` 0; `verify_press_ready` 4/5. The entry
bin moved `73475747 → d693864f` (the pet is now linked), so the arm names from
`xnu_arm_entry.bin` — an `armed-d13-*` set, not a payload-only `armed-window-*` one.

1. **`entry_trace.c`** — move the trace of `entry_wdt_pet` (the definitions of `STAGE90_WDT_*`, the
   `g_wdt_*` statics, and the function body, `:2701-2866`) **out of** the `#if !STAGE90_ENTRY_D13`
   block so it is gated by `#if STAGE90_XNU_RESIDENT` alone; make the **forward declaration**
   (`:2241`) `#if STAGE90_XNU_RESIDENT` (drop `&& !STAGE90_ENTRY_D13`). Keep the two 4570 call sites
   where they are.
2. **Add the D13 call site** inside `__wrap_machine_idle`'s `#if STAGE90_ENTRY_D13` block (`:2113-2169`),
   after the probes:
   ```c
   #if STAGE90_XNU_RESIDENT
       entry_wdt_pet(entry_counter());
   #endif
   ```
   `entry_counter()` is the wrapper's own pass counter (the wrapper's 8-byte frame
   lives in the `slot`, so nothing may keep state across the `bl`; a call at the tail is safe, the
   951 probes already prove it).
3. **`tools/test_resident_guard.py`** — un-skip D13 for the pet-*presence* half: a `RESIDENT=1` D13 image
   **must** carry `entry_wdt_pet` and call it from `__wrap_machine_idle` once. Keep the 4570-only
   frame/clause checks skipped for D13.
4. **Guards:** in `src/entry/build_entry.sh` **and** `scripts/preflight_boot_check.sh`, refuse an arm
   with `STAGE90_XNU_RESIDENT=1` whose linked image carries no `entry_wdt_pet` called from a live site —
   a build/check refusal, not a comment ([[mi4-a-claim-in-a-comment-is-not-a-check]]). The failed-height
   symptom (empty USB stream + no return + the next power-on being Android) is exactly what 975 §6d
   records.

**Not in scope here:** the resident arm's RAM console still dies with the reset; the pet makes the run
**live** (the ladder's index-17 enumeration on `USBMODE=2`), not **recoverable** once it ends. A
recoverable resident log needs the stream (959–962) or a channel that survives a reset.

*Provenance: `src/entry/entry_trace.c` (`:2113-2169`, `:2241`, `:2251`, `:2380`, `:2701-2866`, `:3030`,
`:3475`), `src/entry/build_entry.sh:1094-1130`, `src/stage90_main.c:1247`, `src/hw_watchdog.c:184-233`,
`src/stage90.h:4110`, `tools/test_resident_guard.py:62-71` read by value this session; the built
`out/stage90/xnu_arm_entry.elf` disassembled with `arm-none-eabi-objdump`; the reference tree
`external/xnu-hd2-darwin13/xnu` searched for a watchdog driver. Follows
[[mi4-975-window-on-the-working-entry]], [[mi4-goal-is-press-gated]], [[mi4-xnu-reboot-path-cannot-reset]].*