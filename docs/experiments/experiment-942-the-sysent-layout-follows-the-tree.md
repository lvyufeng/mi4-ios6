# 942 — the `struct sysent` layout follows the tree (2026-10-08)

This rung is not one defect but the whole **513–522 experiment block**: the D13 entry build stopped at
a tree-pinned *seam*, then at a tree-pinned *census name*, then at a tree-pinned *table* — and behind
each was the same class ([[mi4-913-ios7-rebase-decision]]): an asset or a layout pinned to 4570 that
must follow the selected tree.

## The walls, in the order the build hit them

1. **The seam is 4570's.** `STAGE90_XNU_SEAM_POC=1` in the D13 arm selected the 533/535/572/678/686/690
   seam, which is built out of `caches.c` (`FlushPoU_Dcache`, `FlushPoC_DcacheRegion`,
   `platform_cache_idle_enter/_exit`). **D13 ships no `caches.c`** and `entry_trace.c`'s whole seam body
   is `#if !STAGE90_ENTRY_D13`. Now refused **at the switch** (`build_entry.sh` just after `SEAM_ON=`),
   naming the cause: the two switch files disagree and the built image would name a rung it does not
   contain ([[mi4-off-option-two-spellings]], [[mi4-silence-is-a-reading-only-if-success-is-silent]]).

2. **513–522 are 4570's machine, gated as one block.** The clauses read 4570's *three-door* idle
   (`cpu_idle`/`cpu_idle_exit`, `SetIdlePop`, `idle_enable`, `Idle_load_context`, `cpu_idle_wfi`),
   4570's cache repair (`cpu_signal_handler_internal`, `CleanPoC_Dcache`, `ml_get_timebase`) and
   4570's **assym offsets**. D13 has no `cpu_idle` at all (its idle model is `machine_idle` tested
   against `do_power_save`, `pmCPU.c:41`) and its genassym emits none of `CPU_INT_STATE`, `ACT_CPUDATAP`,
   `ACT_PCBDATA`, `EXC_CTX_SIZE`, `CPU_ISTACKPTR`, `TH_KSTACKPTR`. 937 had already pruned those wraps,
   so the clauses were checking the *body* of a step the tree already gated out — one published skip
   naming every missing symbol, not a rename.

3. **`kalloc_canblock` is same-object on D13.** Its only in-tree callers are `osfmk/kern/kalloc.c:560`
   and `:567` — it calls itself — so `--wrap` never fires and the census called it dead. The *shape* the
   list already encodes for `matchPassive`, made by the other tree: a `same_object` entry plus an
   assertion that it really is in that bucket (a branch to `__wrap_` appearing later means re-derive,
   not widen).

4. **The `pthread_functions` table is 4570's.** `src/supply/stage90_pthread_functions.c` compiles itself
   out on a tree without `<sys/pthread_shims.h>`, so a D13 image has no `stage90_pthread_functions`
   symbol and the check's subject does not exist. Published skip, like 473.

5. **The `struct sysent` layout is per-tree — the real finding.** See below.

## Finding — `struct sysent` is two different ABIs, and the check assumed 4570's

`tools/check_sysent_table.py` reads `sysent[]` out of the linked image and compares its slots with
`syscalls.master`. It had **two 4570 constants baked in**: a 16-byte stride and a `sy_arg_munge32`
word that names a munger symbol. On D13 both are false:

- **D13's `bsd/sys/sysent.h:42-53` reorders the members** — 4570 (with `CONFIG_REQUIRES_U32_MUNGING`,
  `sysent.h:39-53`) is `{ sy_call; sy_arg_munge32; sy_return_type; sy_narg; sy_arg_bytes }`, **16
  bytes**, munger at +4, `sy_narg` at +12, `sy_arg_bytes` at +14. D13 is `{ sy_narg; sy_resv;
  sy_flags; sy_call; sy_arg_munge32; sy_arg_munge64; sy_return_type; sy_arg_bytes }`, **24 bytes**,
  `sy_call` at **+4**, munger at +8, `sy_narg` at **0**, `sy_arg_bytes` at +20.
- **D13 has no munger symbol at all.** Its `bsd/kern/makesyscalls.sh` `__arm__` arm emits
  `/* ARM does not need mungers for BSD system calls. */` and `#define munge_w NULL` for every munger;
  `bsd/dev/arm/unix_syscalls.c:353` marshals with `uthread->uu_arg[i] = state->r[i]` and **never names a
  munger**. So "the image defines no `munge_www`" was the tree's own fact, not a defect.

The check's failure was a *layout* error misreported as a *missing symbol*: the 16-byte reader walked
past slot 0 at the wrong stride and read slot 1's `sy_narg`/`sy_call` half-words as garbage, then the
pre-flight symbol census failed on `munge_*` and never reached the read. Both `sysent` (stride-24 slot
0 = `nosys`) and `nsysent` (440, the D13 `NUM_SYSENT`) were in the D13 image all along.

The fix is not a rename: the tool now **reads which tree and derives the layout**, by the same
discriminator the whole build uses ([[mi4-913-ios7-rebase-decision]] — D13 ships `osfmk/sys/types.h`),
selecting stride, `sy_call` offset, whether the munger word names a symbol (`4570`) or must be zero
(`d13`), and which `syscalls.master` gives the numbering. Index 8's name is per-tree too (`enosys` in
4570's master, `nosys` in D13's). The `--selftest` mutations are listed per-layout: the three
munger-word mutations are 4570-only, and D13 instead gains "a non-zero munger word in 197's slot".

`build_entry.sh` passes `--tree "$XNU_TREE"`. 4570-neutrality is structural: with no `--tree`, the
default tree is 4570 and every path is byte-for-byte the old one.

## State after this rung

The D13 entry build now passes **513–522 (skipped), 473 (skipped), and the sysent read** — the record
line reads *"sysent at 0x805428ec, nsysent at 0x806021e0 = 440, stride 24 (sy_call at +4, NULL mungers),
10 witnesses"*, with all eight fixture slots holding their `__wrap_*` and the ten witnesses matching the
D13 master. The build advances to **459's pool-config check**:

    FAIL: the object pool is the 'RELEASE' kernel; this image links all of it and this step needs
    STAGE90_XNU (RELEASE + mockfs)

— `out/xnu_kernel_obj_d13/config.stamp` says `config RELEASE`, i.e. the D13 kernel pool was last built
as a plain RELEASE kernel and must be rebuilt as `STAGE90_XNU` (RELEASE + mockfs) before the D13 entry
link can reach the root-device checks. That is the next rung.

## Provenance

`tools/check_sysent_table.py`, `src/entry/build_entry.sh`. Host-side, reversible, **no press**.

- D13 build: `/tmp/d13_entry_build26.log` (sysent passes; stops at 459's pool-config check).
- 4570 neutrality: `tools/check_sysent_table.py --selftest` against
  `out/stage90/frozen/armed-storage-054f8269/xnu_arm_entry.elf` (a 4570 image) reads
  `stride 16` and every `munge_*` name, all 24 mutations refused, rc 0. The tool's no-`--tree` default
  is 4570, so a plain `make check`/build is unchanged.
- `make check` exits 0.