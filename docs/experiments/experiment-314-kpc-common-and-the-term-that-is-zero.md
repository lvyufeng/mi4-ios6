# Experiment 314 — `kpc_common.c`, and the step where `.text` closes exactly because one term is zero

**Step:** link **`osfmk/kern/kpc_common.c`** (`out/xnu_kernel_obj/osfmk_kern_kpc_common.o`,
manifest:562) — the object that defines `kpc_common_init`, the name 313 stopped on.

**Prediction:** *`kpc_common_init` is three real lock calls and a tail branch, so it returns and the stop
is one call further on — the next call `kernel_bootstrap_thread` makes, `ktrace_init` at caller key
`0x8000E660`.* Plus counts: **23 resolved / 1 added** (`kperf_sample`), 748 → 726 undefined and
658 → 637 function stubs.

**Result:** the key lands exactly — `stub_hit=ktrace_init` at `xnu_entry_stub_caller=0x8000e660` =
`kernel_bootstrap_thread + 0xe0`. **And for the first time in four steps the count prediction is right
in both halves:** 23 resolved / 1 added, measured. The step's own surprise is elsewhere — in what does
*not* move.

## The object, and the `.text` arithmetic that closes with no residual

`osfmk_kern_kpc_common.o` is `.text` **8072** / `.bss` 64 / `.rodata.str1.1` **4** / `__DATA,__data`
**120**, with 45 defined symbols and 38 references.

The baseline was built in this session with an **empty stand-in object** in this slot, reproducing 313
exactly (748 / 658 / 90, `.text` 0x11F4C0, `.data` 0x80120000 size 0x18CA8, `.bss`
0x80138DC0..0x8016FD18, headroom 1639144).

|  | predicted | measured |
|---|---|---|
| undefined | 726 | **726** |
| function stubs | 637 | **637** |
| storage stubs | 89 | **89** |
| resolved / added | 23 / 1 | **23 / 1** |

The split is exact as well, and it is a *different* split from 313's. Of the 23 resolved, 22 are
functions and **one is storage** — `kpc_actionid`, which 313 created as a 24-byte stand-in and which
this object defines as `uint8_t kpc_actionid[0x18] __attribute__((aligned(64)))`. The one added,
`kperf_sample`, is a function. Two counters, two different movements, and the arithmetic closes.

`.text` 0x11F4C0 → **0x1210A0** is +0x1BE0, and this time the numbers close exactly:

```
  this object's .text                                  +0x1F88   (8072)
  the stub object's .text, net                         -0x1F8   (22 bodies retired, 1 created)
  the stub object's .rodata.str1.4, net                -0x1A8   (22 names retired = 0x1B8 padded,
                                                                 1 created = 0x10)
  .text-region alignment fill                          -0x008   (map total 0x88D0 -> 0x88C8)
                                                       -------
                                                        +0x1BE0
```

## The term that is zero, and why it is worth a section of its own

The 313 block predicted this object would add **4** bytes of `.rodata.str1.1` — it has a 4-byte
`.rodata.str1.1`, so that is what a sum of inputs says. **It adds zero**, and the reason is a
duplicate. The four bytes are the string `"kpc"`, and `bsd_kern_kern_kpc.o` — linked two steps earlier
— already put the same four bytes into the same mergeable group. The image contains exactly **one**
`kpc\0`, at **0x8011B622** — a byte scan of `out/stage90/xnu_arm_entry.bin` finds that offset and no
other — and the linked `kpc_init` loads exactly that address as `lck_grp_alloc_init`'s first argument:

```
8010276c:  movw r0, #46626   ; 0xb622
80102770:  movt r0, #32785   ; 0x8011
80102774:  bl   80011608 <lck_grp_alloc_init>
```

So one string serves both objects, and the linker is right. The map, however, still prints a
contribution line for the one that contributed nothing:

```
 .rodata.str1.1
                0x000000008011d402        0x4 /mnt/data/mi4-ios6/out/xnu_kernel_obj/osfmk_kern_kpc_common.o
 *fill*         0x000000008011d402        0x2
```

Both lines are pre-relaxation: the address is one the section never had, and the `0x4` is four bytes
the section never grew by. **This is the second half of the lesson 301 and 312 taught.** 301: a
*mergeable section's size* in the object is an upper bound (`arm-none-eabi-size -A` said 0xC7 where the
link kept 0x3A). 312: the map prints both numbers side by side (`0x181` beside `0x1ac (size before
relaxing)`). 314: **a mergeable input's contribution line in the map is an upper bound too** — it is
printed for an input that contributed nothing, and the only way to see that is to add up what the
section actually did and find that the extra term does not fit.

