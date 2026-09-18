# Experiment 170 — XNU's own `PE_state` named the device tree: `xnu_entry_dtinit_base=0x00280000`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
MI4IOS6_STAGE90_XNU real XNU entry xnu_entry_dtinit_base=0x00280000
 stub_hit=DTInit

No errors detected
```

`0x00280000` is `ENTRY_DT_PA`, the address the payload copies the Apple-format device tree to. The
number in this line was not written by the payload: `DTInit`'s argument is `PE_state.deviceTreeHead`,
which `pexpert/arm/pe_init.c:293` copied out of `boot_args->deviceTreeP` eleven lines earlier, so
this is **XNU's own structure, read by XNU's own function, under XNU's own page tables, reporting
where XNU thinks the tree is** — and it agrees with where this project put it.

Reaching that line also measures everything before it. `PE_init_platform`'s `PE_state.initialized ==
FALSE` branch ran to completion: the eight `PE_state.video` fields decoded out of `boot_args->Video`,
and the `v_pixelFormat` assignment at `:302` — which is XNU's real `osfmk/arm/strlcpy.c` calling
XNU's real `osfmk/arm/strlen.s`, because exp-169's run said `strlcpy` was the first thing missing
and this image links both. Then, and only then, the call at `:310`.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
no contract reports a non-zero failure count, and the device returned to Android on its own. The
hardware watchdog was again the only net across the jump.

## One experiment, two objects, and why this one is not a rule being bent

Exp-169's run named `strlcpy`. Linking `osfmk/arm/strlcpy.o` alone would have produced
`stub_hit=strlen` — a name that `nm -u out/xnu_kernel_obj/osfmk_arm_strlcpy.o` had already printed
on the host, since its undefined set is exactly `memcpy` and `strlen` and `memcpy` is in the image
already. So both are linked:

| object | where it came from | size |
| --- | --- | --- |
| `osfmk/arm/strlcpy.c` | the device, exp-169's `stub_hit=strlcpy` | 68 B of text |
| `osfmk/arm/strlen.s` | `nm -u` on the object above | 8 B of text |

The rule the image grows by is that nothing is added because it looks relevant — not that exactly
one file is added per run. An object named by the device, plus the one symbol that object's own
undefined set names and that has no references of its own, is a chain of two measured facts; the
alternative was a hardware run spent reporting what a host tool already said. Both are leaves.

## What changed in the image

| | exp-169 | now |
| --- | --- | --- |
| XNU objects linked | 6 | 8 (`strlcpy.o`, `strlen.o`) |
| text | 52036 B | 52228 B |
| image | 131560 B | 131560 B |
| `.bss` | 0x00220070 – 0x00221e00 | unchanged |
| undefined symbols | 108 (94 functions, 14 storage) | 107 (93 functions, 14 storage) |

The count went down by one rather than by two because `strlcpy.o` brought `strlen` in with it — the
same shape exp-169 recorded when `pe_init.o` pushed the frontier from 86 to 108. Between the two
runs the list's membership changed by two entries and its size by one.

## The instrument did what it was written for

`DTInit` is not a generated stub. It is hand-written in `entry_stubs.c`, and defining it there is
what keeps it out of the generated set: pass 1 never sees it undefined, so the generator never
emits a version of it. That is the same mechanism `panic` uses, and it is how a stub can report a
*value* rather than only its name:

```c
int DTInit(void *base)
{
    entry_kv("xnu_entry_dtinit_base", (uint32_t)(uintptr_t)base);
    entry_stub_hit("DTInit");
}
```

It was written in exp-169, where it did not fire, and its comment there records the run that said
why. This is the run it was waiting for.

## The next edge, and the decision it forces

`DTInit` is defined by `pexpert/gen/device_tree.c`, and there are two of those in this workspace:

- `out/xnu_kernel_obj/pexpert_gen_device_tree.o` — **XNU 4570's own**, compiled by
  `tools/build_xnu_arm_kernel.sh` from `external/xnu-4570.1.46/pexpert/gen/device_tree.c`, 3956
  bytes. This is the one whose API matches `pe_init.o`: `DTEntry`, and `DTInitEntryIterator` for the
  iterator the 2050 version does not have.
- `out/stage90/xnu-objects/device_tree.o` — the **2050** reader that the *payload* links behind
  `STAGE90_XNU_REAL_DT`, 3880 bytes, with `DTCreateEntryIterator` instead.

Exp-106 already found that these two are not interchangeable, from the other direction: any 4570
caller linked against 2050's implementation will not link. Linking the 4570 object is the step that
makes XNU's own reader walk the tree this project built, and it is also the step that removes the
hand-written `DTInit` above — the link will report a duplicate symbol, which is the mechanism
`entry_stubs.c` documents for exactly this moment.

## Reproduce

```bash
# the entry image, with XNU's own objects
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 52228, image 131560, 107 undefined, 93 stubs

cp out/stage90/xnu_arm_entry_blob.c stages/stage90/xnu_arm_entry_blob.c
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)

(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'dtinit_base\|stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -3

# the two device_tree objects, side by side
arm-none-eabi-nm -P --defined-only out/xnu_kernel_obj/pexpert_gen_device_tree.o | grep '^DT'
arm-none-eabi-nm -P --defined-only out/stage90/xnu-objects/device_tree.o | grep '^DT'
```
