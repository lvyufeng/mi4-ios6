#!/usr/bin/env bash
#
# Compile the kernel Apple's own manifest says an ARM RELEASE build is made of.
#
#   ./tools/build_xnu_arm_kernel.sh                 # compile everything in the manifest
#   ./tools/build_xnu_arm_kernel.sh --limit 50      # the first 50, for a quick look
#   ./tools/build_xnu_arm_kernel.sh --dir osfmk     # one component
#   ./tools/build_xnu_arm_kernel.sh --blockers 25   # the distinct things blocking the rest
#   ./tools/build_xnu_arm_kernel.sh --platform-only # only the three out-of-manifest blocks, whose
#                                                   # objects the pool's own `rm -f *.o` would eat
#
# The manifest is both halves of the kernel: `.c` compiled with `clang` and `.cpp` with `clang++`,
# through one pipeline and one per-component define table, reported separately. `.s` is still
# skipped — that is `xnu_arm_assemble.sh`'s job.
#
# Two environment variables that exist for controlled comparisons rather than for building:
#
#   XNU_KERNEL_EXTRA_DEFINES='-DFOO=1'   appended after this configuration's own defines, so a
#                                        "what if this flag were global" question is one command
#                                        against the real build instead of a hand-copied flag list.
#                                        That difference is not cosmetic: the hand-copied list is
#                                        how the C++ block was measured wrong — see experiment-154.
#   XNU_KERNEL_OBJ_OUT=/absolute/path    where the objects go. Must be absolute: this script `cd`s.
#
# This is the honest measurement. `build_xnu_arm_layer.sh` answers "does osfmk/arm compile" — all
# 32 files in the directory — and it does. But a kernel is not a directory: it is the files
# `list_sources.py RELEASE` selects from Apple's `*/conf/files` lists under Apple's own
# configuration. This compiles that set.
#
# Prerequisites, both reproducible:
#   ./tools/build_mig.sh                        -> out/mig/build/migcom
#   ./tools/gen_mach_headers.sh                 -> out/mach_headers/ (the MIG output)
#   ./tools/gen_option_headers.py               -> out/xnu_options/ (the OPTIONS/ macros)
#   ./tools/gen_libkern_version.sh              -> out/xnu_generated/libkern/version.h
#   ./tools/gen_bsd_headers.sh                  -> out/xnu_generated/bsd/sys/sysproto.h
#   ./tools/xnu_config/list_sources.py RELEASE --write out/xnu_arm_manifest.txt
#
# The generated roots come BEFORE every source tree, which is Apple's own order:
# `INCFLAGS = $(INCFLAGS_LOCAL) $(INCFLAGS_GEN) ...` with `INCFLAGS_LOCAL = -I.` (MakeInc.def:466-469)
# - the build directory, which is where MIG and makesyscalls put their output, ahead of the
# component's source tree. experiment-119 measured the opposite and concluded the generated root
# must go last; that was right for what it was testing and wrong as a general rule, because the
# generated root at the time held MIG output for *every* `.defs`, including `mach/notify.h`, which
# Apple never generates and which shadows the hand-written one. With the output set taken from
# Apple's Makefiles (xnu_config/mig_outputs.py), the order flips: 395 vs 384 in the minimal
# configuration. See experiment-124.
#
# What to expect: the ARM layer compiles and most of the kernel does not. The output that matters is
# `--blockers`: a per-file error count is not actionable, but the list of distinct missing names is,
# because each is either a header to supply, a value to choose, or a file to compile.

set -uo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
SHIMS=$REPO_ROOT/stages/stage90/shims
SHIMS_ARM=$REPO_ROOT/stages/stage90/shims_arm
MIG_HEADERS=${MIG_HEADERS:-$REPO_ROOT/out/mach_headers}
MIG_KSERVER=${MIG_KSERVER_OUT:-$REPO_ROOT/out/mach_headers/kserver}
OUT=${XNU_KERNEL_OBJ_OUT:-$REPO_ROOT/out/xnu_kernel_obj}
MANIFEST=${MANIFEST:-$REPO_ROOT/out/xnu_arm_manifest.txt}

# Apple's COMPONENT_LIST, `makedefs/MakeInc.def:46`. It is named once here because it is read in
# two places - the per-file import roots below, and the runtime block after the loop - and a second
# copy of it is how `-I$XNU/bsd` went missing for 68 libkern files once already. The error that
# produced was `sys/_types/_u_int.h not found`, from a force-include, naming neither the component
# nor the missing root.
COMPONENT_LIST=(osfmk bsd libkern iokit pexpert security san)

# The EABI runtime, which is not in the manifest and is not Apple's. `armv7-unknown-netbsd-eabi`
# (and `armv7-none-eabi` before it) lowers an aggregate copy to `__aeabi_memcpy4`, where a Darwin
# target lowers it to `memcpy` - so the ELF path needs four symbols Apple's tree never mentions.
# They are compiled here, with the loop's own flags, because a second flag list is this project's
# most repeated defect: the target triple used to be spelled out in four scripts before
# experiment-161. See stages/stage90/xnu_aeabi_runtime.c.
RUNTIME_SOURCES=("$REPO_ROOT/stages/stage90/xnu_aeabi_runtime.c")
RT_OUT=${XNU_RT_OBJ_OUT:-$REPO_ROOT/out/xnu_rt_obj}

# The configuration to build. `RELEASE` is Apple's full iOS kernel; `STAGE90_BOOT` is the minimal
# one declared in tools/xnu_config/minimal/STAGE90_BOOT.local, and its manifest is built by passing
# the same XNU_MASTER_LOCAL to list_sources.py. Default is the full one, because the full one is
# what "does XNU compile" means; the minimal one is what "can this boot" means.
CONFIG=${XNU_KERNEL_CONFIG:-RELEASE}
DEVICE_TABLE=${XNU_DEVICE_TABLE:-$REPO_ROOT/out/device_table.txt}

LIMIT=0
ONLY_DIR=""
SHOW_BLOCKERS=0
ONLY_PLATFORM=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --limit)    LIMIT=$2; shift 2 ;;
        --dir)      ONLY_DIR=$2; shift 2 ;;
        --blockers) SHOW_BLOCKERS=${2:-25}; shift 2 ;;
        # `--platform-only`: compile the four out-of-manifest blocks and nothing else.
        #
        # It exists so that the files in this build that are *this project's* - the platform expert
        # in `stages/stage90/xnu_platform/` and the pthread function table in
        # `stages/stage90/xnu_supply/`, whose every edit needs a compile to check - can be iterated
        # on without recompiling 698 Apple objects to reach them. Measured cost of the alternative: a
        # full run is minutes, and the first version of that file failed on one undeclared
        # identifier; measured cost of this mode, at the platform block alone: 0.9 seconds.
        #
        # Two things make it safe rather than a second build path:
        #   * the manifest is emptied, so the loop body cannot run - not skipped by a branch, but
        #     given nothing to read. Every counter stays 0 and the per-file machinery above is
        #     untouched.
        #   * the `rm -f "$OUT"/*.o` truncation is skipped, because the objects it would delete are
        #     the pool's, which the entry link needs and this mode is not going to rebuild.
        # It still checks the component table, the device table and the generated roots, so a
        # platform-only run is a real build of those four blocks under the same conditions.
        --platform-only) ONLY_PLATFORM=1; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
if [[ $ONLY_PLATFORM -eq 1 ]]; then
    # An empty regular file rather than `/dev/null`: the check above asks `[[ -f $MANIFEST ]]`, which
    # is false for a character device, and the first version of this mode stopped there.
    MANIFEST=$REPO_ROOT/out/.platform_only_manifest.txt
    : > "$MANIFEST"
fi

# **The manifest is generated from the table, not required** (experiment 440). It was a hand-run
# prerequisite and the table is written by this script, so the two could disagree — and they did: the
# first 440 build wrote a table turning `ether`, `loop` and `bpfilter` on, compiled the *previous*
# manifest that omits `bpf.c`, `bpf_if_loop.c`'s neighbours and four ethernet files, and reported
# `615 tried / 614 compile / 1 fail` — a clean success that had not built anything the step was
# about. A required input that a generated input determines has to be generated too, or the report is
# about two different configurations.
if [[ $ONLY_PLATFORM -eq 0 ]]; then
    LS_MESSAGE=$("$TOOLS_DIR/xnu_config/list_sources.py" "$CONFIG" --write "$MANIFEST" 2>&1) || {
        echo "$LS_MESSAGE" >&2
        exit 2
    }
    [[ ${VERBOSE:-0} -eq 0 ]] || printf '%s\n' "$LS_MESSAGE"
fi

[[ -f $MANIFEST ]] || {
    echo "no manifest at $MANIFEST - run:" >&2
    echo "  ./tools/xnu_config/list_sources.py RELEASE --write $MANIFEST" >&2
    exit 2
}
[[ -d $MIG_HEADERS/mach ]] || {
    echo "no generated mach headers - run tools/gen_mach_headers.sh first" >&2; exit 2; }

# The per-component flag table is an invariant of every count this script prints, so check it here
# rather than discovering a drift from a surprising number. It is a table transcribed out of XNU's
# own Makefile templates, and a transcription is exactly what drifts.
"$TOOLS_DIR/check_component_defines.py" >/dev/null || {
    echo "component_defines.sh no longer matches the Makefile templates - run:" >&2
    echo "  ./tools/check_component_defines.py" >&2
    exit 2
}

# And the same check for the conditions this script defines by hand: each must agree with
# xnu_config/device_table.py, which is what the manifest reads. They disagreed for several stages -
# `-DMONOTONIC=1` was defined here while the manifest excluded the only file implementing it - and
# nothing compared them, so it is compared here now.
# Check first, write second: the tool does the comparison and exits 1 on a disagreement, so writing
# before checking would overwrite the very file the check is about.
"$TOOLS_DIR/xnu_config/device_table.py" >/dev/null || {
    echo "the conditions this script defines disagree with xnu_config/device_table.py:" >&2
    "$TOOLS_DIR/xnu_config/device_table.py" >/dev/null
    exit 2
}
"$TOOLS_DIR/xnu_config/device_table.py" --write "$DEVICE_TABLE" >/dev/null

# And the **device** headers, generated here rather than required, for the reason `gen_pseudo_inits.py`
# is run here: the value has to belong to the configuration, and a stale one is not an error, it is a
# wrong answer. `mkheaders.c:85-101` writes `#define N<COND> <count>` from the configuration's own
# `d_slave`, and until experiment 440 four of these five headers were hand-written constants - with
# values that disagreed with both the table and the generated `pseudo_inits[]`. The generator rewrites
# the directory and removes headers for conditions the configuration no longer has.
XD_MESSAGE=$("$TOOLS_DIR/gen_device_headers.py" 2>&1) || {
    echo "$XD_MESSAGE" >&2
    exit 2
}
[[ ${VERBOSE:-0} -eq 0 ]] || printf '%s\n' "$XD_MESSAGE"

