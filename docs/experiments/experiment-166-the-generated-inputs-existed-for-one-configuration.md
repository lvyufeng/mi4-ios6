# Experiment 166 — the generated inputs existed for one configuration only

Date: 2026-09-18
Host only — nothing here runs on the device.
Artifacts: `out/xnu_device/STAGE90_BOOT/` (generated, uncommitted), `tools/gen_device_headers.sh`
(docs), `tools/build_xnu_arm_kernel.sh` (a preflight that would have caught this)

Experiment-165 left six compile failures in `STAGE90_BOOT`, and four of them were one cause that is
not in XNU at all: `bsd/dev/arm/conf.c`, `bsd/kern/tty_ptmx.c` and `bsd/kern/tty_pty.c` include
`<pty.h>`, `bsd/kern/bsd_init.c` includes `<loop.h>`, and those headers are generated — for
`RELEASE` only.

| `STAGE90_BOOT` | before | after |
| --- | --- | --- |
| C | 420 of 426 | **423 of 426** |
| C++ | 83 of 83 | 83 of 83 |
| objects in the measurement link | 503 compiled + 17 assembled | 506 + 17 |
| undefined, measurement link | 102 | **94** |
| boot-path stubs, from `arm_init` | 9 of 102 | 9 of 94 (unchanged) |
| files that fail to compile | 6 | **3** |
| `.text` in the measurement image | 2840256 bytes | 2844704 bytes |
| `.data` / `.bss` | 125408 / 242744 | 125464 / 246200 |

`RELEASE` is unchanged, and measurably so rather than by argument: it already had the four headers,
the regenerated set is byte-identical, and a rebuild reports the same 612 of 615, 83 of 83, and the
same three failing files.

## A header that is missing is not an error, it is the host's header

The directory was there. `tools/gen_device_headers.sh` has honoured `XNU_KERNEL_CONFIG` since it was
written, and `tools/build_xnu_arm_kernel.sh` has read `out/xnu_device/$CONFIG` just as long. What was
missing was the *run* for the second configuration:

```
$ ls out/xnu_device/
RELEASE
$ ls out/xnu_device/RELEASE/
bpfilter.h  loop.h  ptmx.h  pty.h
```

The reason that produced six failures instead of an error message is the interesting part. With no
`pty.h` on the include path, clang does not stop — it searches its default system directories, and
because the target is an ELF triple whose driver sysroot is `/`, the host's glibc headers are among
them. So the file compiled the **host's** `pty.h`:

```
In file included from bsd/dev/arm/conf.c:111:
In file included from /usr/include/pty.h:22:
In file included from /usr/include/features.h:392:
/usr/include/features-time64.h:20:10: fatal error: 'bits/wordsize.h' file not found
```

