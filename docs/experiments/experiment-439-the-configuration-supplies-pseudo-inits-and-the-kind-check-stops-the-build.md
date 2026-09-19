# Experiment 439 — `pseudo_inits` becomes the array its header declares, and the guard is a *kind* check

**Step:** one new generator, one new link input, one new check that stops the build.

- `tools/gen_pseudo_inits.py` — derives and renders `pseudo_inits[]` for a configuration.
- `tools/build_xnu_arm_kernel.sh` runs it, and compiles the generated source in the platform block's
  bsd-rooted list (now three files), because the file includes `<dev/busvar.h>`.
- `stages/stage90/xnu_arm_boot/build_entry.sh` gives it `STAGE90_PSEUDO_INITS_OBJ`, links it, and
  calls the check below on the undefined set pass 1 produces.
- `tools/check_stub_kinds.py` — refuses a **function** stand-in for a symbol the tree declares an
  **object**.

## Why: 438's stop was an instruction, and the instruction was a stub's prologue

Experiment 438's run ended here, and the two lines that matter are the first and the fourth:

    real XNU entry: exception: prefetch abort
     xnu_entry_kv_written=0x00000346
     xnu_entry_kv_dropped=0x00000000
     xnu_entry_stub_caller_v=0x00000000        <- no stub was entered at all
     xnu_entry_stub_caller_digits=0x00000000
     xnu_entry_stub_caller_w0=0x756e7820        " xnu"   <- the field was never written; these are
     xnu_entry_stub_caller_w1=0x746e655f        "_ent"   <- the tail of a previous log line
     xnu_entry_abort_entries=0x00000000
     xnu_entry_prefetch_abort_ifar=0xe52de004   <- the faulting instruction address
     xnu_entry_prefetch_abort_ifsr=0x00000005
     xnu_entry_prefetch_abort_lr=0xe52de008
     xnu_entry_prefetch_abort_pc=0xe52de004
     xnu_entry_prefetch_abort_spsr=0xa0000013
     xnu_entry_prefetch_abort_ttbr0=0x8070404a
     xnu_entry_prefetch_abort_ttbr1=0x8070404a
     xnu_entry_prefetch_abort_ttbcr=0x00000001
     xnu_entry_prefetch_abort_sctlr=0x30c5787d

Three lines of source name it completely:

    bsd/kern/bsd_init.c:861     bsd_autoconf();                       <- inside `bsd_init`
    bsd/kern/bsd_init.c:1095    for (pi = pseudo_inits; pi->ps_func; pi++)
                                    (*pi->ps_func) (pi->ps_count);
    bsd/dev/busvar.h:46         extern struct pseudo_init pseudo_inits[];

`struct pseudo_init { int ps_count; int (*ps_func)(int count); }`. The image supplied `pseudo_inits`
as a **function** stand-in — the 0x18-byte body `movw r0, #name; push {lr}; mov r1, lr; movt r0,
#name; pop {lr}; b entry_stub_hit` — so the second word of the array's first entry, which is the
walk's own **termination test**, was the stand-in's `push {lr}`. Non-NULL. `blx r1` jumped to
`0xE52DE004` — the ARM encoding of `push {lr}` — an address no mapping covers, and the prefetch abort
is that jump.

**A function stand-in for an array is not a slightly wrong stand-in.** It is a valid little table
whose second word points into its own instructions, and no amount of care inside the stub generator
can see it: `build_entry.sh` decides function-or-storage from `nm` over the object pool, and when the
pool defines nothing there is no type information at all — the generator falls through to a function
stub. The undefined list gives **names and never kinds**.

## The fix: derive the array, exactly as Apple's build does

`pseudo_inits` is generated in Apple's kernel too — `SETUP/config/mkioconf.c:79-100` writes

    extern int %s(int);                     one per pseudo-device with a d_init
    struct pseudo_init pseudo_inits[] = {
        {%d, %s},                           count = d_slave, and 1 when d_slave <= 0
        ...
        {0, 0},
    };

