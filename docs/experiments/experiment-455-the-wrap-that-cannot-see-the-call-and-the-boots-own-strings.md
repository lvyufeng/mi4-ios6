# Experiment 455 — the wrap that cannot see the call, and the boot's own strings

**Status: built, gated, run on hardware.** The step's question was 454's: the 30 s deadline wait
is `IOFindBSDRoot`'s, and since the resource it waits for was published *before* the wait, the
frontier is the registry rather than the sleep. So 455 wraps the registry query — the two functions
that are the query itself (`copyExistingServices`, `matchPassive`) and the four factories that build
the dictionary it is asked (`serviceMatching`/`resourceMatching`, `TRACE_LDFLAGS` 27 -> 33) — and
the answer is a **negative result about the instrument, with a positive control inside the same
run**: the two query wrappers produced **zero records**, the four factory wrappers fired **three
times**, and the reason is in the linked image rather than in the run — `ld --wrap` rewrites an
*undefined* reference, and `IOService::waitForMatchingService` calls `copyExistingServices` from
`IOService.cpp`, the object that defines it, so all eleven call sites went to the real function.
The frontier itself is unchanged and reproduces 454 **to the tick**: the two
`IORecursiveLockSleepDeadline` records are `waitForMatchingService+0xe0`, `dl - now = 575999988`
ticks in both, and `dl` and `now` advance together by 205641 ticks between them. What the run does
give is the boot's *own strings*, resolved host side from the pointers the factories recorded:
`IOPMPowerSource`, `"IORTC"`, `"IOResources"`, and the publishes `"IORTC"` then **`"IOBSD"`** —
`publishResource("IOBSD")` and `IOFindBSDRoot`'s `serviceMatching("IOResources")` are adjacent
records, which is the measurement that the wait's dictionary was built *after* the resource it waits
for was already published. Two build-time checks were added, and both of them were wrong at first
(one died of SIGPIPE, one called a reachable wrapper unreachable). Run: 303883 bytes, **287 live
records**, 33 blocks with 8 returns, 6 sleeps, **2 IOKit deadline waits**, 3 dictionary records, 0
query records, 25 x `persistent_write_attempted=0x00000000`, 87 x
`failure_mask=0x00000000`, no `exception:`/`panic:`/`stub_hit=`, device back on its own. `.text`
5013120 -> **5016032**, entry bin 5208596 -> **5224980**, `.bss` `0x804f7a40 .. 0x80548c58` ->
`0x804fba40 .. 0x8054cd98`, payload `cfbc8cc4...` 8245248 bytes, 33 `__wrap_` symbols.

## What the step adds

Six new `--wrap`s and four new instruments, all of them in `entry_stubs.c`/`entry_trace.c`:

| wrap | what its record is for |
| --- | --- |
| `_ZN9IOService20copyExistingServicesEP12OSDictionarymm` | the query's own `(dict, inState, options, result)` — the return value *is* the measurement |
| `_ZN9IOService12matchPassiveEP12OSDictionaryj` | the verdict, filtered on `this == gIOResources` so the record is about the resource root and not about whichever candidate the walk happened to ask |
| `serviceMatching` (`PK8OSString`, `PKc`) | which name the query asked for, and which dictionary object it got |
| `resourceMatching` (`PK8OSString`, `PKc`) | the same for the resource-root route |

`entry_rs_state()` is the third instrument and the one that reaches inside an object this project
has no class for: it calls `IOService::getResourceService()` — `_ZL12gIOResources` is a *local*
symbol, so the accessor is the only way another object can see the resource root at all — and then
reads `__state[0]`/`__state[1]` at `+36`/`+40`. It is safe to call from the report path only because
the function is a **leaf** (`movw/movt/ldr/bx lr`), and the offsets are not written down twice: the
build reads `IOService::getState()`'s own load and refuses the link if it is not `+36`.

Two new checks in `verify_trace_symbols` came out of the same reasoning — *a claim in a comment is
not a check* — and they are the two facts the instrument rests on. The first disassembles
`getResourceService`'s window (bounded by the **next symbol** rather than by a fixed 24 bytes, and
truncated at its first `bx lr`) and requires no `bl`, no `blx` and no `push`. The second is the
doubled-underscore check: 455's first draft wrote `__ZN9IOService18getResourceServiceEv` for the
mangled name, and it **linked** — the generator invents a stub for anything undefined — so the image
carried the real function at `0x8012eb3c` *and* a generated stub at `0x804553bc` that the instrument
would have called. Itanium mangling starts with exactly one `_Z`, so every `__Z...` in the image is
that mistake and nothing else; the check refused the build, the name was fixed in both the
declaration and the call, and the undefined-symbol count fell 28 -> 27.

