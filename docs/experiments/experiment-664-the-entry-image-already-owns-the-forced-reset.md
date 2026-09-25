# 664: the entry image already owns the forced reset — a false absence, the encoding it hid behind, and what it changes about the next arm

663 §3.2 measured XNU's own reboot path and found it a NULL hook behind an infinite spin, and closed with a
claim about the entry image: *"the entry image contains **0** occurrences of `f9017` and **0** of `fc4ab000`,
so both literals are new"*. **The second half of that sentence is false**, and it was false in a way this tree
had already written the method for.

This step corrects it, states what is actually in the image, and draws the consequence — which is not cosmetic:
**the forced reset 663 §2 was designing already exists in the entry image, and it is the mechanism by which
every returning run returns.** Nothing was built and no device was touched; every reading below is
`arm-none-eabi-objdump`, `nm` and greps over the frozen image and its sources.

## 1. The false absence, and the encoding it hid behind

The address is in the image. At `80005794`:

```
8000578c:  movw r3, #0xbfff
80005790:  mov  r2, #0
80005794:  movt r3, #0xfc4a
80005798:  str  r2, [r3, #-4095]     ; r3 = 0xfc4abfff ; 0xfc4abfff - 0xfff = 0xfc4ab000
```

That is **`MSM8974_PSHOLD`**, built as a `movw`/`movt` pair and reached by a negative-offset store. A
flat-string search for `fc4ab000` cannot see it, because the literal never appears: the address is assembled
from two 16-bit immediates. And the *other* store three instructions earlier is the same shape for the other
address this step is about:

```
80005778:  movw r3, #0x5501
8000577c:  mov  r2, #0xfa00000
80005780:  movt r3, #0x7866
80005784:  str  r3, [r2, #1628]      ; 0x0fa00000 + 0x65c = RESTART_REASON
```

**Two ways this could have been caught, both already in the tree, and one of them is a check that runs on
every press:**

* `tools/check_storage_refs.py:22-25` says, of the instrument that exists to catch storage references:
  *"Confirmed on this payload: the GIC base appears as `movt r3, #63744` (`0xf900`) and **PS_HOLD as `movt
  ..., #64586` (`0xfc4a`)**."* — `#64586` **is** the `movt` at `80005794`. The instrument's own docstring
  names the instruction that refutes the claim.
* The gate has a dedicated `movt_pattern(imm)` helper (`preflight_boot_check.sh:1377`) and prints a row for
  exactly this page (`:1330-1360`), whose comment states the rule in this project's own words: *"before
  concluding a value is absent, establish that the extractor could have seen it"* — and then gives the
  concrete trap: *"`llvm-objdump` defaults to a pre-ARMv7 decoder on it and prints `.word` for every `movw`,
  `movt`, `ubfx` and `dmb` … A search run that way answers 'nothing materialises the watchdog's page' while
  being unable to see the two encodings most likely to carry it."*

So the defect is not that a check was missing. It is that **a claim about an encoding was taken with a
method blind to the encoding, in a file whose neighbour documents the method and prints the same page's
row.** The corrected statement is in §2.

## 2. The real fact: `entry_epilogue` is a forced PS_HOLD reset, and it is how a run comes back

`stages/stage90/xnu_arm_boot/entry_stubs.c`:

```
:70   #define RESTART_REASON     0x0fa0065cu
:71   #define RESTART_NORMAL     0x78665501u
:72   #define MSM8974_PSHOLD     0xfc4ab000u
:3531 __attribute__((noreturn, noinline)) void entry_epilogue(const char *why)
:4227     *(volatile uint32_t *)(uintptr_t)RESTART_REASON = RESTART_NORMAL;
:4228     __asm__ volatile ("dsb sy" ::: "memory");
:4229     *(volatile uint32_t *)(uintptr_t)MSM8974_PSHOLD = 0u;
:4230     __asm__ volatile ("dsb sy" ::: "memory");
:4232     for (;;) { __asm__ volatile ("wfe"); }
```

The disassembly of that tail is §1's two stores: `RESTART_REASON ← 0x78665501`, `dsb`, `PSHOLD ← 0`, `dsb`,
then `800057a0 wfe` / `800057a4 b 800057a0` — the halt. And the function is `noreturn`, so **this is not a
diagnostic tail; it is the end of the run.**

