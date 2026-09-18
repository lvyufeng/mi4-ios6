# Experiment 169 — the image crossed the old page-table limit, and `PE_init_platform` stopped at `strlcpy`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU xnu_entry_args_topOfKernelData=0x00260000
MI4IOS6_STAGE90_XNU xnu_entry_bss_start=0x00220070
MI4IOS6_STAGE90_XNU xnu_entry_bss_end=0x00221e00
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=strlcpy

No errors detected
```

The prediction was `stub_hit=DTInit` and the device said `strlcpy`. That is the correction this run
bought, and it is a source-reading error on my part rather than a surprise: `PE_init_platform`'s
`PE_state.video.v_pixelFormat` assignment is `pexpert/arm/pe_init.c:302` and the `DTInit` call is
`:310`, so the first thing in the function that this image does not provide is eight lines *earlier*
than the one the previous experiment's document named. The stub that fired is the frontier, and the
frontier is not where reading the calls in the order you remember them puts it.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in every contract that reports it.
The device returned to Android on its own and reported no errors. As in exp-168 the hardware
watchdog was the only net across the jump — the software dead-man cannot survive `_start`
switching TTBR0/TTBR1, which is why the gate says so explicitly.

## The image now crosses a limit, which is why the limit moved first

`pexpert/arm/pe_init.o` is 34927 bytes of text against exp-168's 26744 bytes of headroom, so this
experiment had to raise `topOfKernelData` rather than fit under it — and the numbers above show the
move was load-bearing rather than tidy:

```
  old topOfKernelData   0x00220000   <- where _start built its tables in exp-168
  new topOfKernelData   0x00260000
  image bss             0x00220070 .. 0x00221e00