The step also had to split the epilogue's key list. Adding 455's keys to `entry_epilogue` failed the
assembler with `bad immediate value for offset (4508)`: `ldr rX, .Lpool` is PC-relative over ±4095
bytes and the epilogue is already ~4.4 KB of per-key `entry_write_kv`. `entry_write_455_kv()` is
`noinline` and holds 41 keys (455's, plus 454's iolock block moved into it), and the worst
PC-relative pool distance afterwards is **3360** bytes. The comment there says what the next step
should do about it: make the key list a data table, so no future experiment re-approaches the limit.

## The build's own defect: a check that dies silently on success

`set -euo pipefail` plus an early-exiting *reader* is a trap, and the first version of the
reachability check walked into it. `sym_next` was
`nm -n … | awk '$1 == s { p = 1; next } p { print; exit }'`; `awk` stops reading at the first
match, so `nm` — still writing a symbol table of ninety-odd thousand lines into a pipe nobody is
reading — is killed by SIGPIPE, and under `pipefail` the *pipeline* exits 141. `next=$(sym_next …)`
therefore aborted the whole build: it stopped dead **after `verify_pad`, with no message at all**,
and the only sign was a status of 141 and a log that ended between two `say` lines. A check that
dies silently on success is worse than no check. Every reader in the function now reads its input to
the end and selects after the fact, and the `| head -1` idiom (safe only while the writer's output
fits in the pipe buffer) is gone. The fix is in the file's comment, because the next reader of that
function is the person who would otherwise write it again.

## The measurement: zero records from the two functions the step was built for

The run reports **three** `dict_*` records and **zero** `match_*`/`mpass_*` records. The three are
resolvable host side, because the factory's `name` argument is a pointer in the entry image:

| seq | site | the call | `name` | resolved |
| --- | --- | --- | --- | --- |
| 1 | `0x80150ad0` (`IOPMrootDomain::start`) | `bl __wrap_…serviceMatchingEPKc…` | `0x804762cc` | `"IOPMPowerSource"` |
| 2 | `0x8011d378` (`IOKitInitializeTime`) | `bl __wrap_…resourceMatchingEPKc…` | `0x804701c0` | `"IORTC"` |
| 3 | `0x80205118` (`IOFindBSDRoot`) | `bl __wrap_…serviceMatchingEPK8OSString…` | `0xc0520bc0` | a **heap** pointer — the unique `OSSymbol` for `"IOResources"`, i.e. `gIOResourcesKey`'s runtime value |

(The strings are read out of `xnu_arm_entry.bin` at file offset `VA - 0x80000000`, which is a
host-side reading of a device reading: the pointer came from the run, the characters from the image
the device ran.)

The two query wrappers are in the image and are never branched to. `objdump -d` on the linked image
finds **11 real call sites** to `copyExistingServices`/`matchPassive` — and not one `bl`/`blx` to
either `__wrap_`:

    8012e038: eb000053  bl 8012e18c <_ZN9IOService20copyExistingServicesEP12OSDictionarymm>   (0x8012e18c = the real function; the wrapper is at 0x80455c4c)
    80135b54: ebffe18c  bl 8012e18c <…copyExistingServices…>                                  (inside waitForMatchingService)
    …nine more, all in the same object, plus the two branches inside the wrappers themselves

The mechanism is one sentence of the linker's own documentation: `--wrap` rewrites an **undefined**
reference, and a reference from an object that also *defines* the symbol is resolved inside that
object before the wrapper is ever consulted. `waitForMatchingService`, `getMatchingServices`,
`copyMatchingService` and `waitForService` all live in `IOService.cpp`; `copyExistingServices` and
`matchPassive` are defined in `IOService.cpp`; so every call the boot makes to them is invisible to
the wrap. The four factories are the internal control: their callers are in `IOKitBSDInit.cpp`,
`IOStartIOKit.cpp` and `IOPMRootDomain.cpp`, three other objects, and every one of the three ran.

A wrapper links whether or not anything reaches it. The symbol is defined, the undefined-symbol
count does not move, the build's other checks pass — and a run that reports nothing looks exactly
like a run in which the measured thing did not happen. That is the defect this step found, and it is
the same shape as 449's and 450's: *the instrument's failure mode was the silence it was built to
remove.*

## The check that makes it structural — and the check's own two mistakes

The repair is a build-time check, not a comment: every `--wrap=` in `TRACE_LDFLAGS` must have a
**branch to its wrapper** somewhere in the linked image, or be listed with a reason. It has three
honest readings of zero, and they are kept apart:

| bucket | members | why |
| --- | --- | --- |
| branched to | 30 | reached by a `b`/`bl`/`blx` in the image |
| same-object only | `copyExistingServices`, `matchPassive` | `ld` resolves the reference inside `IOService.cpp`; the tracer itself has to supply the call site — which is exactly what 456 will do |
| never called here | `sleep` | `sleep` is the KPI entry to `_sleep` and nothing in this kernel calls it; 453 wrapped the whole seven-name family because `_sleep` is `static` |

The check was wrong twice before it was right, and both mistakes are the class of thing this project
keeps recording. The first version matched `<__wrap_symbol>` *anywhere* in the disassembly, which
every wrapper satisfies by its own `<__wrap_symbol>:` label — so it printed "all 33 reachable",
including the two the device had just proved unreachable. The second version required a `bl`, which
is not what a tail call is: `last_kernel_constructor` is three instructions ending in

    80268960: ea07b2ed  b 8045551c <__wrap_iokit_post_constructor_init>

so the corrected check reported a reachable wrapper as dead. A check is an instrument too, and the
only reason both were caught is that the answer it gave was already known from the run.

The good news is what the check buys: the next step's instrument cannot be built on a dead wrapper
without the build saying so.

## The frontier is unchanged, and the run says so to the tick

| reading | 454 | 455 |
| --- | --- | --- |
| site (`__builtin_return_address(0)`) | `0x80135a54` = `waitForMatchingService+0xe0` | `0x80135f94` = `waitForMatchingService+0xe0` |
| `dl - now`, both records | 575999988 ticks | 575999988 ticks |
| `dl` and `now` between the two calls | 206259 ticks, together | 205641 ticks, together |
| `inter` / `ent` | 0 / 1 | 0 / 1 |

The site resolves in the image that ran: `0x80135f94 - 4 = 0x80135f90` is
`bl 8045588c <__wrap_IORecursiveLockSleepDeadline>`, and `waitForMatchingService` itself moved by
`0x540` between the two builds — which is the growth of the instrument's own object at `0x8000xxxx`,
linked ahead of the XNU objects (`.text` as a whole grew `0x2912`, the rest of it being the six new
wrappers at `0x80455xxx`, which are linked after). The *only* difference between the two runs'
deadlines is which two of the boot's waits were recorded, and both are the same constant, computed
from the same clock.

The timing, from the run's own records: the first block at `t = 3.9327 s`, the last at `3.9523 s`
(375487 ticks, 19.6 ms of trace), the two waits entered 205641 ticks apart (10.7 ms) — so wait 1 in
*this* run was woken ~10.7 ms later, not the 106 us 454 measured between its block records; both
readings are of the same event and neither is a duration claim about the other. The last record of
the run is **575992236 ticks (29.9996 s) short** of wait 2's deadline: the deadline did not pass, it
never arrived, and the hardware watchdog (`hw_watchdog_timeout_s=0x19`, 25 s) returned the device
first.

## What the run's own strings buy

Interleaved with the factory records, in the log's own order, are the publishes:

    3983  dict_site=0x80150ad0  dict_name=0x804762cc   "IOPMPowerSource"      IOPMrootDomain::start
    3990  pub2_key=0x804701c0                          "IORTC"               publishResource(const char *)
    3997  dict_site=0x8011d378  dict_name=0x804701c0   "IORTC"               IOKitInitializeTime -> resourceMatching
    4003  iolock_site=0x80135f94  now=0x0482a277                             wait 1, entered
    4086  pub2_key=0xc20a3db0                          a runtime string      publishResource(const char *)
    4108  pub2_key=0x80472ae3                          "IOBSD"               publishResource(const char *)
    4109  dict_site=0x80205118  dict_name=0xc0520bc0   "IOResources"         IOFindBSDRoot -> serviceMatching
    4115  iolock_site=0x80135f94  now=0x0485c5c0                             wait 2, entered - never returns

Three things are read out of this, and none of them needed the query wrappers:

1. **`"IOBSD"` was published before the wait that wants it**, and by the boot's own record order
   rather than by an argument from the source's shape. `publishResource` is wrapped only in its
   `const char *` form, so these three records are the `const char *` calls, and the third is the
   one `IOKitBSDInit()` makes.
2. **Wait 1 (the IORTC resource wait) also entered its sleep** — after `"IORTC"` had been published
   at 3990. `IOKitInitializeTime` uses `resourceMatching("IORTC")`, whose dictionary is
   `{IOProviderClass: IOResources, IOResourceMatch: "IORTC"}`, and that route ends at
   `IOResources::matchPropertyTable`'s **first** branch, `ok = (0 != getProperty(str))`
   (`IOService.cpp:5095`) — a property that exists by 3997. It slept anyway. So the fast path's guard
   *before* the matcher failed for the IORTC query too, which means the guard is failing for both
   queries and the 30 s deadline is what both of them fell back on.
