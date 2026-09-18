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

ENTRY_BASE=0x00200000
# 2 MB: the payload's boot_args hand XNU physBase = virtBase = 0x00200000 and memSize = 0x00200000,
# so this is the only window XNU's own tables map.
ENTRY_SIZE=0x00200000

# The two numbers that bound the image, read out of stage90.h rather than repeated here, because
# the boot_args the payload hands `_start` are built from the same macros and a disagreement
# between the two is a defect this project has a name for. `topOfKernelData` is where `_start`
# puts its own page tables (`osfmk/arm/start.s:149`); everything this image owns has to end below
# it, and `_start` then writes 40 KB (ten pages) of table entries starting at it. The device tree
# is copied in above that, at ENTRY_DT_OFFSET. So there are two ways to fail and both are checked:
# an image reaching past the limit is overwritten by XNU's tables, and a limit set so high that
# the tables reach the tree is the same corruption from the other end.
stage90_macro() {
    arm-none-eabi-gcc -E -dM -I"$STAGE_DIR" -include stage90.h - </dev/null |
        awk -v n="$1" '$1 == "#define" && $2 == n {print $3}' | sed 's/[uUlL]$//'
}
ENTRY_DATA_LIMIT=$(stage90_macro STAGE90_XNU_TOP_OF_KERNEL_DATA_OFFSET)
ENTRY_DT_OFFSET=$(stage90_macro STAGE90_XNU_ENTRY_DT_OFFSET)
if [[ -z $ENTRY_DATA_LIMIT || -z $ENTRY_DT_OFFSET ]]; then
    echo "could not read the entry limits out of stage90.h - expected" >&2
    echo "STAGE90_XNU_TOP_OF_KERNEL_DATA_OFFSET and STAGE90_XNU_ENTRY_DT_OFFSET" >&2
    exit 2
fi
# What `start.s`'s invalidation loop covers, in bytes: `(PGBYTES/4 + PGBYTES/4*4) * 2` words.
ENTRY_TABLE_BYTES=0x0000A000

# `STAGE90_ENTRY_REAL_ARM_INIT=1` links XNU's own compiled objects - `arm_init.o`, `data.o`,
# `bcopy.o`, `bzero.o` - in place of the stand-ins that name themselves and return, and generates a
# reporting stub for everything still missing. Default off, so the proven image stays the default
# and the two are one variable apart.
REAL_ARM_INIT=${STAGE90_ENTRY_REAL_ARM_INIT:-0}
STUB_DEFINES=()
[[ $REAL_ARM_INIT -eq 1 ]] && STUB_DEFINES=(-DSTAGE90_ENTRY_REAL_ARM_INIT=1)

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
    require "$ARM_INIT_OBJ"  "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_DATA_OBJ"  "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_BCOPY_OBJ" "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_BZERO_OBJ" "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_CPU_OBJ"   "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_PE_INIT_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_STRLCPY_OBJ" "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_STRLEN_OBJ"  "run ./tools/assemble_arm_layer.sh first"
    require "$ARM_DEVICE_TREE_OBJ"  "run ./tools/build_xnu_arm_kernel.sh first"
    require "$ARM_PE_IDENTIFY_OBJ"  "run ./tools/build_xnu_arm_kernel.sh first"
    LINK_OBJS+=("$ARM_INIT_OBJ" "$ARM_DATA_OBJ" "$ARM_BCOPY_OBJ" "$ARM_BZERO_OBJ" "$ARM_CPU_OBJ" \
                "$ARM_PE_INIT_OBJ" "$ARM_STRLCPY_OBJ" "$ARM_STRLEN_OBJ" "$ARM_DEVICE_TREE_OBJ" \
                "$ARM_PE_IDENTIFY_OBJ")

    # The RTABI aliases. Assembly, and assembled by the payload's toolchain like the vectors are,
    # since it is plain ARM with no XNU macros in it.
    run arm-none-eabi-gcc -mcpu=cortex-a15 -marm \
        -c "$BOOT_DIR/entry_arm_rtabi.s" -o "$OUT/xnu_arm_entry_rtabi.o"
    LINK_OBJS+=("$OUT/xnu_arm_entry_rtabi.o")

    say "== pass 1: which symbols do XNU's own objects need? =="
    arm-none-eabi-ld -T "$BOOT_DIR/entry.ld" -nostdlib --no-demangle \
        -o "$OUT/xnu_arm_entry_pass1.elf" "${LINK_OBJS[@]}" 2> "$OUT/xnu_arm_entry_pass1.err" || true
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
        echo 'void entry_stub_hit(const char *name);'
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
                    echo "void $sym(void) { entry_stub_hit(\"$sym\"); }"
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
run arm-none-eabi-ld -T "$BOOT_DIR/entry.ld" -nostdlib -Map "$OUT/xnu_arm_entry.map" \
    -o "$OUT/xnu_arm_entry.elf" \
    "${LINK_OBJS[@]}"

