# Experiment 159 — the real `arm_init` ran on the device, and named `cpu_data_init`

Date: 2026-09-18
Build switch: `STAGE90_ENTRY_REAL_ARM_INIT = 1` for the entry image, `STAGE90_XNU_ENTRY = 1` for the
payload; gate flag `--allow-xnu-entry`
Other switches: `HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU Stage84 Mach-O/XNU loader preflight ok
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=cpu_data_init

No errors detected
```

`experiment-106` ended at "branched to arm_init", with `arm_init` a Stage-owned stub. **That stub is
gone: the real `osfmk/arm/arm_init.c`, compiled by the same build that produced the measurement
image, executed on the device.** It got as far as calling `cpu_data_init()` — `osfmk/arm/cpu.c` —
which is the first thing this image does not provide, and stopped there by design, naming it.

The device returned to Android on its own. `persistent_write_attempted = 0` in every contract that
reports it, `no_external_mutation = 1` in all three of the link/object/compile-graph markers, and
nothing was flashed.

## What XNU's own code did, in order, on the hardware

1. `_start` (`osfmk/arm/start.s`) — the whole entry sequence from experiment-106, unchanged: I-cache,
   `boot_args` at the ABI-checked offsets, exception vectors patched, TTBR0/TTBR1/TTBCR, a V=P
   section and a `memSize`-sized mapping at `topOfKernelData`, caches cleaned by set and way,
   DACR/PRRR/NMRR, SCTLR with TEX remap and high vectors, TLB flush, VFP, branch to `arm_init`.
2. `arm_init` (`osfmk/arm/arm_init.c`) — the real one. Its first statement, `const_boot_args =
   *args;`, is a 320-byte struct copy that clang lowers to `bl __aeabi_memcpy4`. **That copy ran
   through XNU's own `memcpy`** — `osfmk/arm/bcopy.s`, via the RTABI alias below — because the log's
   single `stub_hit` line is `cpu_data_init` and not `__aeabi_memcpy4`. The line after it,
   `str r5, [r0]` (`BootArgs = &const_boot_args`), is the one before the call.
3. `cpu_data_init(&BootCpuData)` — the reporting stub. Reaching it stops the run, disables the MMU
   and the caches, and writes its own name into the RAM console. That name is the measurement.

## The first stub hit is the interesting number, and it is one edge in

`cpu_data_init` is in `osfmk/arm/cpu.c`, which this project already compiles
(`out/xnu_kernel_obj/osfmk_arm_cpu.o`) — the stub stands in for an object that exists, not for one
that cannot be built. The same is true of all 43 names in this image's stub list: they are the
*closure* of `arm_init`, which experiment-158 measured to be the whole kernel. So the device result
is not "XNU is missing something"; it is "the image is deliberately small, and this is the edge of
it" — and the number that says what to link next is `cpu.c`, then whatever that pulls in.

## The image, before and after

| | stub image (`=0`) | with XNU's own objects (`=1`) |
| --- | --- | --- |
| text | 12104 B | 13224 B |
| image | 12444 B | **82244 B** |
| .bss | 0x00202f58 – 0x00214580 (71208 B) | 0x00214000 – 0x00215648 (5704 B) |
| generated stubs | 46 (41 functions, 5 storage) | 43 (38 functions, 5 storage) |
| stack | two 32 KB Stage-owned arrays | `data.s`'s real `intstack` (16 KB) and `fiqstack` (4 KB) |
| bytes from the base | 0x14580 | 0x15648 |

The `.bss` shrink is not a saving, it is a correction: 64 KB of the old image's bss was two hand-made
stacks that XNU's own `osfmk/arm/data.s` defines better.

## Five things found while making it run

### 1. A stand-in has to be at least as big as what it stands for

The first version of the stub generator gave **every** storage symbol 64 bytes. Two were smaller
than the symbol they replaced:

| symbol | real size | source of the size | old stand-in |
| --- | --- | --- | --- |
| `EntropyData` | **68** | `nm -S` on `osfmk_prng_random.o`; `entropy_data_t` is `uint32_t *index_ptr; uint32_t buffer[16]` (`osfmk/prng/random.h:47`) | `uint64_t EntropyData[2]` = 16 |
| `UNDReply_subsystem` | **68** | `nm -S` on the MIG-generated `..._UNDReplyServer.o` | 64 |
| `BootCpuData` | 816 | `osfmk/arm/data.s` offsets (`cdeSize_NUM`/`cdSize_NUM`) | 64 |

