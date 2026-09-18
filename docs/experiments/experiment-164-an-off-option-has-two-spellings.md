# Experiment 164 — an off option has two spellings, and the tree depends on both

Date: 2026-09-18
Host only — nothing here runs on the device.
Artifacts: `tools/gen_option_headers.py` (the whole of the change)

| `STAGE90_BOOT` | before | after |
| --- | --- | --- |
| C | 418 of 426 | **419 of 426** |
| C++, unchanged | 82 of 83 | 82 of 83 |
| objects in the measurement link | 500 compiled + 17 assembled | 501 + 17 |
| undefined, measurement link | 220 | **114** |
| boot-path stubs, from `arm_init` | 23 of 220 | **11 of 114** |
| — `stub_blockers.py`: a file that fails to compile | 14 | **2** |
| — : a file not in the manifest | 5 | 5 |
| — : no source in the tree | 4 | 4 |
| `.text` in the measurement image | 2810496 bytes | 2835728 bytes |
| `.data` / `.bss` | 125256 / 242168 | 125400 / 242680 |

| `RELEASE` | before | after |
| --- | --- | --- |
| C, C++, undefined, boot-path stubs | 612 of 615, 82 of 83, 63, 9 of 63 | **identical** |

106 symbols closed, 0 opened, all from one file: `osfmk/kern/task.c`. No line of XNU's source was
changed, no configuration option was turned on, and no compiler flag was added. The whole change is
one `#undef` line in a generated header.

## The largest single lever in the image was one `#ifdef`

`stub_blockers.py --min` on the image experiment-163 left says where the boot path stops:

```
  14  a file that fails to compile
       3  bsd_scale_setup   bsd/dev/unix_startup.c
       3  task_init         osfmk/kern/task.c
       4  init_task_ledgers osfmk/kern/task.c
       4  task_deallocate  osfmk/kern/task.c
       ... twelve of the fourteen rows are osfmk/kern/task.c
```

Twelve of the twenty-three boot-path stubs are one file, and that file fails to compile for **one**
error:

```
osfmk/kern/task.c:1598:1: error: conflicting types for 'task_collect_crash_info'
osfmk/kern/task.h:633:22: note: previous declaration is here
```

`STAGE90_BOOT` is the one configuration where `CONFIG_MACF` is off — the minimal configuration
leaves SECURITY out on purpose — and in that configuration the two spellings of the same option
disagree about the same argument:

```c
osfmk/kern/task.h:633          osfmk/kern/task.c:1598
extern kern_return_t            kern_return_t
task_collect_crash_info(        task_collect_crash_info(
        task_t task,                    task_t task,
#if CONFIG_MACF                        #ifdef CONFIG_MACF
        struct label *crash_label,              struct label *crash_label,
#endif                                 #endif
        int is_corpse_fork);            int is_corpse_fork)
```

Three things make this worth its own experiment rather than a footnote.

**It is latent upstream, and still there.** Apple's configurations all set `CONFIG_MACF=1`
(`MASTER.x86_64:54` names it in SECURITY), so `#if` and `#ifdef` agree and nobody has ever seen
this. Fetched from `apple-oss-distributions/xnu` at `main`, `osfmk/kern/task.c:2398` still reads
`#ifdef CONFIG_MACF` — the same mismatch, a decade later, while the *call site* two hundred lines
below it, `:2610`, reads `#if CONFIG_MACF`.

**`#define X 0` is not the same as "off", and the tree contains both readings.** `mkheaders.c:114-135`
writes `#define <MACRO> 0` for every option the configuration does not select — one line per option,
unconditionally, into a header `meta_features.h` then `#include`s. That is faithful and it is
reproduced faithfully here. But `#if X` reads an undefined `X` as 0 while `#ifdef X`, `#ifndef X`
and `defined(X)` do not, and a scan of the tree — every `.c`/`.h`/`.cpp`/`.s`/`.m` under
`osfmk`, `bsd`, `libkern`, `iokit`, `security`, `pexpert` and `stages` — finds **56** sites that test
one of these generated macros with `#ifdef`/`#ifndef`/`defined()`. Every one of them means "the
option is off", because that is what an author means by `#ifdef DIAGNOSTIC`. Apple never sees most
of them, for the reason above: Apple's configurations have the option on.