The error names glibc, a header XNU never asked for, and nothing about the file that was absent. Any
of the four could have been read as a toolchain problem, and this is the second time in two
experiments that a missing generated input has presented as something else (the first: the option
headers of experiment-125, where one shared directory silently gave one build the other's values).

The generated headers are now present for both configurations and are **byte-identical**
(`diff -r out/xnu_device/RELEASE out/xnu_device/STAGE90_BOOT` is empty) — the values are chosen facts
about a device table, not functions of the option set, and that is exactly why the omission was
invisible while only `RELEASE` was being built.

## What the four files were worth

Ten symbols closed, two opened, and the arithmetic — 102 → 94 — is only visible by measuring both
states. Removing the three objects the compile fix added and re-running the link reproduces 102
exactly, so the before-image and the after-image differ by nothing else, and the two stub symbol
sets diff as:

```
  10 closed   bdevsw cdevsw cdevsw_flags chrtoblk chrtoblk_set
              isdisk nblkdev nchrdev ptsd_kqfilter ptsd_kqops
   2 opened   devfs_make_node_clone devfs_mutex
```

Eight of the ten are the BSD device-layer tables `bsd/dev/arm/conf.c` exists to define —
`bdevsw`/`cdevsw`, `nblkdev`/`nchrdev`, `chrtoblk`/`chrtoblk_set` — which is what experiment-132
predicted that file was for.

The boot path does not move (9 of 94 against 9 of 102): all four files sit behind `bsd_init`, which
still does not compile.

## The two that opened are the configuration disagreeing with itself

`devfs_make_node_clone` is defined in `bsd/miscfs/devfs/devfs_tree.c:1493`, a file that is
`optional devfs`; `tty_ptmx.c` is `optional ptmx` and calls it from `ptmx_init()` with no `#if DEVFS`
guard. Both halves are on in Apple's configurations; here, `ptmx` is on and

```
out/xnu_options/STAGE90_BOOT/devfs.h:1:#define DEVFS 0
out/xnu_options/RELEASE/devfs.h:1:      #define DEVFS 1
```

so the moment `tty_ptmx.c` started compiling it called into a component this configuration does not
build. **Recorded, not turned on.** `DEVFS 1` would pull three files and their symbol set into a
configuration built for a first boot, and the alternative — `NPTMX 0` — has not been measured (the
`NPTY 0` measurement of experiment-130 is about a different device and does not carry over). This is
the same shape as `NBPFILTER` and `GPROF`: a configuration decision with a price that has not been
taken, written down at the point where the price became visible.

## The durable part

`build_xnu_arm_kernel.sh` now refuses to start without this configuration's device headers, exactly
as it already refused without this configuration's option headers, and the reason is in the check
rather than in this file:

```bash
[[ -n $(compgen -G "$DEVICE_HEADERS/*.h") ]] || {
    echo "no device headers for $CONFIG at $DEVICE_HEADERS - run:" >&2
    echo "  XNU_KERNEL_CONFIG=$CONFIG ./tools/gen_device_headers.sh" >&2
    exit 2
}
```

Verified by removing the directory: exit 2, and a message naming the command. `gen_device_headers.sh`
said "there is exactly one header in it today" — true when it was written, four headers now — and now
records that it is per configuration and why the absence of one is silent.

**What this does not fix is the class.** The preflight catches *this* generator's output, because this
generator is known; a header that XNU includes and nothing generates still falls through to the
host's. The measurement to make next is whether the whole include path can be closed —
`-nostdinc` with clang's own builtin headers — which would turn every future instance of this into
`file not found`, at the price of every file that legitimately needs something from `/usr/include`.

## What is left

Both configurations now fail **three** files, and they agree on two of them:

| | `STAGE90_BOOT` | `RELEASE` |
| --- | --- | --- |
| `bsd/kern/subr_prof.c` | `error: unknown type name 'STATIC'` (160) — `GPROF` | same |
| `osfmk/kperf/kperfbsd.c` | `conflicting types for 'ffs'` — the per-component defines | same |
| one more | `bsd_init.c`: `bsd/netinet/mptcp_var.h:465: no member named 't_mptcb' in 'struct tcpcb'` (`MPTCP` off here), the experiment-164 shape again | `bsd/net/if_bridge.c`: `DLT_EN10MB` outside its `#if NBPFILTER > 0` guard |

`bsd_init.c` is on the boot path — `osfmk/kern/startup.c:629` calls it from `kernel_bootstrap` — and
its one remaining error is an option off in one configuration whose field is guarded by a different
spelling at the definition and the use, which is what experiment-164 fixed for `CONFIG_MACF`.

## Reproduce

```bash
# the change
XNU_KERNEL_CONFIG=STAGE90_BOOT ./tools/gen_device_headers.sh
diff -r out/xnu_device/RELEASE out/xnu_device/STAGE90_BOOT     # empty: not configuration-dependent yet

# the preflight
mv out/xnu_device/STAGE90_BOOT /tmp/ && ./tools/build_xnu_arm_kernel.sh ; mv /tmp/STAGE90_BOOT out/xnu_device/

# the measurement
XNU_KERNEL_CONFIG=STAGE90_BOOT XNU_MASTER_LOCAL=$PWD/tools/xnu_config/minimal/STAGE90_BOOT.local \
  MANIFEST=$PWD/out/xnu_arm_manifest_min.txt XNU_KERNEL_OBJ_OUT=$PWD/out/xnu_min_obj \
  ./tools/build_xnu_arm_kernel.sh          # C 423 of 426, C++ 83 of 83
./tools/measure_link.sh --min --keep-stubs # 94 undefined
./tools/stub_reach.py --min --from arm_init --list 40   # 9 of 94
./tools/stub_blockers.py --min                          # unchanged: 5 / 4

# the ten closed and two opened, from the two images rather than from a guess: remove the three
# objects the fix added, link, keep the stub file, restore, link again, diff
mv out/xnu_min_obj/bsd_dev_arm_conf.o out/xnu_min_obj/bsd_kern_tty_ptmx.o \
   out/xnu_min_obj/bsd_kern_tty_pty.o /tmp/keep3/
./tools/measure_link.sh --min --keep-stubs      # 102 - the before-image, reproduced
cp out/link/STAGE90_BOOT-stubs.s /tmp/before.s
mv /tmp/keep3/*.o out/xnu_min_obj/ && ./tools/measure_link.sh --min --keep-stubs   # 94
diff <(grep -o '^[A-Za-z_][A-Za-z0-9_]*:' /tmp/before.s | sort) \
     <(grep -o '^[A-Za-z_][A-Za-z0-9_]*:' out/link/STAGE90_BOOT-stubs.s | sort)

# the configuration disagreement behind the two opened symbols
grep -n DEVFS out/xnu_options/{STAGE90_BOOT,RELEASE}/devfs.h
grep -n 'devfs_make_node_clone' external/xnu-4570.1.46/bsd/kern/tty_ptmx.c
grep -n 'optional devfs' external/xnu-4570.1.46/bsd/conf/files
```
