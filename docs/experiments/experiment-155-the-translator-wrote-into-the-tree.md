# Experiment 155 — the assembler translator was writing into Apple's tree, through a symlink

Date: 2026-09-18
Host only — nothing here runs on the device.
Artifacts: `tools/assemble_arm_layer.sh`, and four files in `external/xnu-4570.1.46` reverted

| | before | after |
| --- | --- | --- |
| `git -C external/xnu-4570.1.46 status --short` | **4 files modified** | 0 |
| `stages/stage90/build.sh` | **exit 1**, stopped at the compile-graph scan | exit 0, all images produced |
| `stage90_xnu_compile_graph_no_external_mutation` | `0x00000000` | `0x00000001` |
| manifest `.s` assembling | 17 of 17 | 17 of 17 |
| the 17 objects | — | **byte-identical** |

## How it was found

Not by looking for it. `stages/stage90/build.sh` refused to complete, and the reason it gave was
one of its own fail-closed markers:

```
stage90_xnu_compile_graph_no_external_mutation=0x00000000
stage90_xnu_compile_graph_failure_mask=0x80000000
```

`xnu_compile_graph_scan.py:503` computes that bit by running `git status --short` inside every
checkout under `external/`, and `external/xnu-4570.1.46` **is a git repo of its own**. Four files in
it were modified:

```
 M osfmk/arm/WKdmData_new.s
 M osfmk/arm/data.s
 M osfmk/arm/lz4_decode_armv7NEON.s
 M osfmk/arm/machine_routines_asm.s
```

— the exact four files `experiment-150` is about. The project's rule is that XNU's source is never
modified, and both the tool's own header comment and the note written about experiment-150 say so in
as many words:

> **This is a dialect translation, not a source change**: the tree is never written to.

## The mechanism, and why reasoning about it was not enough

`assemble_arm_layer.sh` needed two things at once: translated copies of the `.s` files, and XNU's
*relative* includes to keep resolving — `bsd/dev/arm/cpu_in_cksum.s:50` is
`#include "../../../osfmk/arm/arch.h"`, so a copy has to sit at the same depth under the tree root.
It got both by mirroring the components into its output directory:

```bash
for d in osfmk bsd libkern iokit pexpert security san libsa EXTERNAL_HEADERS; do
    [[ -d $XNU/$d ]] && ln -sfn "$XNU/$d" "$OUT/translated/$d"
done
...
if [[ $("$TRANSLATE" "$src" "$OUT/translated/$rel") -gt 0 ]]; then
```

`$OUT/translated/osfmk` **is** `$XNU/osfmk`. So `open("$OUT/translated/osfmk/arm/data.s", "w")` is
`open("$XNU/osfmk/arm/data.s", "w")` — the translator's output file was Apple's source file, and the
translation replaced it on every run. `translate_arm_asm.py:120` even does
`os.makedirs(os.path.dirname(dst), exist_ok=True)`, which through a symlink creates directories
inside the tree.

First attempt at confirming it was a `git checkout` of the four files, which reported the tree clean
— and was wrong to believe, because the next step re-dirtied it. That is the actual measurement:

```
$ git -C external/xnu-4570.1.46 checkout -- osfmk/arm/{data.s,machine_routines_asm.s,
      lz4_decode_armv7NEON.s,WKdmData_new.s}
$ git -C external/xnu-4570.1.46 status --short          # (nothing)
$ ./tools/assemble_arm_layer.sh
assemble: 17 ok, 0 failed; 26 symbol(s) de-underscored
$ git -C external/xnu-4570.1.46 status --short
 M osfmk/arm/WKdmData_new.s
 M osfmk/arm/data.s
 M osfmk/arm/lz4_decode_armv7NEON.s
 M osfmk/arm/machine_routines_asm.s
$ sha256sum external/xnu-4570.1.46/osfmk/arm/data.s
540e1c49335535e708d296cabc68b187d55df400b953632a5008dc1e412883fa      # identical to the old copy
```