and the entries come from `dtab`, i.e. the **configuration file's** `pseudo-device` lines in the
order they appear, filtered by doconf's `<feature>` logic. This project already reproduces that
pipeline in `tools/xnu_config/expand.sh CONFIG`, so the derivation is mechanical:

    expand.sh <CONFIG> | the `pseudo-device` lines | those with an `init` word | in order

The count rule (`count <= 0 -> 1`) and the `{0, 0}` terminator are `mkioconf.c`'s, not choices made
here. RELEASE keeps `bpfilter` and `fsevents`; STAGE90_BOOT does not — which is why the generated
source is **per configuration**, in its own path, and why `--check` refuses a stale one. Same reason
as 438's per-component option headers: the array has to belong to the configuration whose objects
are linked.

`mkioconf.c`'s `extern int NAME(int);` declarations do not agree with the real prototypes —
`pty_init` is `int pty_init(int n_ptys)` but `random_init` is `void random_init(void)`,
`fsevents_init` is `void fsevents_init(void)`, `mdevinit` is `void mdevinit(int)`, `bpf_init` is
`void bpf_init(void *)`. Apple's generated file declares them this way and includes nothing that
would notice; the call is `(*pi->ps_func)(pi->ps_count)`, and on the AAPCS the count arrives in `r0`.
Reproducing the *declaration* and not the signature is what lets this file compile under the build's
own `-Werror` set.

## The guard: a kind check, because a name is not a kind

438 said what the check had to be, and this is it: **a symbol declared an array must not be stubbed
as a function.** `tools/check_stub_kinds.py` reads the undefined list and the pool's `nm` dump, and
for every name the pool does **not** define it looks for a declaration in the tree's own headers.
Two patterns, both needed: a prototype is the name followed by `(`; an object is an `extern`
declaration with the name not followed by `(`. An object-declared name fails the build.

It is called from `build_entry.sh` after pass 1's undefined list and `nm` dump are written and
**before** the stand-ins are generated, so an object-kind symbol refuses the build rather than
becoming an array whose first entry is a `push {lr}`.

**What it will not do, and the check prints this on every run rather than implying it passes:** a
declaration scan over `*.h` with two patterns can miss an object declared in a shape those patterns
do not match. Those names come out **unclassified**, counted and listed. A negative result from a
fixed-shape scan is a statement about the scan as much as about the tree — the rule this project's
own defect ledger keeps arriving at — so the count is printed whether or not anything failed.

**Four wrong-kind stand-ins are latent and are recorded, not fixed.** `KNOWN_KINDS` names them with
the measured cause, and the table is validated in **both** directions — an entry that is no longer an
object-declared stub fails the check — so it cannot rot into a list of things that used to be true.
`pseudo_inits` is deliberately *not* in it: 439 supplies it, and if it ever came back the
both-direction check is what would say so.

    etherbroadcastaddr   bsd/net/ethernet.h:131    definer bsd/net/ether_if_module.c, `optional ether`
    lo_ifp               bsd/net/if_var.h:1402     definer bsd/net/if_loop.c, `optional loop`
    osrelease            bsd/sys/sysctl.h:1138     definer config/version.c, a tree-root source
    ostype               bsd/sys/sysctl.h:1139     the same file

**And the first two have a cause that is a defect in its own right, measured while scoping this
step.** The expanded configuration's devices are

    bpfilter ether fsevents loop mdevdevice ptmx pty random

while `out/device_table.txt` holds

    monotonic ptmx pty xpr_debug

— six of the eight conditions the configuration declares are not in the table the manifest is built
against, so `bsd/net/bpf.c` (`optional bpfilter`), `bsd/net/ether_if_module.c` (`optional ether`) and
`bsd/net/if_loop.c` (`optional loop`) never enter the manifest. It is the same defect
`tools/xnu_config/device_table.py`'s own docstring already records for `monotonic`, and it is *not*
fixed here: it changes which sources are compiled, and its blast radius has to be measured as its own
step. What it means for this one is measurable instead — see the prediction.

## The prediction, written before the build and before the run

Measured host-side, from the 438 image's own artefacts, before anything was rebuilt:

| name | in `xnu_arm_entry_undef.txt` | in the pool (`nm`) |
| --- | --- | --- |
| `pseudo_inits` | yes — and it is the array | **no** → function stub today |
| `bpf_init` | no — nothing referenced it | **no** |
| `pty_init` | no | `T`, 0x16C |
| `ptmx_init` | no | `T`, 0x158 |
| `mdevinit` | no | `T`, 4 |
| `fsevents_init` | no | `T`, 0x224 |
| `random_init` | no | `T`, 0x8C |

So:

1. **`pseudo_inits` leaves the undefined list and `bpf_init` joins it, and the stub count does not
   move: 42 function stubs before, 42 after.** The composition changes and the number does not, which
   is the point — a count is not a kind either.
2. **The kind check passes, with the four recorded object-kind stand-ins and the unclassified count
   printed.** It fails today, on `pseudo_inits`, and that failure is the check's own positive test.
3. **The closure moves 424 → 425** (`bpf_init` is the one new edge).
4. `.data` grows by the array — 0x38 = 56 bytes, seven `{count, ps_func}` pairs — plus alignment, and
   the entry image's size changes accordingly.
5. **The run's stop moves out of the walk's termination test and one level deeper.** The walk is
   `pty_init(16)`, `ptmx_init(1)`, `mdevinit(1)`, `bpf_init(4)`, `fsevents_init(1)`, `random_init(1)`
   — and `bpf_init` is the only one of the six with no definition anywhere in the pool. So the
   cleanest prediction is **`stub_hit=bpf_init`**, with the caller key the loop's own return address.
   **The falsifier, and it is the more likely half:** `pty_init`, `ptmx_init` or `mdevinit` may reach
   a stub *inside* themselves first, in which case the stop is that stub with a caller key inside
   whichever of the three it is. Either way the stop is no longer `ifar = 0xE52DE004`, and the abort
   counters that 438's `data abort` populated stay zero.


## The measurement

**The check is its own positive test.** Against the 438 image's undefined list it fails — on exactly
one name, and on the right one:

    $ ./tools/check_stub_kinds.py
    stub kinds are not safe for 1 name(s):
      'pseudo_inits' is declared as an OBJECT (external/xnu-4570.1.46/bsd/dev/busvar.h:46) and this
      image has no definition of it, so the generator would emit a FUNCTION stand-in. ...

and against 439's list it passes, with the four recorded names and the twenty-one unclassified ones
counted and printed:

    ok: 42 undefined; 0 storage by nm, 3 defined by the pool; 39 with no pool definition - 14 declared
    in a header as functions, 4 declared as objects and all of them in KNOWN_KINDS
    (etherbroadcastaddr, lo_ifp, osrelease, ostype), 21 unclassified

**The object, before it was linked** — `pseudo_inits D 0x38`, six `R_ARM_ABS32` relocations in
configuration order, and `bpf_init` the only one of the six that the pool does not define:

    00000004 R_ARM_ABS32  pty_init      0000001c R_ARM_ABS32  bpf_init
    0000000c R_ARM_ABS32  ptmx_init     00000024 R_ARM_ABS32  fsevents_init
    00000014 R_ARM_ABS32  mdevinit      0000002c R_ARM_ABS32  random_init

**The array, in the linked image**, read out of `.data` at `0x804D3D4C` before the device ran:

    804d3d4c  10000000 00102980   {16, 0x80291000}  pty_init
    804d3d54  01000000 a8052980   {1,  0x802905a8}  ptmx_init
    804d3d5c  01000000 48952680   {1,  0x80269548}  mdevinit
    804d3d64  04000000 fc4b4480   {4,  0x80444bfc}  bpf_init    <- the stub, T 0x80444bfc size 0x18
    804d3d6c  01000000 a03b3e80   {1,  0x803e3ba0}  fsevents_init
    804d3d74  01000000 50a72680   {1,  0x8026a750}  random_init
    804d3d7c  00000000 00000000   {0, 0}           the terminator

