# 595: the next boot sends the sleepless arm, and the frozen arm is parked rather than spent

594 built the arm that might reach the OS and left it unbootable, on the grounds that rebuilding the
payload "spends the freeze" and that is the user's decision. This step is that decision taken the way
the goal asks for it, and the two things that make it cheap: **the frozen pair is parked byte-for-byte
first, and the gate's own clause already names this rebuild as the owed one.**

Nothing was destroyed, nothing was flashed, and the arm this replaces is two `cp`s away.

## 1. Why the sleepless arm gets the next power press

The goal is 「起码要能进入操作系统」. The other candidate for the next boot is the frozen 574 measurement
arm, and it **provably cannot** reach the OS: 547 §4 pre-registers a death at `platform_cache_idle_exit`'s
`pop {fp, pc}`, and 593 showed that death is the *first arrival* at the window — the very thing 594's arm
never does. A boot of it buys one measurement (`b1`) and no progress on the goal.

The reverse question — does 594's arm need the frozen run first? — is answered by 594 §4's own reader: its
baseline is **520's and 533's archived logs**, not a fresh 574 run. So the order is free, and the only
scarce thing in this phase is the power press.

## 2. The rebuild is the gate's own sanctioned branch, not a spent freeze

`preflight_boot_check.sh:877` states the rule in the gate's own words, and it is a two-branch test on one
hash:

> First rebuild the entry image with the switches this arm needs … Then compare: if `sha256sum
> out/stage90/xnu_arm_entry.bin` still equals the `STAGE90_XNU_ENTRY_SHA256` the record names, the changed
> source fed no compiler input, the payload's blob clause is already satisfied, and `./build.sh` **must
> NOT** be run — it does not reproduce (408) … **If the hash differs, the arm really is a new one and
> `./build.sh` is owed, with `STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1'`** or the new image never jumps
> into XNU.

The hash differs: `696a0f39…` against the record's `151425c4…`. So this is the owed branch. The freeze
discipline the gate protects is "do not rebuild the payload for a change in nothing the compiler saw";
here the change **is** the artifact the compiler saw.

**And `build_entry.sh` is reproducible while the payload link is not** — that asymmetry is the whole reason
the park is bytes and not a rebuild. It was measured tonight, not assumed: the switch-off entry build
reproduced the frozen `151425c4…` **byte-for-byte**, and the gate's clause above is what records that
`./build.sh` does not reproduce (408).

## 3. What was parked before anything was built

`/tmp/r594/frozen-payload/` — the whole frozen payload and the frozen entry pair, copied and re-hashed:

| artifact | sha256 |
| --- | --- |
| `stage90-qcdt.img` (what `fastboot boot` sends) | `914f45ac7098341b25c027f71e8551d11c9b13f4f40d3bd9e470714435e2939a` |
| `stage90.bin` | `0f108392b9b20fc025311ea0b71c2a0cec5e8760e7e39867f68994cf5e2092a3` |
| `stage90.img` | `8274b1c4ae2dfc5ea317e17a0cf3f6e024e3bf7eaafdef964cabc5b66d75f339` |
| `stage90.elf`, `stage90_fixture.macho`, `SHA256SUMS.txt`, `stage90-build-config.txt` | as built 01:33 |
| `xnu_arm_entry.bin` / `.elf` / `-config.txt` / `-sources.txt` | `151425c4…` / `3bc72605…` / record `IDLE_NO_SLEEP=0` |

The parked copies were re-hashed against the originals (`914f45ac…`, `0f108392…`) rather than trusted.
**Revert is: copy the two groups back.** No rebuild is involved in going back, which is the point — a
revert that needed `build.sh` would not be a revert at all.

## 4. The pre-flight that isolated the one clause at issue

Before spending the build, the arm-on entry image was put in `out/` with the payload still frozen. The gate
refused at **EXIT=1 with exactly one line**: `does not carry the arm … byte for byte`. Everything before it
passed — build configuration, `sha256sum -c`, `no source file is newer than the image`, and the header's
`kernel_size=6015356` / the blob's compiled-in `5519996` — so **no other clause in the gate objects to this
arm**, and the payload was the only thing owed. A ten-minute build is worth ten seconds of that.

(And the transient state is the fail-safe one: while the two are mismatched the gate refuses and no boot
can start. That is the same shape observed mid-restore earlier tonight, and it is the direction to want.)

## 5. The build, and the self-check that it changed one thing

`STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh`, the invocation the gate names.