**The tool reverted the tree and the tool un-reverted it, in one script.** A run of the script is
what makes the difference, and a `git status` taken before rather than after one says nothing.

## The fix

The mirror is built the other way round: **directories are real, files are symlinks into the tree**,
and only the directories on the path to a file that actually needs translating stop being symlinks.
Two small functions do it, and it is worth noting that they are short because the property wanted is
simple — *no path this script opens for writing may pass through a symlink*:

```bash
materialize_dir() {   # "" for the tree root: real directory, entries symlinked in
    [[ -L $dst ]] && rm -f "$dst"
    mkdir -p "$dst"
    for e in "$src"/*; do ...; ln -sfn "$e" "$dst/$b"; done   # unless already there
}
materialize_for() {   # make every directory above a file real, root first
```

and the translation now goes to a scratch file first, so a directory is only made real when the file
in it needed translating:

```bash
n=$("$TRANSLATE" "$src" "$OUT/translated.tmp")
if [[ $n -gt 0 ]]; then materialize_for "$rel"; mv -f "$OUT/translated.tmp" "$OUT/translated/$rel"; ... fi
```

`materialize_for` is called root-first so that replacing a symlink with a directory never orphans a
path already built beneath it, and `materialize_dir` never overwrites an entry that exists — which
is what keeps an earlier translated copy in place when a later file re-materialises its directory.

## The evidence that the in-tree edits were pure leakage

The fear in a case like this is that the tree edit was doing something the tool does not — in which
case reverting it breaks the build. It is not:

| | result |
| --- | --- |
| manifest `.s` that assemble, from a **pristine** tree | **17 of 17** |
| the 17 objects vs the ones built from the edited tree | **byte-identical**, all 17 |
| symbols defined by `data.o`, `machine_routines_asm.o`, `lz4_decode_armv7NEON.o`, `WKdmData_new.o` | identical |
| `git status` in the tree **after** a full run | clean |
| `stages/stage90/build.sh` | **exit 0**; `stage90.bin`, `stage90.img`, `stage90-qcdt.img` produced |
| the three `*_no_external_mutation` markers | `0x00000001` |

So the two mechanisms were the same mechanism. `translate_arm_asm.py` covers exactly the constructs
the hand edits had put in the tree — `.const` and `.section __DATA, __c…` to GNU section directives,
and parameterless `.macro`s whose bodies use `$N` to GNU named parameters with `\p0\()` — which is
also why the edits were **idempotent**: the second run found nothing left to substitute on its own
output and rewrote the file with identical bytes.

It also means `experiment-150`'s headline number — 17 of 17 — was measured with an already-mutated
tree. That number still stands, and now it stands for a reason it did not have before: it was
re-measured against the original source.

## What made this possible, and what would have prevented it

**A claim in a comment is not a check.** The tool asserted the property it was violating, in a
sentence written by the same person who wrote the code, in the same file. The project has a habit of
recording *why* something is safe; here the recording was doing the work instead of the code.

**The check already existed and was already wired in.** `xnu_compile_graph_scan.py`'s
`external_clean` gate is four lines of `git status`, it is run by `build.sh` before anything is
built, and it had been reporting the violation for as long as the violation existed — as a
`failure_mask` bit in a wall of thirty markers, which is exactly the shape this project has learned
to mistrust. It was found because the payload build *stopped*, not because the number was read.

One stale artifact was removed while checking: `out/xnu_asm_translated/` held eight top-level
symlinks into the tree and no real files — a mirror from an earlier design of this same mechanism,
which had also never produced anything except by writing through those symlinks.

## What is left of the four files now

Nothing. They are back to their published contents, the tree is clean, and the four translated copies
live in `out/xnu_asm_obj/translated/` where the build puts them. **The rule "XNU's source is never
modified" now holds for the assembly path as written, and not only as intended.**
