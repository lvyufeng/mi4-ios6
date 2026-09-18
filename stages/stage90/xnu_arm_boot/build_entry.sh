#!/usr/bin/env bash
#
# Build the XNU entry image: XNU's real osfmk/arm/start.s, linked with the symbols it needs, at a
# base the payload can jump to.
#
#   ./build_entry.sh            # build into out/stage90/
#   ./build_entry.sh --verbose  # show the commands
#
# The result is:
#   out/stage90/xnu_arm_entry.bin    the raw image, to be copied to ENTRY_BASE
#   out/stage90/xnu_arm_entry.elf    for inspection
#   out/stage90/xnu_arm_entry.h      base, size, entry and bss bounds, for the payload
#
# Why an image rather than objects in the payload. XNU's `_start` is position-dependent: it
# converts its link-time addresses to physical ones with `addr - virtBase + physBase`, builds its
# own page tables and switches TTBR0/TTBR1 to them. So it has to be linked at a base of its own,
# with a `boot_args` describing that base, and entered at that base. See xnu_arm_boot/entry.ld and
# entry_stubs.c for the layout and for what the symbols do.
#
# Two toolchains, deliberately. start.s is assembled by clang (nothing else will do - see
# xnu_arm_assemble.sh's header), and the Stage-owned stubs are compiled by the payload's
# arm-none-eabi-gcc, which is what the rest of the project uses. Both produce ARM EABI objects and
# the link is where they meet.

set -euo pipefail

cd "$(dirname "$0")"
STAGE_DIR=$(cd .. && pwd)
REPO_ROOT=$(cd "$STAGE_DIR/../.." && pwd)
BOOT_DIR=$STAGE_DIR/xnu_arm_boot
OUT=$REPO_ROOT/out/stage90
mkdir -p "$OUT"

VERBOSE=0
[[ ${1:-} == --verbose ]] && VERBOSE=1

# **Where the image is linked and run, and the one place this value is written down.** The linker
# script takes it as a `--defsym` symbol rather than defining it itself (entry.ld's header says why),
# so a link that forgets to pass this fails loudly instead of landing at zero.
#
# Experiment 241 moved it 0x00200000 -> 0x80000000. `is_sane_zone_ptr`'s first test is
# `pmap_kernel_va` = [0x80000000, 0xFFFEFFFF], a compile-time constant, and it is false for every
# address an image below `VM_MIN_KERNEL_ADDRESS` produces - so `free_to_zone` panicked on the
# boot path (experiments 236 to 240). 0x80000000 is where this device's RAM starts (`RAM_PHYS_BASE`)
# and where its kernel normally loads. Nothing above the image moved relative to it: every offset in
# the layout below is derived from the image's own size.
ENTRY_BASE=0x80000000

# The image's own size decides everything that sits above it, so the layout is *computed* after the
# link and written into xnu_arm_entry.h, rather than being a constant here that the payload repeats.
# These numbers have moved twice already (experiments 168 and 169) and the second time it was
# because two copies of one of them disagreed; from here the payload reads the same header, so a
# disagreement is not expressible. In order, from the base:
#
#   [ENTRY_BASE        .. bss_end)             the image: text, data, bss, stacks
#   [ENTRY_ARGS_OFFSET .. + one page)          the boot_args copy `_start` reads
#   [DATA_LIMIT        .. + ENTRY_TABLE_BYTES) `_start`'s own page tables
#   [ENTRY_DT_OFFSET   .. + ENTRY_DT_MAX)      the device tree
#   [ENTRY_BASE        .. + ENTRY_SIZE)        the window, mapped by the payload and by XNU
#
# `topOfKernelData` is DATA_LIMIT: `osfmk/arm/start.s:149` loads it out of boot_args into TTBR0 and
# TTBR1 and then writes ENTRY_TABLE_BYTES (ten pages) of entries starting there. So the image has to
# end below it, the tree has to begin above it, and both are checked before anything is written.
ENTRY_TABLE_BYTES=0x0000A000   # `(PGBYTES/4 + PGBYTES/4*4) * 2` words, from start.s's loop
ENTRY_DT_MAX=0x00020000        # the tree buffer; the tree is 0x7294 today
ARGS_BYTES=0x00001000          # one page, which is what `boot_args` needs to fit in

# `STAGE90_ENTRY_REAL_ARM_INIT=1` links XNU's own compiled objects - `arm_init.o`, `data.o`,
# `bcopy.o`, `bzero.o` - in place of the stand-ins that name themselves and return, and generates a
# reporting stub for everything still missing. Default off, so the proven image stays the default
# and the two are one variable apart.
REAL_ARM_INIT=${STAGE90_ENTRY_REAL_ARM_INIT:-0}
STUB_DEFINES=()
[[ $REAL_ARM_INIT -eq 1 ]] && STUB_DEFINES=(-DSTAGE90_ENTRY_REAL_ARM_INIT=1)
# `STAGE90_ENTRY_TRACE=1` links `entry_trace.c` and `--wrap`s five functions - `kalloc_canblock`,
# `lck_grp_alloc_init`, `kernel_memory_allocate`, `vm_page_wait`, `thread_block` - so a run that
# hangs inside real XNU code says which frame it stopped in and why. It is a *diagnostic*, not a
# stage: the traced image runs the same code, but it is not the image a stage is judged on, so this
# is off by default and the stage that experiment 268 measured is built without it. The wrappers
# change no argument - the first version forced `canblock` to FALSE and the report it produced was
# about the forcing rather than about the run; that is written up in `entry_trace.c`.
#
# What the trace bought, since the stage it was built for did not need it: 268's run was never a
# hang - 267's "silent hang" was a payload built before the step's object was in the image - and the
# frontier moved on its own. But the trace turned "the ipc tables are built" from an inference into
# a reading, and it caught the comment above calling the two allocations 512 bytes when they are
# 256. Its own first run then produced a *wrong negative*: `g_kv_buf` was 2048 bytes, filled at
# 2038, and `entry_kv` dropped the rest in silence, which is why this run's buffer is 8192 and why
# the epilogue now reports `xnu_entry_kv_dropped`. Both are in `entry_stubs.c`.
ENTRY_TRACE=${STAGE90_ENTRY_TRACE:-0}
TRACE_LDFLAGS=()
TRACE_OBJS=()
if [[ $ENTRY_TRACE -eq 1 ]]; then
    TRACE_LDFLAGS=(--wrap=kalloc_canblock --wrap=lck_grp_alloc_init
                   --wrap=kernel_memory_allocate --wrap=vm_page_wait --wrap=thread_block)
fi
# `STAGE90_ENTRY_CHECKPOINT=<symbol>` turns one function into a terminal stop: the link redirects
# every reference to it through a wrapper that calls `entry_stub_hit`, so the run reports at that
# symbol exactly as it would at a missing one and stops there. It is the tracer's move at one
# symbol instead of five, and for the case the tracer cannot cover: 280's run was silent, so the
# question is not *why* a frame blocked but *whether the run reached a frame at all*. The one
# variable is the symbol name; `entry_checkpoint.c` carries the argument. Unset - which is the
# default and every stage image - the file is not compiled and the link is unchanged.
ENTRY_CHECKPOINT=${STAGE90_ENTRY_CHECKPOINT:-}
# `STAGE90_ENTRY_CHECKPOINT_SKIP=<n>` makes that checkpoint non-terminal: the first `n` calls go
# through to the real function and the (n+1)th reports. It exists because a `--wrap` redirects
# *every* reference, and a name whose first executed site is already behind the frontier - which is
# most of `cpu_machine_idle_init`'s twenty calls, `ml_static_vtop` eight times among them - can
# otherwise not be probed at a later site at all. Only meaningful with `STAGE90_ENTRY_CHECKPOINT`.
ENTRY_CHECKPOINT_SKIP=${STAGE90_ENTRY_CHECKPOINT_SKIP:-}
# `STAGE90_ENTRY_CHECKPOINT_AFTER=1` makes that checkpoint terminal *after* the call instead of at
# it: the wrapper calls the real function, records `cp_ret` - its return value - and then reports.
# It answers the one question neither of the other two variants can: what a function *did* rather
# than where it was called from. A report is a returned value; a silence is a function that does not
# return, which is a finding rather than an absence because the plain checkpoint on the same symbol
# has already proved the call site is reached. It composes with `_SKIP`: the first `n` calls pass
# through and the `n+1`th is the one run for real. `AFTER` alone is `SKIP=0`. Its one limitation is
# in `entry_checkpoint.c` and applies to callees with stack arguments, `bcopy_phys` among them.
ENTRY_CHECKPOINT_AFTER=${STAGE90_ENTRY_CHECKPOINT_AFTER:-}
CHECKPOINT_LDFLAGS=()
if [[ -n $ENTRY_CHECKPOINT ]]; then
    CHECKPOINT_LDFLAGS=(--wrap=$ENTRY_CHECKPOINT)
fi
# `osfmk_arm_pmap.o` is linked in every real-`arm_init` build, so `pmap_bootstrap` is defined by
# XNU's own object and the probe that used to stand on it - a second definition - is compiled out
# here rather than left to be a link error. Same mechanism as `arm_init`'s stand-in above, and the
# same consequence: that probe's twelve values were experiment 195's measurement and cannot be
# taken again, because the function they were taken from now runs for real.
[[ $REAL_ARM_INIT -eq 1 ]] && STUB_DEFINES+=(-DSTAGE90_ENTRY_REAL_PMAP_BOOTSTRAP=1)
# ...and likewise for `_consume_kprintf_args`, which `osfmk_kern_printf.o` also defines, and for
# `panic`, which `osfmk_kern_debug.o` defines - the first retired stand-in whose replacement is not
# equivalent to it. See entry_stubs.c.
[[ $REAL_ARM_INIT -eq 1 ]] && STUB_DEFINES+=(-DSTAGE90_ENTRY_REAL_KPRINTF=1)
[[ $REAL_ARM_INIT -eq 1 ]] && STUB_DEFINES+=(-DSTAGE90_ENTRY_REAL_PANIC=1)
# ...and for `EntropyData`, which `osfmk_prng_random.o` defines as an *initialized* struct -
# `entropy_data_t EntropyData = { .index_ptr = EntropyData.buffer }`, which is why that object's
# `.data` is 488 bytes rather than empty. The stand-in here was the right size and the wrong value;
# experiment 211 is what linked the object and found out, and the link error that said so was
# `multiple definition of 'EntropyData'`, not any measurement of the value.
[[ $REAL_ARM_INIT -eq 1 ]] && STUB_DEFINES+=(-DSTAGE90_ENTRY_REAL_ENTROPY_DATA=1)
# `kdebug_enable`, which `bsd_kern_kdebug.o` defines. This is the first collision in this file that
# was *not* predicted by the object list: the stand-in has been here since before `arm_init` was real,
# because `osfmk/arm/start.s` declares `_kdebug_enable` and one of the first objects linked needed the
# name to resolve. Nothing had ever linked the object that owns it, so nothing had ever said so - and
# the link said it the moment experiment 225 did, as `multiple definition of 'kdebug_enable'`.
#
# The replacement is equivalent, which is not true of the last one: `bsd/kern/kdebug.c:310` is
# `unsigned int kdebug_enable = 0;`, `nm -S` reports it `B 0 4`, and the stand-in is a zero-initialized
# `uint32_t`. Same size, same type, same value - so unlike `EntropyData` there is nothing here for a
# later experiment to discover. `bsd/sys/kdebug.h:1034` declares the same `unsigned int`, and
# `osfmk/kern/debug.c:609` reads it.
[[ $REAL_ARM_INIT -eq 1 ]] && STUB_DEFINES+=(-DSTAGE90_ENTRY_REAL_KDEBUG_ENABLE=1)

# The one thing in this image that is neither XNU's nor this project's: the compiler's own runtime.
# Experiment 182's run stopped at `__aeabi_uldivmod`, which is the ARM EABI helper for 64-bit
# division and comes from libgcc - measured then, not assumed: no file in the XNU tree mentions the
# symbol, no compiled XNU object defines it, and no compiled XNU object defines any `__aeabi_*`
# symbol at all. So there is nothing here for `entry_arm_rtabi.s` to alias it to, the way it aliases
# `__aeabi_memcpy` to XNU's own `bcopy`.
#
# libgcc rather than a hand-written division routine, deliberately. `__aeabi_uldivmod` in libgcc is
# a tested implementation of a fiddly algorithm, and a second one written here would be a number
# this project cannot check against anything - the shape of mistake the probe mechanism exists to
# avoid. `--start-group` because `__aeabi_uldivmod` in that archive references `__udivmoddi4` in the
# same archive, and a single pass over an archive can miss a member it pulls late.
#
# The group is added at the *end* of the link, so it supplies only what nothing else did; whatever
# it brings beyond the named symbol shows up in the undefined list the same way any object's would.
LIBGCC=${STAGE90_ENTRY_LIBGCC:-$(arm-none-eabi-gcc -print-libgcc-file-name)}

say() { printf '%s\n' "$*"; }
run() { [[ $VERBOSE -eq 1 ]] && printf '  %s\n' "$*"; "$@"; }

say "== assembling XNU's entry point =="
run "$STAGE_DIR/xnu_arm_assemble.sh" > "$OUT/xnu_arm_assemble.log" 2>&1 || {
    tail -5 "$OUT/xnu_arm_assemble.log"; exit 1; }
say "  osfmk/arm/start.s: $(grep -c . "$OUT/xnu_arm_assemble.log") lines of report"

say "== compiling the symbols start.s needs =="
run arm-none-eabi-gcc -mcpu=cortex-a15 -marm -ffreestanding -fno-builtin -fno-common -fno-pic \
    -O2 -Wall -Wextra -Werror -std=gnu11 "${STUB_DEFINES[@]}" \
    -c "$BOOT_DIR/entry_stubs.c" -o "$OUT/xnu_arm_entry_stubs.o"
run arm-none-eabi-gcc -mcpu=cortex-a15 -marm -ffreestanding \
    -c "$BOOT_DIR/entry_vectors.s" -o "$OUT/xnu_arm_entry_vectors.o"

# The hang tracer, only when asked for. Compiled here rather than in the link block because it is a
# translation unit like the two above, and linked into `LINK_OBJS` at the bottom.
if [[ $ENTRY_TRACE -eq 1 ]]; then
    run arm-none-eabi-gcc -mcpu=cortex-a15 -marm -ffreestanding -fno-builtin -fno-common -fno-pic \
        -O2 -Wall -Wextra -Werror -std=gnu11 \
        -c "$BOOT_DIR/entry_trace.c" -o "$OUT/xnu_arm_entry_trace.o"
    say "  STAGE90_ENTRY_TRACE=1: tracing ${TRACE_LDFLAGS[*]}"
fi

# The one-symbol checkpoint, likewise only when asked for. `-Werror` is on because the wrapper is
# three lines; the symbol name arrives as a bare token so the wrapper's own name can be built from
# it with `##` and the string it reports with `#`.
if [[ -n $ENTRY_CHECKPOINT ]]; then
    CHECKPOINT_DEFINES=(-DSTAGE90_ENTRY_CHECKPOINT_SYM="$ENTRY_CHECKPOINT")
    if [[ -n $ENTRY_CHECKPOINT_SKIP ]]; then
        CHECKPOINT_DEFINES+=(-DSTAGE90_ENTRY_CHECKPOINT_SKIP="$ENTRY_CHECKPOINT_SKIP")
    fi
    if [[ -n $ENTRY_CHECKPOINT_AFTER ]]; then
        CHECKPOINT_DEFINES+=(-DSTAGE90_ENTRY_CHECKPOINT_AFTER=1)
    fi
    run arm-none-eabi-gcc -mcpu=cortex-a15 -marm -ffreestanding -fno-builtin -fno-common -fno-pic \
        -O2 -Wall -Wextra -Werror -std=gnu11 \
        "${CHECKPOINT_DEFINES[@]}" \
        -c "$BOOT_DIR/entry_checkpoint.c" -o "$OUT/xnu_arm_entry_checkpoint.o"
    if [[ -n $ENTRY_CHECKPOINT_AFTER ]]; then
        say "  STAGE90_ENTRY_CHECKPOINT=$ENTRY_CHECKPOINT: running call $(( ${ENTRY_CHECKPOINT_SKIP:-0} + 1 )) for real and reporting its return value"
    elif [[ -n $ENTRY_CHECKPOINT_SKIP ]]; then
        say "  STAGE90_ENTRY_CHECKPOINT=$ENTRY_CHECKPOINT: stopping at call $((ENTRY_CHECKPOINT_SKIP + 1))"
    else
        say "  STAGE90_ENTRY_CHECKPOINT=$ENTRY_CHECKPOINT: stopping there"
    fi
fi

# The part of the entry image that runs XNU's own code. clang, because the XNU objects it links
# against were built by clang (see xnu_arm_assemble.sh for why that is not a preference), and with
# one clang warning suppressed: XNU's EXTERNAL_HEADERS/stddef.h defines ptrdiff_t as a null-pointer
# subtraction, which clang flags under -Werror and gcc does not.
say "== compiling the in-kernel probe (XNU's own DT and boot-arg code) =="
XNU=$REPO_ROOT/external/xnu-4570.1.46

LINK_OBJS=(
    "$OUT/xnu_arm_start.o"
    "$OUT/xnu_arm_entry_vectors.o"
    "$OUT/xnu_arm_entry_stubs.o"
)

if [[ $REAL_ARM_INIT -eq 1 ]]; then
    # --- XNU's own objects, and a generated stub for everything they still need -------------------
    #
    # Each object below is the *compiled* XNU object, taken from the same builds that produced the
    # measurement image - so this is XNU's own code and data, at the same optimization, with the
    # same flags, not a recompilation. They link into this image because the objects are
    # relocatable and the addresses are fixed by this link, which is what makes the 0x00200000 base
    # a parameter rather than a recompile.
    #
    # Every path is checked, because each one is produced by a different tool and a missing file
    # would otherwise be a link error naming a symbol rather than a build that says which step to
    # run. The choice of *which* objects is deliberate and small: `data.o` is the per-CPU data and
    # the boot stacks, and `bcopy.o`/`bzero.o` are the memory routines the compiler's EABI calls
    # are aliased to (see entry_arm_rtabi.s). Nothing is added "because it looks relevant" - the
    # closure of `arm_init` is the whole kernel (see tools/entry_closure.py, experiment-158), so a
    # rule like that would not stop anywhere.
    require() {
        [[ -f $1 ]] || { say "no $1 - $2" >&2; exit 2; }
    }
    # The MIG server objects live in the tree-object directory under a **flattened** name, because
    # build_xnu_arm_kernel.sh keys an object by its source path with `$XNU/` removed and every `/`
    # turned into `_` (line 443) - and the kserver sources are under `out/`, not under the tree, so
    # for them nothing is removed and the whole absolute path is flattened. Deriving the name here
    # rather than spelling it out keeps this correct if the repository moves; the alternative,
    # `_mnt_data_mi4-ios6_out_mach_headers_kserver_mach_mach_vm_server.o`, is this project's own
    # path baked into a file name.
    kserver_obj() {
        local p="$REPO_ROOT/out/mach_headers/kserver/$1"
        printf '%s/%s.o\n' "$REPO_ROOT/out/xnu_kernel_obj" "$(printf '%s' "$p" | sed 's|/|_|g; s|\.c$||')"
    }
    ARM_INIT_OBJ=${STAGE90_ENTRY_ARM_INIT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_arm_init.o}
    ARM_DATA_OBJ=${STAGE90_ENTRY_DATA_OBJ:-$REPO_ROOT/out/xnu_asm_obj/data.o}
    ARM_BCOPY_OBJ=${STAGE90_ENTRY_BCOPY_OBJ:-$REPO_ROOT/out/xnu_asm_obj/bcopy.o}
    ARM_BZERO_OBJ=${STAGE90_ENTRY_BZERO_OBJ:-$REPO_ROOT/out/xnu_asm_obj/bzero.o}
    # `osfmk/arm/cpu.c`, and it is here because the device named it rather than because it looked
    # relevant: experiment-159's run reached `cpu_data_init()` and stopped there, and that function
    # is a field-by-field initialization of a `cpu_data_t` (`cpu.c:332-401`) whose only references
    # outside itself are the storage symbols `ExceptionVectorsTable` and `RTClockData` - both of
    # which this image already carries. So it is a leaf, and linking it is what turns the measured
    # `stub_hit=cpu_data_init` into the *next* thing `arm_init` asks for. The rest of `cpu.o` is
    # stubbed as usual: the closure of `arm_init` is the whole kernel, so the image grows one object
    # at a time and each run reports the next edge.
    ARM_CPU_OBJ=${STAGE90_ENTRY_CPU_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_cpu.o}
    # `pexpert/arm/pe_init.c`, again named by the previous run rather than by inspection:
    # experiment-168's `stub_hit=PE_init_platform` is `arm_init.c:159`, and this object defines it.
    # It is not a leaf the way `cpu.o` is - `PE_init_platform` pulls in `DTInit` and the rest of
    # XNU's device-tree reader, which is the point of the run: it makes XNU's own tree reader walk
    # the tree this project built, from inside XNU's own `arm_init`, with XNU's own page tables.
    # It is also 34927 bytes of text against experiment-168's 26744 bytes of headroom, which is why
    # `topOfKernelData` moved in the same experiment.
    ARM_PE_INIT_OBJ=${STAGE90_ENTRY_PE_INIT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_arm_pe_init.o}
    # `osfmk/arm/strlcpy.c`, named by experiment-169's run: with `pe_init.o` in the image,
    # `PE_init_platform` ran its body and stopped at `stub_hit=strlcpy`, which is its first call
    # that this image does not provide (`pe_init.c:302`, the pixel-format assignment - eight lines
    # before the `DTInit` call the previous experiment predicted). 68 bytes of text and no external
    # references of its own, so it is a leaf, and linking it is what puts the `DTInit` probe added
    # in entry_stubs.c in reach.
    ARM_STRLCPY_OBJ=${STAGE90_ENTRY_STRLCPY_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_strlcpy.o}
    # `osfmk/arm/strlen.s`, and the one place this build links two objects in one experiment on
    # purpose rather than one. `nm -u osfmk_arm_strlcpy.o` is `memcpy` and `strlen`; `memcpy` is
    # already in the image, and `strlen` is a leaf in the assembly pool. Linking `strlcpy.o` alone
    # would spend a hardware run reporting a name the host already printed, and the object after it
    # - the one whose call the `DTInit` probe in entry_stubs.c is waiting for - would still not be
    # reached. So both are here: the edge the device named, and the one symbol that edge needs.
    ARM_STRLEN_OBJ=${STAGE90_ENTRY_STRLEN_OBJ:-$REPO_ROOT/out/xnu_asm_obj/strlen.o}
    # `osfmk/arm/strncpy.c`, named by experiment-185's `stub_hit=strncpy` - which is `locks.c:169`,
    # the fourth statement of `lck_mod_init`, reached now that the lock subsystem's own code runs.
    # 104 bytes of text, and its `nm -u` is `memcpy`, `memset` and `strnlen`: `memcpy` and `memset`
    # are already in the image (bcopy.o/bzero.o, aliased by entry_arm_rtabi.s), so it is a leaf
    # apart from `strnlen`, which is in the assembly pool. Same shape as the strlcpy/strlen pair
    # above and linked the same way - the edge the device named, plus the one symbol that edge needs.
    #
    # It is also worth noting what this symbol is: it has been in the undefined list since the image
    # was first assembled and only became the *edge* when the code calling it became real. The
    # closure is not a queue.
    ARM_STRNCPY_OBJ=${STAGE90_ENTRY_STRNCPY_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_strncpy.o}
    # `osfmk/arm/strnlen.s`, the leaf `strncpy.c` needs - 184 bytes of text, no undefined references.
    ARM_STRNLEN_OBJ=${STAGE90_ENTRY_STRNLEN_OBJ:-$REPO_ROOT/out/xnu_asm_obj/strnlen.o}
    # XNU 4570's own device-tree reader, `pexpert/gen/device_tree.c`, and this is the object whose
    # *absence* made `DTInit` a symbol experience-170's probe reported. It defines `DTInit`,
    # `DTFindEntry`, `DTGetProperty`, `DTLookupEntry`, `DTInitEntryIterator` and `DTIterateEntries`
    # - five of which are also in `nm -u` on the object below, so it is named twice over.
    #
    # It is XNU 4570's, not the 2050 reader the *payload* links behind `STAGE90_XNU_REAL_DT`
    # (`out/stage90/xnu-objects/device_tree.o`, 3880 bytes, `DTCreateEntryIterator`). Exp-106 found
    # from the other side that the two are not interchangeable; this is the side where it matters,
    # because the caller here is 4570's own `pe_init.o` and the iterator API it uses
    # (`DTInitEntryIterator`) does not exist in 2050.
    ARM_DEVICE_TREE_OBJ=${STAGE90_ENTRY_DEVICE_TREE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_gen_device_tree.o}
    # `pexpert/arm/pe_identify_machine.c`. Strictly this is the call `PE_init_platform` makes after
    # `DTInit` (`pe_init.c:311`), and the one this project's method would let a run name - but the
    # object above cannot produce a run worth doing on its own: 4570's `DTInit` is
    # `DTRootNode = base; DTInitialized = (DTRootNode != 0);`, two statements with no calls in them,
    # so the only outcome of linking it alone is the stub for this object's function firing. It is
    # linked here because it is what makes the reader above do something measurable: its first act
    # is `pe_arm_get_soc_base_phys()`, which finds `arm-io` in the tree, reads `device_type` and
    # `ranges`, and returns `ranges[1]` - so this is the object that turns "the reader is present"
    # into "the reader read this project's device tree".
    ARM_PE_IDENTIFY_OBJ=${STAGE90_ENTRY_PE_IDENTIFY_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_arm_pe_identify_machine.o}
    # `osfmk/device/subrs.c`, named by experiment-171's `stub_hit=strcmp`. The reader above compares
    # property names and values to walk a node (`device_tree.c:375` and `:144`) and `pe_identify_machine`
    # compares the SoC device type against Apple's board names (`:58` onward), so either of the two
    # ways that run could have got there needs this object to get any further.
    ARM_SUBRS_OBJ=${STAGE90_ENTRY_SUBRS_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_device_subrs.o}
    # `osfmk/arm/strncmp.s`, named by experiment-172. It is the comparison the reader makes on the
    # `state` property of the cpu node it just found (`pe_identify_machine.c:117`), and it is the
    # last thing between the walk and the probe in entry_stubs.c: if `state` matches "running", the
    # reader goes on to read the frequencies out of the same node and then returns.
    ARM_STRNCMP_OBJ=${STAGE90_ENTRY_STRNCMP_OBJ:-$REPO_ROOT/out/xnu_asm_obj/strncmp.o}
    # `pexpert/gen/pe_gen.c`, named by experiment-173's `stub_hit=pe_init_debug` - the last
    # statement of `PE_init_platform`, so the one symbol between that function and its return, and
    # therefore the one between the image and the `ml_parse_cpu_topology` probe that has been
    # waiting since experiment 171. 508 bytes of text, and it also defines `PE_putc`,
    # `PE_init_printf`, `PE_enter_debugger` and `PE_get_random_seed`, all of which are stubs today.
    ARM_PE_GEN_OBJ=${STAGE90_ENTRY_PE_GEN_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_gen_pe_gen.o}
    # `pexpert/gen/bootargs.c`, and this is `nm -u` on the object above, not a guess: `pe_init_debug`
    # is four `PE_parse_boot_argn` calls and a bitmask. Linking pe_gen.o alone would therefore
    # produce a run whose one possible outcome the host can already name, which is the same reason
    # strlcpy and strlen travelled together in experiment 170.
    ARM_BOOTARGS_OBJ=${STAGE90_ENTRY_BOOTARGS_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_gen_bootargs.o}
    # `pexpert/arm/pe_bootargs.c`, named by experiment-174's `stub_hit=PE_boot_args`: the whole file
    # is one accessor, `(char *)((boot_args *)PE_state.bootArgs)->CommandLine`. It travels alone,
    # and that is measured rather than assumed - `nm -u` on the object names `PE_state` and nothing
    # else, and `pexpert/arm/pe_init.o` (in this link since experiment 172) defines it as real
    # storage. So this object adds a definition and no obligation, which is the opposite of the
    # pe_gen/bootargs pair above.
    ARM_PE_BOOTARGS_OBJ=${STAGE90_ENTRY_PE_BOOTARGS_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_arm_pe_bootargs.o}
    # `osfmk/arm/machine_routines.c`, named by experiment-176's `stub_hit=ml_parse_cpu_topology` -
    # which `osfmk/arm/arm_init.c:217` calls as the first thing after the platform expert is up.
    #
    # This is the first object in the frontier that is not small. It is 4135 bytes of text across 71
    # functions and references 79 undefined symbols, 56 of them new to this image; every object
    # before it added three to five. The linker resolves an object's references whether or not the
    # function making them ever runs, so linking it whole is what pulls those 56 in - and what the
    # run then measures is which of them XNU's own code reaches next.
    ARM_MACHINE_ROUTINES_OBJ=${STAGE90_ENTRY_MACHINE_ROUTINES_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_machine_routines.o}
    # `osfmk/arm/cpu_common.c`, named by experiment-177's `stub_hit=cpu_processor_alloc`. Small
    # again: 2428 bytes of text, 36 symbols, 31 references of which 17 are new. It defines
    # `cpu_processor_alloc` (`:472`), which for the boot CPU is `return &BootProcessor;`, and it
    # also defines `current_processor` and `cpu_number` - which is why the probe now stands one edge
    # further on, at `thread_bootstrap`, where the addresses `arm_init` just wrote can be read.
    ARM_CPU_COMMON_OBJ=${STAGE90_ENTRY_CPU_COMMON_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_cpu_common.o}
    # `osfmk/kern/thread.c`, named by experiment-178's `stub_hit=thread_bootstrap`. This is the
    # first object whose size changes the character of the step: 16944 bytes of text, 77 functions,
    # 161 references of which 135 are new. Whether "one object" is still the right unit at that size
    # is what this link measures.
    ARM_KERN_THREAD_OBJ=${STAGE90_ENTRY_KERN_THREAD_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_thread.o}
    # `osfmk/kern/timer.c`, named by experiment-179's `stub_hit=timer_init` - the first call any
    # function in `thread.o` makes. Small: 320 bytes of text across 7 functions, 3 references, 1 of
    # them new. Needed because `thread_bootstrap` ends with three `timer_init()` calls; the object
    # after this one is `machine_routines_asm.o`, which defines `machine_set_current_thread` - the
    # last call in `thread_bootstrap`, and the one that writes the TPIDRPRW that `current_thread()`
    # reads back at `arm_init.c:241`.
    ARM_KERN_TIMER_OBJ=${STAGE90_ENTRY_KERN_TIMER_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_timer.o}
    # `osfmk/arm/machine_routines_asm.s`, named by experiment-180's `stub_hit=machine_set_current_thread` -
    # the last statement of `thread_bootstrap`. Assembled rather than compiled (`tools/assemble_arm_layer.sh`),
    # because it is Apple's assembly. It is where the thread really becomes current: the first
    # instruction of `machine_set_current_thread` writes TPIDRPRW, which is the register
    # `current_thread()` (`osfmk/arm/cpu_data.h:52`) reads back - so linking this object is what makes
    # `arm_init.c:241`'s `thread = current_thread()` return the thread `thread_bootstrap` just made.
    # 2280 bytes of text, 76 symbols, 12 references of which 4 are new.
    ARM_MACHINE_ROUTINES_ASM_OBJ=${STAGE90_ENTRY_MACHINE_ROUTINES_ASM_OBJ:-$REPO_ROOT/out/xnu_asm_obj/machine_routines_asm.o}
    # `osfmk/arm/rtclock.c`, named by experiment-181's `stub_hit=rtclock_early_init`. 2136 bytes of
    # text, 19 functions, 18 references of which 3 are new. `rtclock_early_init` is one call -
    # `PE_register_timebase_callback(timebase_callback)` - and that accessor is already real
    # (`pexpert/arm/pe_init.o`), and it *invokes* the callback immediately with
    # `gPEClockFrequencyInfo.timebase_frequency_hz` over 1. So linking this object makes
    # `timebase_callback` real, and it is the first code in the image to divide.
    ARM_ARM_RTCLOCK_OBJ=${STAGE90_ENTRY_ARM_RTCLOCK_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_rtclock.o}
    # `osfmk/kern/startup.c`, named by experiment-183's `stub_hit=kernel_early_bootstrap`. 2728 bytes
    # of text across 7 functions and 98 references, 72 of them new - the largest step since
    # `thread.o`, and the one that brings in the names of the kernel's own startup sequence:
    # `lck_mod_init`, `timer_call_init`, `ipc_init`, `task_init`, `thread_call_initialize`,
    # `vm_mem_bootstrap`, `sched_init`, `machine_load_context`, `console_init`, `bsd_init`.
    # `kernel_early_bootstrap` itself (`startup.c:226`) is four statements: one boot-arg parse, then
    # `lck_mod_init()` and `timer_call_init()`.
    ARM_KERN_STARTUP_OBJ=${STAGE90_ENTRY_KERN_STARTUP_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_startup.o}
    # `osfmk/kern/timer_call.c`, named by experiment-184's `stub_hit=lck_mod_init` - not because
    # `lck_mod_init` is in it, but because it is the *next* thing `kernel_early_bootstrap`
    # (`startup.c:238`) asks for, one statement after the symbol the probe answered. 11321 bytes of
    # text across 32 functions and 33 references, so this is the larger half of the same function.
    # `timer_call_init` (`timer_call.c:248`) is five statements, and the first three are locks:
    # `lck_attr_setdefault`, `lck_grp_attr_setdefault`, `lck_grp_init`, then `timer_longterm_init`
    # and `timer_call_init_abstime`. It is the first edge in this image that lands in the lock
    # subsystem - none of which has run - which is why the step is measured before it is taken.
    ARM_KERN_TIMER_CALL_OBJ=${STAGE90_ENTRY_KERN_TIMER_CALL_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_timer_call.o}
    # `osfmk/kern/locks.c`, named by experiment-184's `stub_hit=lck_mod_init`. 5943 bytes of text
    # across 47 functions. `lck_mod_init` (`locks.c:140`) is short and has no missing callees:
    # one boot-arg parse, then `queue_init`, a `bzero`, a `strncpy` of "Compatibility APIs",
    # `enqueue_tail`, and `lck_grp_attr_setdefault`/`lck_attr_setdefault`/`lck_mtx_init_ext` - all
    # of which this object also defines. It is the first code in this image that initializes a lock,
    # and it is linked with `timer_call.o` because the two are the same statement pair in
    # `kernel_early_bootstrap` and each was sized on its own before either was added.
    ARM_KERN_LOCKS_OBJ=${STAGE90_ENTRY_LOCKS_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_locks.o}
    # `osfmk/arm/locks_arm.c`, named by experiment-186's `stub_hit=lck_mtx_init_ext` - the last
    # statement of `lck_mod_init`. 9868 bytes of text across 64 functions and 37 references, so this
    # is a step the size of `thread.o` in exp-179 rather than the size of the last three, and it is
    # measured before it is taken. It is also where the mutex itself lives: `lck_mtx_lock`,
    # `lck_mtx_unlock`, `lck_mtx_ilk_unlock`, `mutex_pause`, `MutexSpin`, and the six `hw_atomic_*`
    # functions this file's neighbour `locks.o` referenced.
    ARM_LOCKS_ARM_OBJ=${STAGE90_ENTRY_LOCKS_ARM_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_locks_arm.o}
    # `osfmk/arm/arm_timer.c`, named by experiment-187's `stub_hit=timer_call_get_priority_params`.
    # 1044 bytes of text across 11 functions and 168 bytes of data, 11 references - the cheapest
    # step since exp-186, and the first whose object is a *driver* rather than more of the kernel
    # initializing itself. `timer_call_get_priority_params` is one line returning
    # `&tcoal_prio_params_init`, and that 168-byte table is the whole of the object's `.data`: the
    # struct is `timer_coalescing_priority_params_ns_t` (`timer_queue.h:114`), whose member offsets
    # (0, 4, 8, 12..32, 32/40/48/56/64, 72, 96, 144) add up to exactly 168, which is how the image
    # knows the table it links and the struct `timer_call.c` reads it through are the same shape.
    # The object also carries `timer_queue_assign`, `timer_call_cpu`, `timer_intr`,
    # `timer_resort_threshold` and `quantum_timer_set_deadline`, all of which the image already
    # references.
    ARM_ARM_TIMER_OBJ=${STAGE90_ENTRY_ARM_TIMER_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_arm_timer.o}
    # `osfmk/arm/cpuid.c`, named by experiment-188's `stub_hit=do_cpuid` - the first symbol
    # `cpu_init` asks for, and the statement in `arm_init` right after `kernel_early_bootstrap`.
    # 850 bytes of text across 9 functions and 10 references, and it resolves five things at once:
    # `do_cpuid`, `do_cacheid`, `do_mvfpid`, `do_debugid`, `cpuid_info` and `cache_info`. This is
    # the first object in the image whose work is reading the CPU - MIDR, CLIDR, CCSIDR and the
    # MVFR registers - rather than reading what XNU or the device tree computed.
    ARM_ARM_CPUID_OBJ=${STAGE90_ENTRY_ARM_CPUID_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_cpuid.o}
    # `osfmk/arm/machine_cpuid.c`, which is what the line above is missing: all nine of cpuid.o's
    # `machine_*` references are here, and this object's own `nm -u` is empty. 220 bytes of text
    # across 9 functions and 24 bytes of `.bss`, so it is self-contained and costs nothing beyond
    # its size. It is linked in the same step rather than discovered in the next one because a
    # frontier that is only reachable through stubs is not a frontier.
    ARM_ARM_MACHINE_CPUID_OBJ=${STAGE90_ENTRY_ARM_MACHINE_CPUID_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_machine_cpuid.o}
    # `osfmk/kern/processor.c`, named by experiment-189's `stub_hit=processor_bootstrap`. 4784 bytes
    # of text, 96 of data and 1616 of `.bss` across 42 references, and it defines both
    # `processor_bootstrap` (`processor.c:120`) and `processor_init` (`processor.c:135`), so one
    # object covers two statements of `arm_init`'s path. What it brings is the scheduler's own
    # state: `processor_array`, `pset0`, `master_processor`, `master_cpu`, `processor_count`,
    # `processor_list` - the objects `sched_init` and the scheduler are about to start using. The
    # new obligation to watch is `processor_data_init`, which is in `processor_data.c` and is the
    # first symbol `processor_init` reaches that nothing defines, which is why the probe is there.
    ARM_KERN_PROCESSOR_OBJ=${STAGE90_ENTRY_KERN_PROCESSOR_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_processor.o}
    # `osfmk/kern/processor_data.c`, the first symbol experiment-190's `stub_hit=processor_data_init`
    # named. 72 bytes of text and two references (`memset`, `timer_init`), both already satisfied in
    # this image, so this is the smallest non-empty step in the sequence: it resolves
    # `processor_data_init` and adds nothing at all. The object is the `processor_data_t` at the end
    # of `struct processor` - `timer_init` over the idle, system and user states, and the debugger
    # state's `db_current_op`.
    ARM_KERN_PROCESSOR_DATA_OBJ=${STAGE90_ENTRY_KERN_PROCESSOR_DATA_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_processor_data.o}
    # `osfmk/arm/machine_routines_common.c`, named by experiment-191's
    # `stub_hit=ml_set_interrupts_enabled`. 2923 bytes of text, 44 of data and 108 of `.bss` across
    # 18 references. Five of the eighteen are undefined in this image and become stubs -
    # `ast_taken_kernel`, `get_threadtask`, `kernel_task`, `proc_get_effective_thread_policy` and
    # `thread_get_perfcontrol_class` - and all five belong to the perfcontrol and AST paths, none of
    # which is on this boot path. The one symbol that matters is `ml_set_interrupts_enabled`
    # (`machine_routines_common.c:507`): not a register access but twenty lines of scheduler state
    # inspection, with the compiled `cpsid if` at the end of its disable path. `processor_init`
    # calls it twice, both times with 0, so only that path runs.
    #
    # The probe had to move off `ml_set_interrupts_enabled` before this object could be measured:
    # the object defines the symbol the exp-191 probe defined, and a link that sees two definitions
    # leaves a partial undefined-reference list - the failure that produced exp-190's wrong
    # measurement of 11 and 11.
    ARM_MACHINE_ROUTINES_COMMON_OBJ=${STAGE90_ENTRY_MACHINE_ROUTINES_COMMON_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_machine_routines_common.o}
    # `osfmk/arm/arm_vm_init.c`, named by experiment-192's `stub_hit=arm_vm_init` and measured
    # before the run that consumed it. 8176 bytes of text, 8 of data and 216 of `.bss` across 23
    # references - the largest object since `thread.o` in exp-179, and the first thing in this
    # sequence the original goal statement is about: `arm_vm_init` is where the kernel starts
    # setting up memory for the drivers above it.
    #
    # Six of its twenty-three references are the Mach-O section readers (`getsegdatafromheader`,
    # `getsegbynamefromheader`, `getsectbynamefromheader`, `getlastaddr`, plus `firstseg` and
    # `nextsect`), which are all in one object with two references of its own, so the two are
    # linked as one step the way `cpuid.o` and `machine_cpuid.o` were in exp-189. The header those
    # readers walk is defined in this image by `entry_macho.s`; see that file for why an ELF needs
    # one and why a zeroed stub for `_mh_execute_header` is a data abort rather than a slow path.
    ARM_ARM_VM_INIT_OBJ=${STAGE90_ENTRY_ARM_VM_INIT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_arm_vm_init.o}
    LIBKERN_KERNEL_MACH_HEADER_OBJ=${STAGE90_ENTRY_KERNEL_MACH_HEADER_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/libkern_kernel_mach_header.o}
    # `osfmk/vm/vm_resident.c`, named by experiment-194's `stub_hit=vm_set_page_size`. The function
    # is twelve statements and 52 bytes with no calls in it, but its *object* is the largest this
    # sequence has linked: 31692 bytes of text, 104 of data and 5772 of `.bss` across 113
    # references, and it is the first object from the VM proper rather than the ARM layer. The 113
    # become stubs, and that is the reason this step is measured before it is taken.
    #
    # Nothing between `vm_set_page_size` and the next undefined symbol calls into any of them:
    # `set_mmu_ttb`, `set_mmu_ttb_alternate` and `flush_mmu_tlb` are real, and what follows is a
    # block of stores into `arm_vm_init.o`'s own globals. So the run reaches `pmap_bootstrap`, which
    # is `osfmk/arm/pmap.c`, and that is where the probe stands.
    VM_VM_RESIDENT_OBJ=${STAGE90_ENTRY_VM_RESIDENT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_vm_vm_resident.o}
    # `osfmk/arm/pmap.c`, named by experiment-195's `stub_hit=pmap_bootstrap`. This is the pmap
    # proper - 45052 bytes of text, 72 of data, 1032 of `.bss` and 93 references - and the point at
    # which the kernel starts building page tables for memory that is not this image.
    #
    # It is linked for a reason that the call graph does not show. `pmap_bootstrap`'s twenty-six
    # `bl` sites all name symbols this image already provides (and `__aeabi_uldivmod`, which libgcc
    # supplies), so the walk reports no stop inside it - but it also reads and writes `kernel_pmap`,
    # `kernel_pmap_store`, `pmap_stamp`, the io-region descriptors and the four bookkeeping tables,
    # and every one of those is a *generated storage stub* today. A storage stub is zero, and the
    # first statement of `pmap_bootstrap` is `kernel_pmap->tte = cpu_tte` - a store through a null
    # pointer. Its size is the other half: the generator takes storage sizes from `nm -S` over this
    # project's object pool, which is the right size for the array and the wrong *content* for the
    # pointer. So the object is what makes this function's data real, and the object is the step.
    #
    # Two collisions follow from linking it, and both are settled rather than discovered: the
    # `pmap_bootstrap` probe in `entry_stubs.c` is compiled out (it is a second definition), and
    # the run no longer stops inside `pmap_bootstrap` at all - it stops at whatever the image
    # reaches next, which is the measurement.
    ARM_PMAP_OBJ=${STAGE90_ENTRY_PMAP_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_pmap.o}
    # `osfmk/arm/lowmem_vectors.c`, named by experiment-197's `stub_hit=patch_low_glo_static_region`.
    # 72 bytes of text and 988 of data, and the data is the point again: `lowGlo` is an
    # *initialized* page-aligned structure full of self-referential pointers (`&version`, `&kmod`,
    # `&osversion`, `&pmap_object_store.memq`, and the `vm_page` offsets a debugger reads) that a
    # zero-filled stand-in cannot stand in for at all. It also defines `patch_low_glo`, which this
    # image's boot args reach: `debug=0x144` is on the payload's command line, `arm_init.c:323-325`
    # tests `(debug & MIN_LOW_GLO_MASK) == MIN_LOW_GLO_MASK` with `MIN_LOW_GLO_MASK = 0x144`, and
    # the call follows `arm_vm_init`'s return.
    ARM_LOWMEM_VECTORS_OBJ=${STAGE90_ENTRY_LOWMEM_VECTORS_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_lowmem_vectors.o}
    # `osfmk/kern/printf.c`, named by experiment-198's `stub_hit=printf_init`. 5607 bytes of text,
    # 296 of `.bss` and 26 references - the largest step since `pmap.o`, and the first object this
    # project links whose subject is output.
    #
    # It is the object entry_stubs.c spent a paragraph arguing *against* linking, and the argument
    # still holds: it was about buying four bytes (`_consume_kprintf_args` is an empty variadic
    # function, and the hand-written stand-in for it is the real one). What changed is the reason.
    # `printf_init` is three statements and lives here and nowhere else, and it is the next thing
    # the device reaches, so the object is the step. Two consequences, both settled: the
    # hand-written `_consume_kprintf_args` is now a second definition and is compiled out under
    # `STAGE90_ENTRY_REAL_KPRINTF`, and the console stack this brings - `cnputc`, `PE_kputc`,
    # `debug_putc`, `os_log_with_args`, `paniclog_flush` - becomes 24 stubs that stop the run if
    # reached. `printf_init` reaches none of them: its two `simple_lock_init` calls are
    # `arm_usimple_lock_init`, which the image has had since the lock subsystem came in, and its
    # third statement is `bsd_log_init`.
    ARM_KERN_PRINTF_OBJ=${STAGE90_ENTRY_KERN_PRINTF_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_printf.o}
    # `bsd/kern/subr_log.c`, named by experiment-199's `stub_hit=bsd_log_init` - the first object in
    # this sequence from `bsd/` rather than `osfmk/`. 5851 bytes of text, 4316 of data, 12496 of
    # `.bss` and 42 references, which makes it the largest `.bss` and the widest reference set of
    # any step so far, for a function whose entire body is a comment:
    #
    #     void bsd_log_init(void) { /* After this point, we must be ready to accept characters */ }
    #
    # That is the shape of step the method produces at a boundary: the frontier names the next
    # missing *symbol*, and the symbol here is a no-op. Nothing of the 42 is reachable from it -
    # `tsleep`, `kernel_map`, `mach_vm_map_kernel`, `kalloc_canblock`, `selwakeup` and the two
    # `__firehose_*` calls all sit behind other functions in the file - so the run should pass
    # through and stop at `panic_init`, `arm_init.c:330`, back in `osfmk/`.
    BSD_KERN_SUBR_LOG_OBJ=${STAGE90_ENTRY_BSD_SUBR_LOG_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/bsd_kern_subr_log.o}
    # `osfmk/kern/debug.c`, named by experiment-200's `stub_hit=panic_init`. 5549 bytes of text, 40
    # of data, 721 of `.bss` and 61 references. Only four statements of `panic_init` run, and the
    # interesting one is the first: `getuuidfromheader(&_mh_execute_header, &uuidlen)` walks *this
    # image's own Mach-O header* - the one experiment 194 added - looking for `LC_UUID`. The header
    # has `ncmds = 2` and two `LC_SEGMENT` commands, so the search returns NULL and the
    # `uuid_unparse_upper` call behind it is not reached; `entry_frontier.py` reports it anyway,
    # because it does not model branches.
    ARM_KERN_DEBUG_OBJ=${STAGE90_ENTRY_KERN_DEBUG_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_debug.o}
    # `pexpert/arm/pe_consistent_debug.c`, named by experiment-201's
    # `stub_hit=PE_consistent_debug_inherit`. 394 bytes of text, 4 of `.bss`, 4 references, three of
    # them already satisfied - the smallest object this sequence has linked. Its function looks up
    # `/chosen`'s `consistent-debug-root` and, if it finds one, hands it to `ml_map_high_window`.
    #
    # It will not find one. `stage90_main.c:647-661` writes that property only under
    # `#if STAGE90_XNU_REAL_DT`, whose default is 0 (`stage90.h:6922-6923`), so `/chosen` has four
    # properties and not five, `DTGetProperty` fails and the function returns -1. The run should
    # therefore stop at the next statement of `arm_init`, which is `PE_init_kprintf` - a symbol in
    # no object linked here, and the same shape as experiment 201's prediction: a call that exists
    # in the source and is not reachable on this configuration.
    PEXPERT_PE_CONSISTENT_DEBUG_OBJ=${STAGE90_ENTRY_PE_CONSISTENT_DEBUG_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_arm_pe_consistent_debug.o}
    # `pexpert/arm/pe_kprintf.c`, named by experiment-202's `stub_hit=PE_init_kprintf`. 536 bytes of
    # text, 4 of data, 48 of `.bss` and 16 references. It also defines `PE_kputc` and
    # `disable_serial_output`, which this image has been carrying as generated *storage* stubs
    # (4 bytes each, sized from this very object) - so linking it turns both into the real variable,
    # and the generator stops stubbing them because pass 1 now sees them defined.
    PEXPERT_PE_KPRINTF_OBJ=${STAGE90_ENTRY_PE_KPRINTF_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_arm_pe_kprintf.o}
    # `pexpert/arm/pe_serial.c`, named by experiment-203's `stub_hit=serial_init`. 421 bytes of
    # text and **six references - every one of them already satisfied** by objects this image has
    # had since experiments 168-171 (`PE_parse_boot_argn`, `pe_arm_get_soc_base_phys`, `DTFindEntry`,
    # `DTGetProperty`, `ml_io_map`, `arm_debug_read_dscr`). So this step adds no obligation at all,
    # which is the first time that has been true since `cpu.o` in experiment 168.
    #
    # Its compiled body, read from the object rather than from the source, is: `dcc` is not a boot
    # arg; `pe_arm_get_soc_base_phys()` is non-zero; then `DTFindEntry("boot-console", NULL, ...)`,
    # `DTFindEntry("name","uart0",...)` and `DTFindEntry("name","uart1",...)` in turn - and this
    # project's tree has no serial node of any of those three names (`grep` finds none in
    # `stage90_main.c` or `apple_dt.c`), so all three fail and the function returns 0. The
    # `S3CUART` and `ARM_BOARD_CONFIG_MV88F6710` branches are not compiled in - the object carries
    # no `strcmp` reference and none of their strings - so what is left is the
    # `else return 0` at `pe_serial.c:803`.
    #
    # It is linked anyway, because the point of the step is the *lookup*: `serial_init` is the first
    # XNU code to ask this project's device tree for a serial device, and the answer it gets is the
    # one that decides `PE_kputc` (`serial_putc` or `cnputc`).
    PEXPERT_PE_SERIAL_OBJ=${STAGE90_ENTRY_PE_SERIAL_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/pexpert_arm_pe_serial.o}
    # `osfmk/console/video_console.c`, named by experiment-204's `stub_hit=initialize_screen`.
    # 27079 bytes of text, 4376 of data, 1360 of `.bss` and 31 references - the largest step since
    # `pmap.o`, and the object the goal statement puts out of scope ("ignore graphics for now").
    # It is linked anyway, because XNU's console *is* the video console until something replaces it:
    # `PE_create_console` (`pe_init.c:379`) calls `PE_initialize_console(info, kPETextMode)`, whose
    # `default:` case calls `initialize_screen(info, op)` unconditionally, and `initialize_screen`
    # lives only here. The console is a basic driver and it is on the boot path; the *graphics* the
    # goal defers are the framebuffer, not this.
    #
    # What should run is one branch of it. `initialize_screen`'s first act, with `boot_vinfo`
    # non-null, is to look for a framebuffer, and this payload's `boot_args` leaves `Video` zeroed
    # (`boot_args.c` sets twelve fields and `Video` is not among them), so:
    #
    #     if (!newVideoVirt && !new_vinfo.v_physaddr) {
    #         kprintf("initialize_screen: No video - forcing serial mode\n");
    #         new_vinfo.v_depth = 0;
    #         (void)switch_to_serial_console();      <-- the stop
    #         ...
    #
    # which is the same symbol experiment 204 predicted and did not get, one call frame deeper and
    # by a different route. Which of the two routes is worth being explicit about: if the stop is
    # `switch_to_serial_console`, XNU has decided this device has no framebuffer.
    OSFMK_CONSOLE_VIDEO_OBJ=${STAGE90_ENTRY_VIDEO_CONSOLE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_console_video_console.o}
    # `osfmk/console/serial_general.c`, named by experiment-205's `stub_hit=switch_to_serial_console`.
    # 824 bytes of text and 14 references - but `switch_to_serial_console` itself is three statements
    # and calls nothing at all:
    #
    #     int old_cons_ops = cons_ops_index;
    #     cons_ops_index = SERIAL_CONS_OPS;
    #     return old_cons_ops;
    #
    # (`osfmk/console/serial_general.c` - it is the whole function.) So this step does not stop
    # inside the function it exists for: `initialize_screen`'s no-video branch calls it, it runs, it
    # returns the old console index, and `initialize_screen` finishes the branch
    # (`gc_graphics_boot = FALSE; disableConsoleOutput = FALSE; gc_acquired = TRUE;`). `cons_ops_index`
    # is the one new obligation and it is storage (4 bytes, `osfmk_console_serial_console.o`).
    #
    # The frontier therefore moves to `arm_init`'s tail, and the prediction is written in
    # experiment 206's log rather than here: the calls after PE_create_console are `PE_init_printf`
    # (a tail call to `vcattach`, which video_console.o provides), `cpu_machine_idle_init` (whose
    # `cpu.c:562` maps the low vectors and calls `ml_io_map`), `PE_init_platform(TRUE, &BootCpuData)`,
    # `cpu_timebase_init`, `fiq_context_init`, `early_random` (still a stub) and `machine_startup`.
    # Experiment 206's run stopped at `stub_hit=io_map` - inside `cpu_machine_idle_init`, NOT inside
    # `PE_init_platform`'s `pe_arm_init_interrupts`, which `arm_init.c` calls two calls later and
    # which that run never reached. `pe_arm_map_interrupt_controller` could not have got there in any
    # case: the tree has no `interrupt-controller`/`master` node on purpose.
    OSFMK_CONSOLE_SERIAL_GENERAL_OBJ=${STAGE90_ENTRY_SERIAL_GENERAL_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_console_serial_general.o}
    # `osfmk/arm/io_map.c`, named by experiment-206's `stub_hit=io_map`. 360 bytes of text, no data
    # and no `.bss`, seven references, two definitions (`io_map` and `io_map_spec`). Five of the
    # seven are already real here - `panic` (exp-201), `pmap_map` / `pmap_map_bd` /
    # `pmap_map_bd_with_options` (pmap.o, exp-197), `virtual_space_start` (vm_resident.o, exp-195) -
    # and the other two, `kernel_map` and `kmem_alloc_pageable`, are already undefined names in this
    # image, so the step introduces no new obligation: 2 resolved, 0 added.
    #
    # This is the first time this kernel creates a mapping for itself. `ml_io_map` is called from
    # `cpu.c:562` with `ml_vtophys(gPhysBase)` and `PAGE_SIZE`, and `gVirtBase == gPhysBase ==
    # 0x00200000` here, so the call is `io_map(0x00200000, 4096, VM_WIMG_IO)`. `kernel_map` is still
    # the generated 4-byte zero stand-in - which is the *right* value: XNU creates the real one in
    # `kmem_init`, long after `arm_init` - so `io_map` takes its "VM is not initialized" branch and
    # carves the page off `virtual_space_start`.
    #
    # `virtual_space_start` is `0x40000000`: `pmap_bootstrap` sets it to
    # `(gVirtBase + MEM_SIZE_MAX + 0x3FFFFF) & 0xFFC00000` (`arm_vm_init.c:505`, with
    # `MEM_SIZE_MAX` = `0x40000000` at `:134`), and `arm_vm_init.c:517` builds the page tables for
    # that same VA with the same expression - the 1280-page loop of experiment 197. So
    # `pmap_pte(kernel_pmap, 0x40000000)` finds a real PT entry and `pmap_map_bd` writes one PTE
    # into it. `flags` is 7, which is `VM_WIMG_IO`, not the 6 (`VM_WIMG_WCOMB`) that selects the
    # `pmap_map_bd_with_options` leg; the `assert` that would have checked that is `((void)0)` in
    # this RELEASE kernel (`assert.h:106`), so the only `panic`s left in the compiled `io_map` are
    # `round_page`'s overflow checks.
    #
    # The write that follows lands on this image's own first physical page, and that is XNU's design
    # rather than an accident: PA `0x00200000` is `gPhysBase`, in a real kernel that page *is* the
    # low-vectors page, and here it holds `_start` and the Mach-O header `entry_macho.s` writes -
    # both already consumed. So the prediction, read off the object's call order rather than the
    # source's, is `bcopy_phys` (`osfmk/arm/loose_ends.c`, not linked): two real `bcopy`s into the
    # new mapping and three real `ml_static_vtop` calls come first, and `CleanPoC_DcacheRegion` and
    # the `bcopy(running_signature, IOS_STATE, 8)` come after it.
    OSFMK_ARM_IO_MAP_OBJ=${STAGE90_ENTRY_IO_MAP_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_io_map.o}
    # `osfmk/arm/loose_ends.c`, named by experiment-207's `stub_hit=bcopy_phys`. 3671 bytes of text
    # and one function of it is the step: `bcopy_phys` at offset 0. The rest is a grab bag this image
    # gets for free - `bzero_phys`, the `ml_phys_read*`/`ml_phys_write*` family, `ml_probe_read`,
    # `ffs`/`fls`/`bcmp`/`memcmp`, `setbit`/`clrbit`/`testbit`, `copypv`, `copyin_validate`/
    # `copyout_validate`, `ml_thread_policy`.
    #
    # Cost: 7 resolved (`bcopy_phys bzero_phys copyin_validate copyout_validate ffs setbit testbit`)
    # and 1 added - `flush_dcache64`, which is `osfmk/arm/caches_asm.s` and which only `copypv`
    # references. Nothing on the boot path calls `copypv`, so the one new stub is a stub the run
    # will not reach.
    #
    # The prediction, again read off the object's call order rather than the source's: with
    # `bcopy_phys` real, `cpu_machine_idle_init` continues from call 13 to call 16 (the second
    # `bcopy_phys`, for `CpuDataEntries_paddr`) and stops at call 17,
    # `CleanPoC_DcacheRegion((vm_offset_t)phystokv((char *)gPhysBase), PAGE_SIZE)` - `caches_asm.s`
    # too, and also not linked. The two calls after it are a real `ml_static_vtop` and
    # `bcopy(running_signature, IOS_STATE, 8)` into the mapping experiment 207 created, so this
    # should be the run that finishes `cpu_machine_idle_init`'s exception-vector work. Its last call
    # is `clean_dcache`, which is `osfmk/arm/caches.o` (`osfmk_arm_caches.o`) - in the link *closure*
    # but not in this link, which are different things and this comment blurred them once.
    OSFMK_ARM_LOOSE_ENDS_OBJ=${STAGE90_ENTRY_LOOSE_ENDS_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_loose_ends.o}
    # `osfmk/arm/caches_asm.s`, named by experiment-208's `stub_hit=CleanPoC_DcacheRegion`. 596 bytes
    # of text, ten references, and nineteen globals - the whole cache-maintenance surface:
    # `CleanPoC_Dcache*`, `CleanPoU_Dcache*`, `FlushPoC_Dcache*`, `FlushPoU_Dcache`, `clean_dcache64`,
    # `flush_dcache64`, `clean_mmu_dcache` and the `invalidate_*` / `InvalidatePoU_Icache*` family.
    #
    # Cost: 7 resolved (`CleanPoC_Dcache CleanPoC_DcacheRegion CleanPoC_DcacheRegion_Force
    # CleanPoU_Dcache CleanPoU_DcacheRegion flush_dcache64 InvalidatePoU_Icache`) and 0 added. The
    # object's ten references are `EntropyData ExceptionVectorsBase fiqstack_top gPhysBase gPhysSize
    # gVirtBase intstack_top kdebug_enable` - all already real, from data.o and the asm layer - plus
    # `clean_dcache` and `flush_dcache`, which are already undefined names in this image and
    # therefore already stubs. This is assembled by the same clang pipeline as
    # `machine_routines_asm.o`, so it links the same way.
    #
    # The one function in it that will execute is `CleanPoC_DcacheRegion`, and it is worth having
    # read: its trip count comes from its `length` argument, not from `CTR`/`CCSIDR`:
    #
    #     mov  r1, r1, LSR #MMU_CLINE     // set cache line counter
    # ccdr_loop:
    #     mcr  p15, 0, r0, c7, c10, 1     // clean dcache line to PoC
    #
    # So with `CACHE_MODE = NONE` (caches off, as in every stage so far) it is 128 no-op `mcr`s. That
    # matters because the alternative shape - a loop whose count comes from a cache-geometry register
    # - is exactly experiment 196's defect. `phystokv((char *)gPhysBase)` is `0x00200000` here
    # (`gVirtBase == gPhysBase`), so what it cleans is the low-vectors page the two `bcopy_phys`
    # calls of experiment 208 just wrote.
    #
    # Prediction for the run: `clean_dcache`, which is `osfmk_arm/caches.o` and is not linked. It is
    # the *last* call of `cpu_machine_idle_init` (`cpu.c:594`), so this is the step that takes that
    # function to its final call - and the step after it is the one where it returns and `arm_init`
    # reaches `PE_init_platform(TRUE, &BootCpuData)`.
    OSFMK_ARM_CACHES_ASM_OBJ=${STAGE90_ENTRY_CACHES_ASM_OBJ:-$REPO_ROOT/out/xnu_asm_obj/caches_asm.o}
    # `osfmk/arm/caches.c`, named by experiment-209's `stub_hit=clean_dcache` - which is the *last*
    # call of `cpu_machine_idle_init` (`cpu.c:594`), so this is the object that lets that function
    # return. 2744 bytes of text, no data, no `.bss`, 26 references, 17 definitions: the
    # `platform_cache_*` family, `cache_xcall`/`cache_xcall_handler`/`cache_sync_page`,
    # `dcache_incoherent_io_flush64`/`_store64`, `flush_dcache_syscall`, and `clean_dcache`/
    # `flush_dcache` themselves.
    #
    # Cost: 11 resolved, 0 added - every one of its 26 references is already a real definition in
    # this image (the cache-maintenance functions experiment 209 linked, the pmap and machine-routines
    # symbols earlier steps paid for, and `kvtophys`/`flush_core_tlb`/`cpu_signal`/
    # `up_style_idle_exit`, which the asm layer brings). A free step.
    #
    # What `clean_dcache` does when it runs: it is the `phys == FALSE` leg and `cpu_cache_dispatch` is
    # still `(cache_dispatch_t) NULL` (`cpu_data_init` zeroed `BootCpuData` in experiment 168 and
    # nothing has set that field), so it is one `CleanPoC_DcacheRegion(addr, length)` over the
    # `cpu_data_t` `getCpuDatap()` returns from `TPIDRPRW`. No branch of it reaches `kvtophys` or the
    # dispatch callback.
    #
    # **The prediction is not `clean_dcache` - it is `early_random`, four calls later.** With this
    # object linked, `cpu_machine_idle_init` returns; `if (arm_diag & 0x8000)` is not taken
    # (`arm_diag` is only assigned from the `diag` boot arg at `arm_init.c:278`, and the command line
    # in `boot_args.c` has none); `PE_init_platform(TRUE, &BootCpuData)` runs *and completes*;
    # `cpu_timebase_init` and `fiq_context_init` are both call-free and real; and
    # `__stack_chk_guard = early_random()` is the first stub. That `PE_init_platform` completes is the
    # part that matters: its `else` branch is `pe_arm_init_interrupts(args); pe_arm_init_debug(args);`
    # and both bodies finish on this tree without a new symbol - `pe_arm_map_interrupt_controller`
    # returns 0 at its `gPicBase == 0` check because this tree deliberately has no
    # `interrupt-controller`/`master` node, and `pe_arm_init_debug` returns at its
    # `cpu-debug-interface` lookup. So the run after this one should be the first where **Phase 3's
    # `pe_arm_init_interrupts` is entered by XNU itself**.
    OSFMK_ARM_CACHES_OBJ=${STAGE90_ENTRY_CACHES_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_caches.o}
    # `osfmk/prng/random.c`, named by experiment-210's `stub_hit=early_random`. 2488 bytes of text,
    # 488 of data, 20 of `.bss`, 26 references.
    #
    # One of its definitions retires a *storage stand-in*, and it is the right-size-wrong-value kind
    # ([[mi4-stand-in-size-is-not-value]]): `entropy_data_t EntropyData = { .index_ptr =
    # EntropyData.buffer }` is an initialized struct, so a zero-filled array of the same 68 bytes is
    # not a neutral substitute. `erandom` - the other half of the same `static struct` - is *not* a
    # stand-in of any kind: `grep -c '^erandom$'` against the pre-step undefined list is 0, because
    # nothing in this image referenced it while `early_random` was a stub. Both are the object's own
    # symbols once the object is linked, at `EntropyData 0x0025e4f8` and `erandom 0x0025e53c`.
    #
    # **The prediction is `ccdrbg_factory_nisthmac`, and it is a *clean* stub hit.**
    # `early_random`'s disassembly, read rather than the source's statement order, is:
    #
    #       ldr  r0, [r4, #16]      ; erandom.seedset
    #       cmp  r0, #0
    #       beq  <slow path>        ; TAKEN - BSS is zero
    #       mov  r1, #64            ; sizeof(EntropyData.buffer)
    #       bl   PE_get_random_seed
    #       cmp  r0, #63
    #       bhi  <carry on>         ; NOT TAKEN
    #       bl   panic              ; "EntropyData needed %lu bytes, but got %u.\n"
    #
    # and `PE_get_random_seed` (`pexpert/gen/pe_gen.c:119`, real here since experiment 173) returns 0
    # unless `/chosen` has a `random-seed` property - this project's tree had none until this
    # experiment added one. So the *first* version of this step, which linked the object without
    # touching the tree, walked straight into the panic.
    #
    # **Where that panic actually goes was written down wrongly the first time and is corrected
    # here, because the wrong version would have made the run unreadable.** It was predicted as
    # `stub_hit=PEHaltRestart` on the reasoning that `panic`'s path is all real until
    # `PEHaltRestart(kPEPanicBegin)` in `iokit_Kernel_PlatformExpert.o`. Disassembling
    # `panic_trap_to_debugger` rather than reading `debug.c` top to bottom says otherwise:
    #
    #       96c: bl   ml_wants_panic_trap_to_debugger     ; returns FALSE, the `beq` is taken
    #       9a0: bl   current_processor                   ; CPUDEBUGGERCOUNT++ -> 1
    #       9b8: cmp  r0, #6
    #       9bc: bcc  a14                                 ; TAKEN - 1 < NESTEDDEBUGGERENTRYMAX+1
    #       9c4: bl   PEHaltRestart                       ; NOT REACHED
    #
    # `PEHaltRestart` sits behind `CPUDEBUGGERCOUNT > NESTEDDEBUGGERENTRYMAX`, and that count is
    # `PROCESS_DATA(...).db_entry_count` - zeroed `.bss`, so 1 after the increment, so the guard
    # skips it. `write_trace_on_panic` and `kdebug_enable` are both `B` in this image - zero - so
    # `kdbg_dump_trace_to_file`, the other stub on the path, is skipped too, and
    # `PE_arm_debug_panic_hook` is `B` - NULL - so its branch is not taken either. `panic` passes
    # `ctx = NULL` (`mov r3, #0` at 0x244), so the `handle_debugger_trap` branch is skipped. What
    # is left on the path is `DebuggerTrapWithState`, which after `DebuggerSaveState` executes
    # `TRAP_DEBUGGER` - `debug.c:121`, `#define TRAP_DEBUGGER __asm__ volatile("trap")`, an ARM
    # `udf` - and then `panic_stop()`.
    #
    # `panic_stop()` is `panic_spin_forever()` on ARM, not `pmCPUHalt`: `debug.c:133-137` defines
    # the `pmCPUHalt` form only `#if defined(__i386__) || defined(__x86_64__)` and everything else
    # gets `#define panic_stop() panic_spin_forever()`. Both it and `paniclog_append_noflush` are
    # real in this image - `for (;;) { }` after one write into the panic log.
    #
    # **So there is no undefined symbol anywhere on the panic path, and no stub could have been
    # hit.** The trap would have gone through VBAR - installed by the payload's `start.S` and not
    # affected by MMU state - into the payload's own `stage90_undef_c_handler`, which logs
    # `MI4IOS6_STAGE90 undef: addr=... lr=...` and returns to the next instruction, after which
    # XNU spins in `panic_spin_forever` forever. That is a hang, not a stub hit: the run would have
    # produced no `stub_hit` line at all, and the log's last XNU-side entry would have been the
    # `undef` breadcrumb. The device would have been recovered by the payload's watchdog and
    # dead-man, which are armed whether or not the boot is expected to hang.
    #
    # That is the reason this experiment does two things and says so plainly. The object is linked
    # because that is the rule - the previous run named `early_random` and this object defines it -
    # and the `/chosen` `random-seed` property is added in the *same* step because the object's own
    # code path is designed to be fatal without it: `random.c` says "Insufficient entropy is fatal.
    # We must fill the entire entropy buffer during initialization." Aiming a device run at a branch
    # whose stated purpose is to stop the machine is not a measurement, and the forward version
    # settles it just as well - if the run reports a stub past `early_random`, then
    # `PE_get_random_seed` returned 64 and `entropy_readall` ran, which is only possible if the seed
    # was there. The finding behind it is a gap in the *simulated handoff contract*, the third of
    # its kind after `state` on the cpu nodes (experiment 193) and `device_type = "timer"`.
    OSFMK_PRNG_RANDOM_OBJ=${STAGE90_ENTRY_PRNG_RANDOM_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_prng_random.o}
    # `osfmk/corecrypto/ccdbrg/src/ccdrbg_nisthmac.c`, named by experiment-211's
    # `stub_hit=ccdrbg_factory_nisthmac`. 1620 bytes of text, 24 of data, no `.bss`, 9 references,
    # 7 definitions.
    #
    # **What the stub skipped is not a return value but four function pointers and a size**, and the
    # step after this one is the first in fifty where the next call is *indirect*:
    #
    #     void ccdrbg_factory_nisthmac(struct ccdrbg_info *info, const struct ccdrbg_nisthmac_custom *custom)
    #     {
    #         info->size = sizeof(struct ccdrbg_nisthmac_state) + sizeof(struct ccdrbg_nisthmac_custom);
    #         info->init = init; info->generate = generate; info->reseed = reseed; info->done = done;
    #         info->custom = custom;
    #     };
    #
    # `ccdrbg_init` is not a symbol in this image and does not need to be - it is inlined, and in
    # `early_random` it is the `blx r7` at 0x214 where `r7 = drbg_info.init`. So linking this object
    # makes the factory real and the very next thing that happens is a call through the pointer it
    # just wrote.
    #
    # **The prediction is `cchmac_init`, read from `init`'s disassembly rather than from the source's
    # nesting, because the first call in the function is not the one that runs:**
    #
    #       8: ldr  r0, [r0, #20]   ; info->custom        18: ldr r2, [r0]   ; custom->di
    #      1c: strd r0, [r4]                             20: ldr r2, [r2]  ; di->output_size
    #      24: cmp  r2, #64                              28: bhi cc       ; NOT TAKEN
    #      3c: bls  50            ; success             48: bl  cc_clear   ; error path only
    #      88: bl   memset        ; keysize 0           9c: bl  memset     ; vsize 0
    #      b8: bl   hmac_dbrg_update  -> whose first call is cchmac_init (0x3b4)
    #
    # `cc_clear` is on the error path, and `hmac_dbrg_update`'s own body reaches `cc_clear` only
    # after `cc_cmp_safe` (0x5f8, against `cchmac_init` at 0x3b4). `cchmac_init` is defined by nothing
    # in this tree's linked set and referenced by nothing, so it does not exist as a stub today and
    # arrives as a new one in exactly this step - along with `cchmac_update`, `cchmac_final`,
    # `cchmac`, `cc_cmp_safe` and `cc_try_abort`, all six absent from the image entirely.
    #
    # **The thing to watch, and it is [[mi4-stand-in-size-is-not-value]] in its sharpest form so
    # far.** `erandom`'s initializer is `.drbg_custom = { .di = &ccsha1_eay_di, .strictFIPS = 0 }`,
    # and `ccsha1_eay_di` is, as of experiment 211, a *storage stub*. `init` reaches the digest
    # through it - `state->custom->di`, then `di->output_size`, `di->state_size`, `di->block_size` -
    # and a zeroed `ccdigest_info` reads 0 for all of them. The consequence is visible in the
    # disassembly above: `cmp r2, #64; bhi <error>` is **not taken**, so the code proceeds as if the
    # input were valid and the boot stops at some later missing symbol rather than at a failure that
    # names this one. So linking this object is not by itself enough for a working DRBG; the step
    # that links `osfmk_corecrypto_ccsha1_src_ccsha1_eay.o` is the one that gives `ccsha1_eay_di` a
    # value, and until then every field read through it is zero.
    OSFMK_CCDRBG_NISTHMAC_OBJ=${STAGE90_ENTRY_CCDRBG_NISTHMAC_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_ccdbrg_src_ccdrbg_nisthmac.o}
    # `osfmk/corecrypto/cchmac/src/cchmac_init.c`, named by experiment-212's `stub_hit=cchmac_init`.
    # 444 bytes of text, no data, no `.bss`, 4 references, 1 definition.
    #
    # **The prediction written here was `ccdigest_init`, and experiment 213's run disproved it.** It
    # is left in place rather than deleted because the way it was wrong is the point - see below -
    # and the run's actual result was a prefetch abort, not a stub hit:
    #
    #     exception: prefetch abort    ifar=0x00000000    ifsr=0x0000000f
    #
    # The old reasoning: `cchmac_init` calls `ccdigest_init` at 0x28 and `ccdigest_update` at 0x3c,
    # both before anything else, and both are absent from the image, so whichever comes first is the
    # stop. The *order* was right and the conclusion was still wrong, because neither call is
    # reached. What decides it is three instructions at the top, and they read the zeroed
    # `ccsha1_eay_di` - the stand-in this step's `di` argument still is:
    #
    #        8: ldr  r0, [r0, #8]   ; di->block_size = 0
    #       18: cmp  r0, r2          ; r2 = key_len = state->keysize = di->output_size = 0
    #       1c: bcs  94              ; TAKEN - 0 >= 0, so 0x28 and 0x3c are SKIPPED
    #       ...
    #      11c: ldr  r3, [r5, #24]   ; di->compress = NULL
    #      130: blx  r3              ; -> jump to address 0
    #
    # `ccdigest_info` is `{output_size, state_size, block_size, oid_size, oid, initial_state,
    # compress, final}`, so offset 24 is `compress` - and `di->initial_state` (offset 20) is NULL
    # too, which is why the `memcpy` at 0x10c was handed a NULL source and survived on its zero
    # length. `IFSR = 0x0F` is a *permission* fault at page granularity, not a translation fault, so
    # VA 0 is mapped and not executable rather than unmapped; `IFAR = 0` is the branch target.
    #
    # **The reason the prediction could be made at all is a method defect, and it is the useful part
    # of this step.** Every prediction in this project has come from an object's relocation table -
    # `objdump -dr | grep R_ARM_CALL`, or `nm -u` - and an indirect call has **no relocation**, so it
    # is invisible to both. This object has three (`blx r3` at 0x58, 0x130, 0x198) and they are not
    # in the list that was read. Experiments 207-212 were not wrong because the objects' stops
    # happened at direct calls reached first; `cchmac_init` is the first object where a call through
    # a pointer was reachable before any direct call, and the gap showed. The object's call graph is
    # `objdump -d` and a grep for `blx` / `ldr pc`, and - because this branch is taken on *data* -
    # the register values feeding its branches have to be read too.
    #
    # **The frontier is therefore a value, not a symbol: `ccsha1_eay_di` needs its definition.**
    # `osfmk_corecrypto_ccsha1_src_ccsha1_eay.o` - 4932 bytes, defining `ccsha1_eay_di` and
    # `sha1_compress`, referencing `ccdigest_final_64be` and `ccsha1_initial_state` - is what turns
    # the zeroed stand-in into an initialized `ccdigest_info`. That step is taken deliberately rather
    # than reached, which is what experiment 212's doc said it should be, and it ends the stretch of
    # runs that pass on empty parameters: three fields of the same stand-in have now been observed to
    # be zero, one per experiment (`output_size`, `initial_state`, `compress`).
    OSFMK_CCHMAC_INIT_OBJ=${STAGE90_ENTRY_CCHMAC_INIT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac_init.o}
    # `osfmk/corecrypto/ccsha1/src/ccsha1_eay.c`, **not** named by a stub hit - experiment 213's run
    # produced none, so this step is the first whose frontier is a *value* rather than a symbol. It is
    # the one experiment 212's doc said should be taken deliberately: 4932 bytes of text, defining
    # `ccsha1_eay_di` (`R`) and `sha1_compress` (`t`), referencing `ccdigest_final_64be` and
    # `ccsha1_initial_state`.
    #
    # **What it fixes, field by field.** `ccsha1_eay_di` is a `ccdigest_info`, whose layout is
    # `{output_size, state_size, block_size, oid_size, oid, initial_state, compress, final}`. Its
    # `.rodata` relocations say exactly what goes in the four pointer slots:
    #
    #     00000010 R_ARM_ABS32  .rodata.str1.1         ; oid         - the OID string
    #     00000014 R_ARM_ABS32  ccsha1_initial_state   ; initial_state - NEW, becomes a stub
    #     00000018 R_ARM_ABS32  .text                  ; compress      - sha1_compress, same object
    #     0000001c R_ARM_ABS32  ccdigest_final_64be    ; final         - NEW, becomes a stub
    #
    # Three of those six fields have each been observed to be zero by one run: `output_size` by
    # experiment 212's `bhi` guard, `initial_state` by the NULL source of a zero-length `memcpy` in
    # experiment 213, and `compress` by experiment 213's `blx r3` at 0x130 - the prefetch abort at
    # address 0. After this step `compress` is `sha1_compress`, which is a **leaf with no calls and
    # no branches to check** (`objdump -dr` on it shows zero R_ARM_* entries), so `cchmac_init`'s
    # indirect call lands in real code for the first time.
    #
    # **The prediction is `stub_hit=cchmac_update`**, made the corrected way - disassembly plus the
    # register values, not the relocation list. With `di` non-zero, `cchmac_init`'s first branch goes
    # the other way (`block_size` 64 vs `key_len` 20, so `bcs 0x94` is taken and the 0x94 arm runs
    # instead of the 0x28 arm), it reaches `blx r3` twice with `di->compress` now real, and returns.
    # `hmac_dbrg_update` then executes exactly four instructions before its next call:
    #
    #     3b4: bl   cchmac_init       ; real, returns
    #     3b8: ldr  r2, [r4, #16]     ; no branch and no indirect call anywhere between
    #     3c8: bl   cchmac_update     ; <-- STUB, the stop
    #
    # `cchmac_update` is still undefined: 4 bytes of object, a tail call to `ccdigest_update`, and
    # nothing has linked it. `ccdigest_final_64be` and `ccsha1_initial_state` arrive as new stubs in
    # this step and are *not* reached first - the first is `di->final`, which `cchmac_init` does not
    # use, and the second is only ever a `memcpy` source.
    OSFMK_CCSHA1_EAY_OBJ=${STAGE90_ENTRY_CCSHA1_EAY_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_ccsha1_src_ccsha1_eay.o}
    # `osfmk/corecrypto/cchmac/src/cchmac_update.c`, named by experiment-214's
    # `stub_hit=cchmac_update`. **Four bytes of text**, the smallest object this link will ever carry,
    # and its whole content is one instruction:
    #
    #     cchmac_update:  eafffffe  b  <ccdigest_update>       (R_ARM_JUMP24)
    #
    # So it resolves `cchmac_update` and adds nothing at all: `ccdigest_update` is *already* a stub in
    # this image, added by experiment 213. Checked the corrected way as well - a tail call is not a
    # `blx`, and the object has no other branch or indirect call in it.
    #
    # **The prediction is `stub_hit=ccdigest_update`**, and the `b` lands in the stub, so nothing past
    # it matters for this run. What is worth recording is what that stub will do once it is real,
    # because it is the first object in this sequence to make a *second* call through a pointer this
    # project has just repaired - 280 bytes, two indirect calls, and every input it reads is real as
    # of experiment 214:
    #
    #       28: udiv r1, r5, r2     ; 32-bit hardware divide, real on this core
    #       30: ldr  r3, [r7, #24]  ; di->compress
    #       3c: blx  r3             ; -> sha1_compress, real and a leaf
    #       5c: ldmib r7, {r0, r2}  ; di->state_size = 20, di->block_size = 64
    #
    # So the step after next should be the first in which a `ccdigest_*` helper runs to completion on
    # the device, and its prediction is to be made from that disassembly rather than from the
    # relocation list - which is the rule experiment 213 paid for.
    OSFMK_CCHMAC_UPDATE_OBJ=${STAGE90_ENTRY_CCHMAC_UPDATE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac_update.o}
    # `osfmk/corecrypto/ccdigest/src/ccdigest_update.c`, named by experiment-215's
    # `stub_hit=ccdigest_update` - which is also where the `b` of the step above landed. **280 bytes
    # of text, two indirect calls, one definition, one reference** (`memcpy`).
    #
    # This is the first object in the sequence whose correctness rests on a value this project
    # repaired in an earlier step rather than on a symbol it is linking now: `di` is the real
    # `ccsha1_eay_di` from experiment 214, so `di->compress` is `sha1_compress` (a leaf, no calls, no
    # branch), `di->state_size` is 20, `di->block_size` is 64, and the `udiv` at 0x28 is a hardware
    # divide that this core really has:
    #
    #       28: udiv r1, r5, r2     ; 32-bit hardware divide, real on this core
    #       30: ldr  r3, [r7, #24]  ; di->compress
    #       3c: blx  r3             ; -> sha1_compress, real and a leaf
    #       5c: ldmib r7, {r0, r2}  ; di->state_size = 20, di->block_size = 64
    #
    # Its one reference is `memcpy`, real since long before this sequence, and its indirect calls go
    # through `di` - so this is the first object since 212 with **no stubbed symbol on its own path at
    # all**. Every input it reads is real; it is the value argument from experiment 214 that made it
    # so, which is why that step was taken deliberately rather than waited for.
    #
    # **The prediction is `stub_hit=cchmac_final`**, made the corrected way - from the disassembly of
    # the *caller*, plus the register values feeding its branches, not from a relocation list. With
    # `ccdigest_update` real, `hmac_dbrg_update` continues past its call at 0x3c8, and the arms
    # converge: `cchmac_final` is reached unconditionally, whichever of the `da`/`db`/`dc` emptiness
    # tests at 0x3e0/0x404 comes out. Both arms are `bl`, so this is visible as a direct call - the
    # two are `bl 0x4b4` after the fall-through and `bl 0x4c0` on the `bne 0x49c` arm:
    #
    #     3dc: bl   cchmac_update     ; real now (a tail call into ccdigest_update)
    #     3e0: cmp  r9, #0            ; the da/db/dc emptiness tests
    #     404: bne  420               ; -> 418: bl cchmac_update   (real)
    #     49c: bne  4bc               ; -> 4c0: bl cchmac_final    (STUB)
    #     4a4: bl   cchmac_update     ; real
    #     4b4: bl   cchmac_final      ; <-- STUB, the stop, on the fall-through arm
    #
    # The reason the two arms are worth writing out is that the *previous* step's answer would have
    # been wrong had it been read off a relocation list, and there is no way to tell the two cases
    # apart except by reading the disassembly: here both arms happen to lead to the same symbol, so
    # the old method would have got this one right by luck.
    #
    # `cchmac_final` is 120 bytes and references `memcpy`; it is the last of the four `cchmac_*`
    # functions. After it, `di->final` is `ccdigest_final_64be` - which `nm` has reported as a function
    # stub since experiment 214 - and that is the prediction after this one, subject to the same rule.
    OSFMK_CCDIGEST_UPDATE_OBJ=${STAGE90_ENTRY_CCDIGEST_UPDATE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_ccdigest_src_ccdigest_update.o}
    # `osfmk/corecrypto/cchmac/src/cchmac_final.c`, named by experiment-216's `stub_hit=cchmac_final`.
    # **120 bytes of text, no data, no `.bss`, 1 definition, 1 reference (`memcpy`), 2 indirect calls**
    # - 0x24, and a tail `bx r3` at 0x74 that is unreachable while the first one is a stub.
    #
    # The step it took to get here was the first in this sequence whose object had *no* stubbed symbol
    # on its own path: `ccdigest_update` ran to completion, made an indirect call into real code
    # (`sha1_compress`) and returned, and did a `udiv` on the way. Its own stop was therefore in its
    # *caller*, which is the same shape this step has.
    #
    # **The prediction is `stub_hit=ccdigest_final_64be`**, and unlike the last two this one needs no
    # arm analysis at all, because the object has no conditional branch before its first indirect
    # call:
    #
    #      0: push  {r4, r5, r6, lr}
    #      4: mov   r6, r0            ; r6 = di     (cchmac_final(di, ctx, out) - di first)
    #      8: ldr   r0, [r0, #4]      ; di->state_size = 20
    #      c: ldr   r3, [r6, #28]     ; di->final   = ccdigest_final_64be
    #     14: add   r0, r1, r0        ; ctx + state_size: the state area inside the hmac ctx
    #     1c: add   r2, r0, #8
    #     20: mov   r0, r6            ; -> di
    #     24: blx   r3               ; <-- STUB, the stop
    #
    # The argument order is checked against the header rather than the call site, because this project
    # has been wrong about argument order before: `void cchmac_final(const struct ccdigest_info *di,
    # cchmac_ctx_t ctx, unsigned char *digest)` (`EXTERNAL_HEADERS/corecrypto/cchmac.h:84`). So `r6` is
    # `di`, `[r6, #28]` is `di->final`, and it is not a field of the context. The caller agrees:
    # `hmac_dbrg_update` sets `r0 = r5, r1 = r8` at 0x4ac/0x4b0 immediately before `bl cchmac_final`,
    # and `cchmac_init` reads `di->block_size` and `di->state_size` out of the same `r5`.
    #
    # `cchmac_final` resolves one symbol and adds nothing - `memcpy` is long since real. The step after
    # it will be `osfmk_corecrypto_ccsha1_src_ccdigest_final_64be.o` (500 bytes, **zero undefined
    # symbols**, two indirect calls both through `di->compress`), the object that finally makes
    # `di->final` real; its stop will be past itself rather than inside itself, so its prediction will
    # come from `hmac_dbrg_update` again.
    OSFMK_CCHMAC_FINAL_OBJ=${STAGE90_ENTRY_CCHMAC_FINAL_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac_final.o}
    # `osfmk/corecrypto/ccsha1/src/ccdigest_final_64be.c`, named by experiment-217's
    # `stub_hit=ccdigest_final_64be` - the object that finally makes `di->final` real. **500 bytes of
    # text, no data, no `.bss`, 1 definition, 0 undefined symbols, 2 indirect calls** (0xb4 and 0x19c),
    # both through `di->compress`, which is `sha1_compress`.
    #
    # It lives in the `ccsha1` directory although what it defines is a generic digest helper; the
    # frontier rule names the object, not the directory, which is why the object name is spelled out
    # here rather than derived.
    #
    # The step that got here was the first in this sequence with **nothing new executing**: with
    # `cchmac_final` real and `ccdigest_final_64be` still a stub, the run stopped five instructions
    # into `cchmac_final` - its `blx r3` at 0x24, with `r3 = [di + 28]` - so that step's measurement
    # was its own accounting and nothing else. Worth remembering when reading a future cost table: a
    # clean stub hit one symbol further along does not by itself mean anything new ran.
    #
    # **The prediction is `stub_hit=cchmac`.** After `cchmac_final` returns, `hmac_dbrg_update`
    # resumes at 0x4b8 or 0x4c4, and the two arms differ - but they call the *same* symbol, so this
    # prediction needs the arm analysis and does not care what its outcome is:
    #
    #     4b4: bl   cchmac_final      ; real now, returns
    #     4b8: b    4d0               ; ---- arm A ----
    #     4c0: bl   cchmac_final      ; real now, returns
    #     4c4: orr  r0, r6, r9        ; ---- arm B: (da_len | db_len)
    #     4c8: cmp  r0, #1
    #     4cc: bne  578               ; if not 1 -> arm B's own path
    #     4f8: bl   cchmac            ; <-- STUB, arm A's stop
    #     578: mov  r7, #0
    #     5a8: bl   cchmac            ; <-- STUB, arm B's stop
    #
    # `cchmac` is the one-shot init/update/final wrapper (`osfmk_corecrypto_cchmac_src_cchmac.o`, 128
    # bytes), still a 12-byte stub at 0x0023967c. This is the only kind of "safe" that is not the same
    # as experiment 216's: there, both arms converged and the arm did not matter; here the arms are
    # different code and happen to end at the same symbol. If this prediction is wrong, the mistake is
    # in reading the disassembly, not in a guess about an argument.
    #
    # The step after this one is the one that matters more: with `ccdigest_final_64be` real,
    # `cchmac_final` runs to completion for the first time - its `memcpy`, its arithmetic block, and
    # both of its calls through `di->final`, the second a tail call that returns straight to
    # `hmac_dbrg_update`. That is the first time the whole HMAC finalisation executes on this device.
    # The qualifier from experiment 214 still stands: the state it finalises started from
    # `ccsha1_initial_state`, still a zeroed stub, so the *machinery* completes and the *value* is not
    # SHA-1's.
    OSFMK_CCDIGEST_FINAL_64BE_OBJ=${STAGE90_ENTRY_CCDIGEST_FINAL_64BE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_ccsha1_src_ccdigest_final_64be.o}
    # `osfmk/corecrypto/cchmac/src/cchmac.c`, named by experiment-218's `stub_hit=cchmac`. **128
    # bytes of text, no data, no `.bss`, 1 definition, 4 references** - `cchmac_init`, `cchmac_update`
    # and `cchmac_final`, all three real as of experiment 217, and `cc_clear`, still a 12-byte stub.
    #
    # The step that got here is the first in this stretch whose stop is *evidence of completion*
    # rather than merely of arrival: `hmac_dbrg_update` can only reach `cchmac` if `cchmac_final`
    # returned, and `cchmac_final` returns only if both of its calls through `di->final` entered real
    # code and returned. Each of those is a full generic digest finalisation with two `blx r3` calls
    # into `sha1_compress`. So that run executed the complete HMAC-SHA1 finalisation - inner digest,
    # the `memcpy` into the outer context, outer digest - while `ccsha1_initial_state` was still a
    # zeroed stub, which is the qualifier that keeps the claim to the *machinery* and not the value.
    #
    # **The prediction is `stub_hit=cc_clear`**, and it needs nothing beyond the object's shape,
    # because `cchmac` has no branches at all:
    #
    #      0: push  {r4, r5, r6, sl, fp, lr}
    #     2c: sub   r5, sp, r0        ; the HMAC context, VLA-sized on the stack
    #     34: mov   r0, r4            ; di
    #     38: mov   r1, r5            ; ctx
    #     3c: bl    cchmac_init       ; real
    #     50: bl    cchmac_update     ; real (a tail call into ccdigest_update)
    #     60: bl    cchmac_final      ; real since 217
    #     70: add   r0, r0, #12       ; cchmac_ctx_size, for the wipe
    #     74: bl    cc_clear          ; <-- STUB, the stop
    #     7c: pop   {r4, r5, r6, sl, fp, pc}
    #
    # Straight-line code means no arm analysis and no data dependence, which makes this the least
    # interesting prediction in the sequence and the most certain. The step after it will be
    # `osfmk_corecrypto_cc_src_cc_clear.o`, the first object in this stretch that is not in the
    # `cchmac`/`ccdigest` family.
    OSFMK_CCHMAC_OBJ=${STAGE90_ENTRY_CCHMAC_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_cchmac_src_cchmac.o}
    # `osfmk/corecrypto/cc/src/cc_clear.c`, named by experiment-219's `stub_hit=cc_clear`. **20 bytes
    # of text, no data, no `.bss`, 1 definition, 1 reference**, and the whole of it is an argument
    # shuffle into a tail call:
    #
    #      0: mov  r3, r0        ; n
    #      4: mov  r0, r1        ; p     cc_clear(n, p) -> memset_s(p, n, 0)
    #      8: mov  r1, r3
    #      c: mov  r2, #0
    #     10: b    <memset_s>    ; (R_ARM_JUMP24)
    #
    # The step that got here is the first in which a whole one-shot HMAC ran: the stop was inside
    # `cchmac`, at its closing wipe, so `cchmac_init`, `cchmac_update` and `cchmac_final` had all
    # returned - a complete HMAC-SHA1 from key to digest with every function on the path real, and
    # with `ccsha1_initial_state` still a 20-byte zeroed stub, so the machinery complete and the value
    # not SHA-1's.
    #
    # **The prediction is `stub_hit=memset_s`.** This is the case the frontier rule was built for: the
    # object named by the previous run reaches for a *new* symbol the moment it executes, and that
    # symbol is not in the image - `grep -w memset_s out/stage90/xnu_arm_entry_undef.txt` is empty
    # today. So this step **adds** an undefined symbol for the first time since experiment 214, and the
    # count stays at 364: one resolved, one added.
    #
    # `memset_s` is not `memset`. The image already has `memset` and `secure_memset` at the *same*
    # address 0x00202dac - one body, two names, from `osfmk/arm/bzero.s` - and `memset_s` is a
    # different function in `osfmk/kern/memset_s.c`: C11 Annex K shape, clamps `n` to `smax`, returns
    # EINVAL/E2BIG/EOVERFLOW, and exists so the wipe cannot be optimised away. Its own reference is
    # `secure_memset`, which is already real, so linking it in the step after this resolves one symbol
    # and adds none, and the stop after that moves on to `cc_cmp_safe` at 0x5e4.
    #
    # Worth writing out, because the run will not stop where a reading of the call list suggests: with
    # `cc_clear` real, control returns to `cchmac` at 0x78, and `hmac_dbrg_update` continues at 0x4fc
    # with two further `cchmac` invocations (0x5a8 and 0x5cc) *before* `cc_cmp_safe` - and each of
    # those calls `cc_clear` again and therefore `memset_s` again.
    OSFMK_CC_CLEAR_OBJ=${STAGE90_ENTRY_CC_CLEAR_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_cc_src_cc_clear.o}
    # `osfmk/kern/memset_s.c`, named by experiment-220's `stub_hit=memset_s` - reached not from the
    # caller's next missing call but from inside `cchmac`, whose closing wipe is `cc_clear` and whose
    # `cc_clear` is a tail call to this. **80 bytes of text, no data, no `.bss`, 1 definition, 1
    # reference**, and that reference - `secure_memset` - is already real at 0x00202dac, where plain
    # `memset` shares the address. So this object has nothing missing on its own path and cannot stop
    # anywhere itself.
    #
    # It is the C11 Annex K shape, and its whole point is that the wipe cannot be optimised away:
    #
    #     int memset_s(void *s, size_t smax, int c, size_t n)
    #     {
    #             if (s == NULL) return EINVAL;
    #             if (smax > RSIZE_MAX) return E2BIG;
    #             if (n > smax) { n = smax; err = EOVERFLOW; }
    #             secure_memset(s, c, n);      /* osfmk/arm/bzero.s - real */
    #             return err;
    #     }
    #
    # **The prediction is `stub_hit=cc_cmp_safe`**, and unlike the last one this is a plain list-walk:
    # the object resolves cleanly, so the stop is the caller's next *missing* call, not a call the
    # object makes. After `memset_s` returns, the first `cc_clear` (inside the first `cchmac`)
    # completes, `cchmac` pops at 0x78, `hmac_dbrg_update` resumes at 0x4fc, and every remaining call
    # on that path is real - `cchmac_init` 0x510, `cchmac_update` 0x524/0x538/0x564, `cchmac_final`
    # 0x574, `cchmac` 0x5a8 and 0x5cc - until `cc_cmp_safe` at 0x5e4.
    #
    # **Two things to have written down before the step after that**, because it is the first in this
    # sequence that could end somewhere other than a stub hit, and the first that could execute NEON:
    #
    #   1. `cc_cmp_safe` is called with `state->vsize` (20) and two 20-byte V buffers, and its result
    #      forks at 0x5e8. On the *equal* arm the code wipes the state and calls `cc_try_abort`, whose
    #      one reference is `panic` - real since the panic object was linked - and XNU's ARM `panic`
    #      reaches `TRAP_DEBUGGER` (a `udf`) and then `panic_spin_forever`. That would show up as an
    #      `exception:` line rather than a `stub_hit=`, and it is the DRBG's own FIPS 140-2 4.9.2
    #      conditional test failing on purpose (`ccdrbg_nisthmac.c:211`, `:440`), not a defect.
    #   2. `cc_cmp_safe.o` contains NEON (`vmov.i32 q8`, `vld1.8`, `veor`, `vorr`) on the path taken
    #      when the length is 32 or more (`cmp r0,#32; bcc 0xc4`). The call here passes 20, so the
    #      scalar path runs and no NEON instruction executes - but a later call with 32 or more would
    #      be the first NEON code in this project's history to run, and whether CPACR/FPEXC permit it
    #      in this CPU state is a question nothing has asked yet.
    OSFMK_MEMSET_S_OBJ=${STAGE90_ENTRY_MEMSET_S_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_memset_s.o}
    # `osfmk/corecrypto/cc/src/cc_cmp_safe.c`, named by experiment-221's `stub_hit=cc_cmp_safe`.
    # **376 bytes of text, no data, no `.bss`, 1 definition, 0 undefined symbols** - so linking it
    # leaves *nothing* missing anywhere in the DRBG, and this is the last object of the corecrypto
    # stretch. It is a constant-time compare: the XOR-accumulate loop, then a single test of the
    # accumulator, with NEON used only for lengths of 32 or more (`cmp r0,#32; bcc 0xc4`). Both calls
    # in play pass `state->vsize` = 20, so the scalar path runs and no NEON instruction executes.
    #
    # The step that got here ran the whole HMAC_DRBG instantiate sequence up to this compare: five or
    # more complete HMAC-SHA1 computations in one boot, with the stop one call past them. What is left
    # of the DRBG after this is the compare, a return, and the same compare in `generate`.
    #
    # **The prediction is `stub_hit=bsd_scale_setup`** - the longest prediction in this sequence, and
    # the first that says the PRNG is *finished*. Every step was read rather than assumed:
    #
    #   1. cc_cmp_safe returns non-zero -> 0x5ec bne 0x608
    #   2. hmac_dbrg_update returns r7 = 0 (0x578 mov r7,#0; 0x60c mov r0,r7) = CCDRBG_STATUS_OK
    #   3. `init` returns OK - 0xb8 is its last call, only a pop follows
    #   4. early_random's two `if (rc != CCDRBG_STATUS_OK) panic(...)` checks pass
    #   5. early_random -> ccdrbg_generate -> `generate`; addl_len = 0 so no hmac_dbrg_update, and
    #      bytesLeft = 0, so 0x210 -> 0x254 -> 0x25c cchmac (real) -> 0x290 cc_cmp_safe (real now)
    #      -> copies 8 bytes out -> 0x2b4 return
    #   6. early_random returns the 8 bytes; arm_init stores them in `__stack_chk_guard`:
    #          328: bl  early_random
    #          330: bic r0, r0, #0xff00     ; the stack canary comes from early_random
    #          338: str r0, [r1]            ; __stack_chk_guard
    #          340: bl  machine_startup
    #   7. machine_startup (real, 0x206b84): four PE_parse_boot_argn (real), then
    #          206c6c: bl kernel_bootstrap - unconditional, the last thing it does
    #   8. kernel_bootstrap (real, 0x20d560): _consume_printf_args (real, no calls),
    #      PE_parse_boot_argn x4 and PE_parse_boot_arg_str (all real - the last calls DTLookupEntry
    #      and DTGetProperty, both real), and then the first missing symbol:
    #          20d604: bl bsd_scale_setup   <-- STUB at 0x00239900
    #
    # The only conditional branch between `kernel_bootstrap`'s entry and that call skips a store, not
    # a call, so the stop is not data-dependent.
    #
    # **The caveat, and it is new: this prediction's alternative outcome is a panic.** Both FIPS
    # compares branch to `cc_clear` + `cc_try_abort` on the *equal* result, and `cc_try_abort`'s single
    # reference is `panic`, which is real in this image; XNU's ARM `panic` reaches `TRAP_DEBUGGER` (a
    # `udf`) and then `panic_spin_forever`. So a run ending in an `exception:` line rather than a
    # `stub_hit=` would mean the DRBG's own health check fired - a finding, not a defect - and a run
    # stopping at some *other* symbol in the DRBG would mean the reading above is wrong, with the
    # symbol naming where.
    OSFMK_CC_CMP_SAFE_OBJ=${STAGE90_ENTRY_CC_CMP_SAFE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_corecrypto_cc_src_cc_cmp_safe.o}
    # `bsd/dev/unix_startup.c`, named by experiment-222's `stub_hit=bsd_scale_setup` - the first
    # object in this sequence that is not XNU's ARM or corecrypto layer, and the first that is reached
    # from `kernel_bootstrap` rather than from `arm_init`.
    #
    # **1291 bytes of text, 96 of data, 48 of `.bss`, 4 definitions, 21 references**, and the whole of
    # `bsd_scale_setup` is one instruction:
    #
    #     0000045c <bsd_scale_setup>:
    #      45c: eafffffe  b  0 <bsd_exec_setup>        (R_ARM_JUMP24)
    #
    # The step that got here is the one that finished the PRNG: with `cc_cmp_safe` real, everything
    # from `hmac_dbrg_update`'s FIPS compare up to `kernel_bootstrap` returned, and the stop is inside
    # `kernel_bootstrap`. `arm_init` now completes; `__stack_chk_guard` is set from `early_random`'s
    # return value; `machine_startup` runs. The qualifier still holds and is not weakened by any of it:
    # `ccsha1_initial_state` is 20 zero bytes, so the canary is a deterministic function of the device
    # tree and the timebase, not of entropy - the machinery is real and the value is not SHA-1's.
    #
    # **The prediction is `stub_hit=bsd_exec_setup`**, and it needs no arm analysis: the tail call is
    # unconditional and `bsd_exec_setup` is defined by `bsd_kern_bsd_init.o` (`bsd/kern/bsd_init.c`),
    # which is not linked yet. So this step **adds** the symbol the next run stops on, the same shape
    # as experiment 220's `cc_clear` -> `memset_s`.
    #
    # The cost table for this step will not look like the last seven, and deliberately: this object
    # defines **four** symbols (`bsd_startupearly`, `bsd_mbuf_cluster_reserve`, `bsd_bufferinit`,
    # `bsd_scale_setup`) and references 21, of which five (`__aeabi_uldivmod`, `bzero`, `panic`,
    # `PE_get_default`, `PE_parse_boot_argn`) are already real and the other sixteen are either already
    # stubs from earlier objects or arrive now - among them `bsd_exec_setup`, `bufinit`,
    # `kernel_memory_allocate`, `kmem_suballoc`, `sysctl_handle_int`, and the storage the BSD side
    # needs (`kernel_map`, `mb_map`, `mbutl`, `desiredvnodes`, `sane_size`, `nmbclusters`,
    # `mbuf_default_ncl`, `buf_headers`, `sysctl__kern_children`, `tcp_recvspace`, `tcp_sendspace`).
    # The build reports 300 -> 304 function stubs and 61 -> 67 storage stubs, and the entry image's
    # file size moves for the first time since experiment 214. The undefined count goes **up**, which
    # is expected and is not a regression.
    OSFMK_BSD_DEV_UNIX_STARTUP_OBJ=${STAGE90_ENTRY_BSD_DEV_UNIX_STARTUP_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/bsd_dev_unix_startup.o}
    # `bsd/kern/bsd_init.c`, named by experiment-223's `stub_hit=bsd_exec_setup`. **3653 bytes of text,
    # 80 of data, 2720 of `.bss`** - the largest single object this link has taken on - and the first
    # step whose frontier is a *group*: it defines `bsd_exec_setup` (the symbol that stopped the last
    # run) plus `bsd_init`, `bsd_early_init`, `bsd_autoconf`, `bsdinit_task` and `bsd_utaskbootstrap`,
    # and `boothowto`, `cmask`, `domainname`, `dumpdev` and their neighbours as data. Only
    # `bsd_exec_setup` is reached.
    #
    # **The prediction is `stub_hit=kernel_debug_string_early`**, and it rests on `bsd_exec_setup`
    # being a **leaf** - it calls nothing at all:
    #
    #     c58: cmp  r0, #7
    #     c5c: bhi  c7c                      ; r0 > 7 -> the default arm; `kernel_bootstrap` passes 0
    #     c60: ...  two switch tables indexed by r0
    #     c7c: movw r1, #0x2000 / movw r0, #0x201 / movt r1, #0x844
    #     c88: str  r1, [bsd_pageable_map_size]
    #     c94: str  r0, [bsd_simul_execs]
    #     ca0: bx   lr
    #
    # Every arm writes two of this object's own `.bss` variables and returns. So control lands back in
    # `kernel_bootstrap` at 0x20d608, where there is no call and no branch between it and the next
    # call:
    #
    #     20d608-20d678: movw/movt/ldr/str/add/asr   ; the cluster and scale arithmetic, straight line
    #     20d67c: bl kernel_debug_string_early       <-- STUB, the stop
    #
    # `kernel_debug_string_early` stays a stub: this object does not mention the symbol at all (`nm -u`
    # is empty for it), so whatever defines it, this step does not.
    #
    # **This is the step where the ledger gets big.** `bsd_kern_bsd_init.o` references **132 symbols,
    # of which 28 are already in the image and 104 are not**, so its build adds on the order of a
    # hundred stubs at once - and `.bss` grows by 2720 bytes while the entry image's file size, which
    # had been constant for nine steps, moves again. The headroom below `topOfKernelData` is large
    # (1.6 MB), so there is room; but from here the image's size and `.bss` bounds are numbers to read
    # in every table rather than to assume, because a single step can now move them.
    BSD_KERN_BSD_INIT_OBJ=${STAGE90_ENTRY_BSD_KERN_BSD_INIT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/bsd_kern_bsd_init.o}
    # `bsd/kern/kdebug.c`, named by experiment-224's `stub_hit=kernel_debug_string_early`. **21729 bytes
    # of text, 312 of data, 136 of `.bss`, 112 definitions, 104 references** - much the largest object
    # this link has taken on, and the only object in the build that defines the symbol.
    #
    # The whole of what is reached is four lines of C (bsd/kern/kdebug.c:1328) that stuff the message
    # into four `uintptr_t`s and hand them to `KERNEL_DEBUG_EARLY`, which compiles to nothing in this
    # configuration - there is no call after the `strncpy`, only `add sp, sp, #16` and `pop {r4, pc}`:
    #
    #     dc8: push  {r4, lr} / dcc: sub sp, sp, #16
    #     dd0: vmov.i32 q8, #0                  <-- NEON
    #     ddc: vst1.64 {d16-d17}, [r0]          <-- NEON
    #     de4: bl    strlen                     ; real in this image (0x00204668)
    #     df8: bl    strlen                     ; the MIN(sizeof(arg), strlen(message)) pair
    #     e08: bl    strncpy                    ; real in this image (0x002046c4)
    #     e10: pop   {r4, pc}
    #
    # So the function has no stubbed dependency of its own and cannot stop inside itself.
    #
    # **This is the first NEON code in this project's history that will execute**, and unlike
    # `cc_cmp_safe`'s vector path - which needs a length of 32 and has never been taken - it is
    # unconditional, the first instruction pair of the function. It will not trap: `osfmk/arm/start.s`'s
    # `join_start` enables CP10/CP11 in CPACR and `join_start_1` sets `FPEXC.EN`, both on the path the
    # run has already taken, and both present in this image:
    #
    #     200354: mrc p15, 0, r2, c1, c0, 2     ; read CPACR
    #     20035c: orr r2, r2, r3, lsl #20       ; 0xF << 20: coprocessors 10 and 11
    #     200360: mcr p15, 0, r2, c1, c0, 2
    #     20036c: beq 2003a0 <join_start_1>     ; taken when invoked from _start
    #     2003a0: vmrs r2, fpexc / orr r2, r2, #0x40000000 / vmsr fpexc, r2
    #
    # (`_start` reaches `join_start` by a direct branch, and `arm_init` is called from `start.s` after
    # it returns - and experiment 222's run demonstrably reached `arm_init`.)
    #
    # **The prediction is `stub_hit=vm_mem_bootstrap`.** `kernel_debug_string_early` returns into
    # `kernel_bootstrap`, whose very next statement is `bl vm_mem_bootstrap` with nothing in between,
    # and `vm_mem_bootstrap` is undefined in this image while this object does not define it (a scan of
    # its 104 references finds no mention of it) - so it stays a 12-byte stub. The argument the stub
    # receives is a second, independent statement of the same prediction: `r0` at the stop is the string
    # `"vm_mem_bootstrap"`, and `kernel_bootstrap`'s log strings sit in the object's own `.rodata` in
    # call order - `"vm_mem_bootstrap"`, `"cs_init"`, `"vm_mem_init"`, `"telemetry_init"` - so the run
    # prints the name of the routine it is about to call and then stops on that routine's stub.
    #
    # **The build measured: 9 resolved (8 function stubs and `kdebug_enable`), 43 added (42 function
    # stubs and `kperf_kdebug_active`), 472 -> 506 undefined, stubs 392fn/80st -> 426fn/80st, text
    # 296600 -> 319332, image 404464 -> 421240, `.bss` 105688 -> 105920, headroom 1588152 bytes, and
    # the run stopped on `vm_mem_bootstrap` with kv 0x1b.** The one thing the object list did not
    # predict is `kdebug_enable`: `bsd_kern_kdebug.o` defines it, and this file's stand-in for it - in
    # `entry_stubs.c` since before `arm_init` was real, because `start.s` references the name - became
    # a duplicate definition. Retiring it is why the *empty-object* build has 472 undefined rather than
    # 471, and why 9 symbols resolve instead of 8. See `entry_stubs.c` for why the replacement is
    # equivalent and why that is worth stating.
    BSD_KERN_KDEBUG_OBJ=${STAGE90_ENTRY_BSD_KERN_KDEBUG_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/bsd_kern_kdebug.o}
    # `osfmk/vm/vm_init.c`, named by experiment-225's `stub_hit=vm_mem_bootstrap`. **994 bytes of text,
    # no data, 24 of `.bss`, 29 definitions, 24 references.** It defines `vm_mem_bootstrap` (280 bytes)
    # and `vm_mem_init`, plus `vm_kernel_ready`, `kmem_ready`, `kmem_alloc_ready`, `kmapoff_pgcnt`,
    # `kmapoff_kaddr`, `zlog_ready`, `vm_min_kernel_address` and `vm_max_kernel_address`, none of which
    # are in the image today.
    #
    # `vm_mem_bootstrap` is a straight run of initialisation calls, each preceded by
    # `kernel_debug_string_early` of its own name - which experiment 225 made real, so those all
    # return and print nothing:
    #
    #     10: bl kernel_debug_string_early
    #     1c: bl vm_page_bootstrap(sp+12, sp+8)   <-- real, from osfmk_vm_vm_resident.o (exp 195)
    #     28: bl kernel_debug_string_early
    #     2c: bl zone_bootstrap                   <-- ABSENT, and not referenced by the image
    #     ...
    #
    # **Correction, recorded after experiment 226. The prediction below was `zone_bootstrap` and it
    # was wrong; the stop was `vm_compressor_init_locks`.** The reasoning that follows is kept in
    # place because of how it fails, which is twice over:
    #
    #   - **`vm_page_bootstrap` is `0x888` = 2184 bytes, not 888.** `nm -S -P` prints sizes in
    #     hexadecimal with no prefix, and that was read as decimal - so the disassembly window used
    #     was `0x00218b50..0x00218ec8`, the first 40% of the function. It has **27 direct calls, not
    #     five**, and `vm_map_steal_memory`, which experiment 227 is about, was outside the window.
    #   - Even with all 27 in hand, "the callee is real" is not "the callee has nothing missing".
    #     `vm_page_init_lck_grp` is real; its last instruction is `R_ARM_JUMP24 vm_compressor_init_locks`,
    #     a tail call into an object the image did not have. The reading was one level down and the
    #     path was two.
    #
    # Both of those are now what `tools/xnu_entry_callwalk.py` does instead of a hand. The paragraph
    # below about the NEON is still right, and the NEON in question ran in experiment 227 rather than
    # in 226 or 225 - 226 stopped at `0x00241330`, which is before the first `vld1.64` at `0x00218c30`.
    #
    # `vm_page_bootstrap` is also where the second batch of NEON executes - `vld1.64 {d16-d17},
    # [r1 :128]`, `vdup.32`, `vshl.s32` and an aligning `vst2.32 {d24-d27}, [r1 :64]!` in a loop.
    # Experiment 225 proved the first NEON (in `kernel_debug_string_early`) does not trap, because
    # `start.s`'s `join_start` sets CPACR's CP10/CP11 and `join_start_1` sets `FPEXC.EN`; this step
    # exercises the same grant, including the alignment requirements, which are met by construction
    # (the `:128` load is the 16-byte-aligned literal table at 0x00218dc0; the `:64` store base is
    # 0x0026c038 + r7 stepping by 64).
    OSFMK_VM_VM_INIT_OBJ=${STAGE90_ENTRY_OSFMK_VM_VM_INIT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_vm_vm_init.o}
    # `osfmk/vm/vm_compressor.c`, named by experiment-226's `stub_hit=vm_compressor_init_locks`.
    # **23260 bytes of text, 144 of data, 16248 of `.bss`, 206 definitions, 100 references** - the
    # largest object this link has taken on, and almost all of it arrived unused the last time a step
    # this size happened.
    #
    # **Experiment 226's prediction was `zone_bootstrap` and it was wrong.** The stop was
    # `vm_compressor_init_locks`. The reasoning that had predicted `zone_bootstrap` is in the
    # `OSFMK_VM_VM_INIT_OBJ` block above, along with what was wrong with it.
    #
    # `tools/xnu_entry_callwalk.py` now does that transitive walk over the linked ELF, and it is what
    # found this: run against experiment 226's own image it names `vm_compressor_init_locks`, and
    # against that experiment's empty-object image it names `vm_mem_bootstrap`, which is what
    # experiment 225 actually printed. Both are cases with known answers, so the tool is checked
    # against the device rather than against itself.
    #
    # **The walk was run before the device was touched, it said `stub_hit=vm_map_steal_memory`, and
    # the device printed exactly that.** The path this step has to get through first is
    # `vm_compressor_init_locks` itself: 88 bytes, `lck_grp_attr_setdefault`, `lck_grp_init`,
    # `lck_attr_setdefault` and a tail `lck_rw_init` - every one of them already real in the image.
    # The build measured 2 resolved, 36 added, 519 -> 553 undefined.
    OSFMK_VM_VM_COMPRESSOR_OBJ=${STAGE90_ENTRY_OSFMK_VM_VM_COMPRESSOR_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_vm_vm_compressor.o}
    # `osfmk/vm/vm_map.c`, named by experiment-227's `stub_hit=vm_map_steal_memory`. **76439 bytes of
    # text, 52 of data, 416 of `.bss`, 202 definitions, 162 references** - nearly three times the
    # largest object linked so far. `vm_map_steal_memory` itself is 120 bytes and its three calls are
    # all `pmap_steal_memory`, which is already real, so it runs to completion.
    #
    # **The prediction is whatever `tools/xnu_entry_callwalk.py` says after this object is linked**,
    # and it is taken before the device is touched. Experiment 227 is the first step whose prediction
    # came from the tool rather than from a hand reading, and it held - and the two experiments before
    # it are why: 225 read `vm_page_bootstrap` as 888 bytes (it is `0x888` = 2184) and 226 then read
    # the call graph one level too shallow, so the stop was `vm_compressor_init_locks` where the
    # prediction said `zone_bootstrap`. A transitive closure over a 1744-function image is not
    # something to compute by hand.
    #
    # **Experiment 228's prediction was `zone_bootstrap` and it was wrong.** The stop the device
    # named was `OSCompareAndSwap16`, at `pmap_enter_options+0x1054`, inside a compare-and-swap retry
    # loop. The prediction was the tool's, and the tool was wrong for a reason that turned out to be
    # a bug in it and is now fixed - see `tools/xnu_entry_callwalk.py`'s `_guarded_addresses`.
    #
    # The bug: the tool called a call site guarded if it sat inside the span of a forward conditional
    # branch, which is the rule that keeps `lck_mtx_lock`'s `bl panic` assertion out of the walk. A
    # loop's exit test is also a forward conditional branch spanning the whole loop body, so the rule
    # also threw away loop bodies - `pmap_steal_memory`'s whole page-allocation loop, including its
    # `pmap_next_page_hi` and its trailing `pmap_enter`. The fix records whether an address was
    # reached by an actual branch; a loop head reached by an explicit `b` is not conditional whatever
    # span it sits in. `pmap_enter` and `pmap_next_page_hi` are now correctly unguarded, the
    # assertion is still guarded, and the two earlier answers the tool is checked against still hold.
    #
    # The path the run took, and where the walk now stops honestly:
    #   vm_page_bootstrap        unguarded   the walk reaches all of this
    #   pmap_steal_memory        unguarded   loop body now correctly entered
    #     -> pmap_enter          unguarded, and entered
    #       -> pmap_enter_options unguarded, and entered
    #         -> OSCompareAndSwap16  inside `pmap_enter_options`'s cold tail, at +0x1054
    # `pmap_enter_options` is 0x1090 bytes and its entry block ends at +0x114; everything past that
    # is behind a conditional branch, which is what `-O2` code of that size looks like. So the walk's
    # straight-line answer is a *lower bound*, and the answer is behind a branch it cannot read. The
    # tool's `--assume-taken CALLER+0xNNN` walks into such a site on request, and with the two this
    # run passed through it names `OSCompareAndSwap16` exactly - its third check against an answer
    # the device produced.
    #
    # `OSCompareAndSwap16` is defined by `libkern/gen/OSAtomicOperations.c`, and nothing else in the
    # image references it except `bsd_kern_uipc_mbuf.o`, which is not linked - so the step after this
    # one is that object, and it is a cheap one: the object defines 27 symbols and has no undefined
    # reference at all.
    OSFMK_VM_VM_MAP_OBJ=${STAGE90_ENTRY_OSFMK_VM_VM_MAP_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_vm_vm_map.o}
    # `libkern/gen/OSAtomicOperations.c`, named by experiment-228's `stub_hit=OSCompareAndSwap16`.
    # **1104 bytes of text, no data, no `.bss`, 27 definitions, zero references** - the smallest
    # object linked since the crypto ones, and the first with nothing undefined. Every symbol it
    # defines is `T`: the eight widths of increment/decrement/add, the bit and/or/xor, the two
    # test-and-set, and the eight compare-and-swap.
    #
    # **The prediction written here before the build was wrong, and the measurement is what it is.**
    # It said "resolves one stub and adds twenty-six". The build measured **10 resolved and 0
    # added**: the image had heard of only ten of the 27, and because the object has no undefined
    # reference of its own, the other seventeen can add nothing - they arrive as new *definitions*
    # the linker has no obligation to make, the `vm_kernel_ready` case from experiment 226 again.
    # `0 added` is a first for this sequence. The ten that were undefined were all 12-byte function
    # stubs: `OSAddAtomic`, `OSAddAtomic16`, `OSAddAtomic64`, `OSBitAndAtomic16`, `OSBitOrAtomic16`,
    # `OSCompareAndSwap`, `OSCompareAndSwap16`, `OSCompareAndSwap64`, `OSCompareAndSwapPtr`,
    # `OSIncrementAtomic` - 12 bytes each and nothing else, so the image had been linking stubs for
    # names XNU 4570 does reference in full.
    #
    # Note what the object is *for* here: only `OSCompareAndSwap16` is on the path the run took. The
    # other nine came along because the image's undefined set is a set, not a path.
    LIBKERN_GEN_OSATOMICOPERATIONS_OBJ=${STAGE90_ENTRY_LIBKERN_GEN_OSATOMICOPERATIONS_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/libkern_gen_OSAtomicOperations.o}
    # `osfmk/vm/vm_resident.c`, by way of `memorystatus_pages_update` - named by experiment-229's
    # `stub_hit=memorystatus_pages_update`. That symbol is `bsd/kern/kern_memorystatus.c`, but the
    # *call* that reached it is not in pmap.c: `pmap_startup` is defined in `osfmk/vm/vm_resident.c`
    # (line 1123), and its last call before the trailing `panic` is `VM_CHECK_MEMORYSTATUS`, the
    # `vm_page.h` macro that expands to `memorystatus_pages_update(...)` under `CONFIG_JETSAM`. It is
    # straight-line source - `for (i = pages_initialized; i > 0; i--) { ... } VM_CHECK_MEMORYSTATUS;`
    # - and the walk still calls it guarded, because it is behind the loop's exit branch and the walk
    # only follows fall-through and unconditional `b`. Third experiment running in which the answer
    # was inside a large `-O2` function rather than on the walk's straight-line path.
    #
    # `osfmk/vm/vm_resident.o` is already linked, so the object that has to be added is the one that
    # *defines* the symbol: `bsd/kern/kern_memorystatus.c` -> **31388 bytes of text plus a 1612-byte
    # `initcode` section, 620 of data, 136 of `.rodata`, 712 of `.bss`, 1254 of `.rodata.str1.1`,
    # 209 definitions, 110 references** - the largest object since `vm_map.o`, and the first with a
    # section this build has not seen (`initcode`).
    #
    # **The prediction is `stub_hit=zone_bootstrap`.** `memorystatus_pages_update` is the only stub
    # left inside `pmap_startup`, and it is the function's last call, so `pmap_startup` completes and
    # returns to `vm_page_bootstrap`, which finishes after `arm_usimple_lock_init` (already real).
    # `vm_mem_bootstrap` then calls `kernel_debug_string_early` (real) and, at `+0x2c`,
    # `zone_bootstrap` - a stub, and the next call in the caller after `vm_page_bootstrap`. This is
    # the same string experiment 228 predicted and missed; it is a different frontier with a much
    # shorter argument this time, and the argument is the caller's own call list rather than a walk.
    BSD_KERN_KERN_MEMORYSTATUS_OBJ=${STAGE90_ENTRY_BSD_KERN_KERN_MEMORYSTATUS_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/bsd_kern_kern_memorystatus.o}
    # **Experiment 230's prediction was `zone_bootstrap` and it was wrong - the fourth consecutive
    # miss, and this one is instructive because the right answer was one question away.**
    # `memorystatus_pages_update` is the first thing `bsd/kern/kern_memorystatus.c` runs, and its own
    # second statement is `vm_pressure_response();` under `VM_PRESSURE_EVENTS`. So the stop was inside
    # the object that had just been linked, not three calls past it. The rule that names it is now in
    # the tool's header and is not a walk at all: **root the walk at the symbol the last run named**,
    # because that symbol is where the run stopped and the function behind it is one level *down*.
    # `python3 tools/xnu_entry_callwalk.py --root memorystatus_pages_update` prints
    # `vm_pressure_response`, which is what the device printed - a retrodiction rather than a
    # prediction, and the first one the tool has produced without `--assume-taken`.
    #
    # `osfmk/vm/vm_pageout.c` -> **54812 bytes of text, 152 of data (32 `.data` + 120 in a
    # `__DATA, __data` section), 1992 of `.bss`, 2764 of `.rodata.str1.1`, 303 definitions, 219
    # references** - the largest object by definition count this link has taken on, and the first
    # with XNU's own Mach-O section spelling (`__DATA, __data`) alongside the ELF ones.
    #
    # **The prediction is `stub_hit=<whatever --root vm_pressure_response says after this object is
    # linked>`**, taken before the device is touched, with the plain walk from `kernel_bootstrap`
    # recorded beside it as the lower bound it is.
    OSFMK_VM_VM_PAGEOUT_OBJ=${STAGE90_ENTRY_OSFMK_VM_VM_PAGEOUT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_vm_vm_pageout.o}
    # **Experiment 231's prediction was `zone_bootstrap` and it held** - the first prediction to hold
    # since experiment 227, after four consecutive misses, and the first in this sequence that was
    # not made by walking the call graph at all. It was made by reading two branches out of the
    # source, because the four misses before it had established that the walk cannot see past them:
    #
    #   vm_pressure_response's first instruction test is `if (vm_pressure_events_enabled == FALSE)
    #   return;`, and `vm_pressure_events_enabled` is FALSE at its definition (vm_pageout.c:4089) and
    #   set TRUE only at line 4572, so the whole of `vm_pressure_response` is skipped, including the
    #   `thread_wakeup_prim` stub inside it;
    #
    #   `memorystatus_pages_update` next tests `memorystatus_available_pages <=
    #   memorystatus_available_pages_pressure`, and that global is initialised to 0
    #   (kern_memorystatus.c:658) against a few hundred thousand available pages, so that branch is
    #   not taken either and the `thread_wakeup_prim` stub inside *it* is skipped.
    #
    # Everything between there and `zone_bootstrap` is real, and `zone_bootstrap` is `vm_mem_bootstrap`'s
    # next call at +0x2c. Four predictions had aimed at this symbol; this is the one that arrived.
    # The build measured 22 resolved, 35 added, 653 -> 666 undefined.
    #
    # `osfmk/kern/zalloc.c` -> **16672 bytes of text, 8 of data, 56280 of `.bss`, 1386 of
    # `.rodata.str1.1`, 128 definitions, 72 references**. The `.bss` is the largest single
    # contribution this link has taken - the zone table - and it is zeroed by the payload rather than
    # stored, so it costs image bytes and not file bytes.
    OSFMK_KERN_ZALLOC_OBJ=${STAGE90_ENTRY_OSFMK_KERN_ZALLOC_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_zalloc.o}
    # **Experiment 232's prediction was `thread_call_setup` and it held** - two in a row, and the
    # first step in which the plain walk from `kernel_bootstrap` and the frontier-rooted walk gave the
    # *same* answer. They converged because the straight-line path is now real: `vm_mem_bootstrap`
    # calls `kernel_debug_string_early`, `vm_page_bootstrap`, `kernel_debug_string_early` and
    # `zone_bootstrap`, and all four are real code rather than 12-byte stubs. The lower bound and the
    # rooted answer are the same number when there is nothing left to hide behind.
    #
    # The build measured 23 resolved, 10 added and **666 -> 653 undefined** - the count went down, by
    # 13, which is more than it has moved in any direction since the entry image began growing. Eight
    # of the 23 were storage stubs, including `zone_array` at 0xd800, so 55296 bytes of zero stopped
    # being a stub and started being the zone table.
    #
    # `osfmk/kern/thread_call.c` -> **12600 bytes of text, 2432 of data, 61769 of `.bss`, 1451 of
    # `.rodata.str1.1`, 77 definitions, 47 references**. The second-largest `.bss` contribution after
    # `zalloc`'s, and like it, zeroed by the payload rather than stored.
    #
    # **The prediction is taken from the tool after this object is linked** - rooted at
    # `thread_call_setup` and from `kernel_bootstrap` both, as experiment 232 was, and recorded here
    # before the device is touched.
    OSFMK_KERN_THREAD_CALL_OBJ=${STAGE90_ENTRY_OSFMK_KERN_THREAD_CALL_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_thread_call.o}
    # **Experiment 233's prediction was `vm_object_bootstrap` and it held** - three in a row. It is
    # also the first stop since experiment 227 that is not inside one of four adjacent functions
    # (`vm_page_bootstrap`, `pmap_startup`, `memorystatus_pages_update`, `zone_bootstrap`): this one
    # is in `vm_mem_bootstrap` itself, the caller, which is the frame that has been quoted as "the
    # next corner" in three consecutive documents. Seven of `vm_mem_bootstrap`'s calls now execute in
    # real code. The build measured 6 resolved, 5 added, 653 -> 652 - the smallest step by undefined
    # count since experiment 222, and 61769 bytes of `.bss` arrived with it, taking the headroom under
    # `topOfKernelData` from 1588216 at experiment 224 to 1255224.
    #
    # `osfmk/vm/vm_object.c` -> **38320 bytes of text, 48 of data (24 in `.data` and 24 in a
    # `__DATA, __data` section), 1568 of `.bss`, 972 of `.rodata.str1.1`, 152 definitions, 149
    # references**.
    #
    # **The prediction written here before the build was `kmem_init`, taken from `vm_mem_bootstrap`'s
    # call list - `zone_bootstrap` real, `vm_object_bootstrap` the stop, `vm_map_init` real,
    # `kmem_init` the next stub. The rooted walk says otherwise and the rooted walk wins:**
    #
    #   walk from vm_object_bootstrap:
    #     vm_object_bootstrap
    #       zinit
    #         kmem_alloc_kobject   STUB
    #
    # `vm_object_bootstrap` calls `zinit` on its second statement, and `zinit`'s allocation is
    # `kmem_alloc_kobject`, so the stop is one level in and one level further down - the
    # experiment-230 mistake, caught this time because the check was written down before the build
    # rather than after. **The prediction is `stub_hit=kmem_alloc_kobject`**, and the plain walk from
    # `kernel_bootstrap` agrees with it, which the plain walk could not do for either of the last two
    # steps.
    OSFMK_VM_VM_OBJECT_OBJ=${STAGE90_ENTRY_OSFMK_VM_VM_OBJECT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_vm_vm_object.o}
    # **Experiment 234's prediction was `kmem_alloc_kobject` and it was wrong.** The device named
    # `snprintf`. Both are calls in `zinit`, 228 bytes apart, and what separates them is a runtime
    # flag no walk can read:
    #
    #   if (kmem_alloc_ready) {                                            zalloc.c:2229
    #           if (zone_names_start == 0 || (... + len) > PAGE_SIZE) {
    #                   printf("zalloc: allocating memory for zone names buffer\n");
    #                   kmem_alloc_kobject(kernel_map, &zone_names_start, ...);  zinit+0x5e4
    #           }
    #           ...
    #   } else { z->zone_name = name; }
    #   ...
    #   if (num_zones_logged < max_num_zones_to_log) { ... snprintf(...); }      zinit+0x6c8
    #
    # `kmem_alloc_ready` is 0 when `zinit` runs, so the `else` branch is taken, the block - the
    # allocation included - is skipped, and the run reaches `snprintf` *after* it. Not "probably 0":
    # exactly three sites in the image build the address `0x002b6570`, two of them in `zinit` and
    # both loads, and the one store in the whole image is later in the boot than the call -
    #
    #   vm_mem_bootstrap+0x02c  bl zone_bootstrap
    #   vm_mem_bootstrap+0x03c  bl vm_object_bootstrap    <- zinit runs here, flag still 0
    #   vm_mem_bootstrap+0x074  bl kmem_init              <- a stub; XNU sets the flag in here
    #   vm_mem_bootstrap+0x148  str r1, [0x002b6570]      <- kmem_alloc_ready = 1, after both
    #
    # **The tool was also wrong about the control flow, and that part was a bug, now fixed.**
    # `kmem_alloc_kobject` sits inside the span of `beq zinit+0x63c`, the guard on
    # `kmem_alloc_ready`, so the span rule should have called it guarded - and calling it guarded is
    # what would have made the walk answer `snprintf`. It did not, because `by_branch` was propagated
    # along the whole straight-line run from one unconditional `b` to the next (`b zinit+0x254` to
    # `b zinit+0x638`, 996 bytes), and `_is_guarded` consults `by_branch` before spans. The
    # propagation now stops at the first conditional branch crossed; experiment 228's loop body is
    # unaffected because its head-to-back-edge run is straight. Two earlier drafts of this comment
    # blamed other mechanisms and both were wrong - measured, the rule change moves 778 call sites in
    # the current image from unguarded to guarded and none the other way, and it changes no answer on
    # any of the three archived images that experiments 228 to 231 were predicted from.
    #
    # `osfmk/vm/vm_object.c` -> **38320 bytes of text, 48 of data, 1568 of `.bss`, 972 of
    # `.rodata.str1.1`, 152 definitions, 149 references**. The build measured 39 resolved, 26 added
    # and **652 -> 639 undefined** - the second decrease in the sequence, 33 of the 39 being function
    # stubs against 6 storage.
    #
    # **The prediction is no longer `kmem_alloc_kobject`** - the last run's answer was `snprintf`, and
    # that is what the walk names now.
    #
    # `bsd/kern/subr_prf.c` -> **1996 bytes of text, 4 of data, 68 of `.rodata.str1.1`, 22
    # definitions, 20 references** - the smallest object since `libkern_gen_OSAtomicOperations.o`,
    # and it defines `snprintf`, `vsnprintf`, `kvprintf`, `tablefull`, `v_putc` - the console output
    # pointer `putchar` calls - and the whole `tprintf` family.
    #
    # **The run did not stop on a stub at all.** It ended with `exception: undefined instruction`
    # and `kv_written=0x00000000` - the first result of that kind in this sequence: no stub was
    # reached, and real code was executing when the CPU refused an instruction. Experiment 236
    # answered where, by making the undefined-instruction vector report its own address, and the
    # answer is `xnu_entry_undef_pc=0x0022d1a8` - the `udf #65006` inside `DebuggerTrapWithState`,
    # reached only through `panic_trap_to_debugger`, which is called only by `panic`,
    # `panic_with_options` and `panic_context`. **XNU panicked.**
    #
    # The build measured 1 resolved, 7 added and **639 -> 645 undefined**: `snprintf` resolved, and
    # the printf machinery's own tty layer arrived with it - `constty` as the one storage stub, plus
    # `proc_session`, `session_rele`, `tputchar`, `tty_lock`, `tty_unlock` and `ttycheckoutq`.
    #
    # So the frontier is not an object any more. There is nothing left to link until the panic is
    # named, and the next step is an instrument rather than a step: read what XNU said before it
    # trapped, from the panic string pointer the trap is handed.
    #
    # Experiments 237 to 239 are that instrument, and they close the question with an answer that is
    # not an object at all. Three runs, three readings, no object linked in any of them and not one
    # byte of image size across all three.
    #
    # **237: the panic state is cached, and the cache is cleared before the trap.** XNU keeps the
    # panic message in three globals - `debugger_panic_str`, `debugger_message`,
    # `debugger_panic_caller`, all `B` objects in the already-linked `osfmk_kern_debug.o`, so
    # declaring them costs nothing and cannot go stale the way a hard-coded address could - and all
    # three read zero. That is predicted rather than surprising: `handle_debugger_trap` sets
    # `debugger_panic_str` at `debug.c:905` and restores it to `NULL` at `debug.c:947`, and it runs
    # *before* `DebuggerTrapWithState`. So the message has to be read where it cannot have been
    # cleared - the registers, which the vector trampoline does not touch.
    #
    # **238: the message, out of the argument registers.** `panic_trap_to_debugger` moves its first
    # four arguments into callee-saved registers at `+0x10` and they stay there - `r9 = fmt`,
    # `r8 = va_list *args`, `r7 = reason`, `r6 = ctx` - and `DebuggerSaveState` pushes and restores
    # them without writing them. The run read `r9` and dumped the six words it points at:
    # **`"zfree: freeing invalid "`**, which is `zalloc.c:1208`,
    #
    #   if (__improbable(!is_sane_zone_element(zone, element)))
    #           panic("zfree: freeing invalid pointer %p to zone %s\n",
    #                 (void *) element, zone->zone_name);
    #
    # `free_to_zone` was handed an element its own sanity check rejected. A first version of 238 also
    # read `r4` as `panic_options_mask`; the disassembly showed `fleh_undef`'s own prologue emitting
    # `mov r4, lr` before the first statement, so the key was deleted rather than kept with a caveat.
    #
    # **239: the check that fired is the first one, and it cannot pass at this image's addresses.**
    # The two arguments - element and zone name - were read one dereference short, and `r8` is why:
    # it is a `va_list *`, so the word at it is the list's `__ap` and not the first argument.
    # `panic`'s own frame settles the offset to the byte - `str r1, [sp, #16]` with the varargs at
    # `sp+28` makes `__ap` twelve bytes into the struct, exactly the `0x0029be94 - 0x0029be88` the
    # run measured. Experiment 240 reads the frame instead of the live registers, which is also the
    # only way to get `r5 = db_panic_caller`: `fleh_undef`'s prologue clobbers `r5` with
    # `mrs r5, SPSR`, but saves it at `[sp+4]`. `sl` and `r4` are the two halves of the 64-bit
    # options mask and not the caller - `stm sp, {r4, sl}` at the `DebuggerTrapWithState` call - a
    # correction to 238's own comment, which read `sl` as the mask and `r4` as the caller.
    #
    # The two bounds that check compares against, `zone_map_min_address` and `zone_map_max_address`,
    # both read zero - which is what `zone_init` never having run looks like. `zone_init` is their
    # only writer (`zalloc.c:2958-2959`), its only caller in the image is `vm_mem_bootstrap+0x204`,
    # and before it on that path sit two stubs (`kmem_init` at `+0x074`, `kext_alloc_init` at
    # `+0x1a4`), while its own first call is `kmem_suballoc` - also a stub. A stub ends the run, so
    # these globals have never been anything but zero here.
    #
    # **But that is not why the free panicked.** `is_sane_zone_ptr` tests in this order -
    # `pmap_kernel_va`, then alignment, then the bounds - and the bounds test is not even reached
    # for `vm_map_zone`, which is `Z_COLLECT FALSE` / `Z_FOREIGN TRUE`, and whose element is a
    # multiple of the zone's 160-byte element size. The test that fires is the first one, and
    # `pmap_kernel_va` is `[0x80000000, 0xFFFEFFFF]` (`osfmk/arm/pmap.h:371`,
    # `osfmk/mach/arm/vm_param.h:169-170`) - a compile-time constant with nothing to do with where
    # this image was linked. This image runs at `0x00200000`, and two consequences follow, both
    # arithmetic:
    #
    #   arm_vm_init.c:496   vm_kernel_slide = gVirtBase - 0x80000000         = 0x80200000
    #   arm_vm_init.c:505   virtual_space_start = (gVirtBase + MEM_SIZE_MAX + 0x3FFFFF) & 0xFFC00000
    #                                                                       = 0x40400000
    #
    # so every address XNU's own allocator hands out is below `VM_MIN_KERNEL_ADDRESS` and is
    # rejected by XNU's own kernel-address test. The panicking free is `vm_map_init+0x260`'s
    # `zcram(vm_map_zone, map_data, map_data_size)` (`vm_map.c:869`), the first free on the
    # straight-line boot path - `map_data` is the boot's first `pmap_steal_memory`, so it *is*
    # `virtual_space_start`, `0x40400000`, and `vm_map_zone`'s name is the literal `"maps"`.
    #
    # **So the frontier has moved off objects and onto the image's base, and no object can fix it.**
    # The next stage links and runs the image at a base at or above `0x80000000`, which is where the
    # device's RAM already starts (`RAM_PHYS_BASE`, stage90.h:21) and where this SoC's kernel
    # normally loads (`0x80008000`). That is `ENTRY_BASE`, the payload's mapping of the window, and
    # `physBase`/`virtBase` in the boot_args - with the entry epilogue's identity trick as the thing
    # to re-derive rather than assume.
    #
    # **240 read the frame and the two arguments, and the diagnosis is closed.** Three of the four
    # things it measured were predicted to the byte before the run:
    #
    #   frame_r4 = 0, frame_sl = 0     the two halves of db_panic_options
    #   frame_r6 = 0, frame_r7 = 0     ctx and reason
    #   frame_r8 = 0x0029be88          panic_args, the same value exp-238 and exp-239 read live
    #   frame_lr = 0x0022d40c          the instruction after the udf
    #   frame_r9 = trap_r9_fmt         the format string - equality with the live read is the
    #                                  prediction that holds across builds, not a constant address
    #   panic_ap = 0x0029be94          the va_list's __ap, exp-239's word, one dereference in
    #   panic_element = 0x40401f20     in the 0x4040xxxx chunk
    #   zone_name_w0 = 0x7370616d      "maps"
    #
    # The element is read as a *value* out of the va_list and never dereferenced, so `entry_image_ptr`
    # is deliberately NOT widened for it: 0x40400000 is outside the image, may well be unmapped, and a
    # data abort inside the abort handler says nothing at all. `ap` and the zone-name pointer are both
    # inside the image, so the guard that was already here covers every read that happens.
    #
    # **`frame_r5 = 1`, and it is not the caller.** exp-239 said r5 = db_panic_caller, which is true of
    # `panic_trap_to_debugger` (`ldr r5, [sp, #64]`) and false of the function the trap is actually in:
    # `DebuggerTrapWithState` reloads r5 from its own stack argument (`ldr r5, [sp, #40]` at 22d3ec),
    # which is `db_proceed_on_sync_failure` - a boolean the call site sets with `mov r0, #1`. The
    # caller goes into `lr` there, and `lr` is destroyed by the `bl DebuggerSaveState` one instruction
    # before the `udf`, so the caller is not in any register at the trap. It does not need to be: the
    # condition is settled by the two arguments, and `free_to_zone` has exactly one `bl panic` in the
    # image (0x26fe80), so the caller is free_to_zone+0x148 whatever the trace.
    #
    # The element is not the chunk's first byte because `zcram` hands its elements to
    # `random_free_to_zone` - the function is named for what it does - so which element the check sees
    # first is a draw. What the region, the zone and the condition say is unaffected.
    #
    # Cost: 64 bytes of entry text (592689 -> 592753), 0 bytes of image, 645 undefined, the stub set
    # unchanged. `ENTRY_KV_BUF` 1024 -> 2048 takes the run to 25 keys and 831 bytes, which moves the
    # image's `.bss` end and with it the *derived* boot_args offset (897024 -> 901120) - the layout
    # block working, not a hazard. And the payload was rebuilt from the regenerated header.
    #
    # **241 moved the base to 0x80000000, and the zfree panic is gone.** The five runs before it
    # (236-240) all ended at the same `udf` inside `DebuggerTrapWithState`, reached through
    # `panic_trap_to_debugger`, because `is_sane_zone_ptr`'s first test - `pmap_kernel_va` =
    # [0x80000000, 0xFFFEFFFF], a compile-time constant - is false for every address an image at
    # 0x00200000 produces: `virtual_space_start` came out at 0x40400000 and `vm_kernel_slide` at
    # 0x80200000. At 0x80000000 the two become 0xC0000000 and 0, and the mandatory-path free passes
    # the check it was failing.
    #
    # What it stops at instead is a **data abort at 0x80300000 with DFSR = 0x0000080f**: bit 11 set,
    # so it was a *write*, and FS = 0b01111, a **permission fault on a level-2 page**. That is a
    # fact about the mapping and not about the instruction: everything `_start` installs in this
    # window is a 1 MB *section* with AP_RWNA (memSize is 8 MB, so `mapveqp` and not `mapveqpL2`),
    # and a section cannot produce a page permission fault. So XNU's own pmap was live and had
    # mapped 0x80300000 read-only, and the writer is not named yet - `fleh_dataabt` still reports
    # only DFAR and DFSR. Experiment 242 gives it `lr_abt - 8` and the MMU registers
    # (TTBR0/TTBR1/TTBCR/SCTLR), which is the same treatment `fleh_undef` got in 236.
    #
    # Also in 241, and the reason the move was this small: the base is now written down in exactly
    # one place. `entry.ld` used to carry its own `. = 0x00200000` while this file carried
    # ENTRY_BASE - the one-value-two-definitions defect - and it now takes the base from a
    # `--defsym` this file injects, so a link that forgets it fails with `undefined symbol
    # ENTRY_BASE' referenced in expression` rather than producing an image at zero. `entry_image_ptr`
    # likewise stopped testing the literals [0x00200000, 0x04000000) and now tests the linker's own
    # `__entry_text_start` .. `__entry_image_end`: the move would otherwise have turned every guarded
    # read in fleh_undef into a *skipped* one, silently. Everything above the image needed nothing:
    # the layout is derived from the image's size, so the build reports the same offsets at either
    # base, and the payload's window loop, its boot_args and start.s are all macros or boot_args
    # reads. Cost: 32 bytes of text (592753 -> 592785), 0 bytes of image, 645 undefined, stub set
    # unchanged.
    #
    # **242's prediction, written before the run.** `fleh_dataabt` and `fleh_prefabt` now report
    # `lr_abt` and `pc_abt` (LR minus 8 for a data abort, minus 4 for a prefetch abort - the
    # architecture's own definitions), the word at `pc_abt`, and the mapping state. The four
    # boundary globals have values this project can predict and check:
    #
    #   cpu_ttep   0x80204000   arm_vm_init.c:373, `boot_ttep + ARM_PGBYTES*4`, boot_ttep being
    #                           args->topOfKernelData = 0x80200000
    #   avail_start ~0x8020C000  arm_vm_init.c:399, `cpu_ttep + ARM_PGBYTES*6`, plus two pages
    #                           from the pre-initialization loop (off_end = 5 MB for
    #                           mem_segments = 1, step ARM_TT_L1_PT_SIZE = 4 MB)
    #   gPhysBase   0x80000000   the boot_args, and now also the link address
    #   mem_size    0x00800000   args->memSize, left alone against xmaxmem
    #
    # and `TTBR0` is the one that separates the two candidate causes: `set_mmu_ttb(cpu_ttep)` would
    # leave 0x80204xxx there, so a `TTBR0` of 0x80204xxx means the fault is inside the boot tables'
    # regime - `_start`'s, then whatever `pmap_bootstrap` and `arm_vm_prot_init` did to them - while
    # anything else means the pmap has installed tables of its own and the fault is later still.
    #
    # **242 measured it: `ttbr0 = ttbr1 = 0x8020404a`, `cpu_ttep = 0x80204000`, and the writer is
    # named.** `lr_abt - 8 = 0x80024f5c` is `str r5, [r0], #4` - the first PTE store of
    # `pmap_init_pte_static_page` (0x80024f1c, 96 bytes), whose only caller in the whole image is
    # 0x8001880c inside `arm_vm_page_granular_helper`'s allocate-a-coarse-table branch. The page it
    # writes is the page it just took from `avail_start`: the built code keeps the pre-increment value
    # in `ppte` (`ldr r3,[avail_start]` / `str r3+4096,[avail_start]` / `add r6,(r3-gPhysBase),gVirtBase`),
    # `DFAR = 0x80300000` equals `ppte` exactly, and `avail_start` in the handler is `0x80301000` -
    # one page above. So this is store number zero of 1024, into a page that is read-only.
    #
    # It is read-only because `pmap_init_pte_static_page` writes *all* 1024 entries with
    # `ARM_PTE_AP(AP_RONA)` - a read-only fill of a whole 4 MB window - while its caller re-protects
    # only the pages inside [start, _end). The two descriptors differ by one bit and it is visible in
    # the two `bfi`s: the fill's is 0x612, the helper's own base is 0x412. So the residue of the first
    # call (RWX over [0x80000000, 0x800906c0), window [0x80000000, 0x80400000)) is
    # [0x80091000, 0x80400000), and after the __DATA call it is [0x800db000, 0x80400000).
    #
    # **Why this image reaches an allocation inside that residue at all is the finding.** Our
    # synthetic Mach-O has two segments, and `getsegdatafromheader` sets `*size = 0` and returns NULL
    # when a segment is absent (`libkern/kernel_mach_header.c`), so `segPRELINKTEXTB` and
    # `segSizePRELINKTEXT` are both zero - which makes `RWNX(segPRELINKTEXTB + segSizePRELINKTEXT,
    # end_kern - (segPRELINKTEXTB + segSizePRELINKTEXT), force_coarse_physmap)` become
    # `RWNX(0, end_kern)`: not the small PreLinkInfoDictionary range a real kernel has, but 1024
    # iterations of the 4 MB alignment loop, each taking a page-table page from `avail_start`. And
    # `avail_start` is inside the residue.
    #
    # **Why the fault is at 0x80300000 rather than at the second allocation, 0x80223000, is the TLB.**
    # `avail_start`'s first page is 0x80222000, not 0x8020A000: `pmap_bootstrap`'s table allocation
    # (`pmap.c:2841-2850`) takes pp_attr 0x1000, io_attr 0 (our `/defaults` node has no
    # pmap-io-ranges, so niorgns = 0), pv_lock 0x800, pv_head 0x2000 and
    # ptd_root_table_size = sizeof(pt_desc_t) * 4096 = 20 * 4096 = 0x14000, ending at 0x80222000.
    # Then `pmap_bootstrap`'s own `memset` of [0x8020A000, 0x80222000) runs while L1 index 0x802 is
    # still the 1 MB section `start.s` installed, and nothing calls `flush_mmu_tlb()` between it and
    # the fault (the last one is just before `pmap_bootstrap`, `arm_vm_init.c:491`). A cached section
    # translation survives the L1 entry being turned into a coarse table, so all 222 allocations
    # before the fault stay inside that megabyte and bypass the walk; the 223rd is 0x80300000, the
    # first page of the next megabyte, whose L1 entry the first call had already replaced with a table
    # of read-only PTEs. Hence DFAR = round_up_1MB(0x80222000) = 0x80300000, which is a prediction and
    # not an observation.
    #
    # So the fault is this image's Mach-O, not XNU's pmap: the header is one segment short of the one
    # `arm_vm_init` does arithmetic with. **243 adds `__PRELINK_TEXT` to `entry_macho.s`** - and the
    # result is two blocks below, because it is the experiment that changes the frontier.
    #
    # Cost: 1056 bytes of text (592785 -> 593841), fleh_prefabt 204 B and fleh_dataabt 368 B, 0 bytes
    # of image (703352), layout unchanged, 645 undefined, stub set unchanged. The four globals it
    # reads are read-only and `ENTRY_KV_BUF` 2048 was already enough for the 14 keys (552 bytes).
    #
    # **243 added the third segment, and with it the fault is gone and the frontier is a function
    # again.** The segment is *empty*: `vmaddr = __entry_image_end`, `vmsize = 0`, `fileoff` and
    # `filesize` 0, `maxprot`/`initprot` 0 - which is the honest description of a kernel with no
    # kexts, and which also happens to be the arithmetic that matters, because `getsegdatafromheader`
    # returns `sc->vmaddr` and stores `sc->vmsize`. So
    # `segPRELINKTEXTB + segSizePRELINKTEXT` is 0x800da248, a real address, and the call is
    # `RWNX(0x800da248, 0xdb8)` - the round-up slop of the image's last page - instead of
    # `RWNX(0, 0x800db000)`. The plan one block above said `vmsize = end_kern - __entry_image_end`;
    # the segment as built says 0. Both bound the call (`end_kern` is `round_page(__entry_image_end)`,
    # so the two ranges end at the same address) and the built one is the better description, because
    # the slop of the final page is memory the pmap should map. 0xdb8 < 0x1000 in either case.
    #
    # **The check that matters is in `tools/host_entry_macho_check.sh`, and it is about the range, not
    # the segment.** The old header passed for fifty experiments while wrong for the reason that made
    # it pass: every field matched the linker's own symbols. So the check now computes
    # `end_kern - (segPRELINKTEXTB + segSizePRELINKTEXT)` from the decoded commands and fails unless
    # it is non-negative and smaller than a page - 0xdb8 today - and the second half is not
    # decoration: a `__PRELINK_TEXT` ending past `end_kern` makes the subtraction negative and the
    # `unsigned long size` enormous, a worse failure than the one being fixed. It was tested by
    # breaking it (vmaddr temporarily `__entry_text_start`): two FAIL lines, exit 1, and clean again
    # after restoring. `fleh_dataabt` also reports `end_kern`, `segPRELINKTEXTB` and
    # `segSizePRELINKTEXT`, so a run that *does* abort again still shows whether the header reached the
    # code; the prediction was that they are never printed, and the absence is the result.
    #
    # **The result: no exception at all, and the run stops at `stub_hit=kmem_init`.** `kv_written ==
    # kv_in_dram == 0x14` (20 bytes, five keys): the abort handler did not run and wrote nothing, the
    # three new keys included. The chain is `arm_init+0x340` -> `machine_startup+0xe8` ->
    # `kernel_bootstrap+0x120` -> `vm_mem_bootstrap+0x074` -> `kmem_init`, and `vm_mem_bootstrap` is
    # straight-line from its first instruction to that call (`kernel_debug_string_early`,
    # `vm_page_bootstrap`, `zone_bootstrap`, `vm_object_bootstrap`, `vm_map_init`, `kmem_init`, all
    # `bl` with no conditional branch between them). So reaching `kmem_init` says every call before it
    # returned - the EVB special case, `arm_vm_init`'s pre-initialization loop for
    # `virtual_space_start = 0xC0000000`, `patch_low_glo_static_region`, the rest of `arm_init`,
    # `machine_startup`, `kernel_bootstrap` and the first five calls of `vm_mem_bootstrap`. In
    # particular 239's `vm_map_init+0x260` free passed, which is what the base move was for.
    # `zone_init` is still ahead at `vm_mem_bootstrap+0x204`, so `zone_map_min_address` and
    # `zone_map_max_address` are still zero and 239's second finding is unchanged - it simply is not
    # in the way any more. **The one-object-per-run method is back**: what is missing here is a
    # function, not an address, and the next object answers it.
    #
    # One thing the run did not show, kept because it is the shape of every remaining stop: the
    # device's line is `real arm_init reached a symbol this image does not provide`, a label written
    # when the only interesting stub was inside `arm_init`. `entry_stub_hit` is handed a name and
    # nothing else, so it cannot be more specific (206's lesson: `stub_hit=<symbol>` names a symbol,
    # never a caller), and with the stop this far from `arm_init` the label is now wrong about where.
    # The fix is one argument - a generated stub can pass `__builtin_return_address(0)`, which is in
    # `lr` for the instruction after its `bl` - and it belongs in 244 with the object link, because
    # from here every stop is a stub somewhere far from `arm_init`.
    #
    # Cost: `ncmds` 2 -> 3, `sizeofcmds` 180 -> 236, `__TEXT` vmsize 0x906c0 -> 0x90700, `__DATA`
    # vmaddr/vmsize 0x800906c0/0x49b88 -> 0x80090700/0x49b48, text 593841 -> 594065 (+224, and 64 of
    # them the load command itself), fleh_dataabt 368 -> 440 (the three keys), 0 bytes of image
    # (703352), `.bss` 0x800ab4f0..0x800da248 unchanged, layout unchanged, 645 undefined, stub set
    # unchanged. `__entry_image_end` and therefore `getlastaddr()`/`end_kern` do not move: the header
    # grew, so `__DATA`'s front moved up 64 bytes and it is 64 bytes shorter, with its *end* where it
    # was - which is why the payload needed no rebuild beyond reading the new `.bin`.
    BSD_KERN_SUBR_PRF_OBJ=${STAGE90_ENTRY_BSD_KERN_SUBR_PRF_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/bsd_kern_subr_prf.o}
    # `osfmk/vm/vm_kern.c` -> **9132 bytes of text, 674 of `.rodata.str1.1`, 8 of `.bss`, no
    # `.data`, 26 global definitions, 79 references**. The object 243's stop named: `kmem_init` is
    # 0x1ac bytes of it at offset 0x1e54, and the three that follow it in the boot are all here too -
    # `kmem_alloc_kobject` (0x24 bytes, `vm_object_bootstrap`'s second statement and the call 234
    # predicted for that step), `kmem_suballoc` (0x1bc, `zone_init`'s first call) and `kmem_alloc`
    # (0x60). So one object stands behind three of the next four stops, and that is why this is the
    # step rather than another probe: the frontier is a function, not an address - `vm_kernel_slide`
    # is 0 under experiment 241's base, so nothing here can be rejected by the compile-time constant
    # that `is_sane_zone_ptr` applies. (That constant is the whole reason 239 ended the
    # one-object-per-run method; the memory `mi4-kernel-base-is-a-compile-time-constant` is the one
    # to read before proposing an object for a boot-path failure, and this run is the case where
    # reading it says the method is usable again.)
    #
    # Two collisions are impossible and one is worth stating. Impossible: nothing else in this
    # project's object pool defines any of the 26 (`nm -A --defined-only` over
    # `out/xnu_kernel_obj/*.o out/xnu_asm_obj/*.o` names only this file for every one of them), and
    # `entry_stubs.c` has no hand-written definition of any `kmem_*`, `kernel_memory_*`,
    # `kernel_map` or `vm_kernel_addr*` name. Worth stating: 21 of the 24 *storage* symbols this
    # object references are already defined by objects the image links - `sane_size`, `max_mem`,
    # `vm_kernel_slide`, `kernel_pmap`, `vm_page_locks`, `vm_page_free_target` and the rest - so they
    # will not become stubs, and the three that will (`vm_global_no_user_wire_amount`,
    # `vm_global_user_wire_limit`, `vm_user_wire_limit`) already are, at 4 bytes each. The generator
    # fails the build rather than guess a storage size, so this is a prediction the build checks.
    #
    # **The prediction, written before the build.** `kmem_init` is 0x1e54 bytes into the object and
    # its ARM-specific body is
    #
    #     kernel_map = vm_map_create(pmap_kernel(), VM_MIN_KERNEL_AND_KEXT_ADDRESS,
    #                                                    VM_MAX_KERNEL_ADDRESS, FALSE);
    #     while (pmap_virtual_region(region_select, &region_start, &region_size)) {
    #             ... vm_map_enter(kernel_map, &map_addr, ..., VM_OBJECT_NULL, ...);
    #     }
    #
    # so the first thing it calls is `vm_map_create` (defined - `osfmk_vm_vm_map.o` is linked since
    # experiment 239, which is where 239's `vm_map_init+0x260` panic came from) and the first thing
    # it *loops* on is `pmap_virtual_region` (defined - `osfmk_arm_pmap.o`). Which stub it reaches
    # first is therefore a question about `vm_map_create`'s own body, and the answer is not something
    # to guess: it is read off the disassembly of the linked image, by walking the calls and asking
    # which of them is a stub. That walk is the prediction, and the device is what checks it.
    #
    # **And the reporting fix travels with it**, because of where the stop now is: `entry_stub_hit`
    # gains a second argument, the `lr` the stub was entered with (a generated stub is a one-liner,
    # so `__builtin_return_address(0)` is that register read before anything can clobber it -
    # verified by compiling the two shapes with this build's own flags and reading the disassembly:
    # `mov r1, lr` before the tail call). The run then reports `xnu_entry_stub_caller`, which names
    # the *call site* rather than the symbol, and `tools/host_resolve_entry_addr.sh` turns that
    # address back into `function+0xNN` against the image. The stop line's own text said "real
    # arm_init reached a symbol" about a stub four frames below `arm_init`; with the caller reported
    # the sentence no longer has to carry that, and it becomes what it can be exact about - it now
    # reads "a symbol this image does not provide was called".
    #
    # **244 measured it, and both halves landed.** The build resolved 12 and added 3 - `copyinmap`,
    # `kernel_map`, `kernel_memory_allocate/depopulate/populate`, `kmem_alloc`, `kmem_alloc_flags`,
    # `kmem_alloc_kobject`, `kmem_alloc_pageable`, `kmem_free`, `kmem_init`, `kmem_suballoc` against
    # `SHA256_Init/Update/Final` - so 645 -> 636 undefined, 559 -> 551 function stubs and 86 -> 85
    # storage. Text 594065 -> 610225, and the split is worth having: **the reporting change alone
    # costs 6720 bytes**, measured by building with this object replaced by an empty one (600785,
    # with exp-243's 645-symbol stub set), of which 6708 is 559 stubs going from 12 bytes to 24 -
    # `movw/movt` + `b` becomes `movw/movt` + `mov r1, lr` + `push`/`pop {lr}`, because gcc has to
    # restore `lr` so the tail-called `entry_stub_hit` still returns to the original caller. The
    # caller argument is therefore not free, and it is bought once for every stub in the image.
    #
    # The device's run: **`stub_hit=vm_map_store_init`, `xnu_entry_stub_caller=0x8004651c`**, which
    # resolves to `vm_map_create+0x5c` - the return address of `80046518: bl 80083604
    # <vm_map_store_init>` in the disassembly the prediction was made from. No exception, no abort
    # key, `kv_written == kv_in_dram == 0x3e` (62 bytes: the two lines, 28 + 34). And the stop is
    # one instruction past `bl zalloc` / `cmp r0, #0` / `bne`, with the object's own `panic` for the
    # NULL case *not* taken: a real zone allocation out of `vm_map_zone` returned on this hardware.
    # `zone_init` (`vm_mem_bootstrap+0x204`) is still ahead, so 239's zero zone-map bounds are
    # unchanged; `kmem_suballoc`, its first call, is answered by this same object.
    #
    # Cost: 9132 bytes of text in the object, 674 of `.rodata.str1.1`, 8 of `.bss` (all linked), 636
    # undefined, entry text 610225, image 703352 -> 719736, `.bss` end 0x800da248 -> 0x800de208,
    # `__entry_image_end` 0x800de208 and `end_kern` 0x800df000, the derived `boot_args` offset
    # 901120 -> 917504 with topOfKernelData/tree/window unmoved, 1187320 bytes of headroom, and the
    # payload rebuilt from the regenerated header (its text grows by exactly the entry image's
    # 16384). `persistent_write_attempted=0x00000000` in all 25 contracts that report it.
    #
    # Next: `vm_map_store_init` is in `osfmk_vm_vm_map_store.o` (908 bytes of text, 11 definitions,
    # 14 references) - already built. It is a dispatcher: it calls `vm_map_store_init_ll` and
    # `vm_map_store_init_rb`, which are in `osfmk_vm_vm_map_store_ll.o` (776) and
    # `osfmk_vm_vm_map_store_rb.o` (5808), both built as well.
    OSFMK_VM_VM_KERN_OBJ=${STAGE90_ENTRY_OSFMK_VM_VM_KERN_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_vm_vm_kern.o}
    # `osfmk/vm/vm_map_store.c` -> **908 bytes of text, 41 of `.rodata.str1.1`, 11 definitions, 14
    # references, no storage at all**. The object 244's stop named: `vm_map_store_init` is the
    # definition `vm_map_create` calls, and the answer to it is this file rather than a probe.
    #
    # **It is a dispatcher, and that shapes the prediction.** Its definitions come in three layers -
    # the portable ones (`vm_map_store_init`, `vm_map_store_entry_link`, `vm_map_store_lookup_entry`,
    # `vm_map_store_update`, `vm_map_store_copy_insert`, `vm_map_store_copy_reset`) and the two
    # implementations they select between, `vm_map_store_*_ll` (in `osfmk_vm_vm_map_store_ll.o`) and
    # `vm_map_store_*_rb` (in `osfmk_vm_vm_map_store_rb.o`). In 4570 both stores are live: the RB
    # tree is the lookup structure and the "ll" store is the hole list.
    #
    # Nothing here is storage, so the generator has no size to get wrong: all 14 references are
    # `panic` (real, `osfmk_kern_debug.o`) and the twelve `_ll`/`_rb` implementations.
    #
    # **245 measured it, and the object moved no address.** The build resolved 10 and added 13 - the
    # portable entry points in, the two implementations out, net 636 -> 639 undefined and 551 -> 554
    # function stubs with the storage count unchanged at 85. Text 610225 -> 611345 (+1120 = the
    # object's 908 plus three 24-byte stubs and their names), and **the image, `.bss`, layout,
    # headroom, `__entry_image_end` and the payload's own size are all unchanged** - the growth fitted
    # inside the linker script's alignment padding, so the payload was rebuilt only to carry a new
    # copy of a same-sized `.bin`. First step in this sequence where linking an object cost nothing in
    # layout, and worth having measured rather than assumed.
    #
    # The prediction was read off the disassembly, because this file is a *dispatcher* and the
    # source's program order is not the answer: `vm_map_store_init`'s first statement is
    # `vm_map_store_init_ll(hdr)`, unconditional, and only behind `vm_map_store_has_RB_support(hdr)` -
    # compiled to `cmp r0, #0xbaadc0d1` (`SKIP_RB_TREE`, `vm_map_store.h:126`) / `popeq` - does it
    # tail-call `vm_map_store_init_rb`. The device said **`stub_hit=vm_map_store_init_ll`,
    # `xnu_entry_stub_caller=0x8008070c`** = `vm_map_store_init+0xc`, the return address of
    # `80080708: bl 800839f0 <vm_map_store_init_ll>`: the stop is before the `cmp` at `+0x18`, so the
    # RB decision has still not been taken on this device. No exception, `kv_written == kv_in_dram ==
    # 0x41` (65 bytes: 31 + 34).
    #
    # Next: `vm_map_store_init_ll` is in `osfmk_vm_vm_map_store_ll.o` - 776 bytes of text, 8
    # definitions, and two references (`_consume_printf_args`, `OSCompareAndSwapPtr`) that **are both
    # already defined** by objects the image links. So 246's stop will be inside one of the eight
    # definitions or deeper, which is a prediction for its own disassembly rather than for an object
    # list.
    OSFMK_VM_VM_MAP_STORE_OBJ=${STAGE90_ENTRY_OSFMK_VM_VM_MAP_STORE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_vm_vm_map_store.o}
    # `osfmk/vm/vm_map_store_ll.c` -> **776 bytes of text, 8 definitions, and exactly two references,
    # both already defined**: `_consume_printf_args` (`bsd_kern_subr_prf.o`, linked since 235) and
    # `OSCompareAndSwapPtr` (`libkern_gen_OSAtomicOperations.o`, linked since 228). The object 245's
    # stop named, and **the first step in this sequence whose object has nothing missing under it** -
    # so the frontier is inside one of these eight definitions or deeper, and that is a question for
    # the disassembly.
    #
    # The eight are the hole-list store: `vm_map_store_init_ll`, `vm_map_store_lookup_entry_ll`,
    # `vm_map_store_entry_link_ll`, `vm_map_store_entry_unlink_ll`, `first_free_is_valid_ll`,
    # `update_first_free_ll`, `vm_map_store_copy_insert_ll`, `vm_map_store_copy_reset_ll` - the
    # implementation half of the dispatcher 245 linked, and the reason the module comment above says
    # both stores are live in 4570.
    #
    # Worth noting before the run, because it is the kind of thing that has cost a step here: the
    # `_ll` and `_rb` objects share a *family* of names and differ by suffix, and the stub list is
    # keyed on exact names. A prediction of `vm_map_store_init_ll` when the run stops on
    # `vm_map_store_init_rb` would be a wrong reading of a right run, not a missing object.
    #
    # **246 measured it, and the object moved the stop rather than resolving it.** `vm_map_store_init_ll`
    # is `bx lr` - four bytes, a genuinely empty function - because the LL store has nothing to
    # initialise in 4570. So the call returns and the dispatcher continues: the build resolved 6 and
    # added 0 (639 -> 633 undefined, 554 -> 548 function stubs), and the stop moved to the *next*
    # thing `vm_map_store_init` does, which is the `vm_map_store_has_RB_support` test and then the tail
    # call. Two of the object's eight definitions were never stubs at all
    # (`first_free_is_valid_ll`, `vm_map_store_lookup_entry_ll`), which is what "resolved everything I
    # linked" would have got wrong.
    #
    # The branch is decided by `hdr.rb_head_store.rbh_root != SKIP_RB_TREE`, a runtime value no walk
    # can read - the shape that cost 234 a run - so it was resolved the way that one was, by finding
    # the writers: all nine uses of `SKIP_RB_TREE` are in `vm_map_store.c`'s predicate and in
    # `vm_map.c`'s copy/clip/switch-context paths, none of which this boot has reached, so the
    # sentinel is not in the header and the tail call is taken. Device: **`stub_hit=vm_map_store_init_rb`,
    # `xnu_entry_stub_caller=0x8004651c`** - and that address is `vm_map_create+0x5c`, the *same* one
    # 244 reported, because the call is a `b`: `pop {r4, lr}` restored the `lr` the prologue saved, so
    # the key holds `vm_map_create`'s return address and not the tail call site.
    #
    # **Read the key that way from here on.** `caller - 4` is the address of a call instruction only
    # when the call was a `bl`; for a tail-called stub it points at a `bl` to a *different* symbol,
    # which is exactly what `0x80046518: bl vm_map_store_init` is. Resolving `caller - 4` and comparing
    # its target against the stub that stopped the run is how the two cases are told apart, and
    # `tools/host_resolve_entry_addr.sh` prints both readings for that reason.
    #
    # No exception, `kv_written == kv_in_dram == 0x41` (65 bytes: 31 + 34), 87 contracts with
    # failure_mask=0 and 25 with persistent_write_attempted=0. Text 611345 -> 611793 (+448), and the
    # image, `.bss`, `__entry_image_end`, layout, headroom and the payload's own size are unchanged
    # again - two steps in a row where linking an object cost nothing in layout.
    #
    # Next: `vm_map_store_init_rb` is in `osfmk_vm_vm_map_store_rb.o` - 5808 bytes of text, 24
    # definitions, 4 references (`panic`, `vm_map_holes_zone`, `zalloc`, `zfree`) that are **all four
    # already defined**, so that step may also have nothing missing underneath it.
    OSFMK_VM_VM_MAP_STORE_LL_OBJ=${STAGE90_ENTRY_OSFMK_VM_VM_MAP_STORE_LL_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_vm_vm_map_store_ll.o}
    # `osfmk/vm/vm_map_store_rb.c` -> **5808 bytes of text, 24 global definitions, 4 references**
    # (`panic`, `vm_map_holes_zone`, `zalloc`, `zfree`) - **all four already defined**, so this is the
    # second step in a row whose object has nothing missing underneath it. It is the largest object
    # since experiment 239's `vm_map.o`, and the one that brings the red-black tree
    # (`rb_head_RB_INSERT`, `rb_head_RB_REMOVE`, `rb_head_RB_MINMAX`, `rb_node_compare`,
    # `update_holes_on_entry_creation`, `update_holes_on_entry_deletion`, `vm_map_combine_hole`,
    # `vm_map_delete_hole`) and the hole-list code that allocates from `vm_map_holes_zone` - which is a
    # storage definition in the already-linked `osfmk_vm_vm_map.o`, so unlike most storage symbols in
    # this sequence it is real data rather than a stand-in.
    #
    # The object 246's stop named: `vm_map_store_init_rb` is its first definition, and the call to it
    # was a tail call - which is why 246's `xnu_entry_stub_caller` was `vm_map_create+0x5c` rather than
    # an address inside `vm_map_store_init`.
    #
    # **247 measured it, and it is the step where a whole function finished.** `vm_map_store_init_rb`
    # is three instructions (`mov r1, #0` / `str r1, [r0, #24]` / `bx lr`), so like 246's `bx lr` it
    # moves the stop rather than resolving it - and this time out of the dispatcher, because
    # `kmem_init`'s whole closure turned out to be real. Enumerated from the disassembly, call target
    # by call target, before the run: `kmem_init` -> `vm_map_create`, `pmap_virtual_region`,
    # `vm_map_enter`, `panic`, `__aeabi_uldivmod`; `vm_map_create` -> `zalloc` (twice),
    # `vm_map_store_init`, `lck_rw_init`, `lck_mtx_init_ext`. So the prediction was that `kmem_init`
    # completes and the stop moves to `vm_mem_bootstrap`'s next call after it, whose candidates are
    # the `kmapoff`/`early_random` block: `vm_allocate_kernel` (`+0xf4`, a stub) when
    # `early_random() & 0x1ff` is non-zero, `kext_alloc_init` (`+0x1a4`, a stub) when it is zero.
    #
    # Device: **`stub_hit=vm_allocate_kernel`, `xnu_entry_stub_caller=0x800403f4`** =
    # `vm_mem_bootstrap+0xf8`, whose `caller - 4` is `800403f0: bl 80085030 <vm_allocate_kernel>` - the
    # call site itself, this call being a `bl` (246's was a `b`, and that is how the two are told
    # apart). `kv_written == kv_in_dram == 0x3f`. And the run measured four things by *completing*
    # them rather than by predicting them: `kernel_map` was created by `vm_map_create` in full (zone
    # allocation, both stores, the hole list, `lck_rw_init`, `lck_mtx_init_ext`);
    # `pmap_virtual_region(0, ...)` returned TRUE for `[0x80000000, 0x40000000)` so `vm_map_enter`
    # reserved a gigabyte in it and did not panic; `PE_parse_boot_argn("kmapoff")` returned FALSE (our
    # boot args have no such key); and `early_random()` ran its whole first-call path - seed from
    # `PE_get_random_seed`, `ccdrbg_factory_nisthmac`, `ccdrbg_init` with a `ml_get_timebase()` nonce,
    # `ccdrbg_generate` - returning a value whose low nine bits are not zero. See the experiment
    # document for the marker that makes each of those a measurement rather than an absence.
    #
    # Cost: text 611793 -> 617809 (+6016), resolved 7 and added 0 (`vm_map_store_lookup_entry_rb` was
    # linked but was never a stub; `vm_map_store_has_RB_support` is still inlined into the dispatcher's
    # `cmp`), 626 undefined, 541 function and 85 storage stubs, and **the image, `.bss`,
    # `__entry_image_end`, the layout, the headroom and the payload's size are unchanged for the third
    # step running** - the alignment padding has absorbed the 6464 bytes of text that 245, 246 and 247
    # added between them.
    #
    # Next: `vm_allocate_kernel` is in `osfmk_vm_vm_user.o` - 16196 bytes of text, 91 references, so
    # unlike the last three steps it brings a great deal with it (`vm_map_copy*`, `vm_map_protect`,
    # `vm_map_remove`, `vm_map_wire_kernel`, the `upl_*` family, and new `ipc_port_*` and
    # `memory_object_*` stubs).
    OSFMK_VM_VM_MAP_STORE_RB_OBJ=${STAGE90_ENTRY_OSFMK_VM_VM_MAP_STORE_RB_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_vm_vm_map_store_rb.o}
    # `osfmk/vm/vm_user.c` -> **16196 bytes of text, 25856 bytes of file, 91 references** - the object
    # `vm_allocate_kernel` is in, and the largest step since this sequence began in earnest. Unlike
    # 245, 246 and 247 it brings a great deal with it: `vm_map_copy_discard`, `vm_map_copy_extract`,
    # `vm_map_copyin_common`, `vm_map_copyout`, `vm_map_copy_overwrite`, `vm_map_protect`,
    # `vm_map_remove`, `vm_map_wire_kernel`, `vm_map_msync`, the `upl_*` family, and references to
    # `ipc_port_*`, `memory_object_*`, `vm_object_*` and `vm_page_*` that will mostly be new stubs.
    #
    # The stop it answers is `vm_mem_bootstrap+0xf4`: `vm_allocate_kernel(kernel_map, &kmapoff_kaddr,
    # kmapoff_pgcnt * PAGE_SIZE_64, VM_FLAGS_ANYWHERE, VM_KERN_MEMORY_OSFMK)`, called because
    # `PE_parse_boot_argn("kmapoff", ...)` returned FALSE and `early_random() & 0x1ff` was non-zero.
    #
    # Its text is more than twice anything linked since 239, so this is the step where the entry
    # image may finally move an address again: 245, 246 and 247 fitted inside the linker script's
    # alignment padding, and 16 KB is more than three times what those three took between them.
    #
    # **248 measured it: it does, and it stops where the source says it should.** Resolved 9, added 6
    # (`ipc_kobject_set`, `ipc_port_alloc_special`, `ipc_port_copy_send`, `ipc_port_nsrequest`,
    # `ipc_space_kernel`, `vm_fault` - the Mach IPC port the memory-entry path needs and the page-fault
    # entry) so 626 -> 623 undefined, 541 -> 539 function and 85 -> 84 storage stubs, `log_executable_mem_entry`
    # being the storage one. Text 617809 -> 634033 (+16224) **did not fit**: the image grew 719736 ->
    # 736192, `.bss` moved 0x800af4f0..0x800de208 -> 0x800b34f0..0x800e2208, `__entry_image_end` 0x800de208
    # -> 0x800e2208 with `end_kern` 0x800df000 -> 0x800e3000, the derived `boot_args` offset 917504 ->
    # 933888, headroom 1187320 -> 1170936, and the payload was rebuilt from the regenerated header. The
    # three preceding steps had all fitted in the alignment padding; this is where those "unchanged"
    # rows end, which is what the layout block exists for.
    #
    # Device: **`stub_hit=kext_alloc_init`, `xnu_entry_stub_caller=0x800404a4`** = `vm_mem_bootstrap+0x1a8`,
    # whose `caller - 4` is `800404a0: bl 800873e4 <kext_alloc_init>` - one call past a platform pmap
    # initialisation, exactly as predicted. `kv_written == kv_in_dram == 0x3c`. What ran, in order, by
    # *completing*: `vm_allocate_kernel` returned KERN_SUCCESS (its argument checks passed and its one
    # call, `vm_map_enter`, entered `kmapoff_pgcnt * 4096` bytes into `kernel_map` - a real kernel
    # virtual allocation through the map code 247 created); `PE_parse_boot_argn("log_executable_mem_entry")`
    # ran; **`pmap_init()` ran**; `kmem_alloc_ready = TRUE` was stored; and
    # `PE_parse_boot_argn("zsize")` returned FALSE so `zsize = sane_size >> 2` and its clamps ran.
    # `zone_init` (`+0x204`) is real and is the step after this stub.
    #
    # Next: `kext_alloc_init` is in `osfmk_kern_kext_alloc.o` - 344 bytes of text, four definitions
    # (`kext_alloc_init`, `kext_alloc`, `kext_free`, `g_kext_map`) and four references, of which
    # `mach_vm_allocate_kernel` and `mach_vm_deallocate` are still stubs and `mach_vm_allocate_kernel`
    # is in the object this step links.
    OSFMK_VM_VM_USER_OBJ=${STAGE90_ENTRY_OSFMK_VM_VM_USER_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_vm_vm_user.o}
    # `osfmk/kern/kext_alloc.c` -> **344 bytes of text, four definitions, four references**. The
    # object 248's stop named: `kext_alloc_init` is the function `vm_mem_bootstrap+0x1a4` calls, and
    # it is small enough that its whole closure can be read off the disassembly before the run.
    #
    # Its references are `_consume_printf_args` (real), `kernel_map` (real, a storage definition in
    # `osfmk_vm_vm_kern.o` since 244) and `mach_vm_allocate_kernel` / `mach_vm_deallocate` - still
    # stubs, and the first of them is in the very object 248 linked. So unlike 245 to 248 this step can
    # be answered by its own predecessor's object, and the two `mach_vm_*` names are the candidates.
    #
    # What it is for, from the source: it carves `g_kext_map` out of `kernel_map` with
    # `kmem_suballoc`-style arithmetic and panics if the region does not fit ("kext_alloc_init:
    # kmem_suballoc failed"), which makes it the first function on this path whose *purpose* is to
    # reserve address space for something that does not exist yet in this kernel - there are no kexts
    # in this image at all.
    # **249 measured it, and the object is not the source's function.** The built `kext_alloc_init` is
    # fifteen instructions with **not one call** - two `.bss` flags set to 1 and one word copied -
    # because the whole 2 GB-reservation body is inside `#if CONFIG_KEXT_BASEMENT`, which is x86-only.
    # So the build resolved 1 (`kext_alloc_init`) and added 0 (623 -> 622 undefined, 539 -> 538
    # function stubs, storage 84), text 634033 -> 634321 (+288), and the stop had to move again.
    #
    # Which it did, twice: `zone_init` (`+0x204`) and `vm_page_module_init` (`+0x214`) are real, and
    # `zone_init` was walked as well (no stub on its straight-line closure), so the prediction was
    # `kalloc_init` at `+0x224`. Device: **`stub_hit=kalloc_init`, `xnu_entry_stub_caller=0x80040524`**
    # = `vm_mem_bootstrap+0x228`, whose `caller - 4` is `80040520: bl 800873d4 <kalloc_init>`.
    # `kv_written == kv_in_dram == 0x38`. **`zone_init` ran** - the largest thing this sequence has
    # executed, the function that creates the zone map and every zone in it, and the only writer of the
    # two globals (`zone_map_min_address`, `zone_map_max_address`) that experiment 239 read as zero and
    # explained with "`zone_init` has never executed in this image". Its first call, `kmem_suballoc`, is
    # answered by 244's object.
    #
    # Image unchanged (`.bss` end 0x800e2208 -> 0x800e2248, `__entry_image_end`/`end_kern`/layout
    # unmoved, headroom 1170872), payload text unchanged, `persistent_write_attempted=0x00000000` in
    # all 25 contracts.
    #
    # Next: `kalloc_init` is in `osfmk_kern_kalloc.o` - 4472 bytes of text, 33 references, defining the
    # whole `kalloc`/`kfree`/`kalloc_canblock` family, so the next stop is more likely to be one of its
    # still-stubbed references than another whole function.
    # 250: `osfmk_kern_kalloc.o` - 4472 bytes of text, 33 references, 35 global definitions, and the
    # whole `kalloc`/`kfree`/`kalloc_canblock`/`OSMalloc` family. Six times 249's object, and the first
    # time since 248 that the build is likely to *add* stubs as well as resolve them.
    #
    # Resolved 6, added 0 - measured by building the step twice, once with the object and once with an
    # empty stand-in in its place (the stand-in build reproduces 249's 622 / 538 / 84 exactly):
    #   resolved (functions) kalloc_canblock  kalloc_init  kfree
    #   resolved (storage)   kalloc_large_total  kalloc_map  kfree_nop_count
    # Only 6 of the object's 35 definitions had been stubs - the rest were never referenced by
    # anything in the image. 622 -> 616 undefined, 538 -> 535 function and 84 -> 81 storage stubs.
    #
    # `kalloc_init` is 210 bytes and every call in it is real (`kmem_suballoc`, `panic`, `zinit`,
    # `zone_change` x2, `lck_grp_init`, `lck_mtx_init`, `lck_grp_alloc_init`), and
    # `xnu_entry_callwalk.py --root kalloc_init` found no stub on its straight-line path, so the
    # prediction was that it *completes* and the stop moves to the next stub in `vm_mem_bootstrap`:
    # `+0x234 vm_fault_init` (STUB), with `+0x244 memory_manager_default_init`,
    # `+0x254 memory_object_control_bootstrap` and `+0x264 device_pager_bootstrap` also stubs.
    #
    # Device: **`stub_hit=vm_fault_init`, `xnu_entry_stub_caller=0x80040534`** =
    # `vm_mem_bootstrap+0x238`, whose `caller - 4` is `80040530: bl 8008a3f4 <vm_fault_init>`.
    # `kv_written == kv_in_dram == 0x3a`. **`kalloc_init` completed**, so on this hardware: the kalloc
    # map was created by `kmem_suballoc(kernel_map, ...)` and the `panic` on its failure was not taken;
    # `kalloc_max = 16384`, `kalloc_max_prerounded = 8193`, `kalloc_kernmap_size =
    # kalloc_largest_allocated = 0x40001` were stored; the `zinit` loop created 28 zones (the ARM
    # `k_zone_size` table at `kalloc.c:192-205` reaches 8192 below `kalloc_max`, and 16384 stops it) -
    # 28 `zinit` and 56 `zone_change` calls on top of the zone system `zone_init` had just built; the
    # 256-entry `k_zone_dlut` was built and `k_zindex_start` set; and `kalloc_lck_grp`/`kalloc_lock`
    # were initialised, with `OSMalloc_init()` **inlined** at the end (the self-linked queue head, then
    # `lck_grp_alloc_init` stored into `OSMalloc_tag_lck_grp`).
    #
    # Image: text 634321 -> 639953 (+5632), image 736192 -> 752648 (+16456) so **the image moved for
    # the second step in a row**: `.bss` 0x800e2248 -> 0x800e64c8, `__entry_image_end`/`end_kern`
    # 0x800e64c8/0x800e7000, layout args 933888 -> 950272, headroom 1153848. Payload text 1244866.
    # `persistent_write_attempted=0x00000000` in all 25 contracts, `failure_mask=0x00000000` in all 87.
    #
    # Next: `vm_fault_init` is in `osfmk_vm_vm_fault.o` - 30392 bytes of text and 156 references, the
    # largest object in this sequence and twice 248's `vm_user.o`, so its closure cannot be read in
    # full from the disassembly the way 244-250 were; the prediction for 251 is about `vm_fault_init`'s
    # own closure rather than the fault path whose name the object carries.
    OSFMK_KERN_KALLOC_OBJ=${STAGE90_ENTRY_OSFMK_KERN_KALLOC_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_kalloc.o}
    # 251: `osfmk_vm_vm_fault.o` - 30392 bytes of text (31667-byte object), 156 references, 58 global
    # definitions: the fault path (`vm_fault`, `vm_fault_page`, `vm_fault_enter`, `vm_fault_wire`/
    # `_unwire`, `vm_fault_copy`, `vm_pre_fault`, `kdp_lightweight_fault`, the `vm_page_validate_cs`/
    # `vm_cs_*` code-signing validation) and the counters. Twice 248's `vm_user.o`.
    #
    # Resolved 9, added 15 - the first step since 248 that brings more new frontiers than it closes:
    #   resolved vm_fault vm_fault_cleanup vm_fault_copy vm_fault_enter vm_fault_init vm_fault_page
    #            vm_fault_unwire vm_fault_wire vm_page_validate_cs
    #   added    cs_enforcement cs_invalid_page cs_validate_range current_thread_aborted
    #            kcdata_estimate_required_buffer_size kcdata_get_memory_addr
    #            os_reason_alloc_buffer_noblock panic_on_cs_killed set_thread_exit_reason
    #            task_update_logical_writes throttle_lowpri_io vm_compressor_pager_get
    #            vnode_pager_cs_check_validation_bitmap vnode_pager_get_object_mtime
    #            vnode_pager_get_object_name
    # 616 -> 622 undefined, 535 -> 540 function and 81 -> 82 storage stubs, text 639953 -> 671505
    # (+31552). None of the added fifteen is reached by this run - the stop is earlier, in
    # `vm_mem_bootstrap`; they are the code-signing/vnode-pager boundaries the fault path reaches out to.
    #
    # `vm_fault_init` is the short *bootstrap* half of the file: four calls, all real
    # (`__aeabi_uldivmod`, `PE_parse_boot_argn("vm_compressor")`, `_consume_printf_args` on the
    # "Ignoring" path, `PE_get_default("kern.vm_compressor")`), and `xnu_entry_callwalk.py --root
    # vm_fault_init` found no stub on its straight-line path, so the prediction was that it completes
    # and the stop moves to `memory_manager_default_init` (`+0x244`).
    #
    # Device: **`stub_hit=memory_manager_default_init`, `xnu_entry_stub_caller=0x80040544`** =
    # `vm_mem_bootstrap+0x248`, whose `caller - 4` is `80040540: bl 80088dbc <memory_manager_default_init>`.
    # `kv_written == kv_in_dram == 0x48`. **`vm_fault_init` completed**: `vm_hard_throttle_threshold =
    # sane_size * (35 - MIN(sane_size / 1 GB, 25)) / 100` was computed and stored to 0x800e8d88 (the
    # `cmp r0, #25 / rsblt r1, r0, #35` and the 64-bit `__aeabi_uldivmod` by 100 are both in the
    # instruction stream); `PE_parse_boot_argn("vm_compressor")` returned FALSE so the
    # `VM_PAGER_MAX_MODES` bit-test loop was *not* entered and `_consume_printf_args` was not called;
    # the device-tree fallback `PE_get_default("kern.vm_compressor", &vm_compressor_mode, 4)` ran; and
    # `PE_parse_boot_argn("vm_compressor_threads", &vm_compressor_thread_count, 4)` ran.
    #
    # Image moved for the third step (248, 250, 251): image 752648 -> 769072 (+16424), `.bss`
    # 0x800bb500-0x800ea548, layout args 950272 -> 966656, headroom **1137336** bytes below
    # topOfKernelData - a little over a megabyte, and the number to watch. Payload text 1261290
    # (+16424). `persistent_write_attempted=0x00000000` in all 25 contracts, `failure_mask=0x00000000`
    # in all 87.
    #
    # Next: `memory_manager_default_init` is in `osfmk_vm_memory_object.o` (9508 bytes of text, 58
    # references, 58 definitions) - and that same object also defines `memory_object_control_bootstrap`,
    # the *next* stub in `vm_mem_bootstrap` (`+0x254`), so one object may close two stops.
    OSFMK_VM_VM_FAULT_OBJ=${STAGE90_ENTRY_OSFMK_VM_VM_FAULT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_vm_vm_fault.o}
    # 252: `osfmk_vm_memory_object.o` - 9508 bytes of text, 58 references, 58 definitions: the
    # memory-object and memory-object-control layer, and (because the frontier asked for it) **both** of
    # the next two stops in `vm_mem_bootstrap`, `memory_manager_default_init` (`+0x244`) and
    # `memory_object_control_bootstrap` (`+0x254`).
    #
    # Resolved 18, added 1 (`ipc_port_make_send`) - the reverse of 251's shape, and all 18 are names this
    # image had been stubbing since it started: memory_object_init/_terminate/_deallocate/_map, the
    # `_data_*` family, memory_object_control_allocate/_collapse/_disable/_to_vm_object,
    # memory_object_to_vm_object, vm_object_sync, vm_object_update. 622 -> 605 undefined, 540 -> 523
    # function stubs, storage unchanged at 82. Text 671505 -> 680369 (+8864).
    #
    # Both target functions are short and neither calls a stub:
    #   memory_manager_default_init      str r1,[r0] (memory_manager_default = NULL) then `b lck_mtx_init`
    #   memory_object_control_bootstrap  bl zinit(8, 0x10000, 4096, "mem_obj_control"),
    #                                    bl zone_change(zone, Z_CALLERACCT=5, 0),
    #                                    `b zone_change(zone, Z_NOENCRYPT=6, 1)`
    # `xnu_entry_callwalk.py` found no stub on either closure, so the prediction was that the run passes
    # **two** stops in one step and lands on `device_pager_bootstrap` (`+0x264`).
    #
    # Device: **`stub_hit=device_pager_bootstrap`, `xnu_entry_stub_caller=0x80040564`** =
    # `vm_mem_bootstrap+0x268`, whose `caller - 4` is `80040560: bl 80091a08 <device_pager_bootstrap>`.
    # `kv_written == kv_in_dram == 0x43`. Both completed: `memory_manager_default` set to
    # MEMORY_OBJECT_DEFAULT_NULL and `lck_mtx_init(&memory_manager_default_lock, &vm_object_lck_grp,
    # &vm_object_lck_attr)` tail-called; `mem_obj_control_zone` created by `zinit` (the immediate 8 is
    # `sizeof(struct memory_object_control)`, so the maxmem argument is the source's `8192*i`) and the
    # two `zone_change` calls made - Z_CALLERACCT FALSE and **Z_NOENCRYPT TRUE**, this zone never being
    # written to disk during hibernation.
    #
    # Note both functions END IN A TAIL CALL (`b lck_mtx_init`, `b zone_change`) - the shape 246
    # identified. Had either callee been a stub, the caller key would have named `vm_mem_bootstrap` and
    # `caller - 4` would have pointed at `bl memory_manager_default_init`: a different symbol. The rule
    # stands - resolve `caller - 4` against the image the run used and compare its target with the stub
    # that was hit.
    #
    # Image moved for the **fourth** step in a row (248, 250, 251, 252): image 785456 (+16384), `.bss`
    # 0x800bf500-0x800ee548, layout args 966656 -> 983040, headroom **1120952** bytes below
    # topOfKernelData. Payload text 1277674. `persistent_write_attempted=0x00000000` in all 25
    # contracts, `failure_mask=0x00000000` in all 87.
    #
    # Next: `device_pager_bootstrap` is in `osfmk_vm_device_vm.o` - 1536 bytes of text, 28 references, 22
    # definitions - small enough to read in full. `vm_paging_map_init` (`+0x274`) is already real.
    OSFMK_VM_MEMORY_OBJECT_OBJ=${STAGE90_ENTRY_OSFMK_VM_MEMORY_OBJECT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_vm_memory_object.o}
    # 253: `osfmk_vm_device_vm.o` - 1536 bytes of text, 28 references, 22 definitions: the device-pager
    # layer (`device_pager_init`/`_setup`/`_lookup`/`_map`/`_reference`/`_deallocate`/`_terminate`, the
    # `_data_*` family, `_populate_object`, `_synchronize`, `device_pager_ops` and three lock attrs).
    #
    # Resolved 2, added 2 - the first step where they are equal:
    #   resolved device_pager_bootstrap  is_device_pager_ops
    #   added    device_close  device_data_action
    # so 605 undefined before and after (523 function and 82 storage stubs), text 680369 -> 682353.
    #
    # `device_pager_bootstrap` is five statements and none calls a stub (`zinit(40, MAX_DNODE*40 =
    # 0x61a80, PAGE_SIZE, "device node pager structures")`, `zone_change(Z_CALLERACCT, FALSE)`,
    # `lck_grp_attr_setdefault`, `lck_grp_init(&device_pager_lck_grp, "device_pager", ...)`, and a
    # **tail call** to `lck_attr_setdefault`), and `xnu_entry_callwalk.py` confirmed it. What made this
    # step different is what follows: `vm_mem_bootstrap` has only `vm_paging_map_init` (real) and a
    # `kernel_debug_string_early` after it before `pop {r4, r5, r6, r7, fp, pc}` - so the prediction was
    # that `vm_mem_bootstrap` **returns** and `kernel_bootstrap` stops at `cs_init` (`+0x130`).
    #
    # Device: **`stub_hit=cs_init`, `xnu_entry_stub_caller=0x8000db74`** = `kernel_bootstrap+0x134`,
    # whose `caller - 4` is `8000db70: bl 80091f04 <cs_init>`. `kv_written == kv_in_dram == 0x34`.
    # **`vm_mem_bootstrap` returned** - the first stop outside it since 247 began it, six steps ago
    # (247 vm_allocate_kernel +0xf8, 248 kext_alloc_init +0x1a8, 249 kalloc_init +0x228,
    # 250 vm_fault_init +0x238, 251 memory_manager_default_init +0x248, 252 device_pager_bootstrap
    # +0x268, 253 cs_init from kernel_bootstrap+0x134). Everything that function does has now run or
    # been stepped over, `device_pager_bootstrap` and `vm_paging_map_init` included: `device_pager_zone`
    # created by `zinit` (the immediate 40 is `sizeof(struct device_pager)`, 0x61a80 is `MAX_DNODE*40`),
    # `zone_change` made, and a third lock group initialised (`lck_grp_attr_setdefault`,
    # `lck_grp_init("device_pager")`, tail-called `lck_attr_setdefault`).
    #
    # And the stop is now in **`cs_init` (`bsd/kern/kern_cs.c`) - the first BSD-layer function this
    # image has ever been stopped at**; every previous stop in this sequence was in `osfmk`.
    #
    # Image **unchanged** (first time in five steps - the 1984 bytes of text fitted in the alignment
    # padding): `__entry_image_end`/`end_kern`/layout/payload all where 252 left them, `.bss` end
    # 0x800ee688, headroom 1120632. `persistent_write_attempted=0x00000000` in all 25 contracts,
    # `failure_mask=0x00000000` in all 87.
    #
    # Next: `cs_init` is in `bsd_kern_kern_cs.o` - 2120 bytes of text, 21 references - and it is where
    # 251's fifteen boundaries (`cs_enforcement`, `cs_invalid_page`, `cs_validate_range`,
    # `vnode_pager_cs_check_validation_bitmap`, `panic_on_cs_killed`) are finally wanted from. Past it
    # in `kernel_bootstrap`: `vm_mem_init` (real), `oslog_init` (real), `telemetry_init` (STUB).
    OSFMK_VM_DEVICE_VM_OBJ=${STAGE90_ENTRY_OSFMK_VM_DEVICE_VM_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_vm_device_vm.o}
    # 254: `bsd_kern_kern_cs.o` - 2120 bytes of text, 21 references - the code-signing subsystem
    # (`cs_init`, `cs_enforcement`, `cs_invalid_page`, `cs_debug`, `panic_on_cs_killed`, the trust-cache
    # machinery). Resolved 5, added 13 - and the 13 arrive together, which is what entering `bsd/kern`
    # for the first time looks like:
    #   resolved cs_debug  cs_enforcement  cs_init  cs_invalid_page  panic_on_cs_killed
    #   added    csblob_find_blob csblob_get_entitlements cs_hash_type osobject_retain proc_lock
    #            proc_unlock sysctl__vm_children threadsignal ubc_cs_blob_get UBCINFOEXISTS
    #            vn_getpath vnode_lock vnode_unlock
    # 605 -> 613 undefined, 523 -> 532 function and 82 -> 81 storage stubs, text 684993 (+2640).
    #
    # **The prediction was WRONG, and the reason matters.** `cs_init` is short and calls no stub
    # (`PE_parse_boot_argn`, `lck_grp_attr_alloc_init`, `lck_grp_alloc_init`, tail `lck_grp_attr_free`)
    # so it should complete; the prediction for what follows came from `xnu_entry_callwalk.py --root
    # oslog_init`, which names `__firehose_buffer_create` - and it was written as
    # `stub_hit=__firehose_buffer_create, caller oslog_init+0x70`. Two causes: (1) the walker's output
    # was read through `head -3`, so its conditional-alternatives paragraph - the one that mattered -
    # was never on screen; (2) the walker's closure is not the executed path, because it treats
    # `kmem_alloc_flags` (oslog_init's call at `+0x3c`, *before* the firehose call at `+0x70`) as clean
    # while the real path goes through it into the pmap. "No stub on the straight-line path" is a
    # statement about the model, not about the run.
    #
    # Device: **`stub_hit=ledger_credit`, `xnu_entry_stub_caller=0x80029cfc`**, whose `caller - 4` is
    # `80029cf8: bl 80093454 <ledger_credit>` - inside the function starting at 0x800298ac, which nm
    # labels `pmap_expand`. The three inlined `pmap_tt_ledger_credit` sites from `pmap.c:3631/3639/3647`
    # are at 0x80029cd0/0x80029ce4/0x80029cf8 and the `beq` at `+0x410` was **taken**, skipping the
    # first two - a measured fact, since `ledger_credit` is a stub and the run could not have passed the
    # first. `kv_written == kv_in_dram == 0x3a`.
    #
    # Caution: nm -S reports `pmap_expand` as 538 bytes while the stop is 0x450 bytes into it, and the
    # disassembly shows no `pop {..., pc}` between 0x80029a00 and 0x80029d40 - one function runs
    # through all of it, so the symbol's size field is not the function's extent. Identify the function
    # by address-minus-base, not by the size column.
    #
    # What it means: `cs_init` completed; `vm_mem_init` is a single `b vm_object_init` and
    # **`vm_object_init` is `bx lr`** (four bytes); `oslog_init` ran and its `kmem_alloc_flags` call is
    # where the run went - allocating the OS-log buffer is the first kernel-map allocation that has ever
    # needed *new page tables*, so it entered the pmap's expansion path. **The frontier has changed
    # character**: for thirty-odd steps the stop was the next *initialisation* function; this one is
    # inside a runtime memory allocation several frames deep in the ARM pmap.
    #
    # Image did not move: 785648 (+192), layout args 983040 unchanged, headroom 1120504, payload text
    # 1277866. `persistent_write_attempted=0x00000000` in all 25 contracts,
    # `failure_mask=0x00000000` in all 87.
    #
    # Next: `ledger_credit` is in `osfmk_kern_ledger.o` - 8324 bytes of text, 35 references - a
    # *runtime* object, which fits where the frontier now is. Read the WHOLE callwalk output for 255.
    BSD_KERN_KERN_CS_OBJ=${STAGE90_ENTRY_BSD_KERN_KERN_CS_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/bsd_kern_kern_cs.o}
    # 255: `osfmk_kern_ledger.o` - 8324 bytes of text, 35 references - the ledger subsystem.
    # Resolved **19**, added 1 (`thread_block_reason`):
    #   resolved ledger_credit ledger_debit ledger_dereference ledger_disable_callback ledger_entry_add
    #            ledger_entry_setactive ledger_get_entry_info ledger_get_limit ledger_get_period
    #            ledger_init ledger_instantiate ledger_reference ledger_rollup ledger_set_action
    #            ledger_set_callback ledger_set_limit ledger_set_period ledger_template_complete
    #            ledger_template_create
    # 613 -> 595 undefined, 532 -> 514 function stubs, text 692865 (+7872).
    #
    # With the ledger real, every direct call in `pmap_expand` after 254's stop (`ptd_alloc`,
    # `lck_spin_lock`, `lck_spin_unlock`, `pmap_tt_deallocate`) and in `pmap_enter_options` is real, so
    # the allocation should complete and `oslog_init` should reach the call 254 could not:
    # **prediction `stub_hit=__firehose_buffer_create`, caller `oslog_init+0x70`** - the same name as
    # 254's wrong answer but for the opposite reason (the call in between is now linked).
    #
    # Device: **`stub_hit=__firehose_buffer_create`, `xnu_entry_stub_caller=0x8002cd5c`** =
    # `oslog_init+0x70`, `caller - 4` = `8002cd58: bl 80094a30 <__firehose_buffer_create>`.
    # `kv_written == kv_in_dram == 0x45`. **The allocation completed**: `oslog_init`'s compiled
    # constants say so - `mov r2, #73728` (`size + 2*PAGE_SIZE`, size = 65536 = 16 chunks x 4096),
    # `r3 = 19` (`VM_KERN_MEMORY_LOG`), stack arg 48 (`KMA_GUARD_FIRST|KMA_GUARD_LAST`) - so this run
    # made a **73728-byte guarded allocation from `kernel_map`**, the first kernel-memory allocation
    # this kernel has made for a purpose rather than to initialise itself. It went into the pmap (254's
    # `ledger_credit`), page tables were expanded, the ledger accounted for it, `__bzero` cleared it,
    # and the `panic("Failed to allocate memory for firehose logging buffer")` at `+0x48` was not taken.
    #
    # **THE FINDING: `__firehose_buffer_create` is not implemented anywhere in this source tree.** It is
    # called at `bsd/kern/subr_log.c:874` and defined nowhere - not osfmk, not bsd, not libkern.
    # `libkern/firehose/` ships headers with **`KERNELFILES =` empty** in its Makefile: Apple's firehose
    # is a closed kernel library/kext, and `FIREHOSE_BUFFER_KERNEL_CHUNK_COUNT` is not in the tree
    # either (the compiled constant says 16). This is the first frontier symbol in the whole sequence
    # whose implementation does not exist in the tree, so 256 is a **decision, not another link**:
    # (1) provide it from the entry image - it cannot be a stop-on-call stub, because `oslog_init` uses
    # its return value as `kernel_firehose_addr` and the kernel logs there from then on, so it needs a
    # minimal real implementation or a handoff returning the already-allocated buffer; or (2) build it
    # from the shipped headers against the in-tree `libkern/os/log.c`, which does know the chunk layout.
    #
    # Image moved: 802176 (+16528), `.bss` 0x800c35c0-0x800f28c8, layout args 983040 -> 999424,
    # headroom **1103672** bytes, payload text 1294394. `persistent_write_attempted=0x00000000` in all
    # 25 contracts, `failure_mask=0x00000000` in all 87.
    OSFMK_KERN_LEDGER_OBJ=${STAGE90_ENTRY_OSFMK_KERN_LEDGER_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_ledger.o}
    # 257: the firehose, **ported rather than linked**. 254 and 255 stopped at `__firehose_buffer_create`
    # and 256 measured that no object in the tree defines it (`libkern/firehose/Makefile` has
    # `KERNELFILES =` empty). The implementation is Apple's own, from
    # `apple-oss-distributions/libdispatch` (`src/firehose/firehose_buffer.c`, Apache-2.0, unmodified),
    # compiled for armv7 freestanding by `build_xnu_arm_kernel.sh`'s `FIREHOSE_SOURCES` block, with the
    # newer tree's `os/` and firehose headers in `stages/stage90/firehose/portinc/` shadowing the tree's.
    # **First time this project builds a component Apple ships outside the tree.**
    #
    # The object: 4096 bytes of text, 12 references. Resolved 2, added 6:
    #   resolved __firehose_buffer_create  __firehose_merge_updates
    #   added    __firehose_allocate  __firehose_buffer_kernel_chunk_count
    #            __firehose_buffer_push_to_logd  __firehose_critical_region_enter
    #            __firehose_critical_region_leave  __firehose_num_kernel_io_pages
    # 595 -> 599 undefined, 514 -> 518 function stubs, text 697217 (+4352), image/layout/payload all
    # unchanged (the +4352 fitted in the alignment padding).
    #
    # `__firehose_buffer_create` calls exactly one thing on the first-call path: `firehose_buffer_create`
    # (in the same object), whose only call is `__firehose_allocate`. **Prediction: `stub_hit=__firehose_allocate`,
    # caller `firehose_buffer_create+0x28`.** Device: **`stub_hit=__firehose_allocate`,
    # `xnu_entry_stub_caller=0x8009411c`** = `firehose_buffer_create+0x28`, `caller - 4` =
    # `80094118: bl 80095a30 <__firehose_allocate>`. `kv_written == kv_in_dram == 0x40`. **The ported code
    # ran**: the stop moved *inside Apple's firehose implementation*, not to the next missing XNU symbol.
    #
    # Note one of the six: `__firehose_buffer_kernel_chunk_count` is **read as data** (`ldrb r1, [symbol]`),
    # not called, so a stub there is *silent* - it would hand `oslog_init` a garbage size instead of
    # stopping. The `mi4-stand-in-size-is-not-value` shape in a new place.
    #
    # Incident worth knowing: `build_xnu_arm_kernel.sh --limit 1` **truncates the object directory at the
    # start** and then builds one file, so it emptied `out/xnu_kernel_obj` (695 objects -> 0) and cost a
    # full rebuild (612/615 C, 83/83 C++ reproduce). `--limit` is not a dry run.
    #
    # Next: the six added names are the kernel's side of the interface; the four that are functions plus
    # `__firehose_num_kernel_io_pages` are what the ported code calls next, and in a newer XNU they live in
    # `osfmk/kern/firehose.c`, which 4570 does not have - the same port problem one layer down, same
    # remedy (a second entry in `FIREHOSE_SOURCES`). `libkern/os/log.c:580` is in the tree and defines
    # `__firehose_allocate`, so that one may come from the tarball.
    FIREHOSE_OBJ=${STAGE90_ENTRY_FIREHOSE_OBJ:-$REPO_ROOT/out/xnu_firehose_obj/firehose_buffer.o}
    # 258: the firehose's **kernel side**, and with it the port is complete.
    #
    # 257's stop was `__firehose_allocate`, called from `firehose_buffer_create` - a call *inside*
    # Apple's implementation, and the link then had six names it could not resolve. Four of them are
    # defined **in the tree**, by an object the manifest already compiles and this entry image simply
    # had not linked yet: `external/xnu-4570.1.46/libkern/os/log.c` is in
    # `out/xnu_arm_manifest.txt:403`, and `out/xnu_kernel_obj/libkern_os_log.o` defines
    # `__firehose_allocate` (`:577`), `__firehose_buffer_push_to_logd` (`:571`),
    # `__firehose_critical_region_enter` (`:596`) and `__firehose_critical_region_leave` (`:602`).
    # So this is the oldest method in this project - link the object that defines them - and not a
    # second port. Experiment 162 had already found `__firehose_allocate` at `log.c:580`; the
    # measurement here is that all four are in **one** object.
    #
    # Measured before the run: the object is 5551 bytes of text, 68 of data, 49 of bss, with 26
    # definitions and **44 references, 40 of which this image already satisfies**. That is the number
    # that makes this cheap: the four new ones are `atm_get_diagnostic_config`,
    # `mach_continuous_approximate_time`, `OSKextKextForAddress` and `_os_trace_addr_in_text_segment`,
    # all off the first-call path. Nine of its definitions currently exist here as stubs
    # (`_os_log_internal`, `os_log_with_args`, `_os_log_default`, `startup_serial_logging_active`,
    # `oslog_s_error_count`, and the four `__firehose_*` functions); linking the object replaces them,
    # because the stub set is generated from the undefined list *after* the link attempt.
    #
    # The other two of the six are **not functions and not in the tree**:
    # `portinc/os/firehose_buffer_private.h:60-61` declares both as `uint8_t`, and
    # `firehose_buffer_create` reads them with `ldrb` - so a stub there is read as *data* and would not
    # stop the run at all, it would hand the buffer `0x40 << 12` (257 recorded the byte). Their
    # definitions are in Apple's closed `libfirehose_kernel` library, and are supplied by the port as
    # `stages/stage90/firehose/firehose_kernel_config.c`: **16** chunks and **8** io pages, read from
    # the newer header's `FIREHOSE_BUFFER_KERNEL_DEFAULT_CHUNK_COUNT` / `_IO_PAGES` and agreeing with
    # the 16 this project's shim gave `oslog_init`, which is what sized the 73728-byte allocation 255
    # measured. So the port is two files now: `firehose_buffer.c` (Apple's) and this one.
    #
    # **The prediction.** `firehose_buffer_create` has exactly one call - `__firehose_allocate` - so
    # once it is real the whole create path completes: the header at `kernel_firehose_addr`, the
    # 15-entry ring, the bank split (15 - 8 = 7), the write-back of `size`. `oslog_init` then returns
    # to its caller, and its caller is **`kernel_bootstrap`, at `+0x188`** (`subr_log.c`'s
    # `oslog_init` is called from `kernel_bootstrap`'s straight line, `bl 8002ccec <oslog_init>` at
    # `0x8000dbc8`). The next instructions there are `kernel_debug_string_early` (real - it calls only
    # `strlen` and `strncpy`), then `telemetry_init`, which is a **stub** at `0x80098a04`
    # (`bl 80098a04 <telemetry_init>` at `0x8000dbd8`). So: **`stub_hit=telemetry_init`,
    # `xnu_entry_stub_caller=0x8000dbdc`** - the stub hands `entry_stub_hit` its own `lr`, so
    # `caller - 4` = `0x8000dbd8` = **`kernel_bootstrap+0x198`**: the first stop past the firehose, and
    # the first inside `kernel_bootstrap`'s own body since 253's `cs_init`.
    LIBKERN_OS_LOG_OBJ=${STAGE90_ENTRY_LIBKERN_OS_LOG_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/libkern_os_log.o}
    FIREHOSE_CONFIG_OBJ=${STAGE90_ENTRY_FIREHOSE_CONFIG_OBJ:-$REPO_ROOT/out/xnu_firehose_obj/firehose_kernel_config.o}
    # 259: the stop 258 predicted, linked. `telemetry_init` is `osfmk/kern/telemetry.c:120`, the
    # object is `out/xnu_kernel_obj/osfmk_kern_telemetry.o` (5482 bytes of text, 28 of data, 744 of
    # bss, 30 definitions, 57 references), and it is in the manifest at `out/xnu_arm_manifest.txt:595`
    # - so this is 258's method again, one object.
    #
    # Measured before the run, because the object is larger than the last few: 40 of its 57 references
    # are already satisfied, and the **17 new ones** are `get_task_dispatchqueue_serialno_offset`,
    # `host_get_special_port`, `kperf_ucallstack_sample`, `proc_did_throttle`,
    # `proc_get_darwinbgstate`, `proc_get_effective_task_policy`, `proc_pid`, `proc_uniqueid`,
    # `proc_was_throttled`, `stack_snapshot_from_kernel`, `task_did_exec`, `task_grab_latency_qos`,
    # `telemetry_notification` and the four `vm_shared_region_*` - almost all BSD-layer, and none of
    # them on this call's path (below). Four of its definitions exist here as stubs today
    # (`telemetry_init`, `telemetry_task_ctl`, `bootprofile_init`, `bootprofile_wake_from_sleep`) and
    # linking the object replaces them.
    #
    # **The prediction.** `telemetry_init` calls only `lck_grp_init`, `lck_mtx_init`,
    # `PE_parse_boot_argn`, `kmem_alloc`, `bzero` and the `kprintf` helper - **all six already real in
    # this image** - so it completes, and what it does on the way is a real 16384-byte
    # `kmem_alloc(kernel_map, ..., VM_KERN_MEMORY_DIAG)` (the boot arg is absent, so the size is
    # `TELEMETRY_DEFAULT_BUFFER_SIZE`, 16 KB: the second real kernel allocation this frontier has
    # made, after 255's 73728-byte guarded one). It returns to `kernel_bootstrap+0x19c`, and the
    # straight line from there is `PE_i_can_has_debugger` (real), `PE_parse_boot_argn` (real, both
    # taken only if the debugger is present), `kernel_debug_string_early` (real) and then
    # `0x8000dc24: bl console_init` - a stub. So:
    # **`stub_hit=console_init`, `xnu_entry_stub_caller=0x8000dc28`** (`caller - 4` = `0x8000dc24` =
    # `kernel_bootstrap+0x1e4`). Device: **`stub_hit=console_init`,
    # `xnu_entry_stub_caller=0x8000dc28`** = `kernel_bootstrap+0x1e8` - the prediction, again address
    # for address. So `telemetry_init` completed, with its 16 KB `kmem_alloc` inside it.
    #
    # Measured: resolved 4 (`telemetry_init`, `telemetry_task_ctl`, `bootprofile_init`,
    # `bootprofile_wake_from_sleep`), added 17; 592 -> 605 undefined, 514 -> 527 function stubs,
    # storage stubs unchanged at 78; text 702564 -> 708644; image 802248 -> **818656** (+16408, a
    # 16 KB alignment block: the image moves again for the first time since 255); bss end 0x800f6b88;
    # headroom 1086584; payload text 1294466 -> 1310874. `kv_written == kv_in_dram == 0x39`, two
    # bytes shorter than 258's 0x3b because `telemetry_init` is two characters shorter than
    # `console_init` and the stub name is recorded verbatim.
    OSFMK_KERN_TELEMETRY_OBJ=${STAGE90_ENTRY_OSFMK_KERN_TELEMETRY_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_telemetry.o}
    # 260: `console_init`, the first target in `kernel_bootstrap`'s line that is not an allocator or
    # a lock group. It is `osfmk/console/serial_console.c:166` - note the directory: this is
    # `osfmk/console/`, which the manifest has listed all along
    # (`out/xnu_arm_manifest.txt:477-480`: `serial_console.c`, `serial_general.c`, `video_console.c`,
    # `video_scroll.c`) - and the object is `out/xnu_kernel_obj/osfmk_console_serial_console.o`.
    #
    # **The cheapest step in a long time, and the measurement that makes it so: 2646 bytes of text,
    # 36 of data, 52 of bss, 27 references - and *every one* of the 27 is already satisfied by this
    # image, so the link adds nothing at all.** Nine of the object's definitions exist here as stubs
    # (`console_init`, `console_write`, `console_cpu_alloc`, `cngetc`, `cnputc`, `cnputc_unbuffered`,
    # `_serial_getc`, `cons_ops_index`, `nconsops`) and become real. Added: zero.
    #
    # **The prediction.** `console_init` calls only `OSCompareAndSwap` (real - `ldrex` at
    # `0x80057ec4`), `kmem_alloc` (real), `panic` (real) and `arm_usimple_lock_init` (real, the ARM
    # layer), plus `console_ring_lock_init`/`hw_lock_init` which are inline; so it completes, and what
    # it does is the third real kernel allocation of this frontier -
    # `kmem_alloc(kernel_map, &console_ring.buffer, KERN_CONSOLE_BUF_SIZE, VM_KERN_MEMORY_OSFMK)`,
    # guarded by `OSCompareAndSwap(0, KERN_CONSOLE_RING_SIZE, &console_ring.len)` so that the first
    # caller wins and later ones return early. It returns to `kernel_bootstrap+0x1e8`, where the
    # straight line is `kernel_debug_string_early` (real) and then
    # `0x8000dc34: bl stackshot_init` - a stub. So:
    # **`stub_hit=stackshot_init`, `xnu_entry_stub_caller=0x8000dc38`** (`caller - 4` = `0x8000dc34` =
    # `kernel_bootstrap+0x1f4`). Device: **`stub_hit=stackshot_init`,
    # `xnu_entry_stub_caller=0x8000dc38`** = `kernel_bootstrap+0x1f8` - the prediction, a third time.
    # `console_init` completed, and the 16 KB console ring is real memory.
    #
    # Measured: resolved **9**, added **0** - the first step in a long time with no new boundaries at
    # all. 605 -> 596 undefined, 527 -> 520 function stubs, 78 -> 76 storage; text 708644 -> 711044
    # (+2400), image 818656 -> 818696 (+40), and `.bss` end, the derived `args` offset (+1015808),
    # `topOfKernelData` and the headroom are all **unchanged** for the first time since 257.
    # `kv_written == kv_in_dram == 0x3b`, two bytes above 259's 0x39 - `stackshot_init` is two
    # characters longer than `console_init`, recorded verbatim as always.
    OSFMK_CONSOLE_SERIAL_CONSOLE_OBJ=${STAGE90_ENTRY_OSFMK_CONSOLE_SERIAL_CONSOLE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_console_serial_console.o}
    # 261: `stackshot_init`, and the first step in a while that is not cheap. The symbol is
    # `osfmk/kern/kern_stackshot.c` - **`kern_stackshot.o`, not `stackshot.o`**: `bsd/kern/stackshot.c`
    # (manifest line 79) is the syscall half, and the initialiser lives in the osfmk half (manifest
    # line 559). Note that the spelling matters here in the way `mi4-one-value-two-definitions`
    # warns about: both files are in the manifest, both compile, and only one defines `stackshot_init`.
    #
    # The object: 17737 bytes of text, 136 of bss, **116 references** - 76 already satisfied, and
    # **40 new**, the largest block this frontier has added since 254. They are the stackshot
    # machinery's own dependencies and they cluster: the `kcdata_*` writers
    # (`kcdata_memory_alloc_init`, `kcdata_add_*_with_description`, `kcdata_write_buffer_end`, ...),
    # the coalition surface (`coalition_id`, `coalition_type`, `coalition_iterate_stackshot`, ...),
    # the kdp debugger readers (`kdp_vtophys`, `kdp_pthread_find_owner`, ...), the machine-trace pair
    # (`machine_trace_thread`, `machine_trace_thread64`), `mt_stackshot_thread`/`_task`,
    # `gLoadedKextSummaries`, and a handful of `*_kdp` name helpers. Three of its definitions exist
    # here as stubs and become real: `stackshot_init`, `do_stackshot` and
    # `stack_snapshot_from_kernel` - the last of which **259 added**, so this is the frontier closing
    # on a name it introduced two steps ago.
    #
    # **The prediction.** `stackshot_init` is 120 bytes and calls six things - `lck_grp_attr_alloc_init`
    # (`0x80010e44`), `lck_grp_alloc_init` (`0x80010ec8`), `lck_attr_alloc_init` (`0x80011274`),
    # `lck_mtx_init` (`0x80013604`), `clock_timebase_info` (`0x8000d44c`, real: five instructions that
    # `ldrd` the timebase out of `0x800b0a30` and `bx lr`) and `__aeabi_uldivmod` (the EABI runtime) -
    # and **all six are real**. So it completes: it allocates a lock group and its attribute, a mutex,
    # reads the timebase and computes `sfs_system_max_fault_time`. It returns to
    # `kernel_bootstrap+0x1fc`, where the straight line is `kernel_debug_string_early` (real) and then
    # `0x8000dc44: bl sched_init` - a stub. So: **`stub_hit=sched_init`,
    # `xnu_entry_stub_caller=0x8000dc48`** (`caller - 4` = `0x8000dc44` = `kernel_bootstrap+0x204`).
    # Device: **`stub_hit=sched_init`, `xnu_entry_stub_caller=0x8000dc48`** = `kernel_bootstrap+0x208`
    # - the prediction, a fourth time. `stackshot_init` completed, lock group and all.
    #
    # Measured: resolved 3, added 40; 596 -> 633 undefined, 520 -> 554 function stubs, 78 -> 79
    # storage; text 711044 -> 730436 (+19392), image 818696 -> **835080** (another 16 KB block),
    # bss end 0x800facc8, args +1032192, headroom 1069880; payload text 1327298 (+16384).
    # `kv_written == kv_in_dram == 0x37`, four below 260's 0x3b - `sched_init` is four characters
    # shorter than `stackshot_init`, recorded verbatim as always.
    OSFMK_KERN_KERN_STACKSHOT_OBJ=${STAGE90_ENTRY_OSFMK_KERN_KERN_STACKSHOT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_kern_stackshot.o}
    # 262: the scheduler, and **two objects rather than one** - the first time this frontier has linked
    # more than one, and the reason is a real one rather than convenience.
    #
    # `sched_init` is `osfmk/kern/sched_prim.c:357` (`out/xnu_arm_kernel_obj/osfmk_kern_sched_prim.o`,
    # 31296 bytes of text, 107 references, 83 satisfied, **24 new**). But `sched_init`'s calls into the
    # scheduler are **indirect**: it loads the address of the `sched_multiq_dispatch` table and reads
    # its slots as function pointers -
    #
    #     78: movw r6, #:lower16:sched_multiq_dispatch      ; a *data* symbol
    #     84: ldr  r4, [r6]                                 ; slot 0  = the name string
    #    12c: ldr  r0, [r6, #4]                             ; slot 1  = SCHED(init)
    #    130: ldr  r7, [r6, #12]                            ; slot 3  = SCHED(processor_init)
    #    134: ldr  r5, [r6, #16]                            ; slot 4  = SCHED(pset_init)
    #    138: ldr  r6, [r6, #140]                           ; slot 35 = SCHED(rt_init)
    #    13c: blx  r0 ... 14c: blx r6 ... 184: blx r5 ... 19c: bx r1
    #
    # - so `sched_prim.o` alone is not a measurable step: `sched_multiq_dispatch` is currently a
    # **storage stand-in** in this image (zeros at `0x800f9d40`), which would make slot 0 a NULL string,
    # `strlcpy(sched_string, NULL, 48)` a NULL dereference, and `blx r0` a jump to 0. That stops the run
    # and measures *this step's omission*, not the frontier. The table is therefore linked with it:
    # `osfmk_kern_sched_multiq.o` (`osfmk/kern/sched_multiq.c`, manifest:580), 7418 bytes of text,
    # 52 references, 25 satisfied and 27 new (`run_queue_*`, `sched_timeshare_*`,
    # `sched_compute_timeshare_priority`, `update_priority`, `choose_processor`, ...), and its
    # `sched_multiq_dispatch` is **statically initialised in `.rodata`** (172 bytes of slots), so the
    # indirect targets become real addresses - and where a slot names a symbol this image does not have,
    # the `blx` lands on that symbol's *stub* and stops the run cleanly.
    #
    # `SCHED()` is the multiq scheduler here, which is what `SCHED(sched_name)` in the dispatch table's
    # first slot decides, and it is the reason `sched_multiq.o` is the second object and not
    # `sched_traditional.o` or `sched_dualq.o` - all three are in the manifest
    # (`out/xnu_arm_manifest.txt:579-582`) and exactly one is the one this build's config selects.
    #
    # **The prediction, and it is a chain rather than a symbol.** Resolving the table in the linked
    # image (`sched_multiq_dispatch` -> `0x800b82e8`, and its slots read out of the image) says which
    # functions `sched_init` will call, and each of them is followed one level:
    #
    #   slot 1  (0x04) = `sched_multiq_init`      - real; its calls are PE_parse_boot_argn, the printf
    #                                               helper, zinit, zone_change, lck_* - all real - and
    #                                               it **tail-calls `sched_timeshare_init`**
    #   slot 35 (0x8c) = `sched_rtglobal_init`    - real; calls `sched_rtglobal_runq` through the table
    #                                               and `arm_usimple_lock_init`, both real, and tails
    #                                               into `memset`
    #   slot 4  (0x10) = `sched_multiq_pset_init` - real, two instructions, a tail call to
    #                                               `run_queue_init` (real)
    #   slot 3  (0x0c) = `sched_multiq_processor_init` - likewise, tail into `run_queue_init`
    #
    # and `sched_timeshare_init`, `sched_rtglobal_init`, `sched_rtglobal_runq` and `run_queue_init`
    # were each **checked for the stub body** (a `movw r0, #<name>` followed by `push {lr}`) and none
    # of them has it. Note that `sched_timeshare_init` and `sched_rtglobal_init` appear in **neither**
    # the resolved nor the added list, and that is not an error: nothing in the image referenced either
    # name before this step, so neither was in the previous `undef` file to be subtracted. A name that
    # was absent from the frontier entirely is invisible to a delta of two undefined sets.
    #
    # So `sched_init` completes, and the stop should be **past it**, at the next stub in
    # `kernel_bootstrap`'s line: `0x8000dc54: bl 800a71a0 <ltable_bootstrap>`, still a stub. So:
    # **`stub_hit=ltable_bootstrap`, `xnu_entry_stub_caller=0x8000dc58`** (`caller - 4` =
    # `0x8000dc54` = `kernel_bootstrap+0x214`). Device: **`stub_hit=ltable_bootstrap`,
    # `xnu_entry_stub_caller=0x8000dc58`** = `kernel_bootstrap+0x218` - the prediction, a fifth time,
    # and this one was a chain of four functions reached indirectly through a data table. `sched_init`
    # completed: the multiq scheduler is initialised.
    #
    # Measured: resolved 30, added 28; 633 -> 631 undefined, 554 -> 555 function stubs, 79 -> 76
    # storage; text 730436 -> 768836 (+38400), image 835080 -> 867888 (+32808), bss end 0x80103108,
    # args +1069056, topOfKernelData +3145728 (it is derived at megabyte granularity), headroom
    # 2084600; payload text 1360106 (+32808). The 30 resolved are the thread and scheduling API -
    # `thread_block`, `thread_setrun`, `sched_tick`, `assert_wait`, `idle_thread`, `sched_startup` and
    # the rest - which had been stand-ins since this image first referenced them.
    # `kv_written == kv_in_dram == 0x3d`, six above 261's 0x37.
    OSFMK_KERN_SCHED_PRIM_OBJ=${STAGE90_ENTRY_OSFMK_KERN_SCHED_PRIM_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_sched_prim.o}
    OSFMK_KERN_SCHED_MULTIQ_OBJ=${STAGE90_ENTRY_OSFMK_KERN_SCHED_MULTIQ_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_sched_multiq.o}
    # 263: `ltable_bootstrap`, and the cheap shape again - 262's `osfmk_kern_ltable.o` measure:
    # `osfmk/kern/ltable.c` (manifest:567), 5175 bytes of text, 272 of bss, **19 references and all 19
    # already satisfied by this image**, so the link resolves `ltable_bootstrap` itself and **adds
    # nothing**. `ltable_bootstrap` calls only `lck_grp_init` and `PE_parse_boot_argn`, both real, so it
    # completes. **Prediction: `stub_hit=waitq_bootstrap`, `xnu_entry_stub_caller=0x8000dc68`**
    # (`caller - 4` = `0x8000dc64` = `kernel_bootstrap+0x224`). Device: **`stub_hit=waitq_bootstrap`,
    # `xnu_entry_stub_caller=0x8000dc68`** = `kernel_bootstrap+0x228` - the prediction, a sixth time.
    #
    # Measured: resolved **1** (`ltable_bootstrap` itself), added **0**; 631 -> 630 undefined,
    # 555 -> 554 function stubs, storage unchanged at 76; text 768836 -> 773988 (+5152), image
    # 867888 -> 884272 (another 16 KB block), bss end 0x80107208, args +1085440, headroom 2067960,
    # payload text 1376490. `kv_written == kv_in_dram == 0x3c`, one below 262's 0x3d because
    # `waitq_bootstrap` is one character shorter than `ltable_bootstrap` - both are 9 characters of
    # `_bootstrap`, so the difference is `ltable` (6) against `waitq` (5).
    # 268: `ipc_table_init`, and the first object since 260 whose every reference is already paid
    # for. 267's stop was `ipc_table_init`; the object that defines it is `osfmk/ipc/ipc_table.c`
    # (manifest:521), `osfmk_ipc_ipc_table.o` - **544 bytes of text, 80 of data, 8 of bss, ten
    # definitions and exactly two references**, `kalloc_canblock` and `kfree`, both made real by 250
    # (`osfmk_kern_kalloc.o`). So the link resolves and adds **nothing**, the 258/260/263/266 shape:
    # an object the manifest has been building all along, waiting for the image to link it.
    #
    # Two of the ten definitions were already in hand from 266 - `ipc_table_alloc` (0x28 bytes) and
    # `ipc_table_free` (0x10), both thin wrappers over `kalloc`/`kfree` - and they are what 266's link
    # added; this step is the third caller of the file. The other seven are `ipc_table_init` itself
    # (0x1e8 = 488 bytes, the whole `.text` of the function), the four table globals
    # (`ipc_table_entries`/`ipc_table_requests` are `B`, zeroed; `ipc_table_entries_size` =
    # `ipc_table_requests_size` = 0x40 = 64, read straight out of `.data`), and three `*.site` records
    # - gcc's caller-site data, which is the third argument `kalloc_canblock(size, canblock, site)`
    # takes, loaded with `movw`/`movt` before each call rather than a symbol this step must satisfy.
    #
    # `ipc_table_init` is **two `kalloc_canblock` calls and two `ipc_table_fill` loops**, with no
    # `panic` call anywhere in it: this build has `MACH_ASSERT` off, so the two
    # `assert(... != ITS_NULL)` lines are compiled out and the disassembly shows only the two `bl`s.
    # Each loop is 64 iterations of a doubling-fill (`16` elems of `struct ipc_entry` for the entries
    # table, `2` of `struct ipc_port_request` for the requests table), **256 bytes allocated per
    # call** - `sizeof(struct ipc_table_size) * 64` = 4 * 64, for *both* tables, because
    # `ipc_table.c:138` multiplies `ipc_table_requests_size` by `sizeof(struct ipc_table_size)` and
    # not by the element it fills with. (268's first write-up of this comment said 512. The
    # measurement corrected it: see the traced run below. This is the kalloc path 250 made real, over
    # the kalloc map 250 created.) **Prediction: `stub_hit=ipc_voucher_init`,
    # `xnu_entry_stub_caller=0x800abd88`** (`caller - 4` = `0x800abd84` = `bl <ipc_voucher_init>`).
    # Device: **`stub_hit=ipc_voucher_init`, `xnu_entry_stub_caller=0x800abe28`** (`caller - 4` =
    # `0x800abe24` = `ipc_bootstrap+0x180` = `bl 800c3974 <ipc_voucher_init>`, the call right after
    # `bl ipc_table_init` at `ipc_bootstrap+0x17c`) - the prediction, an eleventh time. The address is
    # 0xa0 above the prediction's and both are right: `ipc_bootstrap` was at `0x800abc04` in 267's
    # image and is at `0x800abca4` in 268's, so the prediction was the right *offset* - `+0x184`, the
    # next `bl` after the one that had just returned - written in the previous step's coordinate
    # system. An absolute caller address does not carry across a step that grows the image; resolve
    # against the ELF that made the run, which is what `tools/host_resolve_entry_addr.sh --elf` is
    # for. 267's `+0x17c` for `bl ipc_table_init` is the same offset 268 measures.
    #
    # Measured: resolved **3** (`ipc_table_init`, `ipc_table_alloc`, `ipc_table_free`), added **0**;
    # 863 -> 860 undefined, 787 -> 784 function stubs, storage unchanged at 76. The trace below is
    # what turned "the ipc tables are built" from an inference into a reading: both calls succeed,
    # `t268_kalloc_size` = `t268_kalloc_actual` = 0x100 for each, callers `ipc_table_init+0x34` and
    # `ipc_table_init+0x120`, returning 256 bytes apart.
    # 269: `ipc_voucher_init`, and the first step whose *value* is wrong in a way the run cannot see.
    # 268's stop was `ipc_voucher_init`; the object that defines it is `osfmk/ipc/ipc_voucher.c`
    # (manifest:522), `osfmk_ipc_ipc_voucher.o` - **12335 bytes of text, 120 of data, 2200 of bss,
    # 73 definitions and 35 references**, the largest single step since 257. A voucher is a
    # first-class Mach object: its own hash table (`ivht_bucket`, `iv_global_table`), two zones, an
    # attribute-manager registry, and the whole `mach_voucher_*` / `host_*_mach_voucher_*` MIG
    # surface - 22 of the 73 definitions are names the image has been carrying as stubs since the
    # IPC init surface was added in 265.
    #
    # **26 of the 35 references are already real**, which is why this step is a link and not a
    # cascade - and the other nine are stubs, which is where the useful measurement is. Reading the
    # object's own relocations says *where* each is referenced, and not one of them is reached from
    # `ipc_voucher_init`:
    #
    #     task_max                <- ipc_voucher_init                  (was already a stub)
    #     ipc_port_alloc_special  <- convert_voucher_attr_control_to_port, convert_voucher_to_port
    #     ipc_port_nsrequest      <- convert_voucher_attr_control_to_port, convert_voucher_to_port
    #     ipc_port_release_send   <- ipc_voucher_receive_postprocessing, ipc_voucher_send_preprocessing
    #     kernel_task             <- ipc_voucher_receive_postprocessing, ipc_voucher_send_preprocessing
    #     ipc_port_make_send_locked    <- convert_voucher_attr_control_to_port, convert_voucher_to_port
    #     ipc_port_make_sonce_locked   <- convert_voucher_attr_control_to_port, convert_voucher_to_port
    #     ipc_port_dealloc_special     <- convert_voucher_attr_control_to_port, convert_voucher_to_port,
    #                                     iv_dealloc, ivac_dealloc
    #     ipc_object_translate         <- convert_port_name_to_voucher, host_create_mach_voucher,
    #                                     mach_voucher_attr_control_create_mach_voucher
    #
    # The first five were *already* stubs before this step - other linked objects reference them -
    # and the last four are new, because a stub only exists for a name something in the link actually
    # references and nothing did until now. (The check that says "already real" has to be against the
    # image's *real* definitions, not against absence from the stub list: absent there means "not
    # needed", which is a different claim, and that is how a first pass at this table read the four
    # new names as already satisfied.) So the step is 22 resolved and 4 added.
    #
    # `ipc_voucher_init`'s own body, disassembled from the object:
    #
    #     movw/movt task_max ; ldr    ; movw/movt thread_max ; ldr ; add r0, r1, r0 ; lsl r1, r0, #7
    #     mov r0, #0x40 ; mov r2, #0x40 ; bl zinit        ; str -> ipc_voucher_zone
    #     bl zone_change
    #     mov r0, #0x40 ; ... ; bl zinit                  ; str -> ipc_voucher_attr_control_zone
    #     bl zone_change
    #     bl lck_spin_init(&ivht_lock_data, &ipc_lck_grp, &ipc_lck_attr)   ; 128 queue_init stores
    #     bl lck_spin_init(&ivgt_lock_data, &ipc_lck_grp, &ipc_lck_attr)
    #     pop {fp, lr} ; b user_data_attr_manager_init      (tail call, same object)
    #
    # `zinit`, `zone_change`, `lck_spin_init` and `queue_init` are all real, and
    # `user_data_attr_manager_init` -> `ipc_register_well_known_mach_voucher_attr_manager` ->
    # `ivac_alloc` stays inside this object. **Prediction: `stub_hit=ipc_importance_init`**,
    # `xnu_entry_stub_caller=0x800abe2c` - the call after `bl ipc_voucher_init` in `ipc_bootstrap`,
    # that is `ipc_bootstrap+0x188`, the return address of the `bl ipc_importance_init` at `+0x184`.
    # Stated as an offset *and* an address because 268 learned that `ipc_bootstrap` can move when the
    # link grows (0x800abc04 -> 0x800abca4 across 267 -> 268): here it did not, `ipc_voucher.o` lands
    # after `ipc_table.o` in the same region, so both forms are checkable against the built ELF.
    #
    # The thing this step cannot measure, and the reason its comment is longer than its change:
    # `task_max` is a **storage stand-in of size 4 whose value is 0**, so
    # `ipc_voucher_max = (task_max + thread_max) * 2` computes `(0 + 1536) * 2 = 3072` where the real
    # kernel computes `(512 + 1536) * 2 = 4096` - `CONFIG_TASK_MAX=512` is Apple's
    # `config/MASTER.arm`, `thread_max` is real because `thread.o` has been linked since the
    # scheduler. The consequence is a *silently smaller zone*: `zinit` round_pages `max` and clamps
    # it up to the allocation size (`if (max && (max < alloc)) max = alloc;`, `zalloc.c:2168`), so
    # the zone is created, `ipc_voucher_init` returns, and nothing anywhere reports that its ceiling
    # is 3072 * 64 = 192 KB where the real kernel's is 4096 * 64 = 256 KB. **This is the
    # [[mi4-stand-in-size-is-not-value]] class reaching a value that is used in arithmetic rather than
    # as a pointer**, and no `--wrap` can see it: the read is an `ldr` from an address the stub
    # generator allocated, not a call. The repair is `osfmk/kern/task.o`, which defines
    # `task_max = CONFIG_TASK_MAX` in `.data` - a much larger object, and its own step.
    #
    # Measured: resolved **22** (`ipc_voucher_init` plus the 21 other names the IPC surface has been
    # stubbing since 265 - the `convert_*_to_voucher` family, `host_*_mach_voucher_*`, the five
    # `mach_voucher_*` entry points and `mach_init_activity_id`), added **4** (all four
    # `ipc_port_make_*`/`ipc_object_translate`/`ipc_port_dealloc_special`, all off the path); 860 ->
    # 842 undefined, 784 -> 766 function stubs, storage unchanged at 76 (the two storage names this
    # object needs - `kernel_task`, `task_max` - were already stubbed); text 904484 -> 915940, image
    # 1015520 -> 1015640, bss end 0x8012a508 -> 0x8012c648, args +1232896 -> +1236992, headroom
    # 1917688 -> 1915320.
    #
    # Device: **`stub_hit=ipc_importance_init`** - the prediction, a twelfth time - and the caller is
    # the return address of the `bl ipc_importance_init` in `ipc_bootstrap`, which the final image
    # puts at `+0x188`, i.e. the return address of the `bl` at `+0x184`. The whole run of calls, by
    # their *return* addresses, is `ipc_table_init` +0x180, `ipc_voucher_init` +0x184,
    # `ipc_importance_init` +0x188, `semaphore_init` +0x18c, and the final image has them as
    # `800abfc0 bl ipc_table_init / 800abfc4 bl ipc_voucher_init / 800abfc8 bl ipc_importance_init /
    # 800abfcc bl semaphore_init`, then `pop {r4, r5, fp, lr}` and a tail `b host_notify_init`.
    # 268's lesson repeated exactly: the *offset* is the durable form
    # and the absolute address is not, because the link below grew again here - the diagnostics this
    # step ended up adding moved `ipc_bootstrap` from 0x800abe28 to 0x800abfcc.
    #
    # That run also found two defects, both in this image rather than in XNU, and both fixed here:
    #
    #   - **The data-abort handler's own report path could fault.** The handler used to make 17
    #     `entry_kv` calls after recording its facts. In this step's first run one of those records
    #     faulted, which re-entered the handler, which wrote another record, which faulted - 0x139
    #     entries, a full 8192-byte results buffer of 313 bare `xnu_entry_data_abort_dfar` keys with
    #     no value after any of them, and a report whose `why` line was three characters long. The
    #     handler now records into `.bss` and goes straight to the epilogue, which reports all of it
    #     through `entry_write_kv` after the mmu is off - the one path here with a record of working.
    #     A second entry can no longer happen, and if it does it says so and leaves.
    #   - **`entry_epilogue`'s `why` line printed garbage.** It read `"47"`, then nothing at all,
    #     where the string is `a symbol this image does not provide was called`. The parameter is
    #     passed correctly and stored to `[sp, #4]` on entry, so what the run says is that the *slot*
    #     did not survive to the report. The string is now copied to `.bss` at entry
    #     (`g_why`) and the pointer is printed beside it; the line reads correctly, and the pointer
    #     it reports - 0x800ca2b8, first byte 'a' - says the string was always fine.
    #
    # And one open question, recorded rather than guessed: the first run's fault decodes to a read at
    # `low16(&g_hex) + index` - the digit table's own low half plus a nibble - with a *section
    # translation* fault stored in DFSR, on a read. `dfar` is a store's address in the canon run and
    # a load's in the instrumented one, and the code between the table's address being formed and
    # the read has no branch and no call in it. The digits are computed arithmetically now, which
    # removes the read entirely and takes the run to **zero aborts**, so the step is not blocked on
    # it - but "the page is not mapped" and "the address lost its high half" cannot both be true, and
    # the run that separates them is the next thing this thread needs.
    # 270: `ipc_importance_init`, and the step that is *inside* a conditional.
    # 269's stop was `ipc_importance_init`, and its object is `osfmk/ipc/ipc_importance.c`
    # (manifest:511), `osfmk_ipc_ipc_importance.o` - **13312 bytes of text, 24 of data, 48 of bss,
    # 72 definitions and 51 references**. It is large but shallow: 72 definitions, of which only
    # **3** are names the image already carries (`ipc_importance_init`,
    # `ipc_importance_thread_call_init`, `task_importance_list_pids` - all three current stubs), so
    # this is a 3-resolved step rather than a 265- or 269-shaped one. That is the 250 shape: most of
    # an object's definitions are names nothing in the image references, and what an object *defines*
    # is not what the link *needs*.
    #
    # The seven names it adds are all off the path, and they are the QoS/boost surface the
    # importance machinery reaches for at runtime rather than at init:
    #
    #     ipc_port_impcount_delta                      <- ipc_importance_send, _task_reference
    #     ipc_port_importance_delta                    <- ipc_importance_hold/drop paths
    #     ipc_port_importance_delta_internal           <- the *_internal_assertion paths
    #     ipc_port_sync_qos_delta                      <- ipc_importance_send
    #     task_importance_reset                        <- ipc_importance_disconnect_task
    #     task_policy_update_complete_unlocked         <- ipc_importance_task_*_assertion
    #     task_update_boost_locked                     <- ipc_importance_task_*_assertion
    #
    # `ipc_importance_init`'s body, disassembled from the object - and the reason this comment is
    # short where 269's was long is that the function is *structurally* the same as
    # `ipc_voucher_init`, which 269 already proved runs:
    #
    #     ldr thread_max ; ldr task_max ; add r1, r4, r5 ; add r4, r1, r1, lsl #1   ; (t + th) * 3
    #     add r1, sp, #14 ; mov r2, #26 ; bl PE_parse_boot_argn
    #     lsl r1, r4, #6  ; mov r0, #96 ; mov r2, #96 ; bl zinit   ; str -> ipc_importance_task_zone
    #     bl zone_change(Z_NOENCRYPT)
    #     lsl r1, r4, #5  ; mov r0, #48 ; mov r2, #48 ; bl zinit   ; str -> ipc_importance_inherit_zone
    #     bl zone_change(Z_NOENCRYPT)
    #     bl lck_spin_init(&ipc_importance_lock_data, &ipc_lck_grp, &ipc_lck_attr)   ; inlined lock init
    #     bl ipc_register_well_known_mach_voucher_attr_manager(&ipc_importance_manager, 0, 2,
    #                                                          &ipc_importance_control)
    #     cmp r0, #0 ; beq done ; bl _consume_printf_args ; done: pop {r4, r5, fp, pc}
    #
    # Every one of those six calls is real in this image: `PE_parse_boot_argn`, `zinit` and
    # `zone_change` have been real since 250, `lck_spin_init` since 262, and
    # `ipc_register_well_known_mach_voucher_attr_manager` **since 269** - it is defined by
    # `ipc_voucher.c`, and 269's run reached `ipc_importance_init`, which means `ipc_voucher_init`
    # returned, which means that function and its `user_data_attr_manager_init` caller already ran
    # on the device once. `_consume_printf_args` is real too (retired by 199), so even the error
    # branch - `kr != KERN_SUCCESS` - is not a stub: it would print "Voucher importance manager
    # register returned" and continue, which is a different outcome from this one and would be
    # visible in the log as a kprintf rather than as a `stub_hit`.
    #
    # **Prediction: `stub_hit=semaphore_init`, `xnu_entry_stub_caller = ipc_bootstrap+0x18c`** - the
    # return address of `bl semaphore_init`, the next call after `bl ipc_importance_init` in
    # `ipc_bootstrap` (`ipc.c:206` -> `:211`, `ipc_init.c`'s source order matching the linked
    # image's for once: `mig_init`, `ipc_table_init`, `ipc_voucher_init`, `ipc_importance_init`,
    # then `semaphore_init`, `mk_timer_init`, and a tail `b host_notify_init`). Stated as an offset
    # first, because 267/268/269 each moved the absolute address and none moved the offset.
    #
    # The one thing this step *cannot* see is the same thing 269 could not: `ipc_importance_max =
    # (task_max + thread_max) * 2` reads `task_max`, which is still a storage stand-in whose value is
    # 0, so it computes `(0 + 1536) * 2 = 3072` where the real kernel computes
    # `(512 + 1536) * 2 = 4096`, and both `zinit` ceilings are a quarter smaller than they should be
    # (3072 * 96 = 288 KB where 4096 * 96 = 384 KB; 3072 * 48 = 144 KB where 4096 * 48 = 192 KB).
    # The image shows the arithmetic even though one input is invisible in it: `add r1, r4, r5` is
    # `thread_max + task_max`, `add r4, r1, r1, lsl #1` is that sum times three, and the two `lsl`
    # immediates are 6 and 5 - the compiler folded `* 2 * sizeof(struct ...)` into `* 3` then
    # `<< 6` (96 = 3 * 32) and `<< 5` (48 = 3 * 16). So the ×2 that is *not* in the instruction
    # stream is exactly the one whose other factor, `task_max`, is a zero stand-in, and this step
    # makes that stand-in the second place it silently shrinks a zone rather than the first. The
    # repair remains `osfmk/kern/task.o`, which is its own step.
    # 271: `semaphore_init`, and a zone whose ceiling is *live* arithmetic rather than a constant.
    # 270's stop was `semaphore_init`, and its object is `osfmk/kern/sync_sema.c` (manifest:587),
    # `osfmk_kern_sync_sema.o` - **4567 bytes of text, 0 of data, 12 of bss, 32 definitions and 36
    # references**. **5 resolved, 3 added.** The five are `semaphore_init`, `semaphore_create`,
    # `semaphore_destroy`, `kdp_sema_find_owner` and - the interesting one - **`semaphore_max`**,
    # which the image has been carrying as a *storage stand-in*. The three added
    # (`port_name_to_semaphore`, `port_name_to_thread`, `thread_syscall_return`) are the syscall
    # surface, none of them on the init path.
    #
    # `semaphore_init` is three instructions of substance and two calls, both real since 250:
    #
    #     ldr semaphore_max ; mov r0, #64 ; mov r2, #64 ; lsl r1, r0_max, #6 ; bl zinit
    #     str -> semaphore_zone ; bl zone_change(Z_NOENCRYPT, TRUE)
    #
    # So it completes, and the prediction is the next call in `ipc_bootstrap`.
    #
    # `semaphore_max` is worth the paragraph, because it is **not** a constant and it is **not**
    # zero at this point in the boot. `sync_sema.c:67` declares `unsigned int semaphore_max;` with no
    # initializer, so this object contributes a `.bss` variable holding zero - and linking it changes
    # nothing about the *value*, because the image's stand-in was also zero-initialized. What makes
    # the value non-zero is a **writer**: `scale_setup()` in `osfmk/kern/startup.c` (called by
    # `kernel_bootstrap` as its first substantive act, long before `ipc_bootstrap`) ends with
    #
    #     ipc_space_max = SPACE_MAX;  ipc_port_max = PORT_MAX;
    #     ipc_pset_max = SET_MAX;     semaphore_max = SEMAPHORE_MAX;
    #
    # and it is real code in this image - `scale_setup` is a function of its own at 0x8000e004, and
    # its last store is `str r0, [r1]` with `r1 = 0x8012f780`, which `nm` on the image says is
    # `semaphore_max`. So the read in `semaphore_init` sees `PORT_MAX >> 1`, computed a moment
    # earlier by that same function - **and `PORT_MAX` is `task_max * 3 + thread_max * 3 + 40000`,
    # which is `task_max` again.** With the stand-in at zero that is 44608 and `semaphore_max` is
    # 22304; with the real 512 it would be 46144 and 23072. Fourth consumer of the same stand-in,
    # after 269's voucher zone, 270's two importance zones, and the three `*_max` globals here.
    #
    # That also means the `zinit` in `semaphore_init` sizes a **1.4 MB zone** (22304 * 64), which is
    # the largest single `max_mem` any of these steps has asked for - a number worth having on the
    # record before the run, because if `zinit` were to fail it would fail loudly here.
    #
    # **Prediction: `stub_hit=mk_timer_init`, `xnu_entry_stub_caller = ipc_bootstrap+0x190`** - the
    # return address of `bl mk_timer_init`, the call after `bl semaphore_init` (`ipc_init.c:213`),
    # which in the 270 image is `800abfd0: bl semaphore_init` -> return +0x18c, then
    # `800abfd4: bl mk_timer_init` -> return +0x190. Offset first, absolute address second.
    #
    # Measured: **`stub_hit=mk_timer_init`**, `xnu_entry_stub_caller=0x800ac0b4` =
    # `ipc_bootstrap+0x190` - the fourteenth prediction in a row. Resolved 5, added 3; 846 -> 844
    # undefined, 770 -> 769 function stubs, 76 -> 75 storage; text 930244 -> 935012, image
    # 1032048 -> 1048432, bss end 0x801306c8 -> 0x801346c8, args +1253376 -> +1269760, headroom
    # 1898808 -> 1882424, payload 1526504 -> 1542888.
    #
    # And the run found the third reporter defect in three steps, this one **state-dependent**.
    # `entry_stub_hit`'s caller record came out ` xnu_entry_stub_caller=0x800:<0;4` where the
    # prediction is 0x800ac0b4. The wrong characters are not noise: `entry_kv` writes a non-decimal
    # digit as 0x57 + d and a decimal one as 0x30 + d, and 0x30 + 0xa = ':', 0x30 + 0xc = '<',
    # 0x30 + 0xb = ';' - so the string says the conditional add took the decimal branch for every
    # non-decimal nibble, and `:<;` decodes back to a, c, b, i.e. **the true value is recoverable
    # from the corruption**. The compiled code cannot do that: the whole image holds exactly one
    # `add rN, rN, #87` (so there is no second copy of the loop), and the ELF bytes at 0x8000211c
    # are `cmp r3,#9 / add r4,r3,#0x57 / addls r4,r3,#0x30`.
    #
    # So the step made the value travel three roads, and the second run read:
    #
    #     xnu_entry_stub_caller_v=0x800ac0b4    value, via entry_write_kv (a .rodata table read)
    #     xnu_entry_stub_caller_w0=0x3c3a3030   the bytes entry_kv stored: '0','0',':','<'
    #     xnu_entry_stub_caller_w1=0x0a343b30   ... '0',';','4','\n'
    #     xnu_entry_stub_caller  =0x800:<0;4    written by entry_kv during the run
    #     xnu_entry_stub_caller_a=0x800:<0;4    the same value, same call site, one call later
    #     xnu_entry_stub_caller_e=0x800ac0b4    the same value, written by entry_kv from the epilogue
    #
    # (1) the bias is in `g_kv_buf` itself, so neither the value nor the transfer is at fault;
    # (2) two calls in the same machine state give the same wrong answer, so it is not a
    # per-invocation hazard; (3) **the same instruction at the same address in the same image gives
    # the right answer from the epilogue**, where SCTLR.C and SCTLR.I are clear and the mmu is off.
    # The epilogue's copy is a real `bl entry_kv` (0x800024ac -> 0x80002024), not an inlined second
    # copy - which the `#87` count settles independently.
    #
    # The measurable statement is therefore about *fetch*, not about arithmetic: the entry window's
    # instructions are not executed as the memory says while the I-cache is on and XNU's page tables
    # are live, and they are executed as the memory says when both are off. On ARMv7-A instruction
    # fetches from Strongly-ordered or Device memory are unpredictable, and this configuration is
    # `SO_ONLY`. **The mechanism is not measured and is not claimed**: two candidates fit equally
    # (XNU's own descriptors for the window, which this image already has stubs queued to read; or
    # I-cache lines left over for those physical addresses by something else that executed there,
    # which an invalidate before the jump would settle). The step's own result does not depend on
    # the corrupt characters - the caller is confirmed by `+0x190`, by `_v`, by `_e`, and by the two
    # words read out of the buffer.
    OSFMK_KERN_SYNC_SEMA_OBJ=${STAGE90_ENTRY_OSFMK_KERN_SYNC_SEMA_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_sync_sema.o}
    OSFMK_IPC_IPC_IMPORTANCE_OBJ=${STAGE90_ENTRY_OSFMK_IPC_IPC_IMPORTANCE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_ipc_ipc_importance.o}
    OSFMK_IPC_IPC_VOUCHER_OBJ=${STAGE90_ENTRY_OSFMK_IPC_IPC_VOUCHER_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_ipc_ipc_voucher.o}
    OSFMK_IPC_IPC_TABLE_OBJ=${STAGE90_ENTRY_OSFMK_IPC_IPC_TABLE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_ipc_ipc_table.o}
    # 293: `bsd_kern.o`, a function that returns a constant, and the pad's first real test
    #
    # **The 292 run reported** `stub_hit=get_task_uniqueid` at `coalitions_adopt_task + 0x0dc`. The
    # object that defines it is `osfmk/kern/bsd_kern.c`, `osfmk_kern_bsd_kern.o` - and it also defines
    # `get_task_crash_label`, one of the three names 292 obliged, so this step is again worth two
    # names for the price of one.
    #
    # **The object, measured.** 4488 bytes of `.text` plus 16 of `.rodata.str1.1`, 61 definitions and
    # 34 references:
    #
    #     resolved  23   get_task_uniqueid, get_task_crash_label, get_bsdtask_info/set_bsdtask_info,
    #                    get_bsdthread_info, get_threadtask, get_task_map, get_task_page_table,
    #                    get_task_internal(_compressed), get_task_phys_footprint(+_recent_max),
    #                    get_task_purgeable_nonvolatile(_compressed), get_task_purgeable_size,
    #                    get_task_resident_max, get_task_iokit_mapped, get_task_alternate_accounting
    #                    (+_compressed), get_task_cpu_time, get_task_dispatchqueue_serialno_offset,
    #                    current_thread_aborted, task_act_iterate_wth_args
    #     added      5   bank_billed_balance_safe, bank_serviced_balance_safe, bsd_threadcdir,
    #                    get_dispatchqueue_serialno_offset_from_proc, proc_pidversion
    #     under      7   act_set_astbsd, bsd_getthreadname, mt_core_supported, mt_fixed_task_counts,
    #                    proc_uniqueid, psignal, thread_update_qos_cpu_time
    #     real      29
    #
    # Thirty-eight more definitions are referenced by nothing - `fill_task_rusage` and its six
    # siblings, `get_task_frozen`, `get_task_pmap`, `swap_task_map` and the rest of the BSD task
    # accessor surface. They arrive as dead code, as in 292, and none of the 38 collides with a
    # symbol the image already has.
    #
    # **The prediction, and its centre is a function whose body is four instructions.** `get_task_uniqueid`
    # is:
    #
    #     ldr  r0, [r0, #568]      ; task->bsd_info
    #     cmp  r0, #0
    #     beq  1f
    #     b    proc_uniqueid       ; tail call, only when bsd_info is set
    #   1: mvn  r0, #0
    #     mvn  r1, #0              ; UINT64_MAX
    #     bx   lr
    #
    # `task_create_internal` sets `new_task->bsd_info = NULL` (task.c:1045, and the image has
    # `str r5, [r4, #152]` among the other zero stores at 0x800bfe6c), and nothing in the boot path
    # so far has run any BSD code. So **the new body takes the `beq` and returns UINT64_MAX without
    # calling anything** - it does not reach `proc_uniqueid`, which is one of the seven `under` names
    # and is still a stub. Three call sites in `coalitions_adopt_task` (`+0x1b4`, `+0x220`, `+0x254`)
    # all take it, and all three are followed by `and r0, r0, r1 / cmn r0, #1 / beq`, which the
    # constant satisfies.
    #
    # So this step **overshoots the symbol it links**, as 290 did, and the second half of the
    # prediction is what makes it interesting: `get_task_uniqueid` was the *last* stub on the whole
    # path from `task_create_internal` back out through `task_init`. What remains is measured, not
    # assumed, and every guard was read:
    #
    #   * the rest of the coalition adopt, for both types. `COALITION_NUM_TYPES` is 2 (RESOURCE and
    #     JETSAM, both `has_default = 1`), so the loop runs twice; the second op is
    #     `i_coal_jetsam_adopt_task` (0x800bf060), and a depth-3 scan from it finds only hits behind
    #     `panic`.
    #   * the two `kernel_debug` calls in `coalitions_adopt_task` (+0x244, +0x278) - the `KDBG_RELEASE`
    #     of the coalescing path - are each behind `ldr r0, [0x80130f20] / mvn r1, #8 / tst r0, r1`,
    #     and 0x80130f20 is `kdebug_enable`, a **`.bss`** symbol this payload zeroes. They are not
    #     reached, so `kernel_debug -> kernel_debug_internal -> current_proc` is not either.
    #   * `coalition_remove_task_internal` (+0x204) is in the `kr != KERN_SUCCESS` cleanup arm only.
    #   * `task->coalition[RESOURCE] == COALITION_NULL` -> `panic` at +0x638 is not taken: the store
    #     `str r7, [r0, #936]` happens before `get_task_uniqueid`, which is exactly why the first
    #     stop was where it was.
    #   * `place_task_hold` at +0x724 is guarded by `kernel_task != TASK_NULL`, and `kernel_task` is
    #     still NULL - it is written from the out-parameter at +0x73c, at the very end.
    #   * `task_init`'s tail is `vm_map_deallocate` and a **tail call** to `lck_spin_init`, both real.
    #   * `kernel_bootstrap`'s `kernel_debug_string_early` (76 bytes) has no stub within three calls.
    #   * `thread_init`'s first nine calls are `zinit` x2, `zone_change` x4,
    #     `lck_grp_attr_setdefault`, `lck_grp_init`, `lck_attr_setdefault`. The only stub reachable
    #     from them is `zinit -> btlog_create` at `zinit + 0x950`, behind two `.bss` guards
    #     (`zone_btlog_enabled` and `z->z_btlog == NULL`) - and `task_init` itself already called
    #     `zinit` at `+0x90` in every run since 289, so that path is settled empirically as well as
    #     by reading.
    #
    # **Predicted stop:**
    #
    #     stub_hit=stack_init    xnu_entry_stub_caller=0x800092ac   (thread_init + 0xd8, r12 0x800091d4)
    #
    # `stack_init`, `thread_policy_init` and `machine_thread_init` are `thread_init`'s tenth, eleventh
    # and twelfth calls, at +0xd4, +0xd8 and +0xdc - three stubs in a row with nothing between them.
    #
    # **The build, and this is the step where the 16 KB boundary finally moves.** `.data` is
    # 16 KB-aligned and currently at 0x8010c000; the last thing before it, `__TEXT,initcode`, ends at
    # **0x8010babc**, leaving **1348 bytes**. The object brings 4504 bytes of `.text` and `.rodata`,
    # retires 23 stub names and obliges 5. At 292's measured ~62 bytes per retired name that is
    # `4504 - 23*62 + 5*62` = **+3388**, which is 2040 bytes more than the room available. So:
    #
    #                   predicted        measured
    #     undefined     800
    #     function      706
    #     storage        94
    #     .data         0x8010c000 -> **0x80110000** (16 KB further on)
    #     __bss_start   0x80123c08 -> **0x80127c08**
    #     bss end       0x8015a458 -> **0x8015e458** (`.bss` size unchanged: the storage set is)
    #     image         1198104  -> **1214488** (+16384, the `*fill*` the map file has always shown)
    #     text          1096376  -> ~1099764 (the soft number; the name cost is 52-65 bytes measured
    #                    across 290, 291 and 292, so the band is 1097100-1097400 - either way, past
    #                    the boundary)
    #
    # The falsifier is a `.data` still at 0x8010c000 with an image of 1198104, which would mean the
    # per-name cost is under 20 bytes and the 292 figure is not reusable.
    #
    # **The build, every count exact.**
    #
    #                   predicted        measured
    #     undefined     800              800
    #     function      706              706
    #     storage        94               94
    #     .data         0x80110000       0x80110000
    #     __bss_start   0x80127c08       0x80127c08
    #     bss end       0x8015e458       0x8015e458
    #     image         1214488          1214488
    #     text          ~1099764         1099960      (band 1097100-1097400 at 52-65 bytes a name;
    #                                                  the measured cost is **51.1**, `(4504-3584)/18`,
    #                                                  just under the band's floor)
    #
    # So the 16 KB boundary moved for the first time in four experiments, and the prediction that it
    # would was right where the step-292 arithmetic was not. `.data` is at 0x80110000, `__bss_start`
    # follows it, and the image gained exactly the 16384 bytes of `*fill*` the map file has shown at
    # that seam since the walk began. Headroom 1727400 -> 1711016.
    #
    # The pin tracked the move without being asked to: the writes are now 0x8015e448 and 0x8015e44c,
    # 0x4000 further on, `ResetHandlerData - ExceptionLowVectorsBase` = 0x15e444 - and the reserved
    # slot moved by the same 0x4000 (`__bss_start` 0x80123c08 -> 0x80127c08). That is the whole point
    # of the 291 fix, measured for the second time.
    #
    # **The run.**
    #
    #     stub_hit=stack_init     xnu_entry_stub_caller=0x800092ac
    #
    # `0x800092ac` is `thread_init + 0xd8`, the return address of the `bl` at 0x800092a8, with
    # `thread_init` at 0x800091d4. So `get_task_uniqueid`'s new body took the `beq` and returned
    # UINT64_MAX at all three of its call sites without reaching `proc_uniqueid`, the coalition loop
    # ran for both types, `task_create_internal` and `task_init` both returned, and
    # `kernel_bootstrap` reached `thread_init` - **the first XNU function in this walk that is about
    # creating a thread rather than filling in a structure.** The three stubs at +0xd4, +0xd8 and
    # +0xdc are now the frontier, and they are three in a row with nothing between them.
    #
    # Preflight clean (`loader_xnu_entry_stub_status=0x90000001`,
    # `high_va_data_verified=0x00000001`), log 301111 bytes, no `exception:` line.
    #
    # **Safety:** non-persistent `fastboot boot` only, nothing flashed,
    # `persistent_write_attempted=0x00000000` x25, `failure_mask=0x00000000` x87,
    # `xnu_entry_failures=0x00000000`, and the device returned to Android on its own
    # (`getprop ro.build.version.release` = 10).
    #
    # **Next:** experiment 294 - `osfmk/kern/stack.c`, `thread_policy.c` and `osfmk/arm/machine_thread.c`
    # for `stack_init`, `thread_policy_init` and `machine_thread_init`, three consecutive
    # unconditional calls with nothing between them, so a single step can take all three. After that
    # `kernel_bootstrap`'s own tail: `atm_init`, `bank_init`, `ipc_pthread_priority_init`,
    # `corpses_init`, then `kernel_thread_create` and `load_context` - the first point in this whole
    # walk where XNU starts a thread rather than filling in a structure.
    OSFMK_KERN_BSD_KERN_OBJ=${STAGE90_ENTRY_OSFMK_KERN_BSD_KERN_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_bsd_kern.o}
    # 292: `mac_mach.o`, and a prediction that leaves two functions entirely
    #
    # **The 291 run reported** `stub_hit=mac_exc_create_label` at `ipc_task_init + 0x0dc`. The object
    # that defines it is `security/mac_mach.c`, `security_mac_mach.o` - **not** `security/mac_exc.c`,
    # which is what 291's own next-step note named and which is not a file in this tree. Nothing is
    # lost by the correction: the two names 291 obliged, `mac_exc_free_action_label` and
    # `mac_exc_inherit_action_label`, are defined in `mac_mach.c` too, so one object settles all
    # three.
    #
    # **The object, measured.** 3532 bytes of `.text` and nothing else - no `.data`, no `.rodata`, no
    # `.bss`, 15 definitions and **19** references. The one string literal, in
    # `mac_exc_action_check_exception_send`'s `printf`, is inside the object's `.text` input section,
    # so this is the first object of the walk whose entire contribution is code.
    #
    #     resolved  12   mac_exc_create_label, mac_exc_associate_action_label, mac_exc_free_label,
    #                    mac_exc_free_action_label, mac_exc_inherit_action_label,
    #                    mac_exc_update_action_label, mac_exc_update_task_crash_label,
    #                    mac_exc_create_label_for_proc, mac_exc_create_label_for_current_proc,
    #                    mac_task_check_expose_task, mac_task_check_set_host_special_port,
    #                    mac_task_check_set_host_exception_ports
    #                    - every one of them already a stub body in this image
    #     added      3   get_task_crash_label (`osfmk/kern/bsd_kern.c`), proc_self and proc_task
    #                    (`bsd/kern/kern_proc.c`)
    #     under      7   current_proc, get_bsdtask_info, kauth_cred_get, kauth_cred_proc_ref,
    #                    kauth_cred_unref, proc_find, proc_rele - already stub bodies, so this object
    #                    adds no demand for them
    #     real       9   current_task, mac_error_select, mac_labelzone_alloc, mac_labelzone_free,
    #                    mac_policy_list, mac_policy_list_conditional_busy,
    #                    mac_policy_list_unbusy, task_pid, _consume_printf_args
    #
    # Three of the 19 references are new demands and not seven, because the seven names above are
    # *already* stubs - the counting rule 288 had to correct, and this is the fourth object it has
    # held for. The other three definitions, `mac_exc_action_check_exception_send`,
    # `mac_task_check_set_host_exception_port` and `mac_thread_userret`, are referenced by nothing:
    # not by the image and not by the object's own `.text`. They arrive as dead code, and each is
    # confirmed absent from the image's symbol table, so the link cannot report a duplicate.
    #
    # **The prediction, and the interesting half of it is a correction to 291's next-step note.** The
    # thirteen unrolled iterations of `for (i = FIRST_EXCEPTION; i < EXC_TYPES_COUNT; i++)` call the
    # two names this object defines and nothing else, and both `MAC_PERFORM` bodies are measured
    # no-ops rather than assumed ones:
    #
    #   * the first loop's bound is loaded from the list itself: `ldr r0, [r6, #12]` is
    #     `mac_policy_list.staticmax` (numloaded 0, max 4, maxindex 8, staticmax 12, chunks 16,
    #     freehint 20, entries 24 - `struct mac_policy_list`, and the object's offsets match that
    #     layout exactly), and `mac_policy_init` sets it to 0 while nothing at boot calls
    #     `mac_policy_register`. `cmp r0, #0 / beq` at `mac_exc_create_label + 0x2c` skips the whole
    #     loop.
    #   * the second loop is behind `mac_policy_list_conditional_busy()`, whose first statement is
    #     `if (mac_policy_list.numloaded <= mac_policy_list.staticmax) return (0);` (mac_base.c:318)
    #     - `0 <= 0` - so the indirect `blx r2` that would reach `entries[i].mpc` and the
    #     `mac_policy_list_unbusy()` after it are both unreachable.
    #
    # So the iterations reduce to thirteen `mac_labelzone_alloc(MAC_WAITOK)` -> `zalloc(zone_label)`
    # (real, and the zone exists because `mac_policy_init` called `mac_labelzone_init` at 275) and
    # thirteen stores through `mac_exc_associate_action_label`. Then the `parent == TASK_NULL` arm -
    # `host_priv_self` (12 bytes, no call) and `host_get_host_port` -> `host_get_special_port` (three
    # real calls) - whose `assert(kr == KERN_SUCCESS)` is compiled out, as the image shows: no
    # comparison follows the call.
    #
    # **Then `ipc_task_init` returns, and the correction.** 291's note said "the nine further stub
    # calls `task_create_internal` makes at +0x300 and up". That is wrong, and the image says so
    # twice over: `task_create_internal` makes **exactly three** stub calls - `vm_shared_region_get`
    # at +0x300, `vm_shared_region_set` at +0x30c and `task_affinity_create` at +0x348 - and all
    # three are inside `if (parent_task != TASK_NULL)`, the arm that `task_init`'s `TASK_NULL`
    # argument does not take:
    #
    #   * the branch at +0x2cc (0x800bfe90) tests a word that is set to 1 **only** on the parent path
    #     (`str r7, [sp, #12]` sits inside `if (parent_task != TASK_NULL)`; the `beq` that skips it is
    #     taken for a null parent), and the block it jumps to is unmistakably the else - it stores
    #     `KERNEL_SECURITY_TOKEN`/`KERNEL_AUDIT_TOKEN` out of `.data` and picks `BASEPRI_KERNEL` when
    #     `kernel_task == TASK_NULL`, which is exactly the source of the else arm and the opposite of
    #     the parent arm's "inherit the parent's shared region".
    #   * the else path from there makes no stub call at all: `__bzero` x4, `kalloc_canblock`,
    #     `memset`, the `task_rollup_accounting_info` else arm (no call), and `coalitions_adopt_init_task`.
    #
    # `task_init` then returns through `vm_map_deallocate` and a tail call to `lck_spin_init`, both
    # real, and `kernel_bootstrap` - whose remaining calls are `kernel_debug_string_early` and
    # `thread_init` - hands over. **`thread_init`'s fourth call is `stack_init`, a stub**, at
    # `thread_init + 0xd4` (0x800091d4, caller 0x800092ac), followed by `thread_policy_init` and
    # `machine_thread_init`, three stubs in a row with nothing between them. That is where the
    # frontier would land if the coalition block were clean. **It is not clean:**
    #
    #     coalitions_adopt_init_task          coalitions_adopt_task(init_coalition, task)
    #       -> coalitions_adopt_task          the inlined coalition_adopt_task_internal:
    #            coalition_lock               lck_mtx_lock, real
    #            reaped/terminated check      `ldrb r0, [r7, #32] / tst r0, #12` - both clear
    #            coal_call(adopt_task)        blx through the ops table at 0x80103bfc: type 0's
    #                                         entry is i_coal_resource_adopt_task (0x800bebd8)
    #            counters, task->coalition[type] = coal
    #       -> get_task_uniqueid              ***STUB***  `if (get_task_uniqueid(task) != UINT64_MAX)`
    #
    # `get_task_uniqueid` is called on the way *out* of the inlined internal function, after
    # `out_unlock`, so it is reached whether or not `coal_call` succeeds. Predicted stop:
    #
    #     stub_hit=get_task_uniqueid    xnu_entry_stub_caller=0x800bd900   (coalitions_adopt_task + 0x0dc)
    #
    # with `coalitions_adopt_task` at 0x800bd820, in `osfmk_kern_coalition.o`, which this step does not
    # move because `mac_mach.o` goes at the end of the link list, after it.
    #
    # So the step is the first whose stop is **outside both of the functions the walk has been inside
    # since 288** - three hundred lines of `task_create_internal`, `ipc_task_init` and `task_init` all
    # return - and the first whose frontier is a name from a subsystem this walk has not touched:
    # `get_task_uniqueid` is `osfmk/kern/bsd_kern.c`, the BSD glue, not security and not IPC.
    #
    # **The falsifiers, named in advance.** Every one is a guard that had to be read rather than
    # assumed, and the ones that are *not* on this path are as much a part of the prediction:
    #
    #     kalloc_canblock -> ledger_debit -> ledger_entry_check_new_balance -> set_astledger
    #         `KALLOC_ZINFO_SALLOC` is called only on the large-allocation path (kalloc.c:679, inside
    #         the `size >= kalloc_max_prerounded` else), and `sizeof(struct io_stat_info)` takes
    #         `get_zone_dlut`. The debit is not reached, so the ledger AST is not either.
    #     kalloc_canblock -> vm_tag_alloc -> vm_tag_bt -> OSKextGetAllocationSiteForCaller
    #         `if (site) tag = vm_tag_alloc(site)` is inside the same large-path else (kalloc.c:645).
    #     kmem_alloc_flags -> trace_backtrace
    #         `log_leaks` in `.bss` is zero - 291's reading, unchanged.
    #     coalitions_adopt_init_task -> panic -> panic_trap_to_debugger -> PEHaltRestart
    #         only if `coalitions_adopt_task` returns non-zero (its own `if (kr != KERN_SUCCESS) panic`)
    #     task_create_internal -> panic("created task is not a member of a resource coalition")
    #         if the adopt did not store into `task->coalition[RESOURCE]`; a panic here would show as
    #         a panic line or as silence, not as a `stub_hit`
    #     i_coal_resource_adopt_task -> panic -> ...      a depth-4 scan from it finds only hits behind
    #         `panic`; nothing on its success path
    #     place_task_hold -> thread_hold, get_audit_token_pid       ***not reached***: `place_task_hold`
    #         is guarded by `kernel_task != TASK_NULL && kernel_task != new_task`, and `kernel_task` is
    #         still NULL - it is written from the out-parameter at the very end of the function
    #     thread_init -> stack_init, thread_policy_init, machine_thread_init   ***not reached***: after
    #         the coalition block
    #     lck_mtx_lock -> ... -> ast_taken_kernel     nothing has posted an AST this early (291's
    #         reading of the preemption path); a depth-2 scan of `lck_mtx_lock` finds no stub
    #
    # **The build.** 12 stubs retired, 3 obliged, so 827 -> **818** undefined, 733 -> **724**
    # function stubs, 94 storage. The object brings no `.data` and no `.bss`, so the file's size can
    # only change if `.text`'s growth pushes `.data` - which is 16 KB-aligned and currently sits at
    # exactly 0x8010c000 - over the boundary. `__TEXT,initcode`, the last thing before it, ends at
    # 0x8010af1c: **4324 bytes of room**. The object's 3532 bytes of `.text` are offset by the twelve
    # retired stub bodies (6.3 bytes each - 4616 bytes of `.text` for 733 names in the stub object)
    # and the three new ones, for a net of about **+3.4 KB**, which leaves about **870 bytes**. So:
    #
    #     predicted        measured
    #     undefined   818
    #     function    724
    #     storage      94
    #     image         1198104  (unchanged, and for the first time with under a kilobyte to spare)
    #     .data         0x8010c000  (unchanged)
    #     __bss_start   0x80123c08
    #     bss end       0x8015a458 -> about 0x8015a3f8, moving *down* by the nine fewer stub slots
    #     text          ~1096500 (the one soft number; 3532 - 12*9.6 + 3*9.6)
    #
    # The bss move is the step's second measurement, and it is the pin's first real test: nine fewer
    # stub `.bss` slots move `__entry_reset_handler_data` down with them, and XNU's two writes must
    # follow it into the reserved slot. Before 291 the same movement took them into `entry_epilogue`;
    # `verify_pad` derives both addresses from the link rather than comparing a literal, so if it
    # still holds with the slot under 0x8015a448 the instrument is measuring what it claims to.
    #
    # **The build.** Twelve stubs retired and three obliged, and all three counts are exact:
    #
    #                   predicted        measured
    #     undefined     818              818
    #     function      724              724
    #     storage       94               94
    #     image         1198104          1198104
    #     .data         0x8010c000       0x8010c000
    #     __bss_start   0x80123c08       0x80123c08
    #     text          ~1096500         1096376      (+2976; the soft number, off by 130)
    #
    # Two things the numbers corrected, both worth keeping:
    #
    #   * the object's 3532 bytes of `.text` net +2976, so a retired stub name is worth about **62
    #     bytes**, not the 46 the 291 arithmetic suggested - the same figure 290's three retirements
    #     implied (65). The 2976 is `realstubs.o`'s `.text` (17592 -> 17376) and `.rodata.str1.4`
    #     (15388 -> 15060) shrinking against the new object exactly.
    #   * **`__bss_end` did not move, and the prediction that it would was wrong.** The two write
    #     addresses are still 0x8015a448 and 0x8015a44c, and `.bss` is 0x35c18 both before and after.
    #     The modelling error is worth recording because it is a fact about the generator: the
    #     function stubs have **no `.bss` slot at all** - their bodies and their name strings are the
    #     whole of their cost, both in `realstubs.o` - so `.bss` moves only when the *storage* set
    #     moves, and the storage set is unchanged at 94. The 291 note that attributed +64 to
    #     "alignment inside the stub object's storage" was right about the mechanism and wrong about
    #     the trigger. Which means the pin is more stable than this step expected: retiring function
    #     names cannot move the slot, because it cannot move `.bss`.
    #
    # The pad did not move either (0x800023d4, branching over 512 bytes to 0x800025d4, exactly as in
    # 291's post-fix build) - it lives in `xnu_arm_entry_stubs.o`, the fixed entry-side object whose
    # `.text` is 4616 bytes, at offset 0x454 in it.
    #
    # **The run.**
    #
    #     stub_hit=get_task_uniqueid     xnu_entry_stub_caller=0x800bd900
    #
    # `0x800bd900` is `coalitions_adopt_task + 0x0dc`, the return address of the `bl` at 0x800bd8fc,
    # and the `_a` and `_e` copies of the caller address agree with `_v`. So one run measured the
    # longest chain this walk has cleared *and* the first one that leaves a function:
    # thirteen real label allocations and thirteen real associations, `ipc_task_init`'s whole
    # `TASK_NULL` arm, `task_create_internal`'s else path through `KERNEL_SECURITY_TOKEN`,
    # `kalloc_canblock`, the rollup else, `task_init`'s own tail, and eight real levels into
    # `coalitions_adopt_init_task` -> `coalitions_adopt_task` -> `i_coal_resource_adopt_task`. Both
    # functions 291 predicted the frontier would spend several more steps inside are behind it.
    #
    # Preflight clean (`loader_xnu_entry_stub_status=0x90000001`,
    # `high_va_data_verified=0x00000001`), log 301118 bytes, no `exception:` line,
    # `xnu_entry_kv_dropped=0x00000000`, and every abort-record field zero.
    #
    # **Safety:** non-persistent `fastboot boot` only, nothing flashed,
    # `persistent_write_attempted=0x00000000` x25, `failure_mask=0x00000000` x87,
    # `xnu_entry_failures=0x00000000`, and the device returned to Android on its own
    # (`getprop ro.build.version.release` = 10).
    #
    # **Next:** experiment 293 - `osfmk/kern/bsd_kern.c` (`osfmk_kern_bsd_kern.o`) for
    # `get_task_uniqueid`, if the prediction holds. A pleasant one if it does: the same object also
    # defines `get_task_crash_label`, one of the two names *this* step obliges, and it is 4488 bytes
    # of `.text`, 60 definitions and 34 references.
    SECURITY_MAC_MACH_OBJ=${STAGE90_ENTRY_SECURITY_MAC_MACH_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/security_mac_mach.o}
    # 291: `ipc_tt.o`, and a prediction that does not depend on which branch is taken
    #
    # **The 290 run reported** `stub_hit=ipc_task_init` at `task_create_internal + 0x224`. The object
    # that defines it is `osfmk/kern/ipc_tt.c`, `osfmk_kern_ipc_tt.o` - the largest step since 288:
    # **12676 bytes of `.text`, 60 definitions, 53 references**, and the object whose whole port-name
    # conversion surface this walk has been calling through since 278.
    #
    #     resolved  37   the fifteen `convert_*` names, `ipc_task_init`/`enable`/`disable`/`reset`/
    #                    `terminate`, the four `ipc_thread_*`, `mach_ports_lookup`/`_register`,
    #                    `port_name_to_thread`, `space_deallocate`, `space_inspect_deallocate`,
    #                    the five `task_*_special_port`/`*_exception_ports` and the five `thread_*`
    #                    - every one of them already a *stub* in this image, so this step retires 37
    #                    names, the second-largest retirement of the walk after 277's 16-name host
    #                    surface (and the largest by count)
    #     added      2   mac_exc_free_action_label, mac_exc_inherit_action_label
    #                    - both functions, both from the same unlinked `security/mac_exc.c`
    #
    # Eight of the 53 references (`io_free`, `ipc_object_copyin`, `ipc_object_translate` and five
    # `mac_exc_*`) are already stubs and 43 are already real, so `added` is 2 and not 45 - the
    # counting rule 288 had to correct. The fifty-fifth name in the object's `.text` is `ipc_task_init`
    # itself, at **object offset 0**, so unlike 284 this time the object offsets *are* function offsets
    # and no adjustment is needed.
    #
    # **The prediction, read off the object's own call list in address order.** Every `bl` in
    # `ipc_task_init`'s 0x2e8 bytes, with its status in the current image:
    #
    #     +0x020  ipc_space_create          real    (ipc_space.o, linked long before this walk)
    #     +0x034  panic                     real
    #     +0x04c  ipc_port_alloc_special    real    (ipc_port.o, linked in 278 - it was 277's stop)
    #     +0x064  panic                     real
    #     +0x06c  ipc_port_alloc_special    real
    #     +0x084  panic                     real
    #     +0x09c  lck_mtx_init              real
    #     +0x0c4  ipc_port_make_send        real
    #     +0x0d8  mac_exc_create_label      ***STUB***   <- the stop, return address +0x0dc
    #     +0x0e4..+0x1a4  the other twelve unrolled MACF iterations (create_label +
    #                    associate_action_label, both stubs)
    #     +0x1b8  lck_mtx_lock              real
    #     +0x1c0..+0x264  the `parent != TASK_NULL` arm: eight ipc_port_copy_send,
    #                    mac_exc_inherit_action_label, lck_mtx_unlock
    #     +0x2a8  host_priv_self            real
    #     +0x2b8  host_get_special_port     real    (the `parent == TASK_NULL` arm)
    #
    # **The stop is at +0x0d8 whichever branch is taken**, which is worth stating because it is the
    # first prediction of the walk that does not depend on a branch: the `CONFIG_MACF` loop
    # (`for (i = FIRST_EXCEPTION; i < EXC_TYPES_COUNT; i++)`, thirteen iterations, fully unrolled)
    # sits *above* `if (parent == TASK_NULL)` in the source and above both arms in the text. The one
    # thing the parent value would decide is which arm runs *after* the stop, which is the next step's
    # business.
    #
    # **What has to return before it, and why each one does.** Eight real calls stand between the
    # entry and the stop, and three of them have a stub somewhere inside them - the same trap 285-290
    # kept meeting, so each guard was read rather than assumed:
    #
    #     ipc_space_create   calls zalloc, ipc_table_alloc, memset, lck_spin_init, zfree - all real.
    #                        The `zfree` is in the `if (table == IE_NULL)` arm, i.e. only when
    #                        `it_entries_alloc` fails, which needs kalloc to be out of memory.
    #     ipc_table_alloc    is `return kalloc(size)` - `VM_ALLOC_SITE_STATIC(0, 0)`, **flags 0**,
    #                        so `kalloc_canblock`'s `vm_tag_alloc(site)` reaches `vm_tag_bt` only if
    #                        `VM_TAG_BT` is set, and it is set only by the `*_tag_bt` macro variants
    #                        (kalloc.h:100-139). So `OSKextGetAllocationSiteForCaller` - a stub, and
    #                        the deepest thing under this path - is not reached.
    #     kmem_alloc_flags   has exactly one stub call, `trace_backtrace` at +0x50, behind
    #                        `ldr r0, [0x8012a1ac] / cmp r0, #0 / beq +0x54`. That word is `log_leaks`,
    #                        in `.bss` (so zeroed by the payload) and set only by the sysctl of that
    #                        name. Closed.
    #     lck_spin_unlock    is a four-byte tail call into `hw_lock_unlock` -> `_enable_preemption`,
    #                        whose one stub call `ast_taken_kernel` sits behind
    #                        `thread->machine.CpuDatap->cpu_pending_ast & AST_URGENT`
    #                        (locks_arm.c:319). Nothing has posted an AST this early. Closed.
    #     zfree              has four stub calls - `trace_backtrace`, `btlog_add_entry`,
    #                        `OSBacktrace`, `btlog_remove_entries_for_element` - all in the zone
    #                        logging paths, which 287 already measured as needing a `zlog` boot-arg
    #                        this payload does not carry, and `zfree` is not on this path anyway.
    #
    # Predicted report: **`stub_hit=mac_exc_create_label`**, `xnu_entry_stub_caller` = the return
    # address of the `bl` at function offset +0x0d8, i.e. **`ipc_task_init + 0x0dc`**. **Falsifiers,
    # named in advance:** `ast_taken_kernel` (an urgent AST is pending at boot), `log_leaks` being
    # non-zero, `OSKextGetAllocationSiteForCaller` (a `VM_TAG_BT` allocation site), or one of
    # `zfree`'s four - each would mean one of the guards above is open, and each is a measurement
    # about a boot-arg or a global rather than about this step.
    #
    # **Predicted build deltas:** 864 -> **829** undefined (37 out, 2 in), 770 -> **735** function
    # stubs (37 out, 2 in), 94 -> **94** storage (nothing in). Text is the uncertain one for 290's
    # reason - the stub object's name strings live inside `.text` and thirty-seven of them leave the
    # image while two arrive - so: text 1082456 + 12676 (object) - 888 (37 retired bodies) + 48 (2
    # new bodies) - the retired names' bytes, i.e. roughly **+11.0 KB**. Read-only content is
    # therefore about 12.6 KB against **15268 bytes of slack** (the read-only region ends at
    # 0x8010845c and `.data` sits at 0x8010c000), so `.data` should **not** move, the image should
    # grow by only the object's 48 bytes of `__DATA,__data`, and `__bss_start`, bss end and the
    # headroom should all be unchanged. Worth flagging now: **that leaves about 2.6 KB of slack, so
    # the step after this one probably crosses the boundary.**
    #
    # ================================================================== and then the build refused it
    #
    # **The step's prediction was never tested, because the instrument failed first - correctly.**
    # `verify_pad` stopped the build:
    #
    #     FAIL: entry_skip_pad is at 0x800023d4, which puts the branch itself on or above
    #           0x8000235c, one of the addresses XNU writes
    #
    # Thirty-seven retired stubs moved `&ResetHandlerData - &ExceptionLowVectorsBase` from 290's
    # 0x2448 down to **0x2358** - 0x7c bytes *below* the 512-byte pad's start - so XNU's two writes
    # were about to land in `entry_epilogue`'s own code again, 282's and 288's failure a third time.
    # The check 288 built caught it before the device was touched: the third defect this project has
    # had stopped by a build rather than by a run, and the first one stopped *before* it cost a
    # single silent run.
    #
    # **The treadmill is the point.** The difference is a difference between the addresses of two
    # *generated stub bodies*, 0x18 bytes apart each, so it moves by 0x18 for every stub name that
    # enters or leaves the alphabetically-ordered stub object between "E" and "R". The five values
    # that have been measured are 0x2404 (281), 0x24a8 (288), 0x24c0 (289), 0x2448 (290) and
    # 0x2358 (291): a pad can be re-aimed at each of them, and the next step moves it again. So the
    # pad was not widened and not moved - **the value was given one definition**, in `entry.ld`:
    #
    #     ExceptionLowVectorsBase = ENTRY_BASE;                        /* the image base */
    #     ResetHandlerData = __entry_reset_handler_data - 4;           /* a reserved .bss slot */
    #
    # which is the same fix - one value, one definition - that 288 applied to the *check* and that
    # this step applies to the *value*. Two consequences, both of them measured in the linked image:
    # the two writes land in the sixteen reserved bytes at 0x8015a448 (zeroed by the payload, never
    # executed, read by nothing), and `cpu.c:565`'s `bcopy(&ExceptionLowVectorsBase, LowExceptionVectorsAddr,
    # 0x90)` plus the page-long copy that follows it become a page copied onto itself through two
    # mappings of the same physical page - where before, with the base being a stub address, they
    # took 4096 bytes of stub bodies over page zero, i.e. over start.s's own code. That second
    # consequence had been latent since the beginning and is worth stating: **nothing was copying the
    # vectors anywhere; it was copying the stub object onto page zero.**
    #
    # Both names are script symbols and pass 1 links with the same script, so the generator never
    # sees them as undefined and never stubs them: two fewer stubs, and the corrected prediction is
    # 864 -> **827** undefined and 770 -> **733** function stubs, 94 storage. `verify_pad` now checks
    # the two write addresses against the reserved slot - and still checks that `ExceptionLowVectorsBase`
    # is the image base, and that the pad is a real skipped range, since a skipped 512-byte region
    # between the sweep and the geometry costs image bytes and nothing else.
    #
    # **The build:** 827 undefined, 733 function stubs, 94 storage - the corrected prediction to the
    # unit. Text 1082328 -> **1093400**, image 1198056 -> **1198104**: **+48 bytes, exactly the
    # object's `__DATA,__data`**, because `ipc_tt.o`'s 12.5 KB of text fitted inside the 15268 bytes
    # of slack, so `.data` did not move and `__bss_start` is unchanged at **0x80123c08**. bss end
    # 0x8015a408 -> 0x8015a458 (+16 for the reserved slot, +64 of alignment inside the stub object's
    # storage), headroom 1727480 -> 1727400. The two writes:
    #
    #     entry_skip_pad at 0x800023d4 branches over 512 bytes to 0x800025d4
    #     XNU writes 0x8015a448 and 0x8015a44c (ResetHandlerData - ExceptionLowVectorsBase = 0x15a444),
    #     both inside the reserved slot at 0x8015a448
    #
    # **And `ipc_task_init` is where the run now goes**, with the prediction below unchanged by any of
    # the above: the stop is at function offset +0x0d8 whichever branch is taken.
    #
    # **The run - and it is the first one that measured the pinned instrument:**
    #
    #     stub_hit=mac_exc_create_label        xnu_entry_stub_caller=0x800c8d1c
    #
    # `0x800c8d1c` is `ipc_task_init + 0x0dc`: the function links at 0x800c8c40 and the image's own
    # instruction stream has `bl mac_exc_create_label` at 0x800c8d18. So the longest chain of calls
    # this walk has had to clear - eight real calls before the stop, including two
    # `ipc_port_alloc_special`s, `ipc_space_create` with its `kalloc_canblock` underneath and
    # `ipc_port_make_send` - all returned, and none of the three guards named as falsifiers was open:
    # no `ast_taken_kernel`, no `trace_backtrace`, no `OSKextGetAllocationSiteForCaller`. The `parent`
    # value never mattered, which is what made this prediction branch-independent.
    #
    # **The instrument change is measured too, and that is the more important half of this run.**
    # The report arrived normally with XNU's two writes aimed at the reserved `.bss` slot instead of
    # at a skipped pad in the entry's text: the report path is intact, `kernel_entry ok`,
    # `loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`. So the treadmill
    # is over - the two addresses are now a property of the link script rather than of where the stub
    # object's alphabetically-ordered bodies happen to sit, and they cannot drift again.
    #
    # Preflight clean, log 301121 bytes, no `exception:` line.
    #
    # **Safety:** non-persistent `fastboot boot` only, nothing flashed,
    # `persistent_write_attempted=0x00000000` x25, `failure_mask=0x00000000` x87,
    # `xnu_entry_failures=0x00000000`, and the device returned to Android on its own
    # (`getprop ro.build.version.release` = 10).
    #
    # **Next:** experiment 292 - `security/mac_exc.c` for `mac_exc_create_label` and its
    # `mac_exc_associate_action_label` partner, the two names this step itself added and the thirteen
    # unrolled loop iterations that call them. After that the frontier is `ipc_task_init`'s remaining
    # real work - the two arms of `if (parent == TASK_NULL)`, whose `host_get_special_port` arm is
    # real and whose `parent != TASK_NULL` arm is eight `ipc_port_copy_send`s - and then the nine
    # further stub calls `task_create_internal` makes at +0x300 and up.
    OSFMK_KERN_IPC_TT_OBJ=${STAGE90_ENTRY_OSFMK_KERN_IPC_TT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_ipc_tt.o}
    # 290: `machine_task.o`, and an object whose whole function is empty
    #
    # **The 289 run reported** `stub_hit=machine_task_init` at `task_create_internal + 0x200`. The
    # object that defines it is `osfmk/arm/machine_task.c`, `osfmk_arm_machine_task.o` - and it is the
    # smallest step this walk has taken in a hundred experiments.
    #
    # **The object, measured.** 324 bytes of `.text` for five functions, five definitions and six
    # references - and `machine_task_init` sits at object offset 0x140, the **last four bytes** of that
    # text, because its body is empty (`machine_task.c:171-175`: three `__unused` parameters and not one
    # statement). A `bx lr` and nothing else. So the prediction is that this step overshoots, the way
    # 285's did.
    #
    #     resolved   5   machine_task_set_state   machine_task_get_state   machine_task_init
    #                    machine_task_terminate   machine_thread_inherit_taskwide
    #                    - all five are already *stubs* in this image, so the object retires all five
    #     added      2   ads_zone            (osfmk/arm/pcb.c:58   - storage, 4 bytes)
    #                    copy_debug_state    (osfmk/arm/pcb.c:361  - function)
    #
    # Three of the six references (`bzero`, `zalloc`, `zfree`) are real, and the fourth
    # (`machine_thread_set_state`) is already a stub - so under the rule `under = (stub list ∪ undefined
    # list)` it is not new, which is the counting rule 288 had to correct. **Both added names come from
    # the same object**, `osfmk/arm/pcb.c`, which is not linked: this step *also* obliges its successor,
    # and the ledger should say so now rather than have the next step discover it. `ads_zone` is a
    # storage stand-in and it is worth noting what kind of value it is - four zero bytes standing for a
    # `zone_t` - because `machine_task_set_state` and `machine_task_terminate` pass it to `zalloc` and
    # `zfree`; nothing on this boot's path calls either, but the first `task_terminate` will.
    #
    # **The prediction is about a place rather than a name.** `task_create_internal` is the object 288
    # linked, and 289's ledger listed its stub calls in address order. The call this step makes real
    # returns immediately, so the frontier is the *next* one on that list:
    #
    #     +0x1fc  machine_task_init     ***this object*** - an empty body, returns at once
    #     +0x220  ipc_task_init         <- the stop, return address +0x224
    #     +0x300  vm_shared_region_get  +0x30c  vm_shared_region_set
    #     +0x348  task_affinity_create  +0x360  task_is_marked_importance_donor
    #
    # and the eight instructions between the two calls are `add`, `mov` and `str` only - measured in the
    # linked image, so `bl` appears nowhere among them and nothing can stop in between:
    #
    #     800bfdc4  add r0, r4, #504   800bfdc8  mov r5, #0           800bfdcc  str r0, [r4,#504]
    #     800bfdd0  mov r1, sl         800bfdd4  str r0, [r4,#508]    800bfdd8  mov r0, r4
    #     800bfddc  str r5, [r4,#512]  800bfde0  str r5, [r4,#524]    800bfde4  bl ipc_task_init
    #
    # Predicted report: `stub_hit=ipc_task_init`, `xnu_entry_stub_caller` = **0x800bfde8** =
    # `task_create_internal + 0x224`. **Falsifier, named in advance:** a report naming any of the five
    # names this object defines would mean `machine_task_init` faulted or called something - impossible
    # for a `bx lr`, so it would be a statement about the stub generator or the pad rather than about
    # this step; and a report naming `copy_debug_state` or `ads_zone` would mean one of the object's
    # other three functions was entered, and nothing on this path calls them.
    #
    # **Predicted build deltas:** 867 -> **864** undefined (5 out, 2 in), 774 -> **770** function stubs
    # (5 out, 1 in), 93 -> **94** storage (one in). Text 1082328 -> about 1082652 (+324) - and this is
    # the first step in three where `.data` should **not** move, because 324 bytes of read-only content
    # is well inside the slack the read-only region now has.
    # **The build and the run.** 864 undefined, 770 function stubs, 94 storage - all three exactly as
    # predicted (5 out, 2 in). `__bss_start` unchanged at **0x80123c08**, `.data` unchanged at
    # 0x8010c000, bss end 0x8015a3c8 -> 0x8015a408, headroom 1727544 -> 1727480, and **the image
    # unchanged at 1198056 bytes** - the prediction that `.data` would not move for the first time in
    # three steps. Text 1082328 -> **1082456**, i.e. **+128, not the +324 the object's own sizes
    # suggest**, and the reason is worth recording because it applies to every future step: `entry.ld:46`
    # places `*(.rodata .rodata.*)` **inside the `.text` output section**, and the generated stub
    # object's symbol-name strings live there. This step retires five stub bodies (5 x 0x18 = 0x78) and
    # five symbol *names*, and adds one of each plus `ads_zone`'s name, so the object's 324 bytes of
    # `.text` arrive net of a name-string region that also shrank. The byte split is not asserted here;
    # the measured 128 is. The lesson: `text size` grows by the object's `.text` plus or minus the
    # names entering and leaving the stub object, so a name-retiring step always undershoots a
    # body-only prediction.
    #
    # **`verify_pad` moved down for the first time**, which is the whole reason its derived form exists:
    #
    #     entry_skip_pad at 0x800023d4 branches over 512 bytes to 0x800025d4
    #     XNU writes 0x8000244c and 0x80002450 (ResetHandlerData - ExceptionLowVectorsBase = 0x2448),
    #     and both land inside what it skips
    #
    # 0x2448 against 289's 0x24c0 - down 0x78, and the interval can be read exactly: `nm` says **386
    # symbols at 0x18 bytes apart** sit between `ExceptionLowVectorsBase` (0x800ecbf4) and
    # `ResetHandlerData` (0x800ef03c), i.e. the interval *is* the generated stub object, so it shrinks
    # when stubs are retired, exactly as it grew when they were added. The observed range across the
    # experiments that have printed it is 0x2404 (281) .. 0x24c0 (289) .. 0x2448 (290), inside a pad
    # spanning 0x23d4 .. 0x25d4. The check fails the build if a future value leaves that window, which
    # is what makes this safe; the day it fails, the fix is to *move* the pad rather than widen it.
    #
    # **The run:**
    #
    #     stub_hit=ipc_task_init        xnu_entry_stub_caller=0x800bfde8
    #
    # `0x800bfde8` is `task_create_internal + 0x224`, the return address of the `bl ipc_task_init` at
    # 0x800bfde4 - predicted before the device was touched, and the eight instructions between the two
    # calls are the `add`/`mov`/`str` the ledger listed. The image also shows this step's own effect
    # directly: `machine_task_init` was a stub at 0x800ede1c and is now real at **0x800c8c3c**. So one
    # run measured that the empty function was entered and returned, that nothing in
    # `task_create_internal` between the two calls can stop, and that the frontier is the second of the
    # ten stub calls that function makes.
    #
    # Preflight clean (`loader_xnu_entry_stub_status=0x90000001`,
    # `high_va_data_verified=0x00000001`), log 301114 bytes, no `exception:` line.
    #
    # **Safety:** non-persistent `fastboot boot` only, nothing flashed,
    # `persistent_write_attempted=0x00000000` x25, `failure_mask=0x00000000` x87,
    # `xnu_entry_failures=0x00000000`, and the device returned to Android on its own
    # (`getprop ro.build.version.release` = 10).
    #
    # **Next:** experiment 291 - `osfmk/kern/ipc_tt.c` (`osfmk_kern_ipc_tt.o`) for `ipc_task_init`. A
    # first reading, before the build: **12676 bytes of `.text`, 60 definitions and 53 references** -
    # the largest step since 288, and one that should retire a block of `ipc_*` stubs rather than one.
    # Two names this step obliged are still waiting and are *not* in this object: `ads_zone` and
    # `copy_debug_state`, both from `osfmk/arm/pcb.c` (`osfmk_arm_pcb.o`, 1132 bytes of `.text`, 17
    # definitions - the two of them plus the whole `machine_*` context-switch surface). They are only
    # reached by `machine_task_set_state` and `machine_task_terminate`, which nothing on this path
    # calls, so `pcb.o` can wait until a `task_terminate` does.
    OSFMK_ARM_MACHINE_TASK_OBJ=${STAGE90_ENTRY_OSFMK_ARM_MACHINE_TASK_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_arm_machine_task.o}
    # 289: `task_policy.o`, and two new names that both turn out to be reachable
    #
    # **The 288 run reported** `stub_hit=task_watch_init` at `task_init+0xb0`, once the pad's
    # contract was fixed. The object that defines `task_watch_init` is `osfmk/kern/task_policy.c`,
    # `osfmk_kern_task_policy.o` - and a pleasant surprise measured before the build: it defines
    # **both** of the two new names in `task_init`'s body that 288 introduced, `task_watch_init` and
    # `proc_init_cpumon_params`, so this step is worth two calls rather than one.
    #
    # **The object, measured.** 14636 bytes of text, 16 of data, 240 of rodata, 479 of
    # `rodata.str1.1`, 28 of bss, 72 of `__DATA,__data`, 16 of `rodata.cst16`, 71 definitions and 82
    # references. It resolves **28** - 26 functions and the two storage stand-ins 288 had just added
    # (`default_task_effective_policy` and `default_task_requested_policy`, both `R`) - and adds
    # **8**: seven functions and one storage (`thread_qos_policy_params`, `R`):
    #
    #     mig_strncpy   proc_apply_resource_actions   proc_apply_task_networkbg   proc_pidpathinfo_internal
    #     proc_restore_resource_actions   thread_policy_update_complete_unlocked
    #     thread_policy_update_tasklocked   thread_qos_policy_params
    #
    # **The prediction, and it is the longest chain yet because two of the five functions it has to
    # clear are ones this step itself makes real.** `task_init`'s calls in address order after the
    # 288 stop:
    #
    #     8  task_watch_init          ***this object*** - its whole body is one `lck_mtx_init`, real
    #     9  PE_parse_boot_argn       real, and its own extent has no call at all
    #    10  PE_get_default           real - and this is the one branch in the chain that a *string*
    #                                 decides, the third kind of stop-decider this walk has met. Its
    #                                 body looks `/defaults` up first (`DTLookupEntry(NULL, ...)`),
    #                                 and reaches `IODTGetDefault` - **a stub** - only when that lookup
    #                                 *fails*. The payload's synthetic tree has a `defaults` child
    #                                 (`stage90_main.c:735`, `apple_dt.c:230`), and `kSuccess` is 1
    #                                 (`device_tree.h:125`), so the lookup succeeds, the code takes the
    #                                 `DTGetProperty` path - real - and `IODTGetDefault` is never called.
    #    11-13 _consume_printf_args x3   real
    #    14  proc_init_cpumon_params  ***this object*** - five calls, PE_parse_boot_argn and
    #                                 PE_get_default only, both real
    #    15-21 PE_parse_boot_argn x7  real
    #    22  task_create_internal     real (task.o, linked in 288) - and its first stub call is the
    #                                 next thing on the path
    #
    # **`task_create_internal` is where the frontier actually is**, and it is worth listing its stub
    # calls in address order because the run will name one of them and the ledger should say which
    # came first:
    #
    #     +0x1fc  machine_task_init                     <- the first, return address +0x200
    #     +0x220  ipc_task_init
    #     +0x300  vm_shared_region_get        +0x30c  vm_shared_region_set
    #     +0x348  task_affinity_create        +0x360  task_is_marked_importance_donor
    #     +0x3f4  task_is_marked_importance_receiver
    #     +0x428  task_is_marked_importance_denap_receiver
    #     +0x480  task_policy_create          +0x6b0  ipc_task_enable
    #
    # and everything it calls before that - `zalloc`, `ledger_instantiate`, `sched_group_create` -
    # was scanned in the linked image and has no stub in it.
    #
    # Predicted report: `stub_hit=machine_task_init`, `xnu_entry_stub_caller` = `task_create_internal
    # + 0x200`. The falsifier, named in advance because this chain is long: a report naming
    # `IODTGetDefault` would mean the `/defaults` lookup failed and the tree is not what
    # `apple_dt.c` promises, which is a measurement about the device tree rather than about this step.
    #
    # **Predicted build deltas:** 887 -> **867** undefined (28 out, 8 in), 793 -> **774** function
    # stubs, 94 -> **93** storage, text 1067704 -> 1082340 (+14636). Read-only content is 15355 bytes
    # and the slack after 288's 32 KB move is whatever the build reports; if it crosses, `.data`
    # moves a block again.
    # **The build and the run.** 867 undefined, 774 function stubs, 93 storage - all three exactly as
    # predicted (28 out, 8 in). Text 1067704 -> **1082328**; the arithmetic predicted 1082340, so 12
    # bytes high, the same kind of slack 286 showed at 36. Image 1181584 -> **1198056**, `__bss_start`
    # 0x8011fbf8 -> **0x80123c08**, bss end 0x80156388 -> 0x8015a3c8, headroom 1743992 -> **1727544**.
    # `.data` crossed another 16 KB block, 0x80108000 -> **0x8010c000** - the second step in a row to
    # move it - and the whole image grew by 0x4058, which is the 0x4000 the base moved plus the 0x58
    # the two sections above `.data` (`__DATA, __const` 0x144, `__DATA, __data` 0xa98) grew together.
    #
    # `verify_pad` printed the derivation it now owns, and this is the first step that shows the
    # derived form tracking a moving value rather than a written-down one:
    #
    #     entry_skip_pad at 0x800023d4 branches over 512 bytes to 0x800025d4
    #     XNU writes 0x800024c4 and 0x800024c8 (ResetHandlerData - ExceptionLowVectorsBase = 0x24c0),
    #     and both land inside what it skips
    #
    # 0x24c0 is 0x18 above 288's 0x24a8 and 0xbc above 281's 0x2404, and both addresses still land
    # inside the pad - which is the whole point of having stopped comparing against 0x2404/0x2408.
    #
    # **The run:**
    #
    #     stub_hit=machine_task_init        xnu_entry_stub_caller=0x800bfdc4
    #
    # `0x800bfdc4` is `task_create_internal + 0x200`: the function links at 0x800bfbc4 and the image's
    # own instruction stream has `bl machine_task_init` at 0x800bfdc0, so the reported caller is the
    # return address of that call to the byte. **The prediction held, falsifier included** - the report
    # names `machine_task_init` and not `IODTGetDefault`, so the `/defaults` lookup `PE_get_default`
    # makes *succeeded*, and the payload's synthetic tree is what `apple_dt.c` promises.
    #
    # So one run measured the longest chain of the walk - fourteen calls that had to return before the
    # stop - including the two names this step itself brought in: `task_watch_init` (one
    # `lck_mtx_init`) and `proc_init_cpumon_params` (five `PE_parse_boot_argn` / `PE_get_default`
    # calls) both completed, along with `PE_get_default`, the three `_consume_printf_args`, seven more
    # `PE_parse_boot_argn`, and `task_create_internal`'s own prelude - `zalloc`, `ledger_instantiate`
    # and `sched_group_create`, none of which contains a stub.
    #
    # Preflight clean (`loader_xnu_entry_stub_status=0x90000001`,
    # `high_va_data_verified=0x00000001`), log 301118 bytes, no `exception:` line.
    #
    # **Safety:** non-persistent `fastboot boot` only, nothing flashed,
    # `persistent_write_attempted=0x00000000` x25, `failure_mask=0x00000000` x87,
    # `xnu_entry_failures=0x00000000`, and the device returned to Android on its own
    # (`getprop ro.build.version.release` = 10).
    #
    # **Next:** experiment 290 - `osfmk/arm/machine_task.c` (`osfmk_arm_machine_task.o`) for
    # `machine_task_init`. A first reading of the object, before the build: **324 bytes of `.text` for
    # five functions**, five definitions, six references, and `machine_task_init` sits at object offset
    # 0x140 - the last four bytes of that text, because **its whole body is empty**
    # (`machine_task.c:171-175`: three `__unused` parameters and no statement at all). So the
    # prediction is again that the step overshoots: the frontier moves to the next stub call in
    # `task_create_internal`, which this image shows is `bl ipc_task_init` at 0x800bfde4, return
    # address **0x800bfde8 = task_create_internal + 0x224**. What could stop it first is the code
    # between the two calls - seven instructions of `mov`/`str`/`add` and one `vmov.i32`, no `bl`.
    OSFMK_KERN_TASK_POLICY_OBJ=${STAGE90_ENTRY_OSFMK_KERN_TASK_POLICY_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_task_policy.o}
    # 288: `task.o`, and the object the walk has been circling since 285
    #
    # **The 287 run reported** `stub_hit=init_task_ledgers` at `coalitions_init+0xe8`. The object
    # that defines it is `osfmk/kern/task.o`, `osfmk_kern_task.o` - the largest single step this walk
    # has taken since `kern_event.o` in 280, and the object 285's census, 287's four new obligations
    # and the pending list have all been pointing at. **23504 bytes of text, 496 of bss, 104 of data,
    # 1094 of `rodata.str1.1`, 144 of `__DATA,__data`, 264 of `__TEXT,__os_log`, 146 definitions and
    # 224 references.**
    #
    #     resolved   60   (54 functions and 6 storage stand-ins: dead_task_statistics, kernel_task,
    #                      max_task_footprint_mb, task_ledgers, task_ledger_template, task_max -
    #                      every one of them a *generated* stand-in, checked against the stub list)
    #     added      59   (56 functions and 3 storage: default_task_effective_policy,
    #                      default_task_requested_policy, exc_via_corpse_forking)
    #
    # The resolved set is the largest of the walk and it includes `task_init` itself and `task_max` -
    # the two names the pending list has carried for several steps - so this step retires a whole
    # block of adjacent stubs rather than one. **The `added` count is worth a warning**, because a
    # first pass at it said 66: that pass checked each reference against the *image's* symbol table
    # without subtracting the generated stubs, so seven names that are already stand-ins in this
    # image (`task_watch_init`, `proc_init_cpumon_params`, ...) were counted as new. The rule is
    # `under = (stub list ∪ undefined list)`, not `the image's nm output`.
    #
    # **Predicted build deltas:** 888 -> **887** undefined (60 out, 59 in - the count barely moves),
    # 791 -> **793** function stubs, 97 -> **94** storage. Text 1042736 -> 1066240 (+23504), and
    # **`.data` should jump a whole 32 KB block**: the read-only region ends at 0x800fe93c with
    # 0x16c4 bytes of slack, and this object's read-only content is 24862 bytes (text + `str1.1` +
    # `os_log`), so `.data` goes 0x80100000 -> **0x80108000**, `__bss_start` 0x80117b68 -> about
    # 0x8011fc60, and the image 1148528 -> about **1165176**.
    #
    # **The prediction, read off the bodies the way 285-287 were.** Two functions have to run before
    # `task_init` is reached, and both were scanned for stub calls in the linked image first:
    #
    #     init_task_ledgers      24 x ledger_entry_add, 11 x ledger_track_credit_only,
    #                            4 x ledger_set_callback, ledger_track_maximum,
    #                            ledger_template_create, ledger_template_complete, panic
    #                            - ALL real, and each of those was scanned in turn: no stub anywhere
    #     coalition_create_internal   9 calls, all real
    #
    # So `coalitions_init` completes and `task_init` - now real, because this object defines it -
    # begins. Its calls in address order, with statuses taken from the *stub list*:
    #
    #     1 lck_grp_attr_setdefault  real      2 lck_grp_init  real       3 lck_attr_setdefault  real
    #     4 lck_mtx_init  real                 5 lck_mtx_init  real       6 zinit  real
    #     7 zone_change  real                  8 task_watch_init  ***NEW STUB***  <- the stop
    #
    # `zinit` is called again here, and 287's measurement covers it: the only stub inside it is
    # `btlog_create`, behind the `zlog` boot-arg guard that this payload does not trip.
    #
    # Predicted report: `stub_hit=task_watch_init`, the caller being the `bl task_watch_init` in
    # `task_init`. **The offset was derived wrong and the build corrected it** - the first draft said
    # object offset 0x3fc (`task_init + 0x1b0`), read off a list of relocation addresses that
    # included non-call entries. The linked pair settles it: `task_init` at 0x800bf6d0 and
    # `bl task_watch_init` at 0x800bf77c, so the object offset is 0x2fc, the function offset is
    # **+0xac**, and the predicted caller is **0x800bf780**. This is 284's (a) again - an offset read
    # from the wrong frame - and it is the second time the build has caught one before the device
    # was touched, which is the point of writing predictions down first.
    #
    # **The build:** 887 undefined, 793 function stubs, 94 storage - all three exactly as predicted.
    # Text 1042736 -> **1067704**, image 1148528 -> **1181584**, `__bss_start` 0x80117b68 ->
    # **0x8011fbf8**, bss end 0x8014e188 -> 0x80156388, headroom 1794040 -> 1743992. The 32 KB jump
    # was right: `.data` moved 0x80100000 -> 0x80108000, and the image grew by 33056 bytes.
    #
    # **And then the run was silent - for the instrument's reason, a third time, by a mechanism
    # nothing had looked at since 281.** The log came back at exactly 294042 bytes, the silent-log
    # size, with no `stub_hit` and no `exception:` line. A checkpoint at `task_init` was silent too,
    # and a checkpoint at `machine_init` - which 286 and 287 both reported from *past* - was silent
    # as well, which is what said the fault was not the boot's.
    #
    # It is 282 again, reached a different way. `cpu.c:570-580` writes to
    # `gPhysBase + (&ResetHandlerData.cpu_data_entries - &ExceptionLowVectorsBase)` and the same with
    # `boot_args`; 281 measured that difference as 0x2404/0x2408 and **`verify_pad` compared against
    # those two literals ever since**, while the difference itself is not a constant - it spans the
    # *generated stub object*, whose size grows with every step of this walk. Read out of this
    # image's own instruction stream, at `cpu_machine_idle_init+0x1a8`:
    #
    #     80004294: movw r2, #0xb494 ; movt r2, #0x800e   ->  r2 = ResetHandlerData  = 0x800eb494
    #     800042a4: sub  r5, r2, r5                        r5 = ExceptionLowVectorsBase = 0x800e8fec
    #                                                      -> r5 = the difference = **0x24a8**
    #     800042b4: add  r2, r1, #8    (r1 = gPhysBase)    -> boot_args       target = 0x800024b0
    #     800042f0: add  r2, r1, #4                        -> cpu_data_entries target = 0x800024ac
    #
    # The gap of 0x24a8 is 152 generated stub bodies at 0x18 bytes apart, sitting between
    # `ExceptionLowVectorsBase` (0x800e8fec) and `ResetHandlerData` (0x800eb494) in the linked `.text`
    # - so the difference grows as this walk adds objects. **The two writes were landing at
    # 0x800024AC/0x800024B0, 0x58 bytes past the end of the 128-byte pad, back inside
    # `entry_epilogue`'s own code**, and `verify_pad` passed every build because it was checking
    # 0x2404/0x2408. Every report was silent for 282's reason; the boot may have run any distance.
    #
    # **The fix, in two parts, both of them about the value having one definition.** The pad is 512
    # bytes now (`b 1f` over 127 NOPs), and `verify_pad` **derives the two addresses from the linked
    # image** - `ResetHandlerData` and `ExceptionLowVectorsBase` at the struct's own field offsets,
    # 4 and 8 - instead of comparing against anything written down. Every build now prints them:
    #
    #     entry_skip_pad at 0x800023d4 branches over 512 bytes to 0x800025d4
    #     XNU writes 0x800024ac and 0x800024b0 (ResetHandlerData - ExceptionLowVectorsBase = 0x24a8),
    #     and both land inside what it skips
    #
    # **The run, with the instrument working again:**
    #
    #     stub_hit=task_watch_init        xnu_entry_stub_caller=0x800bf900
    #
    # which is `task_init + 0xb0` - `task_init` links at 0x800bf850 in the rebuilt image, and the
    # 0x180 that 0x800bf900 sits past the pre-fix 0x800bf780 is exactly the 384 bytes the wider pad
    # added above it. So the prediction held where it was a prediction, and the five calls before the
    # stop - the two `lck_mtx_init`s, `zinit` (again, with 287's measurement covering its one stub),
    # `zone_change` and the four `lck_*` setup calls - all returned.
    OSFMK_KERN_TASK_OBJ=${STAGE90_ENTRY_OSFMK_KERN_TASK_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_task.o}
    # 287: `init_task_ledgers`, and the step where the 16 KB boundary finally moves
    #
    # **The 286 run reported** `stub_hit=coalitions_init` at `kernel_bootstrap+0x1a8`. The object that
    # defines it is `osfmk/kern/coalition.c`, `osfmk_kern_coalition.o` - and `coalitions_init` is one
    # of the two functions in this walk whose body is worth reading *completely* before the build,
    # because two of its calls are the kind that decide everything:
    #
    #     zinit(&coalition_zone, 0x2c, 0, "coalition")       real, but 0x97c bytes long
    #     zone_change                                        real, 0 calls of its own
    #     PE_parse_boot_argn x2                              real
    #     lck_grp_attr_setdefault / lck_grp_init / lck_attr_setdefault / lck_mtx_init   all real
    #     init_task_ledgers                                  ***STUB***  <- the stop
    #     coalition_create_internal x4                       real (defined in this object)
    #     panic                                                              real
    #
    # `zinit` is the one that had to be read rather than assumed, and this is the third step in a row
    # where the answer came from the *boot args*. `zinit` is 0x97c bytes - three times the size the
    # name suggests - because GCC inlines its zone-logging setup into it, and that setup contains the
    # **only** stub call in the whole of `zinit`:
    #
    #     8006e550: bl btlog_create
    #
    # `btlog_create` is a stub in this image, so if the run reached it the frontier would be there
    # instead. It does not. Every `bl` in `zinit`'s extent, with its own status:
    #
    #     real  lck_spin_lock x2   strcmp   lck_attr_setdefault   lck_mtx_init_ext
    #           lck_spin_unlock x3  panic x2  strlen x2  _consume_printf_args x3
    #           kmem_alloc_kobject  __bzero   strlcpy   snprintf   PE_parse_boot_argn x3
    #     STUB  btlog_create x1
    #
    # and the code that guards it is `zalloc.c:2259-2360`, which is entered on every `zinit` but
    # reaches the btlog loop only when `log_records_init == FALSE && zone_logging_enabled == TRUE`.
    # `zone_logging_enabled` is set by `track_this_zone(z->zone_name, zone_name_to_log)` where
    # `zone_name_to_log` comes from `PE_parse_boot_argn("zlog1".."zlog10")` and then
    # `PE_parse_boot_argn("zlog")` - and this payload's boot-args line contains no `zlog` of any
    # spelling (`zinit` does make those eleven `PE_parse_boot_argn` calls, and they are three of the
    # three the census above counts: two for `zlog`-family lookups, one elsewhere). So the loop is
    # skipped, `btlog_create` is never called, and `zinit` returns.
    #
    # **The other thing this step is for: the 16 KB boundary finally moves.** Experiment 284 found
    # that the entry image's `.bin` ends at the end of `__DATA,__data`, that `.data` is placed
    # 16 KB-aligned after the read-only region, and that within the slack a step's text growth is
    # invisible in the image size. The slack was 0x1be4 bytes after 285 and this object carries
    # **11312 bytes of text**, so this step crosses. `.data` should move from 0x800fc000 to
    # 0x80100000, and with it `__bss_start` and the whole of `.bss`: the image should jump by a full
    # 16 KB block plus the 8 bytes of `.data` this object adds - the first time in this sequence that
    # a prediction of that shape has had to be made, and the reason `text size` and `__bss_start` are
    # the numbers to read.
    #
    # **The object, measured.** 11312 bytes of text, 8 of data, 332 of bss, 72 of rodata, 881 of
    # `rodata.str1.1`, 47 definitions and 44 references. It resolves **13**, every one a function:
    #
    #     coalition_get_page_count  coalition_get_pid_list  coalition_id     coalition_is_leader
    #     coalition_is_privileged   coalition_is_reaped     coalition_is_terminated
    #     coalition_iterate_stackshot   coalitions_init     coalition_term_requested
    #     coalition_type            kdp_coalition_get_leader
    #     task_coalition_update_gpu_stats
    #
    # and adds **5**: four functions and one storage, all from objects this build has compiled:
    #
    #     coalition_notification   mach_coalition_notification_user.o  T 0x64
    #     init_task_ledgers        osfmk_kern_task.o                  T 0x5bc
    #     task_cpu_ptime           osfmk_kern_task.o                  T 0xc
    #     task_energy              osfmk_kern_task.o                  T 0x8c
    #     task_ledger_template     osfmk_kern_task.o                  B 4
    #
    # Note that four of the five come from `osfmk_kern_task.o` and **the storage one is a pointer**
    # (`task_ledger_template`), which is zero here for the same reason `clock_count` was in 285: it
    # is a stand-in, and nothing in this image has ever set it.
    #
    # **Predicted build deltas:** 896 -> **888** undefined (13 out, 5 in), 800 -> **791** function
    # stubs, 96 -> **97** storage, text 1031088 -> **1042400** (+11312), image 1132136 -> about
    # **1148544** (+16 KB and 8), and `__bss_start` 0x80113b60 -> about 0x80117b68.
    #
    # Predicted report: `stub_hit=init_task_ledgers`, `xnu_entry_stub_caller` = the return address of
    # the `bl init_task_ledgers` in `coalitions_init` - object offset 0x1638, so the caller is
    # `coalitions_init + 0xe8` once the linked address is known.
    #
    # **The build and the run.** 888 undefined, 791 function stubs, 97 storage, text 1031088 ->
    # **1042736**, image 1132136 -> **1148528**, `__bss_start` 0x80113b60 -> **0x80117b68** - the
    # boundary prediction was exact to the byte, including the 16 KB jump, and the counts were exact
    # to the unit. bss end 0x8014a008 -> 0x8014e188, headroom 1794040 -> 1777272. `coalitions_init`
    # links at 0x800bdda4, `init_task_ledgers`'s stub at 0x800e383c, and the run:
    #
    #     stub_hit=init_task_ledgers        xnu_entry_stub_caller=0x800bde8c
    #
    # `0x800bde8c` is `coalitions_init + 0xe8`, the instruction after the `bl` at +0xe4 - so `zinit`
    # was executed for the first time in this boot and returned, with `btlog_create` never called,
    # and `zone_change`, both `PE_parse_boot_argn` calls, `lck_grp_attr_setdefault`, `lck_grp_init`,
    # `lck_attr_setdefault` and `lck_mtx_init` all returned after it.
    OSFMK_KERN_COALITION_OBJ=${STAGE90_ENTRY_OSFMK_KERN_COALITION_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_coalition.o}
    # 286: `coalitions_init`, and the first prediction that is a *chain* rather than a name
    #
    # **The 285 run reported** `stub_hit=ntp_init` at `clock_config+0x6c`. Linking `ntptime.o` makes
    # that call real, so the frontier moves again - and this time the prediction has to follow four
    # functions past the stop, because the next eight calls in `kernel_bootstrap` include three that
    # are already real. Written out in order, with each one's disposition measured in the image:
    #
    #     ntp_init        (this step)  calls lck_grp_attr_alloc_init, lck_grp_alloc_init,
    #                                  lck_attr_alloc_init, lck_spin_alloc_init,
    #                                  nanoseconds_to_absolutetime, timer_call_setup - ALL real
    #     clock_config                 after ntp_init, one tail call: nanoseconds_to_absolutetime,
    #                                  real -> completes
    #     machine_init                 `is_clock_configured = TRUE; if (debug_enabled) pmap_map_globals();`
    #                                  and `debug_enabled` is 0 - see below -> returns
    #     clock_init                   `b clock_oldinit`, real since 285; clock_oldinit references
    #                                  only `clock_count` and `clock_list` and makes no calls at all
    #                                  -> completes
    #     ledger_init                  5 instructions, tail call `b lck_grp_init`; lck_grp_init calls
    #                                  __bzero, strlcpy and lck_mtx_lock, all real -> completes
    #     coalitions_init              ***STUB***  <- the stop
    #
    # **`debug_enabled` is the hinge of the whole prediction**, so it is measured rather than
    # assumed. It is `SECURITY_READ_ONLY_SPECIAL_SECTION(volatile uint32_t, "__TEXT,__const")
    # debug_enabled = FALSE;` (pe_init.c:39) and the only writer in the tree is pe_init.c:350, which
    # copies it out of the device tree property `/chosen/debug-enabled`. The payload's synthetic
    # `/chosen` carries name, boot-args, stdout-path, ram-console-reg, random-seed and (when the
    # consistent-debug switch is on) consistent-debug-root, and no `debug-enabled`. `PE_init_platform`
    # is real in this image, so the lookup does run and finds nothing. And the linked word agrees:
    # `debug_enabled` is at 0x800fa640, the first four bytes of `__TEXT,__const`, and the image
    # contains 0x00000000 there. So `machine_init` returns at its fourth instruction.
    #
    # The one thing that would falsify the prediction cheaply is `pmap_map_globals` being reached -
    # it is real, and it has no `bl` in its first 0x60 bytes - so a report naming it would mean
    # `debug_enabled` was set somewhere this reading did not find, which is a measurement about the
    # device tree rather than about this step.
    #
    # **The object, measured.** 3068 bytes of text, 12 of data, 172 of bss, 35 of `rodata.str1.1`,
    # 8 definitions and 24 references. It resolves **2** - `ntp_init` and `ntp_update_second`, both
    # functions - and adds **2**, both functions: `mac_system_check_settime`
    # (security_mac_system.o, 0xf0) and `nanotime` (bsd_kern_kern_time.o, 0x2c). The other six
    # definitions (`adjtime`, `ntp_adjtime`, `ntp_get_freq`, `ntp_gettime`, `time_esterror`,
    # `time_status`) are names nothing has referenced yet; `time_esterror` and `time_status` are the
    # object's own `.data`, 4 bytes each, which is why the image should grow by 12 and not by 0.
    #
    # **Predicted build deltas:** 896 -> **896** undefined (2 out, 2 in), 800 -> **800** function
    # stubs, 96 -> **96** storage, text 1027984 -> 1031052 (+3068), image 1132128 -> **1132140**.
    # This is the first step in a while whose *counts* are all predicted to be unchanged, which makes
    # the run the only evidence that the object landed at all - another reason to have the caller
    # address predicted to the byte.
    #
    # Predicted report: `stub_hit=coalitions_init`, `xnu_entry_stub_caller` = **0x8000e228**, the
    # return address of `bl coalitions_init` at 0x8000e224 in `kernel_bootstrap`.
    #
    # **The build and the run.** 896 undefined, 800 function stubs, 96 storage, `__bss_start`
    # 0x80113b58 -> 0x80113b60, bss end 0x80149f48 -> 0x8014a008, headroom 1794232 -> 1794040 - all
    # three counts and both invariants exactly as predicted. Text 1027984 -> **1031088** and image
    # 1132128 -> **1132136**; the arithmetic said 1031052 and 1132140, so text came out 36 bytes
    # larger and the image 4 bytes smaller than the sum of the object's sections suggested. And:
    #
    #     stub_hit=coalitions_init        xnu_entry_stub_caller=0x8000e228
    #
    # **The chain held.** One run measured that `ntp_init` completes with all six of its lock and
    # timer calls real; that `clock_config` completes; that `machine_init` takes its `debug_enabled`
    # branch the way the linked word at 0x800fa640 said it would and returns without reaching
    # `pmap_map_globals`; that `clock_init`'s tail call into `clock_oldinit` returns; and that
    # `ledger_init`'s tail call into `lck_grp_init` returns. Seven functions, four of them named in
    # advance as things that would *not* stop the run, and the stop came exactly one call later.
    BSD_KERN_KERN_NTPTIME_OBJ=${STAGE90_ENTRY_BSD_KERN_KERN_NTPTIME_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/bsd_kern_kern_ntptime.o}
    # 285: `ntp_init`, and a step that is predicted to overshoot the symbol it links
    #
    # **The 284 run reported** `stub_hit=clock_oldconfig` at `clock_config+0x68`. The object that
    # defines it is `osfmk/kern/clock_oldops.c`, `osfmk_kern_clock_oldops.o` - and the interesting
    # thing about this step is that linking it should *not* move the frontier to the symbol it
    # defines.
    #
    # **The object, measured.** 3128 bytes of text, 52 of rodata, 29 of `rodata.str1.1`, 172 of bss,
    # 32 definitions and 25 references. It resolves **10**, every one a function (no storage, so no
    # size to get wrong), four of which are the Mach clock-server entry points this object is
    # actually for:
    #
    #     clock_oldconfig   clock_oldinit        clock_alarm          clock_get_attributes
    #     clock_get_time    clock_service_create clock_set_attributes clock_set_time
    #     host_get_clock_control                 host_get_clock_service
    #
    # and adds **6**: four functions (`clock_alarm_reply`, `ipc_clock_enable`, `ipc_clock_init`,
    # `port_name_to_clock`) and two *storage* variables - `clock_count` (D, 4) and `clock_list`
    # (D, 0x18=24), both from `osfmk_arm_conf.o`, which is not in this link. `clock_list` is 24 bytes
    # because it is `struct clock_list_entry clock_list[CLOCK_COUNT]` with CLOCK_COUNT 2 (two 12-byte
    # entries: a pointer and a function pointer), and its size comes from that definition.
    #
    # **The prediction, and it is the whole point of the step: the run should stop on `ntp_init`, not
    # on anything in this object.** `clock_oldconfig`'s body - 0xf4 to 0x1a8, 0xb4 bytes - calls
    # exactly three things directly:
    #
    #     +0x14  arm_usimple_lock_init    real
    #     +0x2c  thread_call_setup        real (osfmk_kern_thread_call.o, linked long ago); its own
    #                                     body is a `bl __bzero` and a tail call, no stub
    #     +0x44  timer_call_setup         real (osfmk_kern_timer_call.o); its own body calls
    #                                     arm_usimple_lock_init, lck_spin_lock and
    #                                     lck_mtx_lock_spin_always, all real
    #
    # and then an *indirect* `blx r0` in a loop over `clock_list` - which this link walks zero times,
    # because it is guarded by `clock_count`, and `clock_count` arrives as a storage stand-in: four
    # zero bytes. `ldr r0, [r5]; cmp r0, #1; blt +0xa4` skips the whole loop. So `clock_oldconfig`
    # runs to completion, returns to `clock_config`, and `clock_config`'s next call is `ntp_init` -
    # which 284 added as a stub and this object does not define.
    #
    # Predicted report: `stub_hit=ntp_init`, `xnu_entry_stub_caller` = **`clock_config + 0x6c`**
    # (`bl ntp_init` is at +0x68, so the return address is 0x800b97e8). The caveats, stated because
    # they are real: `thread_call_setup` and `timer_call_setup` are real but have **never been
    # executed** by this image, so a stop inside one of them would be this step's measurement rather
    # than a defect; and `clock_oldinit` - the other symbol this object resolves - is what
    # `clock_config`'s `clock_init` neighbour tail-calls, so it is not on this path at all.
    #
    # **Predicted build deltas:** 900 -> **896** undefined (10 out, 6 in), 806 -> **800** function
    # stubs, 94 -> **96** storage, text 1025040 -> 1028168 (+3128). The image itself should grow by
    # almost nothing: the read-only region ends at 0x800fa41c with 0x1be4 bytes of slack, and this
    # object's 3209 bytes of read-only content fit inside it, so `.data` - and therefore `.bin`,
    # which ends at `__DATA,__data` - should not move at all. What moves is `.bss`: `+172` for the
    # object's own `alarm_lock`, `alarm_zone`, `alrmdone`, ... and `+28` for the two new stand-ins,
    # so bss end 0x80149e08 -> about 0x80149ed0, and `__bss_start` unchanged at 0x80113b58.
    #
    # **The build, and the run.** `896` undefined, `800` function stubs, `96` storage, `__bss_start`
    # unchanged at 0x80113b58, bss end 0x80149f48, headroom 1794552 -> 1794232 - all three counts and
    # both invariants exactly as predicted; text 1025040 -> **1027984**, which is 184 bytes less than
    # the arithmetic above, the object's `.text` being 3128 and the rest of its read-only content
    # landing partly in sections that were already aligned. `clock_oldconfig` links at `0x800bb110`,
    # `ntp_init`'s stub at `0x800e16a4`, so the predicted caller is `0x800b97e8`. And the run:
    #
    #     stub_hit=ntp_init        xnu_entry_stub_caller=0x800b97e8
    #
    # **The prediction held, and this is the first time a step's report came from a *later* call in
    # the function that stopped the previous step than the symbol it linked.** The run measured that
    # `clock_oldconfig` completes: `arm_usimple_lock_init`, `thread_call_setup` (with its `__bzero`)
    # and `timer_call_setup` (with `arm_usimple_lock_init`, `lck_spin_lock` and
    # `lck_mtx_lock_spin_always` inside it) all execute and return, and the `clock_list` walk is
    # taken zero times because `clock_count` is a storage stand-in of four zero bytes. So two of the
    # 25 references this object added to the image - the two that are *storage* - are the ones that
    # decided the shape of the step, by being zero.
    OSFMK_KERN_CLOCK_OLDOPS_OBJ=${STAGE90_ENTRY_OSFMK_KERN_CLOCK_OLDOPS_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_clock_oldops.o}
    # 284: `clock_oldconfig`, and the first frontier that is *inside* the function it links
    #
    # **The 283 run reported** `stub_hit=clock_config` with `xnu_entry_stub_caller=0x800076c4`, which
    # is `machine_init + 0xc`; `machine_init` is the first of the twelve init functions
    # `kernel_bootstrap` calls after `ipc_init` returns, and `clock_config` is the first thing it
    # calls. So the object to link is the one that defines `clock_config` - `osfmk/kern/clock.c`,
    # `osfmk_kern_clock.o` - and this is the first step in the walk where the symbol the run stopped
    # at is defined in an object whose *body* still needs something: `clock_config` is where the
    # frontier resumes, not where it ends.
    #
    # **The object, measured.** `osfmk_kern_clock.o` is **6316 bytes of text, 4 of data, 176 of bss,
    # 12 of rodata, 48 definitions and 41 references**. Against this image it resolves **17** -
    # fifteen function stubs and two *storage* stand-ins, and for those two the check that matters is
    # that the sizes agree byte for byte, which they do (`hz_tick_interval` is `D` size 4 in the
    # object and 0x4 in the generated stand-in; `mach_absolutetime_asleep` is `B` size 8 and 0x8):
    #
    #     absolutetime_to_continuoustime      clock_absolutetime_interval_to_deadline
    #     clock_config                        clock_continuoustime_interval_to_deadline
    #     clock_deadline_for_periodic_event   clock_get_calendar_microtime
    #     clock_get_calendar_nanotime         clock_get_uptime
    #     clock_init                          clock_interval_to_deadline
    #     clock_timebase_init                 continuoustime_to_absolutetime
    #     delay                               mach_continuous_approximate_time
    #     mach_continuous_time                hz_tick_interval (D, 4)
    #     mach_absolutetime_asleep (B, 8)
    #
    # and adds **8** new obligations, every one of them a *function* this project has already
    # compiled, so each stand-in's size still comes from a definition rather than from a guess:
    #
    #     clock_oldconfig   clock_oldinit                        (osfmk_kern_clock_oldops.o)
    #     ntp_init          ntp_update_second                    (bsd_kern_kern_ntptime.o)
    #     commpage_update_boottime        commpage_update_mach_continuous_time
    #                                        (osfmk_arm_commpage_commpage.o)
    #     PEGetUTCTimeOfDay               PESetUTCTimeOfDay
    #                                        (iokit_Kernel_IOPlatformExpert.o)
    #
    # The remaining 24 of the object's 48 definitions are names nothing in this image has referenced
    # yet (`clock_gettimeofday`, `delay_for_interval`, `clock_update_calendar`, `clock_lock`, ...),
    # and none of them collides: each is defined exactly once in the pool - by this object - and the
    # pass-1 image does not define it. So the step introduces no duplicate definition; it only makes
    # 24 more names available to whatever comes next.
    #
    # **Prediction: a report, with the name predicted as well, because the body was read before the
    # build.** `clock_config` is not a wrapper. Its calls in *function-relative* order, each checked
    # against this image's definitions. The one trap in reading the object this way is that the
    # object's `.text` begins with another function - `kdp_clock_is_locked`, 0xc bytes - so an
    # object-section offset is a function offset plus 0xc, and the first version of this table was
    # written with the raw offsets and was wrong by exactly that much:
    #
    #     +0x10  arm_usimple_lock_init         real (osfmk_arm_locks_arm.o, linked long ago)
    #     +0x14  lck_grp_attr_alloc_init       real (osfmk_kern_locks.o)
    #     +0x30  lck_grp_alloc_init            real
    #     +0x40  lck_attr_alloc_init           real
    #     +0x60  lck_mtx_init                  real
    #     +0x64  clock_oldconfig              *NEW STUB*  <- the stop, return address +0x68
    #     +0x68  ntp_init                     *NEW STUB*
    #     +0x84  nanoseconds_to_absolutetime   real (tail call, osfmk_arm_rtclock.o)
    #
    # Five of the eight calls in `clock_config` are things this image has had for a long time, which
    # is why the stop is at the sixth. The run should therefore report `stub_hit=clock_oldconfig`
    # with `xnu_entry_stub_caller` = `clock_config + 0x68`, from inside `machine_init`, with no
    # `exception:` line. If it stops on `ntp_init` instead, that is not a wrong object: it would mean
    # `clock_oldconfig` came back, which is a measurement of the same step.
    #
    # **The build, which is where the offsets above were checked:** 909 -> **900** undefined,
    # 813 -> **806** function stubs, 96 -> **94** storage - all three exactly as predicted - text
    # 1019088 -> 1025040, `__bss_start` 0x80113b50 -> 0x80113b58, bss end 0x80149dc8 -> 0x80149e08,
    # headroom 1794616 -> 1794552 bytes. `clock_config` links at **0x800b977c** and
    # `clock_oldconfig`'s stub at **0x800de9d4**, so the predicted caller is **0x800b97e4**, and the
    # `bl` at 0x800b97e0 really is the sixth of the eight calls in address order.
    #
    # One thing did *not* follow the naive arithmetic, and it is worth writing down because it will
    # mislead again: the image grew by **8 bytes**, not by the 6316 the object's text suggests. The
    # entry image's `.bin` ends at the end of `__DATA,__data`, and `.data` is placed 16 KB-aligned
    # after the read-only region (0x800fc000 both before and after this step). The read-only region
    # now ends at 0x800fa41c, so there are 0x1be4 bytes of slack before `.data` has to move, and
    # within that slack the image's *size* grows only by what `.data` itself grew - 4 bytes for
    # `hz_tick_interval` here, plus 4 of alignment. A step that crosses the boundary shows a jump of
    # 16 KB and a step that does not shows almost nothing; `text size` and `__bss_start` are the
    # numbers that move monotonically, and they are the ones to predict with. 0x1be4 is also the
    # distance to the next step that will have to be read carefully.
    OSFMK_KERN_CLOCK_OBJ=${STAGE90_ENTRY_OSFMK_KERN_CLOCK_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_clock.o}
    # 283: `kernel_set_special_port`, and the frontier 280 first predicted - read at last
    #
    # **The plain run reported.** With the pad no longer executed - no checkpoint, no `--wrap`, just
    # the image - the run came back at 301124 bytes with
    #
    #     stub_hit=kernel_set_special_port        xnu_entry_stub_caller=0x800b7648
    #
    # and 0x800b7648 is the return address of the `bl kernel_set_special_port` at 0x800b7644, inside
    # `ipc_host_init` (`0x800b75d0`, linked in 277). This is the symbol the 280-era ledger named from
    # reading `ipc_host.c` - "`ipc_host_init` runs `bl ipc_kobject_set` (+0x5c) and
    # `bl ipc_port_make_send` (+0x64), both real and neither ever executed, and then
    # `bl kernel_set_special_port` (+0x74), which **is** a stub - so the run has to report from
    # there" - and the run it was written for could not produce it, because that run's silence was
    # the instrument's. Four experiments later the same prediction is measured: the run executes
    # `ipc_kobject_set` and `ipc_port_make_send` (both real, both new since 277) and stops three
    # instructions later on the one symbol in the body that is still a stub.
    #
    # The call is the `HOST_SECURITY_PORT` one: the disassembly of `ipc_host_init` is a three-fold
    # repetition of `ipc_port_alloc_special` / `ipc_kobject_set` / `ipc_port_make_send` /
    # `kernel_set_special_port`, and at the reporting site `r1` is `mov r1, #0` - `HOST_SECURITY_PORT`
    # - which matches `ipc_host.c:113`, the first of the three.
    #
    # **What makes this step worth predicting carefully: `kernel_set_special_port` is the only stub
    # in the whole reachable body.** Measured rather than argued, by listing every `bl` in
    # `ipc_host_init` (16 calls) and in `ipc_init` (5) and checking each target against the image's
    # own undefined list:
    #
    #     ipc_host_init      lck_mtx_init, ipc_port_alloc_special, ipc_kobject_set,
    #                        ipc_port_make_send  x3, kernel_set_special_port x3  <- the only stubs
    #     ipc_init           kmem_suballoc x2 (panic-guarded), ipc_host_init   <- ipc_host_init is last
    #     kernel_bootstrap   every one of its calls is real, 0 of 46 stubs
    #
    # So this step cannot stop anywhere in `ipc_host_init`, `ipc_init` or `kernel_bootstrap`: all
    # three run to completion once `host.o` is linked, and the next report must come from *deeper* -
    # from inside one of the init functions `kernel_bootstrap` calls after `ipc_init` returns
    # (`mapping_free_prime`, `machine_init`, `clock_init`, `ledger_init`, `coalitions_init`,
    # `task_init`, `thread_init`, `atm_init`, `mach_init_activity_id`, `bank_init`,
    # `ipc_pthread_priority_init`, `corpses_init`) or from something they reach. None of those twelve
    # calls a stub directly - measured the same way - so the next frontier is *transitive*, which is
    # the first time in this walk that it is.
    #
    # **The object, measured.** `osfmk_kern_host.o` is 4920 bytes of text, 48 of data, 452 of bss, 26
    # definitions and 60 references. Against this image it resolves **19** - eighteen function stubs
    # plus `realhost`, which is a *storage* stand-in of 0x194 bytes whose real definition this object
    # carries:
    #
    #     host_get_io_master   host_get_special_port      host_info          host_kernel_version
    #     host_page_size       host_priv_self             host_priv_statistics  host_processor_info
    #     host_processors      host_processor_set_priv    host_processor_sets   host_self
    #     host_set_atm_diagnostic_flag   host_set_multiuser_config_flags
    #     host_set_special_port          host_statistics     host_statistics64
    #     kernel_set_special_port        realhost
    #
    # and adds **7** new obligations, each a function or variable this build has already compiled, so
    # each stand-in's size still comes from its own definition rather than from a guess - the rule
    # since 244:
    #
    #     atm_set_diagnostic_config   avenrun   commpage_update_multiuser_config
    #     dead_task_statistics        mach_factor   mac_task_check_set_host_special_port
    #     vm_purgeable_stats
    #
    # `realhost` is the interesting one. 277 sized the stand-in at 0x194 bytes and `ipc_host_init`
    # wrote every byte of it - the run measured that - and this step replaces the stand-in with the
    # real definition, so the two sizes can now be compared for the first time. The three
    # `kernel_set_special_port` calls go on to write `realhost.special[id] = port` at offsets 0x94,
    # 0x98 and 0x9c of it (`host.c:896`), which is inside the part 277's stand-in also covered.
    #
    # **Prediction: a report, and not a silence.** The instrument is fixed and the last three runs
    # before this one - `CleanPoC_DcacheRegion`, `machine_startup`, and the plain run - each reported
    # from further along. The three `kernel_set_special_port` calls are the only stub calls in
    # `ipc_host_init`, so all three return and the function completes; `ipc_init` completes with it,
    # since `ipc_host_init` is its last call; and `kernel_bootstrap` has no stubs at all, so the run
    # leaves the whole of XNU's `kernel_bootstrap` behind and reports from inside `task_init`,
    # `thread_init`, `machine_init`, `clock_init` or whatever they reach. The *name* is not predicted
    # - it is the first stub on a transitive path - but the shape is: `stub_hit=<name>` with
    # `xnu_entry_stub_caller` inside one of those functions or their callees, and no `exception:`
    # line. A silence here would mean a real hang at the first stub-free stretch of the boot, which
    # would be a result in its own right and would be the first trustworthy one of those.
    OSFMK_KERN_HOST_OBJ=${STAGE90_ENTRY_OSFMK_KERN_HOST_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_host.o}
    # 280: `klist_init`, and a step that needed a size decision before it could be built.
    # 279's stop was `klist_init`, and the object that defines it is `bsd/kern/kern_event.c`
    # (manifest:34), `bsd_kern_kern_event.o` - the largest step this walk has taken by a wide margin:
    # **43084 bytes of text, 336 of data, 136 of bss, 1349 of strings, 448 of rodata, 229 definitions
    # and 165 references**. It is the BSD kqueue/kevent implementation, and `klist_init` is the
    # smallest thing in it:
    #
    #     4bc8: mov r1, #0
    #     sbcc: str r1, [r0]
    #     4bd0: bx lr
    #
    # - `SLIST_INIT(list)`, three instructions, no call, no `lr`. So it completes and returns.
    #
    # **The step needed one decision before it could be built, and the build is what asked for it.**
    # Of the 165 references, 75 are already real, 21 already have stand-ins, and 69 are new - and
    # **68 of the 69 have a definition in this project's object pool** (each with a measured size, so
    # the generator has nothing to guess). The 69th, `bpfread_filtops`, has no definition anywhere in
    # the pool: `bsd/net/bpf.c` is `optional bpfilter` in `bsd/conf/files:192`, a flag the device table
    # this project builds from does not select, so the file is not in the manifest for this
    # *configuration* and not one of the pool's ~700 objects defines the name. That is the case the
    # storage generator was built to fail on rather than paper over ("add a hand-written definition
    # with the size taken from its source"), and the definition is in `entry_stubs.c` with the size
    # argued two ways: fifteen sibling filter tables in the pool all measure 0x28, and
    # `struct filterops` (`bsd/sys/event.h:939-951`) is two `bool`s and nine pointers = 40 on armv7.
    # Nothing on this path dereferences it - it is one element of a table only `kern_event_init`
    # indexes - and this walk stops far short of that.
    #
    # **Prediction: `stub_hit=kernel_set_special_port`, with the caller at `ipc_host_init+0x78`.**
    # This is the point where the step stops being a step in `kern_event.c` and becomes a step back in
    # `ipc_host_init`, and the whole path was walked by hand before the build:
    #
    #     ipc_host_init+0x2c   bl ipc_port_alloc_special      ; entered in 278, stops in this one
    #     ipc_port_alloc_special+0x90  bl ipc_mqueue_init     ; entered in 279
    #     ipc_mqueue_init+0x58 b  klist_init                  ; a tail call - the step's own name
    #     klist_init           mov/str/bx lr -> returns to `ipc_port_alloc_special+0x94`
    #     ipc_port_alloc_special+0x94  mov r0, r4 / pop {r4, r5, fp, pc}  -> ipc_host_init+0x30
    #     ipc_host_init+0x30   mov r4, r0 / cmp r0, #0 / bne +0x48        ; r0 is the port, non-null
    #     ipc_host_init+0x5c   bl ipc_kobject_set             ; REAL - ipc_kobject.o, linked in 266
    #     ipc_host_init+0x64   bl ipc_port_make_send          ; REAL - retired by 278, never yet run
    #     ipc_host_init+0x74   bl kernel_set_special_port     ; A STUB  <- the stop
    #     ipc_host_init+0x78   mov r2, r0                     ; the return address
    #
    # The two calls between are checked, not assumed: `ipc_kobject_set`'s body in this image calls
    # only `lck_spin_lock` and `lck_spin_unlock` (its four `b` targets - `mk_timer_port_destroy`,
    # `mach_destroy_memory_entry`, `host_notify_port_destroy` - are a *destroy* switch this call does
    # not take, `IKOT_HOST_SECURITY` being a set), and `ipc_port_make_send`'s calls only
    # `lck_spin_lock`, `OSCompareAndSwap` and `lck_spin_unlock`. All five are real, so the first stub
    # the run can reach is `kernel_set_special_port`.
    #
    # The caller key is `ipc_host_init+0x78` - an address inside the function the `bl` is in, the same
    # shape 276/277/278 had, and the third different value this walk has reported from
    # `ipc_host_init`: 277 measured `+0x30`, and this is `+0x78`, because two whole functions ran in
    # between. `kernel_set_special_port` is `osfmk/kern/host.c`, and it is one of the eight names 277
    # added as a stub - so this step retires an obligation this walk created three experiments ago.
    BSD_KERN_KERN_EVENT_OBJ=${STAGE90_ENTRY_BSD_KERN_KERN_EVENT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/bsd_kern_kern_event.o}
    #
    # **280 did not measure that. The run stopped nowhere at all.** The build is exactly what the
    # prediction block describes - **10 resolved, 69 added** (863 -> 921 undefined, 789 -> 827
    # function stubs, storage 74 -> 94; 68 of the 69 are generated from the object, 48 as function
    # stubs and 20 as storage stand-ins, and the 69th is `bpfread_filtops`), text 968100 -> 1013620
    # (+46528 = 0xB5C0), image bytes 1082488 -> 1115688 (one 16 KB alignment step), `.bss`
    # `0x8010fb50` .. `0x80145c88`, headroom 1845944 -> 1811320, `args` one page above the image at
    # `0x80147000`, `topOfKernelData` unmoved at `0x80300000`. The ten retired are `klist_init`
    # itself plus nine of the ten `knote_*` / `kev_*` / `waitq_set__CALLING_PREPOST_HOOK__` names
    # 279's own additions had obliged.
    #
    # The linked path is the prediction's, instruction for instruction: `ipc_host_init` is real at
    # `0x800b7550` with `800b75c4 bl 800dd29c <kernel_set_special_port>` at `+0x74` and the return
    # address at `+0x78`; `kernel_set_special_port` is a healthy generated stub, **24 bytes** at
    # `0x800dd29c`; `klist_init` is real, **12 bytes** at `0x800c0788`, `mov r1, #0 / str r1, [r0] /
    # bx lr`; `ipc_mqueue_init` is 0x5c bytes with its tail call as its last instruction; and the
    # two calls in between (`ipc_kobject_set`, `ipc_port_make_send`) reach only real code.
    #
    # What the device did: **nothing.** The payload's whole ladder completes and is byte-identical in
    # *structure* to the 279 control's, address for address except the 0x8000 the payload itself grew
    # - so the jump is taken. The log's last line is `stage90 xnu_entry: jumping to XNU's _start`,
    # and after it the entry image writes **not one byte**: `stub_hit` count 0, no `exception:
    # <vector>` line, and none of the epilogue's own records (`xnu_entry_csselr_before`,
    # `xnu_entry_ccsidr_l1`, `xnu_entry_kv_words`) that 279's run carries. Two runs of the same image
    # and a third of the checkpoint build below, all three silent.
    #
    # **The 279 configuration, rebuilt from this same ledger in the same session and run on the same
    # device through the same `run_and_capture.sh`, reached the frontier exactly**: `stub_hit=klist_init`
    # at log line 3961 with `xnu_entry_stub_caller_v=0x800ba300` = `ipc_port_alloc_special+0x94`,
    # `kv_written=0x5b`, `kv_in_dram=0x7f`, `kv_dropped=0x0`, `why_byte=0x61`, zero aborts. So the
    # device, the payload, the boot flow, the capture and the entry image's own report path are all
    # healthy *right now*, and the image is the single failing variable.
    #
    # **The location of the failure, as far as it is measured.** `STAGE90_ENTRY_CHECKPOINT=klist_init`
    # - the instrument `entry_checkpoint.c` carries, which links `--wrap=klist_init` so that the very
    # instruction 279's run stopped on becomes a terminal stop of the same kind - was **also silent**.
    # The host side verified the wrap took effect (`ipc_mqueue_init+0x58` re-pointed to
    # `__wrap_klist_init`). A checkpoint at `klist_init` needs no new code to run: it stops at exactly
    # the point 279's run reported from. So in this image the run does not reach that point, or the
    # reporting path that reported it in 279's image does not report in this one - and the instrument
    # was built to say which of those two, so its own reading is the second: *the frontier is not
    # where the problem is*.
    #
    # **Host-side rule-outs, completed before that run.** The disassembly of `[0x80000000,
    # 0x800bbbc0)` - `_start`, `arm_init`, `arm_vm_init` and the whole IPC allocation path - is the
    # 279 image's once `movw`/`movt` pairs are resolved as single addresses: the 1761 raw halfword
    # differences are every one a relocated address or a shifted literal pool, and no instruction
    # differs. Both calls between the stop and the port are real. All 94 storage stand-ins match a
    # pool definition's size exactly and `bpfread_filtops` is the only hand-sized one, and nothing on
    # this path dereferences it. `entry_epilogue` and `entry_probe_dump_kv_words` are off the
    # `entry_stub_hit` path; `g_hex` is outside `.bss`; and there is no size-dependent gate anywhere
    # before the first report.
    #
    # **And the layout, measured this time rather than argued.** A per-symbol address delta between
    # the two images (nm on both, 5685 names in common) gives the whole story and leaves nothing
    # unexplained: **2726 symbols are at the same address**, every one of them below `0x800bb000`;
    # from `0x800bb000` up the stub object is regenerated and shifts by `+0xabf8` / `+0xabe0`; `.data`
    # moves by `+0x8000` because it snaps to a 16 KB boundary and the 0x3A54 bytes of padding below it
    # in 279 became 0x484, and its first 0x8000 bytes - `intstack` and `fiqstack`, 16 KB each, with
    # `intstack_top` at `.data + 0x8000` in **both** images - ride with it, so the boot stack is the
    # same size on the same relative address; `__DATA,__const` and `__DATA,__data` take `+0x8150`, and
    # `.bss` `+0x81c0`. `klist_init`'s stub at `0x800d298c` is simply gone, superseded by the real
    # definition at `0x800c0788`. The only "backwards" deltas in the whole table are
    # `__entry_data_size` and `__entry_data_filesize`, which are absolute linker symbols - values,
    # not addresses - and so are not addresses at all.
    #
    # **The instrument, and the one thing worth carrying forward from it.** `entry_checkpoint.c` plus
    # `STAGE90_ENTRY_CHECKPOINT=<symbol>` in this script: `--wrap` redirects every *reference* to a
    # symbol through a 24-byte wrapper that calls `entry_stub_hit`, so one named function becomes a
    # terminal stop of exactly the shape a missing symbol produces, at one symbol's cost instead of
    # five. It is unset by default and no stage image contains it. It is also the first probe in this
    # walk whose *negative* result is the load-bearing one, and its next use is to bisect from the
    # top of the ladder rather than from the frontier.
    #
    # **A correction owed to 279's own write-up.** Its committed layout paragraph says the 279-vs-280
    # diff "shows only 101 symbols moved". Re-measured, 2959 of the 5685 common symbols moved; 101 is
    # the count of *some* subset, not of the moved set. The *code*-identity conclusion 279 draws from
    # it survives, because that conclusion rests on the disassembly comparison, not on the count - but
    # the sentence as written is wrong and 279's `Reproduce` note is not a sound argument for it.
    #
    # 280 is therefore a step that is **built, predicted, instrumented and not finished**: the walk
    # stands one object short of the frontier it reached in 279, and the next move is not another
    # object. `docs/experiments/experiment-280-*.md` carries the whole record.
    #
    # ---------------------------------------------------------------- 280, continued: the bisect
    #
    # The checkpoint instrument (`entry_checkpoint.c`, `STAGE90_ENTRY_CHECKPOINT=<symbol>`) bisects
    # the silence. Three runs, all on the 280 configuration, all with `persistent_write_attempted`
    # clean in every place and `failure_mask=0x00000000` in every contract:
    #
    #   cpu_data_init   (step 1,  `arm_init+0x40`)  -> **reports**, `caller_v=0x80002fc8`
    #   arm_vm_init     (step 18, `arm_init+0x1d4`) -> **reports**, `caller_v=0x80003160`
    #   machine_startup (step 19, `arm_init+0x340`) -> **silent**
    #
    # The first two are what makes the third a measurement rather than another silence: the report
    # path works in this image (two independent reports) and `_start`, `arm_init` and everything the
    # ladder reaches before `arm_vm_init` all execute. So the silence is not at the frontier 279
    # stopped at, and it is not the instrument.
    #
    # The middle one has to be read carefully, and the careless reading is wrong: `--wrap` *replaces*
    # the function, so a checkpoint at `arm_vm_init` proves the run reaches the call site at
    # `8000315c` and says nothing at all about `arm_vm_init`'s 0x790 bytes. The interval the silence
    # lives in is therefore
    #
    #     [arm_init+0x1d8, arm_init+0x340)  =  arm_vm_init's body  +  the eighteen real calls after it
    #
    # and in 279 *both* halves ran: 279's run reported from `klist_init`, which is reached through
    # `machine_startup` -> `kernel_bootstrap` -> `ipc_init` -> `ipc_host_init` -> `ipc_port_alloc_
    # special` -> `ipc_mqueue_init`. Every one of those eighteen calls is real code below
    # `0x800bb000`, in the address band 279 and 280 are instruction-identical across, so what kills
    # the 280 run is a layout-dependent difference in *data*, not in code - and the first question is
    # which half of the interval it is in.
    #
    # **Prediction, before the device is touched. `STAGE90_ENTRY_CHECKPOINT=patch_low_glo_static_
    # region` reports, with the caller at `0x80003160`, `cp_arg0=0x80300000` and `cp_arg1=0x00006000`.**
    #
    # `patch_low_glo_static_region` is the last thing `arm_vm_init` does and the *only* thing in the
    # image that references it is `arm_vm_init`'s own tail branch:
    #
    #     80018b0c  bl pmap_bootstrap                     ; +0x63c
    #     80018b14  bl arm_vm_prot_init                   ; +0x644  (the granular mappings)
    #     80018bc0  bl pmap_init_pte_page                 ; +0x6f0  (the pte loop)
    #     80018c20  movw r2, #0xfff / ldr r1, [sl]
    #     80018c28  add r1, r1, r2 / bfc r1, #0, #12      ; avail_start = round_page(avail_start)
    #     80018c34  ldr r0, [r0, #16]                     ; args->topOfKernelData
    #     80018c44  sub r1, r1, r0                        ; the size argument
    #     80018c4c  pop {r4, r5, r6, r7, r8, r9, sl, fp, lr}
    #     80018c50  b 8002ade8 <patch_low_glo_static_region>   ; a TAIL CALL - and its one reference
    #
    # So a report here says `arm_vm_init` ran to its last instruction: the 16 KB `bcopy` of the boot
    # page table, the `tte` fault-clearing walk, all seven `getsegdatafromheader` lookups, the
    # `getsectbynamefromheader(__DATA,__const)` and its `nextsect`, `getlastaddr`, `vm_set_page_
    # size`, `set_mmu_ttb` + `set_mmu_ttb_alternate` + `flush_mmu_tlb` (the point the MMU starts
    # walking XNU's own tables), `pmap_bootstrap`, `arm_vm_prot_init` with every `arm_vm_page_
    # granular_*` call, and the `pmap_init_pte_page` loop. And because the branch is a `b` and not a
    # `bl`, `lr` is still the value `arm_init` left at `80003160` - so the caller key is
    # `arm_init+0x1d8`, which is the *same* key the `arm_vm_init` checkpoint reported, for a
    # different reason, and a report here that reads anything else is a finding on its own.
    #
    # The two arguments are the second half of the prediction and they are checkable arithmetic:
    # `args->topOfKernelData` is the payload's `0x80300000`, and `avail_start` is
    # `cpu_ttep + ARM_PGBYTES*6` where `cpu_ttep = boot_ttep + ARM_PGBYTES*4` (`arm_vm_init.c:373,399`)
    # - `0x80304000 + 0x6000 = 0x8030A000` - and then the 4 MB alignment loop takes one page per
    # iteration for `off = 0` and `off = 4MB` against `off_end = (2 + mem_segments*3) << 20 = 5 MB`
    # (`arm_vm_init.c:514,517,523`), so `avail_start` is `0x8030C000` by the tail call and the size
    # is `0x8030C000 - 0x80300000 = 0xC000`.
    #
    # **The run was silent.** `STAGE90_ENTRY_CHECKPOINT=patch_low_glo_static_region`, built and run
    # on the same device through the same capture: the payload's ladder completes, the jump line is
    # written, and after it not one byte - `stub_hit` count 0, no `cp_arg*` record, no `exception:`,
    # `xnu_entry_abort_entries=0x0`. Safety counters clean in every place. So the last instruction of
    # `arm_vm_init` is not reached either, and the silence is **inside `arm_vm_init`'s 0x790 bytes**,
    # not in the eighteen calls after it. Note the asymmetry that produced the answer: this probe is
    # a *tail branch*, so it can only be reached by a run that has already executed everything in
    # `arm_vm_init` above it, and the two probes that reported (`cpu_data_init`, `arm_vm_init`) are
    # both before the function's first byte.
    #
    # **A correction owed to the prediction, made after the run and not by it.** As first written
    # that paragraph said `avail_start` is `0x80306000` and the size `0x6000`; `0x80304000 + 0x6000`
    # is `0x8030A000`, so the sentence and the number were both wrong, and the two pages the loop
    # takes are the rest of the difference. The run was silent, so the argument was never read and
    # the slip measured nothing - but it was written down as a prediction, which is exactly the kind
    # of claim this walk's prediction blocks exist to be checked against.
    #
    # The wrapper's own body is two stores to `lowGlo+0x3b4`/`+0x3b8` at `0x8010c3b4`/`0x8010c3b8`
    # (`lgStaticAddr`, `lgStaticSize`), so replacing it changes nothing the boot goes on to do; and
    # `entry_checkpoint.c` now records the four raw `r0`-`r3` registers, which is what makes the
    # arguments readable at all - a terminal wrapper never needed the prototype, but it can always
    # read what the caller put in the registers.
    #
    # **The next step, and its prediction.** The interval is `arm_vm_init`'s body, and the calls in
    # it that the whole image references exactly once are usable steps: `getlastaddr` (+0x2f8, one
    # reference), `vm_set_page_size` (+0x470, one), `set_mmu_ttb` (+0x480, one), `set_mmu_ttb_alternate`
    # (+0x488, one), `pmap_bootstrap` (+0x63c, one), `arm_vm_prot_init` (+0x644, one) and `nextsect`
    # (one). `bcopy` (+0xac) has thirteen and `flush_mmu_tlb` (+0x48c) has twenty-four, so neither is.
    #
    # **`STAGE90_ENTRY_CHECKPOINT=vm_set_page_size` reports, with the caller at `0x80018944`.** It is
    # 0x470 into a 0x790-byte function, i.e. past the header work and short of the MMU switch, so a
    # report splits what is left in two: the 16 KB `bcopy` of the boot page table, the `tte`
    # fault-clearing walk, the seven `getsegdatafromheader` lookups, `getlastaddr`,
    # `getsectbynamefromheader(__DATA,__const)` and its `nextsect` would all be exonerated, and the
    # failure would be in `set_mmu_ttb`/`set_mmu_ttb_alternate`/`flush_mmu_tlb`, `pmap_bootstrap`,
    # `arm_vm_prot_init` with its eleven `arm_vm_page_granular_*` ranges, or the two-iteration pte
    # loop. Its own body is three stores of constants (`page_mask` 0xfff, `page_size` 0x1000,
    # `page_shift` 12) to three globals, so it cannot be the thing that hangs, and `nm` says the
    # image has exactly one reference to it - `80018940 bl 80019320 <vm_set_page_size>`. Its
    # `cp_arg0`-`cp_arg3` are `void`'s registers and are recorded rather than interpreted.
    #
    # **The run reported, exactly as predicted: `stub_hit=vm_set_page_size`, `caller_v=0x80018944`.**
    # So 55% of `arm_vm_init` is exonerated - the 16 KB `bcopy` of the boot page table, the `tte`
    # fault-clearing walk, all seven `getsegdatafromheader` lookups, `getlastaddr`, the `__DATA,__const`
    # section lookup and its `nextsect`, and the `doconstro` sanity block. `cp_arg0`-`cp_arg3` read
    # `1 / 0x800f2da0 / 1 / 1`, which is what a `void` function's registers look like: recorded and
    # not interpreted. Safety counters clean in every place, and the device came back on its own.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=pmap_bootstrap` reports, with the
    # caller at `0x80018b10` and `cp_arg0=0x80000000`.** The interval left is `[+0x48c, +0x780)`, and
    # `+0x63c` is the nearest thing to its midpoint. What is *between* the last probe and this one is
    # worth stating because it bounds what a silence here would mean: `set_mmu_ttb(cpu_ttep)` at
    # `+0x480` and `set_mmu_ttb_alternate(cpu_ttep)` at `+0x488` write `cpu_ttep | 0x4a` to TTBR0 and
    # TTBR1 (`machine_routines_asm.s:333,347` - the `orr #0x4a` is in the callee, so the checkpoint
    # would see the register before that), `flush_mmu_tlb` at `+0x48c`, and then 432 bytes that store
    # `vm_prelink_*`, `sane_size`, `max_mem`, `vm_kernel_slide`, `vm_kernel_stext/_etext/_base/_top`,
    # `vm_kext_base/_top` and `vm_kernel_slid_base/_top`. Those thirteen stores are the *only* things
    # in the interval that are not a call, and none of them can loop - so a silence at `pmap_bootstrap`
    # would say the image does not survive the page-table switch, which is the one failure mode in
    # this function that reports nothing at all: a fault taken with no vector the CPU can fetch, which
    # is not an abort the handler can describe but a lockup.
    #
    # `pmap_bootstrap`'s own single argument is checkable in the image: at `80018af8 add r0, r0, r4`
    # and `80018b00 bfc r0, #0, #22` fold `(gVirtBase + MEM_SIZE_MAX + 0x3FFFFF) & 0xFFC00000` into
    # that - `MEM_SIZE_MAX` is a 64-bit constant whose low 32 bits are zero, so the addition is
    # `0x80000000 + 0x3FFFFF = 0x803FFFFF` and the mask leaves `0x80000000`.
    #
    # **The run reported: `stub_hit=pmap_bootstrap`, `caller_v=0x80018b10` - and `cp_arg0` read
    # `0xc0000000`, not the `0x80000000` predicted above.** The measurement is the one that is right
    # and the explanation is one line: `#define MEM_SIZE_MAX 0x40000000` (`arm_vm_init.c:134`, the arm
    # define - `arm64/arm_vm_init.c:181` has the `0x100000000ULL` the prediction assumed), so the sum
    # is `0x80000000 + 0x40000000 + 0x3FFFFF = 0xC03FFFFF` and the mask leaves `0xC0000000`. The
    # `cp_arg1`-`cp_arg3` values (`0 / 0x80116cf0 / 0x80116cf8`) are the two `.bss` addresses the
    # preceding stores were using, i.e. caller leftovers, recorded and not interpreted.
    #
    # Which means the page-table switch *is* survived: `set_mmu_ttb`, `set_mmu_ttb_alternate` and
    # `flush_mmu_tlb` all ran, and so did the 432 bytes of `vm_*` stores after them. That removes the
    # one failure mode that would have explained a silence here - an image that stops executing the
    # moment the MMU walks its own tables - and it leaves the rest of `arm_vm_init` as the only
    # candidate interval: `pmap_bootstrap`'s body (`+0x63c`), `arm_vm_prot_init` with its eleven
    # `arm_vm_page_granular_*` ranges (`+0x644`), the two-iteration pte loop (`+0x6f0`), and the tail
    # branch that the `patch_low_glo_static_region` run already showed is not reached.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=arm_vm_prot_init` reports, with
    # the caller at `0x80018b18` and `cp_arg0=0x80147000`.** The gap between `bl pmap_bootstrap` and
    # `bl arm_vm_prot_init` is two instructions that cannot fail (`80018b10 mov r0, r9` /
    # `80018b14 bl`), so a report here says exactly one thing and says it sharply: **`pmap_bootstrap`
    # returned**. A silence says `pmap_bootstrap` is the hang - and that function is `osfmk/arm/pmap.c`'s
    # own, the one experiment 195 measured from a probe that has since been compiled out, and the one
    # that builds `kernel_pmap`, the pv-head and attribute tables and the page-table pages out of
    # `avail_start`/`sane_size`.
    #
    # `cp_arg0` is the boot_args pointer: `80018b10 mov r0, r9` and `r9` holds the argument
    # `arm_init` passed to `arm_vm_init`, so it must read `0x80147000` - the value the payload's own
    # ladder reports as `xnu_entry_args_pa`, which is what makes it a check that joins the two halves
    # of the run rather than a prediction about one of them.
    #
    # **The run was silent. `arm_vm_prot_init` is not reached, so `pmap_bootstrap` is the hang.**
    # `+0x63c` reported and `+0x644` did not, and the two instructions between them cannot fail - so
    # `pmap_bootstrap` is entered and never returns. Everything above it in `arm_vm_init` is now
    # measured rather than argued, and so is everything below it: `arm_vm_prot_init`'s eleven
    # `arm_vm_page_granular_*` ranges, the two-iteration pte loop, the tail branch and the eighteen
    # calls after `arm_vm_init` are all unreached, and the `patch_low_glo_static_region` run already
    # said so from the other end.
    #
    # ---------------------------------------------------------------- the shape of the silence
    #
    # **A panic in this image is silent, and that is the first thing to establish because it changes
    # what "silent" means.** `panic()` writes its message through `printf`, and in this build
    # `printf` and `kprintf` are both redirected to libkern's no-op sinks: `printf.c:195` is
    # `int _consume_printf_args(int a __unused, ...) { return 0; }` and `printf.c:197` is
    # `void _consume_kprintf_args(int a __unused, ...) {}`, and the calls in `arm_init`'s disassembly
    # are `mov r0, #0; bl _consume_kprintf_args` - the format string is not even loaded. So a panic
    # prints nothing at all, then goes to `panic_trap_to_debugger` / `DebuggerWithContext` and never
    # comes back. It is indistinguishable from a hang **from the log**, which is the same defect this
    # walk has hit three times: a measurement that is the thing that is wrong. The earlier note that
    # "a panic there would say so" assumed output; in this configuration it cannot.
    #
    # **And there are exactly two panic sites in `pmap_bootstrap`, both in `pmap_load_io_rgns`
    # (`pmap.c:2710`), which the compiler inlined into it.** Read off the image rather than guessed:
    # the two `bl panic` are at `0x80021d40` and `0x80021d74`, and the format strings they load are
    # `"pmap I/O region %d is not aligned to I/O granularity!\n"` and
    # `"pmap I/O region %d size is not a multiple of I/O granularity!\n"` (`pmap.c:2730,2732`). The
    # checks around them are exact, and they are device-tree checks - which matters, because the tree
    # in this boot is this project's own:
    #
    #     80021d0c  ldr   r1, [sl]                    ; io_rgn_start
    #     80021d10  ldr   r0, [r9, r6, lsl #4]!       ; ranges[i].addr (a 16-byte packed entry)
    #     80021d1c  subs r0, r0, r1 / sbc r1, r2, #0  ; the 64-bit difference
    #     80021d24  mov   r2, r4 / mov r3, #0         ; and io_rgn_granule
    #     80021d28  bl    __aeabi_uldivmod            ; quotient in r0:r1, remainder in r2:r3
    #     80021d2c  orrs  r0, r2, r3 / beq +0x1c      ; both zero means divisible
    #     80021d34  movw  r0, #0xa971 / movt r0, #0x800e
    #     80021d40  bl    panic                       ; (addr - io_rgn_start) % granule != 0
    #     ...
    #     80021d58  udiv  r1, r0, r4 / mls r1, r1, r4, r0 / cmp r1, #0 / beq
    #     80021d68  movw  r0, #0xa9ab / movt r0, #0x800e
    #     80021d74  bl    panic                       ; len % granule != 0
    #
    # `pmap_compute_io_rgns` (`pmap.c:2665`) reads `pmap-io-granule` and `pmap-io-ranges` from
    # `/defaults` in the device tree and returns 0 early - leaving `io_rgn_granule` at zero, which
    # makes `pmap_load_io_rgns` return at its first line - when either property is absent. So the way
    # to reach the two panics below is to *have* both properties and have them disagree: a
    # `pmap-io-ranges` whose entries do not line up with `pmap-io-granule`. (A granule that is zero or
    # not page-aligned has its own panic, `pmap.c:2685-2686`, in the earlier function; a range that
    # overlaps physical memory has another, `pmap.c:2702-2704`.) That is a check on this project's own
    # device tree, which is the one part of the boot this walk *wrote* rather than inherited.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=panic` reports.**
    #
    # A checkpoint at `panic` fires at the **first** panic anywhere in the boot, and that is exactly
    # the question this run needs answered: no panic happens before `pmap_bootstrap` (a panic would
    # have halted the run and the `+0x63c` checkpoint would have been silent, and it was not), so a
    # report here is a panic inside `pmap_bootstrap` and the caller names the site. The prediction is
    # `stub_hit=panic` with `caller_v` at `0x80021d44` or `0x80021d78` - the return addresses of the
    # two `bl panic` above - with `cp_arg0` the format string (`0x800ea971` or `0x800ea9ab` in this
    # build, and it moves with the image, so it is read rather than predicted) and `cp_arg1` the
    # region index `i`. A silence would mean `pmap_bootstrap` loops without panicking, and the next
    # probe would be a call inside it.
    #
    # There is one honest caveat: `panic` has 1014 references in this image, so the checkpoint
    # redirects all of them. That is harmless for a terminal wrapper - it is one build, one run, and
    # the first panic is the only one that will ever be reached - but it means this build is a
    # diagnostic and not a stage image, exactly as the tracer is.
    #
    # **`STAGE90_ENTRY_CHECKPOINT=panic` was silent.** A checkpoint at `panic` fires at the first
    # panic anywhere in the boot, so a silence says `pmap_bootstrap` reaches neither of the two
    # inlined `pmap_load_io_rgns` panics nor any other, and it made `pmap_bootstrap`'s own body the
    # frontier. The rest of this block is what was then read out of the image to find where in that
    # body a run could stop. **The positive control at the end of the block refutes the frontier** -
    # not the reading, which is sound, and which is what made the control worth running.
    #
    # **`pmap_bootstrap`, read whole before spending another run on it.** The function's `bl` list is
    # complete - there is no `blx`, no `ldr pc`, no `pop {...pc}` above the epilogue - and every one
    # of the eight callees is real code, verified by disassembling each entry point:
    #
    #     hw_atomic_add         8001165c  ldrex r2, [r0] ...
    #     arm_usimple_lock_init 80012aa4  push {fp, lr} ...
    #     memset                8000374c  mov r3, r2 ...
    #     DTLookupEntry         800052d8  push {r4-r9, sl, fp, lr}
    #     DTGetProperty         800056e4  push {r4-r9, sl, fp, lr}
    #     cpu_number            80008d70  mrc 15, 0, r0, cr13, cr0, {4}
    #     PE_parse_boot_argn    80006cf4  mov r3, #0 ...
    #     panic                 8002dc34  sub sp, sp, #12 ...
    #
    # so `pmap_bootstrap` cannot report a `stub_hit` however far into it a run gets - there is no stub
    # in it to stop at. Its 466 main-body instructions contain exactly **two** backward branches, and
    # both are bounded:
    #
    #     8002176c  bne 80021730   `pmap_compute_io_rgns`' ranges walk, bounded by prop_size/16
    #     800219e4  bne 800219cc   `ptd_bootstrap`, bounded by ptd_root_table_size/sizeof(pt_desc_t)
    #
    # and the third loop, the `pmap_load_io_rgns` body at `80021cc8`, is **cold code placed after the
    # epilogue** (`80021cc4 pop {r4, r5, r6, r7, r8, r9, sl, fp, pc}`), reached only by `bcs 80021cc8`
    # from `80021970`. It is entered only when `/defaults` has both `pmap-io-granule` and
    # `pmap-io-ranges` with `prop_size >= 16`, and its first act would be a `prop_size`-bounded walk
    # of at most two entries. That is the one place a wild `io_attr_table` write could live, and this
    # project's device tree does not supply either property - `grep -rn 'pmap-io-granule\|pmap-io-ranges'
    # stages/ tools/` returns only comments in this file - so `io_rgn_granule` stays 0,
    # `pmap_compute_io_rgns` returns 0 through its `return 0` paths, `niorgns` is 0, and
    # `pmap_load_io_rgns` returns at its first instruction at `80021930`. Confirmed in the image:
    # `80021928 ldr r0, [fp]` / `cmp r0, #0` / `80021934 beq 80021974`, where `fp = &io_rgn_granule`
    # (`0x80118578`).
    #
    # Two things that were candidates and are now ruled out by reading rather than by running:
    #
    #   - **The `DTEntry` passed to `DTGetProperty` is not uninitialised.** `DTLookupEntry` returns
    #     `kError` *without* writing `*foundEntry` (`pexpert/gen/device_tree.c:220-260`), and
    #     `pmap_compute_io_rgns` compiles its `assert(err == kSuccess)` out - `80021658` reads the
    #     stack slot straight into `DTGetProperty` with no compare against `800052d8`'s return. Had
    #     `/defaults` been absent that would have been a garbage `DTEntry` dereferenced by
    #     `DTGetProperty`'s `entry->nProperties`. It is present: experiment 193 added it
    #     (`stage90_main.c:735`, `apple_dt_prop_str(b, "name", "defaults")`, asserted by
    #     `apple_dt.c:230 expect_child(root, end, "defaults")` and counted in
    #     `STAGE90_APPLE_DT_ROOT_CHILDREN 21`), so the lookup succeeds and the node's two properties
    #     simply do not match.
    #   - **`avail_start` and `mem_size` are sane and identical in both configurations.** `mem_size`
    #     is `args->memSize` = `STAGE90_XNU_ENTRY_SIZE`, and that macro is
    #     `ENTRY_SIZE=0x00200000` doubled until it covers `ENTRY_DT_OFFSET + ENTRY_DT_MAX` - a
    #     constant of the device-tree layout, **not** a function of how big the entry image came out,
    #     so it is 8 MB in 279 and in 280 alike (`avail_end = 0x80800000`). `avail_start` at
    #     `pmap_bootstrap` is `cpu_ttep + ARM_PGBYTES*6` (`arm_vm_init.c:399`) = `0x80304000 + 0x6000`
    #     = `0x8030A000` in both, `cpu_ttep` being `topOfKernelData + 0x4000` and `topOfKernelData`
    #     unmoved at `0x80300000`. With `mem_size = 8 MB`: `npages = 2048`, `ptd_root_table_size =
    #     24 * (1<<12) = 0x18000`, `ptd_bootstrap` walks 4096 entries, the big `memset` covers about
    #     0x21000 bytes, and the whole table region `[0x8030A000, 0x8032B000)` sits inside the
    #     `arm_vm_page_granular_RWNX(topOfKernelData + ARM_PGBYTES*10, static_memory_end - ...)`
    #     mapping from `arm_vm_init.c:301`, whose start is `0x8030A000` **exactly where
    #     `avail_start` is** and whose end is `static_memory_end = 0x80800000`.
    #
    # Which leaves nothing in the body that can fail, and a measurement that says it does. That
    # combination is this project's oldest defect in its purest form - a claim that is an artefact of
    # how it was taken - and it is why the next step was a **positive control** rather than a finer
    # probe inside `pmap_bootstrap`.
    #
    # **The control, and what it measured. `STAGE90_ENTRY_CHECKPOINT=pmap_init_pte_page` reports.**
    #
    # `pmap_init_pte_page` has two call sites in this image and the first to fire is inside
    # `arm_vm_init`'s pre-init pte loop at `80018bc0` - **after** `bl arm_vm_prot_init` (`80018b14`)
    # and after `pmap_bootstrap` returned. The host side read the wrap back out of the built image
    # before the device was touched: `80018bc0 bl 800e06d4 <__wrap_pmap_init_pte_page>`, with
    # `pmap_init_pte_page` still real at `800250dc`. The run reported
    #
    #     cp_arg0=0x801183d0   kernel_pmap - a value, not a predicted number
    #     cp_arg1=0x80328000   phystokv(avail_start); the fifth argument is on the stack, unrecorded
    #     cp_arg2=0xc0000000   va + off at off = 0, i.e. (gVirtBase+MEM_SIZE_MAX+0x3FFFFF)&0xFFC00000
    #     cp_arg3=0x00000002   the size shift, the fourth argument
    #     stub_hit=pmap_init_pte_page
    #     xnu_entry_stub_caller=0x80018bc4
    #
    # - three of the four arguments exactly as predicted, from the loop's **first** iteration - and it
    # settles the question the other way:
    #
    #   **`pmap_bootstrap` returned.** `avail_start` is `0x8030A000` when it is entered
    #   (`arm_vm_init.c:399`, `cpu_ttep + ARM_PGBYTES*6`, and `cpu_ttep = 0x80304000` in this build,
    #   which the build's own layout line confirms: "0x0000A000 bytes of page tables" above
    #   `topOfKernelData`) and it is `0x80328000` here. So the whole table-carving block ran - the
    #   five regions placed, the big `memset` done, `ptd_bootstrap`'s 4096-entry loop walked,
    #   `pmap_cpu_data_array_init` and both `asid_bitmap` loops run - and `0x1E000` is within `0x2800`
    #   of the size those regions add up to. A body that never returns does not advance `avail_start`
    #   at all. **And `arm_vm_prot_init` returned**, two instructions before the call that reported.
    #
    # **So the `arm_vm_prot_init` silence was not a frontier, and neither were the two taken beside
    # it.** The three empty quarters of this bisect - `patch_low_glo_static_region`,
    # `arm_vm_prot_init` and `panic` - were built and run in one sitting and each produced a
    # **294042-byte** log ending at the payload's `jumping to XNU's _start`, against 301195 and
    # 301197 for the two that reported and 301199 for this one. They are three *different* logs (they
    # differ at byte 1382 and byte 8135), so they are not one capture copied three times - but they
    # are all 294042 bytes, and the payload records only `xnu_entry_image_bytes=0x00110628`, the
    # entry image's size, which is **identical for every checkpoint build** because the wrapper fits
    # in padding the image already had. Nothing in the log can tell those three images apart, and a
    # silence that cannot be told apart from a stale image is not a measurement. The rule this leaves
    # behind is the one the control applies: a checkpoint's silence counts only once the wrap has
    # been read back out of the built image, and the entries it would have reported differ from the
    # entries of the build before it.
    #
    # **The frontier, re-opened: `[arm_vm_init+0x6f0, ...)`.** Its tail was read out of the image as
    # well. The loop is `for (off = 0; off < off_end; off += ARM_TT_L1_PT_SIZE)` with
    # `off_end = (2 + mem_segments*3) << 20 = 5 MB` (`mem_size = 8 MB` gives `mem_segments = 1`) and
    # `ARM_TT_L1_PT_SIZE = 0x400000` - **two** iterations, one `pmap_init_pte_page` and four
    # `cpu_tte` writes each - and what follows is `avail_start = (avail_start + PAGE_MASK) &
    # ~PAGE_MASK`, `first_avail`, and the tail branch to `patch_low_glo_static_region` at `80018c50`.
    # All of it bounded, none of it able to fault: the writes are to `cpu_tte[0xc00..0xc03]`, the L1
    # entries for `0xc0000000`, inside the table at `0x80304000` that is already the live TTB.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=machine_startup`.**
    #
    # `machine_startup` is called from exactly one site in the whole image - `arm_init+0x340`, the
    # last step of the ladder - and it is real code (`80007524`, 0x100 bytes: four `PE_parse_boot_argn`
    # then `bl kernel_bootstrap` at `8000760c`), so it is the one probe above `arm_vm_init` that no
    # earlier call can pre-empt. **The prediction is that it reports**, with `caller_v = 0x800032cc`
    # and no argument record for `machine_startup` itself - it takes none. A report means
    # `arm_vm_init` returned into `arm_init` and `arm_init` ran all seventeen real calls between them
    # (`PE_parse_boot_argn`, `patch_low_glo`, `printf_init`, `panic_init`,
    # `PE_consistent_debug_inherit`, `PE_init_kprintf`, `kern_feature_override`,
    # `switch_to_serial_console`, `PE_create_console`, `PE_init_printf`, `cpu_machine_idle_init`,
    # `get_mmu_control`, `set_mmu_control`, `PE_init_platform`, `cpu_timebase_init`,
    # `fiq_context_init`, `early_random`), and the frontier is inside `machine_startup` or
    # `kernel_bootstrap`. A silence would put the run in `arm_vm_init`'s tail or in one of those
    # seventeen, and the next probe is `patch_low_glo` (`8000318c`, one call site, the first of them).
    #
    # This run is also the check the three suspect silences are owed. `machine_startup` was one of
    # them, taken in the same sitting, so a report here says that silence was an artefact of the
    # sitting and not of the code - the same shape as the 279 control that made 280's silence a
    # measurement in the first place. No inference should be drawn from any of those three until it
    # reports or fails to.
    #
    # **The run was silent - and this one is verified.** `STAGE90_ENTRY_CHECKPOINT=machine_startup`,
    # built and run on the 280 configuration with the wrap read back out of the image
    # (`800032c8 bl 800e06d4 <__wrap_machine_startup>`, `machine_startup` still real at `80007524`)
    # and the payload image checked byte-for-byte against the entry `.bin` it embeds before the
    # device was touched. The log is 294042 bytes and ends at the payload's jump line, the same size
    # as the three suspect silences - so those three are consistent with a real silence, and
    # `machine_startup` joins them as a measured one. `arm_init`'s last step is not reached.
    #
    # **The interval, now with both ends measured: `[arm_vm_init+0x6f0, arm_init+0x340)`.** The lower
    # end reports (`pmap_init_pte_page` at `80018bc4`); the upper end does not. Inside it:
    #
    #     arm_vm_init's tail       the pte loop's second iteration (off = 4 MB), the `avail_start`
    #                              rounding, `first_avail`, and the tail branch to
    #                              `patch_low_glo_static_region` at `80018c50`
    #     arm_init+0x1d8 .. +0x340 `PE_parse_boot_argn`, `patch_low_glo`, `printf_init`, `panic_init`,
    #                              `PE_consistent_debug_inherit`, `PE_init_kprintf`, then the
    #                              `_consume_kprintf_args` triple, `kern_feature_override`,
    #                              `switch_to_serial_console`, `PE_create_console`, `PE_init_printf`,
    #                              `cpu_machine_idle_init`, `get_mmu_control`, `set_mmu_control`,
    #                              `PE_init_platform`, `cpu_timebase_init`, `fiq_context_init`,
    #                              `early_random`
    #
    # and the two functions this interval's lower end enters on the way - `pmap_init_pte_page`
    # (`800250dc`) and `ptd_alloc` (`800251e0`, not inlined) - were read out of the image and are
    # bounded: `pmap_init_pte_page` indexes `pv_head_table[pa_index(pte_p)]` at `80025134` and the
    # index for `avail_start = 0x80328000` is 808 against a table of 2048 entries, then branches to
    # `ptd_alloc` because the entry is NULL and `alloc_ptd` is TRUE, then `__bzero(pte_p, 4096)`;
    # `ptd_alloc(kernel_pmap)` takes its fast path (`ptd_free_count` is 4096 from `ptd_bootstrap` and
    # `ptd_free_list` is non-NULL) and its only loop, `800252e4 bcc 800252e4`, counts 0xA9 + 1 = 170
    # iterations of linking a `pt_desc_t`. Its `ledger_credit` calls at `800253a0`/`800253b4` are
    # behind `cmp r0, r4 / beq` on `kernel_pmap`, so the kernel pmap does not reach them at all.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=patch_low_glo`.**
    #
    # `patch_low_glo` is the first thing in the upper half and it has **one call site in the whole
    # image**, `bl patch_low_glo` at `8000318c` from `arm_init`, so nothing earlier in the boot can
    # pre-empt the report. It is the cleanest split available: it is nine instructions past
    # `arm_vm_init`'s return (`80003160`) and one past `PE_parse_boot_argn` (`80003170`).
    #
    #   - **A report at `caller_v = 0x80003190`** means `arm_vm_init`'s tail ran to its end - the pte
    #     loop's second iteration, the rounding, and the tail branch into
    #     `patch_low_glo_static_region` - and the frontier is `arm_init`'s sixteen remaining calls
    #     (or `patch_low_glo` itself, since a terminal wrapper never enters it). Both halves are then
    #     bisectable the same way: everything from `printf_init` (`80003190`) to `early_random`
    #     (`800032b0`) is a `bl` from `arm_init` with a known return address, so the caller key names
    #     the step.
    #   - **A silence** puts the run inside `arm_vm_init`'s pte loop, in `patch_low_glo_static_region`
    #     (whose own checkpoint was silent, `80018c50`, one reference), or in the `avail_start`
    #     arithmetic - and the next probe is the second loop iteration, which needs the
    #     caller-selective instrument, since the only callable at `off = 4 MB` is the same
    #     `pmap_init_pte_page` at the same site.
    #
    # **The run reported: `stub_hit=patch_low_glo`, `caller_v=0x80003190`.** The prediction held, and
    # the call site is guarded:
    #
    #     8000317c  ldr r0, [sp, #4]
    #     80003180  and r0, r0, #324      ; 0x144 = MIN_LOW_GLO_MASK
    #     80003184  cmp r0, #324
    #     80003188  bne 80003190          ; the `debug` boot arg gates it
    #     8000318c  bl  __wrap_patch_low_glo
    #
    # and the boot-args this project builds do carry `debug=0x144`, so `(debugmode & 0x144) == 0x144`
    # and the call is taken. `cp_arg0` is `0x00000144` - `patch_low_glo` takes no arguments, so the
    # four words are the registers the caller happened to leave, which is what the instrument
    # records and does not interpret - and `cp_arg1=0x800ed9bb`, `cp_arg2=0x0000000c`,
    # `cp_arg3=0x0000006d` are the same leftovers. The log is 301194 bytes.
    #
    # **So `arm_vm_init`'s tail completed.** The pte loop's second iteration at `off = 4 MB`, the
    # `(avail_start + PAGE_MASK) & ~PAGE_MASK` rounding, `first_avail`, and the tail branch into
    # `patch_low_glo_static_region` all ran, and the run returned into `arm_init` - which retires
    # that whole half of the interval along with the `pmap_bootstrap` frontier it replaced. The
    # frontier is now **`[arm_init+0x204, arm_init+0x340)`**, the eighteen real calls from
    # `patch_low_glo`'s call site to `bl machine_startup`, and every one of them is a `bl` from
    # `arm_init` whose return address is a single value:
    #
    #     80003190  printf_init                    80003230  kern_feature_override
    #     80003194  panic_init                     80003250  switch_to_serial_console   (3 sites)
    #     80003198  PE_consistent_debug_inherit    80003264  PE_create_console
    #     800031a4  PE_init_kprintf      (2 sites) 8000326c  PE_init_printf             (2 sites)
    #     800031ac  _consume_kprintf_args  (58)     80003274  cpu_machine_idle_init      (2)
    #     800031cc  PE_parse_boot_argn    (94)      80003284  get_mmu_control            (3)
    #     800031e0  _consume_kprintf_args            8000328c  set_mmu_control            (3)
    #     80003204  PE_parse_boot_argn              8000329c  PE_init_platform           (2)
    #     80003228  _consume_kprintf_args            800032a4  cpu_timebase_init          (2)
    #                                               800032ac  fiq_context_init           (4)
    #                                               800032b0  early_random               (9)
    #
    # - the counts being image-wide, and the second site in each case belonging to a function that
    # only runs later (`PE_init_iokit`, `arm_init_cpu`, `arm_init_idle_cpu`, `PE_initialize_console`,
    # `vm_mem_bootstrap`, `zone_bootstrap`, `initialize_screen`), so `printf_init`, `panic_init`,
    # `PE_consistent_debug_inherit`, `kern_feature_override` and `PE_create_console` are single-site
    # and clean.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=panic` - re-run.**
    #
    # The silent `panic` result belongs to the sitting whose three silences the `pmap_init_pte_page`
    # control refuted, so it carries no weight, and it is the single most informative probe left. A
    # panic is the only mechanism this walk has found that stops a run with **no output at all**:
    # `printf.c`'s `_consume_printf_args` and `_consume_kprintf_args` are the no-op bodies the build
    # compiles `printf`/`kprintf` to, `panic`'s own disassembly references neither, and
    # `panic_trap_to_debugger` with no debugger attached does not return - so a panic and a hang are
    # the same picture in the log. The whole upper half of `arm_init`'s ladder is pexpert code with
    # `assert`s in it (`PE_create_console`, `switch_to_serial_console`, `PE_init_printf`,
    # `cpu_machine_idle_init`, `PE_init_platform` all reach device-tree and console paths), which is
    # where such a panic would come from.
    #
    #   - **A report** names the site in `xnu_entry_stub_caller_v` and puts the format string in
    #     `cp_arg0`, which is the message - and the frontier collapses from eighteen calls to one.
    #   - **A silence, verified the same way as `machine_startup`** (wrap read back out of the image,
    #     payload checked against the `.bin`), says there is no panic and the frontier stays the
    #     ladder, to be split at `PE_create_console` (`80003264`, single-site, the middle).
    #
    # The prediction, stated so that it can be wrong: **a report.** The balance is that a silent
    # panic is the only mechanism the image offers for a stop with no output, that the ladder is the
    # first stretch of the boot with asserts in it, and that `cpu_machine_idle_init` at `80003274`
    # calls `bcopy_phys` three times (`8000416c`, `8000423c`, `80004278`) into pexpert code this
    # walk has never executed. A silence would be the second surprise in a row and would send the
    # bisect back to `PE_create_console`.
    #
    # **The run was silent, and the prediction was wrong.** `STAGE90_ENTRY_CHECKPOINT=panic`, with
    # the wrap read back out of the image (`__wrap_panic` at `800e06d4`, `panic` still real at
    # `8002dc34`, 957 `bl panic` sites redirected) and the payload checked against the entry `.bin`:
    # a 294042-byte log ending at the jump line, identical in size to `machine_startup`'s. **So there
    # is no panic anywhere in the boot.** `panic` is silent-filled by design and `panic_trap_to_debugger`
    # does not return, so this is a real negative, not a lost report: the run is not dying through
    # `panic`, and the ladder is not failing an `assert`.
    #
    # That is worth having on the record by itself: it removes the one mechanism that could have
    # explained a stop with no output, and the two remaining ones are a genuine unbounded loop and a
    # fault whose vector table entry does not report - and 236-242 established that this image's
    # faults do report their vector, DFAR/DFSR and faulting instruction. So the frontier is a loop,
    # inside `[arm_init+0x204, arm_init+0x340)`.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=kern_feature_override`.**
    #
    # The ladder from the source (`arm_init.c:322-380`), in order, with the return address each `bl`
    # gives and the image-wide site count from the disassembly:
    #
    #     80003190  printf_init                   1
    #     80003194  panic_init                    1
    #     80003198  PE_consistent_debug_inherit   1
    #     800031a4  PE_init_kprintf(FALSE)        2   (the other is PE_init_iokit, later)
    #     800031ac  kprintf -> _consume_kprintf_args (no-op)
    #     800031cc  PE_parse_boot_argn("serial")  94
    #     80003230  kern_feature_override(KF_SERIAL_OVRD)  1
    #     80003250  switch_to_serial_console      3   (3 instructions: sets cons_ops_index)
    #     80003264  PE_create_console             1
    #     8000326c  PE_init_printf(FALSE)         2   (the other is PE_init_iokit, later)
    #     80003274  cpu_machine_idle_init(TRUE)   2   (the other is arm_init_cpu, later)
    #     80003284  get_mmu_control               3
    #     8000328c  set_mmu_control               3
    #     8000329c  PE_init_platform              2
    #     800032a4  cpu_timebase_init             2
    #     800032ac  fiq_context_init              4
    #     800032b0  early_random                  9   (the others are zone_bootstrap/vm_mem_bootstrap)
    #     800032c8  machine_startup               1   (measured: silent)
    #
    # `kern_feature_override` is the tenth step and the single-site probe nearest the middle: nine
    # calls below it, seven above it before `machine_startup`. Its own body is a lookup in
    # `kern_feature_table` against the `kern.features` boot-arg, bounded, and it returns a boolean the
    # caller tests.
    #
    #   - **A report at `caller_v = 0x80003234`** means `printf_init`, `panic_init`,
    #     `PE_consistent_debug_inherit`, `PE_init_kprintf`, the `kprintf` sink, the `serial` boot-arg
    #     parse and the feature lookup all completed, and the loop is in the seven remaining calls -
    #     where the next probe is `cpu_machine_idle_init` (`80003274`), the one with a `bcopy_phys`
    #     into an idle-page template and the only step left that this walk has never run in any image.
    #   - **A silence** puts the loop in the nine below, where the next probe is
    #     `PE_consistent_debug_inherit` (`80003198`, single-site) - the first step that reads the
    #     device tree, and therefore the first step whose behaviour this project's own DT controls.
    #
    # The prediction, stated so that it can be wrong: **a report.** `printf_init`, `panic_init`,
    # `PE_init_kprintf` and `kern_feature_override` are all table-and-pointer setup with no loop over
    # anything this project supplies, while `PE_create_console`, `PE_init_printf` and
    # `cpu_machine_idle_init` all reach pexpert console and page paths. A silence would say the loop
    # is in setup code after all, which would point at `PE_consistent_debug_inherit` and the DT.
    #
    # **The run reported: `stub_hit=kern_feature_override`, `caller_v=0x80003234`.** The prediction
    # held, and `cp_arg0=0x00000002` is a real argument - `KF_SERIAL_OVRD`, the constant the source
    # passes - which is the first checkpoint in this walk whose recorded registers are the callee's
    # actual parameters rather than leftovers. So `printf_init`, `panic_init`,
    # `PE_consistent_debug_inherit`, `PE_init_kprintf(FALSE)`, the `kprintf` sink, and the `serial`
    # boot-arg parse all completed without stopping, and `kern_feature_override` was entered. Log
    # 301202 bytes.
    #
    # **The frontier is now `[arm_init+0x2ac, arm_init+0x340)`** - ten calls, in this order:
    #
    #     80003250  switch_to_serial_console    3 insns: `cons_ops_index = SERIAL_CONS_OPS`
    #     80003264  PE_create_console           1 site
    #     8000326c  PE_init_printf(FALSE)       2   (the other is PE_init_iokit, later)
    #     80003274  cpu_machine_idle_init(TRUE) 2   (the other is arm_init_cpu, later)
    #     80003284  get_mmu_control             3   (the others are arm_init_cpu/_idle_cpu, later)
    #     8000328c  set_mmu_control             3   (likewise)
    #     8000329c  PE_init_platform            **2, and the first is at 0x80002fd4** - the early call
    #                                               from arm_init's own early ladder, before
    #                                               `arm_vm_init`, so a `--wrap` on this name reports
    #                                               from there and says nothing about the tail
    #     800032a4  cpu_timebase_init           2   (the other is arm_init_cpu, later)
    #     800032ac  fiq_context_init            4   (the others are arm_init_cpu/_idle_cpu, later)
    #     800032b0  early_random                9   (the others are zone_bootstrap/vm_mem_bootstrap,
    #                                               all after machine_startup)
    #
    # `switch_to_serial_console` is three instructions (`osfmk/console/serial_general.c:111`) and
    # cannot loop. `PE_init_platform` cannot be probed by name at all.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=cpu_machine_idle_init`.**
    #
    # It is the fourth of the ten and the single-site probe nearest the middle: three calls below it,
    # six above it, and its own body is a `bcopy_phys` x3 into an idle-page template
    # (`8000416c`, `8000423c`, `80004278`) - the only step left that no image in this walk has ever
    # run. A terminal wrapper fires on entry, so a report says "reached", not "returned".
    #
    #   - **A report at `caller_v = 0x80003278` with `cp_arg0` non-zero** (`cpu_machine_idle_init(TRUE)`)
    #     means the loop is inside it or in the six above: `get_mmu_control`, `set_mmu_control`,
    #     `PE_init_platform`, `cpu_timebase_init`, `fiq_context_init`, `early_random`. The next probe
    #     there is `cpu_timebase_init` (`800032a4`), which brackets `PE_init_platform` from above.
    #   - **A silence** puts the loop in `PE_create_console` or `PE_init_printf` (`switch_to_serial_console`
    #     cannot loop), and since both are pexpert console paths, the next probe is `PE_create_console`
    #     (`80003264`, single-site) to separate them.
    #
    # The prediction: **a report.** `PE_init_platform` is the largest thing in the ten - it reaches
    # `PE_init_iokit` and the IOKit match - and `early_random` gathers entropy, both of which are
    # places a boot with a synthetic device tree can loop where a real one would not. Nothing in
    # `PE_create_console`/`PE_init_printf` reads a project-supplied table.
    #
    # **The run reported: `stub_hit=cpu_machine_idle_init`, `caller_v=0x80003278`, and the prediction
    # held on both keys at once.** `cp_arg0=0x00000001` is the literal `mov r0, #1` three instructions
    # above the call, so this is the second checkpoint in the walk whose recorded registers are the
    # callee's real arguments and not leftovers - `cpu_machine_idle_init(TRUE)`. `cp_arg1=0x80098644`
    # and `cp_arg2=cp_arg3=0` are the registers that call site does not set, recorded and not
    # interpreted. Log 301202 bytes.
    #
    # **The frontier is `[arm_init+0x2b8, arm_init+0x340)`** - and the interval's low end is now read
    # from the image rather than inferred, because `cpu_machine_idle_init`'s body is the first thing
    # this walk has ever entered that is not a table lookup:
    #
    #     80004090  cpu_machine_idle_init            0x258 bytes, 20 calls
    #     800040b4  beq 800042a0                     ; r0 == 0 -> the epilogue tail. NOT taken: r0 = 1
    #     800040d0  bl PE_parse_boot_argn            ; "..." -> [sp+12]
    #     80004108  bl PE_parse_boot_argn            ; "..." -> [sp+8]
    #     8000410c..80004134  the gate             ; not found -> [sp+8] = 1, b 8000418c
    #     80004138..80004188  a block of ml_static_vtop + bcopy_phys, entered only when the boot arg
    #                                               IS present and its value is 1 and not 2 - so with
    #                                               this project's boot-args the first call of the body
    #                                               that actually runs is 8000418c's
    #     8000418c  (the block above is jumped over) -> r6 = &X, r0 = [r6]
    #     80004198  bl ml_vtophys                    ; the first executed call after the two parses
    #     800041a0  bl ml_io_map                     ; <= experiment 206 stopped HERE, when it was a stub
    #     800041bc  bl bcopy     800041d0  bl bcopy
    #     800041dc  bl ml_static_vtop   ... 80004298  bl ml_static_vtop   (six more, interleaved)
    #     8000423c  bl bcopy_phys   80004278  bl bcopy_phys
    #     8000428c  bl CleanPoC_DcacheRegion
    #     800042c4  bl bcopy
    #     800042dc  bl clean_dcache                 ; the LAST call of the body
    #     800042a0  the epilogue tail: 800042a0..800042e8, stores into [r8] and friends, then return
    #
    # **Which of those 20 calls can be probed by name, computed from the call graph of this image and
    # not from guesswork.** A `--wrap` redirects every reference to the name, so a symbol is usable
    # only if no call site is reachable before the frontier - and the frontier's own reached set is
    # now large enough that most of them are not:
    #
    #     name                   sites  reachable before 80003274?          usable
    #     PE_parse_boot_argn      95    yes: arm_init itself at 800030c0     no
    #     ml_vtophys               2    yes: cpu_data_register 80003e80     no
    #     ml_io_map                5    yes: PE_init_kprintf -> serial_init 8002f17c, and
    #                                   PE_init_platform -> pe_arm_init_interrupts 80004808  **no**
    #     bcopy                   29    yes: PE_init_platform (6), arm_vm_init (1)          **no**
    #     ml_static_vtop          16    **no**: 8 in this body, the other 8 in pmap_create,
    #                                   pmap_map_globals (from machine_init, 8000766c),
    #                                   pmap_pages_alloc, pmap_tt1_deallocate x2,
    #                                   pmap_tt_deallocate x2, pmap_expand - all later   **yes**
    #     bcopy_phys               7    **no**: 3 in this body, pmap_copy_page, pmap_copy_part_page,
    #                                   kdp_copyin, and ml_nofault_copy - whose only nine callers
    #                                   are panic_display_* - all later                    **yes**
    #     clean_dcache             2    **no**: the other site is inside clean_dcache64, and
    #                                   clean_dcache64 has ZERO callers in this image       **yes**
    #
    # Two of those are worth stating as facts rather than as arithmetic, because they close doors:
    # **`ml_io_map` is unusable**, and it is the one symbol in the body with a prior stop on its
    # record. `PE_init_kprintf` - which ran at 800031a4, before the frontier - loads
    # `PE_state.initialized` at 8002eeb0 and takes `panic` when it is zero. **It did not panic**, so
    # the early `PE_init_platform(FALSE, ...)` at 80002fd4 did complete its initialisation; and since
    # `PE_init_kprintf(FALSE)` then falls through 8002eee0 to `bl serial_init` at 8002ef28 with no
    # branch around it, `serial_init` ran before the frontier and its `bl ml_io_map` at 8002f17c is
    # therefore on the reached path. That the run continued past it does not make the symbol
    # probeable - a wrap on it would stop at 8002f180 and say nothing about 800041a0.
    #
    #    - and one that is not a contamination argument at all: `clean_dcache`'s own byte count is
    #      immaterial, but the fact that its *other* reference sits in a function nothing calls means
    #      that if the body hangs before 800042dc, `clean_dcache` will be silent, and if it does not,
    #      `clean_dcache` reports. It is a clean read of "the body finished".
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=bcopy_phys`.** It is the middle
    # of the three usable names: `ml_static_vtop`'s first *executed* site is 800041dc, `bcopy_phys`'s
    # is 8000423c, `clean_dcache`'s is 800042dc - the last call in the body - so a report from
    # `bcopy_phys` falsifies the whole upper half of the body and makes the remaining interval
    # `[80004240, arm_init+0x340)`.
    #
    #   - **A report at `caller_v = 0x80004240`.** The `mov r1, #0` at 80004238 and `mov r3, #0` at
    #     80004228 are two instructions apart from the call, so the pair of argument predictions is
    #     firm: `cp_arg1 = 0x00000000`, `cp_arg3 = 0x00000000`. `cp_arg0` is the `r0` that
    #     `ml_static_vtop` at 80004210 last left there - a physical address, so a value and not a
    #     predicted number. `cp_arg2` is `r1 + 8` where `r1 = (0x800de8bc - 0x800dc4bc) + [0x80116c94]`
    #     - computed from the body, and a number this walk can check.
    #   - **A silence.** The stop is inside the body at or before 80004210, i.e. in `PE_parse_boot_argn`
    #     (twice), `ml_vtophys`, `ml_io_map`, the two `bcopy`s, or the three `ml_static_vtop`s between
    #     800041dc and 80004210 - and the next probe is `ml_static_vtop` itself (`caller_v` would be
    #     `0x800041e0`), which is clean and which splits that stretch again, leaving only
    #     `ml_io_map` and `bcopy` on the far side of it.
    #
    # The prediction: **a report.** Every one of those 20 calls is a leaf-ish primitive with no loop
    # in it that this project's own ledger has not already run - `bcopy_phys` is `memcpy` over 4
    # bytes, `CleanPoC_DcacheRegion` and `clean_dcache` are bounded cache walks, and `ml_io_map` is
    # the one with a synthetic device tree in front of it. If the body is where the stop is, it is
    # more likely to be at `ml_io_map` than at `bcopy_phys` - which is exactly why the probe is
    # `bcopy_phys`: it is above `ml_io_map`'s site by one call and below the last one by nine.
    #
    # **The run reported, and every key in it was predicted.** `stub_hit=bcopy_phys`,
    # **`caller_v=0x80004240`** - the site at 8000423c, exactly as predicted - with
    # `cp_arg1=0x00000000` and `cp_arg3=0x00000000`, the two the `mov r1, #0` at 80004238 and the
    # `mov r3, #0` at 80004228 put there. `cp_arg2=0x80002408` is the computed one and it verifies a
    # number rather than a register: the body sets `r1 = (0x800de8bc - 0x800dc4bc) + [0x80116c94]` and
    # `r2 = r1 + 8`, and `0x80002408 - 0x2400 - 8 = 0x80000000`, so `[0x80116c94]` - one of this
    # object's own `BootArgs_paddr` / `CpuDataEntries_paddr` words - holds the physical base exactly.
    # `cp_arg0=0x80114840` is the register `ml_static_vtop` at 80004210 last left, and it is the one
    # value that is interesting on its own: **`ml_static_vtop(0x80114840)` returned `0x80114840`**, so
    # the entry window's static table maps the image's own `.data` identity. Log 301191 bytes.
    #
    # **The frontier is `[arm_init+0x2bc, arm_init+0x340)`** - the eight calls that follow 8000423c -
    # and the six that follow the body - thirteen sites in all, in execution order:
    #
    #     body tail, firm          conditional
    #     80004248  ml_static_vtop  800042c4  bcopy   ; runs only when r4 == 0x80102000, and r4 is the
    #     8000425c  ml_static_vtop                     boot CPU's CpuData, which arm_init stored into
    #     80004278  bcopy_phys                         TPIDRPRW+0x5bc - so on CPU 0 it runs
    #     8000428c  CleanPoC_DcacheRegion
    #     80004298  ml_static_vtop  800042dc  clean_dcache  ; unconditional, the body's last call
    #     800042e4  pop {r4,r5,r6,r7,r8,r9,fp,pc} -> arm_init+0x2b8
    #
    #     ladder after the body
    #     80003278  ldrb r0,[r8,#1] / tst r0,#0x80 / beq 80003290   ; r8 = 0x8011482c, so this is a
    #                                                                 byte of `arm_diag`-shaped state
    #     80003284  get_mmu_control   8000328c  set_mmu_control      ; BOTH inside the same gate
    #     8000329c  PE_init_platform(TRUE, 0x80102000)
    #     800032a4  cpu_timebase_init(TRUE)
    #     800032ac  fiq_context_init(TRUE)
    #     800032b0  early_random
    #     800032c8  machine_startup(boot_args)
    #
    # **The contamination tables came out cleaner than the last two frontiers', because two whole
    # functions turn out to have no callers at all in this image.** `arm_init_cpu`, `arm_init_idle_cpu`,
    # `clean_dcache64`, `PE_sync_panic_buffers`, `dcache_incoherent_io_flush64` and
    # `dcache_incoherent_io_store64` **each have zero textual references** - so every call site they
    # own is dead code, and a `--wrap` on a name they call, or that calls them, is as good as a wrap
    # on a name with one site:
    #
    #     name                     sites  reachable before 80003274?                     usable
    #     PE_init_platform           2    **yes: arm_init's own early call at 80002fd4**   no
    #     get_mmu_control            3    2 of them in arm_init_cpu / _idle_cpu (dead)     yes
    #     set_mmu_control            3    likewise                                        yes
    #     cpu_timebase_init          2    the other in arm_init_cpu (dead)                yes
    #     fiq_context_init           4    3 in arm_init_cpu / _idle_cpu (dead)            yes
    #     early_random               9    the others in vm_mem_bootstrap and zone_bootstrap,
    #                                     both far past machine_startup                   yes
    #     machine_startup            1    -                                               yes
    #     ml_static_vtop            16    the other 8 in pmap_create, pmap_map_globals (from
    #                                     machine_init at 8000766c), pmap_pages_alloc, pmap_tt1_-
    #                                     deallocate x2, pmap_tt_deallocate x2, pmap_expand   yes
    #     clean_dcache               2    the other inside clean_dcache64, which no one calls   yes
    #     CleanPoC_DcacheRegion     10    ml_arm_sleep, and clean_dcache - and each of the rest is
    #                                     either dead (the dcache_incoherent_io_*64 pair,
    #                                     PE_sync_panic_buffers) or the idle path
    #                                     (platform_cache_idle_enter, from cpu_idle)       yes
    #     bcopy                      29    yes: PE_init_platform (6) and arm_vm_init (1)    **no**
    #
    # `PE_init_platform` stays unusable to the end of this walk, and it is the largest of the thirteen
    # by a wide margin: the `(TRUE, ...)` call is the one that reaches `pe_arm_init_interrupts`
    # (`80004808`), which maps the interrupt controller and reads the device tree for it. It is the
    # single most suspicious step in the interval and it is the one step a name-based probe cannot
    # reach - which is the argument for the caller-selective trampoline sketched above, if the bisect
    # lands on it.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=clean_dcache`.** Of the thirteen
    # this is the seventh - the closest thing to a midpoint - and it is the one that answers the
    # question the whole bisect exists to answer: `clean_dcache` at 800042dc is the body's last call,
    # four instructions before the `pop`, so **a report proves the entire body of
    # `cpu_machine_idle_init` ran**, and a silence puts the stop inside it. Nothing reaches
    # `clean_dcache` through any other door: its only other reference is at `80036eb8`, eight bytes
    # into `clean_dcache64`, and `clean_dcache64` has no callers.
    #
    # The three arguments are all firm, because the three instructions that set them are the three
    # immediately above the call:
    #
    #   - `cp_arg0 = 0x80102000`. `mov r0, r4` at 800042d8, and r4 is `[TPIDRPRW + 0x5bc]` - the
    #     per-CPU `CpuData *` that arm_init wrote at 80003054 with `str r5, [r0, #1468]` where
    #     r5 was 0x80102000. This value is also what the `cmp r4, r0` at 800042a8 tests against, so
    #     the prediction is checkable against the gate as well.
    #   - `cp_arg1 = 0x00000330`: `mov r1, #816` at 800042cc.
    #   - `cp_arg2 = 0x00000000`: `mov r2, #0` at 800042d0.
    #
    #   - **A report** narrows the stop to the six ladder calls, three of which (`get_mmu_control`,
    #     `set_mmu_control` and the `PE_init_platform` that owns the interval's most suspicious
    #     work) sit in one contiguous stretch, and the next probe is `cpu_timebase_init`
    #     (`800032a4`, `caller_v` `0x800032a8`), which splits that stretch in two.
    #   - **A silence** puts it in the body tail, and the next probe is `ml_static_vtop`
    #     (`caller_v` `0x8000424c`), which is provably clean and which then leaves only
    #     `CleanPoC_DcacheRegion` and `clean_dcache` itself below it.
    #
    # The prediction: **a report.** The body tail is five calls of `ml_static_vtop` and `bcopy_phys` -
    # both already exercised this run and in the ten runs before it - plus one cache region clean of
    # 4 KB and one of 0x330 bytes. The six ladder calls, by contrast, include the first
    # `PE_init_platform(TRUE, ...)` this kernel has ever run, whose `pe_arm_init_interrupts` maps
    # hardware from a device tree this project writes by hand.
    #
    # **`STAGE90_ENTRY_CHECKPOINT=clean_dcache` was SILENT** - 294042 bytes, the size every silent log
    # in this walk has had, with no `stub_hit`, no `cp_arg*`, no `exception:` and none of the
    # epilogue's own records. Read back before the run and again after: the image's only two
    # references to the name are `bl __wrap_clean_dcache` at 800042dc and a `b` at 80036eb8, and the
    # second is dead code - `clean_dcache64` is twelve bytes of `mov r1,r2 / mov r2,r3 / b
    # clean_dcache` with **no callers at all**, and the `.word` slot `L_clean_dcache` at 80036f10,
    # which also points at the wrapper, is read by nothing (a literal-pool scan of the whole image
    # finds readers for exactly two slots in that island, `L_gPhysBase` and `L_gVirtBase`, both read
    # by the `L_cond_extern_347_shim`). Image bytes 1115688 and text 1014800 in **both** the
    # `bcopy_phys` and the `clean_dcache` builds, so this is a controlled A/B: same image size, same
    # bss, one call site redirected, and the outcome flips from a report to no output at all.
    #
    # The stop is therefore in `[80004240, 800042dc)`. And that interval turned out to contain
    # something this walk had not seen, because reading the two `bcopy_phys` calls as *writes* rather
    # than as measurement points gives a second, independent explanation of the silence:
    #
    #     cpu.c:533  bcopy_phys(vtop(&BootArgs_paddr),     gPhysBase +
    #                                   (unsigned)&ResetHandlerData.boot_args
    #                                 - (unsigned)&ExceptionLowVectorsBase, 4)
    #                bcopy_phys(vtop(&CpuDataEntries_paddr), gPhysBase +
    #                                   (unsigned)&ResetHandlerData.cpu_data_entries
    #                                 - (unsigned)&ExceptionLowVectorsBase, 4)
    #
    # In this image the link resolves that arithmetic to a destination this project owns:
    #
    #     ExceptionLowVectorsBase = 0x800dc4bc   ResetHandlerData = 0x800de8bc
    #     delta = 0x2400, and gPhysBase = 0x80000000 (measured: cp_arg2 at 8000423c is
    #     0x2400 + 8 + gPhysBase)
    #     -> the two writes land at **0x80002404 and 0x80002408**
    #
    # and those two addresses are, in this image, **inside `entry_epilogue` at 0x80002348** - the
    # function that writes every log line this walk has ever read. `nm` between 0x80002280 and
    # 0x80002600 finds exactly one symbol, `entry_epilogue`, so there is no doubt about the
    # enclosure. The two instructions destroyed are
    #
    #     80002404  rsb r5, r5, #32      ; the set-index shift, replaced by `andshi r1, r0, r0`
    #     80002408  mov lr, #0           ; the way counter, replaced by `andshi r7, r4, r0`
    #
    # because the values written are *addresses* - `[0x80114844]` = `CpuDataEntries_paddr` =
    # 0x80101000 and `[0x80114840]` = `BootArgs_paddr` = 0x80147000, both measured, the second one
    # being the payload's own `xnu_entry_args_pa` - and an address decoded as an ARM data-processing
    # instruction with `cond=HI` sets neither `r5` nor `lr`. The set/way D-cache flush loop that
    # follows (`lsl r1, lr, r5 / orr r2, r1, r3, lsl r0 / mcr 15,0,r2,cr7,cr14,{2}`, outer loop
    # `cmp r6, lr / add lr, lr, #1 / bne`) then counts ways from whatever `lr` already held, which is
    # bounded but astronomically long, so `entry_epilogue` never reaches its ram-console write and
    # the log stays empty. **A hang inside the reporting path looks exactly like a hang in the boot.**
    #
    # This is a *data*-dependent self-modification of this project's own code by XNU, on a boot path
    # that has been reached in this image and the last one, and it is the first candidate mechanism
    # this walk has found that explains a silence with **no frontier at all**. It also puts two
    # earlier readings back in doubt, because both checkpoints sit after this code in the boot order:
    # `machine_startup`'s silence and `panic`'s silence can no longer be read as "not reached" and
    # "there is no panic" unless this is ruled out first. Nothing is retracted yet - the mechanism is
    # derived, not measured - and the next probe is designed to measure it.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=ml_static_vtop`.** Its first
    # *executed* call site is 80004248 - **above the first clobber at 8000423c and below the second
    # at 80004278** - and it is provably clean: its other eight call sites are in `pmap_create`,
    # `pmap_map_globals` (from `machine_init`), `pmap_pages_alloc`, `pmap_tt1_deallocate` x2,
    # `pmap_tt_deallocate` x2 and `pmap_expand`, every one of them far past `machine_startup`.
    #
    # This makes the probe conclusive in **both** directions, which is the only reason to spend a run
    # on a point twelve instructions past one that already reported:
    #
    #   - **A silence at `caller_v = 0x8000424c` proves the clobber.** Between the site that reported
    #     in the `bcopy_phys` build (8000423c, four bytes of `movw`/`movt` and a call to a function
    #     already proven to return) and 80004248 there is *no code that can hang* - so a silence can
    #     only be the reporting path, and the only new event in between is the first `bcopy_phys`.
    #   - **A report at `caller_v = 0x8000424c`, `cp_arg0 = 0x80101000`, `cp_arg1 = 0x80114844`**
    #     kills the clobber theory outright - 279's own report from `klist_init`, which is downstream
    #     of both writes, is evidence on this side - and puts the stop in
    #     `[8000424c, 800042dc)`: `ml_static_vtop` again, `bcopy_phys` at 80004278,
    #     `CleanPoC_DcacheRegion` at 8000428c, `ml_static_vtop` at 80004298, and the conditional
    #     `bcopy` at 800042c4. The next probe would then be `CleanPoC_DcacheRegion` (`caller_v`
    #     `0x80004290`, `cp_arg0` the value of `gVirtBase` and `cp_arg1 = 0x00001000`).
    #
    # The two arguments are read off the two instructions above the call: `80004240: movw r0,#0x1000`
    # and `80004244: movt r0,#0x8010`, so `cp_arg0 = 0x80101000`; `8000424c`'s `movw r1,#0x4844 /
    # movt r1,#0x8011` is *after* the call, so `cp_arg1` is whatever the caller of `arm_init` left -
    # and the value measured at the same key in the `bcopy_phys` report was 0x80114844, which is the
    # same register pair, so it is stated as a measurement and not as a prediction.
    #
    # The prediction: **a report.** 279 reported from `klist_init`, which runs after both writes, in
    # an image whose first 0xbbbc0 bytes are instruction-identical to this one; on that evidence the
    # clobber is survivable and the silence is a real stop. The clobber arithmetic is written down in
    # full above precisely because that prediction could be wrong, and because if it is wrong this is
    # the mechanism that was hiding under it.
    #
    # **The run reported, and it refuted the caller prediction while confirming the argument.**
    # `stub_hit=ml_static_vtop` at **`caller_v=0x800041e0`** - not the 0x8000424c written above. The
    # prediction was wrong because the probe was mis-chosen: `ml_static_vtop` is called **eight times
    # inside the body**, and its first executed site is 800041dc (`caller_v` 800041e0), one call
    # *above* the 80004248 site the prediction pointed at. The two sites skipped are 80004140 and
    # 80004150, inside the `wfi` block that the `bcopy_phys` run had already proved is jumped over.
    # So this run measured "the body reaches 800041dc" - which the `bcopy_phys` report at 80004240
    # already implied - and it did **not** test the clobber at all, because 800041dc is above it.
    # The argument prediction did hold, and it is the firmest one this walk has had:
    # `cp_arg0=0x80000008` is exactly the `movw r0,#8 / movt r0,#0x8000` two instructions up, i.e.
    # `ml_static_vtop((vm_offset_t)&start_cpu)`, and `start_cpu` is linked at 0x80000008. `cp_arg1`
    # 0x800dd4bc and `cp_arg3` 0xe30406d8 are leftovers, recorded and not interpreted. Log 301195.
    #
    # **A rule this produces, worth more than the run: `ml_static_vtop`, `bcopy_phys`, `bcopy` and
    # `PE_parse_boot_argn` are all called *earlier in the body* than the frontier they were meant to
    # probe, so no wrap on them can ever report from a late site.** The instrument redirects every
    # reference, and the report comes from whichever executes first. In `cpu_machine_idle_init`'s
    # body exactly **two** names have their first executed site in the dark half:
    # `CleanPoC_DcacheRegion` (8000428c, its other callers being `ml_arm_sleep`, `clean_dcache` and
    # the idle path) and `clean_dcache` (800042dc, its other reference being dead). Every other call
    # in the body is unusable as a late probe no matter how clean it looks.
    #
    # ## A defect found on the way, and the pad that removes it
    #
    # Reading the two `bcopy_phys` calls as *writes* rather than as probe points gives a second,
    # independent candidate mechanism for a silence, and it is in this project's own code:
    #
    #     cpu.c:533  bcopy_phys(vtop(&BootArgs_paddr),       gPhysBase + &ResetHandlerData.boot_args
    #                                                        - &ExceptionLowVectorsBase, 4)
    #                bcopy_phys(vtop(&CpuDataEntries_paddr), gPhysBase + &ResetHandlerData.cpu_data_entries
    #                                                        - &ExceptionLowVectorsBase, 4)
    #
    # with `ExceptionLowVectorsBase = 0x800dc4bc`, `ResetHandlerData = 0x800de8bc` (delta 0x2400) and
    # `gPhysBase = 0x80000000` (measured: `cp_arg2` at 8000423c was `0x2400 + 8 + gPhysBase`). The
    # arithmetic assumes the vectors blob is linked at the kernel's physical base - true in Apple's
    # own armv7 link and false here - so the two writes land at **0x80002404 and 0x80002408**, and
    # `nm` between 0x80002280 and 0x80002600 finds exactly one symbol there, **`entry_epilogue`** at
    # 0x80002348, on the instructions `rsb r5, r5, #32` (the set shift) and `mov lr, #0` (the way
    # counter) of its set/way D-cache sweep. The values written are addresses (0x80101000 and
    # 0x80147000 - the second is this run's own `xnu_entry_args_pa`), and an address decoded as an
    # ARM data-processing instruction with `cond=HI` sets neither register, so `way` would never be
    # zeroed and the way loop would count from whatever `lr` held.
    #
    # **A pad now covers those eight bytes, in `entry_stubs.c`, immediately before the code that was
    # at 0x80002404** - 32 `nop`s, emitted where the old addresses were, so the pad occupies exactly
    # the two addresses XNU writes and the sweep moves up behind it. NOPs are the right content
    # because a NOP overwritten by an `ands` is still a NOP. Verified in the linked image:
    #
    #     80002404: e320f000  nop {0}        (was: rsb r5, r5, #32)
    #     80002408: e320f000  nop {0}        (was: mov lr, #0)
    #
    # ## The control that says the clobber was not the cause
    #
    # **The same `--allow-xnu-entry` run, plain, with the pad in place and no checkpoint, is still
    # silent** - 294042 bytes, ending at the jump line. So the eight bytes were a real defect and are
    # worth having fixed, but they are **not** what silences this configuration: the plain 280 run
    # stops for its original reason, which is still unfound.
    #
    # What that control does buy is the removal of a confound. Before it, every checkpoint at or
    # after 8000423c was unreadable - "silent" could have meant "not reached" *or* "the report path
    # was broken by the write". The pad separates those two, and every silence from here on is a
    # measurement again.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=CleanPoC_DcacheRegion`**, which
    # is one of the two usable names in the dark half. In the padded image the addresses have all
    # moved by the pad's 0x80: `arm_init` 0x80003008, `cpu_machine_idle_init` 0x80004110, and the
    # site this probe reports from is
    #
    #     800042fc: movw r0, #0x6c50   80004300: mov r1, #4096   80004304: movt r0, #0x8011
    #     80004308: ldr r0, [r0]      8000430c: bl __wrap_CleanPoC_DcacheRegion
    #
    # so `caller_v = 0x80004310`. The other eight sites are `ml_arm_sleep`'s (0x800040d8, the idle
    # path), three inside `clean_dcache` (0x800370e4, 0x8003714c, 0x80037188 - reachable only through
    # 0x8000435c), three inside `dcache_incoherent_io_flush64` / `_store64` (0x80037570, 0x80037640,
    # 0x800376bc - both functions have zero callers) and one in `platform_cache_idle_enter`
    # (0x80037908, from `cpu_idle`). None can run before the frontier.
    #
    #   - `cp_arg0` = the value of **`gVirtBase`** at 0x80116c50, predicted **0x80000000**. This is a
    #     derived prediction and not a guess: `ml_static_vtop(0x80114840)` returned 0x80114840 and
    #     `cp_arg2` at 8000423c fixed `gPhysBase = 0x80000000`, and `ml_static_vtop` is `v - gVirtBase
    #     + gPhysBase` on a static address - identity only if the two are equal.
    #   - `cp_arg1 = 0x00001000`, from the `mov r1, #4096` at 80004300.
    #   - `cp_arg2`, `cp_arg3`: leftovers.
    #   - **A report** cuts the remaining window to `[0x80004310, 0x8000435c)`: this call's own body,
    #     `ml_static_vtop` at 0x80004318, the conditional `bcopy` at 0x80004344, and the
    #     `clean_dcache` at 0x8000435c that the unpadded run said is not reached.
    #   - **A silence** puts the stop in `[0x800042c0, 0x8000430c)` - `ml_static_vtop` at 0x800042c8
    #     and 0x800042dc, and `bcopy_phys` at 0x800042f8 - three calls to two functions that have
    #     both already returned in this image, which would be a result worth having.
    #
    # The prediction: **a report.** The clobber is out of the way, so a silence here would have no
    # mechanism behind it that this walk has not already excluded.
    #
    # **`STAGE90_ENTRY_CHECKPOINT=CleanPoC_DcacheRegion` was SILENT** - 294042 bytes again, in the
    # padded image, with the wrap verified at 0x8000430c before the run. So the run does not reach
    # `CleanPoC_DcacheRegion`, and combined with the point the `bcopy_phys` build measured
    # (0x8000423c unpadded = 0x800042bc padded, reached) the stop is inside
    #
    #     0x800042bc  bcopy_phys        src = vtop(&BootArgs_paddr) = 0x80114840,
    #                                   dst = gPhysBase + 0x2408 = 0x80002408, len 4
    #     0x800042c8  ml_static_vtop(0x80101000)      -> [0x80114844] = CpuDataEntries_paddr
    #     0x800042dc  ml_static_vtop(0x80114844)
    #     0x800042f8  bcopy_phys        src = vtop(&CpuDataEntries_paddr) = 0x80114844,
    #                                   dst = gPhysBase + 0x2404 = 0x80002404, len 4
    #
    # **Two of those three are now excluded by reading them rather than by running them.**
    # `ml_static_vtop` is twelve instructions long (0x80007f08..0x80007f5c):
    #
    #     80007f20: ldr r0, [&gVirtBase]      ldr r1, [&gPhysSize]
    #     80007f28: sub r0, r4, r0            ; arg - gVirtBase
    #     80007f2c: cmp r0, r1
    #     80007f30: bcc 80007f4c              ; in range -> fall through to the identity add
    #     80007f40: bl  panic                 ; OUT OF RANGE -> panic  **and a panic is silent**
    #     80007f4c: ... add r0, r0, [&gPhysBase] ... pop {r4,r5,fp,pc}
    #
    # There is no loop in it: it either adds or panics. So it hangs only by panicking, and the panic
    # needs `arg - gVirtBase >= gPhysSize`. **That cannot happen for either call here**, because the
    # `bcopy_phys` checkpoint build reported from 0x80004240 - past call 3 of the sequence, which is
    # `ml_static_vtop(0x80147000)` with `arg - gVirtBase = 0x147000` - so `gPhysSize > 0x147000` was
    # already established, and 0x101000 and 0x114844 are both smaller. The bound is measured, not
    # assumed, and it is what makes the next probe unambiguous.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=bcopy_phys` with
    # `STAGE90_ENTRY_CHECKPOINT_SKIP=1`** - the new skipped variant of the instrument, which passes
    # the first call through to the real function and reports on the second. Call 1 is the site at
    # 0x800042bc, whose *call site* is already known to be reached; call 2 is 0x800042f8.
    #
    #   - **A silence proves the stop is inside `bcopy_phys`.** With call 1 going through for real,
    #     the only calls between the last measured point and call 2 are the two `ml_static_vtop`s,
    #     both excluded above - so a silence cannot be anything but the body of the first
    #     `bcopy_phys`, and the frontier stops being a missing symbol and becomes a real XNU
    #     function that does not return.
    #   - **A report at `caller_v = 0x800042fc`, `cp_calls = 0x00000002`** proves call 1 returned and
    #     puts the stop inside the second `bcopy_phys` - the one whose destination is 0x80002404,
    #     the other half of the pair the pad was built for.
    #
    # The prediction: **a silence**, and the reason is in `bcopy_phys`'s own body (0x80035ff0), which
    # is not the `memcpy` its name suggests:
    #
    #     80036010: bl pmap_cache_attributes(page_of_src)
    #     80036024: bl pmap_cache_attributes(page_of_dst)
    #     80036038..0x8003604c: bl mmu_kvtop_wpreflight(dst - gPhysSize + gVirtBase)
    #                              ^ for our identity-mapped image that argument is 0x7F802408 -
    #                                *below* the kernel window - so the lookup fails and the fast
    #                                path at 0x80036098 (`b bcopy` with both addresses translated)
    #                                is not taken
    #     800360a4..0x800360b4: the page-boundary check, with `bl panic` at 0x800360c0 if it fails
    #     800360c4: _disable_preemption / cpu_number / pmap_map_cpu_windows_copy /
    #               pmap_cpu_windows_copy_addr - the per-CPU pmap window path
    #
    # So the first `bcopy_phys` of this boot does not copy four bytes; it takes the per-CPU window
    # path, which nothing in this walk has ever executed, and it contains a `panic` whose output this
    # image cannot produce. Both a panic and a hang are the same measurement here, and this is the
    # first time the walk has had a candidate that could be either.
    #
    # **The run was SILENT, and the prediction held.** `STAGE90_ENTRY_CHECKPOINT=bcopy_phys` with
    # `SKIP=1`, built and run: the first call went through to the real `bcopy_phys` and the second
    # was never reached. 294042 bytes, no `stub_hit`, no `cp_calls`, no `exception:`.
    #
    # That makes the conclusion a deduction rather than a reading, and it is worth setting out in
    # full because it is the first frontier this walk has found that is not a missing symbol:
    #
    #     1. the call site at 0x800042bc is reached - measured by the `bcopy_phys` checkpoint
    #        reporting `caller_v = 0x80004240` on the *unpadded* 282g image, where that same call
    #        site is 0x8000423c (the 0x80 between the two is the NOP pad, verified on both sides);
    #     2. the call site at 0x800042f8 is not - measured by this run's silence, in which call 1
    #        ran and call 2 was never reached;
    #     3. the only two calls in between are `ml_static_vtop(0x80101000)` at 0x800042c8 and
    #        `ml_static_vtop(0x80114844)` at 0x800042dc - and `ml_static_vtop` cannot stop. Its body
    #        is twelve instructions with no loop, and its only other exit is `panic`, which needs
    #        `arg - gVirtBase >= gPhysSize`. `gPhysSize` is `args->memSize` (`arm_vm_init.c:353`) -
    #        this project's `STAGE90_XNU_ENTRY_SIZE`, 8 MB - and the two arguments would need
    #        0x101000 and 0x114844, both smaller than the 0x147000 the earlier `ml_static_vtop
    #        (BootArgs)` call already carried past the same check;
    #
    #     therefore: **the stop is inside `bcopy_phys`'s first call.**
    #
    # ## What `bcopy_phys` actually does, from `loose_ends.c:58`
    #
    # It is not a `memcpy`, and the two paths it can take are the fork the next probe is aimed at:
    #
    #     wimg_bits_src = pmap_cache_attributes(src >> PAGE_SHIFT);   /* 0x80036010 */
    #     wimg_bits_dst = pmap_cache_attributes(dst >> PAGE_SHIFT);   /* 0x80036024 */
    #     if (mmu_kvtop_wpreflight(phystokv(dst)) &&                  /* 0x8003604c */
    #         (wimg_bits_src & VM_WIMG_MASK) == VM_WIMG_DEFAULT &&
    #         (wimg_bits_dst & VM_WIMG_MASK) == VM_WIMG_DEFAULT) {
    #         bcopy(phystokv(src), phystokv(dst), bytes);             /* 0x80036098, tail */
    #         return;
    #     }
    #     ... the per-CPU copy-window path: mp_disable_preemption, cpu_number,
    #         pmap_map_cpu_windows_copy x2, pmap_cpu_windows_copy_addr x2, bcopy,
    #         pmap_unmap_cpu_windows_copy x2, mp_enable_preemption
    #
    # `mmu_kvtop_wpreflight` has **exactly one call site in this image** - 0x8003604c, this one -
    # which makes it the cleanest probe the walk has ever had, and `phystokv(dst)` is 0x80002408, a
    # kernel VA inside the image, so whether it succeeds depends on that page being mapped writable
    # by the entry window. The two attribute words decide the rest.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=pmap_map_cpu_windows_copy`.**
    #
    # Predicted in symbol-relative terms, because addresses drift between checkpoint builds and one
    # pair of them is already known to have been mis-recorded because of it: the 32-NOP pad moved the
    # whole of `cpu_machine_idle_init` by exactly 0x80 (282g's unpadded first `bcopy_phys` call site
    # 0x8000423c is this build's 0x800042bc, and 282i's unpadded `ml_static_vtop` report
    # `caller_v = 0x800041e0` is this build's 0x8000425c, the `ml_static_vtop(&start_cpu)` call -
    # both confirmed against the disassembly of the *padded* image). So: **a report is expected at
    # `caller_v = bcopy_phys + 0xf0`**, the return address of the call at `bcopy_phys + 0xec`, which
    # is this build's 0x800360e0.
    #
    # The site census was measured, not assumed: `__wrap_pmap_map_cpu_windows_copy` has 25 call
    # sites in this image and they are exactly - `bcopy_phys` 2 (0x800360dc, 0x800360f0),
    # `bzero_phys` 1, the twenty `ml_phys_read/write*` wrappers 20, and the two functions this walk
    # already knows are dead (`dcache_incoherent_io_flush64`, `dcache_incoherent_io_store64`, zero
    # textual references) 2. The two `bcopy_phys` sites are the first in the image and the only two
    # this boot can reach: `bcopy_phys` is the frontier.
    #
    # So a report here is not just a position, it is a *verdict* on which of the two paths
    # `bcopy_phys` took - and it is worth setting out because the branch is not the one this ledger
    # first assumed. **The earlier claim that this configuration's `SO_ONLY` entry window forces the
    # slow path is wrong, and is left standing above only as the refuted prediction it is.** The
    # mechanism: `pmap_cache_attributes` (pmap.c:8364) does not read the hardware descriptors at all.
    # It reads XNU's own software tables, and the only lines that matter at this point in the boot
    # are
    #
    #     if ((paddr >= io_rgn_start) && (paddr < io_rgn_end)) return IO_ATTR... or VM_WIMG_IO;
    #     if (!pmap_initialized) {
    #         if ((paddr >= gPhysBase) && (paddr < gPhysBase + gPhysSize)) return VM_WIMG_DEFAULT;
    #         else return VM_WIMG_IO;
    #     }
    #
    # `pmap_initialized` is FALSE here, and that is a measured-layout fact rather than a reading:
    # `pmap_init` has exactly ONE call site in this image - `80040970: bl pmap_init`, inside
    # `vm_mem_bootstrap + 0x134` - and `vm_mem_bootstrap` is reached through `kernel_bootstrap`,
    # which is reached through `machine_startup` (arm_init.c:437), which is 61 lines *below* the
    # `cpu_machine_idle_init(TRUE)` call at arm_init.c:376 that this whole frontier sits inside. And
    # the two page numbers are both DRAM: `pn_src = 0x80114`, `pn_dst = 0x80002`, both inside
    # `[gPhysBase, gPhysBase + gPhysSize) = [0x80000000, 0x80800000)` with `gPhysSize` = the 8 MB this
    # project passes. Neither is in an I/O range: `io_rgn_start`/`io_rgn_end` are 0/0 (pmap.c:351-352)
    # unless the device tree has a `pmap-io-ranges` property, and if it does, the ranges are the
    # peripheral windows, not DRAM.
    #
    # **Both attribute words are therefore `VM_WIMG_DEFAULT` (= `VM_MEM_COHERENT` = 0x2) with the
    # fast path's second and third conditions true by construction.** The entire decision collapses
    # onto the first condition, `mmu_kvtop_wpreflight(phystokv(dst))`, and `phystokv` is the identity
    # here (`gVirtBase == gPhysBase`, measured - see above), so the address it asks about is
    # `0x80002408` itself: a kernel VA in this image's own text, four bytes of `entry_epilogue`.
    # `mmu_kvtop_wpreflight` (machine_routines_asm.s:476) is `mcr p15,0,r1,c7,c8,1` / `mrc
    # p15,0,r0,c7,c4,0` - it asks the *hardware*, and what the hardware answers depends on the
    # descriptor the payload built, not on XNU's tables. That descriptor is `stages/stage85/mmu.c:8`,
    # `L1_DESC_SECTION_SO = 0x00010c02`: bits[11:10] = 0b11, "domain 0, **AP full access**", S=1,
    # XN=0. A privileged write to a full-access section in domain 0 translates, so PAR reports no
    # abort, `bics r0, r0, r2` leaves the section base 0x80000000 (nonzero, so the sanity check
    # passes), and the preflight returns **nonzero**.
    #
    # **The prediction: SILENCE.** With the preflight succeeding and both attribute words DEFAULT, the
    # fast path is taken, `bcopy(0x80114840, 0x80002408, 4)` runs, `bcopy_phys` returns, and
    # `pmap_map_cpu_windows_copy` is never called - the copy-window machinery stays unreachable and
    # this probe reports nothing. **A report would refute one of the four measured links above**, and
    # the most valuable one it could refute is the last: it would mean the descriptor the hardware
    # holds at 0x80002408 is not `L1_DESC_SECTION_SO` - the section is not mapped there at all, or it
    # is mapped read-only - which is a fact about the *live* mapping that no probe in this walk has
    # ever been able to see, since `mmu_kvtop_wpreflight` is the only instrument in the image that
    # asks the hardware and this is the first time it has ever been reached. In that case `cp_arg2`
    # would carry `wimg_bits_src` = 0x00000002 as the proof that the collapse above was right and the
    # descriptor was wrong.
    #
    # **And whichever way it goes, it closes an assumption the ledger has been carrying unexamined
    # since the pad.** The pad replaced `rsb r5, r5, #32` and `mov lr, #0` at 0x80002404/0x80002408
    # with NOPs *because* this `bcopy_phys` writes 4 bytes to 0x80002408. If the fast path is what
    # runs, then that write is the first thing the boot does to those addresses - and the plain
    # padded control (282j) was still silent, which places the plain run's stop *at or after* the end
    # of the first `bcopy_phys`, not before it. Every silence this session has been reading as "the
    # frontier is the call site at the top of the interval" is therefore compatible with a stop
    # anywhere in the interval, and the probes that were silent for other reasons - `clean_dcache`,
    # `CleanPoC_DcacheRegion`, `machine_startup`, `panic` - are the ones that need re-reading on the
    # padded image, not re-running.
    #
    # ## The run: SILENT at 294042 bytes, and the fast path is confirmed - with a caveat that matters
    #
    # `STAGE90_ENTRY_CHECKPOINT=pmap_map_cpu_windows_copy`, built, byte-verified (the entry image is
    # embedded exactly once in the payload, at 0x7868c, and `xnu_entry_image_bytes` still reports
    # 0x00110628), and run: zero lines matching `stub_hit=`, `cp_arg`, `cp_calls` or `exception:`,
    # and the log ends where every silent log ends, at `jumping to XNU's _start`. Safety counters
    # clean on all 25 and all 87: `persistent_write_attempted=0x00000000`, `failure_mask=0x00000000`,
    # `xnu_entry_failures=0x00000000`, no abort outside the deliberate 0xdeadc000 self-test, and the
    # device returned to Android by itself.
    #
    # **So `pmap_map_cpu_windows_copy` is not called, which means the copy-window path is not taken,
    # which means `mmu_kvtop_wpreflight` succeeded.** But the caveat is the whole value of this run:
    # a silence only says the *report* did not happen. It is equally consistent with the fast path
    # running, with the preflight returning zero and the slow path dying before its first call, and
    # with the stop being inside one of the two `pmap_cache_attributes` calls - three different
    # worlds. What the run *does* establish is the negative it was built for: nothing this boot does
    # reaches the copy-window machinery, so `pmap_map_cpu_windows_copy`, `pmap_cpu_windows_copy_addr`
    # and `pmap_unmap_cpu_windows_copy` - 25 contamination-prone sites each - are off the table and
    # can be retired from the candidate list rather than probed.
    #
    # ## Bisecting the inside of `bcopy_phys`, and why it is now cheap
    #
    # The interval is `bcopy_phys`'s own body: from the first `pmap_cache_attributes` call at
    # 0x80036010 to the copy-window call at 0x800360dc. The wrappers that made this body unreadable
    # in a bare walk are exactly the problem `--wrap` has with any well-connected symbol - but the
    # site census fixes that, and it was measured rather than guessed:
    #
    #     pmap_cache_attributes   (29 sites)  2 bcopy_phys, 2 copypv, 1 bzero_phys,
    #                                         20 ml_phys_read/write*, 1 kdp_find_phys,
    #                                         2+1 in the two dead dcache_incoherent_io_* functions
    #     mmu_kvtop_wpreflight     (1 site)   bcopy_phys+0x4c, and nowhere else in the image
    #     bcopy                   (29 sites)  already executed twice, at 0x8000423c and 0x80004250
    #
    # Two of those need no `SKIP` at all. **`pmap_cache_attributes`'s first reached site is inside
    # `bcopy_phys`** - the only other caller that is not either dead (`dcache_incoherent_io_*`), the
    # KDP debugger (`kdp_find_phys`), or a `ml_phys_*`/`bzero_phys`/`copypv` function this boot has
    # never entered - so a plain terminal checkpoint there fires at 0x80036010 and at nothing else.
    # And `mmu_kvtop_wpreflight` has one site in the entire image.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=mmu_kvtop_wpreflight`.** It is
    # the higher-information of the two, because it fires *after* both attribute calls and so splits
    # the interval at its middle, and because the two registers it reads are two measurements this
    # project has been carrying as owed:
    #
    #   - `cp_arg0` is `phystokv(dst)` - the arithmetic at 0x80036038-0x80036048 is `ldr r0,[gPhysBase]
    #     / ldr r1,[gVirtBase] / sub r0, r4, r0 / add r0, r0, r1`, with `r4 = dst` from the caller. So
    #     `cp_arg0 = 0x80002408 + (gVirtBase - gPhysBase)`: **0x80002408 means the two bases are
    #     equal**, and anything else measures their difference directly.
    #   - `cp_arg1` is `gVirtBase` itself, loaded at 0x80036040 and never overwritten before the call.
    #     **The prediction is `0x80000000`** - and this is the one place where the dryrun logs and the
    #     identity argument can be checked against each other, because those logs report
    #     `stage90_xnu_pmap_gVirtBase=0x80008000` while `ml_static_vtop`'s measured identity on
    #     `&BootArgs_paddr` says the two bases are equal. The resolution is that the dryrun value is
    #     `contract->proposed_virtBase` (`xnu_pmap_bootstrap_contract.c:253`) - a *proposal* the
    #     contract grades itself against, not a reading of XNU's global - so the prediction stands at
    #     0x80000000 and a report of 0x80008000 would be a genuine surprise of the best kind.
    #
    # So: **a report at `caller_v = bcopy_phys + 0x50`** (this build 0x80036050, the return of the
    # call at 0x8003604c) with `cp_arg0 = 0x80002408` and `cp_arg1 = 0x80000000`. That report says
    # both `pmap_cache_attributes` calls returned and the preflight was entered, leaving exactly two
    # places the stop can be: inside the preflight's own eight instructions, or at or past the fast
    # path's `bcopy` - and those two are told apart by the same probe run again with
    # `STAGE90_ENTRY_CHECKPOINT=bcopy STAGE90_ENTRY_CHECKPOINT_SKIP=2`, since exactly two `bcopy`
    # calls (the two exception-vector copies at 0x8000423c and 0x80004250) are known to have returned
    # before the frontier. **A silence means the stop is in `pmap_cache_attributes`** - the first
    # XNU pmap software-table read this boot has ever made - and the probe for that is the same
    # symbol without `SKIP`.
    #
    # ## The run: REPORTED, every key predicted to the digit, and one of them for free
    #
    # `STAGE90_ENTRY_CHECKPOINT=mmu_kvtop_wpreflight`, built (one site in the whole image, at
    # 0x8003604c; the layout did not move, so the predicted `caller_v` was exact), byte-verified,
    # run: **301201 bytes and `stub_hit=mmu_kvtop_wpreflight`**, at
    #
    #     cp_arg0=0x80002408   cp_arg1=0x80000000   cp_arg2=0x80800000   cp_arg3=0x00000000
    #     xnu_entry_stub_caller=0x80036050  (= bcopy_phys + 0x50, predicted)
    #
    # `cp_arg0` and `cp_arg1` are the two predictions, and both landed:
    #
    #   - `cp_arg1 = 0x80000000` **is `gVirtBase`, read out of the global at 0x80036040**. This is a
    #     direct measurement of a value the walk has been inferring, and it settles the conflict with
    #     the dryrun logs: they report `stage90_xnu_pmap_gVirtBase=0x80008000` because that field is
    #     assigned `contract->proposed_virtBase` (`xnu_pmap_bootstrap_contract.c:253`) - a proposal a
    #     contract grades itself against - and not because XNU's global holds it. Second instance this
    #     session of the project's "a measurement can be the thing that is wrong" class, after the
    #     `SO_ONLY` reasoning that the last run refuted.
    #   - `cp_arg0 = 0x80002408` is `phystokv(dst)`, so `gVirtBase - gPhysBase == 0` and the
    #     `ml_static_vtop` identity on `&BootArgs_paddr` is confirmed by a second, independent route.
    #
    # `cp_arg2 = 0x80800000` was not predicted and is worth more than it looks: that is
    # `gPhysBase + gPhysSize` = 0x80000000 + 8 MB, sitting in a register at a call site that never
    # computes it. It is left there by `pmap_cache_attributes`, which reads `gPhysBase+gPhysSize` to
    # decide its return value and then returns early - so the register's contents are **a
    # measurement of which branch of `pmap_cache_attributes` ran**: the `!pmap_initialized` branch
    # that returns `VM_WIMG_DEFAULT`, exactly as the collapse above predicted, times two calls.
    #
    # Safety counters clean on the same terms as every run before it: 25 ×
    # `persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`,
    # `xnu_entry_failures=0x00000000`, no abort outside the deliberate 0xdeadc000 self-test, device
    # returned to Android on its own.
    #
    # ## Where that leaves the frontier
    #
    # Both `pmap_cache_attributes` calls returned, and the preflight was entered. Everything else in
    # `bcopy_phys`'s body before the copy-window call is register arithmetic that cannot stop, and
    # the slow path is excluded by the run before this one (a preflight returning 0 would have taken
    # it and reported at `pmap_map_cpu_windows_copy`; that run was silent). So the stop is in one of
    # exactly two places:
    #
    #     1. inside `mmu_kvtop_wpreflight` - nine instructions, `mrs` / `cpsid if` / `mov` / `mcr
    #        p15,0,r1,c7,c8,1` / `isb` / `mrc p15,0,r0,c7,c4,0` / `ands` / `bne` / `msr`;
    #     2. at or past the fast path's `bcopy(0x80114840, 0x80002408, 4)`.
    #
    # Neither can be settled by wrapping a *call site*: the first is a leaf with one site already
    # used up by the report above, and the second is `bcopy`, whose earlier calls are behind the
    # frontier so a `SKIP` count would have to be guessed at (28 sites across `PE_init_platform`'s
    # six, `arm_vm_init`'s one, and two already known inside `cpu_machine_idle_init`). So this is
    # where the instrument needed to grow rather than the run to be repeated.
    #
    # ## `STAGE90_ENTRY_CHECKPOINT_AFTER=1`, the value variant
    #
    # Same one-symbol wrapper, but it calls the real function, records **`cp_ret`** - what it
    # returned - and only then reports. That turns a single-site function from a position into a
    # value, and it is the capability the walk has been missing since 280: a terminal checkpoint
    # cannot say what a function did, a `SKIP` checkpoint can only say it from a *later* call, and a
    # leaf does not have one. It is in `entry_checkpoint.c`, it is three lines of behaviour over the
    # existing wrapper, and it is off unless asked for. `AFTER` and `SKIP` are mutually exclusive and
    # the build refuses both at once, because they are two answers to one question.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=mmu_kvtop_wpreflight
    # STAGE90_ENTRY_CHECKPOINT_AFTER=1`.**
    #
    #   - **A report** means the preflight *returned*, with `cp_ret` the value. It cannot be 0: a
    #     zero return takes the copy-window path, which the previous run proved is never reported
    #     from. So `cp_ret` is a physical address, and the body's own arithmetic (`ands r2, r0, #0x2`
    #     for the super-section flag, else mask 0xFFF) picks between two forms for the 1 MB section
    #     mapping at 0x80000000: **`0x80000408` if PAR bit 1 is clear** (mask 0xFFF, section base
    #     0x80000000 | VA bits 11:0), or `0x80202408` if the core reports the section through the
    #     super-section branch. The prediction is **`cp_ret = 0x80000408`** - a 1 MB section is not a
    #     16 MB super section, so PAR[1] should be clear - and either way a nonzero `cp_ret` places
    #     the stop in the fast path's four-byte `bcopy`, whose next probe is `bcopy` with a `SKIP`
    #     count that can then be *measured* rather than guessed (the first `bcopy` report gives call
    #     1's `caller_v`, and the count follows from the ladder between it and 0x800042c0).
    #   - **A silence** means the preflight does **not** return - and because the plain checkpoint run
    #     above proved the call site is reached, that is a positive finding, not an absence: the
    #     frontier becomes an instruction rather than a symbol. The one instruction in those nine
    #     with anything to refuse is `mcr p15, 0, r1, c7, c8, 1`, the address-translation op
    #     `ATS1CPW`, which the architecture permits an implementation to leave UNDEFINED - and a
    #     refused UNDEF taken with the payload's vectors is exactly the shape of a silent stop:
    #     nothing reports, because the reporting path is the thing that was never reached.
    #
    # ## The run: REPORTED, and it is the first *value* this walk has ever read out of a function
    #
    # `STAGE90_ENTRY_CHECKPOINT=mmu_kvtop_wpreflight STAGE90_ENTRY_CHECKPOINT_AFTER=1`, built,
    # byte-verified, run: **301220 bytes and `stub_hit=mmu_kvtop_wpreflight`** at
    # `caller_v = 0x80036050` (predicted), with
    #
    #     cp_ret=0x80002408    cp_arg0=0x80002408   cp_arg1=0x80000000
    #     cp_arg2=0x80800000   cp_arg3=0x00000000
    #
    # **The preflight returns.** That is the finding, and everything else follows from it. The
    # prediction's *class* was right and its *detail* was wrong, which is worth recording rather than
    # tidying: the two candidate forms were `0x80000408` (PAR bit 1 clear, mask 0xFFF) and
    # `0x80202408` (super-section branch, mask 0x00FFFFFF), and the measured value is **neither of
    # those two and yet both** - `0x80002408` is what the super-section branch produces for an
    # identity-mapped address (`0x80000000 | 0x80002408 & 0x00FFFFFF`), and it is also what a 4 KB
    # page descriptor would produce for PA 0x80002000 with the same low bits. So the value proves the
    # translation succeeded and returned the identity mapping, and it does *not* distinguish a
    # section from a page - which is exactly the kind of claim this walk must not make from a value
    # that two different descriptors both produce. `cp_arg1 = 0x80000000` is `gVirtBase`, as
    # predicted, and `cp_arg2 = 0x80800000` is `gPhysBase + gPhysSize` left in r2 by
    # `pmap_cache_attributes`'s early return, as the previous run's copy of it was.
    #
    # Safety counters clean as always: 25 × `persistent_write_attempted=0x00000000`, 87 ×
    # `failure_mask=0x00000000`, `xnu_entry_failures=0x00000000`, no abort outside the deliberate
    # 0xdeadc000 self-test, device back in Android on its own.
    #
    # ## What it leaves: two measurements that disagree with the code
    #
    # The preflight returns nonzero, so `bcopy_phys` does not take its copy-window path, so the fast
    # path runs `bcopy(phystokv(src), phystokv(dst), bytes)` = `bcopy(0x80114840, 0x80002408, 4)` -
    # a *tail branch* to `bcopy`, which is `mov r3,r0 / mov r0,r1 / mov r1,r3` falling into
    # `memmove` at 0x800034a4. And that `memmove`, read instruction by instruction for these
    # arguments, is: `cmp r2,#0` taken as nonzero, `cmpne r0,r1` different, non-overlapping,
    # both addresses 4-byte aligned, `cmp r2,#4` not less, `tst r1,#3` clean, `cmp r2,#16` less - so
    # `Llessthan16_aligned`'s `lsl r2,r2,#28` / `msr CPSR_f,r2` selects exactly one `ldr r4,[r1],#4`
    # and one `streq r4,[r0],#4`, then `b Lexit` → `pop {r0,r4,r5,r7,pc}`. **A four-byte copy with no
    # loop, no indirect call, no cache maintenance and no lock cannot hang.**
    #
    # And 282m measured that it does not come back: `bcopy_phys` with `SKIP=1` - call 1 run for real
    # and call 2 reporting when reached - was silent, and that instrument was checked rather than
    # trusted, because a `SKIP` wrapper that pushes a frame would corrupt the callee's *stack*
    # argument (`bytes` is bcopy_phys's fifth). The built wrapper's pass-through is a sibling call -
    # `ldr lr,[sp,#20]` / `add sp,sp,#24` / `b 80035ff0 <bcopy_phys>` - so `sp` is the caller's again
    # before the callee's prologue, `[sp+40]` is the caller's `str r7,[sp]` = 4, and the instrument
    # is sound. The two measurements therefore disagree with the code, and this project has a
    # memory about what that means: **a measurement can be the thing that is wrong** - seven times
    # over - so the next move is to measure *inside* the disagreement rather than to argue about it.
    #
    # ## `AFTER` now composes with `SKIP`
    #
    # `AFTER` alone fires at call 1, and `SKIP` alone reports at call `n+1` *before* running it.
    # Composed, they are the general probe: the first `n` calls pass through untouched and the
    # `n+1`th is **run for real and its return value recorded**. That is what this frontier needs,
    # because "does the fast path's `bcopy` return" is a question about one specific call of a
    # symbol with 28 call sites. The mutual-exclusion guard the last build carried was wrong and is
    # gone; the two variables answer different halves of one question.
    #
    # One limitation, measured in the built image rather than reasoned about, and it is why the
    # obvious probe (`AFTER` on `bcopy_phys`) is the one probe that must not be used:
    # `__wrap_mmu_kvtop_wpreflight` opens with `strd r4, [sp, #-32]!`, so at its `bl` the callee
    # reads its *fifth* argument out of the wrapper's frame. A callee whose arguments fit in r0-r3 is
    # unaffected; `bcopy_phys(addr64_t, addr64_t, vm_size_t)` puts `bytes` on the stack, so under
    # `AFTER` it would read the caller's garbage as `bytes` and copy for gigabytes - turning the
    # experiment into a longer silence. `bcopy` takes three register arguments.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=bcopy
    # STAGE90_ENTRY_CHECKPOINT_SKIP=2 STAGE90_ENTRY_CHECKPOINT_AFTER=1`.** Skipping two is not a
    # guess: `bcopy` has 28 call sites, and the two that have already completed in this boot are the
    # exception-vector copies at 0x8000423c and 0x80004250 - both *before* 0x800042bc, which the
    # 282g run measured as reached, so both returned. Call 3 is the fast path's.
    #
    #   - The report is expected at `cp_calls=3` with `cp_arg0=0x80114840`, `cp_arg1=0x80002408`,
    #     `cp_arg2=0x00000004` and **`cp_ret=0x80002408`** (`memmove` returns its destination). And
    #     the arguments are what make it self-verifying: if the report shows different ones, call 3
    #     was some earlier site (`PE_init_platform` has six), the report says so, and the count is
    #     measured rather than wrong. `caller_v` will be `0x800042c0` either way, because the call is
    #     a tail branch and the wrapper sees `bcopy_phys`'s own return address.
    #   - **A report settles it: the fast path's `bcopy` returns**, so the stop is in
    #     `[0x800042c0, 0x800042f8)` - the second `bcopy_phys`'s *setup* - where the only instructions
    #     are two `ml_static_vtop` calls and three stores, and where the previous `SKIP=1` silence
    #     would then have to be explained rather than believed.
    #   - **A silence means the fast path's `bcopy` is entered and does not return**, which puts the
    #     frontier on a single `ldr`/`str` pair in `memmove` with the two arguments above - and a
    #     finding that strange is exactly why the composed probe reports its arguments: a wrong call
    #     count and a real hang look identical in a silent log, and the arguments tell them apart.
    #
    # ## The run that moved the count: call 3 is `arm_vm_init`'s, not the fast path's
    #
    # `STAGE90_ENTRY_CHECKPOINT=bcopy STAGE90_ENTRY_CHECKPOINT_SKIP=2 STAGE90_ENTRY_CHECKPOINT_AFTER=1`,
    # built, byte-verified, run: **301226 bytes and `stub_hit=bcopy`** at `caller_v = 0x80018600` -
    # `arm_vm_init + 0xb0`, the return of that function's single `bcopy` call - with
    #
    #     cp_calls=0x00000003   cp_ret=0x80304000
    #     cp_arg0=0x80300000    cp_arg1=0x80304000    cp_arg2=0x00004000   cp_arg3=0x80300000
    #
    # So the prediction was wrong in its *identity* and right in its *design*: call 3 is not the fast
    # path, and the log says so by itself, because the arguments name the call. `cp_arg0 = 0x80300000`
    # is `topOfKernelData` and `cp_arg2 = 0x4000` is 16 KB, which is `arm_vm_init` copying its page
    # tables - and it **returned** (`cp_ret` is `memmove`'s destination). What that refutes is the
    # assumption behind `SKIP=2`: that the two calls before the fast path are `cpu_machine_idle_init`'s
    # two exception-vector copies. They are not; there are calls earlier still (in `PE_init_platform`,
    # `PE_init_iokit` or `STRDUP`, whose six-one-one sites the site map lists), which is why this
    # ledger has stopped predicting `SKIP` counts and started reading them.
    #
    # ## The real finding: XNU's own writes corrupt the *report*, and the walk has been bisecting that
    #
    # Going back to the pad's own claim - "NOPs are the right content because a NOP overwritten by an
    # `ands` is still a NOP" - it is false, and false in exactly the way that produced every silence
    # this session has been reading as a hang. A NOP is still a NOP if nothing executes it after the
    # write: true of the *boot* path (the pad is in `entry_epilogue`, which the boot never runs) and
    # exactly false of the *report* path (every report runs through the pad with XNU's data in it).
    # The two writes happen in `cpu_machine_idle_init`'s body; the report happens later; so from that
    # instruction onwards the entry image's own logging path executes `andshi r7, r4, r0` and
    # `andshi r0, r1, r0` in the middle of its set/way sweep. That is why `clean_dcache`,
    # `CleanPoC_DcacheRegion`, `machine_startup` and 280's frontier were all silent, and why the
    # session's careful bisect of `bcopy_phys` kept landing on a four-byte `memmove` that cannot hang.
    #
    # `entry_stubs.c` now restores the pad in the sweep, and it is placed *first* in `entry_epilogue`'s
    # cache block - earlier in the function than the geometry - because the constraint is only that
    # the pad *contain* the two addresses, so code added before it can only push it off them. The
    # build check added with it reads the linked image back and refuses to build if 0x80002404 or
    # 0x80002408 is not a `nop`: the check that did not exist when the pad was added, and whose
    # absence is what made this defect invisible for a whole session of runs.
    #
    # ## The 282t run: the repair builds, the pad is verified, and it is still silent
    #
    # With the repair in place and both addresses verified as `nop`s (0x800023f4..0x80002473 is the
    # pad, so they sit 16 and 12 bytes inside it), the first probe *after* the writes was run:
    # `STAGE90_ENTRY_CHECKPOINT=machine_startup`, plain and terminal. **294042 bytes, no report.**
    #
    # That is a real result and not another instrument failure, and the argument is the same one that
    # excluded the slow path: at the point `machine_startup` would be reported, the repair has already
    # been installed on every path into `entry_epilogue` - the stubs and the `fleh_*` handlers alike,
    # since the repair is inside the epilogue rather than inside `entry_stub_hit`. So the *report*
    # would work, and its absence means the run does not reach `arm_init.c:437`.
    #
    # **It also re-opens `SKIP=1`.** The 282m silence was read as "`bcopy_phys` call 1 does not
    # return", and at the time that call's report would have been the *first* one attempted after the
    # first write - that is, exactly the report the corruption destroys. So the two readings of 282m
    # - "call 1 does not return" and "call 1 returns, call 2 is reached, and call 2's report is
    # corrupted" - were never distinguished, and the repair is what distinguishes them.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=bcopy
    # STAGE90_ENTRY_CHECKPOINT_SKIP=5 STAGE90_ENTRY_CHECKPOINT_AFTER=1`**, on the repaired image.
    #
    # `SKIP=5` is a reading, not a guess: call 3 is `arm_vm_init`'s, and the two calls between it and
    # the fast path are `cpu_machine_idle_init`'s own two exception-vector copies at +0x12c and +0x140,
    # which are *known* to have returned - the 282g run reported from 0x800042bc, which is after them
    # in a straight-line sequence. So call 6 should be the fast path's.
    #
    #   - **A report at `cp_calls=6` with `cp_arg0=0x80114840`, `cp_arg1=0x80002408`,
    #     `cp_arg2=0x00000004` and `cp_ret=0x80002408`** means the fast path's `bcopy` is reached and
    #     returns - and, because the fast path's `bcopy` happens *after* the first write, it also means
    #     the boot survives the writes, which `machine_startup`'s silence just denied. Those two
    #     cannot both be true, so this is the run that says which of the two is wrong.
    #   - **A report with different arguments** names the true call and its caller, and the next build
    #     takes the difference - the same self-correction as this run's.
    #   - **A silence** now means the fast path's `bcopy` is entered and does not return, and the
    #     ambiguity that has been carried since 282m is gone: with the repair in place, a silence is
    #     the boot's and not the instrument's. That would put the frontier on the four instructions of
    #     `Llessthan16_aligned` (`lsl r2,r2,#28` / `msr CPSR_f,r2` / `ldreq r4,[r1],#4` /
    #     `streq r4,[r0],#4`), which no reading of the code can make hang - and the next instrument
    #     for that is not a call-site probe at all, because `bcopy` reaches `memmove` by *falling
    #     through* into it and a `--wrap` cannot rewrite a fall-through.
    #
    # ## The 282u run: `bcopy` call 6, silent, and the call numbering is *verified* this time
    #
    # `STAGE90_ENTRY_CHECKPOINT=bcopy STAGE90_ENTRY_CHECKPOINT_SKIP=5 STAGE90_ENTRY_CHECKPOINT_AFTER=1`
    # was built, verified, and run: **294042 bytes, silent** - the same size as every silent log this
    # walk has taken.
    #
    # Before reading anything into that, the call numbering was re-derived from the *linked image* at
    # this exact configuration rather than from the census that produced the prediction, because the
    # prediction was wrong once already. `cpu_machine_idle_init` is at 0x80004130 and its `bl`s in
    # address order are: `PE_parse_boot_argn` x2, `ml_static_vtop` x2, `bcopy_phys` (the `wfi == 0`
    # branch, not taken), `ml_vtophys`, `ml_io_map`, **`bcopy` at 0x8000425c**, **`bcopy` at
    # 0x80004270** (`cpu.c:566-567`, the two exception-vector copies), `ml_static_vtop` x3,
    # **`bcopy_phys` at 0x800042dc**, `ml_static_vtop` x2, **`bcopy_phys` at 0x80004318**,
    # `CleanPoC_DcacheRegion` at 0x8000432c, `ml_static_vtop`, **`bcopy` at 0x80004364**
    # (`cpu.c:589`, the running-signature copy), `clean_dcache` at 0x8000437c. And inside
    # `bcopy_phys` there are exactly two wrapped `bcopy` references: the fast path's `b __wrap_bcopy`
    # at 0x800360b8 and the slow path's `bl __wrap_bcopy` at 0x80036144.
    #
    # So with call 3 measured as `arm_vm_init`'s by 282s, the dynamic order is: 1 and 2 (the two
    # `PE_init_platform` sites the site map lists), 3 `arm_vm_init`, 4 and 5 the vector copies at
    # 0x8000425c/0x80004270, **6 the fast path of the first `bcopy_phys`**, 7 the fast path of the
    # second, 8 the running-signature copy. Call 6 is what `SKIP=5` reports on, and it is the one the
    # prediction named. Two things follow. The numbering was right. And the 282g report - from
    # 0x80004240 in the unpadded image, which is the call site at 0x8000423c - had already proved
    # calls 4 and 5 returned, so the silence is not the vector copies.
    #
    # ## What the silence is: `r7`, which is live across the pad, measured in the linked image
    #
    # The repair of 282t is in the built image exactly where it was meant to be: the two stores at
    # 0x800023e4/0x800023e8, `r2 = 0xe320f000` and `r3 = 0x80002000` loaded by `movw`/`movt` over the
    # two instructions before them, `dsb sy` / `isb sy` after, and both addresses reading back as
    # `nop`. It ran, and the run was still silent. So the repair did not take effect, and the
    # measurement that says why is in `entry_epilogue`'s own disassembly rather than in the log:
    #
    #     80002358: ldr r7, [r4, #4]        ; prologue, r4 = entry_vectors_stack
    #     ...
    #     800023f4: 32 nops                 ; the pad, at 0x80002404/0x80002408 = the two words XNU writes
    #     ...
    #     80002574: mov r1, r7              ; first use of r7, 0x180 bytes past the pad
    #     800025b8: ldr r7, [r4, #32]       ; and it is not reloaded before then
    #
    # `r7` is a callee-saved register holding a value the epilogue loaded before the pad and does not
    # touch again until well after it, where it is written into the kv buffer, added to as half of a
    # pointer (`800025cc: add r0, r6, r7`, `800025fc: add r0, r7, #4`), and used as the base of a
    # byte-table read. `0x80147000` decodes as `andshi r7, r4, r0` - `ANDS` with `cond=HI`, so it
    # writes r7 exactly when C is set and Z is clear, and leaves the flags changed either way. Whether
    # it fires depends on the flags at that instruction, which depend on the code before the pad,
    # which is why this defect appears and disappears between builds while the code is instruction-
    # identical - the corrupted *values* are addresses, and the addresses are the layout.
    #
    # The mechanism is therefore real and *erratic*, and the repair's failure has to be an I-side
    # effect: the store puts `nop` in the D-cache and `dsb sy` publishes it, but the pad's line may
    # already have been prefetched into the instruction cache carrying XNU's value, and this project
    # has no instrument that can see the I-cache of a running kernel. An instrument whose correctness
    # rests on an unmeasurable cache property is not an instrument, and the third fix is structural
    # instead: **the pad is no longer anything, because it is no longer executed.**
    #
    # ## The fix, and the run it is for
    #
    # `entry_stubs.c`'s pad now opens with `b 1f` over 31 NOPs, so the whole 128 bytes is jumped over
    # and the two words XNU corrupts are two words nothing ever fetches as instructions. The branch
    # sits at 0x800023d4, twenty bytes below 0x80002404, so it is not one of the corrupted words
    # either. This holds whatever the D-cache, the I-cache, the prefetcher and the flags do, and the
    # build checks the structure rather than the content: `entry_skip_pad` and `entry_skip_pad_end`
    # are linked labels, `verify_pad` decodes the branch at the first of them and refuses the build
    # unless it targets the second, the pad is at least 128 bytes, the branch is at least four bytes
    # below 0x80002404, and both of XNU's addresses are strictly inside the range it skips. This
    # build says: `entry_skip_pad at 0x800023d4 branches over 128 bytes to 0x80002454, and XNU's two
    # writes (0x80002404, 0x80002408) land inside what it skips`.
    #
    # **The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=CleanPoC_DcacheRegion`** - the
    # nearest probe *after* both writes, at 0x8000432c, three instructions past the second
    # `bcopy_phys`.
    #
    #   - **A report at `caller_v = 0x80004330`, `cp_arg0 = 0x80000000`, `cp_arg1 = 0x00001000`**
    #     means the report path survives XNU's writes, and - because this call site is *after* both
    #     `bcopy_phys` calls - that calls 6 and 7 returned too. That would refute the reading that
    #     put the frontier inside `bcopy_phys`, take the instrument's own defect off the table for
    #     good, and move the frontier past 0x8000432c in one step. `cpu.c:583` gives
    #     the two arguments: `CleanPoC_DcacheRegion((vm_offset_t)phystokv((char *)(gPhysBase)),
    #     PAGE_SIZE)` - `gPhysBase` was measured as 0x80000000 by 282g's `ml_static_vtop`, and the
    #     PAGE_SIZE is the `mov r1, #4096` the earlier prediction already read off the instruction
    #     above the call. Its first *executed* call site is this one: the eight others are in
    #     `clean_dcache` (below it and after it), `ml_arm_sleep`, `dcache_incoherent_io_*` and the
    #     panic path, none of which has run by this point.
    #   - **A silence** is now the boot's, and it puts the stop in `[0x800042dc, 0x8000432c)` - the
    #     two `bcopy_phys` calls and the three `ml_static_vtop`s between them. That would be the
    #     first trustworthy silence this walk has had since 280, and it would be worth more than the
    #     report.
    #
    # The prediction: **a report.** Not because of the code - 282m's silence still stands unexplained
    # under the instrument's own defect, and the walk has no measurement of call 6 - but because
    # every silence this walk has taken since 280 has turned out to be the instrument, three times in
    # a row, and because the one report that *is* downstream of both writes and is not from this
    # instrument at all - 279's, from `klist_init` - runs `klist_init` only after
    # `cpu_machine_idle_init` has returned.
    #
    # **The run reported, and it is the first report this walk has had from after XNU's two writes.**
    # 301202 bytes, `stub_hit=CleanPoC_DcacheRegion`, `xnu_entry_stub_caller=0x80004310`, and
    #
    #     cp_arg0 = 0x80000000    cp_arg1 = 0x00001000    cp_arg2 = 0x40000000   cp_arg3 = 0x00112440
    #
    # - the two arguments exactly as predicted (`cpu.c:583`'s `phystokv((char *)(gPhysBase))` is
    # 0x80000000 because the two bases are equal, and its `PAGE_SIZE` is 0x1000), the other two
    # leftovers. The caller reads 0x80004310 rather than the 0x80004330 the prediction named, and by
    # exactly the size of what this build removed: the two repair stores, their `dsb`/`isb` and the
    # pad's own branch arithmetic moved everything after the pad twenty bytes earlier, which the
    # static listing confirms - `bl CleanPoC_DcacheRegion` is at 0x8000430c in this image and was at
    # 0x8000432c in the one before it.
    #
    # Three things follow, and the third is worth more than the step.
    #
    #   - **The report path survives XNU's writes.** That was the open question the skip pad was
    #     built to answer, and the answer is yes - with the caveat that a report cannot by itself
    #     distinguish "the pad is skipped" from "the pad is executed and the corruption happens to be
    #     inert on this run", which is exactly the errancy that made the NOP pad unreadable.
    #   - **Both `bcopy_phys` calls returned**, and so did the two wrapped `bcopy`s inside them (calls
    #     6 and 7 of the numbering above), because this call site is three instructions past the
    #     second one. 282m's silence therefore cannot be read as "the stop is inside `bcopy_phys`'s
    #     first call" - the reading the ledger has carried since - and the interval that silence
    #     actually bounds is empty.
    #   - **The frontier is not in `bcopy_phys` and never was.** Every silence between 280 and this
    #     run was the instrument's: the walk has spent four experiments bisecting a four-byte
    #     `memmove` and a `pmap_cache_attributes` call, both of which this run shows return. The
    #     boot reached 0x8000430c on the *first* run after the pad stopped being executed.
    #
    # Safety on the run: 25 x `persistent_write_attempted=0x00000000`, 87 x
    # `failure_mask=0x00000000`, `xnu_entry_failures=0x00000000`, no abort other than the
    # `high_va_data_abort_handler` contract keys, and the device returned to Android on its own
    # ("No errors detected" in its own log). Non-persistent `fastboot boot` only, nothing flashed.
    #
    # ## The next step, and its prediction. `STAGE90_ENTRY_CHECKPOINT=machine_startup`
    #
    # With the instrument trustworthy the walk can take the step it has wanted to take since 280:
    # `machine_startup` (`0x800075c4`) is `arm_init`'s last act (`arm_init.c:437`) and the boundary
    # between the ladder and XNU's own startup, so a report there proves the *whole* of
    # `arm_init` ran - including the eighteen real calls after `arm_vm_init` that 280's own frontier
    # could not reach.
    #
    #   - **A report at `caller_v = 0x8000xxxx` (inside `arm_init`), `cp_arg0 = 0x80147000`** - the
    #     boot args pointer, which is the value `arm_vm_init`'s report carried at the same key
    #     (`0x80018b18`'s `cp_arg0` in the 279-era ledger) - means `arm_init` is complete.
    #   - **A silence** bounds the stop to `[0x8000430c, machine_startup)` - the rest of
    #     `cpu_machine_idle_init` (`ml_static_vtop`, the conditional `bcopy` at `cpu.c:589`,
    #     `clean_dcache`), the remainder of `arm_init`'s ladder, and the call itself - and unlike
    #     every silence since 280 it would be the boot's, which is what makes it worth taking.
    #
    # The prediction: **a report.** Nothing between this call site and `machine_startup` is a stub
    # that has not already been reported from later in a previous run - 279 reported from
    # `klist_init`, which is *past* `machine_startup` - and the instrument is no longer in question.
    #
    # **The run reported again - `stub_hit=machine_startup` at `caller_v = 0x8000334c`, and the whole
    # ladder is behind it.** 301196 bytes, `cp_arg0 = 0x80147000` exactly as predicted (the boot args
    # pointer, the same value `arm_vm_init`'s report carried), `cp_arg1 = 0x801448c0`,
    # `cp_arg2 = 0x8010e57c`, `cp_arg3 = 0x000000ff` recorded and not interpreted. The caller is
    # inside `arm_init`, and the address is 0x8000334c where the previous image put the same call
    # site at 0x8000336c - the same twenty-byte shift the pad's new shape introduced, which is the
    # second independent reading of it.
    #
    # So `arm_init` reached `arm_init.c:437`, which means every step of the ladder before it ran:
    # `cpu_machine_idle_init` in full - both `bcopy_phys` calls, `CleanPoC_DcacheRegion` (measured in
    # the previous run), the conditional `bcopy` at `cpu.c:589` and `clean_dcache` - and the eighteen
    # real calls `arm_init` makes after `arm_vm_init` returns. That is the interval 280's silence
    # made unreachable and that four experiments were spent inside, and it is now behind the walk.
    #
    # Safety: 25 x `persistent_write_attempted=0x00000000`, 87 x `failure_mask=0x00000000`,
    # `xnu_entry_failures=0x00000000`, the device returned to Android by itself. `fastboot boot`
    # only; nothing was written to storage.
    #
    # ## The next step, and its prediction: a run with **no checkpoint at all**
    #
    # The instrument has been masking the boot's own frontier, and the way to find it is to stop
    # asking the instrument questions and let the image answer the one it answers by itself: a plain
    # build links every object resolved so far and reports `stub_hit=<first symbol it needs and does
    # not have>`, or `exception: <vector>` if it faults first. That is the walk's normal mode - the
    # one that produced 279 - and it has not been available since 280, because every plain run since
    # then has been silent for the pad's reason.
    #
    #   - **A report naming a symbol** is the frontier: the next step links the object that defines
    #     it, exactly as the walk has done 279 times.
    #   - **An `exception:` line** is a fault, and its vector, DFAR/DFSR and instruction are the
    #     frontier.
    #   - **A silence now** would be real, and would have to be explained by the boot rather than by
    #     the instrument - the state the walk has been trying to reach for four experiments.
    #
    # The prediction: **a report, and a `stub_hit` rather than an exception.** The two newest
    # symbols on the path are `kern_event.o`'s (linked in 280, whose own run could not report), and
    # the boot path through IPC init is the one 279's report came from, so the name is expected to be
    # one of the callees that object's own code needs next; the *shape* is what is predicted here,
    # not the name. If it is an exception instead, the walk has a fault to read rather than a symbol
    # to link, and that is a different experiment than the ledger has ever planned for.
    #
    # **The run reported twice over: once through the preflight, and once from a stub two functions
    # further on.** Two runs, because the first exposed a payload defect that this step's growth had
    # triggered, and the second is the step's actual result.
    #
    # **Run one: the payload's own preflight refused the boot.** `loader_xnu_entry_stub_status`
    # came back `0xd0008910` with `failure_mask=0x00008910` - the bits are `BAD_RETURN_STATUS`,
    # `NO_OUTPUT`, `SAFETY_BOUNDARY` and `ARM_VM_INIT_FULL_PMAP` - and the *only* new log line was
    #
    #     stage90_xnu_arm_vm_init_full_pmap_high_va_data_verified=0x00000000
    #
    # against `0x00000001` in every previous run. `kernel_entry bad: Mach-O/XNU loader preflight`,
    # `platform_reboot`, and the device came back on its own. This is the safety gate doing exactly
    # its job: linking 4 KB more XNU code into the entry image grew the payload past an assumption
    # of the Stage84 live-pmap rung, and the rung said so instead of letting the boot proceed on a
    # pmap that disagrees with itself.
    #
    # The mechanism took two runs to get right, and both halves are in
    # `stages/stage90/xnu_arm_vm_init_full_pmap.c`:
    #
    #   - The check writes and reads `stage90_full_pmap_probe_word` through *both* its own address
    #     and `STAGE90_VIRT_BASE +` that address, so **two** mappings have to cover it - the low
    #     identity sections (Phase 1) and the image's page-mapped window (Phase 2). Phase 1 was a
    #     hardcoded pair of sections covering PA [0, 2 MB); the probe word sat at 0x001fc0b4 and
    #     moved to 0x002000b4, one section past the end. **Phase 1 now loops over the image**, which
    #     is what Phase 5 already does for the same reason.
    #   - Fixing that alone changed nothing, because Phase 3 - the 256 MB RAM direct map that starts
    #     at `STAGE90_VIRT_BASE + 0x200000` and writes *sections* - runs after Phase 2 and wins the
    #     L1 entries the image's window had just claimed. The high-VA read then went to PA
    #     0x802000b4 instead of PA 0x002000b4. **Phase 3 now skips the slots the image's window
    #     covers**, and the image's window is what the check exists to test.
    #
    # Both are the same defect class the Phase 2 comment already records ("embedding a larger XNU
    # entry image moved a .bss variable 148 bytes past the end of the window"), and both are now
    # derived from `__stage90_image_end` rather than spelled out, so the next growth moves the
    # mappings with it instead of past them. Also worth recording for the class: `HIGH_VA_DATA` is
    # the bit that fires for a *failure in either half*, and in the second run it was the identity
    # half that was broken - the name points at the wrong side of the comparison.
    #
    # **Run two: `stub_hit=clock_config`, `xnu_entry_stub_caller=0x800076c4`, 301113 bytes.**
    # 0x800076c4 is `machine_init + 0xc`, and `machine_init` is the *first* of the twelve init
    # functions `kernel_bootstrap` calls after `ipc_init` returns - exactly the "next frontier is
    # transitive" shape this step predicted, resolved to a name by the run. So this step measured
    # all of the following in one go:
    #
    #   - `ipc_host_init` completes, including all three `kernel_set_special_port` calls, and with
    #     `realhost` now a real 0x194-byte object rather than a stand-in - the size 277 measured is
    #     the size the real definition has, to the byte;
    #   - `ipc_init` completes, which is `kmem_suballoc` twice and `ipc_host_init` once;
    #   - `kernel_bootstrap` runs from `PE_parse_boot_argn` through `mapping_free_prime` and into
    #     `machine_init`, with *no* stub left anywhere in its own body;
    #   - and the first thing it needs that this image does not have is `clock_config`.
    #
    # Safety on both runs: 25 x `persistent_write_attempted=0x00000000`, 87 x
    # `failure_mask=0x00000000`, `xnu_entry_failures=0x00000000`, and the device returned to Android
    # by itself on both - on the first through the payload's own `platform_reboot`, on the second
    # through the normal exit. `fastboot boot` only; nothing flashed.
    #
    # **The next step: `clock_config`.** It is `osfmk/kern/clock.c` (manifest), so the object is
    # `osfmk_kern_clock.o`, and `machine_init` calls it at `+0xc` - before anything else it does.
    #
    # 279: `ipc_mqueue_init`, and a stop that reports the caller key 278 already reported.
    # 278's stop was `ipc_mqueue_init`, and the object that defines it is `osfmk/ipc/ipc_mqueue.c`
    # (manifest:514), `osfmk_ipc_ipc_mqueue.o` - **5436 bytes of text, 8 of bss, 238 of rodata, 47
    # definitions and 46 references**. The 8 bytes of bss are the two counters `ipc_mqueue_full` and
    # `ipc_mqueue_rcv`, 4 each; the strings are the `"ipc_mqueue_send"` panics and the
    # `"Unknown mqueue type 0x%x: likely memory corruption!"` panic.
    #
    # **5 resolved, 9 added.** The five retired are exactly the five names 278 obliged -
    # `ipc_mqueue_init`, `_deinit`, `_changed`, `_destroy_locked`, `_override_send` - which is the
    # cleanest kind of step: the previous step's own additions coming good, one experiment later. The
    # nine added are all functions, all defined by objects already compiled:
    #
    #     ipc_kmsg_dequeue         ipc_kmsg_enqueue_qos     ipc_kmsg_override_qos
    #     ipc_kmsg_rmqueue         ipc_kmsg_queue_next      ipc_kmsg_delayed_destroy
    #     ipc_kmsg_copyout_size    knote_vanish             mach_msg_receive_continue
    #
    # seven out of `osfmk/ipc/ipc_kmsg.c` (the first names this walk has obliged out of it), one more
    # out of `bsd/kern/kern_event.c`, and one out of `osfmk/ipc/mach_msg.c`.
    #
    # **Prediction: `stub_hit=klist_init`, with the caller at `ipc_port_alloc_special+0x94` - the
    # same value 278 measured, and this time the name is the only thing that tells the two stops
    # apart.** `ipc_port_alloc_special` calls `ipc_mqueue_init(port+16, FALSE, NULL)`, and the
    # object's whole `is_set == FALSE` path is real:
    #
    #     0000: push {r4, r5, fp, lr}            ; saves lr = `ipc_port_alloc_special+0x94`
    #     0008: cmp r1, #0 / beq +0x24           ; is_set is FALSE, so the set path is skipped
    #     0030: bl waitq_init(port+16, SYNC_POLICY_FIFO)     ; REAL (waitq.o, 267)
    #           ... `ipc_kmsg_queue_init` inlined, imq_seqno/imq_msgcount 0,
    #               imq_qlimit = 0x50000 = MACH_PORT_QLIMIT_DEFAULT << 16, imq_fullwaiters FALSE
    #     0050: add r0, r4, #48
    #     0054: pop {r4, r5, fp, lr}             ; lr = `ipc_port_alloc_special+0x94` again
    #     0058: b klist_init                     ; A TAIL CALL, and a stub
    #
    # `klist_init` is `bsd/kern/kern_event.c`, and the last instruction of `ipc_mqueue_init` is a
    # `b`, not a `bl` - so the stub's `lr` is the one `ipc_mqueue_init` inherited from its own caller
    # and carried through its `pop`. That makes the caller key `ipc_port_alloc_special+0x94`: the
    # **same address 278's run reported**, where the `bl` at `caller-4` was the call *into*
    # `ipc_mqueue_init`. Here that same `bl` is one function further back, and what it points at is a
    # function that has already returned. This is 275's shape exactly - a tail call reporting its
    # caller's caller, and an ambiguity between two *consecutive* stops - and it is the second time
    # this walk has met it.
    #
    # `waitq_init`'s own body calls only `hw_lock_init` and `waitq_lock`, both real, so there is no
    # stub between the entry of `ipc_mqueue_init` and its tail.
    OSFMK_IPC_IPC_MQUEUE_OBJ=${STAGE90_ENTRY_OSFMK_IPC_IPC_MQUEUE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_ipc_ipc_mqueue.o}
    #
    # **279 measured it, including the part that was the reason to write it down carefully.** The
    # build measured **5 resolved, 9 added**: 859 -> 863 undefined, 785 -> 789 function stubs, storage
    # unchanged at 74, text 962244 -> 968100, image bytes 1066104 -> 1082488 (one 16 KB alignment
    # step), `.bss` `0x80107a00` .. `0x8013d548`, headroom 1845944. `ipc_mqueue_init` is real at
    # `0x800ba680` and the linked stream is the object's: `800ba69c bl waitq_set_init` (the
    # `is_set == TRUE` arm), `800ba6b0 bl waitq_init`, `800ba6d4 pop {r4, r5, fp, lr}`,
    # `800ba6d8 b 800d298c <klist_init>`.
    #
    # The run stopped at **`stub_hit=klist_init`** with `xnu_entry_stub_caller_v=0x800ba300` =
    # **`ipc_port_alloc_special+0x94`** - *the same value 278's run reported*, which is what the
    # prediction said would happen and why it was spelled out: the last instruction of
    # `ipc_mqueue_init` is a `b`, so the stub's `lr` is the one its own `pop` restored, which is its
    # caller's return address and not the address of the tail call. `caller-4` is `800ba2fc`, and in
    # 278 that same `bl` was the call *into* `ipc_mqueue_init`; here the function it calls has already
    # returned. So `caller-4` alone no longer says which function's body the run is in, and the stub's
    # own name is the only discriminator between two consecutive stops - 275's shape, twice now.
    # `_a` and `_e` agree, `_w0 = 0x62303038` / `_w1 = 0x30303361` = `800ba300` from the first digit
    # (`digits = 0x2e` = 21 + 25, a 10-character name), `kv_written=0x5b` (91 = 21 + 34 + 36),
    # `kv_in_dram=0x7f` (127 = 91 + 36), `kv_dropped=0`, `why_byte=0x61`, zero abort entries, echo
    # intact. 661 of 661 words of `entry_kv` through `entry_stub_hit` match the linked ELF (eighth
    # build running).
    #
    # So the port object's message queue was initialized with a real `waitq_init(port+16, FIFO)` - the
    # waitq machinery 267 linked and 268 measured - and `ipc_mqueue_init` then stopped one instruction
    # from the end, on `klist_init`. `klist_init` is `bsd/kern/kern_event.c` and is the next step.
    # 278: `ipc_port_alloc_special`, and the step that starts on the object XNU's own linkage makes
    # the largest one on this path.
    # 277's stop was `ipc_port_alloc_special`, and the object that defines it is `osfmk/ipc/ipc_port.c`
    # (manifest:517), `osfmk_ipc_ipc_port.o` - **8924 bytes of text, 16 of bss, 81 of
    # `.rodata.str1.1`, 48 definitions and 53 references**. The three strings are `"ipc port"`,
    # `"ipc ports"` and a format string; the 16 bytes of bss are `ipc_port_timestamp_data` (4),
    # `ipc_port_multiple_lock_data` (8) and `ipc_portbt` (4).
    #
    # **20 resolved, 19 added** - bigger than 277 on both sides. The 20 retired are 18 functions and
    # **two storage stand-ins**:
    #
    #     ipc_port_alloc_special          ipc_port_dealloc_special       ipc_port_destroy
    #     ipc_port_make_send              ipc_port_make_send_locked      ipc_port_make_sonce_locked
    #     ipc_port_copy_send              ipc_port_copyout_send          ipc_port_release_send
    #     ipc_port_release_sonce          ipc_port_nsrequest             ipc_port_check_circularity
    #     ipc_port_impcount_delta         ipc_port_importance_delta      ipc_port_importance_delta_internal
    #     ipc_port_sync_qos_delta         kdp_mqueue_send_find_owner     kdp_mqueue_recv_find_owner
    #     ipc_port_multiple_lock_data     ipc_port_timestamp_data        <- storage, 8 and 4 bytes
    #
    # and the 19 added are all functions, each defined by an object the build has already compiled -
    # the rule since 244:
    #
    #     ipc_object_alloc          ipc_object_alloc_name     ipc_object_copyout
    #     ipc_mqueue_init           ipc_mqueue_deinit         ipc_mqueue_destroy_locked
    #     ipc_mqueue_changed        ipc_mqueue_override_send  ipc_entry_lookup
    #     ipc_pset_remove_from_all  ipc_notify_send_possible  ipc_notify_port_destroyed
    #     ipc_notify_send_once      ipc_notify_no_senders     ipc_notify_dead_name
    #     io_free                   ipc_kmsg_reap_delayed     task_is_importance_donor
    #     knote_adjust_sync_qos
    #
    # The last two are the first names this walk has obliged out of `osfmk/kern/task_policy.o` and out
    # of `bsd/`'s `kern_event.c` - but they are on the sync-QoS path, not on the allocation path, so
    # they will be stubs this step does not reach.
    #
    # **Prediction: `stub_hit=ipc_mqueue_init`, with the caller at `ipc_port_alloc_special+0x94`.**
    # `ipc_host_init` calls it first thing (`800b757c bl ipc_port_alloc_special`), and the object's own
    # stream is short and entirely real until its last call:
    #
    #     1ec8: push {r4, r5, fp, lr}
    #     1ed8: ldr r0, [ipc_object_zones]      ; IOT_PORT is 0 (`ipc_object.h:149`)
    #     1edc: bl zalloc                       ; REAL - zalloc.o, and see below
    #     1ee4: beq -> return IP_NULL           ; the only exit that is not the full one
    #     1ef0: bl bzero(port, 128)             ; REAL, and `mov r1, #128` is sizeof(struct ipc_port)
    #     1f08: bl lck_spin_init(port+8, ipc_lck_grp, ipc_lck_attr)   ; REAL
    #     1f1c: strd r2, [r4]                   ; io_bits 0x80000000, ip_references 1
    #     1f50: str r5, [r4, #72]               ; the space argument
    #     1f58: bl ipc_mqueue_init(port+16)     ; A STUB  <- the stop
    #     1f5c: mov r0, r4 / pop {r4, r5, fp, pc}
    #
    # (`ipc_port_init` is real in this object at +0x5c4 and is **inlined** here, which is why the
    # source's `ipc_port_init(port, space, 1)` shows up as the stores above rather than as a call.)
    # The caller key is `ipc_port_alloc_special+0x94`: a `bl`'s return address inside the function the
    # call is in, the same shape 276 and 277 had.
    #
    # **The one thing this prediction depends on is that `zalloc` succeeds**, because a zone that
    # cannot grow takes a real `panic` before the stub is reached - and the argument for it is a
    # measurement, not a hope: `ipc_space_create_special` calls `zalloc` at `800ac520` from inside
    # `ipc_bootstrap`, and 274 is the run in which `ipc_bootstrap` **returned**. So a `zalloc` from a
    # zone `ipc_bootstrap` created with the same `scale_setup` numbers has already worked on this
    # device, once. And `ipc_object_zones[IOT_PORT]` is the *same stand-in array* `ipc_bootstrap`
    # filled with a real `zinit("ipc ports", 128, ipc_port_max*128, 128)` in that run - the stand-in is
    # zeroed at payload start and written by real code before `ipc_port_alloc_special` ever sees it,
    # which is why reading a zeroed stand-in here is not a null-zone panic.
    OSFMK_IPC_IPC_PORT_OBJ=${STAGE90_ENTRY_OSFMK_IPC_IPC_PORT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_ipc_ipc_port.o}
    #
    # **278 measured it, and both halves held.** The build measured **20 resolved, 19 added**:
    # 860 -> 859 undefined, 784 -> 785 function stubs, storage 76 -> 74, text 953220 -> 962244,
    # image bytes unchanged at 1066104 (the growth fitted the padding again), `.bss`
    # `0x80103a00` .. `0x80139548` (219976 bytes), headroom 1862328. `ipc_port_alloc_special` is real
    # at `0x800ba26c` and the linked stream is the object's: `800ba280 bl zalloc`,
    # `800ba294 bl __bzero`, `800ba2ac bl lck_spin_init`, `800ba2fc bl ipc_mqueue_init`.
    #
    # The run stopped at **`stub_hit=ipc_mqueue_init`** with `xnu_entry_stub_caller_v=0x800ba300` =
    # **`ipc_port_alloc_special+0x94`**, whose `caller-4` is `800ba2fc bl 800d0e94 <ipc_mqueue_init>`;
    # `_a` and `_e` agree, `_w0 = 0x62303038` / `_w1 = 0x30303361` = `800ba300` from the first digit
    # (`digits = 0x33` = 26 + 25, a 15-character name), `kv_written=0x60` (96 = 26 + 34 + 36),
    # `kv_in_dram=0x84` (132 = 96 + 36), `kv_dropped=0`, `why_byte=0x61`, zero abort entries, echo
    # intact. 661 of 661 words of `entry_kv` through `entry_stub_hit` match the linked ELF (seventh
    # build running).
    #
    # **So `zalloc` really allocated, on this device, from the zone `ipc_bootstrap` created four
    # experiments ago** - and a real 128-byte port object came out of the "ipc ports" zone, was
    # bzeroed, had its spin lock initialized with the real `ipc_lck_grp`/`ipc_lck_attr` that
    # `ipc_bootstrap` filled in, and took `io_bits = 0x80000000` and `ip_references = 1`. That is the
    # **first IPC port this kernel has ever allocated**, and the first time the walk has executed an
    # allocation path rather than an initialization one. It stopped one instruction later, on
    # `ipc_mqueue_init`, which is `osfmk/ipc/ipc_mqueue.c` and is the next step.
    # 277: `ipc_host_init`, and the largest stub retirement this walk has made.
    # 276's stop was `ipc_host_init`, and the object that defines it is `osfmk/kern/ipc_host.c`,
    # `osfmk_kern_ipc_host.o` - the manifest line is **550** in `out/xnu_arm_manifest.txt` (549 is
    # `osfmk/kern/ipc_clock.c`), which corrects the 549 that 275's and 276's blocks carry: this walk
    # has one manifest citation a line early, and it was made while predicting, not while measuring.
    # The object is **3668 bytes of text, 53 of `.rodata.str1.1`, no data and no bss, 21 functions
    # defined and 26 names referenced**, and the three strings in it are the three panics -
    # `"ipc_host_init"`, `"ipc_processor_init"`, `"ipc_pset_init"`.
    #
    # **16 resolved, 8 added** - the largest single-step retirement in this walk, against a step
    # count that has mostly been one or two. Of the 26 names the object references, 12 are already
    # real code and 6 already have stand-ins (5 functions, plus `realhost`, which is a *storage*
    # stand-in of 0x194 bytes rather than a function), so 18 need nothing. The 16 the object defines
    # that this image currently *stubs* are the ones that retire:
    #
    #     convert_host_to_port          convert_port_to_host        convert_port_to_host_priv
    #     convert_port_to_host_security convert_port_to_processor    convert_port_to_pset
    #     convert_port_to_pset_name     convert_pset_name_to_port   convert_pset_to_port
    #     host_get_exception_ports      host_set_exception_ports    host_swap_exception_ports
    #     ipc_host_init                 ipc_processor_enable        ipc_processor_init
    #     processor_set_default
    #
    # and the 8 that are new obligations are all functions, each defined by an object this build has
    # already compiled - so each stand-in's *size* still comes from its own definition rather than
    # from a guess, which is the rule since 244:
    #
    #     kernel_set_special_port                   osfmk_kern_host.o
    #     ipc_port_copyout_send                     osfmk_ipc_ipc_port.o
    #     mac_exc_associate_action_label            security_mac_mach.o
    #     mac_exc_create_label                      security_mac_mach.o
    #     mac_exc_create_label_for_current_proc     security_mac_mach.o
    #     mac_exc_free_label                        security_mac_mach.o
    #     mac_exc_update_action_label               security_mac_mach.o
    #     mac_task_check_set_host_exception_ports   security_mac_mach.o
    #
    # The other half of the object, and the half this walk has not taken: five of its 21 functions
    # (`convert_processor_to_port`, `host_self_trap`, `ipc_pset_enable`, `ipc_pset_init`,
    # `ref_pset_port_locked`) are referenced by nothing in this image, so they are neither real nor
    # stubs - they are absent, which is the shape 276's four unreferenced label functions had. A
    # definition nothing references is invisible to a delta of two undefined sets.
    #
    # **Prediction: `stub_hit=ipc_port_alloc_special`, with the caller at `ipc_host_init+0x30`.**
    # `ipc_host_init` is the function the walk is standing in, so this step runs it:
    #
    #     0000: push {r4, r5, r6, r7, fp, lr}
    #     0004: movw/movt realhost, host_notify_lock_grp, host_notify_lock_attr
    #     001c: bl lck_mtx_init(&realhost.lock, ...)          ; REAL - locks.o since 269
    #     0028: ldr r0, [ipc_space_kernel]                    ; REAL (ipc_space.c, 265)
    #     002c: bl ipc_port_alloc_special(ipc_space_kernel)   ; A STUB  <- `ipc_port_alloc_kernel()`
    #     0030: mov r4, r0                                    ; the return address, and the stop
    #
    # so the run executes the lock init, reads `ipc_space_kernel`, and stops on the first instruction
    # that becomes a stub again - `bl ipc_port_alloc_special`, which is what XNU's own
    # `ipc_port_alloc_kernel()` macro expands to. The caller key is `ipc_host_init+0x30`: a `bl`'s
    # return address *inside* the function entered, the same shape 276 had, and unlike the three
    # tail-call steps before it.
    #
    # Two things about `realhost` are worth writing down before the run, because this step is the
    # first time real XNU code *writes* to a storage stand-in rather than reading one. It is 0x194
    # bytes, sized from `osfmk_kern_host.o` (which is not linked, which is why it is a stand-in at
    # all), and `ipc_host_init` writes `realhost.lock` through `lck_mtx_init` and then 26 stores of
    # IP_NULL/NULL at offsets 144 through 400 - whose last one, at 400, ends exactly at the stand-in's
    # last byte. That is the case an undersized stand-in would fail, silently overwriting whatever the
    # linker put next; the size is not a guess, and this run is where it stops being theoretical. The
    # eight `mac_exc_*` calls are on the exception-port paths and are not reached here: the object
    # itself defers label initialization (its own comment says so) so `realhost.exc_actions[i].label`
    # is set to NULL, not to a MAC label.
    OSFMK_KERN_IPC_HOST_OBJ=${STAGE90_ENTRY_OSFMK_KERN_IPC_HOST_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_ipc_host.o}
    #
    # **277 measured it, and the prediction held exactly - both halves, and the counts to the unit.**
    # The build measured **16 resolved, 8 added**: 868 -> 860 undefined, 792 -> 784 function stubs,
    # storage unchanged at 76, text 949860 -> 953220, image bytes 1049720 -> 1066104 (one 16 KB
    # alignment step, and `.bss` moved with it: `0x80103a00` .. `0x801395c8`), headroom 1862200 bytes.
    # `ipc_host_init` is real in the image at `0x800b7550`, and the linked disassembly is the object's
    # instruction for instruction: `800b756c: bl lck_mtx_init`, `800b7578: ldr r0, [r6]`,
    # `800b757c: bl ipc_port_alloc_special`, `800b7580: mov r4, r0`.
    #
    # The run stopped at **`stub_hit=ipc_port_alloc_special`** with
    # `xnu_entry_stub_caller_v=0x800b7580` = **`ipc_host_init+0x30`**, whose `caller-4` is
    # `800b757c bl 800ceb54 <ipc_port_alloc_special>`; `_a` and `_e` agree, `_w0 = 0x62303038` /
    # `_w1 = 0x30383537` = `800b7580` read back out of `g_kv_buf` from the first digit
    # (`digits = 0x3a` = 33 + 25, a 22-character name this time), `kv_written=0x67` (103),
    # `kv_in_dram=0x8b` (139 = 103 + 36), `kv_dropped=0`, `why_byte=0x61`, zero abort entries, and
    # the report's echo intact. What ran before the stop, read from its own instruction stream:
    # `lck_mtx_init(&realhost.lock, &host_notify_lock_grp, &host_notify_lock_attr)` — every one of
    # those three names was already in the image (`host_notify_lock_grp`/`_attr` are real code since
    # 274, and `realhost` is the 404-byte storage stand-in), so the kernel's first act inside
    # `ipc_host_init` is the one that finally initializes `realhost`'s lock, and the stand-in took a
    # real write up to its last byte without clobbering its neighbour. Then `ldr r0, [ipc_space_kernel]`
    # read the real special space 265 created. None of the 8 new obligations was reached. 661 of 661 words of `entry_kv` through `entry_stub_hit` match the linked ELF again
    # (sixth build running) - *after* one correction on this side of the wire, which is worth writing
    # down because it is a measurement defect and not a device one: the dump prints each word's *value*
    # as eight hex digits, and the first comparison here read those digits against the image's raw
    # little-endian bytes and reported 660 of 661 mismatched. The word at `0x80002024` is
    # `0xe305c480`; the image holds `80 c4 05 e3`. Comparing the number to the number gives 0
    # mismatches, and that is the fact.
    #
    # So `ipc_host_init` did not finish: it stops on its **second** call that is still a stub - the
    # first, `lck_mtx_init`, is real - and `ipc_port_alloc_special` is `osfmk/ipc/ipc_port.c`, which is
    # the next step. The five `mac_exc_*` / `mac_task_check_*` stubs it brought are on the
    # exception-port paths and were not reached.
    # 276: `mac_labelzone_init`, and the stop two functions further on - inside `ipc_init`.
    # 275's stop was `mac_labelzone_init`, and the object that defines it is `security/mac_label.c`
    # (manifest:669), `security_mac_label.o` - **358 bytes of text, 0 of data, 4 of bss, 8 definitions
    # and 7 references**, and every one of the seven is already real (`bzero`, `panic`, `zalloc`,
    # `zalloc_noblock`, `zfree`, `zinit`, `zone_change`). **1 resolved, 0 added.** Four of the five
    # functions the object defines (`mac_labelzone_alloc`, `mac_labelzone_free`, `mac_label_get`,
    # `mac_label_set`) are referenced by nothing in this image and so are not even stubs.
    #
    # The function is a `zinit` and three `zone_change`s, and it ends in a tail call of its own:
    #
    #     0000: push {r4, lr}
    #     0018: bl zinit(16, 0x20000, 16, "MAC Labels")   ; sizeof(struct label) = 16, a 128 KB ceiling
    #     0030: bl zone_change(zone, Z_EXPAND = 3, TRUE)
    #     0040: bl zone_change(zone, Z_EXHAUST = 1, FALSE)
    #     0050: pop {r4, lr}
    #     0054: b zone_change                            ; Z_CALLERACCT = 5, FALSE
    #
    # so `mac_labelzone_init` completes, and because it was entered by `mac_policy_init`'s tail call,
    # the `pop {r4, lr}` in it carries `kernel_bootstrap+0x248` straight through: control lands back in
    # `kernel_bootstrap` at `8000e148`, exactly where 274's and 275's stops were reported from.
    #
    # **Prediction: `stub_hit=ipc_host_init`, with the caller at `ipc_init+0xe4`.** From `+0x248` the
    # line is one debug string, then `8000e154: bl ipc_init`, and `ipc_init` is real code this image
    # has carried since 265 - but 265 never ran it, because `ipc_bootstrap` stopped before
    # `ipc_bootstrap` returned. Its compiled body calls exactly three things:
    #
    #     bl kmem_suballoc(kernel_map, &min, ipc_kernel_map_size, ...)        ; real (244)
    #     bl panic                                                           ; only on failure
    #     bl kmem_suballoc(kernel_map, &min, ipc_kernel_copy_map_size, ...)   ; real
    #     bl panic                                                           ; only on failure
    #     ... the `msg_ool_size_small` clamp against `kalloc_max_prerounded` (an immediate 8192 and a
    #         `subls r2, r1, #20` for `cpy_kdata_hdr_sz`), and the two `ipc_kernel_copy_map` flags
    #         `no_zero_fill` / `wait_for_space` (`orr r1, r3, #5`), all stores, no calls
    #     800ac33c: bl ipc_host_init                                          ; A STUB
    #     800ac340: add sp, #24 / pop {r4, r5, r6, r7, fp, pc}
    #
    # so the stop is **`ipc_host_init`**, and because that call is a `bl` and not a tail call, the
    # caller key is the *return address inside `ipc_init`* - `ipc_init+0xe4` - and not
    # `kernel_bootstrap`'s. That is the opposite of the last three steps, and it is the reason the
    # prediction is written from the disassembly of the function that will be entered rather than from
    # the one that was left. `ipc_host_init` is `osfmk/kern/ipc_host.c` (manifest:549) and is the step
    # after this one.
    #
    # **276 measured it, and the prediction held in both halves - the step, and the two functions
    # that ran behind it.** The build measured **1 resolved, 0 added**: 869 -> 868 undefined
    # (792 function stubs, 76 storage), text 949572 -> 949860, and the image bytes, `.bss` and the
    # layout all unchanged - the growth fitted the alignment padding again. The run stopped at
    # **`stub_hit=ipc_host_init`** with `xnu_entry_stub_caller=0x800ac340` = **`ipc_init+0xe4`**,
    # whose `caller-4` is `800ac33c bl 800cddac <ipc_host_init>`: the first time in four steps that
    # the caller key is an address *inside* the function the run was in, which is what a `bl` does
    # and a tail call does not. `_v`, `_a` and `_e` all agree; `kv_written=0x5e` (94 = a 24-byte stub
    # record plus the 34- and 36-byte caller records), `kv_in_dram=0x82` (130 = 94 + 36),
    # `kv_dropped=0`, `why_byte=0x61`, zero abort entries, and `_w0 = 0x61303038` / `_w1 = 0x30343363`
    # = `800ac340` read back out of `g_kv_buf` from the first digit (`digits = 0x31` = 24 + 25).
    #
    # So three functions completed in this run that had never run before: `mac_labelzone_init` (the
    # 128 KB zone and its three `zone_change` calls), and then `ipc_init` - **two `kmem_suballoc`
    # calls that built `ipc_kernel_map` and `ipc_kernel_copy_map`**, the `msg_ool_size_small` clamp
    # against `kalloc_max_prerounded`, and the two `ipc_kernel_copy_map` flags. 661 of 661 words of
    # `entry_kv` through `entry_stub_hit` still match the linked ELF (fifth build running).
    SECURITY_MAC_LABEL_OBJ=${STAGE90_ENTRY_SECURITY_MAC_LABEL_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/security_mac_label.o}
    # 275: `mac_policy_init`, and the stop is a tail call that reports its caller's caller - again.
    # 274's stop was `mac_policy_init`, and the object that defines it is `security/mac_base.c`
    # (manifest:664), `security_mac_base.o` - **10087 bytes of text, 1280 of data, 2120 of bss, 115
    # definitions and 72 references**, the first object this walk has taken out of `osfmk/` and `bsd/`
    # into `security/`, and the largest single step since 267. **3 resolved** (`mac_policy_init`,
    # `mac_policy_initbsd`, `mac_policy_initmach` - the only three of its definitions the image
    # references), **32 added**, of which the largest groups are the `sbuf_*` family (`sbuf_new`,
    # `sbuf_setpos`, `sbuf_putc`, `sbuf_printf`, `sbuf_len`, `sbuf_finish`), the MAC label surface
    # (`mac_cred_label_*`, `mac_vnode_label_*`, `mac_mount_label_externalize`,
    # `mac_file_check_get`/`_set`), the `kauth_*` credentials, `namei`/`nameidone`/`vn_setlabel`,
    # `sysctl__children`, `strsep`, `vfs_context_current`, `act_set_astmacf`, `bsd_exception`,
    # `_FREE` and `mac_labelzone_init`. 28 of the 72 references are already real and 12 already stubs.
    # **Every one of the 32 is defined by an object the build has already compiled** (`security_*.o`
    # among them), so the generator can size each stand-in from its own definition - the check that
    # has failed the build rather than guess a size since 244.
    #
    # The built function is data setup and nine calls, and the data half is the first thing this walk
    # has linked that says `CONFIG_EMBEDDED` is on: `mac_policy_list.entries` is `mac_policy_static_entries`,
    # not a `kalloc`, and this object defines that array itself (`SECURITY_READ_ONLY_LATE(static struct
    # mac_policy_list_element) mac_policy_static_entries[MAC_POLICY_LIST_CHUNKSIZE]`, `mac_base.c:273`),
    # so it needs no stand-in and no size decision.
    #
    #     00f4: vld1.64 {d16-d17}, [pc, #0xb4]   ; a 32-byte .rodata constant for the list header:
    #     011c: vst1.32 {d16-d17}, [r1 :128]!    ;   numloaded 0, max 0x200, maxindex 0, staticmax 0,
    #                                             ;   freehint 0, chunks 1
    #     0128: bl bzero(mac_policy_static_entries, 0x800)   ; 512 * 4 bytes
    #     0144: bl lck_grp_attr_alloc_init     \  nine calls, all real, all already in this image
    #     014c: bl lck_grp_attr_setstat        |  (269 linked `locks.o`; 273 and 274 have been
    #     015c: bl lck_grp_alloc_init          |  calling into it on every run since)
    #     0164: bl lck_attr_alloc_init         |
    #     016c: bl lck_attr_setdefault         |
    #     0178: bl lck_mtx_alloc_init          |
    #     018c: bl lck_attr_free               |
    #     0194: bl lck_grp_attr_free           |
    #     019c: bl lck_grp_free                /
    #     01a0: pop {r4, r5, r6, lr}
    #     01a4: b mac_labelzone_init           ; a tail call, and a new undefined name
    #
    # **Prediction: `stub_hit=mac_labelzone_init`, with the caller at `kernel_bootstrap+0x248` - the
    # same offset 274 measured, and that is the point of writing it down.** `mac_policy_init` is
    # entered by `bl mac_policy_init` from `kernel_bootstrap` at `+0x244`, so its own `lr` is
    # `+0x248`; the prologue pushes that `lr` and the epilogue pops it back, and the tail call then
    # leaves *that* value in `lr` for the stub. So **the caller key alone cannot tell this stop from
    # the previous one** - `caller-4` resolves to the same `bl mac_policy_init` either way, and the
    # *name* is what says which function the run has reached. This is 246's and 252's shape for the
    # third step in a row, and the first time the ambiguity is between two consecutive stops of the
    # same walk.
    #
    # `mac_labelzone_init` is `security/mac_label.c` (manifest:669) and is the step after this one.
    #
    # **275 measured it, and both halves of the prediction held - including the half that says the
    # caller key is not enough.** The build measured exactly what was predicted of the link: **3
    # resolved, 32 added** (31 functions and one storage name, `sysctl__children`, 4 bytes, sized from
    # its own definition), 840 -> 869 undefined, 765 -> 793 function stubs, 75 -> 76 storage, text
    # 938308 -> 949572, image bytes 1048440 -> 1049720, `.bss` `0x800ffa00` .. `0x801355c8`, headroom
    # 1878584. The run stopped at **`stub_hit=mac_labelzone_init`** with
    # `xnu_entry_stub_caller=0x8000e148` - **`kernel_bootstrap+0x248`, the same value 274 measured**,
    # because the tail call leaves `mac_policy_init`'s own return address in `lr`. So `caller-4`
    # resolves to the same `bl mac_policy_init` for both stops, and the *name* is what says which
    # function the walk reached: `_v`, `_a` and `_e` all agree, `kv_written=0x63` (99 = the 29-byte
    # stub record plus two 34/36-byte caller records), `kv_in_dram=0x87` (135 = 99 + 36),
    # `kv_dropped=0`, `why_byte=0x61`, zero abort entries. `mac_policy_init` ran: the 32-byte
    # constant into the list header, the 2048-byte `bzero` of `mac_policy_static_entries`, both
    # `LIST_INIT`s, and all nine lock calls.
    #
    # Two things this run settles beyond the step. **The digits window reads from the first digit
    # again**: `_w0 = 0x30303038` and `_w1 = 0x38343165` are `8000e148` read back out of `g_kv_buf`,
    # with `xnu_entry_stub_caller_digits = 0x36` = 54 = 29 + 25 - 273's off-by-one fix, verified on
    # hardware. And **the report's echo is intact in this build** (`real XNU entry stub_hit=mac_labelzone_init`
    # as three clean lines), where 274's was mangled twice identically - the same source-level
    # reporter, two consecutive builds, two different outcomes, which is 272's caution arriving once
    # more: the entry image's report is not a function of its source either.
    SECURITY_MAC_BASE_OBJ=${STAGE90_ENTRY_SECURITY_MAC_BASE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/security_mac_base.o}    # 274: `host_notify_init`, and the walk comes *back out* of `ipc_bootstrap` for the first time.
    # 273's stop was `host_notify_init`, and the object that defines it is `osfmk/kern/host_notify.c`
    # (manifest:548), `osfmk_kern_host_notify.o` - **1616 bytes of text, 0 of data, 364 of bss, 18
    # definitions and 15 references**. **3 resolved** (`host_notify_init`, `host_notify_port_destroy`,
    # `host_request_notification`), **0 added**: the 258/260/263/266/268 shape, and the cheapest kind
    # of step. Thirteen of the fifteen references are already real (`lck_grp_attr_setdefault`,
    # `lck_grp_init`, `lck_attr_setdefault`, `lck_mtx_init_ext`, `lck_mtx_lock`, `lck_mtx_unlock`,
    # `lck_spin_lock`, `lck_spin_unlock`, `ipc_kobject_set_atomically`, `panic`, `zalloc`, `zfree`,
    # `zinit`) and the two that are not (`ipc_port_release_sonce`,
    # `mach_msg_send_from_kernel_proper` - the latter added by 273 one step ago) are already stubs.
    # Five of the definitions matter to the link and two of those (`host_notify_calendar_change`,
    # `host_notify_calendar_set`) are referenced by nothing in this image: 250's shape, again - what
    # an object *defines* is not what the link *needs*.
    #
    # The function is one inlined loop and five calls, and **not one of them can stop**:
    #
    #     queue_init(&host_notify_queue[0]) and [1]    ; inlined, four instructions, no call
    #     bl lck_grp_attr_setdefault(&host_notify_lock_grp_attr)
    #     bl lck_grp_init(&host_notify_lock_grp, "host_notify", &host_notify_lock_grp_attr)
    #     bl lck_attr_setdefault(&host_notify_lock_attr)
    #     bl lck_mtx_init_ext(&host_notify_lock, &host_notify_lock_ext, ...)
    #     bl zinit(16, 65536, 256, "host_notify")      ; 16 = sizeof(struct host_notify_entry)
    #
    # The object has no branch at all in it, so there is nothing to be surprised by: `host_notify_init`
    # completes. The two stubs it does reference are inside `host_notify_calendar_change` and
    # `host_notify_all`, which nothing calls. Its `zinit` ceiling is 64 KB, the third this walk has
    # computed from a `sizeof` it read itself (271's 1.4 MB `semaphore_max`, 273's 416 KB `mk_timer`).
    #
    # **Prediction: `stub_hit=mac_policy_init`, with the caller at `kernel_bootstrap+0x248`.** This is
    # the step where the walk *leaves* `ipc_bootstrap`: `mk_timer_init` was its last call in source
    # order, the tail `b host_notify_init` was reached with `ipc_bootstrap`'s own return address in
    # `lr` (273 measured it: `0x8000e138` = `kernel_bootstrap+0x238`), so when `host_notify_init`
    # returns, control lands **in `kernel_bootstrap`**, not in `ipc_bootstrap`. From there the line is
    #
    #     8000e138: movw/movt r0, <a debug string>
    #     8000e140: bl kernel_debug_string_early      ; real code, and 273's run already passed it
    #     8000e144: bl mac_policy_init                ; a stub
    #
    # so the stop is `mac_policy_init` at `kernel_bootstrap+0x248`, **offset first** - four steps in a
    # row have moved the absolutes and none has moved an offset, and this step moves one too if the
    # new object is laid down before `startup.o` rather than after it. `ipc_bootstrap` returning is
    # the milestone: the whole Mach IPC bootstrap - `ipc_space_create_special` twice, `mig_init`'s 17
    # descriptors, the table, the voucher and importance subsystems, the semaphore, the timer - will
    # have run on this device from end to end.
    #
    # `mac_policy_init` is `security/mac_base.c` (manifest:664), `security_mac_base.o` - **10087 bytes
    # of text, 1280 of data, 2120 of bss, 115 definitions and 72 references** - and it is the next
    # step's subject, not this one's.
    #
    # **274 measured it, and the prediction held to the offset.** Resolved 3 (`host_notify_init`,
    # `host_notify_port_destroy`, `host_request_notification`), added 0, 843 -> 840 undefined, 768 ->
    # 765 function stubs and 75 storage (unchanged), text 936996 -> 938308, image bytes 1048440
    # (**unchanged** - the growth fitted the alignment padding, the 258/260/263/266/268 shape), bss
    # `0x800ff6c8` .. `0x80134808`. The run stopped at `stub_hit=mac_policy_init` with
    # `xnu_entry_stub_caller_v=0x8000e148` = **`kernel_bootstrap+0x248`**, `kv_written=0x60`,
    # `kv_in_dram=0x84`, `kv_dropped=0`, `why_byte=0x61`, zero abort entries, and a second run
    # byte-identical in every report line (only timebase stamps differ). So `ipc_bootstrap` **returned**
    # and the whole Mach IPC bootstrap - `ipc_space_create_special` twice, `mig_init`'s 17 descriptors,
    # the table, the voucher and importance subsystems, the semaphore, the timer, the host notify -
    # ran on this device end to end. The walk is in `kernel_bootstrap` again, where `ipc_init` is real
    # since 265 and the next boundary after it is `mapping_free_prime`.
    #
    # **And the run's buffer echo is mangled, in a new way.** The last block of the report is
    # `entry_write(g_kv_buf)`, and it reads `real XNU entry8000e148t=mac_policy_init` followed by
    # ` xnu_entry_stub_caller=0x` and nothing else: the eight digits of this run's caller overwrote
    # ` stub_hit=`'s first eight characters *in place* at `g_kv_buf[0..7]` - `t=mac_policy_init` is
    # exactly ` stub_hit=mac_policy_init` from byte 8 - and the offset where those digits belong
    # (`g_kv_buf[51..58]`, `g_stub_caller_digits = 0x33` measured) was never written, so the C-string
    # echo ended at the first zero byte there. Three roads agree on that: the echoed text, the echo
    # stopping at 51, and `_w0`/`_w1` (the epilogue's word reads of `&g_kv_buf[51]` and `[55]`) both
    # reading 0 - where 273's build read the stored digits there. The instructions that do this are
    # in memory exactly as the linker wrote them (661 of 661 words of `entry_kv` through
    # `entry_stub_hit` match the ELF again, in this build's own addresses), and `kv_written`/
    # `kv_in_dram` are index values the code *computes* rather than accumulates, so they are 96 and
    # 132 either way. Which instruction's effect differs from what memory says is **not measured and
    # not claimed** - it is 271's open question with a third face: there the digit *arithmetic* was
    # wrong and the address right, here the arithmetic is right (`8000e148` is correct, in order) and
    # the address is not. The frontier result does not depend on it: the stop is named by the stub's
    # own write and the caller by `_v`, and `mac_policy_init` is what `kernel_bootstrap+0x248` says it
    # must be.
    OSFMK_KERN_HOST_NOTIFY_OBJ=${STAGE90_ENTRY_OSFMK_KERN_HOST_NOTIFY_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_host_notify.o}
    # 273: `mk_timer_init`, and the frontier leaves its function - the prediction is a tail call's.
    # 272's stop was `mk_timer_init`, and the object that defines it is `osfmk/kern/mk_timer.c`
    # (manifest:572), `osfmk_kern_mk_timer.o` - **1577 bytes of text, 8 of data, 4 of bss, 12
    # definitions and 20 references**. **2 resolved, 1 added.** The two resolved are `mk_timer_init`
    # itself and `mk_timer_port_destroy`; the one added is `mach_msg_send_from_kernel_proper`, the
    # first new undefined name since 270 and again a syscall surface rather than an init-path call.
    # Fifteen of the twenty references are already real (`zinit`, `zone_change`, `lck_spin_lock`,
    # `lck_spin_unlock`, `zalloc`, `zfree`, `mach_absolute_time`, `copyout`, `OSCompareAndSwap`,
    # `arm_usimple_lock_init`, `ipc_kobject_set_atomically`, `thread_call_setup`, `thread_call_cancel`,
    # `thread_call_enter1`, `thread_call_enter_delayed_with_leeway`) and four are already stubs
    # (`ipc_object_translate`, `ipc_port_release_send`, `mach_port_allocate_qos`, `mach_port_destroy`),
    # and all four are in trap handlers - `mk_timer_arm_trap` and its siblings - not on this path.
    # Ten of the twelve definitions are names the image has never heard of (the `*_trap` syscall
    # surface, `mk_timer_expire`, `mk_timer_qos`, `mk_timer_zone`, one string), which is 250's and
    # 270's shape again: what an object defines is not what the link needs.
    #
    # The function is `sizeof`, a no-op assert, and two calls:
    #
    #     mov r0, #104        ; s = sizeof(mk_timer_data_t)
    #     mov r1, #0x68000    ; 4096 * s  = 425984
    #     mov r2, #0x680      ; 16 * s    = 1664
    #     bl zinit("mk_timer") -> mk_timer_zone
    #     mov r1, #6          ; Z_NOENCRYPT
    #     b zone_change       ; a tail call of its own
    #
    # `assert(!(mk_timer_zone != NULL))` leaves no instruction at all - the object has no undefined
    # reference to `panic` or to an assert helper - so there is nothing between the `push` and the
    # `zinit` that could stop. **Both calls are real, and the run has already proved it**: 271's run
    # *passed* `semaphore_init` and stopped at `mk_timer_init`, and `semaphore_init`'s body is
    # `zinit` + `zone_change` too, so both callees returned on the device once. `mk_timer_init`
    # therefore completes, and the request it makes is `zinit(104, 425984, 1664, "mk_timer")` - a
    # 416 KB ceiling, the largest `max_mem` of any step so far.
    #
    # **Prediction: `stub_hit=host_notify_init`, and for the first time the caller is not in the
    # function the walk was following.** `mk_timer_init` is `ipc_bootstrap`'s last call, and the
    # compiled `ipc_bootstrap` ends with a tail call:
    #
    #     800ac250: bl mk_timer_init       ; return +0x190
    #     800ac254: pop {r4, r5, fp, lr}
    #     800ac258: b host_notify_init     ; a tail call, so no lr is set for it
    #
    # so the stub for `host_notify_init` sees `lr` = whatever that `pop` restored, which is
    # `ipc_bootstrap`'s own return address - the instruction after `bl ipc_bootstrap` in
    # `kernel_bootstrap`, and **not anything inside `ipc_bootstrap`**. In this image that is
    # `8000e134: bl ipc_bootstrap` -> return `0x8000e138`, and `kernel_bootstrap` is at `0x8000df00`,
    # so the prediction is **`kernel_bootstrap+0x238`**: offset first, because four steps in a row
    # have moved the absolute address and none has moved an offset. This is the 252 shape met from
    # the other side - 252 was a tail call *into* a function the walk had already left - and the
    # prediction has to be written against the caller's caller for the same reason.
    #
    # `host_notify_init` is a stub (`out/stage90/xnu_arm_entry_realstubs.c:233`), so the run should
    # stop there with an `lr` that `host_ipc_init` and `ipc_bootstrap` do not appear in at all.
    #
    # **273 measured it, and the prediction held to the offset.** Resolved 2, added 1, 844 -> 843
    # undefined, text 935012 -> 936996, image bytes 1048432 -> 1048440, bss `0x800ff6c8` ..
    # `0x801346c8`. The run stopped at `stub_hit=host_notify_init` with
    # `xnu_entry_stub_caller=0x8000e138` - **`kernel_bootstrap+0x238`**, the caller of the tail call
    # and not an address inside `ipc_bootstrap` - with all three roads (`_v`, `_a`, `_e`) agreeing,
    # `kv_written=0x61`, `kv_in_dram=0x85`, `kv_dropped=0`, `why_byte=0x61`, zero abort entries, and
    # `_w0=0x65303030` / `_w1=0x0a383331` = `000e138\n`, the two words 271's probe reads out of
    # `g_kv_buf`. The fifteenth consecutive prediction to hold, and the first whose answer lies in a
    # different function from the call under test.
    OSFMK_KERN_MK_TIMER_OBJ=${STAGE90_ENTRY_OSFMK_KERN_MK_TIMER_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_mk_timer.o}
    # 267: `mig_init`, and **the step is 18 objects, because the datum it reads has 17 entries.**
    # 266's stop was `mig_init`. `osfmk/kern/ipc_kobject.c` (manifest:551) is the object that
    # defines it - 2404 bytes of text, 68 of data, 12376 of bss, six functions - and it is one
    # object. But `mig_init` walks `mig_e[]`, a 17-entry array of pointers to `struct mig_subsystem`
    # defined **in that same object** (`.data`, 0x44 bytes, 17 `R_ARM_ABS32` relocations) and
    # dereferences each entry with no NULL check:
    #
    #     4c: ldr r1, [r0, #4]        ; mig_e[i]->start
    #     50: ldr r0, [r0, #8]        ; mig_e[i]->end
    #     54: cmp r1, #0
    #     5c: beq 68                  ; !start -> panic
    #     68: panic("the msgh_ids in mig_e[] aren't valid!")   (ipc_kobject.c:213)
    #
    # and ids the 17 entries' objects are in the same tree (manifest:685-705,
    # `out/mach_headers/kserver/**`): `mach_vm_server.c`, `mach_port_server.c`, `mach_host_server.c`,
    # `host_priv_server.c`, `host_security_server.c`, `clock_server.c`, `clock_priv_server.c`,
    # `processor_server.c`, `processor_set_server.c`, `device_server.c`, `lock_set_server.c`,
    # `task_server.c`, `thread_act_server.c`, `vm32_map_server.c`, `UNDReplyServer.c`,
    # `mach_voucher_server.c`, `mach_voucher_attr_control_server.c`.
    #
    # Linking `ipc_kobject.o` alone would therefore hand `mig_e[]` seventeen **storage stand-ins**,
    # each a slot of zeros, so `mig_e[0]->start` would read 0 and the run would enter XNU's *real*
    # `panic` at the first iteration. That is not a frontier stop: it is a measurement of this
    # step's own omission, and `panic` is real code, not a stub, so it would not even report a
    # `stub_hit` - it would print and spin. The **262 argument**, applied to a table of 17 rather
    # than one of 16 bytes: link the table's contents with the object that reads it.
    #
    # The cost is a large addition - **238** names, the link's own delta against exp-266 (an earlier
    # estimate of 256 in this comment counted the union before the link, not what the link after
    # exp-266's image actually added) - and it is the honest size of the step, because the whole
    # Mach IPC dispatch surface arrives with the table that indexes it.
    #
    # **Prediction: `stub_hit=ipc_table_init`** - the call after `bl mig_init` in `ipc_bootstrap`.
    #
    # Measured: resolved **2** (`mig_init`, `ipc_kobject_set`), added **238** (236 functions, 2
    # statics); 627 -> 863 undefined, 553 -> 787 function stubs, 74 -> 76 storage; text 796676 ->
    # 904036 (+107360), image 900688 -> **1015440** (+114752 = seven 16 KB blocks), bss end
    # 0x8012a4c8, args +1228800, `topOfKernelData` unchanged at +3145728, headroom 1923896. Device:
    # **`stub_hit=ipc_table_init`, `xnu_entry_stub_caller=0x800abd84`** = `ipc_bootstrap+0x180`,
    # `caller - 4` = `0x800abd80` - the tenth prediction in a row.
    #
    # **This step also had to fix the payload, and that is the bigger finding.** Its first run died
    # with no line after `mmu_high_bootstrap_collection_dependency_resolution_virt`: the entry
    # image's +114752 bytes grew the *payload* past the 2 MB high-VA alias window that `mmu.c`'s
    # `build_identity_table()` had mapped as two fixed sections since Stage86, and the alias pointer
    # the bootstrap selftest hands XNU's real root (`g_boot_args`, PA 0x20c264) became `0xc020c264`,
    # above the last mapped section. The window is now a loop over `__stage90_image_end`, the same
    # shape as the identity map beside it; `STAGE90_GIC_ALIAS_BASE` moved from 0xc0200000 to
    # 0xc0400000 to make room and is now defined once, in stage90.h; and
    # `xnu_arm_vm_init_full_pmap.c`'s copy of the same two sections got the same loop.
    #
    # The host side had a defect too, and it cost four runs: `run_and_capture.sh` summarises the
    # previous log *before* it boots, and `summarise_log`'s last statement was
    # `[[ $abort -gt 0 ]] && say ...` - status 1 when the log has no abort line, which under `set -e`
    # exited the script before `fastboot`. Four consecutive "runs" re-summarised the same stale log
    # and reported nothing; the alias fix looked like it had failed when it had never been tried.
    MIG_KSERVER_OBJS=(
        "$(kserver_obj mach/mach_vm_server.c)"
        "$(kserver_obj mach/mach_port_server.c)"
        "$(kserver_obj mach/mach_host_server.c)"
        "$(kserver_obj mach/host_priv_server.c)"
        "$(kserver_obj mach/host_security_server.c)"
        "$(kserver_obj mach/clock_server.c)"
        "$(kserver_obj mach/clock_priv_server.c)"
        "$(kserver_obj mach/processor_server.c)"
        "$(kserver_obj mach/processor_set_server.c)"
        "$(kserver_obj device/device_server.c)"
        "$(kserver_obj mach/lock_set_server.c)"
        "$(kserver_obj mach/task_server.c)"
        "$(kserver_obj mach/thread_act_server.c)"
        "$(kserver_obj mach/vm32_map_server.c)"
        "$(kserver_obj UserNotification/UNDReplyServer.c)"
        "$(kserver_obj mach/mach_voucher_server.c)"
        "$(kserver_obj mach/mach_voucher_attr_control_server.c)"
    )
    OSFMK_KERN_IPC_KOBJECT_OBJ=${STAGE90_ENTRY_OSFMK_KERN_IPC_KOBJECT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_ipc_kobject.o}

    # 264: `waitq_bootstrap`, and the frontier closes on names 262 created. 263's stop was
    # `waitq_bootstrap`, and the seven `waitq_*` boundaries 262's scheduler link added
    # (`waitq_lock`, `waitq_unlock`, `waitq_assert_wait64_locked`, `waitq_pull_thread_locked`,
    # `waitq_wakeup64_all`, `waitq_wakeup64_identify`, `waitq_wakeup64_thread`) said which object
    # answers them: `osfmk/kern/waitq.c` (manifest:603), `osfmk_kern_waitq.o`. It is 21084 bytes of
    # text, 8 of data, 172 of bss, and has **49 references - 48 of them already satisfied**, so the
    # link adds exactly one name (`waitq_set__CALLING_PREPOST_HOOK__`, called from
    # `waitq_wakeup64_identify`'s prepost-hook path, off the bootstrap path) and replaces twelve
    # stand-ins. `waitq_bootstrap` itself is 13 calls with only eight distinct targets, and every
    # one is already real: `PE_parse_boot_argn` (x4), `kernel_memory_allocate`, `panic`,
    # `hw_lock_init`, `zinit`, `zone_change`, `_consume_printf_args` (x2) and `ltable_init` (x2) -
    # the last one made real by 263, so this is the frontier resolving names the *last two* steps
    # created. **Prediction:
    # `stub_hit=ipc_bootstrap`, `xnu_entry_stub_caller=0x8000dc78`** (`caller - 4` = `0x8000dc74` =
    # `kernel_bootstrap+0x234`). Device: **`stub_hit=ipc_bootstrap`,
    # `xnu_entry_stub_caller=0x8000dc78`** = `kernel_bootstrap+0x238` - the prediction, a seventh
    # time, and the four `waitq_*` calls 262 added resolved by the object that answers them.
    #
    # Measured: resolved **12** (the seven 262 created - `waitq_lock`, `waitq_unlock`,
    # `waitq_assert_wait64_locked`, `waitq_pull_thread_locked`, `waitq_wakeup64_all`,
    # `waitq_wakeup64_identify`, `waitq_wakeup64_thread` - plus `waitq_bootstrap`, `waitq_init`,
    # `waitq_assert_wait64`, `waitq_wakeup64_one` and `_global_eventq`), added **1**
    # (`waitq_set__CALLING_PREPOST_HOOK__`); 630 -> 619 undefined, 554 -> 543 function stubs,
    # storage unchanged at 76; text 773988 -> 794436 (+20448), image 884272 -> 900664 (16 KB block
    # +8), bss end 0x8010b2c8, args +1101824, topOfKernelData unchanged at +3145728, headroom
    # 2051384, payload text 1392882. `kv_written == kv_in_dram == 0x3a`, two below 263's 0x3c:
    # `ipc_bootstrap` is two characters shorter than `waitq_bootstrap`.
    # 265: `ipc_bootstrap`, and the stop moves *inside* the new object. 264's stop was
    # `ipc_bootstrap`; `osfmk/ipc/ipc_init.c` (manifest:512), `osfmk_ipc_ipc_init.o`, is the object
    # that defines it - 804 bytes of text, 24 of data, 300 of bss, three definitions
    # (`ipc_bootstrap`, `ipc_init`, `ipc_thread_call_init`) and 27 references. Eleven of the 27 are
    # already satisfied and all eleven are real code; the **16 new** names are the whole IPC
    # subsystem's init surface (`mig_init`, `ipc_table_init`, `ipc_voucher_init`,
    # `ipc_importance_init`, `semaphore_init`, `mk_timer_init`, `host_notify_init`, `ipc_host_init`,
    # `ipc_space_create_special`, the two `ipc_space_*` statics, the five zone/data statics).
    #
    # So this step does **not** stop at the next line of `kernel_bootstrap` - it stops one call
    # *inside* the object it just linked. `ipc_bootstrap` is 22 calls - 21 `bl`s and a tail `b` - with
    # 14 distinct targets; everything before
    # `ipc_space_create_special` is satisfied (`lck_grp_attr_setdefault`, `lck_grp_init`,
    # `lck_attr_setdefault`, `lck_spin_init`, `zinit` x4, `zone_change` x5), and the first new name
    # is reached at `ipc_bootstrap+0x168`. **Prediction: `stub_hit=ipc_space_create_special`** with
    # `caller - 4` = that `bl`, i.e. `caller = ipc_bootstrap+0x16c`. Device:
    # **`stub_hit=ipc_space_create_special`, `xnu_entry_stub_caller=0x800abd70`** =
    # `ipc_bootstrap+0x16c` - the prediction, an eighth time, and the first stop that is *inside* the
    # object the step linked rather than at the next line of `kernel_bootstrap`.
    #
    # Measured: resolved **8** (`ipc_bootstrap`, `ipc_init`, `ipc_thread_call_init` plus five of the
    # object's own statics - `ipc_kernel_map`, `ipc_port_max`, `ipc_pset_max`, `ipc_space_max`,
    # `msg_ool_size_small`), added **16** (10 functions, 6 statics); 619 -> 627 undefined,
    # 543 -> 550 function stubs, 76 -> 77 storage; text 794436 -> 795556 (+1120), image 900664 ->
    # 900688 (+24), bss end 0x8010b488, args unchanged at +1101824, headroom 2050936, payload text
    # 1392906. `kv_written == kv_in_dram == 0x45`, above every previous stop because the KV buffer
    # holds a fixed 45-byte prefix followed by the stub's name verbatim, and
    # `ipc_space_create_special` (24 characters) is the longest name this frontier has stopped at.
    #
    # Note for a later step: `ipc_space_kernel` and `ipc_space_reply` are passed *by address* here
    # and today are function stubs; they are `B` symbols in `osfmk_ipc_ipc_space.o`, which is also
    # the object that defines `ipc_space_create_special` - so the step that makes the callee real
    # makes the address operands real in the same move, and no struct is ever written into a stub's
    # text.
    # 266: `ipc_space_create_special`, and a cheap step inside a subsystem that is not cheap.
    # 265's stop was `ipc_space_create_special`; `osfmk/ipc/ipc_space.c` (manifest:520),
    # `osfmk_ipc_ipc_space.o`, is 1008 bytes of text, 12 of bss, nine definitions and 17 references,
    # **13 of them already satisfied**. The four new names (`ipc_right_destroy`, `ipc_right_terminate`,
    # `ipc_table_alloc`, `ipc_table_free`) belong to `ipc_space_terminate`/`ipc_space_destroy`, which
    # the bootstrap does not call - the function this step is about calls only `zalloc` (real,
    # 0x8006f284) and `lck_spin_init` (real, 0x80012538), so it completes, and `ipc_bootstrap` calls
    # it **twice** (for `ipc_space_kernel` and `ipc_space_reply`), so both complete.
    #
    # It is also the step that retires the address-operand hazard 265 recorded: this object defines
    # `ipc_space_kernel` and `ipc_space_reply` as `B` symbols, so the two pointers `ipc_bootstrap`
    # passes stop being function stubs' addresses in the same move that makes the callee real.
    #
    # **Prediction: `stub_hit=mig_init`** with `caller - 4` = `ipc_bootstrap+0x178` (the `bl` after the
    # second `ipc_space_create_special`). Device: **`stub_hit=mig_init`,
    # `xnu_entry_stub_caller=0x800abd80`** = `ipc_bootstrap+0x17c` - the prediction, a ninth time. And
    # `mig_init`'s home is the wrong directory for its name: it is in `osfmk/kern/ipc_kobject.c`
    # (manifest:551), not `osfmk/ipc/mig*.c` - manifest:552 is `osfmk/kern/ipc_mig.c`, a different
    # object with a name one character away.
    #
    # Measured: resolved **4** (`ipc_space_create_special`, `ipc_space_zone`, `ipc_space_kernel`,
    # `ipc_space_reply`), added **4** (`ipc_right_destroy`, `ipc_right_terminate` from `ipc_right.o`;
    # `ipc_table_alloc`, `ipc_table_free` from `ipc_table.o`, which also defines the `ipc_table_init`
    # this step's tail calls next); 627 -> 627 undefined, 550 -> 553 function stubs, 77 -> 74 storage;
    # text 796676 (+1120) but **image unchanged at 900688**, args and `topOfKernelData` unchanged, bss
    # end 0x8010b3c8 (192 lower - three zero-size storage stand-ins replaced by 12 real `B` bytes),
    # headroom 2051128, payload size unchanged at 1392906. `kv_written == kv_in_dram == 0x35`, the
    # lowest recorded: the KV buffer is a fixed 45-byte prefix plus the stub's name verbatim, and
    # `mig_init` is eight characters.
    OSFMK_IPC_IPC_SPACE_OBJ=${STAGE90_ENTRY_OSFMK_IPC_IPC_SPACE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_ipc_ipc_space.o}
    OSFMK_IPC_IPC_INIT_OBJ=${STAGE90_ENTRY_OSFMK_IPC_IPC_INIT_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_ipc_ipc_init.o}
    OSFMK_KERN_WAITQ_OBJ=${STAGE90_ENTRY_OSFMK_KERN_WAITQ_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_waitq.o}
    OSFMK_KERN_LTABLE_OBJ=${STAGE90_ENTRY_OSFMK_KERN_LTABLE_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_ltable.o}
    OSFMK_KERN_KEXT_ALLOC_OBJ=${STAGE90_ENTRY_OSFMK_KERN_KEXT_ALLOC_OBJ:-$REPO_ROOT/out/xnu_kernel_obj/osfmk_kern_kext_alloc.o}
    require "$ARM_INIT_OBJ"  "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_DATA_OBJ"  "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_BCOPY_OBJ" "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_BZERO_OBJ" "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_CPU_OBJ"   "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_PE_INIT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_STRLCPY_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_STRLEN_OBJ"  "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_STRNCPY_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_STRNLEN_OBJ" "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_DEVICE_TREE_OBJ"  "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_PE_IDENTIFY_OBJ"  "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_SUBRS_OBJ"        "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_STRNCMP_OBJ"      "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_PE_GEN_OBJ"       "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_BOOTARGS_OBJ"     "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_PE_BOOTARGS_OBJ"  "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_MACHINE_ROUTINES_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_CPU_COMMON_OBJ"       "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_THREAD_OBJ"      "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_TIMER_OBJ"       "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_MACHINE_ROUTINES_ASM_OBJ" "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_ARM_RTCLOCK_OBJ"       "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_STARTUP_OBJ"      "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_TIMER_CALL_OBJ"   "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_LOCKS_OBJ"        "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_LOCKS_ARM_OBJ"         "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_ARM_TIMER_OBJ"         "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_ARM_CPUID_OBJ"         "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_ARM_MACHINE_CPUID_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_PROCESSOR_OBJ"      "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_PROCESSOR_DATA_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_MACHINE_ROUTINES_COMMON_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_ARM_VM_INIT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$LIBKERN_KERNEL_MACH_HEADER_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$VM_VM_RESIDENT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_PMAP_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_LOWMEM_VECTORS_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_PRINTF_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$BSD_KERN_SUBR_LOG_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_KERN_DEBUG_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$PEXPERT_PE_CONSISTENT_DEBUG_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$PEXPERT_PE_KPRINTF_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$PEXPERT_PE_SERIAL_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CONSOLE_VIDEO_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CONSOLE_SERIAL_GENERAL_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_ARM_IO_MAP_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_ARM_LOOSE_ENDS_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_ARM_CACHES_ASM_OBJ" "run ./tools/assemble_arm_layer.sh first"
    require "$OSFMK_ARM_CACHES_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_PRNG_RANDOM_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CCDRBG_NISTHMAC_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CCHMAC_INIT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CCSHA1_EAY_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CCHMAC_UPDATE_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CCDIGEST_UPDATE_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CCHMAC_FINAL_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CCDIGEST_FINAL_64BE_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CCHMAC_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CC_CLEAR_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_MEMSET_S_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CC_CMP_SAFE_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_BSD_DEV_UNIX_STARTUP_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$BSD_KERN_BSD_INIT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$BSD_KERN_KDEBUG_OBJ"   "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_VM_VM_INIT_OBJ"  "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_VM_VM_COMPRESSOR_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_VM_VM_MAP_OBJ"        "run ./tools/build_xnu_arm_kernel.sh first"
    require "$LIBKERN_GEN_OSATOMICOPERATIONS_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$BSD_KERN_KERN_MEMORYSTATUS_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_VM_VM_PAGEOUT_OBJ"    "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_ZALLOC_OBJ"      "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_THREAD_CALL_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_VM_VM_OBJECT_OBJ"     "run ./tools/build_xnu_arm_kernel.sh first"
    require "$BSD_KERN_SUBR_PRF_OBJ"      "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_VM_VM_KERN_OBJ"       "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_VM_VM_MAP_STORE_OBJ"  "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_VM_VM_MAP_STORE_LL_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_VM_VM_MAP_STORE_RB_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_VM_VM_USER_OBJ"       "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_KALLOC_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_VM_VM_FAULT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_VM_MEMORY_OBJECT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_VM_DEVICE_VM_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$BSD_KERN_KERN_CS_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_LEDGER_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$FIREHOSE_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$LIBKERN_OS_LOG_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$FIREHOSE_CONFIG_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_TELEMETRY_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_CONSOLE_SERIAL_CONSOLE_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_KERN_STACKSHOT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_SCHED_PRIM_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_SCHED_MULTIQ_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_LTABLE_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_WAITQ_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_IPC_IPC_INIT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_IPC_IPC_SPACE_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_IPC_KOBJECT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_IPC_IPC_TABLE_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_IPC_IPC_VOUCHER_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_IPC_IPC_IMPORTANCE_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_SYNC_SEMA_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_MK_TIMER_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_HOST_NOTIFY_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$SECURITY_MAC_BASE_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$SECURITY_MAC_LABEL_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$SECURITY_MAC_MACH_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_IPC_HOST_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_IPC_IPC_PORT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_IPC_IPC_MQUEUE_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_HOST_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_CLOCK_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_CLOCK_OLDOPS_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$BSD_KERN_KERN_NTPTIME_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_COALITION_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_TASK_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_TASK_POLICY_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_ARM_MACHINE_TASK_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_IPC_TT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$SECURITY_MAC_MACH_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$OSFMK_KERN_BSD_KERN_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$BSD_KERN_KERN_EVENT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    for _o in "${MIG_KSERVER_OBJS[@]}"; do
        require "$_o" "run ./tools/gen_mach_headers.sh and ./tools/build_xnu_arm_kernel.sh first"
    done
    require "$OSFMK_KERN_KEXT_ALLOC_OBJ"  "run ./tools/build_xnu_arm_kernel.sh first"
    LINK_OBJS+=("$ARM_INIT_OBJ" "$ARM_DATA_OBJ" "$ARM_BCOPY_OBJ" "$ARM_BZERO_OBJ" "$ARM_CPU_OBJ" \
                "$ARM_PE_INIT_OBJ" "$ARM_STRLCPY_OBJ" "$ARM_STRLEN_OBJ" "$ARM_STRNCPY_OBJ" "$ARM_STRNLEN_OBJ" "$ARM_DEVICE_TREE_OBJ" \
                "$ARM_PE_IDENTIFY_OBJ" "$ARM_SUBRS_OBJ" "$ARM_STRNCMP_OBJ" "$ARM_PE_GEN_OBJ" \
                "$ARM_BOOTARGS_OBJ" "$ARM_PE_BOOTARGS_OBJ" "$ARM_MACHINE_ROUTINES_OBJ" "$ARM_CPU_COMMON_OBJ" "$ARM_KERN_THREAD_OBJ" "$ARM_KERN_TIMER_OBJ" "$ARM_MACHINE_ROUTINES_ASM_OBJ" "$ARM_ARM_RTCLOCK_OBJ" "$ARM_KERN_STARTUP_OBJ" "$ARM_KERN_TIMER_CALL_OBJ" "$ARM_KERN_LOCKS_OBJ" "$ARM_LOCKS_ARM_OBJ" "$ARM_ARM_TIMER_OBJ" "$ARM_ARM_CPUID_OBJ" "$ARM_ARM_MACHINE_CPUID_OBJ" "$ARM_KERN_PROCESSOR_OBJ" "$ARM_KERN_PROCESSOR_DATA_OBJ" "$ARM_MACHINE_ROUTINES_COMMON_OBJ" "$ARM_ARM_VM_INIT_OBJ" "$LIBKERN_KERNEL_MACH_HEADER_OBJ" "$VM_VM_RESIDENT_OBJ" "$ARM_PMAP_OBJ" "$ARM_LOWMEM_VECTORS_OBJ" "$ARM_KERN_PRINTF_OBJ" "$BSD_KERN_SUBR_LOG_OBJ" "$ARM_KERN_DEBUG_OBJ" "$PEXPERT_PE_CONSISTENT_DEBUG_OBJ" "$PEXPERT_PE_KPRINTF_OBJ" "$PEXPERT_PE_SERIAL_OBJ" "$OSFMK_CONSOLE_VIDEO_OBJ" "$OSFMK_CONSOLE_SERIAL_GENERAL_OBJ" "$OSFMK_ARM_IO_MAP_OBJ" "$OSFMK_ARM_LOOSE_ENDS_OBJ" "$OSFMK_ARM_CACHES_ASM_OBJ" "$OSFMK_ARM_CACHES_OBJ" "$OSFMK_PRNG_RANDOM_OBJ" "$OSFMK_CCDRBG_NISTHMAC_OBJ" "$OSFMK_CCHMAC_INIT_OBJ" "$OSFMK_CCSHA1_EAY_OBJ" "$OSFMK_CCHMAC_UPDATE_OBJ" "$OSFMK_CCDIGEST_UPDATE_OBJ" "$OSFMK_CCHMAC_FINAL_OBJ" "$OSFMK_CCDIGEST_FINAL_64BE_OBJ" "$OSFMK_CCHMAC_OBJ" "$OSFMK_CC_CLEAR_OBJ" "$OSFMK_MEMSET_S_OBJ" "$OSFMK_CC_CMP_SAFE_OBJ" "$OSFMK_BSD_DEV_UNIX_STARTUP_OBJ" "$BSD_KERN_BSD_INIT_OBJ" "$BSD_KERN_KDEBUG_OBJ" "$OSFMK_VM_VM_INIT_OBJ" "$OSFMK_VM_VM_COMPRESSOR_OBJ" "$OSFMK_VM_VM_MAP_OBJ"
    "$LIBKERN_GEN_OSATOMICOPERATIONS_OBJ" "$BSD_KERN_KERN_MEMORYSTATUS_OBJ"
    "$OSFMK_VM_VM_PAGEOUT_OBJ" "$OSFMK_KERN_ZALLOC_OBJ"
    "$OSFMK_KERN_THREAD_CALL_OBJ" "$OSFMK_VM_VM_OBJECT_OBJ" "$BSD_KERN_SUBR_PRF_OBJ" \
    "$OSFMK_VM_VM_KERN_OBJ" "$OSFMK_VM_VM_MAP_STORE_OBJ" "$OSFMK_VM_VM_MAP_STORE_LL_OBJ" \
    "$OSFMK_VM_VM_MAP_STORE_RB_OBJ" "$OSFMK_VM_VM_USER_OBJ" "$OSFMK_KERN_KEXT_ALLOC_OBJ" "$OSFMK_KERN_KALLOC_OBJ" "$OSFMK_VM_VM_FAULT_OBJ" "$OSFMK_VM_MEMORY_OBJECT_OBJ" "$OSFMK_VM_DEVICE_VM_OBJ" "$BSD_KERN_KERN_CS_OBJ" "$OSFMK_KERN_LEDGER_OBJ" "$FIREHOSE_OBJ" "$FIREHOSE_CONFIG_OBJ" "$LIBKERN_OS_LOG_OBJ" "$OSFMK_KERN_TELEMETRY_OBJ" "$OSFMK_CONSOLE_SERIAL_CONSOLE_OBJ" "$OSFMK_KERN_KERN_STACKSHOT_OBJ" "$OSFMK_KERN_SCHED_PRIM_OBJ" "$OSFMK_KERN_SCHED_MULTIQ_OBJ" "$OSFMK_KERN_LTABLE_OBJ" "$OSFMK_KERN_WAITQ_OBJ" "$OSFMK_IPC_IPC_INIT_OBJ" "$OSFMK_IPC_IPC_SPACE_OBJ" "$OSFMK_KERN_IPC_KOBJECT_OBJ" "$OSFMK_IPC_IPC_TABLE_OBJ" "$OSFMK_IPC_IPC_VOUCHER_OBJ" "$OSFMK_IPC_IPC_IMPORTANCE_OBJ" "$OSFMK_KERN_SYNC_SEMA_OBJ" "$OSFMK_KERN_MK_TIMER_OBJ" "$OSFMK_KERN_HOST_NOTIFY_OBJ" "$SECURITY_MAC_BASE_OBJ" "$SECURITY_MAC_LABEL_OBJ" "$OSFMK_KERN_IPC_HOST_OBJ" "$OSFMK_KERN_HOST_OBJ" "$OSFMK_KERN_CLOCK_OBJ" "$OSFMK_KERN_CLOCK_OLDOPS_OBJ" "$BSD_KERN_KERN_NTPTIME_OBJ" "$OSFMK_KERN_COALITION_OBJ" "$OSFMK_KERN_TASK_OBJ" "$OSFMK_KERN_TASK_POLICY_OBJ" "$OSFMK_ARM_MACHINE_TASK_OBJ" "$OSFMK_KERN_IPC_TT_OBJ" "$SECURITY_MAC_MACH_OBJ" "$OSFMK_KERN_BSD_KERN_OBJ" "$OSFMK_IPC_IPC_PORT_OBJ" "$OSFMK_IPC_IPC_MQUEUE_OBJ" "$BSD_KERN_KERN_EVENT_OBJ" "${MIG_KSERVER_OBJS[@]}")

    # The RTABI aliases. Assembly, and assembled by the payload's toolchain like the vectors are,
    # since it is plain ARM with no XNU macros in it.
    run arm-none-eabi-gcc -mcpu=cortex-a15 -marm \
        -c "$BOOT_DIR/entry_arm_rtabi.s" -o "$OUT/xnu_arm_entry_rtabi.o"
    LINK_OBJS+=("$OUT/xnu_arm_entry_rtabi.o")

    # The Mach-O header `_mh_execute_header` points at. Assembly rather than C because the fields
    # are linker-script symbols, and the sizes among them are differences the linker has to fold.
    run arm-none-eabi-gcc -mcpu=cortex-a15 -marm \
        -c "$BOOT_DIR/entry_macho.s" -o "$OUT/xnu_arm_entry_macho.o"
    LINK_OBJS+=("$OUT/xnu_arm_entry_macho.o")

    require "$LIBGCC" "install the arm-none-eabi toolchain (arm-none-eabi-gcc -print-libgcc-file-name)"

    say "== pass 1: which symbols do XNU's own objects need? =="
    # The library group is in *this* link as well as the final one, and that is not a detail: pass 1
    # is what produces the undefined set the stubs are generated from, so a symbol libgcc can supply
    # is only left un-stubbed if pass 1 can see libgcc. Adding the group to the final link alone
    # changes nothing - the stub is an ordinary object definition and the linker never looks in the
    # archive for a symbol something already defines. Experiment 183 found that by doing it.
    arm-none-eabi-ld -T "$BOOT_DIR/entry.ld" --defsym=ENTRY_BASE=$ENTRY_BASE \
        -nostdlib --no-demangle \
        -o "$OUT/xnu_arm_entry_pass1.elf" "${LINK_OBJS[@]}" \
        --start-group "$LIBGCC" --end-group 2> "$OUT/xnu_arm_entry_pass1.err" || true
    grep -o "undefined reference to \`[^']*'" "$OUT/xnu_arm_entry_pass1.err" |
        sed "s/.*\`//; s/'//" | sort -u > "$OUT/xnu_arm_entry_undef.txt"
    undef=$(wc -l < "$OUT/xnu_arm_entry_undef.txt")
    if (( undef == 0 )); then
        say "  XNU's own objects link with nothing missing - no stubs needed"
    else
        say "  $undef symbol(s) undefined - see $OUT/xnu_arm_entry_undef.txt"
    fi

    # Data or function, and how much data, decided by the kernel objects rather than by a
    # hand-written list: `nm -S` over the pool gives each symbol's type and its real size.
    #
    # The size is the part that is worth this much trouble. A stand-in for a symbol has to be at
    # least as big as the thing it stands for, and the first version of this generator gave every
    # storage symbol 64 bytes - while `EntropyData` is 68 (`entropy_data_t`, random.h:47) and
    # `BootCpuData` is a whole per-CPU data area (data.s). Neither is a size this image may guess:
    # an undersized stand-in is silently overwritten by the first real code that uses it, and what
    # it overwrites is whatever the linker put next. So: size from the object that defines the
    # symbol, and if that size is not knowable, fail the build and make it a decision.
    arm-none-eabi-nm -A -S -P --defined-only \
        "$REPO_ROOT"/out/xnu_kernel_obj/*.o "$REPO_ROOT"/out/xnu_asm_obj/*.o 2>/dev/null |
        sed 's/^[^:]*: //' | awk 'NF>=2 {print $1, $2, ($4 == "" ? "-" : $4)}' |
        sort -u > "$OUT/xnu_arm_entry_kernsyms.txt"

    {
        echo '/*'
        echo ' * Generated by xnu_arm_boot/build_entry.sh from pass 1'"'"'s undefined set.'
        echo ' *'
        echo ' * One stub per symbol XNU'"'"'s own objects need and this image does not provide.'
        echo ' * Reaching any of them stops the run and writes its name, which is the measurement.'
        echo ' * Storage is sized from the defining object (`nm -S`), never guessed.'
        echo " * $undef symbol(s), from: ${LINK_OBJS[*]##*/}"
        echo ' */'
        echo '#include <stdint.h>'
        echo
        echo 'void entry_stub_hit(const char *name, uint32_t caller);'
        echo
        : > "$OUT/xnu_arm_entry_stubnames.txt"
        n_data=0
        n_func=0
        while IFS= read -r sym; do
            [[ -n $sym ]] || continue
            t=$(awk -v n="$sym" '$1==n {print $2}' "$OUT/xnu_arm_entry_kernsyms.txt" | head -1)
            sz=$(awk -v n="$sym" '$1==n {print $3}' "$OUT/xnu_arm_entry_kernsyms.txt" | head -1)
            case "$t" in
                D|B|R|S|G|C)
                    if [[ -z $sz || $sz == - || $sz == 0 ]]; then
                        say "FAIL: '$sym' is storage (nm says '$t') and its size is not in the" >&2
                        say "      objects this project builds. A stand-in of the wrong size is" >&2
                        say "      overwritten by the first real user of it, so this has to be a" >&2
                        say "      decision: add a hand-written definition with the size taken from" >&2
                        say "      its source, or link the object that defines it. See entry_stubs.c" >&2
                        say "      for how EntropyData and the stacks are handled." >&2
                        exit 1
                    fi
                    echo "/* storage: nm says '$t', size 0x$sz from the object that defines it */"
                    echo "uint8_t $sym[0x$sz] __attribute__((aligned(64)));"
                    echo
                    printf 'data %s %s 0x%s\n' "$sym" "$t" "$sz" >> "$OUT/xnu_arm_entry_stubnames.txt"
                    n_data=$((n_data + 1))
                    ;;
                *)
                    echo "void $sym(void) { entry_stub_hit(\"$sym\", (uint32_t)(uintptr_t)__builtin_return_address(0)); }"
                    printf 'func %s %s\n' "$sym" "${t:--}" >> "$OUT/xnu_arm_entry_stubnames.txt"
                    n_func=$((n_func + 1))
                    ;;
            esac
        done < "$OUT/xnu_arm_entry_undef.txt"
    } > "$OUT/xnu_arm_entry_realstubs.c"

    say "  stubs: $n_func function(s), $n_data storage"
    run arm-none-eabi-gcc -mcpu=cortex-a15 -marm -ffreestanding -fno-builtin -fno-common -fno-pic \
        -O2 -Wall -Wextra -Werror -std=gnu11 \
        -c "$OUT/xnu_arm_entry_realstubs.c" -o "$OUT/xnu_arm_entry_realstubs.o"
    LINK_OBJS+=("$OUT/xnu_arm_entry_realstubs.o")
fi


say "== linking at $ENTRY_BASE =="
# start.o first, so `_start` is the first thing in .text and the image base is the entry point -
# not required (the payload jumps to an explicit address) but it makes the map readable.
[[ $ENTRY_TRACE -eq 1 ]] && LINK_OBJS+=("$OUT/xnu_arm_entry_trace.o")
[[ -n $ENTRY_CHECKPOINT ]] && LINK_OBJS+=("$OUT/xnu_arm_entry_checkpoint.o")
run arm-none-eabi-ld -T "$BOOT_DIR/entry.ld" --defsym=ENTRY_BASE=$ENTRY_BASE \
    -nostdlib -Map "$OUT/xnu_arm_entry.map" \
    ${TRACE_LDFLAGS[@]+"${TRACE_LDFLAGS[@]}"} \
    ${CHECKPOINT_LDFLAGS[@]+"${CHECKPOINT_LDFLAGS[@]}"} \
    -o "$OUT/xnu_arm_entry.elf" \
    "${LINK_OBJS[@]}" \
    --start-group "$LIBGCC" --end-group

entry=$(arm-none-eabi-nm "$OUT/xnu_arm_entry.elf" | awk '$3=="_start"{print "0x"$1}')
bss_start=$(arm-none-eabi-nm "$OUT/xnu_arm_entry.elf" | awk '$3=="__bss_start"{print "0x"$1}')
bss_end=$(arm-none-eabi-nm "$OUT/xnu_arm_entry.elf" | awk '$3=="__bss_end"{print "0x"$1}')
text_end=$(arm-none-eabi-size "$OUT/xnu_arm_entry.elf" | awk 'NR==2{print $1}')

run arm-none-eabi-objcopy -O binary "$OUT/xnu_arm_entry.elf" "$OUT/xnu_arm_entry.bin"

bin_size=$(stat -c%s "$OUT/xnu_arm_entry.bin")
bss_bytes=$((bss_end - bss_start))

# --- the layout, derived from the image that was just linked ------------------------------------
#
# `_start`'s tables and the device tree go above the image, and the window has to cover all of it,
# because the window is what the payload maps and what `memSize` tells XNU to map for itself. Each
# of the four numbers below is the smallest value that satisfies the one before it, so growing the
# image moves them and shrinking it moves them back - nothing here can drift out of step with the
# link, which is the failure this whole block replaced.
align_up() { local v=$1 a=$2; echo $(( ((v + a - 1) / a) * a )); }
# The boot_args copy: the first page above the image, so it cannot overlap it however it grows.
ENTRY_ARGS_OFFSET=$(align_up $((bss_end - ENTRY_BASE)) 4096)
ENTRY_ARGS_OFFSET=$((ENTRY_ARGS_OFFSET + 0x1000))
# topOfKernelData: 1 MB aligned, and at least 1 MB clear of the arguments so the image has room to
# grow into before this number moves again.
ENTRY_DATA_LIMIT=$(align_up $((ENTRY_ARGS_OFFSET + ARGS_BYTES + 0x100000)) 0x100000)
# The tree: 2 MB above the tables, which is 50 times what they need and keeps the two apart in any
# map or disassembly anyone reads.
ENTRY_DT_OFFSET=$((ENTRY_DATA_LIMIT + 0x200000))
# The window: the smallest power of two that covers the tree's whole buffer, and never smaller than
# 2 MB - `_start` maps it as sections and a smaller window would put the tree outside its own map.
ENTRY_SIZE=0x00200000
while (( ENTRY_SIZE < ENTRY_DT_OFFSET + ENTRY_DT_MAX )); do ENTRY_SIZE=$((ENTRY_SIZE * 2)); done

# --- the invariants that make the layout safe ---------------------------------------------------
#
# Both failure modes are silent corruption with no cause in the log, so both are refused here. An
# image reaching past topOfKernelData is overwritten by XNU's own tables a few instructions into the
# boot; a limit set so high that the tables reach the tree is the same corruption done to the tree.
layout_fail() { say "FAIL: $*" >&2; exit 1; }
(( bss_end - ENTRY_BASE <= ENTRY_DATA_LIMIT )) ||
    layout_fail "the image ends $((bss_end - ENTRY_BASE)) bytes from the base, past topOfKernelData at $ENTRY_DATA_LIMIT"
(( ENTRY_ARGS_OFFSET + ARGS_BYTES <= ENTRY_DATA_LIMIT )) ||
    layout_fail "the boot_args at $ENTRY_ARGS_OFFSET do not fit below topOfKernelData at $ENTRY_DATA_LIMIT"
(( ENTRY_DATA_LIMIT + ENTRY_TABLE_BYTES <= ENTRY_DT_OFFSET )) ||
    layout_fail "the $ENTRY_TABLE_BYTES bytes of tables at $ENTRY_DATA_LIMIT reach the tree at $ENTRY_DT_OFFSET"
(( ENTRY_DT_OFFSET + ENTRY_DT_MAX <= ENTRY_SIZE )) ||
    layout_fail "the tree buffer at $ENTRY_DT_OFFSET (+$ENTRY_DT_MAX) is outside the $ENTRY_SIZE window"

# --- the two addresses XNU's own boot path writes to -------------------------------------------
#
# `cpu_machine_idle_init` writes four bytes to each of `gPhysBase + 0x2408` and `gPhysBase + 0x2404`
# (`osfmk/arm/cpu.c:570-580`, through two `bcopy_phys` calls whose destinations it computes from its
# own link). On this image those are `ENTRY_BASE + 0x2408` and `ENTRY_BASE + 0x2404`, and what sits
# there is the NOP pad in `entry_epilogue`'s cache sweep - which the *report* executes, after the
# writes. `entry_stubs.c` explains why the pad is a branch over NOPs rather than NOPs; this check is
# the other half of that contract, and it is what fails the build instead of the run.
#
# This is the check whose absence cost experiment 282 its bisect. The pad was added first and the
# claim made for it was that "a NOP overwritten by data is still a NOP" - true for the boot path,
# which never runs the epilogue, and false for the report path, which always does. The two
# addresses were verified to be `nop`s in the build that added the pad and never again.
#
# The check is structural now, because the fix is. The pad is not a region of NOPs whose content has
# to stay right; it is a region that is *never executed* - `entry_skip_pad` branches over it - so
# what matters is that the branch is where it is and that the two addresses fall inside what it
# skips. Content that is never fetched cannot be corrupted into meaning, which is a property no
# arrangement of NOPs has and no run-time repair can be checked for.
#
# Three things have to hold, and each has failed in a different way already:
#   1. `entry_skip_pad` begins with a branch whose target is `entry_skip_pad_end` - the pad is
#      jumped over. If the compiler ever reorders the inline asm, or a `-O` level stops honouring
#      the label pair, this is what catches it.
#   2. `entry_skip_pad` is at least four bytes *below* 0x80002404, so the branch instruction itself
#      is not one of the two words XNU overwrites. A branch at a corrupted address is a jump to
#      nowhere, which is worse than the NOP it replaced.
#   3. Both addresses XNU writes lie strictly inside the skipped range, so neither holds an
#      instruction the report executes.
verify_pad() {
    local sym addr
    local start end want branch

    sym_addr() { arm-none-eabi-nm "$OUT/xnu_arm_entry.elf" | awk -v s="$1" '$3 == s { print "0x" $1; found = 1 } END { exit(found ? 0 : 1) }'; }
    sym_word() { arm-none-eabi-objdump -d --start-address=$1 --stop-address=$(($1 + 4)) "$OUT/xnu_arm_entry.elf" \
                     | awk -v a="$(printf '%x' "$1")" '$1 == a":" { print $2 }'; }

    start=$(sym_addr entry_skip_pad)   || layout_fail "entry_skip_pad is not in the linked image"
    end=$(sym_addr entry_skip_pad_end) || layout_fail "entry_skip_pad_end is not in the linked image"

    [[ $((end - start)) -ge 256 ]] ||
        layout_fail "the skip pad is $((end - start)) bytes; it has to cover both of XNU's addresses with room to spare"

    # ARM `b <label>` is 0xEA in the top byte and a signed 24-bit word offset in the rest.
    branch=0x$(sym_word $start)
    [[ $(( (branch >> 24) & 0xff )) -eq 0xea ]] ||
        layout_fail "the word at entry_skip_pad ($(printf '0x%08x' $start), $(printf '0x%08x' $branch)) is not a branch - the pad would be executed, not skipped"
    addr=$(( start + 8 + ((((branch & 0xffffff) ^ 0x800000) - 0x800000) << 2) ))
    [[ $addr -eq $end ]] ||
        layout_fail "the branch at entry_skip_pad targets $(printf '0x%08x' $addr), not entry_skip_pad_end ($(printf '0x%08x' $end))"

    # **The two addresses XNU writes are derived here rather than written down, and since experiment
    # 291 they are pinned to a reserved slot instead of being absorbed by the pad below.** `cpu.c:570-580`
    # writes to `gPhysBase + (&ResetHandlerData.cpu_data_entries - &ExceptionLowVectorsBase)` and the
    # same with `boot_args`, so both addresses are functions of the *linked* image - and while both
    # names were stub bodies, that difference was a difference of two positions inside the generated
    # stub object. Experiment 281 measured it as 0x2404, this check compared against those literals
    # for six experiments, and 288 found it at 0x24A8 - outside the pad, with every report silent and
    # this check saying the layout was fine. That is this project's one-value-two-definitions defect,
    # and the fix with one definition per value is `entry.ld`, which now defines both names: the
    # image base, and four bytes below `__entry_reset_handler_data`. So the two addresses must come
    # out as the first two words of that sixteen-byte `.bss` slot - zeroed by the payload, never
    # executed, read by nothing. 281 (0x2404), 288 (0x24a8), 289 (0x24c0), 290 (0x2448) and 291
    # (0x2358, below the pad's start, which is what failed the build and ended the treadmill) are the
    # five values that made this a pin instead of a pad.
    local low rhd slot
    low=$(sym_addr ExceptionLowVectorsBase) || layout_fail "ExceptionLowVectorsBase is not in the linked image"
    rhd=$(sym_addr ResetHandlerData)        || layout_fail "ResetHandlerData is not in the linked image"
    slot=$(sym_addr __entry_reset_handler_data) || layout_fail "__entry_reset_handler_data is not in the linked image - entry.ld reserves it"
    [[ $low -eq $ENTRY_BASE ]] ||
        layout_fail "ExceptionLowVectorsBase is $(printf '0x%08x' $low), not the image base ($(printf '0x%08x' $ENTRY_BASE)) - cpu.c copies a page from it, so anything else copies whatever is there over page zero"

    local targets=() t
    targets+=("$((ENTRY_BASE + rhd + 4 - low))")
    targets+=("$((ENTRY_BASE + rhd + 8 - low))")

    for t in "${targets[@]}"; do
        [[ $t -ge $slot && $t -lt $((slot + 16)) ]] ||
            layout_fail "$(printf '0x%08x' $t) is outside the 16-byte reserved slot at $(printf '0x%08x' $slot) that entry.ld pins XNU's two writes to - ResetHandlerData ($(printf '0x%08x' $rhd)) and ExceptionLowVectorsBase ($(printf '0x%08x' $low)) no longer agree with it"
    done
    say "  entry_skip_pad at $(printf '0x%08x' $start) branches over $((end - start)) bytes to $(printf '0x%08x' $end)"
    say "  XNU writes $(printf '0x%08x' ${targets[0]}) and $(printf '0x%08x' ${targets[1]}) (ResetHandlerData - ExceptionLowVectorsBase = $(printf '0x%x' $((rhd - low)))), both inside the reserved slot at $(printf '0x%08x' $slot)"
}
verify_pad

cat > "$OUT/xnu_arm_entry.h" <<EOF
/* Generated by xnu_arm_boot/build_entry.sh. The XNU entry image, as data for the payload.
 *
 * Everything the payload needs to place, jump to and describe this image - including the layout
 * above it, which used to be constants in stage90.h and in xnu_entry_jump.c that had to agree.
 */
#define STAGE90_XNU_ENTRY_BASE       $ENTRY_BASE
#define STAGE90_XNU_ENTRY_SIZE       $ENTRY_SIZE
#define STAGE90_XNU_ENTRY_ENTRY      $entry
#define STAGE90_XNU_ENTRY_BSS_START  $bss_start
#define STAGE90_XNU_ENTRY_BSS_END    $bss_end
#define STAGE90_XNU_ENTRY_BIN_BYTES  $bin_size
/* The layout above the image, as offsets from STAGE90_XNU_ENTRY_BASE. */
#define STAGE90_XNU_ENTRY_ARGS_OFFSET             $ENTRY_ARGS_OFFSET
#define STAGE90_XNU_TOP_OF_KERNEL_DATA_OFFSET     $ENTRY_DATA_LIMIT
#define STAGE90_XNU_ENTRY_DT_OFFSET               $ENTRY_DT_OFFSET
#define STAGE90_XNU_ENTRY_TABLE_BYTES             $ENTRY_TABLE_BYTES
#define STAGE90_XNU_ENTRY_DT_MAX                  $ENTRY_DT_MAX
#define STAGE90_XNU_ENTRY_ARGS_BYTES              $ARGS_BYTES
EOF

say
say "entry base   $ENTRY_BASE"
say "entry point  $entry"
say "text size    $text_end bytes"
say "image bytes  $bin_size"
say "bss          $bss_start .. $bss_end ($bss_bytes bytes, zeroed by the payload)"
say "layout       args +$ENTRY_ARGS_OFFSET, topOfKernelData +$ENTRY_DATA_LIMIT, tree +$ENTRY_DT_OFFSET, window $ENTRY_SIZE"
say "headroom     $((ENTRY_BASE + ENTRY_DATA_LIMIT - bss_end)) bytes below topOfKernelData"
say "above it     $ENTRY_TABLE_BYTES bytes of page tables, then the tree buffer at ENTRY_BASE + $ENTRY_DT_OFFSET"

say "wrote $OUT/xnu_arm_entry.bin, .elf, .h, .map"
say "the payload build reads the .bin from there directly; nothing to install"