| reading | value |
| --- | --- |
| `stage90-qcdt.img` | `60063c47fc977cfe38c3e03e4a7699e03921fe9783a764601c5b4f79925dec9c` |
| `stage90.bin` | `ce02edfe5266f8d76f2ef6c05c1c18f4b99386fa96a4c7bc5841b48d4272d071` |
| `stage90-build-config.txt` | **byte-identical to the frozen payload's** — the `-dM` dump is the same switch set |
| the new `stage90.bin` contains `xnu_arm_entry.bin` (`696a0f39…`) | **1** time |
| the new `stage90.bin` contains the frozen arm (`151425c4…`) | **0** times |
| `kernel_size` | 6015356, unchanged (the arm is the same length) |

**The identical `-dM` dump is what makes "the switch set did not change" a reading and not a claim.** The
payload is 48 bytes from being the same artifact; the only difference the build was asked for is the arm
inside it, and the one file that records the compiler's view says the compiler's view is unchanged.

## 6. The gate, on the rebuilt pair

`./preflight_boot_check.sh --allow-xnu-entry` → **EXIT=0 / 518 lines / 39,142 bytes / 0 stderr**, and the
blob clause now reads:

```
  the blob is 5519996 bytes at payload offset 494100 = image offset 496148 (page_size 2048 + 494100)
  the linker's symbol says the blob is 5519996 bytes at 494100, which agrees
```

496148 is the offset the gate's own comment predicts for a `STAGE90_XNU_ENTRY=1` payload (the `#if` block's
`bl` plus its argument setup is exactly 48 bytes), and the `== which arm the entry image in out/ is, in
words ==` block prints the sleepless arm's narration — including the sentence that names `xnu_live_repair_*`
as **absent by construction** rather than absent because a publisher was unreachable, which is the one
absence on this arm that is a deliberate silence (594 §5).

## 7. Pre-registered: what this run should read, and what would refute it

| item | value |
| --- | --- |
| the boot | `fastboot boot out/stage90/stage90-qcdt.img` (`60063c47…`), which embeds the entry arm `696a0f39…` |
| **the witness** | `xnu_live_poll_seq` reaching **3** — `entry_note_poll` publishes `g_poll_calls + 1` only *after* `__real_poll` returns, the park is the third poll, and both parked logs stop at **2** (timeouts 5 ms and 40 ms). Corroborated by the largest `xnu_live_poll_timeout_ms` ≥ `ENTRY_PARK_MIN_MS` (1000, read out of `entry_trace.c`) |
| second witness | the park's console group `mini4: the repair -- … called 0 time(s)` present — the baseline cannot print it at all (it is in neither 520 nor 533) |
| the arm's signature | `xnu_live_sip_seq` / `pce_seq` / `wfi_seq` / `repair_seq` / `seam_*` / `slot_cwe_*` / the bracket's `pre`/`rtcpre`/`post` all **absent**; `door_seq` and `idle_seq` still climbing to `0x8000` |
| **the falsifier** | `poll_seq` still **2**. That is the live spin 593 §4 pre-registered: alive, on the bus, never progressing — and it is *not* a hang, so the runner's exit code cannot tell it from a good run |
| the return path, and why the run is bounded | the payload arms the MSM8974 hardware watchdog once (`stage90_main.c:1205`, `STAGE90_HW_WATCHDOG_TIMEOUT_S` 25 s + a 3 s bark/bite gap) **before** the handoff, and **nothing pets it**: `grep -rn 'stage90_hw_watchdog_arm'` finds one call site and there is no pet function, and the XNU side has no MSM watchdog *driver* — the two mentions in `osfmk/arm/` are the panic-log `WDT timeout` string and a boot-arg. So a spinning XNU still resets at ~28 s, the SoC re-enumerates, and the log in DRAM is readable. A run that ends without a return is the watchdog not biting, which would itself be a finding |
| expected runner exit | **0** if the phone returns and adb reads the log; **3** if it returns into a state adb cannot reach (551 — a capture failure, not a hang). Neither is 2 |
| what a good run buys | it is the first boot of any arm that can reach the OS: the pass that killed every previous boot (593's *first arrival* at the window) does not happen, and pid 1's thread runs past the park |

**What it does not buy, stated rather than implied.** A PASS on the witness reads *progress past the death
point*, not "the OS is up": 593 §4 traded the sleep for the boot, so the CPU spins hot until the watchdog
bites, and the readings that decide 「能进入操作系统」 are the ones after the park — the console, the
fixture's own syscalls in the live channel, and item (4)'s user-mode records. And 574's `b1` and the 535/590
axis are still owed; the frozen arm is deferred, not answered.

## 8. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot — the build writes only
under `out/`. `fastboot boot` only, never `flash`, so no outcome of this can write to the device. The
sleepless arm's worst case is an XNU that spins: alive, not bricked, and reset by a hardware watchdog that
nothing in either half of the image pets, with a power press as the manual equivalent. **TWRP stays
withheld**: 「如果os已经能进去了的话」 is unmet until a boot says otherwise.
