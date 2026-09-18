# Experiment 172 — XNU's reader walked to a cpu node's `state` property, and stopped at `strncmp`

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
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=strncmp

No errors detected
```

## Why this one name is worth six claims

Exp-171 could not tell its two possible paths apart, because `strcmp` is reachable from both the
device-tree lookup and `pe_identify_machine`'s board-name chain. `strncmp` has no such ambiguity:

```
$ arm-none-eabi-nm -u out/xnu_kernel_obj/*.o | grep strncmp
pexpert_arm_pe_identify_machine.o:  U strncmp
osfmk_device_subrs.o:               U strncmp
```

The second is inside `strncasecmp`, whose only referents in the whole build are `bsd_net_necp.o`
and `bsd_vfs_vfs_subr.o` — neither in this image, so nothing can call it. The first is
`pexpert/arm/pe_identify_machine.c:117`, and it is the **only reachable `strncmp` in the image**.
The line it is on is:

```c
while (kSuccess == DTIterateEntries(&iter, &cpu)) {
    if ((kSuccess != DTGetProperty(cpu, "state", (void **)&value, &size)) ||
        (strncmp((char*)value, "running", size) != 0))
        continue;
```

Reaching it requires every step before it, so one line in the log settles six things about the
device tree this project built:

1. `pe_arm_get_soc_base_phys()` **returned non-zero** — otherwise `pe_identify_machine` returns at
   `:56` and nothing below ever runs. That means `DTFindEntry("name", "arm-io", ...)` succeeded, the
   node's `device_type` and `ranges` properties were both read, and `ranges[1]` was not zero.
2. The five `strcmp` comparisons against Apple's board names (`"s3c2410-io"`, `"integratorcp-io"`,
   `"olocreek-io"`, `"omap3430sdp-io"`, `"s5i3000-io"`) all ran, with exp-172's real `strcmp`, and
   matched none — so `use_dt = 1`. The tree's `arm-io` device type is not one of Apple's.
3. `DTLookupEntry(NULL, "/cpus", &cpus)` returned `kSuccess`: `/cpus` exists.
4. `DTInitEntryIterator(cpus, &iter)` returned `kSuccess`.
5. `DTIterateEntries(&iter, &cpu)` returned `kSuccess` at least once: `/cpus` has a child.
6. `DTGetProperty(cpu, "state", ...)` returned `kSuccess`: **that child has a `state` property.**

The sixth is the one worth the most, because it is the requirement this project's own tool found by
reading XNU rather than by running it — `tools/xnu_dt_requirements.py`, written in Phase 2, whose
finding was that XNU silently skips a cpu node that has no `state`. That prediction is now measured
on hardware, by XNU's own reader, on the tree this project built.

Two things did *not* happen, and they are informative too. No `kalloc_canblock` stub fired, so the
root-to-`/cpus` path walk took no allocation; and no exception vector fired, so every offset the
reader computed into our tree was a valid address.

Nothing was flashed: `persistent_write_attempted = 0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.

## What changed in the image

| | exp-171 | now |
| --- | --- | --- |
| XNU objects linked | 10 | 11 (`osfmk/device/subrs.o`) |
| text | 56388 B | 59492 B |
| image | 131584 B | 131608 B |
| `.bss` | 0x00220070 – 0x00221e40 | 0x00220070 – 0x00221e80 |
| undefined | 103 (89 functions, 14 storage) | 106 (92 functions, 14 storage) |

Measured by linking an empty object in place of `subrs.o` and diffing the two undefined sets, rather
than by counting:

```
resolved by subrs.o:  strcmp
added by subrs.o:     __MALLOC  strncat  strncpy  strnlen
```

103 − 1 + 4 = 106. `osfmk/device/subrs.c` is 3036 bytes of text and defines thirty-odd functions —
`atoi`, `itoa`, `strcasecmp`, `strchr`, `strnstr`, `STRDUP` and the `__*_chk` family among them —
for exactly one symbol this image wanted. The frontier is a closure, not a shopping list: an object
brought in for one name arrives with all of its own.

## The next object

`strncmp` is `osfmk/arm/strncmp.s`, assembled as `out/xnu_asm_obj/strncmp.o` — already built, like
`strlen.o` before it. With it linked the loop compares the `state` property it just read against
`"running"`, and either the comparison matches and the reader goes on to `timebase-frequency`,
`bus-frequency` and `memory-frequency` from the same node, or it does not and the iteration moves
to the next cpu child.

The probe added in exp-171 is still in place and still has not fired: `ml_parse_cpu_topology`, the
statement `arm_init.c:217` executes once `PE_init_platform` returns, reporting
`pe_arm_get_soc_base_phys()`. That number — the `ranges[1]` XNU took out of `arm-io` — is what the
log will carry when the walk finishes.

## Reproduce

```bash
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
#   ... entry point 0x00200074, text 59492, image 131608, 106 undefined, 92 stubs

cp out/stage90/xnu_arm_entry_blob.c stages/stage90/xnu_arm_entry_blob.c
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -n 'stub_hit\|soc_base_phys\|No errors detected' /tmp/cancro-last_kmsg.txt | tail -3

# every reference to the symbol that fired, and whether it can be reached
arm-none-eabi-nm -A -u out/xnu_kernel_obj/*.o | grep strncmp
arm-none-eabi-nm -A -u out/xnu_kernel_obj/*.o | grep -E 'strncasecmp|strprefix'
```
