# 993: what dyld was actually failing at when `throwf` itself OOM'd

> **SUPERSEDED IN ONE RESPECT (2026-10-12, 994 press `armed-d13-8b4c9a3e`, commit `a4a2532`).**
> §0's answer ("the halt message is the formatter's own OOM literal") was true for 993 **because
> trap −10 was broken**. The 994 wire fix restored the formatter: the halt message is now a real
> formatted **heap** string, and its length is arithmetically pinned to **68..71 bytes**. Under
> that window this document's own §3 / §7.3 **rank 1 is excluded** (its message needs three args
> summing 17..20; the real ones sum ≥ 38). The re-converged ranked list, the rewrap site's three
> arguments, the launchd↔cache cross-check and a pre-registered expectation table for 995's
> witness are all in **§10**.

Read-only forensics over the already-extracted artifacts:

| artifact | what it is |
|---|---|
| `/tmp/dyld_ios7` | iOS 7.1.2 `/usr/lib/dyld` (MH_DYLINKER), 196,576 B, `__TEXT` vmaddr `0x2be00000` |
| `/tmp/dyld_objd.txt` | `llvm-objdump --macho -d` of it (43,112 lines) |
| `/tmp/k993_win.txt` | 993's last_kmsg window around the event |
| `/tmp/throwf_table.txt` | all 145 `bl dyld::throwf` sites with format strings, derived from the dump |

**Address convention.** 993's slide is `0x7D000` (measured, `docs/reference/d13-crash-state-forensics.md` §2).
Every address in this document is **unslid** (link-time) unless a slid one is given explicitly;
`unslid = slid − 0x7D000`. Section 5 shows how every address was derived.

Tags used on every claim: **[测]** measured from the bytes/disassembly in this analysis ·
**[引]** quoted from an existing repo doc (named inline) · **[推]** inference (always stated as such).

---

## 0. Answer in one paragraph

993 died in `dyld::halt` reached from **`_main+0x82c` @ `0x2be04be0`** (halt site #8, the `catch (const char*)`
pad) with message `0x2be1c7fd` = **`"throwf, out of memory error"`**. That message is *not* dyld's complaint
about the system — it is **the formatter's own failure**. `dyld::throwf` calls `__simple_salloc()` *before* it
formats anything, and `__simple_salloc` is a `vm_allocate(mach_task_self(), &hint, 4096, VM_FLAGS_ANYWHERE)`.
On the D13 kernel that trap (−10) reads `flags` out of the **size-high** wire word, which is 0, so the
allocation takes the fixed-address branch and fails; `__simple_salloc` returns NULL and `throwf` throws its
own OOM literal instead of the real message **[测]** (section 6). The real first failure is therefore one of
the `throwf` calls in the window
**`ImageLoader::link` entry (`0x2be09a68`) → `dyld::initializeMainExecutable` (`0x2be04a88`)** — 991 proves
the run got as far as `ImageLoader::link+0x37` (`lr = 0x2be58a9f`), and `r1 = 1` at the crash proves
`initializeMainExecutable` had **not** been entered **[推]**, bounded by the byte-151 witness (section 7.1).
Inside that window the only dyld work is the **link phase of the main executable's dependent dylibs**;
ranked list in section 3, led by
**`ImageLoader::recursiveLoadLibraries+0x66a` @ `0x2be0a35e` — `"Library not loaded: %s\n  Referenced from: %s\n  Reason: %s"`**.

---

## 1. The formatter's self-destruct (measured, this is the load-bearing mechanism)

`dyld::throwf` @ `0x2be01640` **[测]**:

```
2be01640  sub sp, #12 ; push {r4,r5,r7,lr} ; add r7,sp,#8 ; sub sp,#4
2be01648  add.w r4, r7, #8            ; va_list area
2be0164c  mov r5, r0                  ; fmt
2be0164e  stm.w r4, {r1,r2,r3}
2be01652  bl  __simple_salloc         ; <-- FIRST call: 4 KiB buffer
2be01656  mov r4, r0
2be01658  cbz r4, 0x2be0167e          ; NULL -> throw the OOM literal
2be01664  bl  __simple_vsprintf
2be0166a  bl  __simple_string
2be0166e  bl  _strdup                 ; malloc'd copy
2be01672  mov r5, r0
2be01676  bl  __simple_sfree
2be0167a  cmp r5, #0
2be0167c  bne 0x2be0169c              ; ok -> throw the real message
2be0167e  movs r0, #4
2be01680  bl  ___cxa_allocate_exception
2be01684  movw r1,#63848 ; movt r1,#1 ; add r1,pc   -> 0x2be21000 (typeinfo slot)
2be0168c  movw r2,#45411 ; movt r2,#1 ; add r2,pc   -> 0x2be1c7fd  <-- "throwf, out of memory error"
2be01698  str r2, [r0]                              ; 4-byte exception object = the char*
2be016ae  bl  ___cxa_throw
```

* `0x2be1c7fd` = `"throwf, out of memory error"` **[测]** (raw window: `…"mkstringf, out of memory error"\0"throwf, out of memory error"\0"dyld: warning, "…`, the mkstringf variant sits **0x1F bytes earlier at `0x2be1c7de`** — the near-miss is excluded by the arithmetic, not by eyeball).
* The OOM literal is thrown on **either** `salloc == NULL` (`cbz` at `0x2be01658`) **or** `strdup == NULL`
  (`cbz`/`bne` at `0x2be0167a`). Distinguishing them is section 6.

`__simple_salloc` @ `0x2be18dac` **[测]**:

```
2be18dac  push {r7,lr}
2be18db2  movw r0,#33372 ; movt r0,#0 ; add r0,pc -> 0x2be21020 (GOT: _mach_task_self_)
2be18db6  mov r1, sp                 ; &hint   <-- the hint is an UNINITIALISED stack slot (sub sp,#4 just above)
2be18dbc  mov.w r2, #4096            ; size
2be18dc2  movs r3, #1                ; flags = VM_FLAGS_ANYWHERE
2be18dc8  bl  _vm_allocate
2be18dcc  mov r1, r0
2be18dce  movs r0, #0
2be18dd0  cbnz r1, 0x2be18df6        ; nonzero kern_return -> return NULL
```

`_vm_allocate` @ `0x2be1bb70` → `_mach_vm_allocate` @ `0x2be1baa4` → the naked trap, all in the dyld image **[测]**:

```
2be1bb70  _vm_allocate: … 2be1bb8a str r3,[sp] (flags) ; 2be1bb8c movs r3,#0 (size-high) ; 2be1bb8e bl _mach_vm_allocate
2be1baae  ldr r6,[r7,#8]  (arg5 = flags)   2be1baba blx __kernelrpc_mach_vm_allocate_trap
2be1bac6  cmp r5,#0x10000003 ; (MACH_SEND_INVALID_DEST -> MIG fallback __kernelrpc_mach_vm_allocate @2be1af20)
2be1b410  __kernelrpc_mach_vm_allocate_trap:
2be1b410    mov r12, sp
2be1b414    push {r4, r5}
2be1b418    ldr r4, [r12]        ; 5th wire word
2be1b41c    mvn r12, #9          ; r12 = 0xFFFFFFF6 = -10
2be1b420    svc #128
2be1b424    pop {r4, r5}
2be1b428    bx lr
```

So the on-the-wire call is five 32-bit words **`(target, &addr, size_low, size_high, flags)`** —
`r0=task, r1=&hint, r2=0x1000, r3=0x0, r4=0x1` **[测]**, exactly the wire the kernel-side comment in
`external/xnu-hd2-darwin13/xnu/osfmk/ipc/mach_kernelrpc.c` describes for iOS 7 ARMv7. The D13-native body
reads a 4-field struct whose `flags` lands on the **size_high** word = 0 ⇒ not ANYWHERE ⇒ fixed-address
allocation at whatever garbage the uninitialised stack slot holds ⇒ nonzero kern_return ⇒ NULL ⇒
`"throwf, out of memory error"` **[推]** (the *mechanism* is measured; the exact failure code and the
garbage value are not — NON-CLAIMS §8.2).

**This is the single most important consequence for the team:** every real dyld error message on this
kernel is replaced by `throwf, out of memory error`, because the formatter is the first thing to need a
fresh `vm_allocate`. Fixing trap −10 does not merely fix an allocation, it **restores dyld's error
reporting**, which is what tells us where the real wall is.

---

## 2. `dyld::halt` and the crash registers (why the message is trustworthy)

`dyld::halt` @ `0x2be01ebc` **[测]**:

```
2be01ebe  mov r5, r0                              ; msg
2be01ec0  movw r0,#44742 ; movt r0,#1 ; add r0,pc -> 0x2be1cd94 = "dyld: %s\n"
2be01ece  bl  dyld::log
2be01ed2  movw r4,#30336 ; movt r4,#4 ; add r4,pc -> 0x2be49564 (_error_string, __common, 1024 B)
2be01ee4  bl  _strlcpy(_error_string, msg, 1024)
2be01ee8  movw r0,#64104 ; movt r0,#1 ; add r0,pc -> &gLinkContext (0x2be2195c)
2be01ef2  ldrb.w r0, [r0, #151]
2be01ef6  eor r1, r0, #1                           ; second arg = !gLinkContext+151
2be01efc  bl  setAlImageInfosHalt
2be01f00  mov r0, r4
2be01f02  blx _dyld_fatal_error                    ; udf
```

`setAlImageInfosHalt` @ `0x2be08d04` **[测]**: `ldr r3,[r2]` (`r2` → `dyld::gProcessInfo` @ `0x2be21b1c`) →
`str r0,[r3,#32]` (aii+0x20 = message) → `str r1,[r0,#36]` (aii+0x24 = flags) → `bx lr`.

Crash block (from `docs/reference/d13-crash-state-forensics.md` §1) re-read against this disassembly **[测]**:

| reg | value | reading |
|---|---|---|
| `pc` | `0x2be7e0c4` | `_dyld_fatal_error` + 0 (unslid `0x2be010c4`) |
| `lr` | `0x2be7ef07` | `halt+0x4b` — the `blx` return slot at `0x2be01f06` (unslid `0x2be01f07`) |
| `r0` | `0x2bec6564` | `_error_string` (unslid `0x2be49564`, `halt`'s `r4`) |
| `r1` | `1` | `!byte[gLinkContext+151]` — so **+151 was 0** |
| `r5` | `0x2be997fd` | the message (unslid `0x2be1c7fd` = the `throwf` OOM literal) |
| `r6` | `2` | the catch pad's own type discriminator (see §4) |

None of `r4`,`r5`,`r6` is written by `halt` after those points nor by `setAlImageInfosHalt`, and
`_dyld_fatal_error` is a bare `udf` **[测]**, so the register block is a faithful post-mortem of the call.

`gLinkContext+151` has exactly **two** references in the whole image: the store `strb.w r1,[r0,#151]` at
`0x2be01cbe` (first statement group of `dyld::initializeMainExecutable`) and this load **[测]**
(`grep '#151]'` over the dump returns those two). Therefore **`r1 = 1` at the crash proves
`initializeMainExecutable` was never entered** **[推, strong]** — the flag is 0 for the entire
`_main` body up to `0x2be04a88`.

---

## 3. Q1 — `throwf` call sites reachable from the startup path, ranked

All 145 `bl dyld::throwf` sites were re-enumerated from the dump with their enclosing function and offset
(`/tmp/fx22.log`). Restricted to the 993 window (§7.1) and its callees, in rank order.

| # | site (unslid) | enclosing function | message (fmt string) | why reachable / rank |
|---|---|---|---|---|
| **1** | **`0x2be0a35e` (+0x66a)** | **`ImageLoader::recursiveLoadLibraries`** | **`Library not loaded: %s\n  Referenced from: %s\n  Reason: %s`** (`0x2be1e0df`, loaded at `0x2be0a350`) **[测]** | `link`'s **first substantive callee** (`bl` @ `0x2be09aaa`). launchd's `LC_LOAD_DYLIB`s are `/usr/lib/libbsm.0.dylib` and `/usr/lib/libSystem.B.dylib`, neither of which is on the root fs — they must come from the shared cache. Any failure to resolve them surfaces here. **[推]** |
| **2** | `0x2be0a1b6` (+0x4c2) | `ImageLoader::recursiveLoadLibraries` | version/architecture compatibility throw (the literals `Incompatible` `0x2be1e0c5` and `architecture` `0x2be1e0d2` are set up at `0x2be0a2b2` / `0x2be0a2cc` **[测]**; the pre-existing table reads this site as `Incompatible library version: %s requires version %d.%d.%d or later, but %s provides version %d.%d.%d` **[引]** `/tmp/throwf_table.txt`) | same call site family; fires if a cache image's version/platform does not match the loader's expectation **[推]** |
| **3** | `0x2be0b80a`, `0x2be0b824`, `0x2be0b888`, `0x2be0b8c8`, `0x2be0b8da`, `0x2be0b92e`, `0x2be0b93c` (7 sites) | `ImageLoaderMachO::sniffLoadCommands` | `malformed mach-o image: …` family **[引]** `/tmp/throwf_table.txt` | reached from `recursiveLoadLibraries → dyld::load → loadPhase3/5/6 → instantiateFromFile/FromCache`; fires if the bytes being read as a mach header are not one (e.g. a mis-mapped or unmapped shared-cache page) **[推]** |
| **4** | `0x2be0bf64`, `0x2be0bf7e` | `ImageLoaderMachO::parseLoadCmds` | `cannot load '%s' (load command 0x%08X is unknown)` etc. **[引]** | same path, one step later **[推]** |
| **5** | `0x2be0dbf8`, `0x2be0dc26` | `ImageLoaderMachO::mapSegments` | `mmap() error %d at address=0x%08lX, size=0x%08lX segment=%s in Segment::map() mapping %s` **[引]** | mapping a dependent that was found as a file (not in cache); on D13 `mmap`/`vm_map` is exactly the trap family that is broken **[推]** |
| **6** | `0x2be0cf68` | `ImageLoaderMachO::throwSymbolNotFound` | `Symbol not found: %s\n  Referenced from: %s\n  Expected in: %s` **[引]** | reached during `recursiveBind` (`bl` @ `0x2be09b3a`) if a dependency is missing/degraded **[推]** |
| **7** | `0x2be0d990` | `ImageLoaderMachO::assignSegmentAddresses` | `can't map unslidable segment %s to 0x%lX with size 0x%lX` **[引]** | same mapping family as #5 **[推]** |
| **8** | 22 + 18 + 13 + … sites | `ImageLoaderMachOCompressed::rebase` / `eachBind` / `exportedSymbolAddress` / `resolve` / `incrementCoalIterator` / `updateUsesCoalIterator`, and the `Classic` counterparts | uleb/opcode-corruption family **[引]** | only if the image bytes themselves are garbage — i.e. a variant of #3/#5 **[推]** |
| 9 | `0x2be0c564` | `ImageLoaderMachO::loadCodeSignature` | `code signature invalid for '%s'` **[引]** | requires an `LC_CODE_SIGNATURE` that fails; unlikely on these unslid images **[推]** |
| 10 | `0x2be09366` | `ImageLoader::ImageLoader` (ctor) | `too many dependent dylibs in %s` **[引]** | structurally possible, practically not with 2 dependencies **[推]** |

**Excluded by measurement, not by opinion.**

* `dyld::isCompatibleMachO` (`throwf` @ `0x2be02a1e`, `incompatible cpu-subtype: 0x%08X in %s`) is called from
  `_main` **before** `link` (`bl` @ `0x2be04812`, §4). 991 measured its death inside `ImageLoader::link` —
  so 993 passed this check **[推, on 991's measured `lr`]**.
* `dyld::loadPhase6` / `loadPhase3` sites run **inside** `recursiveLoadLibraries`, so they reach the caller's
  message as `Reason: %s` (rank 1), not as their own top-level error **[推]**.
* `mapSharedCache` contains **no** `throwf` at all (§5) **[测]**.
* `dyld::_main`'s own two `throw`s are `const char*` throws built by `___cxa_allocate_exception` + a literal
  (`main executable not a known format` `0x2be1d32d` at `0x2be049b4`) **[测]** — these need **no** formatter and
  therefore could **not** have produced an OOM message. Their absence is part of what makes the OOM meaningful.

---

## 4. Q2 — `_main`'s measured early sequence

Call census of `_main` (`0x2be043b4 … 0x2be04c54`) with every `bl`/`blx` resolved (`/tmp/fx20.log`) **[测]**,
and every pc-relative string reference resolved (`/tmp/fx25.log`) **[测]**:

| addr | call / reference | note |
|---|---|---|
| `0x2be04406` | `dyld::setContext(mh, argc, argv, envp, apple)` | gLinkContext filled |
| `0x2be04416` | `__simple_getenv` with key `executable_path` (`0x2be1ce9c`) | env, not OS version |
| `0x2be0444a` | `__Unwind_SjLj_Register` | **`_main`'s try region starts here** |
| `0x2be04472`-`0x2be044c2` | `getcwd`, `strlen`×3, `strcpy`, `strcat`, `strrchr` | builds `sExecPath` |
| `0x2be044e6` | `issetugid` | |
| `0x2be044fc`/`0x2be04514` | literals `__restrict` / `__RESTRICT` (`0x2be1d454`/`0x2be1d449`) | |
| `0x2be0456e` | `dyld::checkEnvironmentVariables` (`DYLD_` `0x2be1d364`, `LD_LIBRARY_PATH=` `0x2be1d370`) | |
| `0x2be0463c` | `dyld::setContext` (second call) | |
| `0x2be04654` | literal `opt[%i] = "%s"\n` | verbose-opt logging |
| `0x2be0466e`, `0x2be046a0` | `dyld::log` | |
| `0x2be04700`-`0x2be0478a` | vector `reserve`/`__push_back_slow_path` | sImageRoots/sAllImages/sDOFvectors |
| `0x2be047a8` | `usleep` | |
| `0x2be04812` | **`dyld::isCompatibleMachO(&mh, path)`** | cputype/subtype gate; its failure pad throws `main executable not a known format` at `0x2be049b4` **[测]** |
| `0x2be04834` | **`ImageLoaderMachO::instantiateMainExecutable`** | |
| `0x2be04842` | `dyld::addImage` | |
| `0x2be048a8` | **`dyld::mapSharedCache()`** | §5 |
| `0x2be048f2` | `dyld::load(path, LoadContext)` | the `DYLD_INSERT_LIBRARIES` loop |
| `0x2be04940` | **`dyld::link(sMainExecutable, …)`** → `ImageLoader::link` @ `0x2be09a68` | **991/993's death region** |
| `0x2be04958` | `ImageLoader::setNeverUnloadRecursive` | |
| `0x2be04976` | indirect call `[[gLinkContext-ish]]+136`, `cmp r0,#1` → sets `+149`/`+136` | not identified by name |
| `0x2be04a08` | `dyld::link(image, …)` **in a loop** over the remaining images | inserted libraries / other roots |
| `0x2be04a46` | `ImageLoader::applyInterposing` | on the main executable |
| `0x2be04a7c` | `ImageLoader::weakBind` | on the main executable |
| `0x2be04a88` | **`dyld::initializeMainExecutable()`** | window's upper bound |
| `0x2be04aaa` | `ldr r0,[&gLibSystemHelpers(0x2be21a04)]` → `cbz` → halt `libdyld.dylib support not present for LC_MAIN` (`0x2be1ceac`) @ `0x2be04ad6` | **[测]** |

Answers to the brief's specific questions **[测]**:

* **Does it map the shared cache?** Yes — `dyld::mapSharedCache()` @ `0x2be04fa0`, called at `0x2be048a8`,
  **after** the main executable is instantiated and **before** `link`. It checks the magic
  `dyld_v1   armv7` (`0x2be1cf46`, compared at `0x2be05024`/`0x2be05072`) and reads
  `/System/Library/Caches/com.apple.dyld/dyld_shared_cache_armv7` (`0x2be1d1e6`) plus
  `enable-dylibs-to-override-cache` (`0x2be1d251`).
* **Does it check a version/OS-release string?** No OS-release/version string is referenced anywhere in
  `_main`'s pc-relative string set (full list in `/tmp/fx25.log`; the only strings are the env/verbose ones
  in the table above and the halt messages). The cache-compatibility check is the `dyld_v1   armv7` magic,
  not a version string. **[测]**
* **Does it load `libSystem.B.dylib` itself?** Not directly — no `/usr/lib/...` literal exists in `_main`;
  dependencies are loaded inside `recursiveLoadLibraries` (rank 1). **[测]**
* **Look up `_dyld_…`?** No `_dlsym`/`_dyld_lookup` call in `_main`'s call set; the only export-table work is
  the `dyld_funcs` table (see `docs/reference/d13-dyld-helper-table.md`). **[测]**
* **Read `dyld_all_image_infos`?** `_main` **writes** it (via `dyld::gProcessInfo` `0x2be21b1c`,
  `addImagesToAllImages`, `addNonSharedCacheImageUUID` @ `0x2be047f6`); the readers are dyld's exported
  `_dyld_*` functions. Nothing early in the startup path depends on reading it. **[测]**

Which of these could fail on a kernel that has the commpage alias but mis-reads trap −10?
Only the things that allocate: **(a)** any `throwf`'s formatter (fatal, §1); **(b)** `_malloc`'s pool refill (§6);
**(c)** address reservation / segment mapping for images not served from the cache. The **mapping of the cache
itself does not allocate through `vm_allocate`** — it goes through the shared-region syscalls
(`_shared_region_check_np` `0x2be05774`, `_shared_region_map_and_slide_np` `0x2be0579c`, both in the image)
**[测]**, and every one of its failure paths is a `dyld: shared cached file …` **log**, not a throw (§5).

---

## 5. Q3 — earlier / more specific markers, and why the console tells us nothing

**All 13 `dyld::halt` call sites** re-enumerated from the dump (`/tmp/fx23.log`) **[测]** —
this reproduces and confirms `docs/reference/d13-dyld-halt-callers.md`:

| site | function + offset | message source |
|---|---|---|
| `0x2be01e34` | `dyld::runTerminators+0xb4` | `mkstringf`-built |
| `0x2be03554` | `dyld::bindLazySymbol+0xcc` | |
| `0x2be0365c` | `dyld::fastBindLazySymbol+0xd4` | `fast lazy binding from unknown image` |
| `0x2be0369a` | `dyld::fastBindLazySymbol+0x112` | |
| `0x2be04ad6` | `dyld::_main+0x722` | `libdyld.dylib support not present for LC_MAIN` (`0x2be1ceac`) |
| `0x2be04b60` | `dyld::_main+0x7ac` | `could not load inserted library '%s' because %s\n` (`0x2be1cf15`) |
| `0x2be04b7c` | `dyld::_main+0x7c8` | `could not load inserted library '%s'\n` (`0x2be1ceef`) |
| **`0x2be04be0`** | **`dyld::_main+0x82c` — the `catch (const char*)` pad** | **rcvd `[sp+20]`; 993's crash** |
| `0x2be07aae` | `unimplemented()+0xe` | |
| `0x2be090f2` | `_abort+0xe` | `dyld calling abort()\n` |
| `0x2be09166` | `_exit+0xe` | `exit()` — **only caller is `_malloc`** |
| `0x2be09216` | `__stack_chk_fail+0xe` | `stack buffer overrun` |
| `0x2be09266` | `std::__vector_base_common<true>::__throw_length_error()+0xe` | |

**The catch pad `0x2be04bb2`-`0x2be04be4` decoded [测]** — this is what pins 993 to a `const char*` throw:

```
2be04bb0  movs r4, #2                 ; discriminator value
2be04bb2  bl  ___cxa_begin_catch
2be04bb6  str r0, [sp, #20]           ; the caught pointer = the message
2be04bb8  cmp r6, r4
2be04bba  beq 0x2be04bd2              ; r6 == 2  ->  the fatal (const char*) path
2be04bbc  str r5, [sp, #20]
2be04bbe  r0 -> "dyld: launch failed\n" (0x2be1ceda)
2be04bcc  bl  dyld::log               ; non-const-char* exception: log and RETURN (no halt!)
2be04bd0  b   0x2be04bf0             ;   -> __cxa_end_catch / SjLj_Unregister / return
2be04bd2  movs r0,#2 ; str [sp,#108]
2be04bd6  bl  dyld::syncAllImages
2be04bde  ldr r0, [sp, #20]
2be04be0  bl  dyld::halt              ; <-- SITE #8
```

The measured `r6 = 2` at the crash therefore selects the **`const char*`** branch **and** proves the
`"dyld: launch failed"` log at `0x2be04bcc` was **not** executed **[测]** — consistent with
`docs/reference/d13-crash-state-forensics.md`'s register decode, now with the reason named: `r6` is the pad's
own discriminator, `2` means "the exception payload was a `const char*`" (i.e. a `throwf`/`throw "literal"`,
never a C++ object).

**Is there a marker earlier than the OOM?** Yes — but it is *destroyed*, not missing:
`throwf` never reaches `vlog`; the real text only exists as the `%s` argument it was about to format.
What survives is the **exception payload slot** `[sp+20]` at the halt and `aii+0x20` after
`setAlImageInfosHalt` — and by then it has already been overwritten with the OOM literal (§2).
The LEO witness formula in `docs/reference/d13-dyld-helper-table.md` §4
(`state->pc == info-0x20bc8`, `state->r[0] == info+0x278d8`) is the recovery route for a *future* run, and it
will read the OOM literal too unless trap −10 is fixed first.

**The console is silent — and that proves nothing [测 + 推].** `dyld::vlog` @ `0x2be016d4` **[测]**:

```
2be016dc  add r0, pc            -> &gLibSystemHelpers (0x2be21a04)
2be016e4  ldr r0, [r0]
2be016e6  cbz r0, 0x2be016f4                 ; helpers == NULL -> fall through to fd 2
2be016ea  cmp r1, #11 ; blo 0x2be016f4       ; helpers->version < 11 -> fd 2
2be016ee  ldr r0, [r0, #72] ; blx r0         ; helpers->log(...)
2be016f4  movs r0, #2 ; blx _fstat           ; is fd 2 usable?
2be01702  movs r0, #2 ; bl  __simple_vdprintf ; write(2, ...)  <-- the NULL-helpers fallback
2be0170e  … socket(AF_LOCAL) path, __simple_salloc + getpid + … (dyld log server)
```

So a NULL `gLibSystemHelpers` does **not** silence logging — it falls back to `__simple_vdprintf(2, …)`, and
only if fd 2 is unusable does it try a unix socket. **The 993 capture contains zero dyld text** (`/tmp/k993_win.txt`),
so either no `log()` call executed (consistent with a silent `throwf` path) **or** pid 1's fd 2 was not
writable. Both remain open — see NON-CLAIMS §8.4.

---

## 6. `_malloc`, and why the OOM is the formatter and not the allocator

`_malloc` @ `0x2be0dfc0` **[测]**:

```
2be0dfc8  movw/movt/add pc -> 0x2be21028 (GOT entry whose target is 0x2be21a04)
2be0dfd2  ldr r0,[r0] ; ldr r0,[r0]           ; r0 = gLibSystemHelpers
2be0dfd6  cbz r0, 0x2be0dfe0                  ; NULL -> dyld's own pool
2be0dfd8  ldr r2,[r0,#16] ; blx r2            ; helpers->malloc(size)
2be0dfe0  pool path: bump pointer in __MergedGlobals (0x2be21b30)
2be0e01c  bl _vm_allocate(mach_task_self(), &chunk, 153600, VM_FLAGS_ANYWHERE=1)
2be0e020  cbnz r0, 0x2be0e048
2be0e048  log("out of address space for dyld memory pool\n") (0x2be1eedf) ; _exit(1)
2be0e05c  log("dyld memory pool exhausted: size=%lu\n") (0x2be1ef0a)       ; _exit(1)
```

`_exit` @ `0x2be09158` → `halt("exit()")` (site #11, `0x2be09166`) **[测]** — **the pool path never returns
NULL; it dies with a *different* message.** Therefore:

* If `gLibSystemHelpers != NULL`, `_malloc` can return NULL legitimately and the `strdup` at `0x2be0166e`
  could be the failing allocation.
* If `gLibSystemHelpers == NULL`, `_malloc` cannot return NULL, so the only way to reach the OOM literal is
  `__simple_salloc`'s `vm_allocate` failing at `0x2be01658` **[推]**.

Evidence for `gLibSystemHelpers == NULL` at the moment of the fatal `throwf` **[推, chain]**:

1. `r1 = 1` at the crash ⇒ `gLinkContext+151 == 0` (§2) ⇒ `initializeMainExecutable` never entered.
2. `gLibSystemHelpers` (`0x2be21a04`) is **0 in the file image** and has **no writer anywhere in the dyld
   image**: the `movw/movt/add pc` census finds only loads (`dyld::vlog`, `initializeMainExecutable`,
   `fastBindLazySymbol`, `garbageCollectImages`, `_main`, `dlerror*`, `dlopen`, `cxa_*`, `SjLj_*`,
   `_malloc`/`_free`/`_calloc`, `_vm_alloc`, `_xmmap`) **[测]**, and a literal-pool scan for `0x2be21a04` /
   `0x2be21028` returns zero hits **[测]**. So the writer lives outside dyld (libSystem's initializer, in the
   shared cache).
3. `initializeMainExecutable` tests it **after** `runInitializers` (`0x2be01d38`: `ldr r1,[&helpers]; cbz`
   → skip `cxa_atexit`) — i.e. it is the initializer run that is supposed to provide it **[测]**.
4. The 1–3 chain ⇒ at the fatal `throwf` the helpers pointer was still NULL ⇒ the pool allocator was in use ⇒
   the failing call was `__simple_salloc`'s 4 KiB `vm_allocate` **[推]**.

---

## 7. Q4 — the window, and what the first failure therefore is

### 7.1 Bounding the window from below and above

* **Lower bound [测, via 991]:** 991's abort was inside `ImageLoader::link`'s **first** `mach_absolute_time`
  (`blx` @ `0x2be09a9a`, return address `0x2be09a9f` = the measured `lr 0x2be58a9f` slid). Everything before
  that — including `mapSharedCache`, `isCompatibleMachO`, `instantiateMainExecutable` — had already
  succeeded. 993 differs from 991 only by the alias + vecl2 fix, and took the alias instead of faulting.
* **Upper bound [测]:** the byte-151 witness ⇒ `initializeMainExecutable` (`0x2be04a88`) not entered (§2).
* **Both ends are inside `ImageLoader::link`'s call tree** when combined with the fact that `link` returns
  only after its callees: the window is
  **`ImageLoader::link` body (`0x2be09a68`) … `_main`'s post-link block up to `0x2be04a88`**.
* Inside that window the substantive dyld work, in order **[测]** (`/tmp/fx18.log`):
  `setErrorStrings` → **`recursiveLoadLibraries`** (`0x2be09aaa`) → notify(20) → `SjLj_Register` →
  `recursiveUpdateDepth` → **`recursiveRebase`** (`0x2be09b1a`) → notify(30) → **`recursiveBind`** (`0x2be09b3a`)
  → **`weakBind`** (`0x2be09b52`) → notify(40) → **`recursiveGetDOFSections`** (`0x2be09b8a`) →
  **`recursiveApplyInterposing`** (`0x2be09bc8`) → return to `_main` → `setNeverUnloadRecursive` →
  the inserted-library `link` loop (`0x2be04a08`) → `applyInterposing` → `weakBind` → `initializeMainExecutable`.
* **2026-10-12 (dyldtrace) correction to the list above:** the leading `setErrorStrings` is the state-CLEAR
  call inside `link` itself — the `+0x44` hook (`LinkContext+0x44` = `0x2be05d51`, materialized @`0x2be04d78`)
  invoked with `r0..r3 = 0` @`0x2be09a96` (and again @`0x2be09bdc`). The ERROR-populating dispatch is
  `ImageLoader::recursiveLoadLibraries+0x60c` (`0x2be0a300`, kind 1 = `dyld_error_kind_dylib_missing`), a
  callee of the `0x2be09aaa` `bl`. The direct-branch census cannot see either (indirect `blx`);
  see `d13-dyld-halt-message-recovery.md` §9.6.A (revised same day).

### 7.2 Is the shared cache mapped before the first plausible `throwf`?

**Yes — the mapping attempt precedes it, and its failure cannot be a `throwf` [测].**
`mapSharedCache` is called at `0x2be048a8`, i.e. ~0x98 bytes before `dyld::link` at `0x2be04940`, and
the function contains **zero** `throwf` call sites among the image's 145. Its every failure mode is a
`dyld: shared cached file …` log string (`cannot be opened` `0x2be1d0f1`, `is invalid` `0x2be1d0a3`,
`cannot be read` `0x2be1d0c8`, `is corrupt: %s` `0x2be1cf90`, `missing read-only mapping` `0x2be1d023`,
`missing read/write mapping` `0x2be1cfd0`, `could not be mapped` `0x2be1d075`, `existing shared cached in
memory is not compatible` `0x2be1cf56`) — all **[测]**.

Two consequences:

1. "Can't map the cache" is **not** the OOM's cause; it would have appeared as a log line and a fallback to
   file-based loading, after which the failure would surface in `recursiveLoadLibraries` instead.
2. The failing allocation is the **formatter's own 4 KiB `vm_allocate` inside `throwf`**, which destroys the
   real message. So the OOM is *always* a second-order symptom: the first failure happened **just before**
   the fatal `throwf`, in whatever dyld was about to complain about.

### 7.3 The ranked answer to "what was dyld failing at?"

1. **`ImageLoader::recursiveLoadLibraries+0x66a` @ `0x2be0a35e` — `Library not loaded: %s` / `Referenced from: %s` / `Reason: %s`.**
   This is the first thing `link` does, it is the only site that reports "the dependents of the main
   executable could not be brought in", and the D13 environment is exactly one where launchd's two
   `LC_LOAD_DYLIB`s exist **only** in the shared cache. **[推 — the strongest candidate, but not measured]**
2. Same function `+0x4c2` (version/architecture mismatch), if the cache is mapped but its image metadata is
   not what the loader expects.
3. `ImageLoaderMachO::sniffLoadCommands` / `parseLoadCmds` / `mapSegments` (`0x2be0b80a`…, `0x2be0bf64`…,
   `0x2be0dbf8`/`0x2be0dc26`) — "malformed mach-o" / "mmap() error" while instantiating a dependent.
4. `ImageLoaderMachO::throwSymbolNotFound` (`0x2be0cf68`) during `recursiveBind`, if a dependency came up
   de-featured rather than missing.

**Practical consequence for the next arm:** fixing trap −10 does not only make an allocation succeed —
it makes dyld *say which of the above it was*. Until then, `throwf, out of memory error` is the expected
halt message for **any** dyld failure on this kernel, and the 993 crash block
(`r6=2`, `r1=1`, `r5=0x…c7fd`) will look identical no matter which real error preceded it.

---

## 8. NON-CLAIMS (everything here is *not* established by this analysis)

1. **The identity of the first failure is an inference, not a measurement.** No stack, no return address and
   no log line survives from the fatal `throwf`. The window is measured; the choice between the ranked
   candidates in §7.3 is by environment reasoning only. A run that fixes trap −10 and still dies will
   produce a *different* halt message — **that message, not this document, is the ground truth.**
2. **The exact `kern_return` from the failing trap is unknown**, as is the garbage value of the uninitialised
   hint slot `[sp]` in `__simple_salloc`. The reasoning "flags=0 ⇒ fixed-address ⇒ failure" is sound
   (fixed-address allocation at an arbitrary hint is expected to fail), but a *successful* fixed-address
   allocation at that hint is not excluded by anything measured — only by the observed OOM.
3. **The helpers-NULL chain (§6) is an inference.** Its weak link is step 2: absence of a writer *in the
   dyld image* is measured, but the claim "the writer runs during libSystem's initializer" rests on dyld's
   structure (`initializeMainExecutable`'s post-`runInitializers` test), not on tracing the writer.
   If that link is wrong, the OOM could instead be `strdup`'s malloc (libSystem's own malloc) failing.
   **Note this does not change §7.3** — the real message is destroyed either way.
4. **The silence of the console is not evidence.** §5 shows `vlog` has an `fd 2` fallback, but whether pid 1's
   fd 2 was writable in 993 was not measured. Do not read "no dyld output" as "no dyld code ran".
5. **`_main`'s indirect call at `0x2be04976`** (`[[global]]+136` returning 1 → sets `gLinkContext+149`/`+136`)
   is measured but **not identified**; if it is a platform/version gate that can throw, it is a candidate this
   document does not rank. I did not find a `throwf` in `_main`'s own body, but `_main` is inlined code with
   cold pads, so "no throwf site inside `_main`" is asserted only for the function's own range.
6. **`recursiveLoadLibraries+0x4c2`'s exact format string** is quoted from the pre-existing table
   (`/tmp/throwf_table.txt`); this analysis measured its surrounding literals (`Incompatible`,
   `architecture`) but did not re-derive the full format string.
7. **No claim is made about the shared cache's actual mapping state in 993.** §7.2 establishes only that
   `mapSharedCache` *cannot itself* produce a `throwf`.
8. **`_vm_allocate`'s signature** (`(task, address, size, anywhere)`) is inferred from its register shuffling
   (`str r3,[sp]` / `movs r3,#0` / `bl _mach_vm_allocate`); it is consistent with the observed five-word wire
   but was not cross-checked against an iOS 7 libsyscall source.

---

## 9. How these numbers were produced (re-runnable)

```bash
# 1. disassemble (already done): llvm-objdump --macho -d /tmp/dyld_ios7 > /tmp/dyld_objd.txt
# 2. pc-relative string/global resolution.  Object-file format is:
#    "<8 hex>: \t <bytes> \t <mnemonic> \t <operands>", and symbol labels are bare "name:" lines
#    that bind to the NEXT instruction address -- there is no address on the label line.
#    target = (movw|movt<<16) + (pc_of_add + 4)      # pc pipelines as addr_of_add+4
#    (the two variants  a+4  and  align4(a)+4  agree for every 4-byte-aligned 'add')
python3 -I   # scripts are in /tmp (fx13/fx17/fx18/fx20..fx26.log hold the dumps quoted above)
```

Quoted dumps: `/tmp/fx17.log` (initializeMainExecutable), `/tmp/fx18.log` (ImageLoader::link),
`/tmp/fx20.log` (halt, setAlImageInfosHalt, `_main` call census), `/tmp/fx21.log` (symbol nav, vlog),
`/tmp/fx22.log` (all 145 throwf sites + string census), `/tmp/fx23.log` (halt sites, `__simple_salloc`,
`_malloc`, `_vm_alloc`, `strdup`, `throwf`), `/tmp/fx24.log` (the trap wire), `/tmp/fx25.log` (string refs),
`/tmp/fx26.log` (catch pads).

Cross-references: `docs/reference/d13-dyld-halt-callers.md` (the 13 halt sites, first derivation),
`docs/reference/d13-crash-state-forensics.md` (993's register block),
`docs/reference/d13-post-alias-next-walls.md` (991's measured `lr`, the W1 silent-return mechanism),
`docs/reference/d13-dyld-helper-table.md` (the LEO witness / aii+0x20 recovery route).

---

## 10. 994 update — the wire is fixed, the message is real, the ranking re-converges

Evidence source: `out/stage90/captures/cancro-last_kmsg-994-armed-d13-8b4c9a3e.txt` (24,682 lines;
line numbers below are 0-based indices over the copy indexed in `/tmp/w994_regs.txt`; the witness
doc `d13-dyld-witness-995.md` cites `:4101`/`:4103` for the same lines — the two index bases differ
by 52, cite the *content*, not the number).

### 10.1 The delta 993 → 994

| fact (all from the 994 capture) | 993 | 994 |
|---|---|---|
| dyld slide | `0x7D000` | `0xB8000` |
| `pc` at the `udf` | `0x2be7e0c4` | `0x2beb90c4` → unslid **`0x2be010c4` = `_dyld_fatal_error`** 【测】 |
| `lr` | `halt+0x4b` | `0x2beb9f07` → **`0x2be01f07` = `halt+0x4b`** (the `blx` return slot) 【测】 |
| `r1` (`!byte[gLinkContext+151]`) | `1` | `1` ⇒ `initializeMainExecutable` still never entered 【测】 |
| `r5` (halt's message argument) | `0x2be1c7fd` — the **static** OOM literal | `0x2bedc390` → unslid `0x2be24390` = `__ZL18initialPoolContent` (`0x2be231b0`) **+ 0x11E0 ⇒ a heap string** 【测】 |
| the formatter's `vm_allocate` | fails (that is why 993 saw the OOM literal) | **succeeds**: capture `IOS7LAB vm_allocate64 size=00000000:00001000 flags=1 result=0 address=000e1000` then `vm_deallocate64 address=…:000e1000 size=…:00001000 result=0` = `__simple_salloc` + `__simple_sfree` of the fatal `throwf` 【测】 |

The `0x000e1000` buffer is *not* the pool (pool = `0x2be231b0..0x2be489b0`); it is the salloc'd 4 KiB
scratch. The **reported** message is the pool string at `r5`. The run reached
`attempting to start init of /sbin/launchd` and the entry `0x2beb9028` = `__dyld_start` 【测】 —
same pid-1 launch as 993. `_dyld_fatal_error`'s word is `fe de ff e7` = `0xE7FFDEFE` — an **ARM-mode**
`udf`; `lr = 0x2be01f07` (Thumb bit set) + `cpsr 0xb0000010` (T = 0) are therefore *consistent*
(a Thumb `blx` into an ARM leaf), not an artifact — this retires §8's cpsr non-claim 【推, on the two
measured encodings】.

### 10.2 The 994 register block, decoded (capture lines 4161–4169, duplicated 235×)

| reg | raw | unslid | reading |
|---|---|---|---|
| `r0` = `r4` | `0x2bf01564` | `0x2be49564` | `_error_string` — `halt`'s own `strlcpy` destination (`mov r0,r4` at `0x2be01f00`) 【测】 |
| `r1` | `1` | | `!byte[gLinkContext+151]` — `setAlImageInfosHalt`'s second argument (§2) 【测】 |
| `r2` | `0x2bed9b1c` | `0x2be21b1c` | `&gProcessInfo` — `setAlImageInfosHalt`'s `r2 = 0x2be08d10 + 0x18e0c` 【测】 |
| `r3` | `0x2bed9c8c` | `0x2be21c8c` | `[r2]` = the `__all_image_info` struct; the kernel prints the **same** address (`dyld_all_image_info_addr:`) two lines below 【测】 |
| `r5` | `0x2bedc390` | `0x2be24390` | **the message** (halt's `mov r5,r0`, preserved over the `dyld::log` call) = pool + 0x11E0 【测】 |
| `r6` | `2` | | the `_main` catch pad's clause selector — loaded `ldr r6,[sp,#16]` at `0x2be04ba4`, compared to `movs r4,#2` ⇒ the `catch (const char*)` fatal branch 【测, §5】 |
| `r8` | `0x2bedc428` | `0x2be24428` | the **exception object**, pool + 0x1278 (§10.3) 【测】 · **superseded 2026-10-12: `r8 = obj − 0x20` (see §10.4a)** |
| `r9`/`r10`/`r11`/`r12`/`sp`/`r7` | stack values | | frames (`0x2fd47xxx`) 【测】 |

### 10.3 `r8` is the exception object — the chain, by value

* `__cxa_throw` @ `0x2be148c0`: `mov r6,r4` (`0x2be148f4`) then `mov r0,r6; bl __Unwind_SjLj_RaiseException`
  (`0x2be14920`/`0x2be14922`) ⇒ the unwinder receives **the object pointer** `__cxa_allocate_exception`
  returned.
* `__Unwind_SjLj_RaiseException` @ `0x2be1c518`: `str r8,[sp,#-4]!` (`0x2be1c51c`) then **`mov r8,r0`**
  (`0x2be1c526`) ⇒ `r8` = the object for the whole unwind.
* The handler-install path (`0x2be1c5da`–`0x2be1c5e8`) restores only `sp` (`[r0+8]`), `r7` (`[r0]`) and
  `pc` (`bx r1` = `[r0+4]`) ⇒ **`r8` survives to the landing pad**. (The *return* path just above does
  restore `r8`; it was not taken — the pad ran, `r6 = 2` at the udf.) 【测】
* No writer of `r8` in between: `___gxx_personality_sj0` (`0x2be14ec4`) has **no `r8` write**;
  `_scan_eh_tab` restores it (`pop.w {r8,r10,r11}` @ `0x2be152e6`), `__cxa_allocate_exception`
  (`0x2be14604`), `__cxa_begin_catch`, `dyld::syncAllImages` all save/restore it 【测, `r8`-write census】.
* Cross-run check: 993's `r8 = 0x2bea13c0` (slide `0x7D000`) → `0x2be2a3c0` = pool + 0x7210 =
  (**pool + 0x71C0**) + 0x50 — the *same* `+0x50` relation in a run whose message was static 【测】.
* `__cxa_allocate_exception` @ `0x2be14584` calls `do_malloc(size+80)` (`add.w r0,r4,#80` @ `0x2be145d0`)
  and returns **`block + 80`** (`0x2be145f0`) 【测】 ⇒ `r8 = block_cxa + 0x50`.
* **Superseded in part — see §10.4a.** The unwinder receives `obj − 0x20`, not `obj` (the `mov r0,r6`
  @ `0x2be14920` passes the address `___cxa_throw` wrote just below the object, `0x2be14902`), and 995's
  measurement closes only under `r8 = obj − 0x20 = block_cxa + 0x30`.

### 10.4 ⇒ the message length is 68..71 bytes (this is the new discriminator)

* `_strdup` @ `0x2be18144`: `strlen+1` → `bl _malloc` (`0x2be18152`), returns the **block start**
  ⇒ `r5` = the message block start 【测】.
* `_malloc`'s pool path (`0x2be0dfe0`–`0x2be0e046`): `adds r1,#3; bic r4,r1,#3` — **4-byte alignment
  only** — returns the old bump, `free += align4(size)` 【测】.
* `throwf` path B has **no allocation** between `bl _strdup` (`0x2be0166e`) and
  `bl ___cxa_allocate_exception` (`0x2be0169e`): `mov r5,r0` → `bl __simple_sfree` (that is the
  `vm_deallocate` of the capture's second IOS7LAB line — a *vm* free, not a pool allocation) →
  `cmp r5,#0; bne` → `movs r0,#4` → the `bl` 【测, raw bytes】.
* Therefore `block_cxa − r5 = align4(L+1)` with `L = strlen(message)`, and the measured block
  difference is `0x2be243D8 − 0x2be24390 = 0x48` ⇒ **`L ∈ [68, 71]`** 【测 + 推 (`r8` = object is §10.3's measurement)】.
* Same arithmetic for the other heap producers: any `strdup`- or `__Znam`-built message block has
  size `align4(L+1)` and is immediately followed by the 0x54-byte exception block 【推 for the
  direct throws; the `_free`-no-op assumption is §10.9.2】.
* Invariant for the 995 witness (it prints `len=`): **(r8 − 0x50) − r5 == align4(len + 1)**.

### 10.4a Correction, measured by 995 + the dyldtrace lane (supersedes §10.2's `r8` row, §10.3's last two bullets and §10.4's window)

* **`r8 = obj − 0x20`, not `obj`.** `___cxa_throw` runs `mov r0,r6` @ `0x2be14920` with `r6` = the address
  `0x2be14902`'s writeback left (the two words the throw plants just below the object: `0x432B2B00` at
  `obj−0x20`, `0x434C4E47` at `obj−0x1C`) 【测】, and `__Unwind_SjLj_RaiseException`'s `mov r8,r0`
  (`0x2be1c526`) carries **that** value through the unwind. Hence `r8 = (block_cxa + 0x50) − 0x20 =
  block_cxa + 0x30`. The 995 dump agrees from the other side: the witness' interior read at `r8` is `len=0`,
  i.e. **the byte at `r8` is `0x00` — the low byte of `0x432B2B00`** 【测】.
* Hence the **corrected invariant** for `strdup`-built messages: **(r8 − r5) − 0x30 == align4(L + 1)**
  (not `(r8 − 0x50) − r5`). 995: `0x2be24428 − 0x2be24390 = 0x98` ⇒ `align4(L+1) = 0x68` ⇒ **L ∈ [100, 103]** 【测】.
  (993's static-message run re-reads as `obj = pool + 0x7230`, `block = pool + 0x71E0`; the old line treated
  `r8` itself as `obj`.)
* **L = 102 exactly, and the message is the libbsm one.** `copyinstr`'s `len=103` counts the terminating NUL
  (the sibling doc's §9.4): 20 + 23 + 1 + 19 + 13 + 1 + 10 + **15** = 102 for
  `Library not loaded: /usr/lib/libbsm.0.dylib\n  Referenced from: /sbin/launchd\n  Reason: image not found`.
  The `Reason` is the **static 15-byte** literal `0x2be1cc9d`, thrown at `dyld::load` `0x2be02d24`
  (the `byte[LoadContext+5] == 0` side of `0x2be02cc0`; the heap `Did find:` form @ `0x2be02cb6` cannot fit
  the length). `/tmp/c_launchd.txt`: `/usr/lib/libbsm.0.dylib` is launchd's **first** `LC_LOAD_DYLIB` 【测】.
* **§10.5's row 1 verdict is reversed: `recursiveLoadLibraries+0x66a` (`0x2be0a35e`) is the producer**, and
  its gate byte `*[r7,#-164]` (`ldrb;cmp;beq 0x2be0a362` @ `0x2be0a29e`) was **non-zero** in 995 【测】.
* Task D: `0x2be14950 = ___cxa_begin_catch` (label by value), whose return `r4` is the caught inner `char*`
  used as the third `%s` (`bl ___cxa_begin_catch; mov r4,r0` @ `0x2be0a2a2`) — §10.6's argument table holds 【测】.

### 10.5 Which `throw` sites can produce it — the 44-site census and the re-windowed ranking

**(a) Is the message heap or static?** Of the image's 44 direct `___cxa_throw` sites, 8 store a value
whose producer is a register (heap-capable) and 36 store a PC-relative literal (`add rX,pc`) 【测,
`/tmp/c_throw_class3.txt`】. The 8: `throwf`'s own `0x2be016b2` (`str r5,[r0]` after the strdup),
`notifySingle` `0x2be01bd6` (after `bl _strdup`), `dyld::load` `0x2be02cb6` (after `bl __Znam`),
`notifyBatchPartial` `0x2be03d90`, `Compressed::rebase` `0x2be114b8`, `eachBind` `0x2be12974`,
`eachLazyBind` `0x2be1305a`, `__cxa_call_unexpected` `0x2be155f0`. **`r5` = a pool address is measured
(§10.1) ⇒ CONFIRMED: the message is heap; the 36 static-literal sites are excluded** (a literal would
put a `__TEXT/__cstring` address in `r5`). Because *every* one of the 145 `bl dyld::throwf` sites
funnels through `throwf`'s own heap-capable site, all 145 formats remain in play; the L window is
what selects among them.

**(b) The window re-ranks all 145 formats** (base = printed chars with `%s`→empty, `%08X`/`%08lX`→8,
`%lX`→minimum; literal lengths re-measured from the dyld bytes, `/tmp/w994_str2.txt`) 【测】:

| site | message | required args for L∈[68,71] | verdict |
|---|---|---|---|
| `recursiveLoadLibraries+0x66a` `0x2be0a35e` | `Library not loaded: %s\n  Referenced from: %s\n  Reason: %s` (`0x2be1e0df`, len 57) | *old window*: three args summing 17..20 | **FALSIFIED by 995 — this is the producer (§10.4a)**: the window was wrong, not the site; 23 + 13 + 15 = 51 and L = 102 【测】 |
| `throwSymbolNotFound+0x2c` | `Symbol not found: %s\n  Referenced from: %s\n  Expected in: %s` | three args 14..17 | EXCLUDED (paths alone ≥ 26) |
| `mapSegments+0x1fe` | `mmap() error %d at address=0x%08lX, size=0x%08lX segment=%s in Segment::map() mapping %s` | base 73 > 71 | **EXCLUDED by arithmetic alone** |
| `sniffLoadCommands+0x150` / `parseLoadCmds+0x362` / `mapSegments+0x1d0` | `load command #%d length (%u)…` (81) / `…built for OS version %u.%u…` (83) / `truncated mach-o error…` (72) | base > 71 | EXCLUDED by arithmetic |
| `resolve+0xfa`, `resolveUndefined+0x27e`, `eachBind+0x18a`, `incrementCoalIterator+0x3f0`, `updateUsesCoalIterator+0x3fe`, … | `BIND_OPCODE_SET_SEGMENT…` (76+), `bad mach-o binary…` (70 + 3 args), `bad weak bind opcode…` (61 + args) | args must sum ≤ 1, or base > 71 | EXCLUDED |
| `sniffLoadCommands+0x136` | `malformed mach-o image: wrong LC_SEGMENT[_64] for architecture` | n/a | EXCLUDED — fixed 62 ⇒ L = 62 ≠ window |
| `loadPhase3` `@loader_path`/`@executable_path`/relative-rpath sites | base 53/57/56 | paths 15..18 / 11..14 / 12..15 | EXCLUDED in practice — all three require a **restricted** (`__RESTRICT`) binary; launchd is not one 【推】 |
| rebase-family path messages (`Classic+0x2b4`, `Compressed+0x7e2`) | `%s in %s. reloc record…` / `dyld: rebase: %s:*0x%08lX += 0x%08lX` | paths 28..31 / 43..46 | EXCLUDED in practice — the only image rebased by opcodes here is the main executable, path `/sbin/launchd` = 13 ⇒ every message ≪ 68; cache images are prebound 【推】 |
| `doBindFastLazySymbol`, `doBindLazySymbol`, `bindLazySymbol` (runtime lazy binding) | — | — | EXCLUDED by reachability — `r1 = 1` ⇒ no libSystem initializers ⇒ `dyld_stub_binder` cannot have run 【测+推】 |
| `doImageInit+0xa0`, `doModInitFunctions+0xee`, `doTermination+0xd6` | `…function %p not in mapped image for %s` | — | EXCLUDED by reachability — these run in `runInitializers`, after `initializeMainExecutable` 【测, §2/§5】 |
| Classic-only sites (`Classic::*`, `doBindExternalRelocations`, `doBindLazySymbol`, `mapSegmentsClassic`, …) | — | — | EXCLUDED in practice — cache images are **Compressed** (`instantiateFromCache` @ `0x2be108e2`) 【推】 |
| `loadCodeSignature+0x80` | `code signature invalid for '%s'` | path 39..42 | weak — cache images carry no `LC_CODE_SIGNATURE` |
| `ImageLoader` ctor `+0xa6` | `too many dependent dylibs in %s` | path 39..42 | structurally absurd (2 deps) — kept below the line |

**What is left — the re-converged ranked list** (all four are reachable inside `link`'s call tree,
i.e. after 991's measured entry into `ImageLoader::link`, and before the `r1 = 1` bound):

| # | site | message | required argument | why it ranks here |
|---|---|---|---|---|
| **1** | `ImageLoaderMachO::sniffLoadCommands+0x1f4` | `malformed mach-o image: segment %s does not span all load commands` (`0x2be1e447`, len 66) | **segment name of 4..7 chars** — `__TEXT`/`__DATA` (6) gives exactly L = 70 | the only in-window message whose argument is *natural* at that length, and it fires exactly when a **mapped** image's header/segment geometry is inconsistent — i.e. a shared-cache mapping/offset defect. No auxiliary assumption (no unmeasured flag, no file fallback). The ctor path runs it for cache images too 【测 literal + 推 identity】 — **superseded: §10.4a (995) settled the message: `Library not loaded: … Reason: image not found`, L = 102** |
| 2 | `dyld::load` `0x2be02cb6` (direct heap throw; buffer built by `bl __Znam` @ `0x2be02c32`, loop appends `"\n\t"+path` @ `0x2be02c70`/`0x2be02c80`) | `no suitable image found.  Did find:` (`0x2be1ccad`, 35) + `\n\t<path>` per tried path | **one path of 31..34 chars** (or two summing 29..32) — the cache has such paths (`/usr/lib/system/libsystem_m.dylib` 33, `libsystem_asl` 33, `libsystem_info` 34, `libMobileGestalt.dylib` 31 …) | the classic "cache lookup missed, file fallback found nothing" message. Two caveats: its branch is gated by a byte at `[LoadContext+5]` (`ldrb r0,[r0,#5]; cmp r0,#0` @ `0x2be02cc0`, the 0-byte side throws the **15-char** `image not found` @ `0x2be1cc9d` — itself excluded by length), and inside `recursiveLoadLibraries` it would be **rewrapped** unless the catch's gate byte is 0 (§10.6) |
| 3 | `dyld::isCompatibleMachO+0xa2` | `incompatible cpu-subtype: 0x%08X in %s` (`0x2be1cc76`) | **path 28..31** | reachable **only** via `loadPhase6` (`bl` @ `0x2be02fe4`; the other caller `0x2be04812` is `_main`, already passed) ⇒ it implies a **file** was instantiated — i.e. both a cache miss *and* a loose file that parsed far enough to have a header |
| 4 | `sniffLoadCommands+0x25a` | `malformed mach-o image: more than 255 segments in %s` (`0x2be1e5ee`) | path 18..21 | same family as #1 (geometry misread), needs a short-path image |

### 10.6 `recursiveLoadLibraries+0x66a` — the three arguments and every precondition (brief Q2)

Function base `0x2be09cf4` (prologue; the correspondence `0x2be0a35e − 0x66a` closes it) 【测】.
The catch pad `0x2be0a28a`–`0x2be0a35e` 【测, full disassembly re-read】:

```
2be0a28a  ldr r4,[r7,#-140] ; ldr r0,[r7,#-136] ; cmp r0,#1 ; bne.w 0x2be0a3a8   ; SjLj selector gate
2be0a2a2  bl ___cxa_begin_catch ; mov r4,r0                                     ; r4 = the thrown char*
2be0a29e  ldr r5,[r7,#-164]   ; ldrb r0,[r5] ; cmp r0,#0 ; beq 0x2be0a362       ; THE GATE (§below)
2be0a2be  strb.w r2,[r0,#44]  (r2=10, r0=[r7,-184])                             ; state byte := 10
2be0a2c4  blx _strstr(message, "Incompatible" 0x2be1e0c5)  -> 0x2be0a304: code 3, state 7
2be0a2dc  blx _strstr(message, "architecture" 0x2be1e0d2)  -> 0x2be0a328: code 2, state 14
                                                              else        : code 1, state 15
   (all three call r6 = [[r7,-180]+68] with (code, [this+4], *(needed), 0), then...)
2be0a334  r1 = *(char**)[r7,#-156]        ; the NEEDED dylib's path (the vector element)
2be0a33c  r2 = [this+8] (fInstallPath) ; if 0 -> r2 = [this+4] (fPath)   ; the REFERRING image
2be0a340  r3 = [r7,#-220]                 ; the caught inner message (saved r4 on both paths)
2be0a350  r0 = 0x2be1E0DF = "Library not loaded: %s\n  Referenced from: %s\n  Reason: %s"
2be0a358  str 16 -> [r7,#-144]            ; state := 16
2be0a35e  bl dyld::throwf
```

So the three `Reason` arguments, in format order, are **`%s`₁ = the needed library's path,
`%s`₂ = `this->fInstallPath ?: this->fPath`, `%s`₃ = the inner failure message** — the canonical
"Library not loaded / Referenced from / Reason" structure 【测】.

Every precondition that must hold to reach this `throwf`:
1. control unwinds into this pad — the SjLj selector `[r7,#-136] == 1` (`bne.w 0x2be0a3a8` = resume) — and the thrown object is a `const char*` (`r4` is used directly by `strstr`) 【测】;
2. the byte **`*[r7,#-164] != 0`** — if it is 0 the pad jumps straight to `__cxa_end_catch` (`0x2be0a362`) and **no rewrap happens** 【测】;
3. if (2) holds the `throwf` is **unconditional** — the `strstr` results only choose the notify code (3/2/1) and the transient state (7/14/15), they never gate the throw 【测】.
The gate byte's semantic identity (a `LinkContext`/verbose flag) and its 994 value are **not**
measured — but it is the *only* switch between "inner message propagates" and "message is rewrapped".
Combined with §10.5(b): **if the rewrap fired in 994, the halt message would be ≥ ~89 bytes
(51 + ≥24 + ≥13 + ≥1) and could not be the measured 68..71 — so either the rewrap was skipped, or
the throw came from a site outside this try entirely (rebase/bind/notify families)**. That is the
structural reason the §0/§7.3 answer had to move.

### 10.7 launchd ↔ shared-cache cross-check (brief Q3) — the static closure is complete

From `/tmp/c_closure.txt` (cache = `xnu_card_hfs.img+0x455d000`, magic `dyld_v1   armv7`) 【测】:

| check | result |
|---|---|
| images parsed from the file | **696, 0 bad magic** |
| launchd's `LC_LOAD_DYLIB`s | `/usr/lib/libbsm.0.dylib` and `/usr/lib/libSystem.B.dylib` — **both present** |
| `libbsm.0.dylib` | filetype 6, `cur=1.0.0 compat=1.0.0`, 1 dep (`libSystem.B`, `cmd=0xc` = `LC_LOAD_DYLIB`) |
| `libSystem.B.dylib` | filetype 6, `id=cur=1198.0.0 compat=1.0.0`, **28 deps, every one `cmd=0x8000001f`** (`LC_REEXPORT_DYLIB|LC_REQ_DYLD` — a *known* command in dyld-324.1, not an unknown one) |
| transitive closure from launchd | **259 images, 0 missing, 0 version mismatches** |
| `/sbin/launchd`, `/usr/lib/dyld` | **not in the cache** (rootfs files — expected) |

⇒ **No static absence and no version mismatch exists.** Any 994 "not found / not loaded / unknown
load command" for an image in the closure must be a **runtime failure of the cache lookup or of the
mapped bytes**, not a missing file. Path-length sample for the window rows above: the cache's
first 0x80000 holds 146 path-like strings, including `libMobileGestalt.dylib` 31,
`libAccessibility.dylib` 31, `libcharset.1.0.0.dylib` 31, `libAXSpeechManager.dylib` 33,
`libiconv.2.4.0.dylib` 29, `libstdc++.6.0.9.dylib` 30 — so #2/#3's 28..34-char requirement is
satisfiable by real cache paths 【测】.

### 10.8 Pre-registered expectation table for 995's witness (brief Q4)

The witness (`d13-dyld-witness-995.md`) prints the message under **both** live pointers (`A+0x20`,
`state->r[5]`), a verdict line, and an `r8` classification. Read the table top-down; the printed
**text** decides the branch, the printed **len** enforces §10.4's invariant
`(r8 − 0x50) − r5 == align4(len+1)`.

| witness prints | failure judged | fix form |
|---|---|---|
| `dyld> malformed mach-o image: segment __TEXT does not span all load commands` (len 70) | **R1** — a mapped cache image's header/segment geometry is inconsistent: the cache is *mapped*, but one image's bytes are being read at the wrong offsets/addresses | dump that image's `mach_header` (`ncmds/sizeofcmds`, each `segment_command.fileoff/filesize/vmaddr`) from the mapped cache via the witness and compare with the file-backed cache at the same offsets; the defect is in the cache-base/slide or image-table offset arithmetic on the kernel path, not in the image |
| `dyld> no suitable image found.  Did find:` + one `\t/usr/lib/system/libsystem_*.dylib` (len 68..71) | **R2** — the cache lookup itself missed an image that IS in the cache (§10.7) and the file fallback correctly found nothing on the rootfs | instrument the cache lookup (install-name → image-table walk) — the kernel's shared-region mapping returned OK but the loader's lookup over it fails; compare `localSymbolsOffset`/image-table reads through the witness |
| `dyld> incompatible cpu-subtype: 0x…… in /usr/lib/…dylib` (len 68..71) | **R3** — `loadPhase6` instantiated a **file** (only caller path left) and its header was rejected | first establish *which file existed* (an unexpected file found ⇒ a search-path/`DYLD_ROOT_PATH` state issue); then compare the file's header with the cache entry |
| `dyld> malformed mach-o image: more than 255 segments in <path 18..21>` | R4 — same family as R1, independent geometry check | as R1 |
| `dyld> Library not loaded: …` (len ≥ 89) **and** `exception object (signature at r8-0x20)` | **the §10.4 length model is falsified** (that message cannot be 68..71) — do NOT trust the window; the r8 line tells which assumption broke | re-derive: if `r8` says `exception block base` the predicted len is 148..151; if the verdict says `DIFFERS`, one of `A+0x20`/`r5` is not halt's message |
| `dyld> throwf, out of memory error` | the trap −10 wire fix is **not** in this build (regression) | check the build's IOS7LAB pair: the salloc line must read `size=…:00001000 flags=1 result=0` (994 §10.1) |
| any real message with `len` ∉ 68..71 while `r8` classifies as the exception object | the block-adjacency part of §10.4 (a pool allocation between the message and the exception block that the measured `throwf` path does not contain — e.g. via `dyld::load`'s `_free` loop, §10.9.2) | re-derive L from the printed len; the text still names the site |
| `IOS7WITNESS r5 DIFFERS from errorMessage` | the field and the register do not describe the same halt | report as such; do not use either string as the message |
| no `IOS7WITNESS` line at all | the witness never ran (switch did not reach `osfmk_arm_trap.o`, or wrong pc path) | run `tools/check_d13_dyld_witness.py --expect on` on the built image |

### 10.9 NON-CLAIMS (994 addendum)

1. **The L window's assumptions.** `r8` = the exception object is measured (§10.3); the *block
   adjacency* is measured for the `throwf` path (§10.4) and **assumed** for direct heap throws
   (§10.5(a)). If a producer allocates between the message and the exception block, the window
   shifts — which is exactly what §10.8's len-check row is for.
2. **`_free` in the pool path is assumed to be a no-op** (dyld's pool allocator does not reclaim);
   re-measured only at the call sites (`dyld::load` frees the vector's strings between `__Znam` and
   `___cxa_allocate_exception`, `0x2be02c8a`) — if it reclaimed memory, row R2's measured gap could
   include a recycled hole.
3. **The rewrap gate byte's identity and 994 value are unmeasured** (§10.6 precond. 2). Both
   branches of §10.5's structural argument are stated; the witness text decides.
4. **The ranked order is by length-feasibility + reachability, not by measurement** of the failing
   image. R1's message never names the image; only 995's text can pin it.
5. **The rebase-family and Classic-family exclusions rest on structural premises** ("cache images
   are prebound / Compressed") that were not re-measured in this pass.
6. **The 993/994 pool watermark difference is unexplained**: in 993 the exception block was at
   pool + 0x71C0, in 994 the message block at pool + 0x11E0 — ~0x6000 more pool consumed before
   the fatal `throwf` in 993. Recorded, not explained (and 993's own arithmetic — static literal,
   no strdup — is otherwise consistent).
7. **The console is still silent** in 994 (no `dyld:` line even though `halt` logs before
   `strlcpy`) — the fd-2 item (§8.4) remains open; the message exists only in memory, which is
   precisely why 995's witness reads it there.

### 10.10 Repro

All commands are `python3 -I` scripts over `/tmp/dyld_ios7` and `/tmp/dyld_objd.txt`; new artifacts
this pass: `/tmp/w994_regs.txt` (the raw 994 block + IOS7LAB lines), `/tmp/w994_alloc.txt`
(`_malloc`/`__cxa_allocate_exception`/`__Znam`/`_strdup` bodies), `/tmp/w994_throw.txt`
(`__cxa_throw` → `RaiseException`), `/tmp/w994_r8.txt` (`r8`-write census), `/tmp/w994_lenwin.txt`
(per-format windows), `/tmp/w994_final.txt` (full-string windows over all 145 formats),
`/tmp/w994_load.txt` (`dyld::load`'s two throws + caller census of `isCompatibleMachO`),
`/tmp/w994_str.txt` / `w994_str2.txt` (exact literals + try-region bounds),
`/tmp/c_closure.txt` (696-image cache cross-check).