**With `CONFIG_MACF` off there is no value that satisfies both spellings**, which is why this cannot
be fixed with a flag. The prototype and the definition differ by one argument, so either the
prototype gains `crash_label` — and then the call site, `osfmk/kern/task.c:1814`, which is
`#if CONFIG_MACF`, passes one argument too few, so MACF would have to be genuinely on — or the
definition loses it. The second is what "undefined" means, and it has to hold in *every*
translation unit at once, because
`osfmk/kern/task.h:241` guards `struct task`'s own `crash_label` field with a third spelling of the
same option, `#ifdef CONFIG_MACF`. Undefined drops the field, the prototype and the argument
together, consistently. A per-file `-D` cannot: it would give one translation unit a
differently-shaped `struct task` than the rest of the kernel.

## What was changed

`tools/gen_option_headers.py` appends, to `meta_features.h` and only when the option is off:

```c
#undef CONFIG_MACF
```

after the `#include <config_macf.h>` lines. The per-option headers themselves are untouched — they
are still the one `#define <MACRO> <0|1>` line apiece that `mkheaders.c` writes, byte for byte — and
`meta_features.h` is the accumulate side, which is the file this project already writes rather than
receives. It changes nothing for `#if CONFIG_MACF` and everything for `#ifdef CONFIG_MACF`. `RELEASE`
never sees the line at all, because `CONFIG_MACF` is on there: the generator writes the block only
for macros that are in the list *and* off, and `diff -r` between the regenerated `RELEASE` headers and
the ones the 63-undefined image was built with is **empty**.

## The obvious generalisation is wrong, and the measurement says so

The rule that suggests itself — "an off option is undefined, full stop" — was implemented first and
measured, and it is a trap. `XNU_OPTION_OFF_UNDEF=all`, same configuration, same build:

| `STAGE90_BOOT`, off options undefined | |
| --- | --- |
| C | 419 of 426 — the same count |
| fixed, beyond `task.c` | `bsd/kern/subr_prof.c` (its two `#ifdef GPROF` sites) |
| **broken** | `bsd/vfs/vfs_syscalls.c` |

```
bsd/vfs/vfs_syscalls.c:776:27: error: use of undeclared identifier 'MNTK_TYPENAME_OVERRIDE'
```

`bsd/sys/mount_internal.h:243` guards the *definition* of `MNTK_TYPENAME_OVERRIDE` with
`#ifdef NFSCLIENT`, and `vfs_syscalls.c` uses it in six places with no guard at all. **Apple's tree
compiles with NFS off only because `#define NFSCLIENT 0` makes that `#ifdef` true.** So the tree
contains both dependencies — one declaration that needs an off option to be undefined, and one that
needs an off option to be *defined* — and no single rule serves both, in either direction. A count
that came out the same either way (419) hid a file swapped for a file; the file list is what showed
it.

That is also why the change is one macro and not a rule: `CONFIG_MACF` is named, with the site that
justifies it, and `XNU_OPTION_OFF_UNDEF` exists as the knob that reproduces both experiments.
`GPROF` is the next candidate on the same evidence — undefined, it compiles `bsd/kern/subr_prof.c`,
which has failed since the first build — and it is left out because it is off in `RELEASE` too, so it
would move four files there (`bsd_init.c`, `kern_clock.c`, `subr_xxx.c`, `subr_prof.c`) and that cost
has not been measured. A candidate measured in one configuration is not a candidate adopted.

## What it bought

`osfmk/kern/task.c`'s object defines **224** symbols; 106 of them were on the link's undefined list.
They are the task interface entire — `task_create`, `task_terminate`, `task_deallocate`,
`task_info`/`task_set_info`, `task_reference`/`task_release`, `current_task`, `kernel_task`,
`task_pid`, `pid_from_task`, `task_suspend`/`task_resume` and their `_internal` and `2` forms,
`task_wait`, `task_ledgers`, `task_policy`, the dyld-info and corpse interfaces, the four
`host_security_*_task_token` and `task_register_dyld_*` families — and the boot-path stubs among them
are twelve — every row of the fourteen-row "file that fails to compile" category except
`bsd_scale_setup` and `OSKextKextForAddress` in `libkern/OSKextLib.cpp`.

The boot-path stub count halves and, more usefully, the *shape* of what is left changes. Before:

```
    14  a file that fails to compile
       3  bsd_scale_setup           bsd/dev/unix_startup.c
       3  task_init                 osfmk/kern/task.c     <- twelve rows
    5   a file not in the manifest
    4   no source in the tree
```

After:

```
    5   a file not in the manifest      <- now the largest category
       7  MD5Init/MD5Update/MD5Final   libkern/crypto/corecrypto_md5.c
       7  __nosan_bzero                san/memintrinsics.h
       9  chudxnu_thread_get_callstack64_kperf  osfmk/chud/chud_xnu.h
    4   no source in the tree
       4  __firehose_buffer_create    (and flush/reserve at 5)
      10  mach_msg_destroy_from_kernel
    2   a file that fails to compile
       3  bsd_scale_setup             bsd/dev/unix_startup.c
       4  OSKextKextForAddress        libkern/OSKextLib.cpp
```

`bsd_scale_setup` at distance 3 is now the nearest thing a boot hits, and it is a compile failure in
`bsd/dev/unix_startup.c` whose error is a header reaching `bsd/netinet/in_pcb.h` without
`<netinet/in.h>` — a different class from this one. The firehose port, which experiment-162 and 163
both ranked first at distances 4 and 5, is now behind two smaller things.

## Verified, not assumed

- **The baseline is like-for-like, not remembered.** The pre-change image was rebuilt in this
  experiment with `XNU_OPTION_OFF_UNDEF=` (the empty list) into `out/xnu_min_obj_base`, and remeasured:
  C 418 of 426, undefined **220**, boot-path stubs **23 of 220** — the same three numbers
  experiment-163 recorded, from the same generator with one line less.
- **`RELEASE` is unchanged and provably so**: `diff -r` over the regenerated option headers is empty,
  so every `RELEASE` object is the object it was. Re-linked anyway: 63 undefined, 9 of 63 boot-path
  stubs, 612 of 615, 82 of 83.
- **The layer and the assembly are unchanged**: `build_xnu_arm_layer.sh` 32 of 32 and 445 undefined;
  17 assembled objects. Both read the option headers, and both read `RELEASE`'s, which are identical.
- **Both link routes agree**: 114 by `nm -u` on the merged relocatable link and 114 by
  `measure_link.sh`.
- **XNU's tree is clean**: `git -C external/xnu-4570.1.46 status --short` is empty before and after.

## What this does not settle

The other 55 `#ifdef` sites on generated option macros are now known and not acted on. Some of them
are certainly wrong in the same way (`bsd/kern/tty_pty.c:103`'s `#ifndef DEVFS`, `bsd/net/if_llatbl.c:843`'s
`#ifdef INET`) and some are load-bearing in the wrong direction
(`bsd/sys/mount_internal.h:243`'s `#ifdef NFSCLIENT`). Deciding each one is a measurement per
configured file, and the configurations that matter are the ones whose files are in the manifest.

Whether an `#undef` in `meta_features.h` is the *right place* is also open in one respect: a
translation unit that includes an option header **by name after** `meta_features.h` would re-define
the macro and undo it. Exactly one option is included by name anywhere in the tree —
`osfmk/arm/locore.s:63` and its three siblings include `<config_dtrace.h>` — and `CONFIG_DTRACE`'s
sites are all `#if`, so nothing turns on it. Apple's build has the identical ordering hazard for the
identical reason, which is why the generator's comment keeps the `#define` in place rather than
dropping the header.

## Reproduce

```bash
# the change
./tools/gen_option_headers.py                                        # RELEASE, no #undef written
XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  ./tools/gen_option_headers.py                                      # 1 of the 80 off options undefined

# the measurement
XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  MANIFEST=$PWD/out/xnu_arm_manifest_min.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_min_obj \
  ./tools/build_xnu_arm_kernel.sh                                    # C 419 of 426, C++ 82 of 83
./tools/measure_link.sh --min --keep-stubs                           # 114 undefined
./tools/stub_reach.py --min --from arm_init --list 40                # 11 of 114
./tools/stub_blockers.py --min                                       # 2 / 5 / 4

# the two configurations this change was chosen between
XNU_OPTION_OFF_UNDEF=all XNU_KERNEL_CONFIG=STAGE90_BOOT ... ./tools/gen_option_headers.py
XNU_OPTION_OFF_UNDEF=   XNU_KERNEL_CONFIG=STAGE90_BOOT ... ./tools/gen_option_headers.py

# the scan behind the 56
grep -rnE '^\s*#\s*(ifdef|ifndef)\s+CONFIG_MACF' external/xnu-4570.1.46/
curl -sSfL https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/osfmk/kern/task.c \
  | grep -n 'task_collect_crash_info' -A8 | head -30
```
