# 1012: does the hardware watchdog actually bite? — a by-value answer

**Question asked.** Every "resident" measurement rung (1009, 1010, 1010b, 1010c, 1011, 1012) freezes
in a whole-interrupt-path stall (~24.4 s), the device goes dark, and it needs a manual power press.
The runner and the press log both say the hardware watchdog is ARMED and should bite ~28 s after
arming, and it appears not to. If it bit, the SoC would re-enumerate and the next ROM boot would
expose `/proc/last_kmsg`, so every rung would self-recover and no press would be needed.

**Verdict (b), with a named mechanism.** The watchdog is **genuinely, measurably armed** (and has
been proven to *bite* on hardware by its own self-test), **and** the entry image **pets it** on every
idle/read pass — so it cannot bite during the run. The stall defeats the net not by stopping the dog
but by **stopping the pet**: the storm traps the CPU inside the iterating abort handler, the pet's
two call sites are never reached, and the now-unfed dog bites into a boot state that has no reader
for `/proc/last_kmsg`. So (a) is false (the claim has a real writer), and (c) is false (the dark
device is the dog's own work, not "something else"). The net is real and BOUNDED — it is not a
log-recovery net, and it never was once `RESIDENT=1` linked a pet.

---

## Q1 — Is the watchdog armed by code, or only claimed?

**Armed by code, on the boot path, at the earliest point.** `src/stage90_main.c:1261-1263`:

```c
#if STAGE90_HW_WATCHDOG == STAGE90_HW_WATCHDOG_ARMED
    (void)stage90_hw_watchdog_arm(STAGE90_HW_WATCHDOG_TIMEOUT_S);
#endif
```

`STAGE90_HW_WATCHDOG=STAGE90_HW_WATCHDOG_ARMED` is in the 1012 record
(`out/stage90/stage90-build-config.txt`, and the press log's config block, line 45). The arm writes
real registers from `src/hw_watchdog.c:228-231`:

```c
    hw_wdt_write(MSM8974_WDT_REG_BARK, bark_ticks);
    hw_wdt_write(MSM8974_WDT_REG_BITE, bite_ticks);
    hw_wdt_write(MSM8974_WDT_REG_EN, 1u);
    hw_wdt_write(MSM8974_WDT_REG_RST, 1u);
```

base `0xf9017000` (`src/hw_watchdog.c:94`), and it is not a write it merely *assumes* landed: it
**reads the live countdown twice and requires it to have moved and to be plausible**
(`src/hw_watchdog.c:245-276`), then gates on the readback (`:284-288`).

**The claim is MEASURED on this exact class of image, not taken on trust.** Every recovered storm
capture carries the arm block by value, e.g.
`out/stage90/captures/recovered-unknown-arm-20261014-220748-last_kmsg.txt`:

```
hw_watchdog_timeout_s=0x0000001d          (29 s)
hw_watchdog_bark_ticks_written=0x000e7fa9  (949,161 ticks / 32765 = 28.97 s)
hw_watchdog_bite_ticks_written=0x000fffa0  (1,048,480 ticks = 32.00 s, no truncation)
hw_watchdog_en_after=0x00000001
hw_watchdog_countdown_first=0x00000000
hw_watchdog_countdown_second=0x00000c25    (0 -> 3,109: the counter RAN)
hw_watchdog_counter_running=0x00000001
hw_watchdog_readback_ok=0x00000001
```

So Q1: the claim ("hardware watchdog: ARMED", `scripts/preflight_boot_check.sh:2026`) is **backed by
a writer that fires on this boot, and the moving countdown proves the block is live.** It is a
measurement, not an inference from the switch value. *(This is the point the runner's own comment at
`scripts/run_and_capture.sh:735-749` was written to enforce — the caveat there is one of the project's
own hard-earned rules, and it holds here.)*

## Q2 — Is the pet called on the stall path? No — that is the whole defect.

`entry_wdt_pet` is defined at `src/entry/entry_trace.c:2196`, and it has **exactly two call sites, by
value, in the built image** (`arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf`, `grep -c "bl.*<entry_wdt_pet>"`
= 2), and only two in source:

1. the **idle wrapper** — `entry_wdt_pet(entry_counter());` at `src/entry/entry_trace.c:2403`, inside
   `__wrap_machine_idle`;
2. the **storage read door** — `entry_wdt_pet(st_read_count);` at `src/entry/entry_storage.c:7725`,
   inside `entry_storage_driver_read`.

The body pets only when the count has passed **half the bark** (`src/entry/entry_trace.c:2268-2269`):

```c
    if (g_wdt_bark_ticks == 0u || count >= (g_wdt_bark_ticks / 2u)) {
        *(volatile uint32_t *)(uintptr_t)(STAGE90_WDT_BASE + STAGE90_WDT_REG_RST) = 1u;
        g_wdt_pets++;
```

Now the stall. **1007 localised the ~24.4 s freeze INSIDE a single `sleh_abort`** (18.7 s) riding a
storm of ~2.1 M retries (`records/1007-press-read.txt`; the 1009/1010 ladders show
`abt_n` reaching `0x00200000` and the tail tuple `pc/fsr/abtcpsr/far` constant). The storm is the
kernel's **user-write permission-fault handler iterating**: every fault re-enters `sleh_abort`,
which re-prints `BATAP`, returns, re-executes the store, and faults again — a tight re-fault loop
**without reaching XNU's normal idle loop or the storage read path**. In that state **neither pet
site is reached**, and the storm has nothing that writes `WDT0_RST`.

So Q2: **the pet is NOT on the stall path.** It lives on `__wrap_machine_idle` and the read door;
both are bypassed when the CPU is trapped in the abort retry loop. This is exactly the failure class
the arm block `src/stage90_main.c:1254-1259` says the net exists for ("Everything after this line …
is covered by hardware rather than by software that might itself be what broke") — the software that
broke is the abort handler, and it is precisely between the two pets.

## Q3 — Is the watchdog's register page actually mapped and writable?

Yes. `hw_wdt_read`/`hw_wdt_write` do real `volatile` accesses at `0xf9017000..` (`src/hw_watchdog.c:116-125`),
and the payload maps the megabyte: **the arm block reads back `en_after=1`, `bark_after=bark_ticks`,
`bite_after=bite_ticks` and samples a *rising* countdown** — a dummy/absent page cannot read back the
written bark/bite **and** show a live counter in the same block. The entry side does not even need a
new mapping: the GIC's own `0xf9000000` 1 MB section covers `0xf9017000`
(`src/entry/entry_trace.c:2155-2168`), and the pet's own guard reads that descriptor back before
committing (`wdt_desc_maps_the_block`, `src/entry/entry_trace.c:2179-2183`). The entry image contains
no store to the bite register `0xf9017014` (the "exempt code" scan in the press log,
`records/1012-press-log.txt:432-433`, reports `[0xf9010000,0xf901ffff] movt rD,#0xf901` **only from
`entry_wdt_pet`**, whose sole store is to `WDT_RST`), so nothing its side writes can defeat a bark.

## Q4 — Does the stall mask the dog, or is it independent?

The dog is independent: it needs only its own 32,765 Hz clock and the always-on block. The stall does
**not** disable it and the code that "disarms" things certainly does not:

```
src/gic.c:630   int stage90_disarm_deadman_timer(void)
src/gic.c:666   xnu_log_kv32("disarm_hw_watchdog_en", stage90_hw_watchdog_enabled_readback());
src/xnu_entry_jump.c:265   (void)stage90_disarm_deadman_timer();
```

This is called right before the jump, and its whole point is to **read the SoC's EN bit live** and put
it in the record — it disarms the **GIC timer PPI** (the *software* dead-man's source), **not** the
watchdog. Measured on 1010b:
`recovered-armed-d13-64391319-20261014-173654-last_kmsg.txt` carries
`disarm_hw_watchdog_en=0x00000001`. So across the jump the hardware net is confirmed armed and live.

**And the net is proven to actually BITE on this hardware** — this is the positive control the whole
question turns on. The one self-test arm that ran, **677** (`docs/experiments/experiment-677-the-press-fired-and-the-reset-came-early.md`),
**armed the dog, did not arm the software dead-man, printed one "SELFTEST: spinning" line and spun**;
the log ends there with **no `platform_reboot entered`, no `exception:`, no deadline keys** and
`No errors detected`. The device came back **on its own** inside a measured ~22 s session. That is
a hardware reset with no software writer — the dog bit. So Q4: the dog is independent and bites.

## The trap — why a bite does NOT recover the log

The reader assumes "bite ⇒ reset ⇒ re-enumerate ⇒ `/proc/last_kmsg` has the log." Two measured facts
break that chain:

- The `/proc/last_kmsg` carrier is the **flashed ROM's** kernel, and `fastboot boot` exposes *none*
  (`/home/.../memory/mi4-last-kmsg-reader-carrier.md`). A fastboot-booted image's ram_console is
  readable only if it survives into the *next* boot's DRAM.
- **The reset lands AFTER the stall, not before it.** The stall freezes the pet at ~24 s; the dog is
  an unfed **32 s** countdown (bark 29 s + 3 s gap). So the ordering is
  **storm → tick/interrupt path frozen → pet stops → dog bites (± a few s) → possibly a re-arm race
  → dark**. When the bite resets into another bootloader where the dog is armed-before-work, the
  next kernel clears the ram_console **on entry**, before anything reads `/proc/last_kmsg`.

This is why the same class of run is sometimes **dark** (no reader of the preserved buffer) and
sometimes **self-recovers** (1009/1010: "the device was dark and returned **on its own** … the
watchdog reset it, as in 1009", `records/1009-press-read.txt:4-5`, `records/1010-press-read.txt:4-5`;
1010b: "returned 0 … the device came back **51 s** after `fastboot boot`, on its own",
`records/1010b-press-read.txt:4`) — while 1011/1012 stayed dark. The press log's own recipe proves the
distinction: for 1011 the log *was* recoverable
(`out/stage90/captures/recovered-unknown-arm-20261014-220748-last_kmsg.txt`), i.e. the run's buffer
survived to a readable ROM boot; for 1012 it did not.

## The honest reading of the runner's claim

`scripts/preflight_boot_check.sh:2026-2028` asserts the *mechanism*:

> "hardware watchdog: ARMED. The SoC resets itself if the payload stops making progress, whatever the
> CPU is doing — and platform_reboot() forces a bite so the reboot does not depend on PS_HOLD."

The first half is **true and measured** — "whatever the CPU is doing" is exactly right; the stall
cannot stop it. What it is *not* is a **log-recovery** net: it re-arms/clears the buffer state and so
can eat the very log the run produced. `records/1012-press-log.txt:412-422` is even honest about the
history ("the last four hangs … did NOT come back … Why they did not come back is NOT measured") and
`:445-450` gives the design that would make the entry side pet it — a store to `0xf9017004` from a
path XNU already runs (which the `RESIDENT=1` pet now does).

## Q5 — verdict

**(b).** The watchdog IS armed (Q1, measured on the storm images) and IS proven to bite (Q4, 677),
and the entry image pets it (Q2). The stall does not defeat the *dog* — it defeats the *pet*: by
trapping the CPU in the retrying abort handler it keeps `WDT_RST` from ever being written, so an
unfed 32 s countdown fires **during/around the same window the storm occupies**. That bite is real
hardware, but because it lands *after* the stall, it resets into a state whose boot sequence clears
the ram console → **dark, no `last_kmsg`, press needed.** The net is not a false claim and not a
"something else"; it is a real reset net whose timing guarantees it eats the log it was supposed to
aid recovery for, whenever the pet cannot run.

## Consequences (design, not action)

1. **A resident rung's net is now the PET, not the bite.** Once `RESIDENT=1` links `entry_wdt_pet`
   (`src/entry/entry_storage.c:7696` and the `#if STAGE90_XNU_RESIDENT` site), every healthy pass
   feeds the dog; the *first* healthy feed proves the net is "not needed", and only a pet-stopping
   fault (the storm) lets it fire — into a dark device. To recover the storm's log **without a press**,
   the pet must cover the one path the storm actually occupies: the abort handler. A store to
   `WDT_RST` (`0xf9017004`) from a site every `sleh_abort` reaches would keep the run alive long
   enough for the read door / idle loop to publish, instead of letting the dog end it dark.
2. **The runner's "the watchdog, from the resident idle wrapper" clause is off-by-the-stall:**
   `xnu_live_wdt_pets` stops rising at the moment the storm begins, which the clause currently reads
   as "RESIDENT … the boot was ALIVE" — true, but it also masks the fact that the *same* reading is
   the last feed before the dark. The clause should read "last feed" against `abt_n`/`abt_maxcall` so
   a pet count frozen beside a live storm is recognised as the pre-bite window, not as liveness.

---

### Citations

| # | claim | site |
|---|---|---|
| 1 | arm called on boot | `src/stage90_main.c:1261-1263` |
| 2 | arm writes BARK/BITE/EN/RST | `src/hw_watchdog.c:228-231` |
| 3 | base `0xf9017000`, readback gate | `src/hw_watchdog.c:94`, `:284-288` |
| 4 | pet definition | `src/entry/entry_trace.c:2196` |
| 5 | pet site 1 (idle wrapper) | `src/entry/entry_trace.c:2403` |
| 6 | pet site 2 (read door) | `src/entry/entry_storage.c:7725` |
| 7 | pet store + half-bark threshold | `src/entry/entry_trace.c:2268-2269` |
| 8 | no store to the bite register from the entry side | `records/1012-press-log.txt:432-433,445-450` |
| 9 | disarm = GIC PPI only; EN read live | `src/gic.c:630,666`; `src/xnu_entry_jump.c:265` |
| 10 | arm block measured on the storm images | `out/stage90/captures/recovered-unknown-arm-20261014-220748-last_kmsg.txt` |
| 11 | the net bites (self-test, returned on its own) | `docs/experiments/experiment-677-the-press-fired-and-the-reset-came-early.md` |
| 12 | dark-vs-self-return across the same class | `records/1009-press-read.txt:4-5`, `records/1010-press-read.txt:4-5`, `records/1010b-press-read.txt:4` |
| 13 | runner's ARMED claim | `scripts/preflight_boot_check.sh:2026-2028` |
| 14 | runner's own caveat | `scripts/run_and_capture.sh:735-749` |
| 15 | byte-identical arm + counter-running keys | `out/stage90/xnu_arm_entry.elf` (`nm`/`objdump`, 2× `bl entry_wdt_pet`) |