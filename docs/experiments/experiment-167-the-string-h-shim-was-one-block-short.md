# Experiment 167 — the `<string.h>` shim was one block short, and the measurement was reading an older directory

Date: 2026-09-18
Host only — nothing here runs on the device.
Artifacts: `stages/stage90/shims_arm/string.h`

Two things in one stage, and the second is why the first was nearly recorded as having done nothing:
a shim that replaced one of Apple's headers had dropped a block from it, and the tool that measures
the link was reading a directory the build was not writing to.

| `STAGE90_BOOT` | before | after |
| --- | --- | --- |
| C / C++ | 423 of 426, 83 of 83 | 423 of 426, 83 of 83 (unchanged) |
| undefined, measurement link | 94 | **92** |
| boot-path stubs, from `arm_init` | 9 of 94 | **8 of 92** |
| — `stub_blockers.py`: a file not in the manifest | 5 | **4** |
| — : no source in the tree | 4 | 4 |
| `.text` in the measurement image | 2844704 bytes | 2844688 bytes |

| `RELEASE` | before | after |
| --- | --- | --- |
| C / C++ | 612 of 615, 83 of 83 | 612 of 615, 83 of 83 (unchanged) |
| undefined, measurement link | 56 | **54** |
| boot-path stubs, from `arm_init` | 8 of 56 | **7 of 54** |
| `.text` in the measurement image | 4911440 bytes | 4911424 bytes |

## The block: `osfmk/libsa/string.h:97-99`

`__nosan_bzero` was one of the eight boot-path stubs and `stub_blockers.py` filed it under "a file
not in the manifest", naming `san/memintrinsics.h` — the right symptom with the wrong diagnosis. The
header is in the tarball; what was missing was the include that reaches it. The chain, all measured:

- `osfmk/kern/zalloc.c:561` calls `__nosan_bzero` and `:4030` calls `__nosan_strncpy`; the tree's only
  definition of either is the `static inline` at `san/memintrinsics.h:40` and `:43`.
- `zalloc.c`'s translation unit contains **0** occurrences of `san/memintrinsics` or `libsa/string`
  (preprocessed, experiment-165), so with no declaration visible clang emitted an implicit-declaration
  reference to an external symbol nothing defines. `if (g_crypto_funcs == NULL)` is not the only place
  this build relied on a warning being suppressed.
- That header is reached through `osfmk/libsa/string.h:97-99`:

  ```c
  #ifdef PRIVATE
  #include <san/memintrinsics.h>
  #endif
  ```

- **Nothing in `osfmk`, `bsd`, `libkern`, `iokit`, `pexpert` or `security` includes
  `osfmk/libsa/string.h`** — because in Apple's build it *is* the kernel's `<string.h>`.
- Here `<string.h>` resolves to `stages/stage90/shims_arm/string.h`, this project's replacement, which
  declares the same functions and had dropped this block.

So the file that was "not in the manifest" is this project's own, and the fix is the block plus the
one name it needed: `strncat`, the only function in `memintrinsics.h`'s fourteen whose inline body
had nothing to call (`osfmk/libsa/string.h:77` declares it, in the same list the shim's other
declarations were transcribed from).

**The `#ifdef PRIVATE` guard is deliberately not reproduced.** `PRIVATE=1` is one of the build's
global defines, so the guard would change nothing today — but "is this symbol declared" is not a
question a per-component define should be able to answer differently for two translation units that
link against each other, which is exactly the class of defect experiment-164 was. Every name in the
header is `static inline`, so a translation unit that does not use them emits nothing; the compile
counts are the evidence that this is inert: **423 of 426 and 83 of 83 either side, the same three
failing files, and in both configurations `.text` shrinks by exactly 16 bytes** — one 8-byte-aligned
tail per configuration, the same change measured twice.

Closed: `__nosan_bzero` and `__nosan_strncpy`. `__nosan_bzero` was the boot path's third-nearest
stub at 7 edges (`get_zone_page_metadata <- zcram <- vm_map_init <- vm_mem_bootstrap <-
kernel_bootstrap`); `__nosan_strncpy` is off the boot path, from `zfree`'s zone-name copy.

## The measurement read a directory the build was not writing to

The RELEASE row above says 56 → 54. The first measurement of the after-state said **56**, unchanged,
and that is the interesting number.

The build script's default object directory and `measure_link.sh`'s default for a non-`--min` run are
**the same place**:

```
tools/build_xnu_arm_kernel.sh:61   OUT=${XNU_KERNEL_OBJ_OUT:-$REPO_ROOT/out/xnu_kernel_obj}
tools/measure_link.sh:51           OBJ=${XNU_KERNEL_OBJ_OUT:-$REPO_ROOT/out/xnu_kernel_obj}
```

