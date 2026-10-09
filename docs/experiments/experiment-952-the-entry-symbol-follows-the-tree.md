# 952 — the entry symbol follows the tree; an empty substitution is refused (2026-10-09)

The twelfth rung in the tree-pin series ([[mi4-913-ios7-rebase-decision]]). 951 closed the *entry*
build; the frontier moved to the **payload** build (`scripts/build.sh`), which embeds the entry
image. The first D13 payload build stopped at a compiler error in `xnu_entry_jump.c`:

```
error: expected expression before ';' token
  185 |     r->entry_va = STAGE90_XNU_ENTRY_ENTRY;
```

`STAGE90_XNU_ENTRY_ENTRY` had expanded **empty**. It is substituted from `@ENTRY@` in the generated
header `out/stage90/xnu_arm_entry.h`, and `@ENTRY@`'s value comes from
`nm xnu_arm_entry.elf | awk '$3=="_start"{print "0x"$1}'`.

## Two causes, one commit

**1 — the symbol's NAME is pinned to 4570.** 4570's `start.s` declares a global `_start`. D13 ships
no `start.s`; its entry is `osfmk/arm/locore.s:52`'s `EnterARM(_start)`, and that macro
(`masked_globals_asm.h`) writes the symbol with a leading underscore — **`__start`** — the Apple
underscore convention 936 already handles for `xnu_arm_assemble.sh`. So on D13 the lookup for `_start`
returned nothing (the elf has `U _start` — an *undefined reference* left over — and `T __start` at
`0x80000000`). Both symbols are the image's first instruction (`mmu_reinitialize`, at `ENTRY_BASE`);
only the name differs. The lookup now picks the tree's name (`$D13_TRACE`).

**2 — an empty value slips past 459's placeholder check.** The loop substitutes `@ENTRY@` →
`$entry` with `sed s/@ENTRY@//`; when `$entry` is empty the `@ENTRY@` is *deleted* exactly as a real
value would delete it, so 459's `grep -o '@[A-Z0-9_]*@'` finds nothing left and the check **passes** on
a header whose macro is empty — and the payload then fails to compile. This is
[[mi4-silence-is-a-reading-only-if-success-is-silent]] one level down: the check could not tell "no
placeholder left" from "the value that replaced it was nothing". The substitution now **refuses an
empty value** (and the entry lookup names the missing symbol before that). A missing symbol here is not
a valid number the generator should write; it is a `_start`/`__start` mismatch.

## State after this rung

The D13 entry build prints `entry point  0x80000000` and the header carries
`#define STAGE90_XNU_ENTRY_ENTRY 0x80000000`. The D13 payload build gets **past** the compiler error
and stops at the next wall, **953**: `xnu_arm_entry.bin carries no literal 'up_style_idle_exit'` — 515's
boot argument is 4570-only (D13 has no `up_style_idle_exit` global and no `caches.c`; its `arm_init.c`
parses only `maxmem`/`-no-cache`/`serial`). `make check` exits 0; 4570 is untouched (the entry-symbol
pick defaults to `_start`).

## Provenance

`src/entry/build_entry.sh`. Host-side, reversible, **no press**.