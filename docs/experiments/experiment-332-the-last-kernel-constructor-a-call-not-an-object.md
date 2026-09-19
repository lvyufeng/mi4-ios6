# Experiment 332 — the last kernel constructor: a call rather than an object, and a stop one `bl` into it

**Step:** link and run `libsa/lastkernelconstructor.c` — the object whose constructor is the only caller
of `iokit_post_constructor_init`, and therefore the only way `OSKext::initialize()` runs. Nothing else
changes except the definition of `last_kernel_symbol`, which is the second half of Apple's same file.

**Prediction:** *the `.init_array` table's eighth and last entry is `last_kernel_constructor`; the run
stops at the **first** stub inside `iokit_post_constructor_init`, which the image says is
`_Z15IOCPUInitializev`, with the caller key `iokit_post_constructor_init+0x08`; the counts go 782 → 781
undefined / 675 → 674 function / 107 storage (**1 resolved, 0 added**); `.text` moves −0x14 plus fill;
`.init_array` 0x1C → 0x20 and `.bss`'s start moves for the first time in the walk, +0x40.*

**Result:** **the stop is the predicted name at the predicted offset, and every section is the
prediction's except the sign of one — the headroom falls 0x40 rather than rising.** The one resolved name
is the retired stand-in for `last_kernel_symbol`, and `.init_array`'s eight pointers are the seven
`_GLOBAL__sub_I_*.cpp` functions followed by `last_kernel_constructor`.

## The step is a call, three links long, and each link is the only one of its kind

```
OSRuntimeInitializeCPP's scan  ->  .init_array entry #8
  -> last_kernel_constructor()                 libsa/lastkernelconstructor.c:35
    -> iokit_post_constructor_init()           iokit/Kernel/IOStartIOKit.cpp:89
      -> OSKext::initialize()                  libkern/c++/OSKext.cpp:628
```

`iokit_post_constructor_init` has been **in** the image since the IOKit objects were linked — `nm` finds it
at 0x8011b0ec, 0xb4 bytes, body and all — and it has never run, because its only caller is a file this
project has never compiled. `libsa` is not a component of Apple's ARM manifest (722 lines, `grep -c libsa`
= 0), so `lastkernelconstructor.c` is in the tree and in no object list. The file is named for its
position rather than its body: it is the *last* of the C++ static constructors because the link puts it
last, and `.init_array` is walked in link order.

## Apple's file does not compile for this target, and both reasons are measured

| | |
|---|---|
| `external/xnu-4570.1.46/libsa/lastkernelconstructor.c` | 41 lines, two statements |
| the constructor | `static void last_kernel_constructor(void) __attribute__ ((constructor));` |
| the asm | `__asm__(".globl _last_kernel_symbol"); __asm__(".zerofill __LAST, __last, _last_kernel_symbol, 0");` |

Run through the project's own pipeline with a one-line manifest, the single file fails:

```
    <inline asm>:2:1: error: unknown directive
    .zerofill __LAST, __last, _last_kernel_symbol, 0
```

The four-operand `.zerofill` is the **Darwin** assembler's form (segment, section, symbol, size) and this
image is linked as ELF. The second reason is the name: `_last_kernel_symbol` carries Mach-O's spelling of
a C identifier, while the reference in `arm_vm_init.o` is the ELF spelling `last_kernel_symbol`
(`osfmk/arm/arm_vm_init.c:57`, `extern void *last_kernel_symbol`, "Denotes the end of xnu", stored at
`:499` as `vm_kernel_top`) — so a definition under the Darwin name would satisfy nothing at all, and
leave the reference a stub.

**That pipeline run is also a trap worth recording.** Its first act is `rm -f "$OUT"/*.o`
(`tools/build_xnu_arm_kernel.sh:146`), so a one-line `MANIFEST` is not a one-object build: it deleted the
whole 695-object pool, and the entry image could not be built again until the full manifest was compiled.
A narrow manifest is for measurement, never for a build step.

## The port: four bytes of code and one `.init_array` entry

`stages/stage90/xnu_arm_boot/entry_last_kernel_constructor.c` is Apple's constructor — the same function,
the same attribute — compiled with `entry_stubs.c`'s exact flag list, which is what every other non-XNU
translation unit in this image is compiled with.

| `entry_last_kernel_constructor.o` | |
|---|---|
| `.text.startup` | **4** — `b iokit_post_constructor_init`, a **tail branch**, not a `bl` |
| `.init_array` | **4** — one pointer, at `last_kernel_constructor` |
| definitions / references | 1 (`last_kernel_constructor`, local `t`) / 1 (`iokit_post_constructor_init`, real) |

