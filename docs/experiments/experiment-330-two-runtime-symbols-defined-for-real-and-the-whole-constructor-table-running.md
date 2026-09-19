# Experiment 330 — `__cxa_atexit` and `__dso_handle` defined for real, and the whole constructor table running

**Step:** link nothing. Add two definitions to `stages/stage90/xnu_arm_boot/entry_stubs.c`: the C++
runtime's static-destructor registration `__cxa_atexit`, a no-op that returns success, and the
`__dso_handle` it is passed.

**Prediction:** *two stubs retire and nothing else moves — **799 undefined / 690 function / 109
storage** (801/692/109 at 329), `.text` shrinking by the retired stub bodies and names and growing by
the two definitions, `.data` taking `__dso_handle` in its first four bytes, and `.data`, `.sysctl_set`,
`.init_array`, `.bss`, `__bss_end`, the image and the headroom **identical to the byte** because 0x142A00
and 0x1429A0 round up to the same 16 KB boundary. The stop is 328's own prediction, now reachable:
**`_ZN8OSStringC2EPK11OSMetaClass` at the caller key `0x80121CB4` = `withCStringNoCopy+0x50`**.*

**Result:** **both halves exact**, and the absence of 328's fault is the proof that the whole
constructor table ran.

## What was missing, and why the fix is a definition

329 ended one instruction past the first of the six C++ static constructors, at a **tail branch into
`__cxa_atexit`**:

```
_GLOBAL__sub_I_OSKext.cpp:
    ...
    e1a01004   mov  r1, r4          ; arg  = the object
    e3002000   movw r2, #0          ; dso  = &__dso_handle
    e8bd4010   pop  {r4, lr}
    eafffffe   b    __cxa_atexit    ; the stub 329 stopped in
```

That name is defined **nowhere in XNU** — `grep -rn __cxa_atexit external/xnu-upstream/` returns nothing
at all, and `__dso_handle` with it — so this is the one stop in the walk that **no link can move**: there
is no object to grow the image with. It is also the one where the right definition is obvious rather than
approximate: **a kernel never exits**, so a static destructor is never run and registering one is a
successful nothing. The real `__cxa_atexit` returns 0 on success; this one returns 0.

The signature is read off the call sites rather than assumed — r0 the destructor, r1 the object, r2 the
`__dso_handle` address:

```c
int __cxa_atexit(void (*func)(void *), void *arg, void *dso_handle) { ...; return 0; }
void *__dso_handle = &__dso_handle;
```

## The flag is not this step, and that is a measurement

329's `Next` named `-fno-use-cxa-atexit`. Experiment 330 compiled all 83 C++ files with each candidate
and read the objects back (`tools/build_xnu_arm_kernel.sh` grew an `XNU_KERNEL_EXTRA_CXXFLAGS` hook for
it — the same shape as the existing define hook, C++-only, so the comparison is one command against the
real build and not a hand-copied flag list):

| flags | what the object calls | cost |
|---|---|---|
| (none) | `b __cxa_atexit`, and `__dso_handle` | 329's stop |
| `-fno-use-cxa-atexit` | `b atexit` — and **XNU has no `atexit` either** | renames the stop, moves it no distance |
| `-fapple-kext` (`makedefs/MakeInc.def:371`, `CXXFLAGS_GEN = -fapple-kext`) | **no registration call at all** | also moves vtable emission to the key function's TU: `OSCollection.o`, `OSDictionary.o`, `OSObject.o`, `OSKext.o`, `OSSymbol.o` acquire references to `_ZTV8OSObject`, `_ZTV12OSCollection` and **`_ZTV8OSString`** — the last defined by `OSString.cpp`, an object this image does not link, so it would arrive as a *storage stand-in for a vtable*. Linked C++ text grows ~13 KB (OSKext.o 74436 → 83272), moving the 16 KB boundary and every address above it, and re-baselining 324–329 |

So the flag is a step of its own, with its own measurement, and the step that moves *this* frontier is
the smaller one. The measurement is not wasted: it is the reason `-fapple-kext` is *next* rather than
*now*, and it is the reason `OSString.cpp` is the natural object after that.

## The layout: two stubs' worth of text, nothing above the boundary

| | base (329) | measured (330) | delta | predicted |
|---|---|---|---|---|
| undefined | 801 | **799** | −2 | −2 |
| function stubs | 692 | **690** | −2 | −2 |
| storage stand-ins | 109 | **109** | 0 | 0 |
| `.text` | 0x142A00 | **0x1429A0** | **−0x60** | shrink |
| `.data` | 0x80144000 (0x19368) | **0x80144000** (0x19368) | 0 | 0 |
| `.sysctl_set` | 0x8015D368 (0x10C) | **0x8015D368** | 0 | 0 |
| `.init_array` | 0x8015D474 (0x18) | **0x8015D474** (0x18) | 0 | 0 |
| `.bss` | 0x8015D4C0 (0x378D8) | **0x8015D4C0** (0x378D8) | 0 | 0 |
| `__bss_end` | 0x80194D98 | **0x80194D98** | 0 | 0 |
| image | 1430668 (0x15D48C) | **1430668 (0x15D48C)** | 0 | 0 |
| headroom | 1487464 | **1487464** | 0 | 0 |

`.text`'s −0x60 closes in **four measured terms with no residual**:

- **+0x08** — the two real definitions in `xnu_arm_entry_stubs.o` (`__cxa_atexit`'s twelve bytes plus
  alignment).
- **−0x30** — the two retired stub bodies, 0x18 each. Measured by putting the two functions back into a
  copy of the generated `xnu_arm_entry_realstubs.c` and compiling it: `.text` 16560 → 16608.
