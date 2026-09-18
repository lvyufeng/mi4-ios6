# Experiment 197 — The pmap Runs, `arm_vm_init` Reaches Its Last Instruction, and the Frontier Is `patch_low_glo_static_region`

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
 xnu_entry_kv_written=0x00000026
 xnu_entry_kv_in_dram=0x00000026
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=patch_low_glo_static_region

No errors detected
```

`stage90_xnu_entry_stub_no_exception=0x00000001`, `xnu_entry_checks=5`,
`xnu_entry_failures=0x00000000`, and `persistent_write_attempted=0x00000000` in all 25 contracts
that report it. The device returned to Android on its own.

The stop is where the call-graph tool said it would be, but the interesting part is not the name:
**it is the position.** `patch_low_glo_static_region` is the last statement of `arm_vm_init`, and the
only instruction that reaches it is an unconditional tail call at the very end of the function:

```
    1d9c:	e8bd4ff0 	pop	{r4, r5, r6, r7, r8, r9, sl, fp, lr}
    1da0:	eafffffe 	b	0 <patch_low_glo_static_region>
			1da0: R_ARM_JUMP24	patch_low_glo_static_region
```

One occurrence of that symbol in the whole object, after the register restore, with nothing
branching around it. So falling into it is only possible by executing `arm_vm_init` from the top to
the bottom - which means:

- `pmap_bootstrap((gVirtBase+MEM_SIZE_MAX+0x3FFFFF) & 0xFFC00000)` **ran for real**, all of it:
  the kernel pmap initialized field by field, the io-region descriptors read out of the device
  tree, `pp_attr_table`/`io_attr_table`/`pv_head_table`/`ptd_root_table` allocated out of
  physical memory at `avail_start`, `ptd_bootstrap` walking the boot translation table,
  `pmap_cpu_data_array_init`, and the five `simple_lock_init` calls;
- `arm_vm_prot_init(args)` ran for real;
- the `pmap_init_pte_page` loop ran for real - `off_end = (2 + mem_segments*3) << 20` = 5 MB at 4 KB
  per iteration, so **1280 iterations**, each allocating a page at `avail_start` and installing four
  TTE entries in `cpu_tte` for the 0x40400000 window;
- and the kernel did not fault once while doing it: a bad write would have landed in `fleh_dataabt`
  and been reported with its `DFAR`, exactly as designed.

That is the first time any of XNU's memory-management code has run on this device.

`kv_written == kv_in_dram == 0x26` - exp-195's fix still holds, and the same value carried by two
independent routes still agrees.

## What the object cost

`osfmk_arm_pmap.o` - 45052 bytes of text, 72 of data, 1032 of `.bss`, 93 references. Linking it:

```
resolved (35):  cpu_tte cpu_ttep fillPage flush_mmu_tlb_region kernel_pmap kernel_pmap_store
                kvtophys mapping_adjust mapping_free_prime pmap_clear_noencrypt
                pmap_clear_reference pmap_clear_refmod pmap_copy_page pmap_copy_part_page
                pmap_cpu_data_init pmap_disconnect pmap_enter pmap_enter_options pmap_find_phys
                pmap_free_pages pmap_init_pte_page pmap_init_pte_static_page pmap_map_globals
                pmap_map_high_window_bd pmap_max_offset pmap_next_page pmap_next_page_hi
                pmap_set_cache_attributes pmap_set_modify pmap_set_pmap pmap_switch_user_ttb
                pmap_valid_address pmap_virtual_space pmap_zero_page pmap_zero_part_page
added    (16):  CleanPoU_DcacheRegion InvalidatePoU_Icache OSBitAndAtomic16 OSBitOrAtomic16
                OSCompareAndSwap16 _vm_object_allocate bzero_phys cache_sync_page ffs lowGlo
                platform_cache_batch_wimg platform_cache_flush_wimg setbit testbit
                vm_object_lock vm_protect