`last_kernel_symbol` goes where its meaning is computed rather than where Apple's asm put it:
`entry.ld`, next to `ExceptionLowVectorsBase` and `ResetHandlerData`, as `last_kernel_symbol = __bss_end;`.
The mechanism is the same one those two use — **pass 1 links with the script**, so the name is defined
before the stub generator runs, and the 24-byte function body and name slot this image had been carrying
for it are retired instead of stood in for. After the build `nm` reports `A 0x80194D58`, which is
`__bss_end` exactly; before it, `vm_kernel_top` was the stub's own address, 0x80123CE4. Nothing this
image has already run reads `vm_kernel_top` again, so that correction is a correctness of the image's own
description rather than of this step's behaviour.

## The counts and the layout, section by section

**Measured: 782 → 781 undefined, 675 → 674 function, 107 storage unmoved — 1 resolved, 0 added.** The
created half is zero because the object's single reference is `iokit_post_constructor_init`, which the
image defines. The stub list confirms the resolved name by absence: 781 records, 674 `func` + 107 `data`,
and `last_kernel_symbol` is not among them.

| | 331 | 332 | delta |
|---|---|---|---|
| undefined / function / storage | 782 / 675 / 107 | **781 / 674 / 107** | −1 / −1 / 0 |
| `.text` | 0x142FE0 | **0x142FC0** | **−0x20** |
| `.text` placed / fill | 0x1422BC / 0xD24 | **0x142294 / 0xD2C** | −0x28 / +0x8 |
| `.data` | 0x80144000 (0x19398) | **0x80144000 (0x19398)** | 0, 0 |
| `.sysctl_set` | 0x8015D398 (0x10C) | **0x8015D398 (0x10C)** | 0, 0 |
| `.init_array` | 0x8015D4A4 (0x1C) | **0x8015D4A4 (0x20)** | 0, **+4** |
| `.bss` | 0x8015D4C0 (0x37858) | **0x8015D500 (0x37858)** | **+0x40**, 0 |
| `__bss_end` | 0x80194D18 | **0x80194D58** | +0x40 |
| image | 1430720 | **1430724** | +4 |
| headroom | 1487592 | **1487528** | **−0x40** |

The headroom row is the one prediction that was wrong, and it was wrong *by sign*: it is the room *below*
`topOfKernelData`, so a `__bss_end` that moves up by 0x40 leaves 0x40 less room. Every other row and all
six `.text` terms are exact — a sign written by pattern rather than a quantity measured, which is the
family 317 and 321's `created − retired` pair belong to.

`.text`'s growth is the map's own placed-input total, not arithmetic on section sizes — 0x1422BC →
0x142294 is −0x28, and the fill is +0x8 on top (0xD24/57 → 0xD2C/59 records), so **−0x28 + 0x008 =
−0x20** with no residual:

| term | bytes |
|---|---|
| `.text.startup`, this object | **+0x004** |
| the retired stub body (`realstubs.o` `.text` 16200 → **16176**) | **−0x18** |
| the retired name's slot in the merged pool (`realstubs.o` `.rodata.str1.4` 14111 → **14091**) | **−0x14** = `align4(18 + 1)` |
| `.text` fill | **+0x008** |

The name-slot term is measured twice: once as the residual of the placed total and once as the pool's own
section. That is what makes `align4(len + 1)` a measurement in this step rather than 322's rule quoted
again.

**The table, entry by entry.** `objdump -s -j .init_array` gives eight 4-byte pointers at 0x8015D4A4,
resolved against the image the build just made: `_GLOBAL__sub_I_OSKext.cpp`, `_OSMetaClass`,
`_OSDictionary`, `_OSObject`, `_OSCollection`, `_OSSymbol`, `_OSString` — the seven the walk has linked,
in the same order as 331 — and then **`last_kernel_constructor` at 0x80122928**, the first entry in this
table that is not a `_GLOBAL__sub_I_*.cpp` and the last entry the scan will run. And `.bss`'s start moves
for the first time in the walk for the predicted reason: 0x1C was exactly `align64` of itself, so four
more bytes cost 0x40.

The layout checks that touch these numbers all pass, and the one worth naming is the pad:
`ResetHandlerData − ExceptionLowVectorsBase` is now 0x194D44 and both write addresses (0x80194D48,
0x80194D4C) are inside the reserved `.bss` slot — the +0x40 move shifted that arithmetic too, and the
check 288 built is what says it is still inside.

## The stop, read off the image before the run

`iokit_post_constructor_init`'s body is in the image, so its first four calls can be named rather than
guessed — and each one's return address is the caller key a stop there would report:

```
8011b0ec <iokit_post_constructor_init>:  push {r4, r5, fp, lr}
  +0x04  8011b0f0  bl 801265b0 <_Z15IOCPUInitializev>              [a stub]   key +0x08
  +0x08  8011b0f4  bl 801266a0 <_ZN15IORegistryEntry10initializeEv> [a stub]   key +0x0C
  +0x10  8011b0fc  bl 80126868 <_ZN9IOService10initializeEv>        [a stub]   key +0x14
  +0x14  8011b100  bl 80126610 <_ZN11IOCatalogue10initializeEv>     [a stub]   key +0x18
  +0x18  8011b104  bl 801092fc <_ZN6OSKext10initializeEv>                      real, 0x48c bytes
```