- **−0x20** — their two name literals in the *merged* string pool: `xnu_arm_entry_realstubs.o`'s merged
  strings 0x390F → **0x38EF**. (The object-level delta is 0x1E; the linked pool rounds it to 0x20.)
- **−0x18** — `.text` fill: **0xD2D / 59 entries → 0xD15 / 57**, the same sign as 329's −0x004.

**Two of the deletes are ordinary and one is not.** The retired stub bodies and names are the standard
arithmetic of a name becoming real (322's rule: one padded slot per function stub, none for storage).
The one worth recording is that **`__dso_handle` was classified as a *function* stub**: no kernel object
defines it, so the generator's kind lookup (`nm -S` over the kernel objects) found no type and no size and
fell through to the function branch — which is how a name that is really four bytes of data was being
passed to `__cxa_atexit` as an *address* (of the reporting stub's own code). Harmless, because nothing
reads it; visible, because storage did not move while function count fell by two.

Everything above the boundary is identical to the byte, and this time the proof is a *shrinking* `.text`:
329 proved the span's rule from a growing section that stayed inside its 16 KB bucket, 330 proves it from
one that shrank and stayed inside the same bucket. `.data`'s own fill fell 0x7AA7 → **0x7AA3** — the four
bytes the real `__dso_handle` takes.

## The image, symbol for symbol

329's image rebuilds byte-for-byte from the same sources, so the two symbol tables can be compared
directly rather than argued about. Of 6875 symbols **none is added and none retired**; two move far and
one moves four bytes:

| symbol | 329 | 330 |
|---|---|---|
| `__cxa_atexit` | 0x801224D0 (a stub in `realstubs.o`) | **0x80002CD8** (in `entry_stubs.o`) |
| `__dso_handle` | 0x801225C0 (a stub *function*) | **0x80144000** (a real 4-byte object, first in `.data`) |
| `pc_trace_cnt` | 0x80144000 | 0x80144004 — displaced by those four bytes |

**No `.bss` symbol moves at all.** The rest of the diff is inside `.text`: 87 symbols after the new
definitions **+0x8**, 768 after the two retired bodies **−0x30**, the 9 between them **−0x18**, and 11
beyond the string pool **−0x50**.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN8OSStringC2EPK11OSMetaClass
 xnu_entry_stub_caller=0x80121cb4
 xnu_entry_abort_entries=0x00000000
```

`tools/host_resolve_entry_addr.sh 0x80121cb4` → **`_ZN8OSSymbol17withCStringNoCopyEPKc+0x50`**, whose
`caller-4` is `0x80121cb0: bl 80126058 <_ZN8OSStringC2EPK11OSMetaClass>` — a *direct* call, so this key
names its call site exactly. **This is the stop 328 predicted and faulted 0x38 before reaching.**

**And the way its absence is proved is the measurement of the step.** `withCStringNoCopy`'s second
instruction is `ldr r0, [r0, #16]` on `_ZL4pool` — the instruction 328 faulted on with `r0 = 0` — and the
walk cannot reach +0x50 unless that load succeeded. `_ZL4pool` has exactly one writer,
`OSSymbol::initialize()`, which is the **sixth** entry of the constructor table. So:

> **All six constructors ran**, in table order, through `checkModLoad` between them. The C++ static
> constructor machinery of this kernel is complete, and 328's `data abort` is gone
> (`abort_entries=0x00000000`). What stops the boot now is ordinary missing code.

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, hardware watchdog ARMED, software dead-man armed at
60 s), log **301642** bytes, one `stub_hit=` line, no `exception:` line,
`xnu_entry_failures=0x00000000`, `xnu_entry_checks=0x00000005`,
`xnu_entry_image_bytes=0x0015d48c`, `xnu_entry_bss_start=0x8015d4c0`, `xnu_entry_bss_end=0x80194d98`,
`xnu_entry_kv_written=0x6f` (111 records, up from 329's 0x5d = 93) with `xnu_entry_kv_dropped=0x00`,
`disarm_isenabler0 0x000c7fff → 0x00007fff`, `disarm_cntp_ctl 0x00000005 → 0x00000002`,
`disarm_hw_watchdog_en=0x00000001`.

**Safety:** non-persistent `fastboot boot` only, nothing flashed, the hardware watchdog armed across the
jump and not needed — the device returned to Android on its own (`ro.build.version.release` = 10) —
`persistent_write_attempted=0x00000000` ×25 and `failure_mask=0x00000000` ×87.

## What it measures, and what it does not

Measured: the entire `.init_array` table runs, every entry, in order; the first C++ constructor this
kernel ever ran (329) is now one of six that complete; and the missing-symbol stop that no link could fix
is fixed by a definition whose semantics are exact rather than convenient. Not measured: how far
`OSString`'s constructor gets (it is an object this image does not link yet), and whether the
`-fapple-kext` re-baseline changes any of this beyond the addresses — that is a step of its own, and its
cost is now measured before it is taken.

## Next

**Link `libkern/c++/OSString.cpp`** (`libkern_c++_OSString.o`) — the object that defines this stop,
`OSString::initWithCStringNoCopy` and `OSString::free`, i.e. three of the remaining stubs in the very body
the walk is in. Its prediction follows the same two-part shape every step since 320 has used: the counts
by kind, and a stop read off the body in the image the device runs.

**And after it, `-fapple-kext`** — as a deliberate re-baseline rather than a surprise, with the
measurement above standing as its cost: the call goes away everywhere, at the price of new `_ZTV*`
references (one of which, `_ZTV8OSString`, this step may have already resolved by linking `OSString.cpp`)
and ~13 KB of C++ text.