378 -> 359 undefined
```

Both directions link, which is exp-190's rule: the probe is gone from this build, so the
measurement is taken by standing an empty object in for `osfmk_arm_pmap.o` and diffing the undefined
set against the previous build's.

Thirty-five stubs became real functions and data. Sixteen new obligations arrived - and they are
*newly referenced by this object*, not newly missing from the image: `lowGlo`, `setbit`, `testbit`,
`ffs`, `bzero_phys`, `cache_sync_page`, `kvtophys` (the last of which this object defines, so it is
on the resolved side) and the `OSBit*16`/`OSCompareAndSwap16` atomics. `lowGlo` is the interesting
one: it is the whole reason the next object is next.

## The reason this object was the step, and it is not in the call graph

The frontier method reads a call graph, and this step is one the call graph cannot justify. Every
one of `pmap_bootstrap`'s twenty-six `bl` sites names a symbol the image already provides - plus
`__aeabi_uldivmod`, which libgcc supplies - so `tools/entry_frontier.py` reports **no stop inside
`pmap_bootstrap` at all**, and has since exp-195. By that measure the object buys nothing.

What it buys is *data*. Before this step:

| symbol | what the image had |
| --- | --- |
| `kernel_pmap` | a generated `uint8_t kernel_pmap[0x4]` in `.bss` - **zero** |
| `kernel_pmap_store` | a generated `uint8_t kernel_pmap_store[0xc0]` - zero |
| `cpu_tte`, `cpu_ttep` | 4-byte storage stubs (correctly sized, by luck of being pointers) |
| `pmap_stamp`, the io-region descriptors, the four tables | storage stubs sized from `nm -S` |

The sizes were right - the generator takes them from the object that defines the symbol, which is
`osfmk_arm_pmap.o` itself, sitting in this project's pool. The *contents* were the problem. The first
statement of `pmap_bootstrap` is `kernel_pmap->tte = cpu_tte`: a store through a pointer that is
zero. Sizing a stand-in correctly does not make it the right value, and `kernel_pmap` is
`&kernel_pmap_store` in XNU's own source, which no amount of `nm` will tell you.

So this step is the one where the entry image stops standing in for the pmap and starts running it,
and the thing that made it necessary was reading the function body rather than walking its calls.
**The call graph measures which symbols are missing. It does not measure which pointers are null.**

## The probe retired, and the blind spot it exposed

Two consequences of linking the object were settled before the run rather than discovered by it:

1. **`pmap_bootstrap`'s probe in `entry_stubs.c` is compiled out.** It was a second definition of a
   symbol `pmap.o` now defines. It is kept, under `STAGE90_ENTRY_REAL_PMAP_BOOTSTRAP`, because the
   twelve values it wrote are exp-195's measurement and the record is worth more than the lines it
   costs - but they cannot be taken again, because the function they were taken from now runs for
   real. That is the cost of the method: **the probe is consumed by the step that satisfies it.**
2. **The run no longer stops inside `pmap_bootstrap` at all**, so the measurement is the name the
   image reports next.

The tool that predicts that name had a blind spot, and this run is where it showed.
`tools/entry_frontier.py` followed direct `bl` targets only, and `arm_vm_init`'s last statement is a
tail call - a bare `b`. Before the fix it reported nothing between `pmap_bootstrap` and
`arm_init`'s own next call, which would have made the prediction look like a gap rather than a
symbol. The fix is one regex: a `b` to a *named* symbol is followed, a conditional branch and a
local label are not. It is the difference between "the frontier is `patch_low_glo_static_region`"
and "the frontier is unknown", and it was found by reading the disassembly of the function by hand
and noticing the `b` the tool had walked past.

## Cost

| | exp-196 | now |
| --- | --- | --- |
| entry objects linked | 36 | 37 (`osfmk/arm/pmap.o`) |
| entry text | 180876 B | 224748 B |
| entry image | 264400 B | 297240 B |
| entry `.bss` | 0x002405c0 – 0x002479c8 (29704 B) | 0x002485d8 – 0x00250088 (31408 B) |
| undefined | 378 | 359 |
| stubs | 323 functions, 55 storage | 307 functions, 52 storage |
| boot_args offset | +299008 | +335872 |
| headroom below `topOfKernelData` | 1803832 B | 1769336 B |
| payload text | 756214 B | 789054 B |

The image grew 33 KB and the stub count went *down* by nineteen, which is what a step that resolves
thirty-five symbols and adds sixteen obligations looks like. `.bss` ends at 0x00250088, 1.7 MB below
`topOfKernelData`; `tools/host_entry_macho_check.sh` reads `getlastaddr()` back out of the Mach-O
header as 0x00250088 and every segment `arm_vm_init` asks for is either right or absent.

## What is next: `lowmem_vectors.o`, and the one stand-in of the wrong kind

The frontier is `osfmk/arm/lowmem_vectors.c`, and the object is `osfmk_arm_lowmem_vectors.o`: **72
bytes of text, 988 of data, 0 of `.bss`, 6 references.** It defines `lowGlo` - the low-globals
structure at the bottom of kernel memory that a debugger reads, initialized to a page-aligned struct
full of self-referential pointers (`&version`, `&kmod`, `&osversion`, `&pmap_object_store.memq`, and
the offsets into `vm_page` it describes) - plus `patch_low_glo`, `patch_low_glo_static_region` and
`patch_low_glo_vm_page_info`.

It is another object whose value is its data, and this time the data is *initialized*, so the
`nm -S` route cannot stand in for it at all. Its six references are four the image will resolve on
its own and two it will not:

| reference | where it comes from |
| --- | --- |
| `pmap_object_store` | 0xa0 bytes, `osfmk_arm_pmap.o` - resolved by this experiment's own step |
| `vm_kernel_stext` | 4 bytes, `osfmk_arm_arm_vm_init.o` - resolved |
| `kdp_trans_off`, `kmod` | both `B` size 4; sized from the pool like any other storage |
| `version`, `osversion` | **in no object in this project's pool at all** |

`version` and `osversion` are `libkern/libkern/version.h.template:105-109`'s
`extern const char version[]` and `extern char osversion[]` (`OSVERSIZE` 256), defined by
`config/version.c` - which is a *template* whose strings carry Apple's `###KERNEL_VERSION_LONG###`
and `###KERNEL_BUILD_DATE###` placeholders, substituted by a build step this project does not run
and therefore never compiled here. So the generator's `case` falls through to the function branch
and emits `void version(void) { entry_stub_hit("version"); }`: it links, nothing in this image trips
over it, and it is still a stand-in that lies about what it is. The fix is a definition of the right
kind, and it is experiment 198's, because the symbol only becomes reachable when this object is
linked.