**And the frontier's own handler reaches it.** The run dies at the idle exit's `pop {fp, pc}` — and **the fault
is a *prefetch* abort, not a data abort** (corrected by 665 §1, from the returning run's own log:
`sleh_abort: prefetch abort in kernel mode: fault_addr=0x5006e74`, and `0x05006e74` is the `rtcpre_pop` word the
`pop` loaded into `pc`). The entry handler for it is `fleh_prefabt` (`0x8000ae34`, `entry_stubs.c:9298-9318`),
and **both** abort handlers end in the same place:

```
fleh_prefabt  (:9318)    entry_epilogue("exception: prefetch abort");
fleh_dataabt  (:9395)    entry_epilogue("exception: data abort");
...
              (:9401)    entry_epilogue("a data abort inside the data-abort handler");
```

So the chain for every run that ends the way the frontier ends is: `pop` faults → `fleh_dabort` records the
first abort's full state → `entry_epilogue` dumps the report into the RAM console → **`PSHOLD ← 0` → the
phone resets** → Android comes up → the catcher captures `/proc/last_kmsg`. **The log is reachable because
this store is what ends the run, and the warm reset is what preserves it** — which is the same sentence the
payload's own `platform_reboot` comment makes (`stage90_main.c:1064-1071`), written a second time on the
entry side.

**Three consequences follow, and they are the reason this is worth a document:**

1. **The reset primitive 663 §2 and §3.1 were designing for already exists and is already exercised.** Every
   returning run in this project's history is a proof of the PS_HOLD store: a run that reached
   `entry_epilogue` and did *not* reset would sit in the `wfe` loop with the phone dark, and no capture would
   exist. Captures exist (513, 520, 533, 574-park). So the rehearsal arm of 663 §3.1 has a **proven** half
   and an **unproven** half, and they are not the two §3 gave equal weight.
2. **The abort path cannot leave the phone dark by itself.** Both abort handlers end in the same store, and
   `fleh_dataabt`'s *second* exit — reached when the epilogue's own path faulted — is the code's own comment's
   loop (*"a loop here is a loop that never ends"*). So a non-return after a fault means the epilogue's path
   faulted *and* recursed, which is narrower and more specific than 662 §4's three explanations. **And 665 §2
   removes the third of them outright** — the returning run's log shows it entered XNU's own reboot path
   (`Attempting system restart...MACH Reboot`) and spun there, which XNU's path provably does — leaving *a hang
   that takes no fault at all* as the explanation that needs no extra assumption.
3. **It changes which half of the rehearsal is worth a press.** §2's PS_HOLD write is proven by construction;
   the watchdog bite has never been observed to fire. So the rehearsal arm's new code is the bite alone.
   **And 665 §3 adds a requirement to that arm**: a returning run cannot be *attributed* to the bite from
   outside, because the other net may have been what returned it and the report's own caller field
   (`xnu_entry_why`) read NULL in the run that needed it — so the arm must publish a marker that survives.

## 3. The watchdog half survives a proper check — in four encodings, not one

The `f9017` half of the original sentence is **still true**, and it is now checked the way §1 says it must be:
through the encodings an instruction can carry an address in, not through a flat string.

| encoding of `0xf9017000` / `0xf9017014` | hits in `out/stage90/xnu_arm_entry.elf` |
| --- | --- |
| flat text (`grep` over the disassembly) | **0** |
| `movt rD, #0xf901` (the high half) | **0** |
| `movw rD, #0x7000` / `#0x7014` / `#0x7008`, including the rotated-`mov` forms (`#28672`, `#28692`, `#28680`) | **0** |
| an offset immediate from the GIC's own base (`#0x17000` / `#94208`) | **0** |
| literal-pool words in the raw image (little-endian `0xf9017000`, `0xf9017014`) | **0** |

**And the section the stores would land in is mapped, which the log already says.** `mmu_identity_selftest`
reads each descriptor out of the payload's own identity table just before `enable_identity_mmu()` turns it
on, and those values are in the capture:

```
mmu_entry_gic=0xf9010c02      mmu_entry_imem=0x0fa10c02      mmu_entry_pshold=0xfc410c02
```

Decoded against `STAGE90_PMAP_DESC_SECTION_SO = 0x00010c02` (`stage90.h:4349`,
*"TEX=000 C=0 B=0: Strongly-ordered"*) and `L1_SECTION_MASK = 0xfff00000` (`mmu.c:6`):

| recorded value | section base (bits 31:20) | the address it must cover | in that section? |
| --- | --- | --- | --- |
| `0xf9010c02` | `0xf9000000` | the GIC (`0xf9000000`) **and the watchdog (`0xf9017000`)** | **yes** — same index `0xf90` |
| `0x0fa10c02` | `0x0fa00000` | `RESTART_REASON = 0x0fa0065c` | yes |
| `0xfc410c02` | `0xfc400000` | `MSM8974_PSHOLD = 0xfc4ab000` | yes |

