# 953 — the idle-cache boot argument follows the tree; the D13 pipeline builds a boot image (2026-10-09)

The thirteenth rung in the tree-pin series ([[mi4-913-ios7-rebase-decision]]). 952 fixed the payload's
entry-symbol lookup; the D13 payload build then stopped at the **515** clause:

```
FAIL: xnu_arm_entry.bin carries no literal 'up_style_idle_exit'
```

`up_style_idle_exit=1` is the boot argument 4570's `arm_init` parses (`osfmk/arm/arm_init.c:287`) to
select `caches.c:414`'s uniprocessor idle-cache branch — 514's repair. **D13 has neither** the global
nor `caches.c`; its `arm_init.c` parses only `maxmem` / `-no-cache` / `serial`, and its idle is gated
by `do_power_save` (`osfmk/arm/pmCPU.c:41`, a compile default tested by `machine_routines_asm.s:63`).
So on D13 the token has **no reader**, and printing it into the command line is a claim the image does
not honour. The token was written by hand into four string literals (`boot_args.c:48`,
`stage90_main.c:797,805`), each ending `… up_style_idle_exit=1`.

## The repair — the token follows the tree

1. **`scripts/build.sh`** computes the tree discriminator and passes it to the payload:
   `STAGE90_XNU_TREE_D13=1` iff `$XNU_TREE/osfmk/sys/types.h` exists — the **same** discriminator the
   entry build and `build_xnu_arm_kernel.sh` use. A bare `./build.sh` (no `XNU_TREE`) defaults to 4570,
   so the `-D` is `0` and the payload is unchanged.
2. **`src/stage90.h`** defines `STAGE90_BOOT_IDLE_TOKEN`: `" up_style_idle_exit=1"` on 4570,
   `""` on D13 — leading with a space so it appends either way. The four literals now end with the
   macro, so `cmd[]` is byte-identical on 4570 and drops the token on D13.
3. **`scripts/build.sh`'s 515 clause** is tree-gated. On 4570 it stands (the string carries the name
   `arm_init` parses). On D13 it **publishes a skip** *and* requires the token to be **absent** from
   the payload — refusing in both directions ([[mi4-off-option-two-spellings]]: a token with no
   reader is output, not a switch; [[mi4-silence-is-a-reading-only-if-success-is-silent]]: a skip must
   not be able to hide a stray token).

## The D13 pipeline now builds a boot image

With 951 (entry probe site) + 952 (entry symbol) + 953 (boot arg), the **whole D13 pipeline runs to
completion**: the entry build links the D13 image, `build_entry.sh` emits the payload header, and
`scripts/build.sh` compiles and links the payload and packs `stage90.bin` / `stage90.elf` /
`stage90.img` / `stage90-qcdt.img` (9.3 MB). The 515 clause prints its D13 skip; the D13 image carries
**zero** `up_style_idle_exit=1` (verified in the packed image). `make check` exits 0.

4570 is unchanged at source level: the token carry is textually exact (`… no-pub-rtclock
up_style_idle_exit=1`, verified by preprocessing both branches), and `STAGE90_XNU_TREE_D13` defaults to
`0`. The 4570 objects are frozen on the `4570` branch (the standing directive), so 4570's
entry/payload were not rebuilt; a 4570 source-level rebuild through `out/` hits 948's stale-`assym.s`
gate, which is the known frozen-objects state and not this rung.

**Follow-up (954, the press path).** `scripts/preflight_boot_check.sh`'s 549 clause also looks for
`up_style_idle_exit` in the entry image; before any **D13 press** it must publish the same skip. It is
not the build path (pressing is the operator's), so it is recorded rather than changed here.

## Provenance

`scripts/build.sh`, `src/boot_args.c`, `src/stage90.h`, `src/stage90_main.c`. Host-side, reversible,
**no press**.