## `.data` grows by exactly the object, and then moves a whole 16 KB

`.data` 0x18CA8 → **0x18D20** is +0x78 = 120, which is the object's `__DATA,__data` to the byte: five
24-byte `site` records.

But the output section's *start* moves 0x80120000 → **0x80124000**, and that is not this object's
doing. The `.data` output section is **16 KB aligned** (304 recorded the same shape), so it rounds up
to the next 16 KB boundary — and this is the step where `.text`'s end crossed one:

```
.text end   313: 0x8011F4C0   <- below 0x80120000
            314: 0x801210A0   <- above it
.data start 313: 0x80120000
            314: 0x80124000   <- the next 16 KB boundary
```

The whole data block therefore moves a page-group for a reason that has nothing to do with what was
linked, and every address downstream of `.data` moves with it:

| | base (313) | measured (314) | delta |
|---|---|---|---|
| `.text` | 0x11F4C0 | **0x1210A0** | +0x1BE0 |
| `.data` | 0x80120000 | **0x80124000** | +0x4000 alignment, +0x78 object |
| `.sysctl_set` | 0xDC | 0xDC | 0 |
| image | 0x138D84 | **0x13CDFC** | +0x4078 |
| `__bss_start` | 0x80138DC0 | **0x8013CE00** | +0x4040 |
| `.bss` size | 0x36F58 | **0x36F58** | **0** |
| `__bss_end` | 0x8016FD18 | **0x80173D58** | +0x4040 |
| headroom | 1639144 | **1622696** | **−0x4040** |

`__bss_start`'s +0x4040 is 0x4000 (the `.data` step) + 0x78 (the object) − 0x38 (the alignment after
`.sysctl_set` shrank from 0x3C to 0x4, because the 0x78 changed the residue). Because `.bss` itself does
not move, `__bss_end` moves by the same amount and the headroom loses exactly it.

## `.bss`: the fourth case of the alignment rule, and the first where nothing moves

```
 .bss   0x8016e4e8  0xb8  osfmk_arm_kpc_arm.o            <- 313's
 .bss   0x8016e5a0  0x40  osfmk_kern_kpc_common.o        <- this object's 64 bytes
 *fill* 0x8016e5e0  0x20
 .bss   0x8016e600  0x1704 xnu_arm_entry_realstubs.o    <- 0x1744 - 0x40: one slot retired
```

Offsets from the section start: 0x357E0 for the object and 0x35840 for the stand-ins, against 0x357E0
and 0x35800 in 313's map. The gap in front of the 64-byte-aligned stand-ins is
`align64(end) − end` = 0x20, and this object's 0x40 does **not** fit in it — so this is the
**310-shaped case**, and the stand-in block moves to the next 64-byte boundary, +0x40. What is new is
that nothing downstream moves at all: the slot the object displaces is the retired `kpc_actionid`
stand-in, which was also 64 bytes wide, because it too was declared `aligned(64)`.

| step | object `.bss` | fill in front of the stand-ins | `__bss_end` |
|---|---|---|---|
| 308 | 0x18 | 0x2C, of which 0x10 was the gap | **unmoved** |
| 310 | 0x1C | 0x10 | **+0x40** |
| 312 | 0x18 | 0x34 | **0** (the fill absorbed it) |
| 313 | 0xB8 | — | **+0x100** (0xB8 + 0x8 + a new 0x40 slot) |
| 314 | 0x40 | 0x20 — does not fit | **0** (the retire equals the insert) |

The rule 308/310/312 established is unchanged; 314 is the case it does not have to be bent for, and the
only reason the section is unchanged is that the step both inserts 0x40 and retires 0x40.

## The run

```
MI4IOS6_STAGE90_XNU disarm_hw_watchdog_en=0x00000001
MI4IOS6_STAGE90_XNU xnu_entry_entering_at=0x80000074
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_stub_caller_v=0x8000e660   xnu_entry_stub_caller_e=0x8000e660
 xnu_entry_abort_entries=0x00000000   xnu_entry_failures=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ktrace_init
```