**And on hardware:**

    stub_hit=bpf_init
    xnu_entry_stub_caller=0x8003b260
    xnu_entry_stub_caller_v=0x8003b260
    xnu_entry_stub_caller_w0=0x33303038   "8003"
    xnu_entry_stub_caller_w1=0x30363262   "b260"
    xnu_entry_kv_written=0x00000225
    xnu_entry_kv_dropped=0x00000000
    xnu_entry_abort_entries=0x00000000
    xnu_entry_checks=0x00000005   xnu_entry_failures=0x00000000
    No errors detected

**There is no `exception:` line and no `panic:` line, every `abort_first_*` word is zero, and the
prefetch abort is gone.**

### The caller key is the proof, and it is `bsd_init + 0x870` — not `bsd_autoconf`

`bsd_autoconf` exists as its own symbol at `0x8003B4D0` and 438 walked that copy. **The copy that
runs is inlined into `bsd_init`**, and the disassembly is what says so:

    8003b238 <bsd_init+0x848>   mov r0, #0
    8003b23c                    bl  _consume_kprintf_args      <- bsd_autoconf's first call
    8003b240                    bl  kminit                     <- and its second
    8003b244                    movw r4, #15692 ; 0x3d4c
    8003b248                    movt r4, #32845 ; 0x804d       <- r4 = 0x804D3D4C = &pseudo_inits
    8003b24c                    ldr r1, [r4, #4]               <- ps_func
    8003b250                    cmp r1, #0
    8003b254                    beq 8003b270
    8003b258                    ldr r0, [r4]                   <- ps_count
    8003b25c                    blx r1                         <- (*ps_func)(ps_count)
    8003b260                    ldr r1, [r4, #12]              <- THE MEASURED KEY = +0x870
    8003b264                    add r4, r4, #8
    8003b268                    cmp r1, #0
    8003b26c                    bne 8003b258
    8003b270                    bl  IOKitBSDInit               <- bsd_autoconf's tail call, inlined

Three things this one block measures at once. `r4` is loaded with **`0x804D3D4C`**, which is exactly
`&pseudo_inits` as `nm` reports it — so the code the device ran is reading the array *this* step
generated, not a stand-in. The `blx r1` is at `bsd_init+0x86C` and the key `0x8003B260` is its return
address to the byte. And the loop's shape (`ldr r1,[r4,#4]` for the termination test, `ldr r0,[r4]`
for the count, `add r4,#8`) is `mkioconf.c`'s layout, not one this project chose.

**The correction to record:** 438's doc says the `blx` is at `bsd_autoconf+0x28`, from
`xnu_entry_callwalk.py --root bsd_autoconf`. It is — in the standalone copy, whose address is
`0x8003B4F8`. The instruction that faulted is the same *sequence* at `bsd_init+0x86C`. **A symbol
existing is not evidence that it is the code that ran**, and the conclusion 438 drew survived only
because both copies have identical bodies.

### What the stop is worth: three pseudo-device inits ran on the device

`bpf_init` is entry **4** of the array, and a stub does not return — it stops the run. So the walk
reached entry 4, which means **`pty_init(16)`, `ptmx_init(1)` and `mdevinit(1)` all returned**. That
is 0x16C, 0x158 and 4 bytes of Apple's real BSD pseudo-device initialisation executed on this device,
past the point 438 died two instructions earlier — and `--root pty_init`, `--root ptmx_init` and
`--root mdevinit` each reach **no stub on the straight-line path**, which is the host-side half of
the same statement.

The four entries after it (`bpf_init`, `fsevents_init`, `random_init`, and the one already past) are
where the frontier now is, and `bpf_init` is the only one of the six with no definition anywhere in
the pool — `bsd/net/bpf.c` is `optional bpfilter`, and `bpfilter` is one of the six conditions the
configuration declares and `out/device_table.txt` omits. That is the next step's subject.

## Readings

**Prediction (1) and the shape of the thing it measures. Three counts did not move and the boot did:**

| | 438 | 439 |
| --- | --- | --- |
| undefined names | 42 | 42 |
| function stubs | 42 | 42 |
| storage stubs | 0 | 0 |
| objects added to the entry link | 424 | 424 |
| closure (objects on the boot path) | — | 1057 |

`pseudo_inits` left the undefined list and `bpf_init` joined it, and the stub count is the same 42
because entries of that list are **names** and the array needed one new one to replace the one it
retired. **A count is not a kind and a count is not a composition** — which is the whole reason 438's
stop was invisible to every number this project was watching.

**Miss, and the only one: I predicted "the closure moves 424 → 425 (`bpf_init` is the one new
edge)".** The measurement is **424 → 424**, and the reason is exact: **an unresolved symbol adds no
object.** `bpf_init` is a new *edge* in the reference graph and nothing more — the link resolves it
to a stand-in this image generates. I read a name count as an object count, which is
`mi4-measurement-defects`' own tell ("a tool that reports a total will be read as a component") one
step sideways: here a *set of names* was read as a *count of files*.

