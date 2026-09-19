# Experiment 329 — the constructor table described to XNU, and a stop one instruction past the first static constructor this kernel has ever run

**Step:** **link nothing.** A change to `stages/stage90/xnu_arm_boot/entry_macho.s` and to
`tools/host_entry_macho_check.sh`: describe `.init_array` to XNU's own C++ runtime as a
`__DATA,__mod_init_func` section.

**Prediction:** *no count moves (801/692/109), `.text` **+0x40** and only `.text` — the header's 68 bytes
grow `.rodata.macho` in place under the 16 KB boundary, so `.data`, `.sysctl_set`, `.init_array`, `.bss`,
the image and the headroom are all unmoved — and the stop is **`__cxa_atexit`** with the caller key
`0x8011d750` = `OSRuntimeInitializeCPP+0x184`, whose `caller-4` is the `blx r0` of XNU's own constructor
loop, because the initializer's transfer into the C++ runtime is a **tail branch**.*

**Result:** **both halves exact** — the stop and its key, and the layout to the byte. One key measures a
five-fact chain: the section-name match, the constructor count, the scan's indirect call, the first
initializer's whole body, and its tail branch into a stub for a name that no XNU object defines.

## What was missing, and what the step is

328 ended in a `data abort` on `_ZL4pool`. The pool's only writer is `OSSymbol::initialize()`, whose only
two callers are a metaclass constructor nothing calls and `_GLOBAL__sub_I_OSSymbol.cpp` — `.init_array`
entry #6, in a table nothing runs. The constructors were in the image, correct and unreachable.

What finds them in a real kernel is XNU's own runtime, by **section name**:

```c
/* libkern/c++/OSRuntime.cpp:246 */
static boolean_t sectionIsConstructor(kernel_section_t * section) {
    result = !strncmp(section->sectname, SECT_MODINITFUNC, sizeof(SECT_MODINITFUNC) - 1);
    result = result || !strncmp(section->sectname, SECT_CONSTRUCTOR, sizeof(SECT_CONSTRUCTOR) - 1);
    return result;
}
/* :454-481, inside OSRuntimeInitializeCPP - for each segment, for each section */
if (sectionIsConstructor(section)) {
    structor_t * constructors = (structor_t *)section->addr;
    int num_constructors = section->size / sizeof(structor_t);
    for (int i = 0; i < num_constructors && OSMetaClass::checkModLoad(metaHandle); i++)
        if (constructors[i]) (*constructors[i])();
    ...
```

That scan **runs in this image** — 324 watched its two `strncmp`s execute and match nothing — and it
matches nothing because `entry_macho.s` described the `__DATA` segment with one section, an empty
`__const`. `entry.ld`'s own comment on the orphan `.init_array` (318) called the fix "a step of its own";
this is that step.

## The change, and the three numbers around it

| | before | after |
|---|---|---|
| `sizeofcmds` | 236 | **304** |
| `__DATA` `cmdsize` | 124 | **192** |
| `__DATA` `nsects` | 1 | **2** |
| new section | — | **`__DATA,__mod_init_func`**, `addr = __entry_init_array`, `size = __entry_init_array_size` |

`addr` and `size` are the two fields that call reads (`constructors = (structor_t *)addr`,
`num_constructors = size / sizeof(structor_t)`), and both are the linker's own symbols for the table, so
the record claims nothing the image does not already contain. `offset`, `align`, `reloff`, `nreloc` and
`flags` are read by neither that code nor anything else on the boot path and stay zero rather than being
invented. `ncmds` stays 3 — this is a second section of an existing segment.

## The host check caught the record before the device did

The first build emitted the new section record **one byte short**: `sectname` is 16 bytes and
`__mod_init_func` is 15, so it needed one more zero byte than the `__const` record's `.zero 9` pattern
made obvious. Every field after it then decoded one byte off:

```
  section  '_DATA'  '__mod_init_func_' addr=0x188015d4 size=0
FAIL: load command 2 is not LC_SEGMENT (cmd=0x38000000)
FAIL: sizeofcmds: got 304, expected 248
FAIL: no section named __mod_init_func
```

