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
    "$OSFMK_VM_VM_MAP_STORE_RB_OBJ" "$OSFMK_VM_VM_USER_OBJ" "$OSFMK_KERN_KEXT_ALLOC_OBJ" "$OSFMK_KERN_KALLOC_OBJ" "$OSFMK_VM_VM_FAULT_OBJ" "$OSFMK_VM_MEMORY_OBJECT_OBJ" "$OSFMK_VM_DEVICE_VM_OBJ" "$BSD_KERN_KERN_CS_OBJ" "$OSFMK_KERN_LEDGER_OBJ" "$FIREHOSE_OBJ" "$FIREHOSE_CONFIG_OBJ" "$LIBKERN_OS_LOG_OBJ" "$OSFMK_KERN_TELEMETRY_OBJ" "$OSFMK_CONSOLE_SERIAL_CONSOLE_OBJ" "$OSFMK_KERN_KERN_STACKSHOT_OBJ" "$OSFMK_KERN_SCHED_PRIM_OBJ" "$OSFMK_KERN_SCHED_MULTIQ_OBJ" "$OSFMK_KERN_LTABLE_OBJ" "$OSFMK_KERN_WAITQ_OBJ" "$OSFMK_IPC_IPC_INIT_OBJ" "$OSFMK_IPC_IPC_SPACE_OBJ" "$OSFMK_KERN_IPC_KOBJECT_OBJ" "$OSFMK_IPC_IPC_TABLE_OBJ" "$OSFMK_IPC_IPC_VOUCHER_OBJ" "$OSFMK_IPC_IPC_IMPORTANCE_OBJ" "$OSFMK_KERN_SYNC_SEMA_OBJ" "$OSFMK_KERN_MK_TIMER_OBJ" "${MIG_KSERVER_OBJS[@]}")

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
run arm-none-eabi-ld -T "$BOOT_DIR/entry.ld" --defsym=ENTRY_BASE=$ENTRY_BASE \
    -nostdlib -Map "$OUT/xnu_arm_entry.map" \
    ${TRACE_LDFLAGS[@]+"${TRACE_LDFLAGS[@]}"} \
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

