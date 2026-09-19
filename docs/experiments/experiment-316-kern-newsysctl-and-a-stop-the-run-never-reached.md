# Experiment 316 — `kern_newsysctl.c`, and a stop the prediction named but the run never reached

**Step:** link **`bsd/kern/kern_newsysctl.c`** (`out/xnu_kernel_obj/bsd_kern_kern_newsysctl.o`, manifest:46) —
the object that defines `sysctl_early_init`, the name 315 stopped on.

**Prediction:** *`sysctl_early_init` is four calls and all four are already real, so it returns; and the stop
then leaves `kernel_bootstrap_thread`'s straight line, because `PE_init_iokit` at 0x8000e6b0 contains exactly
one stub call — a conditional `bl StartIOKit` at 0x80004a1c. Predicted stop: `StartIOKit`, caller key
`0x80004A20`.* Plus counts: **6 resolved / 6 added**, 719 → 719 undefined and 630 → 629 function stubs.

**Result:** **every count lands, and the stop does not.** The counts were right in all three columns and in
the resolved/added split down to which side of the function/storage line each name falls on. The run stopped
on a name the prediction did not mention — `OSKextKextForAddress`, reached five frames deep inside a
`printf` — and the reason it did is the interesting result of the step.

## The object, and a count prediction that was right in every column

`bsd_kern_kern_newsysctl.o` is `.text` **8132** / `.bss` 28 / `__DATA,__data` 96 / `.rodata.str1.1` 71 /
`.data` **240** / `__DATA,__sysctl_set` **20**, with 59 defined symbols and 35 references.

The baseline was built in this session with an **empty stand-in object** in this slot, reproducing 315 exactly
(719 / 630 / 89, `.text` 0x121A60, `.data` 0x80124000 size 0x18E40, `.sysctl_set` 0x8013CE40 size 0xF4,
`.bss` 0x8013CF40 size 0x36F98, image 0x13CF34, `__bss_end` 0x80173ED8, headroom 1622312).

|  | predicted | measured |
|---|---|---|
| undefined | 719 | **719** |
| function stubs | 630 | **629** |
| storage stubs | 89 | **90** |
| resolved / added | 6 / 6 | **6 / 6** |

The split is exact where it is easy to get wrong. **Resolved 6** — five functions and one storage:

```
func sysctl_early_init T          <- this stop
func sysctl_handle_int T
func sysctl_handle_quad T
func sysctl_handle_string T       <- added by 315, resolved here: it lasts exactly one experiment
func sysctl_io_number T
data sysctl__children B 0x4       <- storage
```

**Added 6** — four functions and two storage, and the two storage ones are the pair that keeps the function
counter from explaining the total:

```
func fuulong T
func mac_system_check_sysctlbyname T
func proc_suser T
func suulong T
data securelevel B 0x4
data sysctl__sysctl_children B 0x4
```

Two counters moving in opposite directions, and the undefined total not moving at all: −6 + 6 = 0. The
`securelevel` and `sysctl__sysctl_children` entries arrive as 4-byte stand-ins because the pool's definition
of each is a `B` of 4 bytes, so the generator has a size to take — the 304 rule, applied twice in one step.

## `.text` closes exactly

`.text` 0x121A60 → **0x123A00** is +0x1FA0:

```
  this object's .text                                  +0x1FC4   (8132)
  this object's .rodata.str1.1, linked                 +0x02D   (0x47 in the object: 26 bytes relaxed)
  the stub object's .text, net                         -0x018   (5 function bodies retired, 4 created)
  the stub object's .rodata.str1.4, net                -0x02C   (retired 5 names = 0x68 padded,
                                                                 created 4 names = 0x3C)
  .text-region alignment fill                          -0x00D   (0xD1A -> 0xD0D)
                                                       -------
                                                        +0x1FA0
```

The map agrees per region: `.text` non-fill +0x1FAD (the first four terms) and `.text` fill −0xD. **The
retired count is five and not six** — `sysctl__children` is storage, and a storage stand-in has neither a
body nor a name string, so it contributes to `__bss_end` and to nothing in `.text`. Reading the six
resolutions as six retired bodies is the natural mistake and it would miss the `.text` total by 0x18 + the
string padding.

## `.data`, `.sysctl_set`, `.bss`

`.data` 0x18E40 → **0x18F90** is +0x150 = the object's `.data` 0xF0 (240) **plus** its `__DATA,__data` 0x60
(96): two different section names in the object that land in the same output section. Nothing steps this
time — the start stays 0x80124000.

`.sysctl_set` moves 0x8013CE40 → **0x8013CF90** and grows 0xF4 → **0x108** (+0x14 = 20), following `.data`.

