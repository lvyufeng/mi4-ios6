# 993: what dyld was actually failing at when `throwf` itself OOM'd

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