**`.data` grew by exactly 0x38 and `image bytes` by exactly 56 — prediction (4), exact:**

| | 438 | 439 | Δ |
| --- | --- | --- | --- |
| `.text` | `0x80000000` `0x4B4C00` | `0x80000000` `0x4B4BE0` | **−0x20** |
| `.data` | `0x804B8000` `0x2E360` | `0x804B8000` `0x2E398` | **+0x38** |
| `.sysctl_set` | `0x804E6360` `0xFD8` | `0x804E6398` `0xFD8` | +0x38, size unmoved |
| `.init_array` | `0x804E7338` `0x118` | `0x804E7370` `0x118` | +0x38, size unmoved, **no new constructor** |
| `bss` | `0x804E7480 .. 0x805388F8` | `0x804E74C0 .. 0x80538938` | both ends +0x40, size unmoved |
| image bytes | 5141584 | 5141640 | +56 |
| copied image ends | 48 B below `__bss_start` | 56 B below | — |
| headroom | 1865480 | 1865416 | −64 |

`.text` **shrank** by 0x20 while the image grew by 0x38, which is not what "removed a stub, added a
stub" suggests, so it was measured rather than argued. Both links were rebuilt here from the same
724-object list with only the stand-in object swapped (the reconstruction is faithful: it reproduces
438's `__entry_text_size = 4934656` and `__bss_start = 0x804E7480` and 439's `4934624` /
`0x804E74C0` exactly), and the two ELFs give the decomposition by symbol address:

    up to 0x80444BF4            Δ = 0        the whole image is byte-identical up to the swapped stub
    0x80444BFC  bpfkqfilter     Δ = +24      the swap's own 0x18 body
    0x80444E54  sysctl__…       Δ = 0        the bodies realign: 42 either way, same total
    0x804B2AC0  debug_enabled   Δ = −4       the merged string blob: ''pseudo_inits'' (−13) → ''bpf_init'' (+9)
    0x804B2AD0  memorystatus_init  Δ = −16   a second aligned fill
    0x804B4BE0  __entry_data_start Δ = −32    .text end

**−4 of content and −28 of aligned fill**, in two steps that land on 16-byte-aligned symbols: a
four-byte change in the merged string blob becomes a thirty-two-byte `.text` change. That is
`mi4-linker-fill-term` — `Δ.text = Σ(inputs) + Σ(aligned fills)` — from the *shrinking* side; 438 met
it growing (−0x24 of content → +0x40 of text). The stub bodies themselves account for nothing: all 42
are 0x18 bytes and they sit at identical addresses in both links (`bpf_attach` at `0x80444BB4`,
`_Z35upl_…` at `0x80444F8C`, span `0x3D8`).

**One new linker warning, and it is not new in kind.** `stage90_pseudo_inits.o uses 32-bit enums yet
the output is to use variable-size enums` — the same warning every one of the pool's objects emits,
because the platform block's bsd-rooted list is compiled with the bsd define set and that set does
not carry the pool's `-fshort-enums`. Recorded because it is the first warning a *generated* file has
produced, not because it says anything about this step.