# And the third table, for the same reason and with a sharper failure mode: which options a
# translation unit sees is a property of its *component* (experiment-438), and the generator that
# writes the per-component `meta_features.h` files was, until then, making a claim in a comment
# instead of a check. Structural, so it fails in a second rather than after 680 files.
XO_MESSAGE=$("$TOOLS_DIR/check_option_headers.py" 2>&1) || {
    echo "$XO_MESSAGE" >&2
    exit 2
}

# And the pseudo-device table's array (experiment-439). `bsd_autoconf()` walks `pseudo_inits` and
# 438's run faulted fetching `0xE52DE004` - the ARM encoding of `push {lr}` - because that symbol had
# been stubbed as a *function* and the walk read the stand-in's own prologue as the first entry's
# `ps_func`. The array is generated rather than typed, from the same expanded configuration Apple's
# `mkioconf.c` reads, and generated **into this configuration's own directory**: RELEASE keeps
# `bpfilter` and `fsevents` and STAGE90_BOOT does not, so a shared path would let whichever
# generation ran last decide for both.
PI_MESSAGE=$(XNU_KERNEL_CONFIG=$CONFIG "$TOOLS_DIR/gen_pseudo_inits.py" --write 2>&1) || {
    echo "$PI_MESSAGE" >&2
    exit 2
}
# The path comes from the generator rather than being spelled again here: one value with two
# definitions is this project's most-repeated defect, and a `$REPO_ROOT/out/...` written in two
# places has nothing comparing them.
PSEUDO_INITS_SRC=$(XNU_KERNEL_CONFIG=$CONFIG "$TOOLS_DIR/gen_pseudo_inits.py" --print-path)

# And the check that compares the three halves of one device decision (experiment-440). "Is
# `bpfilter` on?" is answered by the manifest's table, by the generated `NBPFILTER` header, and by
# the `{4, bpf_init}` entry in the array above - and for several stages those three gave three
# different answers with nothing comparing them, which is how the boot came to stop on `bpf_init`:
# a symbol this image was simultaneously claiming, in a count taken from `config/MASTER`, and
# refusing to compile. Run here, after all three exist and before any file is compiled.
DC_MESSAGE=$("$TOOLS_DIR/check_device_conditions.py" 2>&1) || {
    echo "$DC_MESSAGE" >&2
    exit 2
}