Both honour `XNU_KERNEL_OBJ_OUT`, and the pairing holds only if they are given the same value or
neither is. During this stage the RELEASE build was run with an explicit `XNU_KERNEL_OBJ_OUT` and the
measurement without one, so the tool linked `out/xnu_kernel_obj` — whose objects were 47 minutes old,
from before this change:

```
$ nm --undefined-only out/xnu_kernel_obj/osfmk_kern_zalloc.o | grep -c __nosan
2
$ nm --undefined-only out/xnu_obj/osfmk_kern_zalloc.o        | grep -c __nosan
0
```

A stale directory does not announce itself: it links, it reports a plausible number, and the number
it reported here was the **previous** stage's — which is what exposed it. The rule that would have
prevented it is the one this project keeps having to relearn in a new place: pass the same object
directory to the build and to the measurement, or neither, and when a count does not move after a
change that provably removed two symbols, suspect the count before the change. (`docs/status/roadmap.md`
§5 keeps the list; this is the ninth instance in the measurement class.)

## What is left — 8 stubs, and one of them is not really reachable

`STAGE90_BOOT`, from `arm_init`:

```
   4 edges  __firehose_buffer_create            the port of experiment-162, unchanged
   5        __firehose_buffer_tracepoint_flush   }
   5        __firehose_buffer_tracepoint_reserve }
   7        MD5Init / MD5Update / MD5Final       libkern/crypto/corecrypto_md5.c, `optional crypto`
   9        chudxnu_thread_get_callstack64_kperf only osfmk/chud/i386/ implements it
  10        mach_msg_destroy_from_kernel        no source in the tree
```

**The three MD5 stubs are not reachable by a real kernel in this configuration**, and the way to see
that is a defect in the measurement worth fixing rather than a fact about XNU:

- `osfmk/kern/btlog.c:634` opens `btlog_add_entry` with `if (g_crypto_funcs == NULL) return;` and the
  `MD5*` calls follow it unconditionally.
- `g_crypto_funcs` is defined in `libkern/crypto/register_crypto.c:33` — `crypto_functions_t
  g_crypto_funcs = NULL;` — a file that is `optional crypto`, and `STAGE90_BOOT` has `CRYPTO 0`
  (`out/xnu_options/STAGE90_BOOT/crypto.h`). So in this configuration the symbol is undefined, and it
  would be NULL in an image that had it.
- `measure_link.sh` stubs **every** undefined symbol as a weak *function*: `.type g_crypto_funcs,
  %function` with a two-instruction body. The address is therefore non-zero, `g_crypto_funcs == NULL`
  is false in the image, and `btlog_add_entry` takes the branch a real kernel would not.

So the image's static call graph — and `stub_reach.py`, which walks the objects rather than executing
them — counts a call that the configuration cannot make. The fix is in the stubbing step: an undefined
symbol referenced by a `CALL`/`JUMP24` relocation is a function, and one referenced by `ABS32` is data
and should be a zero word in `.bss`. That is the next measurement improvement, and it is worth more
than one symbol: it is the difference between "the image links" and "the image behaves".

## Reproduce

```bash
# the change
sed -n '105,140p' stages/stage90/shims_arm/string.h      # the block, and why the guard is dropped
sed -n '96,100p' external/xnu-4570.1.46/osfmk/libsa/string.h   # the block it mirrors

# the measurement, minimal (build and measurement share the directory)
XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  MANIFEST=$PWD/out/xnu_arm_manifest_min.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_min_obj \
  ./tools/build_xnu_arm_kernel.sh
./tools/measure_link.sh --min --keep-stubs        # 92
./tools/stub_reach.py --min --from arm_init --list 40   # 8 of 92
grep -c nosan out/link/STAGE90_BOOT-stubs.s              # 0

# the measurement, RELEASE — the defaults agree with each other; pass the same directory to both
MANIFEST=$PWD/out/xnu_arm_manifest.txt ./tools/build_xnu_arm_kernel.sh
./tools/measure_link.sh --keep-stubs              # 54

# the stale-directory trap
nm --undefined-only out/xnu_kernel_obj/osfmk_kern_zalloc.o | grep -c __nosan   # the build's directory
grep -n "XNU_KERNEL_OBJ_OUT" tools/build_xnu_arm_kernel.sh tools/measure_link.sh

# why the three MD5 stubs are unreachable in a real image
sed -n '632,644p' external/xnu-4570.1.46/osfmk/kern/btlog.c
sed -n '33p' external/xnu-4570.1.46/libkern/crypto/register_crypto.c
grep -n "optional crypto" external/xnu-4570.1.46/libkern/conf/files
grep -n "g_crypto_funcs" out/link/STAGE90_BOOT-stubs.s
```