**`kv_written` went 0x346 (838) → 0x225 (549), and the arithmetic is the stub's name.** 438 wrote
nine fault records (`fleh_prefabt`'s) that 439 does not, so the comparison is with 437's 0x226 (550):
**549 = 550 − 1**, and the stub name was `kmstartup` (9 bytes) and is now `bpf_init` (8). The
instrument's byte count moved by the length of the symbol it reports.

## Layout, hashes, safety

    payload      out/stage90/stage90-qcdt.img, 8161280 bytes (7970 KB), sha256
                 cdf58fb9704d6f399ed1d507d3696788ba7e88f271187271237c0716d6fabf02
    entry bin    5141640 bytes, sha256
                 d4f652f9bb89a073353014e433fa3219925cd5fa6ff154ff86d9b9d1648ea18f
    device       xnu_entry_image_bytes=0x004E7488, bss 0x804E74C0 .. 0x80538938 (0x51478)
                 xnu_entry_checks=5, xnu_entry_failures=0, checksum 0x90702935

`verify_sections` still reports exactly `.bss .data .init_array .sysctl_set .text`; the build's own
line reads *"the copied image ends at 0x804e7488, **56 bytes below** `__bss_start`, so the memset
touches nothing that was copied"*; and the entry-bin hash moved even though the size moved by 56
bytes, which is the same lesson 438 recorded from the other direction.

Non-persistent `fastboot boot` only, through both gated scripts (`run_and_capture.sh` re-runs
`preflight_boot_check.sh` and refuses on a gate failure); **nothing flashed, nothing written to
storage**. 25 × `persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`,
**`abort_entries=0`**, `checks=5` / `failures=0`, **no `exception:` and no `panic:`**, `kv_dropped=0`.
The only net armed across the jump was the hardware watchdog
(`hw_watchdog_counter_running=0x00000001`, `hw_watchdog_bite_truncated=0x00000000`), dead-man PPI
disarmed before it (`disarm_isenabler0 0x000C7FFF -> 0x00007FFF`). Payload 7970 KB accepted. Device
returned to Android on its own and was confirmed there (`ro.product.device` cancro,
`ro.build.version.release` 10). 302265 bytes / 3992 lines, ending `No errors detected`.

Per-run logs stay apart: `/tmp/run425_kmsg.txt` … `/tmp/run438_kmsg.txt`, `/tmp/run439_kmsg.txt`.

## Where the frontier is now

`bsd_init`'s statement list no longer contains a stub **that this project's forward walk can see**:

    $ ./tools/xnu_entry_callwalk.py --root bsd_init
    walk from bsd_init reached no stub on the straight-line path.
      indirect calls the walk could not follow:
        bsd_init+0x86c: blx r1        <- the pseudo_inits walk, and the device measured it
        bsd_init+0xad4: blx r0        <- the next unknown after the six entries finish

The first of those two indirect calls is the one this step made *readable*: it is no longer an
unknown, it is `pseudo_inits[3].ps_func` = `bpf_init`. The second is not yet anything. So the
frontier is entry 4 of 6 and then whatever `bsd_init+0xad4` calls.

And entry 4 is the symbol the two halves of this step's discovery meet on:

    bpf_init        bsd/net/bpf.c            `optional bpfilter`  NOT in the manifest (T is a stub)
    fsevents_init   bsd/vfs/vfs_fsevents.c   in the pool: T 0x803E3BA0, 0x224 bytes
    random_init     bsd/dev/random/randomdev.c  in the pool: T 0x8026A750, 0x8C bytes

(the three that already ran: `pty_init` `bsd/kern/tty_pty.c`, `ptmx_init` `bsd/kern/tty_ptmx.c`,
`mdevinit` `bsd/dev/memdev.c`)

and `bpf_init` is the symbol the two halves of this step's discovery meet on. **The
configuration declares eight devices — `bpfilter ether fsevents loop mdevdevice ptmx pty random` —
and `out/device_table.txt` holds four conditions, so `bpf.c`, `ether_if_module.c` and `if_loop.c`
never compile.** That is one defect with two faces, it is measured, and it is the next step's
subject: the condition table has to come from `config/MASTER` and the object manifest, not from a
hand-written list, and the blast radius of the change has to be measured as its own step.

**Still owed and unchanged: the timer** (`ml_init_timebase` plus an MSM8974 `tbd_ops_t` over the GPT
at `0xf9020000`, and 405's `IOCPUInterruptController`). Nothing between here and `vm_pageout` has
taken a deadline yet.