mkdir -p "$OUT"
# Truncate every output. A build script that appends leaves the previous run's failures in the
# list, and a count read from it is then a count of two runs - which is how a 397-file result
# became 582 here before anyone noticed.
#
# The per-file logs need the same treatment, and did not get it until 2026-09-17: a file that fails
# leaves `$OUT/$key.log` behind and nothing removes it, so an `ls *.log` or a "first error per file"
# sweep silently mixes two runs - after the per-component fix, out/xnu_min_obj still held the
# baseline run's `ffs` logs and read as if nothing had changed. Delete them before recreating
# all.log, since this glob would otherwise match it.
rm -f "$OUT"/*.log
# And the objects, for the same reason and one level worse. A file that leaves the manifest - or
# moves, as the MIG `_server.c` files did when the two server-header variants were separated
# (experiment-145) - leaves its `.o` behind, and the link then reports **duplicate symbols for files
# the manifest no longer names**. That is how this was found: the measurement link failed on
# `multiple definition of iokit_server_routine` between `mach_headers/device_device_server.o` and
# `mach_headers/kserver_device_device_server.o`, one of which was stale.
#
# **Not in `--platform-only` mode**, where the manifest is empty and these objects are exactly the
# ones the entry link is holding on to: deleting them would leave the project with no pool until a
# full run, which is the run the mode exists to avoid.
[[ $ONLY_PLATFORM -eq 1 ]] || rm -f "$OUT"/*.o
: > "$OUT/all.log"
: > "$OUT/failed.txt"

# The force-include set and the values are build_xnu_arm_layer.sh's, which is where they were
# worked out and where each one's reason is recorded. Duplicated here rather than factored out
# because the two scripts want to stay independently runnable; if they drift, this comment is the
# pointer back.
#
# **`-include kern/queue.h` was in both lists and is gone from both.** `kern/queue.h:224-225` defines
# two *function-like* macros —
#
#     #define enqueue(queue,elt)  enqueue_tail(queue, elt)
#     #define dequeue(queue)      dequeue_head(queue)
#
# — so `iokit/IOKit/IODataQueue.h:131`'s `virtual Boolean enqueue(void *data, UInt32 dataSize);` was
# rewritten to `enqueue_tail(...)` on the way through the preprocessor, while the out-of-line
# definition at `IODataQueue.cpp:157` (which `#undef`s the macro at `:47`, too late) kept its own
# name. Two IOKit files failed with "out-of-line definition of 'enqueue' does not match any
# declaration", a message that names neither the macro nor the header. Removing the force-include
# leaves **every other object byte-identical** and takes C++ from 78 to 80 of 83. Apple force-includes
# nothing; see experiment-157.
FORCE_INCLUDES=(
    -include sys/_types/_u_int.h
    -include arm/simple_lock.h
    -include kern/ast.h
    -include mach/task_policy.h
    -include mach/thread_policy.h
    -include mi4ios6_build_config.h
    # `caddr_t`, for the same reason `u_int` is above: osfmk/vm/vm_compressor.c uses it and reaches
    # no header that defines it. Apple's build does reach one - the BSD `<sys/types.h>` chain - and
    # which one differs between the two builds enough that chasing it is guesswork; what is measured
    # is that the narrow header (it defines `caddr_t` and nothing else) takes RELEASE from 591 to 592
    # with no regressions, and vm_compressor.c is the largest single item on the boot path.
    -include sys/_types/_caddr_t.h
    # `u_char`, for the same reason and the same place: `osfmk/kern/btlog.c:641` casts to it and
    # reaches no header that defines it — the files that *do* reach `u_char` do so through
    # `osfmk/libsa/types.h`, via `<libsa/stdlib.h>`, which most of osfmk does not include.
    -include sys/_types/_u_char.h
    -include meta_features.h
)
# `-include stdatomic.h` used to be in that list, to get `enum memory_order` for the osfmk/arm
# atomics. It is NOT there any more, and the reason is a side effect nine files wide:
# EXTERNAL_HEADERS/stdatomic.h:38 includes <stddef.h>, which defines `ptrdiff_t`, and
# `libkern/zlib/zutil.h:193-194` is
#
#     #if KERNEL
#         typedef long ptrdiff_t;
#
# in a file Apple compiles into the ARM kernel - so in Apple's build `ptrdiff_t` is NOT defined at
# that point and Apple's kernel <string.h> does not pull stddef.h in. Force-including stdatomic.h
# put it into every translation unit and failed eight libkern/zlib files with "typedef redefinition
# with different types ('long' vs ... 'int')". Removing it is 381 -> 389 of 419 with no regressions.
# Nothing that needs the atomics header includes it by name, so this was invisible until the
# zlib failures were attributed to their actual source.

# The configuration's own options, expanded from MASTER via the doconf pipeline. These are the
# values Apple's build would have; the block below is only the flags that configure the *toolchain*
# and the two that resolve collisions the config cannot express.
CONFIG_DEFINES=()
while IFS= read -r d; do
    [[ -n $d ]] && CONFIG_DEFINES+=("$d")
done < <("$TOOLS_DIR/xnu_config/make_defines.sh" "$CONFIG")

DEFINES=(
    "${CONFIG_DEFINES[@]}"
    # MACH_KERNEL_PRIVATE is NOT here. It is per-component, and putting it here was this build's
    # single largest defect: see xnu_config/component_defines.sh for the table and the reason. In
    # short, MACH_KERNEL_PRIVATE is what reaches kern/misc_protos.h, whose ffs/fls/copyinstr
    # declarations collide with bsd/libkern/libkern.h's, so defining it globally broke every BSD
    # translation unit that includes <sys/systm.h> - 127 files in the minimal configuration.
    -DXNU_KERNEL_PRIVATE=1 -DKERNEL_PRIVATE=1
    -DMACH_BSD=1 -DPRIVATE=1 -DKPC=1 -DMONOTONIC=1 -DXPR_DEBUG=0 -DLOCK_PRIVATE=1
    -DARMA7=1 -DKERNEL=1 -D__arm__=1 -DCONFIG_EMBEDDED=1 -D__ARM_L2CACHE_SIZE_LOG__=21
    # `__ARM__`, and the case is the whole point. `bsd/kern/kern_sysctl.c:2772` is
    # `#if defined(__ARM__)`, and the build defined only the compiler's lowercase `__arm__` - so the
    # 64-bit `SYSCTL_QUAD` branch was taken for 32-bit values and the file failed with
    # `'_sysctl__vm_global_no_user_wire_amount_size_check' declared as an array with a negative
    # size`. One define; and the error named neither the macro nor the file that uses it.
    -D__ARM__=1
    # NPTY/NPTMX: the device conditions this configuration turns on, so that bsd/kern/tty_pty.c,
    # tty_ptmx.c and tty_dev.c are both compiled (device_table.py) and preprocessed consistently.
    # NPTY 1 rather than 0 because 0 does not compile: conf.c's #else branch is missing ptsselect
    # (experiment-130), and tty_pty.c promotes 1 to 32 itself with a #warning.
    -DNPTY=1 -DNPTMX=1
    # __APPLE__ is what Apple's compiler defines and this project's does not. The scripts here use
    # an ELF target triple (see xnu_config/arm_target.sh); Apple's build uses a Darwin target triple,
    # and `__APPLE__` is part of that triple rather than of the source. Supplying the macro is worth
    # 17 files in the minimal configuration and 99 in RELEASE, with no regressions in either:
    #
    #     osfmk/prng/YarrowCoreLib/include/yarrow.h:91   #if defined(macintosh) || defined(__APPLE__)
    #
    # is the clearest case - on that branch YARROWAPI is empty and `WindowsTypesForMac.h` is
    # included, which is where BYTE, UINT, LONGLONG and LPVOID come from. Off it, the vendored
    # Windows library takes its `__declspec(dllimport)` path and seven files die on it.
    #
    # Measured: `-D__MACH__=1` alongside it changes nothing (345 either way), and swapping the whole
    # target triple for `armv7-apple-darwin` is worth exactly one more file (346) while changing the
    # object format from ELF to Mach-O - which is a decision for the link step, not for this
    # measurement. See experiment-121. **And the type widths that triple carries ARE now available
    # without Mach-O** - that is what xnu_config/arm_target.sh is about, and it is worth three files
    # and 35 symbols. experiment-161.
    -D__APPLE__=1
    -DCONFIG_SCHED_TIMESHARE_CORE=1 -DCONFIG_SCHED_TRADITIONAL=1
    # `-D_CLOCK_T=1` used to be here, to force kern_types.h's `typedef struct clock *clock_t` to win
    # over bsd/sys/_types/_clock_t.h's `__darwin_clock_t`. It is gone, and the reason is the same
    # one experiment-118 was about: it was a workaround for MACH_KERNEL_PRIVATE being global. With
    # the per-component defines, kern_types.h's definition is only reachable from osfmk and iokit
    # files, bsd files get _clock_t.h's, and the two no longer meet. Removing it is 560 -> 564.
)

# Generated headers that are not MIG output: bsd/sys/sysproto.h comes from
# bsd/kern/makesyscalls.sh, and it is included by 55 of the failing files. Produced by
# tools/gen_bsd_headers.sh, and placed first so it wins over anything stale.
GENERATED=${XNU_GENERATED:-$REPO_ROOT/out/xnu_generated}
# Per configuration: see tools/gen_option_headers.py. RELEASE and STAGE90_BOOT disagree on 20 of
# these macros, so one shared directory silently gives whichever build was generated last its own
# values - which is what happened, and it surfaced as a duplicate-symbol error in the link.
OPTION_HEADERS=${XNU_OPTION_HEADERS_OUT:-$REPO_ROOT/out/xnu_options}/$CONFIG
LIBSA_EXPORT=${XNU_LIBSA_EXPORT:-$REPO_ROOT/out/xnu_libsa_export}
# The third generator: `device`/`pseudo-device` headers from config(8) - `loop.h`, `pty.h`,
# `ptmx.h`, `bpfilter.h`.
DEVICE_HEADERS=${XNU_DEVICE_HEADERS_OUT:-$REPO_ROOT/out/xnu_device}/$CONFIG
[[ -d $OPTION_HEADERS ]] || {
    echo "no option headers for $CONFIG at $OPTION_HEADERS - run:" >&2
    echo "  XNU_KERNEL_CONFIG=$CONFIG ./tools/gen_option_headers.py" >&2
    echo "  (the other configuration's directory must not be used: they disagree on 20 macros)" >&2
    exit 2
}
# ------------------------------------------------------------------------------------------------
# 438: which options a file sees is a property of its COMPONENT, and the boot's last stub was a
#      phantom of getting that wrong.
#
# 437's run stopped at `stub_hit=kmstartup`, caller key `0x8003B238` = `bsd_init + 0x848`. The walk
# says `bsd_init` has 232 calls and that is the only one still a stub, so the goal's bar was one
# function away - and that function turned out to be one no shipping configuration can define.
#
# `bsd_init.c:852` is `#ifdef GPROF` around `kmstartup();`, and `GPROF` is declared by **libkern**:
# `libkern/conf/files:5` is `OPTIONS/gprof optional gprof`, so `tools/gen_option_headers.py` wrote
# `gprof.h` with `#define GPROF 0` and the FLAT `meta_features.h` force-included it into **every**
# translation unit. `#define GPROF 0` is not "off" to `#ifdef`; it is *defined*. So a BSD file called
# a profiler entry point. Its only definer is `bsd/kern/subr_prof.c` - `standard` in
# `bsd/conf/files:430`, so always compiled - and that file cannot compile: between
# `#ifdef GPROF` (line 83) and `#endif /* GPROF */` (line 339) it uses the macro `STATIC` at line 160,
# which nothing it includes defines. The two facts are the same fact. The body must be dead in every
# build that ships, which means the call must be dead too - and it was not, because this build
# aggregated all eight components' options into one header.
#
# That is not a fudge this project invented: `mkheaders.c` writes each OPTIONS header into the object
# directory of the `conf/files` that declared it, and `MakeInc.def:466` puts **that** directory first
# (`INCFLAGS_LOCAL = -I.`, with `-I$(OBJROOT)/EXPORT_HDRS/$(COMPONENT)` beside it) - so a component
# sees its own options and no others. `scan_options()`'s comment asserted the membership did not
# matter because "the value is the same whichever component declared it", and the value is; the
# membership is not. See `mi4-generator-output-kinds` and `mi4-a-claim-in-a-comment-is-not-a-check`.
#
# **The membership rule alone is not enough, and the measurement is the reason.** Scanning every
# manifest source and all 1411 component headers for `#ifdef`/`#ifndef`/`defined()` reads of an
# option macro finds exactly TWO cross-component reads in the whole tree:
#
#     GPROF        declared by libkern, read by bsd/kern/{bsd_init,kern_clock,subr_prof,subr_xxx}.c
#                  and bsd/sys/gmon.h - the profiler, and every one of those blocks is DEAD when the
#                  option is off. Not shared: bsd simply stops seeing it, which is the only reading
#                  under which Apple's own subr_prof.c compiles. `nm` over all 695 objects finds no
#                  reference to `mcount`, `_gmonparam` or `cfreemem`; `kmstartup` is referenced only
#                  by the call this step removes.
#     CONFIG_MACF  declared by bsd and security, read by osfmk - `osfmk/kern/task.h:241` guards a
#                  `struct task` field with `#ifdef CONFIG_MACF` and `task.c` guards the same
#                  parameter with `#if CONFIG_MACF`. **Shared**: with the macro invisible to osfmk,
#                  `struct task` would have two layouts in one kernel, and the bsd and security code
#                  that allocates and reads it would use the other one. That is an ABI mismatch no
#                  compiler can see, so the shared list keeps the value it has always had.
#
# So the change is: `tools/gen_option_headers.py` writes `<component>/meta_features.h` as well as the
# flat one; this script puts the component's own directory ahead of the flat one for every manifest
# file; and `tools/check_option_headers.py` makes both halves structural - the slices against
# `conf/files`, and the reads against an explicit list (`SHARED` with a reason, or the inert ones
# with a reason; anything else stops the build). The four out-of-manifest translation units (the EABI
# runtime, firehose, the platform expert, the pthread and crypto tables) belong to no component and
# keep the flat header, which is what they were measured with.
#
# **Prediction, written before the build and before the run.**
#
#   * `tools/check_option_headers.py` passes for RELEASE and STAGE90_BOOT, and fails when a slice is
#     made to disagree (demonstrated while writing it: dropping `<gprof.h>` from libkern's slice
#     reported `libkern: missing gprof.h`; the include parser's first version reported five
#     components "missing" the header it had just written, because the shared entries carry a
#     trailing comment).
#   * pool: **695 objects -> 696, 3 failures -> 2.** `bsd_kern_subr_prof.o` is produced for the first
#     time; the two that remain are `bsd_net_if_bridge.c` and `osfmk_kperf/kperfbsd.c`, which this
#     step does not touch. `bsd/kern/subr_prof.c`'s body is inside `#ifdef GPROF`, so the object
#     defines nothing - `kmstartup`, `mcount`, `sysctl_doprof` and `_gmonparam` all vanish with it.
#   * `nm -u out/xnu_kernel_obj/bsd_kern_bsd_init.o` no longer lists `kmstartup`; `bsd_kern_subr_xxx.o`
#     loses `cfreemem`; nothing anywhere references any of them.
#   * **Every other object is byte-identical, and the reason is a property this step's whole design
#     rests on:** the only thing membership changes for a file is which option macros are *defined*.
#     `#if X` reads 0 either way, so only `#ifdef`/`#ifndef`/`defined()` can move - and those are the
#     two reads above, one inert and one held by the shared list. So the expected diff is exactly
#     four objects: `bsd_kern_bsd_init.o` (call gone), `bsd_kern_subr_xxx.o` (`cfreemem` gone),
#     `bsd_kern_subr_prof.o` (new), and `bsd_kern_kern_clock.o` (its `#ifdef GPROF` wraps an
#     `#include <sys/gmon.h>` and nothing else - identical is the prediction).
#   * entry image: the stub set **44 -> 43** (`kmstartup` is a name nothing references any more),
#     `.text` down by one stub body and one name slot - **0x24**, so 0x4B4AC0 -> 0x4B4A9C if no
#     boundary moves - and the layout rows shift by that and no more.
#   * the run: `bsd_init` runs past `+0x844`, and since 435 measured the rest of its statement list
#     real it **returns** - the first time this boot has left `bsd_init` - and the stop moves into
#     `kernel_bootstrap_thread`: `OSKextRemoveKextBootstrap`, `kdebug_free_early_buf`,
#     `serial_keyboard_init`, `vm_page_init_local_q`, `thread_bind`, `vm_pageout()`.
#
# Falsifiers, named in advance: (1) a stop that is still `kmstartup` - the narrowing did not reach
# `bsd_init.o`, which `nm -u` on the object decides in one line; (2) a new `data abort` naming a
# profiler-era symbol - the narrowing removed a definition something did need, and the symbol says
# which; (3) `bsd_init` still not returning, with the stop inside its own body - then "one stub left"
# was a statement about the pre-438 image and something else on that statement list depends on the
# GPROF path; (4) a `struct task` sized differently in two objects - what the shared list exists to
# prevent, and visible as a fault on a task pointer rather than as a compile error.
#
# ------------------------------- measured, 2026-09-19 --------------------------------------------
#
# **None of the four falsifiers fired, and two of the predictions were wrong in a way worth keeping.**
#
# The pool: **695 objects -> 696, 3 failures -> 2**, exactly as predicted, and `bsd_kern_subr_prof.o`
# is the new one. The byte comparison over all 695 before-hashes is the part that carries the design
# argument: **exactly two objects changed, `bsd_kern_bsd_init.o` and `bsd_kern_subr_xxx.o`, and the
# other 693 are byte-identical** - including `bsd_kern_kern_clock.o`, which the prediction called
# identical and which is the one whose `#ifdef GPROF` wraps an `#include <sys/gmon.h>` with nothing
# else in it. Two changed files out of 695 is what "membership only moves `#ifdef`" means when it is
# measured rather than argued.
#
# The two symbols the step was aiming at are both gone:
#
#     nm -u bsd_kern_bsd_init.o   U kmstartup   -> nothing
#     nm -u bsd_kern_subr_xxx.o   U cfreemem    -> nothing
#
# **Prediction miss (a), and it is the interesting one: the stub count went 44 -> 42, not 44 -> 43.**
# The prediction said the new `subr_prof.o` "defines nothing - `kmstartup`, `mcount`, `sysctl_doprof`
# and `_gmonparam` all vanish with it". That is wrong about the file, and the file says so: the
# preprocessor structure is `#ifdef GPROF` at 83, `#endif /* GPROF */` at 339, and `addupc_task` at
# line 370 is **outside it** (so are `PROFILE_LOCK`/`PROFILE_UNLOCK`/`PC_TO_INDEX` at 341-351).
# `subr_prof.c` is not the profiler; it is the profiler *plus* `addupc_task`, which `resourcevar.h:124`
# expands unguarded and `kern_clock.c:379` and `kern_sig.c:3346` call behind nothing but a runtime
# `P_PROFIL`/`P_OWEUPC` flag test. So `addupc_task` was a stub before this step - the closure needed it
# and nothing defined it - and the object that retires it is the one this step made compile:
#
#     nm out/stage90/xnu_arm_entry.elf   T addupc_task 0x8028bb7c
#
# Two stubs retired by one change, from two different directions: `kmstartup` because the caller
# stopped existing, `addupc_task` because the definer started existing. The reading to keep: **a file's
# name is not its `#ifdef` structure, and "an object defines nothing" is a claim about a line range
# that was never checked.** The '380 bytes' the new object carries is the measure of it.
#
# **Prediction miss (b): `.text` grew 0x4B4AC0 -> 0x4B4C00, +0x140, not -0x24.** Removing two stub
# bodies and their name slots is not what sets Δ`.text` - the new real `addupc_task` body more than
# pays for them - and the +0x40 then **changed nothing else at all**:
#
#     text size    4934656 (.text)      <- was 4934336: +0x140 = 0x4B4C00
#     image bytes  5141584              <- unmoved
#     .data        0x804B8000 (0x2E360) <- unmoved, address and size
#     .sysctl_set  0x804E6360 (0xFD8)   <- unmoved
#     .init_array  0x804E7338 (0x118)   <- unmoved
#     bss          0x804E7480 .. 0x805388F8 (332920)   <- unmoved
#     entry bin    sha256 2e7354519b9ecf81573be3729a068de3cf1afb0faa5df3dc9e41d9eb1d99250a
#
# The whole of the growth sat in the `.text` section's own alignment slack, so the image is the same
# 5141584 bytes with the same `.bss` boundary 48 bytes below its end - and the entry bin's hash still
# moved, which is the point: **equal size is not equal layout.** This is `mi4-linker-fill-term` from
# the other side: Δ`.text` was +0x40 and Δimage was 0.
#
# The closure went 423 -> 424 objects (`bsd_kern_subr_prof.o` joined it, since `addupc_task` is
# referenced from two files the closure already had). `copyin` and `copyout`, the new object's only
# unresolvable references, cost nothing because the image already defines both - `T copyin
# 0x8000d3e4`, `T copyout 0x8000d4cc` - and `proc_is64bit` and `stopprofclock` are in the pool. The
# 42 remaining stubs are the pre-437 set minus the two profiler entries, and the two *LLVM* profiler
# stubs are untouched by this step and stay (`__llvm_profile_get_size_for_buffer_internal`,
# `__llvm_profile_write_buffer_internal`) - a reminder that "the profiler" is two unrelated things in
# this tree and only one of them is GPROF.
#
# **Miss (c), the hardware run, and it is the one that names the next step.** The prediction said
# `bsd_init` would return and the stop would move into `kernel_bootstrap_thread`. It did not: the
# result line has **no `stub_hit=` field for the first time**, and a fault block 436 and 437 never
# printed:
#
#     real XNU entry: exception: prefetch abort
#     xnu_entry_prefetch_abort_ifar = 0xE52DE004     xnu_entry_prefetch_abort_ifsr = 0x00000005
#     xnu_entry_prefetch_abort_lr   = 0xE52DE008     xnu_entry_prefetch_abort_spsr = 0xA0000013
#     xnu_entry_prefetch_abort_ttbr0 = ttbr1 = 0x8070404A   ttbcr = 1   sctlr = 0x30C5787D
#     checks=5  failures=0  abort_entries=0  kv_dropped=0  kv_written = 0x226 -> 0x346
#
# `0x346 - 0x226 = 0x120 = 9 x 32`, exactly `fleh_prefabt`'s nine records and nothing else, so **no
# stub was hit on this run at all** - and the reason 437 printed *nothing* for those keys rather than
# zeros is that the handler writes them only when it runs.
#
# `0xE52DE004` is the ARM encoding of `push {lr}`, and `IFAR` and `LR_abt` are independent registers
# that agree on it, so the processor really was fetching from that address. It appears in the image
# as a code word in exactly 43 places - a search for the little-endian word, not a reading: 42 are
# the `push {lr}` of the 42 stub bodies (laid out in `xnu_arm_entry_undef.txt`'s order, 0x18 bytes
# apart, which is a second and independent measurement of the count) and the 43rd is the first
# instruction of `inv_shift_rows`. Every stub is
#
#     movw r0,#name  push {lr}  mov r1,lr  movt r0,#name  pop {lr}  b entry_stub_hit
#
# **and that is the whole stop.** `bsd_init.c:861` is `bsd_autoconf();`, inside `bsd_init` itself, and
# `bsd_autoconf` is `for (pi = pseudo_inits; pi->ps_func; pi++) (*pi->ps_func)(pi->ps_count);` over
# `struct pseudo_init { int ps_count; int (*ps_func)(int); }` - which `bsd/dev/busvar.h:46` declares
# **`extern struct pseudo_init pseudo_inits[]`**. The linked loop is `movw/movt r4,#0x80444e3c;
# ldr r1,[r4,#4]; beq exit; ldr r0,[r4]; blx r1; ldr r1,[r4,#12]; add r4,#8`, and at `0x80444e3c` -
# `pseudo_inits`, entry **28** of the alphabetical undefined list - the two words are `0xE3020928`
# (`movw r0, #0x2928`, read as `ps_count`) and **`0xE52DE004`** (`push {lr}`, read as `ps_func`).
#
# **`pseudo_inits` is supplied as a *function* stub where the header says the symbol is an *array of
# structs*.** The `0` that terminates the walk *is* the array's first `ps_func`, and this image put
# its own prologue there - so `pi->ps_func` is non-NULL, the `blx r1` at `bsd_autoconf+0x28` jumps to
# it, and the fault follows. `tools/xnu_entry_callwalk.py --root bsd_autoconf` says "reached no stub on
# the straight-line path ... indirect calls the walk could not follow: bsd_autoconf+0x28: blx r1" -
# **the walk's blindness and the device's fault are the same instruction.**
#
# **A third face of this instrument's symptom, and the first of its kind measured on hardware: the
# stop is not a missing symbol, not an invented zero and not a boot-arg string - it is a stand-in of
# the wrong *kind*.** The undefined list gives names and never kinds, so nothing host-side could have
# said the symbol was declared an array; the `nm` view (`T pseudo_inits`) is wrong only relative to
# `busvar.h:46`. The step that follows supplies it as the array, generated from the device table the
# way `mkioconf.c:79-100` generates it (one `{count, func}` per `PSEUDO_DEVICE` with a `d_init`,
# terminated by `{0,0}`), and the check that must stop the build is a kind check.
# ------------------------------------------------------------------------------------------------

