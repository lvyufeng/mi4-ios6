# Experiment 175 — The entry image is derived, not written down twice

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU xnu_entry_device_tree_pa=0x00600000
MI4IOS6_STAGE90_XNU xnu_entry_args_topOfKernelData=0x00400000
MI4IOS6_STAGE90_XNU xnu_entry_bss_end=0x00221e48
MI4IOS6_STAGE90_XNU xnu_entry_top_of_kernel_data=0x00400000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=PE_boot_args

No errors detected
```

`stub_hit=PE_boot_args` is exactly what exp-174's run said, and that is the point of this
experiment: the frontier did not move. Everything here is a change to how the entry image and the
layout above it are *built and described*, and the control that says so is that the device produced
the same observable result, at the same addresses, with the same next edge.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## What was wrong

Two things, and they are the same thing twice.

**The layout above the image existed in three places.** `STAGE90_XNU_ENTRY_SIZE`,
`STAGE90_XNU_TOP_OF_KERNEL_DATA_OFFSET` and the device-tree offset were constants in `stage90.h`,
repeated as arithmetic in `build_entry.sh`, and the entry window was two literal sections in
`mmu.c`. Experiment 168 wrote them down; experiment 169 spent its whole run moving them by hand
because the image had grown past the value one copy still held. That is
[the project's standing defect class](../experiment-151-force-includes-and-stdbool.md) —
and this time the fix is structural rather than another correction: the numbers are now *derived
from the link* by `build_entry.sh` after it links, written into a generated `out/stage90/xnu_arm_entry.h`,
and read from there by the payload. There is one definition and it is downstream of the image.

**The entry image was committed as a hex array.** `stages/stage90/xnu_arm_entry_blob.c` was
1332328 bytes of `0x..u,` on disk — the linked image as C source — with `build.sh` `cmp`-ing it
against the binary in `out/` to catch the two drifting, and failing when they had. That `cmp` firing
is what started this experiment: the default `./build.sh` had been failing since the image was
rebuilt without re-installing the array. The check was real and it caught a real thing; the shape
was wrong. The image is up to 5 MB of someone else's machine code, the repository already ignores
binaries, and it is reproducible from committed sources by one command. From here there is one copy
— the binary in `out/` — and the array the payload compiles is generated from it on every build, so
a payload cannot embed an image other than the one `out/` holds.

## The accounting

Derived from `__bss_end = 0x00221e48` on this link, `ENTRY_BASE = 0x00200000`:

| | value | rule |
| --- | --- | --- |
| image | `0x00200000` – `0x00221e48` | the link |
| boot_args | `+143360` (`0x23000`) | first page above `__bss_end` |
| `topOfKernelData` | `+2097152` (`0x200000`) | 1 MB aligned, ≥ 1 MB above the args |
| device tree | `+4194304` (`0x400000`) | 2 MB above the tables |
| window | `8388608` (`0x800000`) | smallest power of two covering the tree buffer, ≥ 2 MB |

Headroom below `topOfKernelData` is **1958328 bytes** (`0x1de1b8`), up from 254392 in exp-174 — the
window doubled because the tree sits at `+4 MB` and its 128 KB buffer has to be inside it, and the
4 MB of the window between the tables and the tree is now mapped and described even though nothing
lives there. That is a deliberate trade: `memSize` is 8 MB, and `_start` walks it in sections
setting up page tables, so the cost of the empty span is that walk and nothing else. It also buys
the room the next several objects need — the 2 MB window had 254 KB left in it.

Four invariants are checked before anything is written, and each names what it protects:

```
the image ends <n> bytes from the base, past topOfKernelData at <m>
the boot_args at <n> do not fit below topOfKernelData at <m>
the <n> bytes of tables at <m> reach the tree at <k>
the tree buffer at <n> (+<m>) is outside the <k> window
```

Both failure modes are silent corruption with no cause in the log — an image reaching past
`topOfKernelData` is overwritten by XNU's own 40 KB of table entries a few instructions into the
boot, and a limit high enough that the tables reach the tree does the same thing to the tree — so
they are refused at build time rather than discovered on the device. The generated header states its
own constants (`ENTRY_TABLE_BYTES = 0xA000`, `ENTRY_DT_MAX = 0x20000`, `ARGS_BYTES = 0x1000`) so the
payload's `mmu.c` loop and `xnu_entry_jump.c`'s placement read the same values the checks used.

The payload's identity map is now a loop over `__stage90_image_end` rather than two literal
sections, and the entry window a loop over `STAGE90_XNU_ENTRY_SIZE` rather than two literal
sections. Both had grown by hand-editing once already.

## What did not change, which is what makes this a refactor

- The generated byte array is **byte-for-byte identical** to the committed one it replaced
  (`132384` bytes, compared element by element before the old file was deleted).
- The payload disassembles to `0x00200000` / `0x00400000` / `0x00800000` where
  `xnu_entry_jump.c`'s placement and `mmu.c`'s window loop now read their macros.
- The device reproduced exp-174's `PE_boot_args`, from the same `bss_end`, with the same
  `topOfKernelData`.
- The default `./build.sh` — which had been failing since the image was last relinked, on the
  `cmp` this experiment removes — builds again, unswitched.

## One thing this experiment found on the way

`build_entry.sh` had not been building in its **default** configuration since exp-159:
`STUB_DEFINES=()` plus `set -u` plus an unquoted `$STUB_DEFINES` is an unbound-variable error, and
the default path is the one that uses the array (the `REAL_ARM_INIT` path replaces it). Every run
since has passed `STAGE90_ENTRY_REAL_ARM_INIT=1`, which is why it was never seen — the same
"the switch-enabled build hides the default" regression this project has a note about. Fixed to
`"${STUB_DEFINES[@]}"`.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   entry point 0x00200074, text 62308, image 132384, 105 undefined, 92 stubs
#   layout args +143360, topOfKernelData +2097152, tree +4194304, window 8388608
#   headroom 1958328 bytes below topOfKernelData

cat out/stage90/xnu_arm_entry.h          # the one definition, written after the link

# the default build now works again, and the switched one embeds out/'s binary
(cd stages/stage90 && ./build.sh)
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -3

# the layout the payload was built with, out of the instruction stream
arm-none-eabi-objdump -d out/stage90/stage90.elf | grep -n '0x200000\|0x400000\|0x800000' | head

# the invariants, by moving the image into them
grep -n 'layout_fail' -A2 stages/stage90/xnu_arm_boot/build_entry.sh
```