An undersized stand-in is silently overwritten by the first real user of it, and what it overwrites
is whatever the linker put next — in this image, the exception vector table and the log buffer.
Nothing had written past 16 bytes yet, because every function that would have (`early_random`) was
itself a stub: **a stub hides the size defect it causes.** The generator now takes each storage
symbol's size from the object that defines it and *fails the build* when that size is not knowable,
so an unknown size is a decision to make rather than a 64-byte guess.

There was a second, quieter half to the same defect. The map that decides "storage or function" was
built from `out/xnu_kernel_obj/*.o` alone — but `BootCpuData`, `CpuDataEntries` and `RTClockData`
are defined in `out/xnu_asm_obj/data.o`, the *translated assembly*, which the old map never looked
at. A symbol missing from the map fell through to the function case, so the previous image contained

```c
void BootCpuData(void) { entry_stub_hit("BootCpuData"); }
```

— a *function* standing in for a per-CPU data area, which `arm_init` passes as `&BootCpuData`. It
was harmless only because its one consumer, `cpu_data_init`, was also a stub. The map now covers
both object directories.

### 2. `gPhysBase`, `gVirtBase`, `gPhysSize` were `const uint32_t` in `.rodata`

XNU declares them

```c
unsigned long gVirtBase, gPhysBase, gPhysSize;      /* osfmk/arm/arm_vm_init.c:80 */
```

and **assigns** them at `:351-353` from the `boot_args`. The entry image had `const uint32_t`
copies: half the size of XNU's own type, in a read-only section, so the first real code to write one
would have faulted into `.rodata` instead of setting a variable. They are now `unsigned long` with
the same values. The duplicate-definition collision is what surfaced this: linking `arm_vm_init.o`
failed with `multiple definition of 'gPhysBase'`, and the interesting part was not the collision but
which of the two definitions was wrong.

### 3. The old stack pointer was not a stack pointer

`start.s:310` is `LOAD_ADDR(sp, intstack_top)` — it loads the **address** of the symbol — and
`arm_init.c:226` is `BootCpuData.intstack_top = (vm_offset_t) & intstack_top;`. Both say the same
thing: in XNU, the symbol's *address* is the top of the stack, which is why `data.s` puts the label
at the end of the `.space`. The entry image had

```c
uint32_t intstack_top = (uint32_t)(uintptr_t)&g_intstack[ENTRY_STACK_BYTES];
```

a *variable* holding an address — so `sp` was the address of a 4-byte variable, and the stack grew
down from there into whatever the linker had placed below `.data`. The runs before this one reached
`arm_init` anyway and logged sane text, because the window is mapped RWX and the descent landed in
image data rather than off the end. Linking `data.o` removed the possibility: `sp` is now
`0x0020c000`, the top of `data.s`'s real 16 KB `intstack`, which is also what `arm_init.c:226`
records in `BootCpuData`.

### 4. The compiler's memory calls are not XNU's memcpy, and had to be

`__aeabi_memcpy4` is defined nowhere in XNU's tree (see experiment-158). It comes from the compiler
runtime, which Apple links into the kernel as `libcc_kext`. Writing one by hand would be a copy
routine whose first bug corrupts the `boot_args` it was copying, so the RTABI names are **tail
branches into XNU's own routines** instead: `bcopy.s`'s `memcpy`/`memmove` and `bzero.s`'s
`memset`/`bzero`, which this project already builds into `out/xnu_asm_obj/`. `entry_arm_rtabi.s` is
that table, and one family in it is not a plain alias: `__aeabi_memset(dest, n, c)` takes its
arguments in a different order from `memset(dest, c, n)`, so those three swap `r1` and `r2` before
branching. ARM RTABI §4.3.4 has the signatures; libgcc's `lib1funcs.S` does the same swap.

### 5. The payload's dry-run pmap had a 1 MB window, and the image crossed it