```

The bss begins 0x70 bytes *past* the old limit and ends well inside the 40 KB of table entries
`start.s`'s invalidation loop writes there, so under the old value every byte of this image's bss
would have been inside XNU's own first-level page tables. The run completing is what says the new
value is in force on the device and not only in the build.

`osfmk/arm/start.s:149` loads the value out of `boot_args` into TTBR0 and TTBR1 and then writes
table entries over the 40 KB (ten pages) that begin there: four L1 pages for each of the two
translation tables, the L2 page it steals for a non-1MB-aligned end, and the high-exception-vector
page table at `+PGBYTES*9`. So there are two ways to fail and both are now checked at build time:

| | before | now |
| --- | --- | --- |
| `topOfKernelData` | `0x00220000` | `0x00260000` |
| where the number lives | `xnu_entry_jump.c`, and again in `build_entry.sh` | `stage90.h` only |
| image past the limit | checked | checked |
| limit so high the tables reach the tree | not checked | checked |

The device tree is the thing above the tables (it is copied to `ENTRY_BASE + 0x80000`), so a limit
raised too far would corrupt it from the other end. `ENTRY_DATA_LIMIT + ENTRY_TABLE_BYTES >
ENTRY_DT_OFFSET` is now a link failure, with `ENTRY_TABLE_BYTES = 0xA000`.

## One value, two definitions — removed rather than repeated

`build_entry.sh` used to carry `ENTRY_DATA_LIMIT=0x00020000` while `xnu_entry_jump.c` carried
`STAGE90_XNU_ENTRY_BASE + 0x00020000u`, with a comment on each saying it was the same number as the
other. This project has a name for that defect class, and this is the third number it has cost.

Both offsets now live in `stages/stage90/stage90.h`:

```
STAGE90_XNU_TOP_OF_KERNEL_DATA_OFFSET   0x00060000u
STAGE90_XNU_ENTRY_DT_OFFSET             0x00080000u
```

`xnu_entry_jump.c` uses them for the `boot_args` it builds; `build_entry.sh` reads them back out of
the same header with `-E -dM -include stage90.h`, the idiom the payload's `build.sh` already uses
for its switches, and refuses to build if either is missing. Neither side repeats the number.

## One more thing the default build found on the way

Building the entry image *without* the switch — the habit this project's notes keep insisting on —
failed, and not because of anything this experiment changed:

```
./build_entry.sh: line 81: STUB_DEFINES: unbound variable
```

`STUB_DEFINES=()` is an empty array when `STAGE90_ENTRY_REAL_ARM_INIT` is unset, and `set -u` plus
an unquoted `$STUB_DEFINES` is an unbound-variable error in bash 5.1 — the 4.4 fix covers
`"${arr[@]}"`, not `$arr`. So the default entry image had not been built since the switch was
added in exp-159, and could not be. `"${STUB_DEFINES[@]}"` is the fix. The default image builds
now: text 9728, image 9752, 317016 bytes of headroom under the new limit.

This is the same shape as the regression this project already has a note about — a run of
switch-enabled builds hides the default — and it is the second time the default configuration has
been the broken one.

## The instrument written ahead of the edge

`entry_stubs.c` gained a hand-written `DTInit` for the run *after* this one:

```c
int DTInit(void *base)
{
    entry_kv("xnu_entry_dtinit_base", (uint32_t)(uintptr_t)base);
    entry_stub_hit("DTInit");
}
```

Its argument *is* `PE_state.deviceTreeHead` — `pe_init.c:293` copied it out of
`boot_args->deviceTreeP` eleven lines earlier — so when it fires the log will carry the address
XNU's own structure holds, read by XNU's own function, rather than the address this project wrote
into `boot_args` and then logged itself. It did not fire this run, and that is now recorded in its
comment along with the run that says why. Anything defined in `entry_stubs.c` is automatically
excluded from the generated stubs (pass 1 never sees it undefined), which is the same mechanism
`panic` already used.

## What changed in the image

| | exp-168 | now |
| --- | --- | --- |
| XNU objects linked | 5 | 6 (`pexpert/arm/pe_init.o`) |
| text | 17120 B | 52036 B |
| image | 98688 B | 131560 B |
| `.bss` | 0x00218008 – 0x00219788 | 0x00220070 – 0x00221e00 |
| generated stubs | 86 (76 functions, 10 storage) | 108 (94 functions, 14 storage) |
| `topOfKernelData` | 0x00220000 | 0x00260000 |
| headroom below it | 26744 B | 254464 B |

The stub count went *up* while the image grew by one object, which is the shape of this method:
each object brings its own undefined references, and the image's stub list is the closure of
`arm_init`, so it grows as a frontier rather than shrinking as a measure of what is missing from
XNU. `PE_init_platform` itself is no longer a stub; `DTInit`, `DTFindEntry`, `DTGetProperty`,
`DTLookupEntry` and `pe_identify_machine` — the calls it is about to make — are.

(The exp-168 figure in the right-hand column is `0x00220000 - 0x00219788`, which is 26744. That
document said 26760; this one is the arithmetic, and the earlier line has been corrected in place.)

## What is above the new limit, and why the limit was not raised further

`0x60000` leaves 384 KB for the image and ends `0x10000` below the device tree, which is checked.
The tree itself is safe for a reason worth stating: `PE_state.deviceTreeHead = boot_args->deviceTreeP`
is the tree the payload copies to `0x00280000`, inside the 2 MB window `_start` maps as
`[physBase, physBase + memSize)`. That is why XNU's own reader can walk it at all once the object
that implements it is linked — and it is why the tree cannot simply be moved out of the image's way.

## The next edge

`osfmk/arm/strlcpy.o` — `out/xnu_kernel_obj/osfmk_arm_strlcpy.o`, already compiled, 68 bytes of
text and no external references of its own. Linking it puts `PE_init_platform` past `:302`, and
`DTInit`'s probe (above) is what reports the next thing it does.

## Reproduce

```bash
# the entry image, with XNU's own objects
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 52036, image 131560, bss end 0x00221e00, 108 stubs
#        data limit 0x00060000, headroom 254464

# the payload embeds a *copy* of that image; the copy is the producer's
cp out/stage90/xnu_arm_entry_blob.c stages/stage90/xnu_arm_entry_blob.c
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)

# the gate, then the non-persistent boot, in one command (never flashes)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'stub_hit\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -3

# the two limits, as the build reads them
arm-none-eabi-gcc -E -dM -I stages/stage90 -include stage90.h - </dev/null |
    grep -E 'STAGE90_XNU_(TOP_OF_KERNEL_DATA|ENTRY_DT)_OFFSET'

# which object defines the next edge, and whether it is already built
ls -l out/xnu_kernel_obj/osfmk_arm_strlcpy.o
sed -n '300,312p' external/xnu-4570.1.46/pexpert/arm/pe_init.c
```