`.bss` 0x8013CF40 → **0x8013D0C0** and size 0x36F98 → **0x37018**, which is +0x80 = 0x5C non-fill + 0x24
fill. The object's own 0x1C is 28 bytes; the other 0x40 is the **fifth case of the alignment rule** and the
first where the arithmetic is a *net* of three moves:

```
  retire  sysctl__children               -0x40    (a 64-byte stand-in slot; now real)
  insert  securelevel                    +0x40
  insert  sysctl__sysctl_children        +0x40
                                        -------
                                         +0x40
```

Three 4-byte variables, three whole 64-byte slots, because all three carry `aligned(64)`. The stand-in block
grows 0x1704 → 0x1744 for exactly that reason. The section closes as 0x1C (this object) + 0x40 (the stand-in
block) + 0x24 (fill):

```
                     base (315)                                   measured (316)
 .bss  ...172760  0x52 ktrace            .bss  ...1728e0  0x52 ktrace
 *fill* ...1727b2  0xe                   *fill* ...172932  0x2    <- new: the 0x52's residue
       (empty_entry.o, 0 bytes)         .bss  ...172934  0x1c  newsysctl
       (no gap needed)                  *fill* ...172950  0x30   <- the 64-byte gap
 .bss  ...1727c0  0x1704 realstubs       .bss  ...172980  0x1744 realstubs
 *fill* ...173ec4  0x4                   *fill* ...1740c4  0x4
 *fill* ...173ec8  0x10                  *fill* ...1740c8  0x10
```

Both gaps in front of the stand-ins are `align64(end) − end` for *different* ends — 0xE in the base because
the empty stand-in ended at 0x801727B2, 0x30 here because the newsysctl object ends at 0x80172950 — so the
0xE → 0x30 is not a like-for-like comparison of one gap. Summing the whole region is what closes the
arithmetic: the fills after the ktrace contribution go 0x22 → **0x46**, +0x24.

| | base (315) | measured (316) | delta |
|---|---|---|---|
| `.text` | 0x121A60 | **0x123A00** | +0x1FA0 |
| `.data` | 0x80124000 (0x18E40) | 0x80124000 (**0x18F90**) | +0x150 |
| `.sysctl_set` | 0x8013CE40 (0xF4) | 0x8013CF90 (**0x108**) | +0x14 |
| `.bss` | 0x8013CF40 (0x36F98) | **0x8013D0C0** (**0x37018**) | +0x180 / +0x80 |
| image | 0x13CF34 | **0x13D098** | +0x164 |
| `__bss_end` | 0x80173ED8 | **0x801740D8** | +0x200 |
| headroom | 1622312 | **1621800** | −0x200 |

`__bss_start`'s +0x180 is 0x150 (`.data`) + 0x14 (`.sysctl_set`) + 0x1C (the alignment after `.sysctl_set`
grew from a 4-byte to a 8-byte residue); `__bss_end` adds the 0x80 of `.bss`.

## The run, and the stop the prediction did not name

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=OSKextKextForAddress
 xnu_entry_stub_caller=0x80096298