The first run of this stage **did not reach the jump**: `Stage84 Mach-O/XNU loader preflight
failed`, with `loader_xnu_entry_stub_failure_mask=0x00008910`. The bits decode to
`RETURN_STATUS | NO_OUTPUT | SAFETY_BOUNDARY | ARM_VM_INIT_FULL_PMAP`, and the underlying contract
was `loader_xnu_arm_vm_init_full_pmap_status=0xd0010000`, failure mask
`HIGH_VA_DATA (0x10000) | SAFETY_BOUNDARY (0x80000000)`. Reading the code:

```c
/* VA 0x80000000 - 0x800fffff maps to PA 0x00000000 - 0x000fffff */
for (uint32_t page = 0; page < 256; page++) { ... }      /* 256 pages = 1 MB */
...
volatile uint32_t *highva_probe = (uint32_t *)(STAGE90_VIRT_BASE +
    (uint32_t)(uintptr_t)&stage90_full_pmap_probe_word);
*highva_probe_identity = 0xaabbccdd;
if (*highva_probe == 0xaabbccdd && ...) { ok } else { FAIL_HIGH_VA_DATA }
```

The probe writes a `.bss` variable through the high alias at `0x80000000 + its physical address`.
The window covers PA [0, 1 MB). The variable's address is now `0x001000B4` — 0xB4 bytes past the
end. The payload's image grew by exactly 0x14000 (`__stage90_image_end` 0x122000 → 0x136000, which
the log prints as `loader_actual_topOfKernelData`), so before this change the variable sat at
0x000EC0B4, inside the window. Past the end, `0x801000B4` resolves through an L1 slot that maps PA
`0x801000B4`, and the read returns something else entirely.

This is a defect in the *check*, not in the image: a hardcoded 1 MB window with the subject of the
test living somewhere below it. The window is now derived from `__stage90_image_end` — one L2 table
per megabyte of payload — and the run reports its own numbers:

```
stage90_xnu_arm_vm_init_full_pmap_l2_image_end=0x00136000
stage90_xnu_arm_vm_init_full_pmap_l2_image_windows=0x00000002
stage90_xnu_arm_vm_init_full_pmap_status=0x90000001
stage90_xnu_arm_vm_init_full_pmap_failure_mask=0x00000000
```

### And one that is not a defect, only a hazard: two copies of the entry image

The payload compiles a *committed* copy of the entry image (`stages/stage90/xnu_arm_entry_blob.c`)
while `build_entry.sh` writes a generated one to `out/stage90/`. They differed by this stage's whole
change, and nothing said so — the payload would have embedded the old image and the run would have
reported the old behaviour. `build.sh` now compares the two and refuses to build, printing both the
install command and the entry-image rebuild command. The check was tested by running `build.sh`
with the two deliberately out of step.

## What this does not mean

- `arm_init` did **not** run to completion. It stopped at its fourth call, by design, because the
  image does not contain `cpu.c`.
- Nothing was proven about the code *after* `arm_init`. The closure measured in experiment-158 is
  the whole kernel, so the honest next question is not "which object" but "how big an image can this
  arrangement carry" — the jump window is `boot_args.memSize`, which the payload currently sets to
  2 MB, and `_start` builds its page tables at `topOfKernelData = base + 0x20000`. Both are payload
  parameters, not architecture.
- The link prints one warning:

  ```
  osfmk_arm_arm_init.o uses 32-bit enums yet the output is to use variable-size enums
  ```

  Currently inert — the entry image contains no enums of its own — but it is a real ABI difference
  between the clang-compiled kernel objects and the payload toolchain's link, and it will matter
  when objects that share enum-carrying structs are linked together.

## Reproduce

```bash
# XNU's own objects: daemons first
./tools/build_xnu_arm_kernel.sh            # out/xnu_kernel_obj/ (arm_init.o, ...)
./tools/assemble_arm_layer.sh              # out/xnu_asm_obj/   (data.o, bcopy.o, bzero.o)

# the entry image, with XNU's own objects linked in
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -c 'entry_stub_hit' out/stage90/xnu_arm_entry_realstubs.c   # stubs generated from the link

# install it and build the payload
cp out/stage90/xnu_arm_entry_blob.c stages/stage90/xnu_arm_entry_blob.c
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)

# and the run, through the gate
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
grep -A2 "jumping to XNU" /tmp/cancro-last_kmsg.txt
```
