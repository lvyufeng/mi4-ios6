# Experiment 174 — `pe_init_debug` ran, and the frontier is `PE_boot_args`

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
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=PE_boot_args

No errors detected
```

## What it says

`PE_boot_args()` is the command-line accessor in `pexpert/arm/pe_bootargs.c` (14 bytes of text), and
in this image only one call leads to it: `PE_parse_boot_argn` → `PE_parse_boot_argn_internal` →
`PE_boot_args()`, reached from `pe_init_debug`'s first `PE_parse_boot_argn("debug", ...)`
(`pexpert/gen/pe_gen.c:53`). So the run says:

- `pe_init_debug` executed — the last statement of `PE_init_platform` (exp-173's edge) is now real
  XNU code rather than a stub;
- `PE_parse_boot_argn`, which parses the command line this payload put in `boot_args`, is real too
  (`pexpert/gen/bootargs.c`) and is now at the point of *asking for* that command line.

It stopped there because the accessor itself is the one thing in the chain not yet in the image.
Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## Two objects, and what they moved

`pexpert/gen/pe_gen.o` was named by exp-173's run; `pexpert/gen/bootargs.o` was named by `nm -u` on
it, since `pe_init_debug` is four `PE_parse_boot_argn` calls and a bitmask. Measured by linking an
empty object in place of both and diffing the undefined sets:

```
resolved:  appleClut8  PE_get_default  pe_init_debug  PE_init_printf  PE_parse_boot_argn
added:     cnputc  Debugger  IODTGetDefault  PE_boot_args  vcattach
```

105 − 5 + 5 = 105 again, and the storage-stub count went **14 → 13** without anything being
deleted: `appleClut8` was a stand-in sized 0x300 from `nm -S`, and `pe_gen.o` defines it as a real
`D 4 300` symbol — the same size, which is a small independent check that the stub-sizing rule has
been right.

| | exp-173 | now |
| --- | --- | --- |
| XNU objects linked | 12 | 14 (`pe_gen.o`, `bootargs.o`) |
| text | 59684 B | 62308 B |
| image | 131608 B | 132384 B |
| `.bss` | 0x00220070 – 0x00221e80 | 0x00220378 – 0x00221e48 |
| undefined | 105 (91 functions, 14 storage) | 105 (92 functions, 13 storage) |

The bss moved because `pe_gen.o` carries 772 bytes of `.data` — `appleClut8`'s 0x300-byte palette
and the `.L.str` string literals — which the linker places ahead of the bss. The window is still far
from full: 254392 bytes of headroom under `topOfKernelData`.

## Two things this run now makes visible that are not about the frontier

- `PE_init_printf` is real. `arm_init.c:337` calls `PE_init_kprintf(FALSE)`, and `PE_init_printf`'s
  `!vm_initialized` branch is `PE_putc = cnputc` — one assignment, and `cnputc` is a stub today. The
  console path is therefore two symbols from being wireable, not a subsystem away.
- `PE_parse_boot_argn` running at all means the command line this project puts in `boot_args`
  (`build.sh`'s `CMDLINE_BASE` plus the cache token and the two `no-*` claims) is about to be parsed
  by XNU's own parser. What XNU does with it is measured by what it does *next*, not by a symbol.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 62308, image 132384, 105 undefined, 92 stubs

cp out/stage90/xnu_arm_entry_blob.c stages/stage90/xnu_arm_entry_blob.c
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'stub_hit\|soc_base_phys\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -3

# the call chain that reaches the symbol that fired
grep -n 'PE_boot_args\|PE_parse_boot_argn' external/xnu-4570.1.46/pexpert/gen/bootargs.c | head
sed -n '50,60p' external/xnu-4570.1.46/pexpert/gen/pe_gen.c

# the next object, and its size
ls -l out/xnu_kernel_obj/pexpert_arm_pe_bootargs.o
arm-none-eabi-size out/xnu_kernel_obj/pexpert_arm_pe_bootargs.o
```