# And the per-component slices, which the loop puts ahead of the flat header. Refused rather than
# fallen back on: the loop *does* fall back when a component has no slice, and for the one file in
# the manifest whose first path segment is not a component directory that is right. For the ones
# that are, a missing slice would silently hand the file the whole kernel's option set - which is
# the defect of experiment-438, whose effect was a boot that stopped on a symbol no shipping
# configuration can define. A fallback that reproduces the bug it was written to fix has to be a
# refusal, and this is the one place that can tell the two cases apart.
#
# The predicate is "declares options", read from the same `conf/files` the generator reads, rather
# than "is in COMPONENT_LIST" - which the first version of this check used and it fired on `san`,
# the one component with a `conf/files` and no `OPTIONS/` line. The measurement says the two agree
# about san and agree for a reason: its `conf/Makefile.template` is also the only one of the eight
# with no `-include meta_features.h`, so Apple's build gives san no option view either. A component
# that sees nothing has no slice to be missing from.
for _c in "${COMPONENT_LIST[@]}"; do
    grep -q '^OPTIONS/' "$XNU/$_c/conf/files" 2>/dev/null || continue
    [[ -f $OPTION_HEADERS/$_c/meta_features.h ]] && continue
    echo "no per-component option headers for $CONFIG at $OPTION_HEADERS/$_c/meta_features.h - run:" >&2
    echo "  XNU_KERNEL_CONFIG=$CONFIG ./tools/gen_option_headers.py" >&2
    exit 2
done
# Required for the same reason the option headers are, and it is worth spelling out why this check
# is not a formality: **a header that is missing from the include path is not an error here, it is
# the host's header**. `bsd/dev/arm/conf.c:111`'s `#include <pty.h>` with no `pty.h` anywhere on the
# path resolved to `/usr/include/pty.h` - the target is an ELF triple whose driver sysroot is `/`,
# so glibc's headers are reachable - and the file died in
# `features-time64.h:20: 'bits/wordsize.h' file not found`, naming neither XNU nor the header that
# was really absent. That is how a missing generated directory stayed invisible for as long as it
# did (experiment-166).
#
# **Experiment 440 turned this from a requirement into a check on an invariant.** The headers are
# written by `gen_device_headers.py` earlier in this script, so "does the directory exist" is no
# longer a question about the operator's last command; what can still be wrong is a *missing*
# directory (something deleted it between the two), which is what this catches.
[[ -n $(compgen -G "$DEVICE_HEADERS/*.h") ]] || {
    echo "no device headers for $CONFIG at $DEVICE_HEADERS - run:" >&2
    echo "  XNU_KERNEL_CONFIG=$CONFIG ./tools/gen_device_headers.py" >&2
    exit 2
}

# The component order is Apple's, from makedefs/MakeInc.def:46-48:
#
#   COMPONENT_LIST        = osfmk bsd libkern iokit pexpert libsa security san
#   COMPONENT_IMPORT_LIST = $(filter-out $(COMPONENT),$(COMPONENT_LIST))
#
# so a component sees its own tree first and then the others in that order. A single build cannot
# vary the order per file the way that mechanism does, so this uses the list's own order and the
# components' headers are mostly in disjoint namespaces (sys/, kern/, mach/, iokit/, pexpert/,
# security/, san/, libkern/), which keeps the approximation honest.
#
# `-I$XNU` is what makes <security/_label.h> and <san/kasan.h> resolve. Both exist in the tarball
# and two hand-written shims used to shadow them - see docs/experiments/experiment-117.
#
# osfmk/libsa is a real component in COMPONENT_LIST and its types.h defines uint_t, but its *source*
# directory must NOT go on the include path: it holds `string.h`, `stdlib.h` and a `sys/` subdir for
# the bootloader context, and putting it there cost 4 files (191 -> 187) by shadowing the real ones.
# Apple exports a *selected list* from each component (EXPORT_MI_LIST in each Makefile) into
# EXPORT_HDRS; exposing the whole directory is not the same thing, and this is the fourth time in
# this project that a broad include path has been the bug rather than the fix.
INCLUDES=(
    -I"$GENERATED/bsd" -I"$GENERATED"
    # The component's OWN option headers, ahead of the flat ones, so that `<meta_features.h>`
    # resolves to this component's slice of them. That is the whole of experiment-438: `libkern`'s
    # `conf/files` declares `gprof`, and a flat `meta_features.h` handed `#define GPROF 0` to a BSD
    # translation unit, where `#ifdef GPROF` at `bsd/kern/bsd_init.c:852` is *defined* - so the boot
    # called `kmstartup()`, which nothing in this image could define. Apple's build separates the two
    # because each component compiles from its own object directory (`MakeInc.def:466`,
    # `INCFLAGS_LOCAL = -I.`), which is what `tools/gen_option_headers.py` now reproduces.
    #
    # Per-file, like `COMP_FIRST_PLACEHOLDER` below, and for the same reason: which slice applies is
    # a property of the file being compiled. The four other readers of this list (the EABI runtime,
    # firehose, the platform block) resolve it to nothing and keep the flat header, which is the
    # behaviour they were measured with - they are this project's own files and belong to no
    # component.
    OPTION_FIRST_PLACEHOLDER
    -I"$OPTION_HEADERS"
    -I"$DEVICE_HEADERS"
    -I"$MIG_HEADERS"
    # `-I$XNU/osfmk` and `-I$XNU/bsd` are NOT here: they are inserted per file by `COMP_FIRST`,
    # because which of the two comes first is a property of the file being compiled and not of the
    # build. They go after the generated roots, never before - putting the component ahead of MIG's
    # output re-breaks the six `osfmk/vm` files experiment-124 fixed.
    COMP_FIRST_PLACEHOLDER
    -I"$XNU/iokit"
    -I"$XNU/libkern"
    -I"$XNU/pexpert"
    -I"$XNU"
    # The filtered `osfmk/libsa` types headers, before `bsd/arm` so that `<types.h>` resolves to the
    # kernel's own rather than to Darwin's machine types. See tools/gen_libsa_export.sh.
    -I"$LIBSA_EXPORT"
    -I"$XNU/osfmk/arm" -I"$XNU/bsd/arm"
    -I"$XNU/EXTERNAL_HEADERS"
    -I"$SHIMS" -I"$SHIMS/kern" -I"$SHIMS/mach"
    -I"$SHIMS_ARM" -I"$SHIMS_ARM/kern" -I"$SHIMS_ARM/mach"
    -I"$SHIMS_ARM/sys" -I"$SHIMS_ARM/sys/_pthread"
)

