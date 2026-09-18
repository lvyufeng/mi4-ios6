# Experiment 198 — `patch_low_glo` Runs, `arm_vm_init` Returns, and the Frontier Is `printf_init`

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
 xnu_entry_kv_written=0x00000016
 xnu_entry_kv_in_dram=0x00000016
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=printf_init

No errors detected
```

`stage90_xnu_entry_stub_no_exception=0x00000001`, `xnu_entry_checks=5`,
`xnu_entry_failures=0x00000000`, and `persistent_write_attempted=0x00000000` in all 25 contracts
that report it. The device returned to Android on its own.

`printf_init` is `osfmk/arm/arm_init.c:328`, and it is the **first statement after
`arm_vm_init(xmaxmem, args)` returns**:

```
	arm_vm_init(xmaxmem, args);

	uint32_t debugmode;
	if (PE_parse_boot_argn("debug", &debugmode, sizeof(debugmode)) &&
	   ((debugmode & MIN_LOW_GLO_MASK) == MIN_LOW_GLO_MASK))
		patch_low_glo();

	printf_init();
```

So this run closed out `arm_vm_init` and took two more steps through `arm_init`:

- **`arm_vm_init` returned.** Experiment 197's stop was its unconditional last instruction; this
  run's stop is six lines further on in `arm_init`, and the only way to get there is to fall off the
  end of `arm_vm_init` and come back. XNU's entire early-VM function is now executed, not entered.
- **`patch_low_glo()` ran for real.** Not by luck: `MIN_LOW_GLO_MASK` is `0x144`
  (`arm_init.c:131`), the condition is `(debug & 0x144) == 0x144`, and the payload's own command line
  has `debug=0x144` in it - the same boot-arg `PE_init_debug` acts on, and visible in the captured
  log. So `lowGlo.lgStext = vm_kernel_stext` was executed with the address XNU's `arm_vm_init`
  assigned. It wrote into the 988-byte `lowGlo` this experiment made real.
- **`PE_parse_boot_argn("debug", ...)` ran** and returned true, which is the branch that made the
  second point happen rather than the third.

`kv_written == kv_in_dram == 0x16` - the evidence path is still carried by two independent routes
and they still agree.

## What the object cost

`osfmk_arm_lowmem_vectors.o` - 72 bytes of text, 988 of data, 0 of `.bss`, 6 references:

```
resolved (5):  lowGlo patch_low_glo patch_low_glo_static_region patch_low_glo_vm_page_info version
added    (2):  kdp_trans_off kmod
359 -> 356 undefined
```

Both directions link, taken by standing an empty object in for `osfmk_arm_lowmem_vectors.o`.

The shape of this step is experiment 197's, one level up. Before it, `lowGlo` was a **988-byte,
zero-filled storage stub** - correctly sized, from `nm -S` on the object that defines it, and empty.
The real one is the same 988 bytes with addresses in it, which is the whole difference between a
structure a debugger can use and a hole. `nm -S` cannot tell you that, and the generator's guard
checks size only; see [[mi4-stand-in-size-is-not-value]] for the sibling case where the symbol is a
pointer.

Two of the six references were neither: `version` and `osversion`.

## The two stand-ins that were the wrong kind, and why they are defined here

`libkern/libkern/version.h.template:105-109` declares

```c
extern const char version[];
#define OSVERSIZE 256
extern char osversion[];
```

and `config/version.c:3,17` defines them - but that file is a *template*. Its strings carry Apple's
`###KERNEL_VERSION_LONG###`, `###KERNEL_BUILD_DATE###` and `###KERNEL_BUILDER###` placeholders,
substituted by a build step this project does not perform, so the file is never compiled here and
neither name appears in any object in this project's pool.

The generator's `case` reads the symbol's type and size from that pool, finds neither, and falls
through to the function branch: `void version(void) { entry_stub_hit("version"); }`. That links, and
nothing in this image trips over it - `lowmem_vectors.c:37-41` only takes the *addresses* of both
names. It is still a stand-in that lies about what it is: two functions in the linker map where the
kernel has two strings, and code where a print would go.

So both are defined in `entry_stubs.c`, at the type and size `version.h.template` states - a
`const char version[]` and a `char osversion[256]` - and the string is written to look like the
placeholder it is, because XNU's real value is assembled from the build date and builder of a build
this project does not perform, and a plausible-looking date would be a number no measurement
produced.

## The correction this experiment carries

Experiment 197's "what is next" section named the wrong obstacle. It said `kmod` was `B` with
`nm -S` size 0 and that `build_entry.sh` would therefore refuse the build; both halves were wrong.
They were wrong for one reason: `nm -S -P` prints **`name type value size`**, so the size is field
`$4` - and the check that produced the claim read `$3`, the **value**. `kmod`'s value is 0 and its
size is 4. The generator that would have run reads `$4` and is right, and it handled both
`kmod` (4) and `kdp_trans_off` (4) without comment, which is why the "added (2)" line above is two
storage symbols and not a build failure.