`tools/host_resolve_entry_addr.sh 0x8000e660` → `kernel_bootstrap_thread+0xe0`, the `bl` at
`0x8000e65c`:

```
8000e64c  bl bootprofile_init            <- ran and returned
8000e658  bl kernel_debug_string_early
8000e65c  bl ktrace_init                 <- THE STOP (lr 0x8000e660)
8000e674  bl kdebug_init
```

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, watchdog ARMED, no storage symbols in the
payload), log **301623** bytes, one `stub_hit=` line, **no `exception:` line**.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, `xnu_entry_abort_entries=0x00000000`, and the device returned to
Android on its own (`ro.build.version.release` = 10).

## What it measures: KPC's initialization is complete

`kpc_init` returned. Its body is now entirely real, and every part of it arrived by this walk:

```
kpc_init                     (312)  three lock calls, then
  kpc_arch_init              (313)  mrc p15, 0, r0, cr9, cr12, {0}; bx lr
  kpc_common_init            (314)  three lock calls, tail branch to lck_mtx_init
  kpc_thread_init            real since osfmk_kern_kpc_thread.o entered LINK_OBJS
```

Together they establish a lock group named `"kpc"` and a mutex over it (312), 184 bytes of ARM PMU
state (`saved_PMOVSR`, `saved_PMCNTENSET`, `saved_PMXEVTYPER`, `saved_counter`, `kpc_running_classes`,
the four `kpc_*_sync` flags — 313), and now 64 more bytes of kpc-common lock state, 120 bytes of `site`
records and the whole `kpc_*` API as real code.

The run also measures something it was not aimed at: **`bootprofile_init` returned.** Between 313's stop
and this one, `kernel_bootstrap_thread` called it at 0x8000e64c and `kernel_debug_string_early` at
0x8000e658, so two more entries of that function are consumed. 314's stop is 0x24 further along the same
straight line than 313's, which is entirely explained by the image growing.

## What it does not measure

* **That any `kpc_*` function works.** The 23 names this step resolved are names the boot has
  *acquired*; the only one it called is `kpc_common_init`. `kpc_get_config`, `kpc_set_running` and the
  rest are linked and unreached, exactly as 313's PMU code is.
* **Whether the `.data` step costs anything.** The 0x4000 is alignment, not content: the image grew
  0x4078 and the headroom lost 0x4040, and the difference is address arithmetic, not bytes.
* **That `kpc_actionid`'s initial value is right.** It is 24 bytes of zeroed `.bss` in both the
  stand-in and the real definition; the storage moved from one to the other unchanged.

## Next

**`bsd/kern/kern_ktrace.c`** (`bsd_kern_kern_ktrace.o`, manifest:40) — the object that defines
`ktrace_init`: `.text` **2564** / `.bss` 82 / `.rodata.str1.1` 250 / `.data` 288 /
`__DATA,__sysctl_set` **24**, with 47 defined symbols and 25 references.

`ktrace_init` is four real lock calls — `lck_grp_attr_alloc_init`, `lck_grp_alloc_init("ktrace", …)`,
`lck_grp_attr_free`, `lck_mtx_alloc_init` — plus `assert(ktrace_mtx != NULL)`, so like 310–314 it
should return. But the stop is predicted to leave `kernel_bootstrap_thread`'s straight line for the
first time in a long while. The next call after `ktrace_init` is `kdebug_init` at 0x8000e674, and

```
8003fed0 <kdebug_init>:  b 8003fefc <kdebug_trace_start>
```

is a one-instruction trampoline into a function that calls four of the eleven names this step resolves,
at six call sites. A walk of this image from `kdebug_trace_start` stops on
`ktrace_end_single_threaded` — one of them.

Predicted stop: **`sysctl_early_init`, caller key 0x8000E6A4**, on the reasoning that the ktrace calls
all resolve, `prng_cpu_init` has no stub call at all, and `bsd_early_init` at 0x8000e6a0 is *another*
one-instruction trampoline whose target is a stub. The caller key is `0x8000E6A4`, not `0x8003A9E4`,
because a `b` does not set `lr` — the address the stub reports is the `bl bsd_early_init`'s return in
`kernel_bootstrap_thread`. Named alternatives: the ktrace path reaches a stub this step does not
resolve; `prng_cpu_init`'s one indirect call stops the run; or `ktrace_init`'s `assert` fires, which
would be a panic rather than a stub and which the log distinguishes.
