# 956 — the entry window's 1 GiB ceiling, and why it is D13's alone (2026-10-09)

The sixteenth rung in the tree-pin series ([[mi4-913-ios7-rebase-decision]]). Every previous rung made
a 4570-pinned value follow the selected tree. This one is the reverse: a value that was **tree-blind**
and had to become tree-*specific*, because the two trees answer the same question differently.

## The defect: no upper bound on the window

`build_entry.sh` bounds `STAGE90_XNU_ENTRY_WINDOW` at the bottom (a 32-bit hex literal, `>= 16 MB`) and
clamps the payload's own identity map below the high-alias base — but **`ENTRY_WINDOW` had no upper
bound at all**. The design comment above the clamp says why:

> the window XNU is TOLD can be as large as the goal wants while the payload's map stops at the alias
> base; the two are decoupled by clamping, NOT by shrinking `memSize`.

That is sound **on 4570**. There `memSize` is clamped against `xmaxmem`, and the kernel pmap's span is
derived as `(gVirtBase + MEM_SIZE_MAX + 0x3FFFFF) & 0xFFC00000`, so raising `MEM_SIZE_MAX` (911b) *moves
the base* and the span never reaches a wrap.

## D13 has no such decoupling, and its ceiling is 1 GiB

`src/xnu_entry_jump.c:150` hands XNU this exact value — `a->memSize = STAGE90_XNU_ENTRY_SIZE` (and
`memSizeActual` with it) — and D13's `osfmk/arm/arm_vm_init.c` clamps nothing:

```
325   gMemSize = args->memSize;
326   max_mem = mem_size = sane_size = gMemSize;
483   pmap_bootstrap(gMemSize, ...);
```

D13's managed map is built from a **fixed VA**:

```
142   #define MANAGED_BASE    0xC0000000
341   managedBaseVA = MANAGED_BASE;
359   l2_cache_to_range(managedCachePA, managedBaseVA, phys_to_virt(cpu_ttb), gMemSize, TRUE);
```

and `l2_cache_to_range` fills `tte_psize = (gMemSize >> 20) << 2` bytes from

```
tte_pbase = addr_to_tte(ttb, 0xC0000000) = ttb + tte_offset(0xC0000000)
tte_offset(addr) = ((addr >> 0x14) & 0xfff) << 2   /* pmap.h:198 */
```

`tte_offset(0xC0000000)` = `((0xC00) & 0xfff) << 2` = **byte `0x3000`** of the 16 KB L1
(`L1_SIZE 0x4000`, `pmap.h:195`). The map fills `gMemSize >> 20` entries of 4 bytes each:

| `gMemSize` | bytes filled | last L1 byte | VA top |
|---|---|---|---|
| `0x04000000` (64 MB, the arm) | `0x100` | `0x3100` | `0xC4000000` |
| `0x3f000000` (headroom max) | `0xFC0` | `0x3FC0` (just fits) | `0xFF000000` |
| `0x40000000` (1 GiB) | `0x1000` | `0x4000` (the whole table) | `0x100000000` |
| `0x5e500000` (911b's ceiling) | `0x1794` | past the table, wrapped | `0x1E500000` |
| `0xC0000000` (the goal's 3 GiB) | `0x3000` | `0x6000`, wrapped | `0x80000000` |

At exactly **1 GiB** the map fills the L1 *exactly* (`[0x3000, 0x4000)`) and its VA top reaches
`0x100000000` — the top of the 32-bit space. One byte more and two things break at once with **no
fault**:

1. `managedBaseVA + gMemSize` wraps 32-bit VA;
2. the loop writes PAST the L1 table (`0x4000 + …`, the `first_avail`/`vectp` region,
   `arm_vm_init.c:350/381`) while `tte_offset`'s `& 0xfff` **masks** the index, so the writes land
   inside the L1 again — silent corruption of the boot tables.

Line 350 writes the same ceiling down independently: `first_avail = managedCachePA +
l2_size(0x40000000)`, a 1 MB L2 reservation for a 1 GiB managed region ("Bit generous.."). So
**1 GiB is D13's entry-window ceiling**, and the goal's 3 GB must be met by the *region-list port*
([[mi4-915-multibank-region-list-design]]), never by raising the window.

> **970 CORRECTION (2026-10-09): 1 GiB is the TOP ceiling, but NOT the binding one.** This rung bounded
> the window at 1 GiB — the point the managed L1 *wrap* makes silent corruption. 970 found a **lower**
> ceiling: D13's managed map is fixed-base and **unclamped**, so a window that reaches the entry's own
> RAM-console VA `0xde500000` occupies the L1 slot `entry_section_install` must install into; the install
> is refused and the boot is **silent, with no log** — an arm that cannot even report why it died. The
> binding ceiling is therefore `RAM_CONSOLE_BASE − MANAGED_BASE = 0x1e500000` (485 MiB), and the table
> row above calling `0x3f000000` the "headroom max" is **wrong** — that value boots silent.
> `build_entry.sh` now refuses any window `>= 0x1e500000`. See
> `docs/experiments/experiment-970-the-window-is-ram-not-just-address.md` §3.

## The repair

1. **`build_entry.sh`** — a D13-gated refusal (`if [[ -f $XNU_TREE/osfmk/sys/types.h ]]`) of
   `ENTRY_WINDOW_REQ >= 0x40000000`, with the full chain in the comment. The D13 arm's `0x04000000` is
   unaffected; 4570's path is code-unchanged. **(970 later added a *second* refusal at `>= 0x1e500000`,
   the RAM-console ceiling — see the correction above.)**
2. **`tools/check_d13_managed_base.py`** (new) — the refusal *verifies its premise*. It re-derives all
   four facts (MANAGED_BASE's value, the fixed `managedBaseVA = MANAGED_BASE` assignment, `gMemSize` as
   the map length, `L1_SIZE`/`tte_offset`'s masking, and the
   `xnu_entry_jump.c` → `args->memSize` → `gMemSize` chain) and refuses if one moves. A D13 whose base
   became derived (the 4570 shape) would make the refusal *wrong*; a tree whose `tte_offset` stopped
   masking would turn "silent corruption" into "fault". The 4570 tree is the **control** — it must ship
   no `MANAGED_BASE`, which also proves the `is_d13` discriminator reads the right tree. `--selftest`
   feeds nine measured mutations; with no `--tree` it checks both repo trees, which is the `make check`
   form.
3. **registration** — `make check` runs `--selftest` then the both-trees form; `build_entry.sh` runs
   `--tree "$XNU_TREE"` beside the refusal it guards, so the same build that refuses also verifies the
   reason.

## Why this is a *tree-pin* rung, inverted

935–955 made pinned values follow the tree. 956 is the same class seen from the other side: a check
that was correct for the tree it was written on and **silently wrong for the other**, because "the
window is decoupled from `memSize`" is a 4570 fact with no D13 analogue. The fix shape is the same —
a tree-gated branch plus a premise-verifying skip/skip-reason
([[mi4-955-press-readiness-names-a-d13-arm]], [[mi4-a-claim-in-a-comment-is-not-a-check]]).

## Provenance

`src/entry/build_entry.sh` (the D13 refusal + the `--tree` check invocation), `tools/check_d13_managed_base.py`,
`Makefile`. Host-side, reversible, **no press**. `make check` = 0; `check_d13_managed_base.py` = ok on
both trees and on its nine-mutation selftest; `bash -n build_entry.sh` clean. The D13 arm
`armed-d13-3af667a3` uses `0x04000000` and is unaffected. **Falsifiable by:** a D13 press at a window
`>= 0x40000000` bricking the boot with no log cause — not run, and refused at build time instead.