entry=$(arm-none-eabi-nm "$OUT/xnu_arm_entry.elf" | awk '$3=="_start"{print "0x"$1}')
bss_start=$(arm-none-eabi-nm "$OUT/xnu_arm_entry.elf" | awk '$3=="__bss_start"{print "0x"$1}')
bss_end=$(arm-none-eabi-nm "$OUT/xnu_arm_entry.elf" | awk '$3=="__bss_end"{print "0x"$1}')
text_end=$(arm-none-eabi-size "$OUT/xnu_arm_entry.elf" | awk 'NR==2{print $1}')

run arm-none-eabi-objcopy -O binary "$OUT/xnu_arm_entry.elf" "$OUT/xnu_arm_entry.bin"

bin_size=$(stat -c%s "$OUT/xnu_arm_entry.bin")
bss_bytes=$((bss_end - bss_start))
if (( bin_size + bss_bytes > ENTRY_SIZE )); then
    say "FAIL: image ($bin_size) + bss ($bss_bytes) does not fit in $ENTRY_SIZE" >&2
    exit 1
fi
# The tighter limit, and the one that matters: `_start` places its page tables at topOfKernelData,
# which the payload sets to ENTRY_BASE + ENTRY_DATA_LIMIT. An image that reaches past it is
# overwritten by XNU's own tables a few instructions into the boot, which looks like corruption
# with no cause in the log.
if (( bss_end - ENTRY_BASE > ENTRY_DATA_LIMIT )); then
    say "FAIL: the image ends at $((bss_end - ENTRY_BASE)) bytes from the base, past the" >&2
    say "      $ENTRY_DATA_LIMIT bytes where _start puts its page tables" >&2
    say "      (STAGE90_XNU_TOP_OF_KERNEL_DATA_OFFSET in stage90.h). Shrink the image or raise" >&2
    say "      it there - and if you raise it, mind the check below." >&2
    exit 1
fi
# The other end of the same window. Raising the limit is how an image grows, so the number that
# must not move is the device tree's: the tables occupy ENTRY_TABLE_BYTES above the limit, and the
# tree sits at ENTRY_DT_OFFSET. A limit that leaves no gap produces the same corruption as an
# oversized image, and would do it to the tree rather than to the image.
if (( ENTRY_DATA_LIMIT + ENTRY_TABLE_BYTES > ENTRY_DT_OFFSET )); then
    say "FAIL: a limit of $ENTRY_DATA_LIMIT leaves the $((ENTRY_DATA_LIMIT + ENTRY_TABLE_BYTES))" >&2
    say "      bytes of tables _start writes overlapping the device tree at $ENTRY_DT_OFFSET." >&2
    exit 1
fi

cat > "$OUT/xnu_arm_entry.h" <<EOF
/* Generated by xnu_arm_boot/build_entry.sh. The XNU entry image, as data for the payload. */
#define STAGE90_XNU_ENTRY_BASE       $ENTRY_BASE
#define STAGE90_XNU_ENTRY_SIZE       $ENTRY_SIZE
#define STAGE90_XNU_ENTRY_ENTRY      $entry
#define STAGE90_XNU_ENTRY_BSS_START  $bss_start
#define STAGE90_XNU_ENTRY_BSS_END    $bss_end
#define STAGE90_XNU_ENTRY_BIN_BYTES  $bin_size
EOF

say
say "entry base   $ENTRY_BASE"
say "entry point  $entry"
say "text size    $text_end bytes"
say "image bytes  $bin_size"
say "bss          $bss_start .. $bss_end ($bss_bytes bytes, zeroed by the payload)"
say "data limit   $ENTRY_DATA_LIMIT (topOfKernelData - base, from stage90.h)"
say "headroom     $((ENTRY_BASE + ENTRY_DATA_LIMIT - bss_end)) bytes below topOfKernelData"
say "above it     $ENTRY_TABLE_BYTES bytes of page tables, then the tree at ENTRY_BASE + $ENTRY_DT_OFFSET"
# The blob the payload embeds. Same shape as the Mach-O fixture's generated header: a plain byte
# array, so the payload build needs no objcopy step of its own.
{
    echo '/* Generated by xnu_arm_boot/build_entry.sh. XNU'"'"'s entry image, as bytes for the payload. */'
    echo '#include <stdint.h>'
    echo
    echo 'const uint8_t stage90_xnu_entry_blob[] = {'
    od -An -v -tu1 "$OUT/xnu_arm_entry.bin" | awk '{for (i=1;i<=NF;i++) printf "    0x%02xu,", $i; print ""}'
    echo '};'
    echo 'const uint32_t stage90_xnu_entry_blob_size = '"$bin_size"'u;'
} > "$OUT/xnu_arm_entry_blob.c"

say "wrote $OUT/xnu_arm_entry.bin, .elf, .h, .map, _blob.c"