That is the same shape as everything in `mi4-measurement-defects`: a number read out of a tool's
output in a format the tool does not print, then stated as a fact about what would happen. The
section in experiment 197 is rewritten with the measured table, and the reproduce block there now
re-runs the sizing rather than remembering it.

## Cost

| | exp-197 | now |
| --- | --- | --- |
| entry objects linked | 37 | 38 (`osfmk/arm/lowmem_vectors.o`) |
| entry text | 224748 B | 224716 B |
| entry image | 297240 B | 313120 B |
| entry `.bss` | 0x002485d8 – 0x00250088 (31408 B) | 0x0024c3e0 – 0x00253c08 (30760 B) |
| undefined | 359 | 356 |
| stubs | 307 functions, 52 storage | 303 functions, 53 storage |
| boot_args offset | +335872 | +348160 |
| headroom below `topOfKernelData` | 1769336 B | 1754104 B |
| payload text | 789054 B | 804934 B |

**988 bytes of object cost 15880 bytes of image**, and the reason is worth recording because it is
the largest alignment effect this sequence has produced: `lowGlo` is
`__attribute__((aligned(PAGE_MAX_SIZE)))` (`lowmem_vectors.c:47`), which on this build is 16 KB, so
`.data` grew from 0x00238000 to 0x0024c000 in order to place it. `lowGlo` landed at **0x0024c000**,
exactly on the boundary, and `osversion` at 0x0024dac4 after it. The image still ends at 0x00253c08,
1.7 MB below `topOfKernelData`, and `tools/host_entry_macho_check.sh` reads `getlastaddr()` back out
of the Mach-O header as the same number.

Text went *down* by 32 bytes while the image grew by 16 KB, which is the two generated function
stubs for `version` and `osversion` being replaced by one string and one array.

## What is next: `osfmk/kern/printf.c`, and the first thing this project links that prints

The frontier is `printf_init` - `osfmk/kern/printf.c:719` - and the object is
`osfmk_kern_printf.o`: **5607 bytes of text, 4 of data, 296 of `.bss`, 26 references.** It is the
largest step since `pmap.o`, and the first one whose subject is output.

What actually runs is small. The whole of `printf_init` is three statements:

```c
	simple_lock_init(&printf_lock, 0);
	simple_lock_init(&bsd_log_spinlock, 0);
	bsd_log_init();
```

The two `simple_lock_init` calls are `arm_usimple_lock_init`, which this image has had since the
lock subsystem came in, so the frontier inside it is the third statement. Walking the object with
`tools/entry_frontier.py` from `printf_init` gives one stop, and it is `bsd_log_init` - which is
`bsd/kern/subr_log.c:0xe04`, in `bsd_kern_subr_log.o`. So the step after this one leaves `osfmk/`
for `bsd/` for the first time, and the 24 other references `printf.o` carries
(`cnputc`, `PE_kputc`, `os_log_with_args`, `paniclog_flush`, the console print buffer, and
`__aeabi_uldivmod`) are all behind code that this run will not reach.

## Reproduce

```bash
# the step: 5 resolved, 2 added, 359 -> 356
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_LOWMEM_VECTORS_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 5 resolved (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 2 added    (unique to B)

./tools/host_entry_macho_check.sh
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./preflight_boot_check.sh && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... stub_hit=printf_init

# the two steps this run took through arm_init, and the boot-arg that made the second one happen
sed -n '318,330p' external/xnu-4570.1.46/osfmk/arm/arm_init.c
grep -n "MIN_LOW_GLO_MASK" external/xnu-4570.1.46/osfmk/arm/arm_init.c
grep -o "debug=0x[0-9a-f]*" /tmp/cancro-last_kmsg.txt | head -1     # debug=0x144, from the payload

# lowGlo was a 988-byte zero-filled stub and is now the real structure: 0x3dc, at a 16 KB boundary
arm-none-eabi-nm -S out/xnu_kernel_obj/osfmk_arm_lowmem_vectors.o | grep lowGlo
grep -n "lowGlo" out/stage90/xnu_arm_entry.map | tail -1
grep -n "aligned(PAGE_MAX_SIZE)" external/xnu-4570.1.46/osfmk/arm/lowmem_vectors.c

# what the next object carries, and where its first stop is
arm-none-eabi-size out/xnu_kernel_obj/osfmk_kern_printf.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_kern_printf.o | wc -l
python3 tools/entry_frontier.py --from printf_init --list 6 \
    out/xnu_kernel_obj/osfmk_kern_printf.o $(cat /tmp/objpaths199.txt)   # bsd_log_init
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
