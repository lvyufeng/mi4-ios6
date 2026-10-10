# Experiment 976 — 915-B rung 1: the low bank injected into DT /memory, provably inert

**Status:** ✅ **BUILT, INERT, NO PRESS OWED.** `STAGE90_XNU_REGIONS=1` appends the device's low bank
`[0x00000000, 0x60000000)` to the payload's `/memory/reg` as **words 2/3**, leaving the boot pair
`{RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE}` at words 0/1 (`src/stage90_main.c:49-64`). It is a **no-op for
every reader** — the arm's whole claim — and `tools/check_d13_regions.py` makes that structural.
**Default 0: at 0 the payload is byte-identical to the shipped 975 arm (`1091566c…`, record
`6c2b6038`), verified by rebuild.** No device behaviour changes, so **no press is owed**; this rung is
the first half of the 915-B region port and is built so the region list exists in the DT for the later
rungs. **PRESS IS THE OPERATOR'S.**

Follows `experiment-915b-the-region-port-v2.md` §4 **rung 1** (the forced rung order). Continues the
915-B line after 958/964.

---

## 1. What rung 1 is, and what it is NOT

**Is:** the first rung of the 915-B region port. It **injects** the low bank into the DT's `/memory/reg`
so a later rung (region registration in `arm_vm_init.c`) can read it. 915-B v2 §4 orders the rungs so
nothing allocates before the carveout spans are excluded and a reachable managed VA exists; rung 1 is
before all of that — pure injection.

**Is NOT:**
- NOT a change to any consumer. No reader reads words 2/3 (§3), so nothing about the boot changes.
- NOT the whole 915-B port. The allocator is not made region-aware, no second interval exists, no
  bounce window exists — those are rungs 2–5 (915-B v2 §4).
- NOT a re-order. The boot pair stays at **index 0/1**; the low bank is **appended**, never prepended
  (915-B v2 §2.4: the v1 `{low,high}` order made the gap-7 gate FALSE → a `platform_reboot`).

## 2. The edit

`src/stage90_main.c:49-64`:

```c
#if STAGE90_XNU_REGIONS
    static const uint32_t memory_reg[] = {
        RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE, 0x00000000u, 0x60000000u,
    };
#else
    static const uint32_t memory_reg[] = {
        RAM_PHYS_BASE, RAM_BOOT_BANK_SIZE,
    };
#endif
```

emitted by the unchanged `apple_dt_prop_u32_array(b, "reg", memory_reg, ARRAY_SIZE(memory_reg))`
(`:883`). `scripts/build.sh` always defines the macro **0 or 1** (`case`-guarded), and records it in
`stage90-build-config.txt` **only when ON** — so the default record stays `6c2b6038` byte-for-byte
(the "an absent key is not a missing value but the value" idiom preflight uses for SMEM_PROBE/MEM_TOTAL).

**`#if`, not `#ifdef`.** The macro is always defined, so `#ifdef` would read a `0` as "on"
([[mi4-off-option-two-spellings]]); the guard refuses the `#ifdef` spelling.

## 3. Why it is inert — the three readers, checked

Genuinely inert requires that **no consumer of `/memory/reg` reads words 2/3**. Re-read by value:

| reader | what it reads | pair-0 comparison |
|---|---|---|
| `src/pe_state.c:38-39` | `reg_value(memory, "reg", 0/1)` | `memoryBase`/`memorySize` |
| `src/pexpert.c:65-69` | `mem_reg[0]`, `mem_reg[1]` (`len >= 8`, still true at 16) | `mem_reg[0]==RAM_PHYS_BASE && mem_reg[1]==args->memSize` |
| `src/xnu_pe_init_platform_false.c:422-425` | `stage90_pe_init_reg_word(memory, "reg", 0u/1u)` | `base==RAM_PHYS_BASE && size==RAM_CONSOLE_BASE−RAM_PHYS_BASE` |

All three read pair 0 alone. And the **D13 kernel itself does not read `/memory/reg` at all**: its
memory geometry comes from `boot_args` — `avail_end = gPhysBase + gMemSize` (`arm_vm_init.c:434`), with
no DT walk (grep-verified: the only `/memory` DT read in D13 is `pe_init.c`'s `/chosen/memory-map`).
So the appendix reaches nothing.

**The check.** `tools/check_d13_regions.py` refuses, as a build/check stop:
1. the `#ifdef` spelling;
2. a `memory_reg[]` whose enabled arm does **not** begin with the boot pair, or whose disabled arm is
   not the boot pair **alone**, or whose enabled arm is not exactly `<boot pair>, 0x00000000u, 0x60000000u`;
3. a reader that reaches `/memory`'s word **≥ 2** (`pe_state.c`, `pexpert.c`,
   `xnu_pe_init_platform_false.c`) — that would **consume** the low bank without the rest of the port;
4. a `build.sh` that stops defaulting to 0, stops passing the `-D`, stops refusing a value other than
   0/1, or stops recording the switch when on.

Scope note: the check is scoped to the **`memory` node** — `pe_state.c`/`xnu_pe_init_platform_false.c`
legitimately read the **GIC** node's `reg` word 2, which is a different node's array. Wired into
`make check`; `--selftest` mutates each fact and asserts the refusal.

## 4. By-value verification

- **Inert off:** `STAGE90_XNU_REGIONS=0` rebuilds `stage90-qcdt.img` to `1091566c…` **byte-identical**
  to the shipped 975 arm, and `stage90-build-config.txt` to `6c2b6038…` (unchanged). The append code
  is absent from the off build.
- **On:** `STAGE90_XNU_REGIONS=1` → `stage90-qcdt.img 242448e7…` (new) and the record carries
  `#define STAGE90_XNU_REGIONS 1`. The object `stage90_main.o` carries the four-word run
  `{0x80000000, 0x5e500000, 0x00000000, 0x60000000}` **exactly once**.
- `make check` 0; `check_d13_regions.py` + `--selftest` pass.

## 5. What this unlocks, and what is still owed

Rung 1 puts the region list where the region-registration rung (915-B v2 §2.1) can read it. Every
later rung is still owed: the carveout reconciliation (rung 2), region-aware predicates + the second
allocator interval (rung 3), per-region PV tables (rung 4), and the bounce window + visualizer field
(rung 5). **None of those is inert**; each is a device run, and rung 5 is not pressable until the
visualizer carries the window's second field (915-B v2 §3.5). The two open inputs (bounce-window SIZE,
SMEM span reconciliation) remain open.

**No press is owed for rung 1**, and none is sent. The device is unmodified by this experiment.

## 6. Scope / non-goals

- **The 3 GB report** (958) is untouched — `max_mem` keeps its ONE writer; rung 1 changes no reported
  quantity.
- **`gMemSize`/`mem_size`/`sane_size`/`gPhysBase`** are untouched (964).
- **The entry window** is untouched (956/970).
- **The 915-B pmap path is not enabled** — rung 1 is injection only.

*Provenance: `src/stage90_main.c:49-64`, `src/pe_state.c:38-39`, `src/pexpert.c:65-69`,
`src/xnu_pe_init_platform_false.c:422-425`, D13 `arm_vm_init.c:434` and `pe_init.c:214`, and
`scripts/build.sh` read by value this session; both arms built and compared by hash; `make check` 0.
Follows [[mi4-915b-region-port-v2-design]], [[mi4-958-3gb-rides-the-report]], [[mi4-964-915b-corrected-against-958]].*