**This section named the wrong obstacle before experiment 198 ran, and the correction is the more
useful paragraph.** It said `kmod` was `B` with `nm -S` size 0, and that `build_entry.sh` would
therefore refuse the build. Both halves were wrong, and they were wrong for one reason: `nm -S -P`
prints `name type value size`, so `$4` is the size - but the check that produced the claim read
`$3`, which is the **value**. `kmod`'s value is 0 and its size is 4; `kdp_trans_off`'s value is 8 and
its size is 4. A column off by one produced a confident statement about what a build would do, and
the build's own generator - which reads `$4` and is right - would have handled both without comment.
It is the same shape as everything in `mi4-measurement-defects`: reading a number out of a tool's
output in a format the tool does not print.

## A correction to experiment 195's reproduce block

`comm -13` prints the lines unique to the **second** file, so in exp-195's recipe the two labels are
the wrong way round: `comm -13` is *added* and `comm -23` is *resolved*. The claims in that document
(7 resolved, 61 added, and the lists themselves) are correct and were corrected against the symbol
lists; only the two inline comments were swapped. Fixed there, and written the right way round here.

## Reproduce

```bash
# the step: 35 resolved, 16 added, 378 -> 359. The probe no longer exists in this build, so the
# measurement is taken by standing an empty object in for pmap.o, exactly as exp-190 does it.
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_PMAP_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 35 resolved  (unique to A)
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 16 added     (unique to B)

./tools/host_entry_macho_check.sh
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./preflight_boot_check.sh && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -20
#   ... stub_hit=patch_low_glo_static_region

# the position that makes the name a proof: one tail call, at the end of arm_vm_init, after the pop
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_arm_arm_vm_init.o | grep -n patch_low_glo
sed -n '505,536p' external/xnu-4570.1.46/osfmk/arm/arm_vm_init.c

# the data problem the call graph does not show
S=out/xnu_kernel_obj/osfmk_arm_pmap.o
arm-none-eabi-nm -S "$S" | grep -E " kernel_pmap$| kernel_pmap_store$"
grep -n "kernel_pmap = \|kernel_pmap_store" external/xnu-4570.1.46/osfmk/arm/pmap.c external/xnu-4570.1.46/osfmk/arm/pmap.h | head

# the tail call the tool could not see, now followed
python3 tools/entry_frontier.py --from arm_init --list 12 "$S" $(cat /tmp/objpaths.txt)

# and the next step, before it is a run: which of its six references the pool cannot size.
# `-S -P` prints `name type value size`, so the size is $4 - the column-off-by-one that made the
# paragraph above wrong the first time is worth re-running rather than remembering.
arm-none-eabi-size out/xnu_kernel_obj/osfmk_arm_lowmem_vectors.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_arm_lowmem_vectors.o
arm-none-eabi-nm -A -S -P --defined-only out/xnu_kernel_obj/*.o out/xnu_asm_obj/*.o |
  sed 's/^[^:]*: //' | awk 'NF>=2 {print $1, $2, ($4 == "" ? "-" : $4)}' | sort -u > /tmp/kernsyms.txt
for s in kdp_trans_off kmod osversion pmap_object_store version vm_kernel_stext; do
  printf '%-22s ' "$s"; awk -v n="$s" '$1==n {print "type="$2" size="$3}' /tmp/kernsyms.txt; echo
done
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