`tools/host_entry_macho_check.sh` found it by walking `cmdsize` — the same chain XNU's own readers walk —
which is why it decoded the *artifact* rather than the assembler source. The check was then extended with
the assertion this step exists for: the section must be named `__mod_init_func`, live in `__DATA`, be the
only section with that name, and — the part that matters — hold **exactly the linked table's address and
size**, asserted against `entry.ld`'s symbols:

```
  __mod_init_func               -> addr=0x8015d474 size=24 (6 constructors), segment '__DATA'
OK: the header is internally consistent, every field matches the linker's own symbols, ...
```

A right name with a wrong address is the failure mode that check is for: it would look correct in a header
dump and call whatever the address happened to be.

## The layout: only `.text`, and only by the record

| | base (328) | measured (329) | delta | predicted |
|---|---|---|---|---|
| `.text` | 0x1429C0 | **0x142A00** | +0x40 | +0x40 |
| `.data` | 0x80144000 (0x19368) | **0x80144000** (0x19368) | 0 | 0 |
| `.sysctl_set` | 0x8015D368 (0x10C) | **0x8015D368** (0x10C) | 0 | 0 |
| `.init_array` | 0x8015D474 (0x18) | **0x8015D474** (0x18) | 0 | 0 |
| `.bss` | 0x8015D4C0 (0x378D8) | **0x8015D4C0** (0x378D8) | 0 | 0 |
| `__bss_end` | 0x80194D98 | **0x80194D98** | 0 | 0 |
| image | 1430668 (0x15D48C) | **1430668 (0x15D48C)** | 0 | 0 |
| headroom | 1487464 | **1487464** | 0 | 0 |

`.text`'s +0x40 closes in two measured terms: **+0x44** of header (`xnu_arm_entry_macho.o`'s
`.rodata.macho` 0x108 → **0x14C** = 28 + 236 → 28 + 304 bytes, placed at the *same* address 0x8013E5E0,
because the growth happens in place) and **−0x04** of `.text`-region fill (0xD31 / 60 fills → 0xD2D / 59).
The counts do not move at all — **801 undefined / 692 function stubs / 109 storage**, byte-for-byte 328's
stub set — because this step links nothing.

