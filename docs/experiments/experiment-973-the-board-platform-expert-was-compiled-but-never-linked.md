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