3. **The order is the 454 conclusion, re-measured with resolved strings**: the publish of `"IOBSD"`
   (4108) is immediately before `IOFindBSDRoot`'s dictionary build (4109) and the wait (4115), and
   nothing in this image publishes `"IOBSD"` a second time — so when the query found nothing, the
   notification it registers cannot fire later either.

The guard that fails is the one line in `copyExistingServices` that runs before the matcher:

    if( (inState == (service->__state[0] & inState))        // inState = kIOServiceMatchedState = 0x4
      && (0 == (service->__state[0] & kIOServiceInactiveState))
      &&  service->matchPassive(matching, options))

`kIOServiceMatchedState` has **no `|=` of its own anywhere in the tree**: the only writer of that
bit is `copyNotifiers`' `orNewState` parameter (`IOService.cpp:4782`,
`__state[0] = (__state[0] | orNewState) & andNewState`), called from `doServiceMatch`'s tail as
`copyNotifiers(gIOMatchedNotification, kIOServiceMatchedState, 0xffffffff)` (`:3754`) — and that call
sits behind a guard (`:3748`):

    if( (0 == (__state[0] & kIOServiceInactiveState))
     && (0 == (__state[1] & kIOServiceModuleStallState)) ) {
        if (resourceKeys) setProperty(gIOResourceMatchedKey, resourceKeys);
        notifiers[0] = copyNotifiers(gIOMatchedNotification, kIOServiceMatchedState, 0xffffffff);

Two things are in that one guard, and 455 can separate neither from the outside — which is what 456
is for: whether `"IOResources"` reaches `kIOServiceMatchedState` at all (the state word), and
whether the `IOResourceMatched` **array** exists on the resource root (`resourceKeys`), because the
matcher's third branch asks `copyProperty(gIOResourceMatchedKey)` on the *candidate*
(`IOService.cpp:5109-5114`) and `publishResource` never adds to any array — it is
`gIOResources->setProperty(key, value)` and nothing else (`:3532-3544`). `resourceKeys` is written
in exactly one place (`:3729`), the `this == gIOResources` branch of
`if (keepGuessing && matches->getCount() && (kIOReturnSuccess == getResources()))`, whose
`matches` comes from `gIOCatalogue->findDrivers(this, &catalogGeneration)`.

## What the next step measures

The corrected instrument does not need a new mechanism; it needs a **call site the wrapper can see**
and a **reading taken where the boot actually is**. Three probes, all attached to the one wrapper
that fires at the frontier — `IORecursiveLockSleepDeadline` — so the reading is taken inside the wait
that never returns:

  * `entry_rs_state()`: `gIOResources`' `__state[0]`/`__state[1]`, live. Prediction: `0x2|0x4` with
    the inactive bit clear and `__state[1] & 0x20000000` (`kIOServiceModuleStallState`) **clear** —
    which would put the failure past the state guard and into the matcher.
  * `IORegistryEntry::copyProperty(gIOResources, gIOResourceMatchedKey)`: the array or NULL, then
    `getCount()` and `getNextIndexOfObject(gIOBSDKey, 0)` — the `"IOBSD"` membership question,
    answered directly instead of by inference. (`gIOResourceMatchedKey` and `gIOBSDKey` are plain
    globals at `0x80531ce4` and `0x80531d80`.)
  * and the query itself, called by the *instrument*: `serviceMatching("IOResources")` +
    `setObject(IOResourceMatched, "IOBSD")` + `copyExistingServices(dict, 4, 1)`, whose record the
    now-known-dead wrapper will finally produce, because the call site is in `entry_trace.c` and not
    in `IOService.cpp`.

The two prediction branches are both informative: state bit clear means the frontier is the state
word (and the module-stall bit is the thing to explain next); state bit set with a NULL array means
`doServiceMatch` never reached its `this == gIOResources` branch on this boot, i.e. the catalogue
matched no driver to the resource root — which would be the *first* fact about this boot's IOKit
that is about the missing kexts rather than about the missing timer.

Safety, unchanged and re-measured on every run: `fastboot boot` only, never flash; every touch
through `stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed; the device returned
to Android on its own; 25 x `persistent_write_attempted=0x00000000`, 87 x
`failure_mask=0x00000000`, no `exception:` line. The trace is read-only with respect to the boot: the
factories record and call through, and in this image two of the six new wrappers are not on any path
the boot takes at all.