(the `1` in the middle of each is bit 16, the section's **shareable** attribute bit, which is *inside* the
descriptor and outside the base field — a decode that reads bits 31:20 as the base is right, and one that
reads the whole word as a base is off by a megabyte. That slip was made and caught here, in the first draft
of this very table.)

**So both of `entry_epilogue`'s stores target a section the payload mapped strongly-ordered before the jump,
and the watchdog shares the GIC's section.** The gate's own comment says the same of the GIC's instal:
`entry_gic.c:377` maps exactly that 1 MB section at run time, so an image can reach the watchdog's registers
without materialising an address in the page. **What §3 establishes is that nothing does — not that nothing
can**, which is the gate's own, narrower wording and the right one.

## 4. What this changes about the next arm — and the one thing it does not settle

663 §3.1's rehearsal arm was: *do nothing at the seam except force both reset paths and not return.* §2 above
splits that arm's premise in two, and the split decides what to build:

* **PS_HOLD: proven.** It is the entry image's own run-ending store, exercised on every returning run. A
  rehearsal of it would spend a press to re-learn something four captures already establish.
* **The watchdog bite: unproven, and now the only net for exactly the failure that is actually happening.**
  The arming is confirmed in the logs we have (`hw_watchdog_enabled=1`, `counter_running=1`,
  `countdown_plausible=1`, and `disarm_hw_watchdog_en=1` read at the jump) — with the caveat that **the
  hanging runs have no logs, so "armed in the hanging run" is an inference from a byte-identical payload, not
  a measurement.** Five hangs, no return, against a bite due 28 s in.

So the arm after this one is not 663 §3.1's two-branch rehearsal but its **single-branch** form: **force the
bite alone, and nothing else, and see whether the phone comes back.** Its verdict is one bit and it is
load-bearing in a way the old design's was not, because §2 has just shown that the *other* net is not the one
that is failing.

**And one question this step opens without settling, named because it is cheap and it is a risk to the return
mechanism itself:** the entry image installs **exactly one** section into XNU's live tables —
`entry_mmio_section(STAGE90_GIC_DIST_BASE, …)` at `entry_gic.c:377`, and it exists because *"the console's
cached table pointer once went stale"*. `entry_epilogue`'s two stores are in **different** sections
(`0x0fa00000`, `0xfc400000`), and no entry-side install covers them. They work — the returning runs are the
evidence — but *nothing checks that they still work*, and the mechanism by which every log in this project
reaches a host depends on a mapping that only the payload's table established. That is a candidate for a
one-line instrument, and it is not built here.

## 5. What this does not do

* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** No boot was observed; `poll_seq` still
  stops at 2 and `slot_post_calls` is still absent. The phone is dark and needs a power press, and the press
  remains the only step that can move the frontier.
* **It does not build anything, and changes no closure member.** `out/` was read and not written; the gate,
  the runner, the readiness tool and `xnu_arm_boot/` are untouched; no watcher was armed, replaced or read
  more than `ps`/`tail` (the armed watcher, pid 3454466, is still the only thing that can press).
* **It does not claim the watchdog bite is broken.** It claims the bite is *unmeasured*, the PS_HOLD path is
  *measured*, and the difference should decide the next arm. Five non-returns with an armed net is strong
  evidence and is not proof: the bite could have fired and Android could have failed to come up.
* **It does not re-run the gate's own watchdog row**, which already performs the multi-encoding scan §3
  reimplements by hand. §3's table is a cross-check of a check, and its value is that it was taken without
  trusting either.
* **TWRP-to-storage stays withheld** — 「如果os已经能进去了的话」 is unmet, and an unobserved boot is not it.

## 6. Safety

No device action of any kind: no `fastboot`, no `adb`, nothing sent anywhere, **nothing written to storage**.
Every reading is host-side and read-only — `arm-none-eabi-objdump -d`, `nm`, greps and `struct.unpack` over
`out/stage90/xnu_arm_entry.elf`/`.bin`, the sources under `stages/stage90/` and `stages/stage90/xnu_arm_boot/`,
`tools/check_storage_refs.py`, and the archived capture
`out/stage90/captures/650-owed-run-park-574-2026-09-24-last_kmsg.txt`. No build, no edit to any file in 660
§5's closure, and 663's design is unchanged in what it *does* — only in which half of it is worth a press.
`fastboot boot` only, never `flash`; the corrections here are to a document, an index row and memory entries.