CC_ARGS=(
    clang --target=$("$TOOLS_DIR/xnu_config/arm_target.sh") -mcpu=cortex-a15 -marm
    -mfpu=neon-vfpv4 -mfloat-abi=softfp
    -ffreestanding -fno-builtin -fno-common -fno-pic -O2 -w -ferror-limit=0
)
# The C++ half of the kernel - libkern/libkern/c++ and iokit, 83 of the manifest's files - is the
# same compiler with `clang++` and the two flags Apple's own rules pass for a kernel build
# (MakeInc.def: `-fno-exceptions -fno-rtti`; there is no exception unwinder or RTTI in a kernel).
# Nothing else differs, which is the point: the per-component define table, the generated roots,
# the include order and the shim placement are the *same mechanism*, because a `.cpp` in `libkern`
# is a `libkern` translation unit like any other.
CXX_ARGS=(
    clang++ --target=$("$TOOLS_DIR/xnu_config/arm_target.sh") -mcpu=cortex-a15 -marm
    -mfpu=neon-vfpv4 -mfloat-abi=softfp
    -ffreestanding -fno-builtin -fno-common -fno-pic -O2 -w -ferror-limit=0
    -fno-exceptions -fno-rtti
)

PER_FILE_TIMEOUT=${PER_FILE_TIMEOUT:-60}
: > "$OUT/timedout.txt"

# Extra `-D` flags for a controlled comparison, appended after the configuration's own. This exists
# because the alternative is a hand-copied flag list, and a hand-copied flag list is how the C++
# block got measured wrong in the first place: the sweep that produced "all 83 fail on
# osfmk/kern/misc_protos.h:254" passed `-DMACH_KERNEL_PRIVATE=1` for every file, which is a
# *component* define (xnu_config/component_defines.sh) and one no libkern or iokit file gets. With
# this hook the same experiment is one command against the real build:
#
#   XNU_KERNEL_EXTRA_DEFINES=-DMACH_KERNEL_PRIVATE=1 ./tools/build_xnu_arm_kernel.sh --dir libkern
#
EXTRA_DEFINES=()
if [[ -n ${XNU_KERNEL_EXTRA_DEFINES:-} ]]; then
    # shellcheck disable=SC2206
    EXTRA_DEFINES=( ${XNU_KERNEL_EXTRA_DEFINES} )
fi

# Extra C++-only flags, for the same reason and with the same shape as the define hook above: a
# controlled comparison has to be one command against the real build, not a hand-copied flag list.
# Only `.cpp` files get these, so a measurement can change the C++ half and nothing else.
#
#   XNU_KERNEL_EXTRA_CXXFLAGS=-fapple-kext ./tools/build_xnu_arm_kernel.sh --dir libkern
#
EXTRA_CXX_FLAGS=()
if [[ -n ${XNU_KERNEL_EXTRA_CXXFLAGS:-} ]]; then
    # shellcheck disable=SC2206
    EXTRA_CXX_FLAGS=( ${XNU_KERNEL_EXTRA_CXXFLAGS} )
fi