```

`tools/host_resolve_entry_addr.sh 0x80096298` → `_os_log_to_log_internal+0x2c`, the `bl` at 0x80096294.
`StartIOKit` — the predicted name — is still a stub in this image (`func StartIOKit T` is present in the
step's own stub listing); it was simply never reached. The chain, read out of the image's disassembly:

```
800046fc  PE_init_iokit            bl printf                  (lr 0x80004700)
8002c2b0  printf                   bl vprintf_internal        (lr 0x8002c2b4)
8002c3ec  vprintf_internal         bl os_log_with_args        (lr 0x8002c3f0)
800959d8  os_log_with_args         bl _os_log_to_log_internal (lr 0x800959dc)
80096294  _os_log_to_log_internal  bl OSKextKextForAddress    (lr 0x80096298)  <- THE STOP
```

`PE_init_iokit` was entered, and its **first** `printf` — at 0x800046fc, 0x320 bytes *before* the conditional
`bl StartIOKit` at 0x80004a1c — put the run five frames deep into a logging stack, where the first stub on
the executed path is the kext lookup `os_log` performs for an address it is asked to describe.

**The prediction was not about a branch that was decided against; it was about a call the run never got to.**
And the tool that would have said so had been run and read past. `tools/xnu_entry_callwalk.py --root
PE_init_iokit` reported **no stub on its straight-line path** and listed the indirect calls it could not
follow. `tools/stub_calls_in_function.py PE_init_iokit` then reported exactly one stub call, `bl StartIOKit`
at 0x80004a1c — and that is a true statement about the function's own body and a false one about the run,
because **a real function can hide a stub below itself** and a one-level-deep listing cannot see it. Naming
the single visible stub call was the mistake: `printf` is real, and `printf` is not a leaf. This is 305's
lesson arriving in a new form — *a predicate that is true of every function on a path is not a predicate
about the path* — and the practical guard is that a step's prediction has to be about the **path**, so it has
to start from the callwalk's warning and not end at the per-function stub list.

## What it measures

`sysctl_early_init` ran and returned, with all four of its calls real, and `sysctl_register_set` is *defined
by this object* (0x24C) — so the object's own 20 bytes of `__DATA,__sysctl_set` entries are the registration
the kernel visited.

Then `PE_init_iokit` ran, and the step measures something larger than itself: **this image's `printf` routes
into `os_log`.** `libkern/os_log.c` was linked long ago and `os_log_with_args` has been real since, but no
real XNU code had ever called `printf` in this walk, so the first `printf` on the bootstrap line is also the
first entry into the os_log stack — and it stops on `OSKextKextForAddress`, the kext lookup os_log performs
for a caller address. That is a structural fact about the image that no earlier step could have shown, and it
is worth the cost of the missed prediction.

## What it does not measure

* **Whether `sysctl_register_set` registered anything usable.** The four calls returned; whether the set it
  registered is well-formed is a question about the values in its 20 bytes, and `sysctl__children` — one of
  them — was a zero-filled stand-in until this step, so what the registration now points at is real for the
  first time and unread.
* **Whether `PE_init_iokit` did anything.** The first `printf` is 0x28 bytes into the function; everything
  after it, including all four `DTLookupEntry`/`DTGetProperty` pairs and `vc_progress_initialize`, is
  unreached in this run.
* **Anything about `fuulong`, `suulong`, `mac_system_check_sysctlbyname`, `proc_suser`, `securelevel` or
  `sysctl__sysctl_children`.** They are the six names this step added: linked as stubs, reached by nothing.
* **That `securelevel` is a working variable.** It is 4 bytes of zeroed `.bss` under a name XNU reads as a
  security level; nothing has written it and nothing has read it.

## The run block

```
MI4IOS6_STAGE90_XNU disarm_hw_watchdog_en=0x00000001
MI4IOS6_STAGE90_XNU xnu_entry_entering_at=0x80000074
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_abort_entries=0x00000000   xnu_entry_failures=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=OSKextKextForAddress
```

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, watchdog ARMED, no storage symbols in the payload), log
**301632** bytes, one `stub_hit=` line, **no `exception:` line**.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, `xnu_entry_abort_entries=0x00000000`, and the device returned to Android on
its own (`ro.build.version.release` = 10).

## Next

**`libkern/OSKextLib.cpp`** (`libkern_OSKextLib.o`, manifest:360) — the object that defines
`OSKextKextForAddress`: a small object, `.text` **1424**, `.rodata.str1.1` **474**, `.data` 4
(`gOSKextUnresolved`), 17 definitions and 25 references. Twelve of those references are already defined in
this image and none is currently undefined.

Predicted **4 resolved** (`OSKextKextForAddress` — called *twice* from `_os_log_to_log_internal`, at
0x80096294 and 0x800962c8 — plus `kext_request`, `kext_dump_panic_lists`, `OSKextRemoveKextBootstrap`) and
**13 added**, every one a function defined by `libkern/c++/OSKext.cpp`: `OSKextLog` and the twelve
`_ZN6OSKext*` methods. So 719 → **728** undefined, 629 → **642** function stubs, storage unchanged at 90.

Predicted stop: **`StartIOKit`, caller key `0x80004A20`** — the same prediction 316 made, and it should land
now, because with `OSKextKextForAddress` real there is **no stub left on the `printf` frame chain**: the
first three functions have no stub calls at all and `_os_log_to_log_internal`'s only stub sites are the two
this step resolves. `PE_init_iokit` then runs to its one stub call, and that call is the *common* path — the
guard at 0x800048e4 loads a variable, and a zero value jumps straight to the `bl StartIOKit` block at
0x80004A0C. The named alternative is the debug branch at 0x80004A28, taken only when
`kdebug_debugid_enabled(0x535)` is true *and* the variable's low bits are set.

The **step after** that is `libkern/c++/OSKext.cpp` (`libkern_c++_OSKext.o`, manifest:369), which defines all
thirteen names this step adds: `.text` **74436** plus a COMDAT `.text.*` group, `.rodata.str1.1` 15092,
`.rodata` 424, `.data` 347, `.bss` 477, `__DATA,__data` 216, `__DATA,__sysctl_set` 4, and an `.init_array`
of 4 bytes that this image's `entry.ld` does not name — it would become an orphan output section, which the
build's own layout report is written to catch.
