# 954 — the preflight gate follows the tree (2026-10-09)

The fourteenth rung in the tree-pin series ([[mi4-913-ios7-rebase-decision]]). 951/952/953 made the
**build** follow the selected XNU tree; the D13 pipeline now packs a boot image. The gate that stands
between a build and a **press** — `scripts/preflight_boot_check.sh` — was still written for 4570, and
its **549** clause looks for `up_style_idle_exit=1` in the boot image:

```
token 'up_style_idle_exit=1': ... in stage90-qcdt.img ...
FAIL: the boot image carries 'up_style_idle_exit=1' 0 time(s) ... where 515's repair must be in two
      command lines ...
```

On D13 the token does not exist (953): D13 ships no `up_style_idle_exit` global and no `caches.c`, and
its idle is gated by `do_power_save` (`pmCPU.c:41`), a compile default, not a boot argument. A D13 press
would have been refused by a clause whose subject the tree does not have.

## The repair — the gate learns which tree the image is

The tree is read from the **hash-bound entry record**, not the shell's environment: 533/954 already
bind `out/stage90/xnu_arm_entry-config.txt` to the entry bin by SHA-256, so a gate that reads
`STAGE90_XNU_TREE_D13` out of that file is talking about *this* image's tree and cannot be reading a
different shell (`mi4-build-variant-comes-from-an-env-default`).

1. **`src/entry/build_entry.sh`** — the config writer emits `STAGE90_XNU_TREE_D13=$D13_TRACE`, the same
   discriminator the build uses. The key is added to **`ENTRY_ARM_KEYS`** and its case arm to the record
   loop, because switching trees is a **deliberate change**: the same switches synthesized from a
   different tree link a different image (951's probe site, 952's entry symbol), so a record that did
   not carry the key would let a build notice nothing when the tree moved under it. The build's own
   678 two-way check refused the writer line until the two were added — the refusal message named the
   fix, and that is what was followed.
2. **`scripts/preflight_boot_check.sh`** — `_entry_tree_d13` is read from the record (default `0` = 4570
   for a pre-954 record); the key joins **`ENTRY_CFG_KEYS`**; the **549** clause is gated.

## The two-way rule at the gate

The **549** clause, on D13, **publishes the skip and still refuses a stray token**:

```
== the boot argument that selects the idle arm ==
549: SKIPPED on D13 - the idle-cache boot argument does not exist in this tree.
  D13's arm_init parses only maxmem/-no-cache/serial and ships no caches.c; its idle is gated
  by do_power_save (pmCPU.c:41), a compile default, not a boot argument. The boot image is
  confirmed NOT to carry up_style_idle_exit=1, so no idle arm is being claimed.
```

Skipping alone would be a silent success: an image that *did* carry `up_style_idle_exit=1` would be
claiming an arm D13 cannot take (`mi4-off-option-two-spellings`), and the skip would hide it. So the
branch first greps the boot image and `fail`s if the token appears. Measured both directions with the
real D13 image — the branch body run verbatim:

- real `stage90-qcdt.img` → the skip above, no failure;
- a one-line image containing `up_style_idle_exit=1` → `FAIL: this is a D13 image (the record says
  STAGE90_XNU_TREE_D13=1) but the boot image carries 'up_style_idle_exit=1' 1 time(s) ...`.

On 4570 (`_entry_tree_d13=0`) the original 549 body runs unchanged, so every existing park — none of
which is a D13 arm — gates exactly as before.

## The key is one-way at the gate, and why

`STAGE90_XNU_TREE_D13` is required **only** when the record carries it; a record without the line is
read as `0` (4570), **the only value any pre-954 build could have written** — the D13 line had no press
before 954, so no parked arm is a D13 arm. This is the same one-way rule as 911b's ceiling, 911c's SMEM
probe and 912's window (`882`'s own defect, now repaired eight times): demanding the key would make
every existing park unpressable, and those presses are still owed. A post-954 record can never reach the
absent branch: 678's two-way check refuses a record whose arm-key list omits the key, and the writer
emits the line unconditionally (`0` or `1`).

## Provenance

`src/entry/build_entry.sh` (writer + `ENTRY_ARM_KEYS` + record loop), `scripts/preflight_boot_check.sh`
(`_entry_tree_d13`, `ENTRY_CFG_KEYS`, the 549 gate). Host-side, reversible, **no press**. `make check` = 0.
The D13 image (`stage90-qcdt.img`) is a **new arm** and is not yet recorded in `records/revert-set.txt`;
recording it is the press-prep step, owed before any D13 press.