The call at +0x04 is a **direct `bl`**, so the stub's reported caller is that function's own offset — not
an inherited `lr`, which is 329's case, and not this step's own constructor, whose body is a four-byte `b`
and which therefore contributes no caller key of its own. And a stub is always the end of a run:
`entry_stub_hit`'s last statement is `entry_epilogue`, which is `noreturn`.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_why_byte=0x00000061                       <- 'a'
 xnu_entry_kv_written=0x00000065
 xnu_entry_kv_in_dram=0x00000089
 xnu_entry_kv_dropped=0x00000000
 xnu_entry_stub_caller=0x8011b0f4                    (also _a and _e)
 xnu_entry_stub_caller_digits=0x00000038             <- a real caller record, unlike 328/331
 xnu_entry_abort_entries=0x00000000                  <- nothing faulted: the stub ended it
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_Z15IOCPUInitializev
```

`0x8011b0f4` minus `iokit_post_constructor_init`'s 0x8011b0ec is **+0x08** — the return address of the `bl`
at that function's +0x04, i.e. its first statement. `kv_written=0x65` is non-zero where 331's was 0,
because the stub path records before the epilogue runs; `digits=0x38` is non-zero, which is what tells a
real caller record from 328's and 331's KV-buffer heads.

**What the run measures beyond the stop.** The key resolves *inside* a function nothing else in this image
calls, and that function is reachable only through the entry this step added — so the whole chain ran:
the scan reached the eighth and last `.init_array` entry, called `last_kernel_constructor`, its `b`
reached `iokit_post_constructor_init`, and that function's first statement was the stub. Reaching entry #8
at all is also the measurement that the seven `_GLOBAL__sub_I_*.cpp` entries returned, and
`abort_entries=0` says nothing faulted on the way through them — which 331's run could not say, because
its stop came after all seven.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img` (4346 KB), nothing flashed,
`25` records of `persistent_write_attempted=0x00000000` and `87` of `failure_mask=0x00000000` with **no
non-zero reading of either**, `xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301632 bytes, and the
device back on Android on its own (`MI 4LTE`, release 10).

## What this step measures

The frontier is now `iokit_post_constructor_init`'s own body, and the four stubs it calls before it
reaches `OSKext::initialize()` are the next four steps in order: `_Z15IOCPUInitializev` (+0x08, this
run), `_ZN15IORegistryEntry10initializeEv` (+0x0C), `_ZN9IOService10initializeEv` (+0x14),
`_ZN11IOCatalogue10initializeEv` (+0x18).

Then `OSKext::initialize()` — the function 331's stop was inside — runs for real, and the image already
says what its own first stop is: six calls in, four `OSArray::withCapacity` and two `OSSet::withCapacity`
at +0x68..+0xCC, with `sKextLock`/`sKextLoggingLock` (`IORecursiveLockAlloc`, `IOLockAlloc`, all real) and
`sKextsByID` (`OSDictionary::withCapacity`, real) written before them. So 331's NULL is cured by the first
five instructions of the function this step made reachable.

Past those six, the body has a stop of a kind this walk has not met yet, and it is worth writing down now
because the step that reaches it should have a prediction. At `OSKext::initialize+0x2C4` the code is

```
801095b0 <_ZN6OSKext10initializeEv+0x2b4>:
801095b0:  movw r1, #0x4440
801095b4:  movt r1, #0x8019      ; r1 = &kOSBooleanTrue = 0x80194440
801095b8:  ldr  r1, [r1]         ; r1 = the 4-byte stand-in's contents = 0
801095c0:  ldr  r2, [r1]         ; <- the fault: dfar = 0x0, insn = 0xe5912000
```

`kOSBooleanTrue` is `extern OSBoolean * const & kOSBooleanTrue` — a **reference** — and a reference's ELF
storage is the *address* of its referent rather than the pointer, so the generated stand-in
(`data kOSBooleanTrue R 0x4`, four zeroed bytes) is dereferenced twice. That is a stand-in of the wrong
*kind* rather than of the wrong size, which is the other half of what
[[mi4-stand-in-size-is-not-value]] records: the size guard cannot see a symbol whose value is supposed to
be an address. The fault would be `dfar = 0x0` at `OSKext::initialize+0x2C4`, and the frame above it is
`iokit_post_constructor_init` at +0x18 — the fifth of the calls in the table above, which is the one that
makes `sKextLock` non-NULL on the way past. (Its stand-in moved with `.bss` in this very step, from
0x80194400 to 0x80194440, which is a reminder that the address to quote for it is the one the image the
run used has.)

```
    # 333: `_Z15IOCPUInitializev` - the stub this run stopped in, whose body is the first statement of
    #      `iokit_post_constructor_init`; the three IOKit `initialize` stubs follow it in that body.
```