# The per-component define set. Apple's build compiles each component with its own
# `<component>/conf/Makefile.template` CFLAGS rather than one global flag set, and the difference is
# not cosmetic - see xnu_config/component_defines.sh, which holds the table and the citations.
# `build_xnu_arm_layer.sh` does not need this: it compiles only osfmk/arm, which is the osfmk row.
#
# Build output has no component path to read, so it needs the same mapping stated explicitly. Apple
# builds each generated file in the component that declares it, and the three roots follow that:
#
#   out/mach_headers/...            from osfmk/*/*.defs        -> osfmk
#   out/xnu_generated/bsd/...       from bsd/kern/syscalls.master, and the sys/ headers -> bsd
#   out/xnu_generated/libkern/...   from libkern/libkern/Makefile:79  -> libkern
#
# Getting this wrong is not silent for long: `init_sysent.c` compiled as osfmk picked up
# `-DMACH_KERNEL_PRIVATE`, which reaches kern/misc_protos.h, and died on the `ffs`/`fls` collision
# that experiment-118 is about. Defaulting to osfmk was the first version of this function and it
# was wrong in exactly that way.
component_of() {
    local p=$1 rel
    case "$p" in
        "$GENERATED"/bsd/*)      printf 'bsd'    ; return ;;
        "$GENERATED"/libkern/*)  printf 'libkern'; return ;;
        "$MIG_HEADERS"/*)        printf 'osfmk'  ; return ;;
    esac
    rel=$(printf '%s' "${p#"$XNU"/}" | cut -d/ -f1)
    printf '%s' "${rel:-osfmk}"
}

ok=0
fail=0
absent=0
skipped=0
tried=0
timedout=0
# Counted separately, because "does XNU compile" is a question about the C and the C++ halves
# together and a single pooled number hides which half moved. The C++ half's denominator is 83 of
# the manifest's 698 - the same manifest, one build.
cpp_ok=0
cpp_fail=0

while read -r src; do
    if [[ ! -f $src ]]; then
        absent=$((absent + 1))
        continue
    fi
    # `.s` only. The assembler is `xnu_arm_assemble.sh`'s job (Darwin dialect, a translation step,
    # and the toolchain question experiment-150 closed), not this script's.
    #
    # `.cpp` used to be skipped here too, with a comment saying it "needs libkern's C++ runtime,
    # which is a separate and larger problem than this measures". That was true of the *link* and
    # false of the compile, and skipping it cost the project a wrong answer: the 83 C++ files were
    # measured by hand, in a loop whose flags were not this script's, and one of those flags
    # (`-DMACH_KERNEL_PRIVATE` globally) made all 83 report the same error - which experiment-152
    # then recorded as "the whole C++ block is behind one dead line". It is behind no such line.
    # See experiment-154. Attempting them here, with this script's own flags, is what makes the
    # count comparable to the C count and the mistake unrepeatable.
    case "$src" in
        *.s|*.S) skipped=$((skipped + 1)); continue ;;
    esac
    is_cpp=0
    case "$src" in *.cpp) is_cpp=1 ;; esac
    if [[ -n $ONLY_DIR && $src != *"/$ONLY_DIR/"* ]]; then
        continue
    fi

    tried=$((tried + 1))
    if [[ $LIMIT -gt 0 && $tried -gt $LIMIT ]]; then
        break
    fi

    # Disambiguate: the manifest has files of the same name in different components. Both
    # extensions are stripped so the object names of the 615 C files are exactly what they were
    # before this script learned about C++, and the collision that could hide in doing so - one
    # `X.c` and one `X.cpp` in the same directory, one object path, whichever compiled last wins,
    # silently - is checked for rather than assumed absent (see the key check after the loop).
    key=$(printf '%s' "$src" | sed "s|$XNU/||; s|/|_|g; s|\.c$||; s|\.cpp$||")
    # Apple's include order puts the file's OWN component first:
    #   INCFLAGS_GEN = -I$(SRCROOT)/$(COMPONENT) -I$(OBJROOT)/EXPORT_HDRS/$(COMPONENT)
    # (MakeInc.def:465), and a single flat include list cannot express that. It matters:
    # `osfmk/kern/ast.h` and `bsd/kern/ast.h` **guard themselves with the same `_KERN_AST_H_`**,
    # and `bsd/kern/kern_event.c:101` includes `<kern/ast.h>` for `AST_KEVENT_REDRIVE_THREADREQ`,
    # which only the BSD one defines. With osfmk first the guard is already taken, the BSD header is
    # skipped, and the error is "use of undeclared identifier" for a macro that is in the file the
    # line above asked for. The array goes BEFORE `INCLUDES` - `-I` order is left to right, and a
    # first version appended it, which made the component LAST and changed nothing.
    # The component roots go where the placeholder is - after the generated roots, before the other
    # The component list is Apple's, from `makedefs/MakeInc.def:46-48`, and the rule is exact:
    #
    #   COMPONENT_LIST        = osfmk bsd libkern iokit pexpert libsa security san
    #   COMPONENT_IMPORT_LIST = $(filter-out $(COMPONENT),$(COMPONENT_LIST))
    #
    # so a file sees its OWN component root first and then every other component in that fixed
    # order. Written as the list minus the file's own component rather than as a case for the two
    # components that happen to have `.cpp` files, because a case is how this was got wrong: adding
    # rows for `libkern` and `iokit` and forgetting `bsd` in them silently dropped
    # `-I$XNU/bsd` for 68 libkern files, and the error - `sys/_types/_u_int.h not found`, from a
    # force-include - named neither the component nor the missing root.
    #
    # `-I$XNU/libsa` is deliberately NOT in the list even though it is a component: its source
    # directory holds `string.h`, `stdlib.h` and a `sys/` for the bootloader context, and putting
    # it on the path costs 4 files by shadowing the real ones (tools/gen_libsa_export.sh exports
    # the three type headers Apple actually exports, and only those).
    SRC_COMPONENT=$(printf '%s' "${src#"$XNU"/}" | cut -d/ -f1)
    COMP_IMPORT=()
    for _c in "${COMPONENT_LIST[@]}"; do
        [[ $_c == "$SRC_COMPONENT" ]] && continue
        COMP_IMPORT+=(-I"$XNU/$_c")
    done
    COMP_ROOTS=(-I"$XNU/$SRC_COMPONENT" "${COMP_IMPORT[@]}")
    # And the same split for the MIG server headers, which Apple builds TWICE from one rule pair
    # (`osfmk/mach/Makefile:231` with MIGFLAGS, `:372` with MIGFLAGS+MIGKSFLAGS). The `simport`
    # lines are behind `#if KERNEL_SERVER`, so the two variants differ; EXPORT_MI_GEN_LIST exports
    # only the first. osfmk reads the build dir (`INCFLAGS_LOCAL`, `INCFLAGS_GEN`) and gets the
    # variant WITH them; every other component reads export roots only (`INCFLAGS_IMPORT`) and gets
    # the one without — which is what keeps a BSD file out of `osfmk/ipc/ipc_kmsg.h`
    # (experiment-145).
    KSERVER_FIRST=()
    [[ $SRC_COMPONENT == osfmk ]] && KSERVER_FIRST=(-I"$MIG_KSERVER")

    # `libkern/gen/OSAtomicOperations.c` needs one thing no other file does: `stdbool.h` NOT to have
    # defined `false`/`true` before its own `enum { false = 0, true = 1 }`. Measured: it is reached
    # because the force-include set drags `mach/vm_param.h` (-> `libkern/os/overflow.h` ->
    # `EXTERNAL_HEADERS/stdbool.h`) into translation units that never include it. A `stdbool.h` shim
    # that defines `bool` but not the two macros fixes this file and **breaks 78 others** - because
    # 78 files write `true`/`false` and need the macros - so it is applied to this file only, with
    # the same per-file mechanism the component roots use. Apple's build needs neither: it
    # force-includes nothing, so it does not reach `stdbool.h` here at all.
    ONE_FILE=()
    [[ ${src#"$XNU"/} == "libkern/gen/OSAtomicOperations.c" ]] &&
        ONE_FILE=(-I"$REPO_ROOT/tools/shims_stdbool")
    # `bsd/sys/kauth.h:113` uses `uid_t` and `gid_t`, and includes nothing that defines them:
    # `sys/types.h` — which does, through `_types/_uid_t.h` — arrives later in the same closure
    # (`kern_ktrace.c`'s trace puts types.h at line 128 and kauth.h at 107). Apple's build reaches
    # them some other way; the narrow answer that does not drag in the whole BSD type set is
    # `sys/types.h` for BSD files only, where `kern_types.h`'s competing `clock_t` is not reachable
    # (it is behind `MACH_KERNEL_PRIVATE`, which a BSD file does not define — experiment-118).
    BSD_FORCE=()
    [[ $SRC_COMPONENT == bsd ]] && BSD_FORCE=(-include sys/types.h)

    # `bsd/dev/unix_startup.c` includes `<netinet/tcp_var.h>`, which includes `<netinet/in_pcb.h>`,
    # and **in 4570 `in_pcb.h` is not self-contained**: it uses `struct in_addr`, `struct in6_addr`,
    # `struct route` and `struct sockaddr_in` at `:114,176,185,295` and includes `<netinet/in.h>`,
    # `<netinet6/in6.h>` and `<net/route.h>` nowhere. It does not have to be self-contained, because
    # Apple's configurations all set `IPSEC=1`, and `in_pcb.h:84`'s `#if IPSEC` then includes
    # `<netinet6/ipsec.h>` → `<net/if.h>` → `<net/if_var.h>` → **`<net/route.h>`** →
    # `<net/radix.h>` → `<net/if_llatbl.h>` → `<netinet/in.h>`. Every one of those four headers is
    # defined by a chain that starts at `net/route.h`. Newer xnu fixed it in the header rather than
    # in the configuration — `in_pcb.h` now opens with `#include <netinet/in.h>` and
    # `#include <sys/socketvar.h>` — which is the same fix, in the other file.
    #
    # So this is not a new dependency; it is the one Apple's build supplies, named directly. Measured
    # on `STAGE90_BOOT`, where `IPSEC` is off and nothing else in `unix_startup.c`'s closure reaches
    # it: one error, `netinet/in_pcb.h:114:17: field has incomplete type 'struct in_addr'`, and
    # `-include net/route.h` is the whole difference. Three other files in the same manifest also
    # reach `in_pcb.h` — `kern_malloc.c`, `sys_generic.c`, `audit_syscalls.c` — and all three compile
    # today because their own closure reaches `net/route.h` anyway (`kern_malloc.c` through
    # `<sys/kpi_mbuf.h>`), which is the control that says the header is the missing piece and not a
    # symptom.
    ROUTE_FORCE=()
    [[ ${src#"$XNU"/} == "bsd/dev/unix_startup.c" ]] && ROUTE_FORCE=(-include net/route.h)

    # `libkern/OSKextLib.cpp` and one declaration that disagrees with itself. `kext_request` is a
    # `friend` of `OSKext` (`OSKext.h:189`) and its definition sits inside the file's
    # `extern "C" {` block (`:38`), so clang reads the friend as C++ linkage and the definition as C
    # linkage. Two of the three errors that follow are not about linkage at all —
    # `'loadFromMkext' is a private member of 'OSKext'` and the same for `handleRequest` — because a
    # `friend` declaration grants access to *that function* and clang does not believe this is it.
    # One `extern "C"` declaration ahead of everything fixes all three, and it is the smallest
    # possible statement of what Apple's source already means; see the header's own comment.
    KEXT_FORCE=()
    [[ ${src#"$XNU"/} == "libkern/OSKextLib.cpp" ]] && KEXT_FORCE=(-include kext_request_c.h)

    # `clock_t`, per component, and this is the `-D_CLOCK_T` question arriving from the other side.
    # experiment-126 removed that flag because the per-component defines made both halves agree:
    # a BSD file gets `bsd/sys/_types/_clock_t.h`'s `__darwin_clock_t`, and `kern_types.h`'s
    # `struct clock *` is not reachable from it. **But `osfmk/kperf/kperfbsd.c` is an osfmk file
    # that includes `bsd/libkern/libkern.h` → `bsd/sys/types.h` → `_clock_t.h`**, so it sees both and
    # they conflict. The flag answers it in the direction that is now correct for osfmk files - it
    # takes `_clock_t.h`'s guard so `kern_types.h`'s definition is the one that survives - and it
    # is given to osfmk files only, which is the same restriction the BSD-only `sys/types.h` above
    # needs and for the same reason: which definition is right depends on the component.
    CLOCK_FORCE=()
    [[ $SRC_COMPONENT == osfmk ]] && CLOCK_FORCE=(-D_CLOCK_T=1)

    # The one file whose source cannot be compiled as written, and the switch that answers it.
    #
    # `osfmk/vm/vm_object.c:355` is `*object = vm_object_template;`, a whole-struct assignment, and
    # `osfmk/vm/vm_object.h:174` gives that struct a `const` member:
    #
    #     const unsigned int wired_page_count;
    #
    # C11 6.3.2.1p1: a structure is not a modifiable lvalue if any member is const-qualified, so the
    # assignment is a constraint violation. It is an **error**, not a warning, and it has no
    # `[-W...]` group, so there is nothing to suppress — `-w`, `-Wno-error`, `-fms-extensions`,
    # `-fms-compatibility` and `-fno-strict-aliasing` were each measured and each changes nothing,
    # and so does every `-std` from gnu89 to gnu17, with clang *or* arm-none-eabi-gcc. No define,
    # include order or target makes the construct legal. experiment-127 found exactly that and left
    # the file failing.
    #
    # What makes it worth answering anyway is the price, which experiment-158 measured: `vm_object.o`
    # defines **58 of the 189** symbols a whole-kernel link leaves undefined — the largest single
    # block, and its referrers are `vm_map.o`, `vm_pageout.o`, `memory_object.o` and `bsd_vm.o`. The
    # object this flag produces defines 152 symbols and exactly 58 of them are in that set.
    #
    # `-Dconst=` is a macro named `const` with an empty body, so every `const` in the translation
    # unit evaporates. It is cruder than the defect and it is deliberate: **it has to be defined
    # before the first token**, because the only other lever is the same trick scoped to
    # `vm/vm_object.h`, and that was measured to fail. A scoped `#define const` around the include
    # leaves the rest of the unit const-qualified, and a declaration reached inside the window then
    # disagrees with the same declaration reached outside it — `stages/stage90/shims/kern/debug.h:6`
    # and `osfmk/kern/debug.h:423` both declare `Debugger`, and reading one `const char *` and the
    # other `char *` is "conflicting types for 'Debugger'". A unit-wide macro cannot produce that
    # class of disagreement, because there is only one reading of every header.
    #
    # What it cannot do is change a struct's layout, a symbol's name or a calling convention, and in
    # C, removing a `const` can only make the compiler *more* conservative about assuming a value's
    # stability. The visible effect is that const objects defined by this unit land in `.data`
    # rather than `.rodata`, which a flat ELF link does not distinguish.
    #
    # Scoped to this file by name, and the scope is load-bearing rather than tidy: applied to the
    # whole `osfmk/vm` directory it *costs* two files. `osfmk/vm/lz4.c` and
    # `osfmk/vm/vm_compressor_algorithms.c` compile today and fail under it — `lz4.h:68`'s
    # `static const size_t lz4_encode_scratch_size = lz4_hash_table_size;` is an initializer that
    # needs `const` to be a constant expression, and dropping it turns the array in
    # `vm_compressor_algorithms.c:52` into a variable-length one. That is the one-value-two-
    # definitions shape from the other side, and it is why this is a per-file switch.
    #
    # Apple's own source accommodates the same `const` a hundred lines below, at vm_object.c:469-470
    # — `// vm_object_template.wired_page_count = 0;`, commented out with the note that static
    # storage is already zero. The whole-struct copy at :355 is what is left.
    CONST_RELAX=()
    [[ ${src#"$XNU"/} == "osfmk/vm/vm_object.c" ]] &&
        CONST_RELAX=(-Dconst=)

    FILE_INCLUDES=("${KSERVER_FIRST[@]}")
    for _inc in "${INCLUDES[@]}"; do
        if [[ $_inc == COMP_FIRST_PLACEHOLDER ]]; then
            FILE_INCLUDES+=("${COMP_ROOTS[@]}")
        elif [[ $_inc == OPTION_FIRST_PLACEHOLDER ]]; then
            # This component's slice of the option headers, or the flat set when this file's first
            # path segment is not a component directory. The fallback is not a nicety: a manifest
            # entry outside the eight component roots would otherwise get *no* option macro at all,
            # and `#define X 0` and "undefined" read the same under `#if` but not under `#ifdef`.
            if [[ -d $OPTION_HEADERS/$SRC_COMPONENT ]]; then
                FILE_INCLUDES+=(-I"$OPTION_HEADERS/$SRC_COMPONENT")
            else
                FILE_INCLUDES+=(-I"$OPTION_HEADERS")
            fi
        else
            FILE_INCLUDES+=("$_inc")
        fi
    done
    FILE_DEFINES=("${CLOCK_FORCE[@]}" "${CONST_RELAX[@]}")

    # shellcheck disable=SC2207
    COMP_DEFINES=( $("$TOOLS_DIR/xnu_config/component_defines.sh" "$(component_of "$src")") )
    # C++ goes through the same pipeline with the same flags, plus its own compiler and the two
    # rules flags. `CPP_FORCE` starts empty: the first measurement of the C++ block in this script
    # is the one with *no* C++-specific accommodation at all, so that whatever is added later has a
    # number to be worth. (The hand sweep this replaces force-included `sys/types.h` for every
    # `.cpp`; that is measured against this baseline below, not assumed.)
    CPP_FORCE=()
    CXX_EXTRA=()
    case "$src" in
        *.cpp) CXX_EXTRA=("${CXX_ARGS[@]}" "${EXTRA_CXX_FLAGS[@]}"); CPP_FORCE=("${CPP_FORCE[@]}") ;;
        *)     CXX_EXTRA=("${CC_ARGS[@]}") ;;
    esac
    # Bounded. A file that sends clang into a loop must cost seconds, not the whole session: one
    # did, for 45 minutes, because this had no timeout and its output was buffered behind a pipe.
    # A timeout is reported as its own outcome rather than as a compile failure, because "clang
    # hung" and "XNU does not compile" are different findings.
    if timeout "$PER_FILE_TIMEOUT" "${CXX_EXTRA[@]}" "${FORCE_INCLUDES[@]}" "${DEFINES[@]}" "${COMP_DEFINES[@]}" "${FILE_DEFINES[@]}" "${BSD_FORCE[@]}" "${ROUTE_FORCE[@]}" "${KEXT_FORCE[@]}" "${ONE_FILE[@]}" "${CPP_FORCE[@]}" "${EXTRA_DEFINES[@]}" "${FILE_INCLUDES[@]}" \
         -c "$src" -o "$OUT/$key.o" 2>"$OUT/$key.log"; then
        ok=$((ok + 1))
        [[ $is_cpp == 1 ]] && cpp_ok=$((cpp_ok + 1))
        rm -f "$OUT/$key.log"
    elif [[ $? -eq 124 ]]; then
        timedout=$((timedout + 1))
        printf '%s\n' "$src" >> "$OUT/timedout.txt"
        cat "$OUT/$key.log" >> "$OUT/all.log"
    else
        fail=$((fail + 1))
        [[ $is_cpp == 1 ]] && cpp_fail=$((cpp_fail + 1))
        cat "$OUT/$key.log" >> "$OUT/all.log"
        printf '%s\n' "$src" >> "$OUT/failed.txt"
    fi
done < "$MANIFEST"

# The EABI runtime. Outside the loop because it is not in the manifest and gets no component
# defines - it is this project's file, and the only thing it may read is what the loop's `INCLUDES`
# and `CC_ARGS` already give it. The `COMP_FIRST_PLACEHOLDER` in `INCLUDES` is per-file in the loop;
# for this one it is `osfmk`, the same default `component_of` uses, and `osfmk` is the right answer
# for a file that includes nothing from the tree.
mkdir -p "$RT_OUT"
RT_INCLUDES=()
for _inc in "${INCLUDES[@]}"; do
    if [[ $_inc == COMP_FIRST_PLACEHOLDER ]]; then
        RT_INCLUDES+=(-I"$XNU/osfmk")
        for _c in "${COMPONENT_LIST[@]}"; do
            [[ $_c == osfmk ]] && continue
            RT_INCLUDES+=(-I"$XNU/$_c")
        done
    elif [[ $_inc == OPTION_FIRST_PLACEHOLDER ]]; then
        : # the flat option headers, which the next entry in the list already gives it (experiment-438)
    else
        RT_INCLUDES+=("$_inc")
    fi
done
rt_fail=0
for _src in "${RUNTIME_SOURCES[@]}"; do
    _o="$RT_OUT/$(basename "${_src%.c}").o"
    if "${CC_ARGS[@]}" "${FORCE_INCLUDES[@]}" "${DEFINES[@]}" "${RT_INCLUDES[@]}" \
           -c "$_src" -o "$_o" 2>"$RT_OUT/$(basename "${_src%.c}").log"; then
        rm -f "$RT_OUT/$(basename "${_src%.c}").log"
    else
        echo "runtime: $(basename "$_src") FAILED - $RT_OUT/$(basename "${_src%.c}").log" >&2
        rt_fail=$((rt_fail + 1))
    fi
done
[[ $rt_fail -eq 0 ]] || exit 4

# The firehose, which is not in the manifest and is not in the tarball at all. `osfmk/kern/firehose.c`
# does not exist: Apple ships the implementation as a closed kernel library (`libkern/firehose/` in the
# tarball has `KERNELFILES =` empty), and `bsd/kern/subr_log.c:874` calls `__firehose_buffer_create`
# anyway - so experiment 255 stopped there. The source is libdispatch's `src/firehose/firehose_buffer.c`,
# Apache-2.0, unmodified; see `stages/stage90/firehose/README.md` for its provenance. It is compiled
# here, with the loop's own flags, for the same reason `RUNTIME_SOURCES` is: a second flag list is this
# project's most repeated defect, and this file reads the same kernel headers the loop's files do.
# `portinc/` is not a shim - it is the newer tree's `libkern/os/` atomics surface, which this 10.13-era
# tree does not ship, placed where `<os/...>` resolves (experiment 256).
FIREHOSE_SOURCES=("$REPO_ROOT/stages/stage90/firehose/firehose_buffer.c" \
                  "$REPO_ROOT/stages/stage90/firehose/firehose_kernel_config.c")
FH_OUT=${XNU_FIREHOSE_OBJ_OUT:-$REPO_ROOT/out/xnu_firehose_obj}
FIREHOSE_INCLUDES=(-I"$REPO_ROOT/stages/stage90/firehose/portinc" -I"$REPO_ROOT/stages/stage90/firehose" -I"$XNU/libkern/firehose")
mkdir -p "$FH_OUT"
fh_fail=0
for _src in "${FIREHOSE_SOURCES[@]}"; do
    _o="$FH_OUT/$(basename "${_src%.c}").o"
    # The port's include roots go FIRST, ahead of the tree's: `portinc/` carries a newer tree's
    # `os/base.h` (which has `OS_OPTIONS`) and newer `firehose_types_private.h`, and the 4570 tree's
    # `libkern/os/base.h` would shadow them from further down the list.
    if "${CC_ARGS[@]}" "${FORCE_INCLUDES[@]}" "${DEFINES[@]}" "${FIREHOSE_INCLUDES[@]}" \
           "${RT_INCLUDES[@]}" \
           -c "$_src" -o "$_o" 2>"$FH_OUT/$(basename "${_src%.c}").log"; then
        rm -f "$FH_OUT/$(basename "${_src%.c}").log"
    else
        echo "firehose: $(basename "$_src") FAILED - $FH_OUT/$(basename "${_src%.c}").log" >&2
        fh_fail=$((fh_fail + 1))
    fi
done
[[ $fh_fail -eq 0 ]] || exit 5

# The platform expert, which is not in the manifest because no configuration of Apple's has it. The
# open-source tree contains no concrete platform expert at all (experiment 362 measured the
# consequence: `IOStartIOKit.cpp:155`'s `IOPlatformExpertDevice` matches the kernel catalogue's one
# personality, `IOPanicPlatform`, and Apple designed that one to panic), so this stage authors the
# missing class - a concrete `IODTPlatformExpert` subclass whose whole content is its metaclass and
# the two pure virtuals the base leaves - and compiles it here.
#
# Here, and with the loop's own flags, for the reason `RUNTIME_SOURCES` and `FIREHOSE_SOURCES` are
# both compiled here rather than in a stage script: a second flag list is this project's most
# repeated defect, and this file is a C++ IOKit translation unit that must be ABI-identical to the
# 83 the loop compiles. Two things are stated rather than derived, and both are stated because
# `component_of` cannot answer them for a file outside the tree:
#
#   * the component is **iokit**, so the per-component defines are iokit's (`component_defines.sh`)
#     and the import roots are COMPONENT_LIST minus iokit, in Apple's order - which is exactly what
#     the loop's `COMP_ROOTS` computes for `$XNU/iokit/Kernel/IOPlatformExpert.cpp`, the file this
#     one subclasses.
#   * `-fapple-kext` is NOT passed, and must not be: the loop's C++ flags do not have it (it was
#     only ever *measured* through `XNU_KERNEL_EXTRA_CXXFLAGS`, experiment-330), so the kernel's
#     `OSObject` vtable is emitted by the class's own key translation unit rather than as a
#     kernel-kext weak definition, and an object built with the flag would disagree with the 83
#     about which vtable that is.
#
# And beside it, one C file: the kernel's personality table with this machine's platform expert in
# front of Apple's fallback (`gIOKernelConfigTables`, `iokit/KernelConfigTables.cpp:35`), which the
# same step replaces one object for one object. It is compiled with `CC_ARGS` - clang - and the
# compiler is load-bearing rather than incidental: clang puts the table string in `.rodata.str1.1`,
# which is where the stock object's table text lives, while gcc puts an identical source in
# `.rodata`, a different input section placed at a different point of the `.text` output section's
# `.rodata` run. It includes nothing, so the component defines and the force-include set cannot
# affect it; the compiler, the target triple and `-O2` are the whole of its configuration.
#
# **457's second class is the second entry of `PLATFORM_SOURCES`, and it is the same kind of thing for
# a different reason.** `MSM8974RootResource.cpp` is an `IOService` subclass whose personality
# (`IOProviderClass = IOResources`) is what makes `gIOCatalogue->findDrivers(gIOResources)` non-empty -
# so it is an iokit translation unit with an `OSMetaClass` and a vtable exactly as the platform expert
# is, compiled by the loop below with the same flags, into the same directory. It carries one extra
# requirement the platform expert does not: its `start` calls the entry image's `entry_live_write`,
# declared `extern "C"` rather than included, so `out/xnu_arm_boot/build_entry.sh` is what must link it
# (the object is not in the manifest, so nothing else ever will).
PLATFORM_SOURCES=("$REPO_ROOT/stages/stage90/xnu_platform/MSM8974PlatformExpert.cpp"
                  "$REPO_ROOT/stages/stage90/xnu_platform/MSM8974RootResource.cpp")
PLATFORM_C_SOURCES=("$REPO_ROOT/stages/stage90/xnu_platform/stage90_platform_config_tables.c")
PL_OUT=${XNU_PLATFORM_OBJ_OUT:-$REPO_ROOT/out/xnu_platform_obj}
PL_ROOTS=(-I"$XNU/iokit")
for _c in "${COMPONENT_LIST[@]}"; do
    [[ $_c == iokit ]] && continue
    PL_ROOTS+=(-I"$XNU/$_c")
done
PL_INCLUDES=()
for _inc in "${INCLUDES[@]}"; do
    if [[ $_inc == COMP_FIRST_PLACEHOLDER ]]; then
        PL_INCLUDES+=("${PL_ROOTS[@]}")
    elif [[ $_inc == OPTION_FIRST_PLACEHOLDER ]]; then
        : # the flat option headers, which the next entry in the list already gives it (experiment-438)
    else
        PL_INCLUDES+=("$_inc")
    fi
done
# shellcheck disable=SC2207
PL_COMP_DEFINES=( $("$TOOLS_DIR/xnu_config/component_defines.sh" iokit) )

# **And a third list (432): the one stage source in this block that includes an XNU header.**
#
# `stages/stage90/xnu_supply/stage90_pthread_functions.c` supplies `struct pthread_functions_s` -
# the table `bsd/kern/pthread_shims.c:275` guards on, whose only writer in Apple's tree is
# `pthread.kext`, a binary that is not in the tarball and that no object in the pool stands in for.
# 431's run is what named it: the boot reached `bsd_init + 0x7F4` and panicked with
# "pthread kernel extension not loaded (function table is NULL)."
#
# It is here, rather than compiled by `build_entry.sh`'s own toolchain, for one reason: **the file
# includes `<sys/pthread_shims.h>`**, and the whole value of that is that the table's layout is the
# layout the kernel was compiled against rather than a copy of it. A second compiler, a second
# include order or a second define set is a second definition of the struct's layout - the defect
# class this project has a memory about - and it would not fail loudly: it would call a function
# through the wrong offset.
#
# So the flags are the loop's own, with two things named rather than derived, for the same reason
# the platform expert's block names them (`component_of` cannot answer them for a file outside the
# tree):
#
#   * the component is **bsd**, because `<sys/pthread_shims.h>` is a BSD header and the file that
#     reads the table (`bsd/kern/pthread_shims.c`) is compiled under `bsd/conf/Makefile.template`'s
#     defines. The import roots are therefore COMPONENT_LIST with `bsd` in front and dropped from
#     the tail, which is what the loop's `COMP_ROOTS` computes for that file.
#   * **the force-includes stay exactly as they are** - the same `FORCE_INCLUDES` the loop's 698
#     files get, plus `-w`. Nothing extra is added for this file, and that is measured rather than
#     assumed: with `eventvar.h` included first inside the source (see its header comment for the
#     circular include that makes the order load-bearing) it compiles under the plain set, under
#     `-include sys/types.h` and under those two plus `sys/kernel_types.h` alike.
# **And a fourth file in that list (437): the crypto dispatch table.**
#
# `stages/stage90/xnu_supply/stage90_crypto_functions.c` supplies `g_crypto_funcs` - the table
# `aes_encrypt_key128` reads through, whose only writer `register_crypto_functions()` is called by
# nothing in this tree. 436's whole-kernel run is what named it: `tcp_init`'s inlined `tcp_tfo_init`
# called `aes_encrypt_key128` unconditionally, and it faulted on `g_crypto_funcs` being NULL.
#
# It is in this block for the same reason the pthread table is, and the reason is the *structs*:
# `crypto_functions_t` and the six descriptor types it is made of come from
# `<libkern/crypto/register_crypto.h>` and `<corecrypto/ccmode.h>`, so the table's member offsets and
# each descriptor's method offsets are the kernel's own rather than a copy. A mirror of any of these
# layouts would not fail loudly - it would call a method through the wrong offset.
#
# The component is **bsd**, the same one the pthread table and `bsd/netinet/flow_divert.c` get: that
# file includes `libkern/crypto/crypto_internal.h` and reads this very table, so the BSD define set is
# *measured* to be sufficient for these headers rather than assumed - and the include roots are
# COMPONENT_LIST with the component in front and dropped from the tail, which reaches
# `$XNU/libkern/libkern/crypto/register_crypto.h` through `-I$XNU/libkern`.
#
# Unlike the pthread table it needs no special treatment of the force-include set: it includes only
# libkern and corecrypto headers, none of which reach a BSD header that `eventvar.h`'s circular
# include is about. The AES implementation it points at is *not* here - `stage90_aes.c` includes
# nothing from the tree at all, so `build_entry.sh` compiles it, for the reason written there: the
# same translation unit has to be runnable on the host for its known-answer tests.
# **And a fifth file in that list (439), which is generated rather than written.**
#
# `pseudo_inits` is the array `bsd_autoconf()` walks (`bsd/kern/bsd_init.c:1083`), and 438's run is
# what named it: a prefetch abort fetching `0xE52DE004` - the ARM encoding of `push {lr}`, which is
# the first word inside every one of that image's 42 stub bodies. The symbol had been stubbed as a
# *function*, and `bsd/dev/busvar.h:46` declares it `extern struct pseudo_init pseudo_inits[]`, so
# the walk's first `ps_func` was the stand-in's own prologue word and `blx r1` jumped to it.
#
# Apple generates this array too - `SETUP/config/mkioconf.c:79-100` writes one `{count, func}` per
# `pseudo-device` line with an `init` word, in the configuration's order, terminated by `{0, 0}` -
# and `tools/gen_pseudo_inits.py` reproduces that derivation from `tools/xnu_config/expand.sh`, which
# is this project's own reproduction of doconf's `<feature>` filtering. So the content is measured
# rather than typed, and it is **per configuration**: RELEASE keeps `bpfilter` and `fsevents`,
# STAGE90_BOOT does not, which is why the generated file lives in its own configuration's directory.
#
# It is in *this* block for the same reason the pthread and crypto tables are: the file includes
# `<dev/busvar.h>`, so `struct pseudo_init`'s layout is the kernel's own rather than a copy of it -
# `-DDRIVER_PRIVATE=1` is in the bsd define set and is what exposes it. The component is **bsd**
# because that is the header's component and because `bsd_autoconf` is the reader.
PLATFORM_BSD_SOURCES=("$REPO_ROOT/stages/stage90/xnu_supply/stage90_pthread_functions.c"
                      "$REPO_ROOT/stages/stage90/xnu_supply/stage90_crypto_functions.c"
                      "$PSEUDO_INITS_SRC")
PL_BSD_ROOTS=(-I"$XNU/bsd")
for _c in "${COMPONENT_LIST[@]}"; do
    [[ $_c == bsd ]] && continue
    PL_BSD_ROOTS+=(-I"$XNU/$_c")
done
PL_BSD_INCLUDES=()
for _inc in "${INCLUDES[@]}"; do
    if [[ $_inc == COMP_FIRST_PLACEHOLDER ]]; then
        PL_BSD_INCLUDES+=("${PL_BSD_ROOTS[@]}")
    elif [[ $_inc == OPTION_FIRST_PLACEHOLDER ]]; then
        : # the flat option headers, which the next entry in the list already gives it (experiment-438)
    else
        PL_BSD_INCLUDES+=("$_inc")
    fi
done
# shellcheck disable=SC2207
PL_BSD_COMP_DEFINES=( $("$TOOLS_DIR/xnu_config/component_defines.sh" bsd) )
mkdir -p "$PL_OUT"
pl_fail=0
for _src in "${PLATFORM_SOURCES[@]}"; do
    _o="$PL_OUT/$(basename "${_src%.cpp}").o"
    if timeout "$PER_FILE_TIMEOUT" "${CXX_ARGS[@]}" "${EXTRA_CXX_FLAGS[@]}" "${FORCE_INCLUDES[@]}" \
           "${DEFINES[@]}" "${PL_COMP_DEFINES[@]}" "${EXTRA_DEFINES[@]}" "${PL_INCLUDES[@]}" \
           -c "$_src" -o "$_o" 2>"$PL_OUT/$(basename "${_src%.cpp}").log"; then
        rm -f "$PL_OUT/$(basename "${_src%.cpp}").log"
    else
        echo "platform: $(basename "$_src") FAILED - $PL_OUT/$(basename "${_src%.cpp}").log" >&2
        pl_fail=$((pl_fail + 1))
    fi
done
# The C one: no component defines, no force-includes, no import roots - it includes nothing, and the
# only thing that has to match the objects around it is the compiler and the target triple.
for _src in "${PLATFORM_C_SOURCES[@]}"; do
    _o="$PL_OUT/$(basename "${_src%.c}").o"
    if timeout "$PER_FILE_TIMEOUT" "${CC_ARGS[@]}" -c "$_src" -o "$_o" \
           2>"$PL_OUT/$(basename "${_src%.c}").log"; then
        rm -f "$PL_OUT/$(basename "${_src%.c}").log"
    else
        echo "platform: $(basename "$_src") FAILED - $PL_OUT/$(basename "${_src%.c}").log" >&2
        pl_fail=$((pl_fail + 1))
    fi
done
# The BSD-rooted C one, and the only difference from the loop above is which defines and which
# roots it gets: the BSD half's, because the header it includes is read by BSD code. Same compiler,
# same `-O2`, same force-include set - `-w` is in `CC_ARGS` for every file in this build.
for _src in "${PLATFORM_BSD_SOURCES[@]}"; do
    _o="$PL_OUT/$(basename "${_src%.c}").o"
    if timeout "$PER_FILE_TIMEOUT" "${CC_ARGS[@]}" "${FORCE_INCLUDES[@]}" "${DEFINES[@]}" \
           "${PL_BSD_COMP_DEFINES[@]}" "${EXTRA_DEFINES[@]}" "${PL_BSD_INCLUDES[@]}" \
           -c "$_src" -o "$_o" 2>"$PL_OUT/$(basename "${_src%.c}").log"; then
        rm -f "$PL_OUT/$(basename "${_src%.c}").log"
    else
        echo "platform: $(basename "$_src") FAILED - $PL_OUT/$(basename "${_src%.c}").log" >&2
        pl_fail=$((pl_fail + 1))
    fi
done
[[ $pl_fail -eq 0 ]] || exit 6

# The key check the loop's comment promises. Two sources, one object path, whichever compiled last
# wins - and it would show up as nothing at all: a build that reports success and an object that
# belongs to a different file. It is the same defect as `-D_CLOCK_T` and the shadowed headers
# (memory: mi4-one-value-two-definitions), so it is checked rather than reasoned about.
dupes=$(sed "s|$XNU/||; s|/|_|g; s|\.c$||; s|\.cpp$||" "$MANIFEST" | sort | uniq -d)
if [[ -n $dupes ]]; then
    echo "ERROR: two manifest sources share one object name:" >&2
    printf '%s\n' "$dupes" | sed 's/^/  /' >&2
    exit 3
fi

echo
echo "== $CONFIG manifest for arm, compiled =="
echo "  C files tried:        $((tried - cpp_ok - cpp_fail))"
echo "  compile:              $((ok - cpp_ok))"
echo "  fail:                 $((fail - cpp_fail))"
echo "  absent from tarball:  $absent"
echo "  timed out (${PER_FILE_TIMEOUT}s): $timedout"
echo "  C++ files tried:      $((cpp_ok + cpp_fail))"
echo "  C++ compile:          $cpp_ok"
echo "  C++ fail:             $cpp_fail"
echo "  skipped (.s):         $skipped"
echo "  objects in $OUT"
echo "  EABI runtime:         ${#RUNTIME_SOURCES[@]} file(s) -> $RT_OUT (not in the manifest)"
echo "  platform expert:      ${#PLATFORM_SOURCES[@]} C++ and ${#PLATFORM_C_SOURCES[@]} C file(s) -> $PL_OUT (not in the manifest)"
echo "  tables this image supplies: ${#PLATFORM_BSD_SOURCES[@]} C file(s) -> $PL_OUT (not in the manifest)"

if [[ $SHOW_BLOCKERS -gt 0 ]]; then
    echo
    echo "== distinct missing names, most common first =="
    grep -hoE "unknown type name '[A-Za-z_][A-Za-z0-9_]*'|use of undeclared identifier '[A-Za-z_][A-Za-z0-9_]*'" "$OUT/all.log" \
        | sed "s/unknown type name '//;s/use of undeclared identifier '//;s/'//" \
        | sort | uniq -c | sort -rn | head -"$SHOW_BLOCKERS"
    if [[ -s $OUT/timedout.txt ]]; then
        echo
        echo "== clang did not finish in ${PER_FILE_TIMEOUT}s =="
        sed 's/^/  /' "$OUT/timedout.txt"
    fi
    echo
    echo "== missing headers =="
    grep -hoE "'[^']*\.h' file not found" "$OUT/all.log" | sort | uniq -c | sort -rn | head -"$SHOW_BLOCKERS"
fi
