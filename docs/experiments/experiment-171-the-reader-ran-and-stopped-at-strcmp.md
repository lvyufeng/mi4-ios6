# Experiment 171 — XNU's own device-tree reader ran against this project's tree, and stopped at `strcmp`

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
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=strcmp

No errors detected
```

`strcmp` is reachable from exactly two places in this image, and the run cannot tell them apart:

- inside the device-tree lookup itself — `pexpert/gen/device_tree.c:375` compares a property's name
  while `FindChild` walks a node's property records (`:144` compares the value), so this is
  `DTFindEntry("name", "arm-io")` reading records out of the tree this project built;
- in `pe_identify_machine`'s first string comparison, `pexpert/arm/pe_identify_machine.c:58`, which
  is only reached if the lookup above *returned*, and returned non-zero.

Either way the reading is the same, and it is new: **XNU 4570's own reader executed against this
project's device tree, inside XNU's own boot path, and consumed node and property records out of
it.** Before this run nothing in the image could read the tree at all — `DTInit` was a stub.

The run stopped before any conclusion, because `strcmp` is the one thing in that chain the image
does not contain. So whether `arm-io` is *found* is still open; it is decided by the comparison
that did not run. `strcmp` is `osfmk/device/subrs.c` (30 bytes of text, already compiled as
`out/xnu_kernel_obj/osfmk_device_subrs.o`), and it is the next object.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in every contract that reports it,
and the device returned to Android on its own. The hardware watchdog was again the only net.

## Two objects, and the reason they travel together

| object | why it is here | size |
| --- | --- | --- |
| `pexpert/gen/device_tree.c` | defines `DTInit`, which exp-170's probe reported as the edge — and five of whose other symbols (`DTFindEntry`, `DTGetProperty`, `DTLookupEntry`, `DTInitEntryIterator`, `DTIterateEntries`) are in `nm -u` on the object beside it | 3956 B file, 4570's |
| `pexpert/arm/pe_identify_machine.c` | the call `PE_init_platform` makes next (`pe_init.c:311`) | 2299 B text |

The second one is not named by a run, so it has to earn its place a different way, and it does:
**4570's `DTInit` is two statements with no calls in it** —

```c
void DTInit(void *base) { DTRootNode = (RealDTEntry) base; DTInitialized = (DTRootNode != 0); }
```

— so an image that links only `device_tree.o` has exactly one possible outcome, the stub for
`pe_identify_machine` firing, and a run whose result the host can already state is not an
experiment. `pe_identify_machine` is also what makes the reader do something measurable: its first
act is `pe_arm_get_soc_base_phys()`, which finds `arm-io`, reads `device_type` and `ranges`, and
returns `ranges[1]`; a zero return makes the function give up before it reads `/cpus` at all.

It is **4570's** reader, not the 2050 one the payload links behind `STAGE90_XNU_REAL_DT`
(`out/stage90/xnu-objects/device_tree.o`, 3880 bytes, `DTCreateEntryIterator`). Exp-106 found from
the other side that the two are not interchangeable; this is the side where it matters, because the
caller is 4570's own `pe_init.o` and `DTInitEntryIterator` does not exist in the 2050 API.

## What changed in the image, and one thing that did not have to

| | exp-170 | now |
| --- | --- | --- |
| XNU objects linked | 8 | 10 |
| text | 52228 B | 56388 B |
| image | 131560 B | 131584 B |
| `.bss` | 0x00220070 – 0x00221e00 | 0x00220070 – 0x00221e40 |
| undefined | 107 (93 functions, 14 storage) | 103 (89 functions, 14 storage) |

The hand-written `DTInit` probe from exp-169/170 is gone, and the link is what said so: linking
`device_tree.o` defines `DTInit` for real, the duplicate symbol is reported, and the definition is
deleted. That is the mechanism `entry_stubs.c`'s header describes, working as documented. Its
successor in the same file probes one edge further on — `ml_parse_cpu_topology`, the statement
`arm_init.c:217` executes after `PE_init_platform` returns — and reports
`pe_arm_get_soc_base_phys()`, so the run that completes the walk prints the number XNU took out of
`arm-io`'s `ranges`. It did not fire here.

One dependency that needed no work at all: `pe_identify_machine.o` references
`gPEClockFrequencyInfo`, and that symbol is defined by **`pe_init.o`** — `B 140 98`, already in the
image. The clock structure XNU fills in with frequencies read from the tree is the real one, in the
same place a whole kernel would have it, not a stub sized by guesswork.

## What this run also put in the image

`pexpert/arm/pe_identify_machine.c` defines `pe_arm_init_interrupts`, `pe_arm_init_debug` and
`pe_arm_init_timer` — the three functions Phase 3's MSM8974 shim exists to replace (experiments
104, 134, 143, 147). They are now real code in the entry image rather than absent symbols. Nothing
calls them on this path (`PE_init_platform` calls `pe_arm_init_interrupts` only in its
`vm_initialized == TRUE` branch), so this run says nothing about whether they work — but the object
a Phase 3 wiring experiment has to wrap is now linked beside it rather than missing from it.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 56388, image 131584, 103 undefined, 89 stubs

cp out/stage90/xnu_arm_entry_blob.c stages/stage90/xnu_arm_entry_blob.c
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'stub_hit\|soc_base_phys\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -3

# the two paths that can reach the stub that fired
grep -n 'strcmp' external/xnu-4570.1.46/pexpert/gen/device_tree.c
grep -n 'strcmp' external/xnu-4570.1.46/pexpert/arm/pe_identify_machine.c | head -3

# the next object, and whether it is already built
arm-none-eabi-nm -A -P --defined-only out/xnu_kernel_obj/*.o | grep -E ': strcmp '
```