The header's own derived fields moved with the text and the check asserts them against the linker symbols:
`__TEXT.vmsize` 0x1429C0 → **0x142A00**, `__DATA.vmaddr` 0x801429C0 → **0x80142A00** (`__entry_data_start`
is `__entry_text_end`, so it carries the alignment pad), `__DATA.vmsize` 0x523D8 → **0x52398** (it is
`__entry_image_end − __entry_data_start`, so it moves *against* the growth), `getlastaddr()` still
0x80194D98, and the PreLinkInfoDictionary range still the 0x268 bytes of slop of the image's last page
(242's rule, re-checked).

The image size is the one that could have moved and did not: the span is
`align16K(text_end) + .data + 0x10C + .init_array`, and 0x1429C0 and 0x142A00 both round up to 0x144000.
That is 304/318/319/321's rule doing real work — this step changed the header and grew `.text` while
leaving the image, the bss, the headroom and every address above the boundary **identical to the byte**.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=__cxa_atexit
 xnu_entry_stub_caller=0x8011d750   (also _a and _e)
 xnu_entry_stub_caller_w0=0x31313038   ("8011")   _w1=0x30353764   ("d750")   digits=0x30
 xnu_entry_abort_entries=0x00000000
```

`tools/host_resolve_entry_addr.sh 0x8011d750` → **`OSRuntimeInitializeCPP+0x184`**, whose `caller-4` is
`0x8011d74c: blx r0` — an *indirect* call, which is what the key should name here, and why the tool prints
"the `bl`, if the call was one". **The predicted symbol and the predicted key are both right.** Unlike
328's record this one is real and complete: `digits = 0x30` puts the two words at the caller record's own
first hex digit and they spell the key's eight digits (`d750` and `8011`).

One key, five measured facts, in order:

1. `OSRuntimeInitializeCPP`'s segment loop reached `__DATA`'s section list.
2. `sectionIsConstructor`'s `strncmp(sectname, …, 15)` **matched** — 324 had watched the same two
   comparisons fail on the same loop.
3. The section's size was divided by four into **six** constructors, and the `blx r0` at 0x8011D74C called
   the **first**: `_GLOBAL__sub_I_OSKext.cpp` (0x8011AEF4), which is `.init_array`'s entry #1.
4. That initializer ran `OSMetaClass::OSMetaClass("OSKext", &OSObject::gMetaClass, 0x60)` (0x8011DEE4) to
   completion — every one of its eight callees (`IOMalloc`, `__bzero`, `kalloc_canblock`, `memcpy`,
   `kfree`, `OSAddAtomic`, `OSKextLog`, `lck_mtx_lock`) is a real definition — and stored the vtable.
5. It then took its **tail branch** (`b __cxa_atexit` at +0x50, after `pop {r4, lr}`) into the stub, which
   is why the key is not the address after a `bl` in the initializer but the return address the initializer
   *inherited* from the scan's `blx`.

So the frontier is now **one instruction past the first C++ static constructor this kernel has ever run**,
and what stops it is a stub for a name **no XNU object defines**: `__cxa_atexit` is nowhere in the tree
(`grep -rn __cxa_atexit external/xnu-upstream/` returns nothing, and `__dso_handle` with it). This is kind
1 of a stop — a missing symbol — with a new property: it cannot be fixed by linking anything, because the
definition does not exist in XNU at all.

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, hardware watchdog ARMED, software dead-man armed at
60 s, no storage symbols in the payload), log **301624** bytes, one `stub_hit=` line, **no `exception:`
line** — 328's fault does not recur, because `OSSymbol::initialize()` is the *sixth* initializer and this
run ends inside the first. `xnu_entry_failures=0x00000000`, `xnu_entry_checks=0x00000005`,
`xnu_entry_image_bytes=0x0015d48c`, `xnu_entry_bss_start=0x8015d4c0`, `xnu_entry_bss_end=0x80194d98`,
`xnu_entry_kv_written=0x5d` with `xnu_entry_kv_dropped=0x00`,
`disarm_isenabler0 0x000c7fff → 0x00007fff`, `disarm_cntp_ctl 0x00000005 → 0x00000002`,
`disarm_hw_watchdog_en=0x00000001`.

**Safety:** non-persistent `fastboot boot` only, nothing flashed, the hardware watchdog armed across the
jump and not needed — the device returned to Android on its own (`ro.build.version.release` = 10) —
`persistent_write_attempted=0x00000000` ×25 and `failure_mask=0x00000000` ×87.

## What it measures, and what it does not

Measured: the C++ static-constructor machinery of this kernel works end to end, from the section name XNU
matches by `strncmp` to the first constructor's own body; and `__cxa_atexit` is the only thing between the
boot and the other five initializers. Not measured: what those five do
(`_GLOBAL__sub_I_OSMetaClass.cpp`, `OSDictionary.cpp`, `OSObject.cpp`, `OSCollection.cpp`, and
`OSSymbol.cpp`, whose `initialize()` writes `_ZL4pool`); whether any later initializer reaches a stub of
its own; and `checkModLoad`'s re-check between constructors — real since 324, but this run stops before it
is called a second time.

## Next

**`-fno-use-cxa-atexit`: a compile flag, not a link.** Since no object can define `__cxa_atexit`, the step
is to stop emitting the call. The six libkern C++ objects of this image are built by `arm-none-eabi-gcc`,
which supports the flag (Apple's `-fapple-kext` implies it). This is a **re-baselining step**: each of the
six `.init_array` entries shrinks by the call's setup, so the counts, `.text` and `.init_array` all move
and the ledger's numbers for 324–329 stop being comparable until they are re-measured.

The prediction after it is the one 328 wrote and could not reach: the six initializers run in order, the
sixth is `OSSymbol::initialize()`, `_ZL4pool` becomes non-null, the walk returns to `postModLoad`'s fifth
call, and the stop is **`_ZN8OSStringC2EPK11OSMetaClass` at the caller key
`_ZN8OSSymbol17withCStringNoCopyEPKc+0x50`** — the stub 328 predicted and faulted 0x38 before.
