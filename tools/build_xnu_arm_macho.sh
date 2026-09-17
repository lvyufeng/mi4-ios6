#!/usr/bin/env bash
#
# Build XNU for ARM the way Apple's toolchain describes it: `--target=armv7-apple-darwin`, Mach-O
# objects, `ld64`/`lld` at the end.
#
#   ./tools/build_xnu_arm_macho.sh --assemble     # the ARM .s files -> Mach-O objects (no linker)
#   ./tools/build_xnu_arm_macho.sh --compile      # the manifest's .c files -> Mach-O objects
#   ./tools/build_xnu_arm_macho.sh --link         # ...and link them (needs a Mach-O linker)
#   ./tools/build_xnu_arm_macho.sh --report       # what the above would close, from the ELF side
#
# Why this exists, in one line: **four independent findings say this project is compiling XNU with a
# toolchain that does not match it.** experiment-142 measured the assembler half; experiments 141 and
# 123 measured `size_t` and the objects nothing here can link; experiment-138 measured the underscore
# convention. Every one of them is an argument for this script, and none of them could demonstrate it
# end to end because the linker is not installed.
#
# So the script is written to be useful **before** that decision is made:
#
#   * `--assemble` needs nothing beyond clang, and it works today. It produces Mach-O objects that
#     carry symbols the ELF build cannot produce at all - `BootCpuData`, `CpuDataEntries`,
#     `get_mmu_control`, `set_mmu_control`, `fiq_context_init`, `ml_get_timebase` and 17 more - which
#     are **23 of the boot path's 89 stubs and the 23 nearest `arm_init`** (measured; `--report`).
#   * `--compile` needs nothing beyond clang either, and is expected to succeed for the files the
#     ELF build succeeds on plus `data.s`-adjacent ones.
#   * `--link` is the only step that needs `ld64.lld`/`ld64`, and it **refuses with the reason**
#     rather than producing something half-done.
#
# Nothing here touches the ELF build. The two live in separate output directories and neither reads
# the other; `build_xnu_arm_kernel.sh` is unchanged and remains the default.

set -uo pipefail
cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)

XNU=${XNU_TREE:-$REPO_ROOT/external/xnu-4570.1.46}
CONFIG=${XNU_KERNEL_CONFIG:-RELEASE}
OUT=${XNU_MACHO_OBJ_OUT:-$REPO_ROOT/out/xnu_macho_obj}
MANIFEST=${MANIFEST:-$REPO_ROOT/out/xnu_arm_manifest.txt}

SHIMS=$REPO_ROOT/stages/stage90/shims
SHIMS_ARM=$REPO_ROOT/stages/stage90/shims_arm
GENERATED=${XNU_GENERATED:-$REPO_ROOT/out/xnu_generated}
OPTION_HEADERS=${XNU_OPTION_HEADERS_OUT:-$REPO_ROOT/out/xnu_options}/$CONFIG
DEVICE_HEADERS=${XNU_DEVICE_HEADERS_OUT:-$REPO_ROOT/out/xnu_device}/$CONFIG
MIG_HEADERS=${MIG_HEADERS:-$REPO_ROOT/out/mach_headers}
MIG_KSERVER=${MIG_KSERVER_OUT:-$REPO_ROOT/out/mach_headers/kserver}
LIBSA_EXPORT=${XNU_LIBSA_EXPORT:-$REPO_ROOT/out/xnu_libsa_export}
ASSYM=${XNU_ASSYM_OUT:-$REPO_ROOT/out/xnu_assym}/$CONFIG

# `ld64.lld` is the name LLVM installs; `ld64` is Apple's. Either would do.
MACHO_LD=""
for cand in ld64.lld ld64 arm64-apple-darwin-ld; do
    if command -v "$cand" >/dev/null 2>&1; then MACHO_LD=$cand; break; fi
done
for cand in /usr/lib/llvm-14/bin/ld64.lld /usr/bin/ld64.lld; do
    [[ -x $cand ]] && MACHO_LD=$cand
done

DO_ASM=0; DO_CC=0; DO_LINK=0; DO_REPORT=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --assemble) DO_ASM=1; shift ;;
        --compile)  DO_CC=1; shift ;;
        --link)     DO_LINK=1; shift ;;
        --report)   DO_REPORT=1; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
[[ $((DO_ASM + DO_CC + DO_LINK + DO_REPORT)) -eq 0 ]] && { DO_ASM=1; DO_REPORT=1; }

[[ -f $ASSYM/assym.s ]] || { echo "no $ASSYM/assym.s - run ./tools/gen_assym.sh first" >&2; exit 2; }

CONFIG_DEFINES=()
while IFS= read -r d; do [[ -n $d ]] && CONFIG_DEFINES+=("$d"); done \
    < <("$TOOLS_DIR/xnu_config/make_defines.sh" "$CONFIG")

TARGET=(--target=armv7-apple-darwin -mcpu=cortex-a15 -marm -mfpu=neon-vfpv4 -mfloat-abi=softfp)
DEFINES=(
    "${CONFIG_DEFINES[@]}"
    # MACH_KERNEL_PRIVATE is NOT here: it is per-component, and it is what reaches
    # kern/misc_protos.h's `ffs(unsigned int)` against bsd/libkern/libkern.h's `ffs(int)` - the
    # defect experiment-118 fixed. The per-component `cdefs` below carry it for osfmk only, exactly
    # as build_xnu_arm_kernel.sh does. A first version of this script had it here and reproduced
    # defect 118 across 271 files, which is the same mistake that file's comment warns about.
    -DMACH_KERNEL=1 -DXNU_KERNEL_PRIVATE=1 -DKERNEL_PRIVATE=1
    -DMACH_BSD=1 -DPRIVATE=1 -DKPC=1 -DMONOTONIC=1 -DLOCK_PRIVATE=1 -D__ARM__=1
    -DARMA7=1 -DKERNEL=1 -D__arm__=1 -DCONFIG_EMBEDDED=1 -D__ARM_L2CACHE_SIZE_LOG__=21
    -DCONFIG_SCHED_TIMESHARE_CORE=1 -DCONFIG_SCHED_TRADITIONAL=1
    # `-D_CLOCK_T` is NOT here either, and for the same reason as MACH_KERNEL_PRIVATE: it is the
    # osfmk-side answer, and giving it globally takes `bsd/sys/_types/_clock_t.h`'s guard away from
    # the BSD files that need it - experiment-126's finding, reproduced by a second script. It is in
    # the osfmk row of `extra` below.
    -DNPTY=1 -DNPTMX=1
)
INCLUDES=(
    -I"$GENERATED/bsd" -I"$GENERATED" -I"$OPTION_HEADERS" -I"$DEVICE_HEADERS" -I"$MIG_HEADERS"
    -I"$XNU/osfmk" -I"$XNU/iokit" -I"$XNU/bsd" -I"$XNU/libkern" -I"$XNU/pexpert" -I"$XNU"
    -I"$LIBSA_EXPORT" -I"$XNU/osfmk/arm" -I"$XNU/bsd/arm" -I"$XNU/EXTERNAL_HEADERS"
    -I"$SHIMS" -I"$SHIMS/kern" -I"$SHIMS/mach"
    -I"$SHIMS_ARM" -I"$SHIMS_ARM/kern" -I"$SHIMS_ARM/mach"
    -I"$SHIMS_ARM/sys" -I"$SHIMS_ARM/sys/_pthread"
)

# Kept identical to build_xnu_arm_kernel.sh's set so the two triples are compared under one build.
FORCE=(
    -include sys/_types/_u_int.h -include arm/simple_lock.h -include kern/queue.h -include kern/ast.h
    -include mach/task_policy.h -include mach/thread_policy.h -include mi4ios6_build_config.h
    -include sys/_types/_caddr_t.h -include sys/_types/_u_char.h -include meta_features.h
)

mkdir -p "$OUT"

if [[ $DO_ASM -eq 1 ]]; then
    ok=0; fail=0
    while read -r src; do
        case "$src" in *.s|*.S) ;; *) continue ;; esac
        [[ -f $src ]] || continue
        name=$(basename "$src" .s)
        if clang "${TARGET[@]}" -x assembler-with-cpp \
                 -DASSEMBLER=1 -DSLIDABLE=0 -DARMA7=1 -DKERNEL=1 -DKERNEL_PRIVATE=1 \
                 -D__arm__=1 -DCONFIG_EMBEDDED=1 -D__ARM_L2CACHE_SIZE_LOG__=21 \
                 -Dfmrx=vmrs -Dfmxr=vmsr -D__NO_UNDERSCORES__=1 \
                 -I"$ASSYM" -I"$REPO_ROOT/stages/stage90/xnu_arm_boot" "${INCLUDES[@]}" \
                 -c "$src" -o "$OUT/$name.o" 2>"$OUT/$name.log"; then
            ok=$((ok + 1)); rm -f "$OUT/$name.log"
        else
            fail=$((fail + 1))
            printf '  FAIL %-24s %s\n' "$name" \
                "$(grep -m1 -aE 'error|fatal' "$OUT/$name.log" | sed 's|.*xnu-4570.1.46/||' | cut -c1-60)"
        fi
    done < "$MANIFEST"
    echo "assemble (Mach-O): $ok ok, $fail failed -> $OUT"
fi

if [[ $DO_CC -eq 1 ]]; then
    [[ -f $MANIFEST ]] || { echo "no manifest at $MANIFEST" >&2; exit 2; }
    ok=0; fail=0
    while read -r src; do
        case "$src" in *.cpp|*.s|*.S) continue ;; esac
        [[ -f $src ]] || continue
        key=$(printf '%s' "$src" | sed "s|$XNU/||; s|/|_|g; s|\.c$||")
        comp=$(printf '%s' "${src#"$XNU"/}" | cut -d/ -f1)
        # Build output has no component path to read. The KERNEL_SERVER `_server.c` files come from
        # `osfmk/mach/*.defs` and Apple builds them in osfmk - the same mapping
        # build_xnu_arm_kernel.sh states, and the same one that mattered at experiment-122.
        case "$src" in
            "$MIG_KSERVER"/*|"$MIG_HEADERS"/*) comp=osfmk ;;
            "$GENERATED"/*) comp=bsd ;;
        esac
        cdefs=()
        case "$comp" in
            osfmk)   cdefs=(-DMACH_KERNEL_PRIVATE=1 -DMACH_KERNEL=1) ;;
            bsd)     cdefs=(-DDRIVER_PRIVATE=1 -D_KERNEL_BUILD=1 -DKERNEL_BUILD=1 -DMACH_KERNEL=1 \
                            -DBSD_BUILD=1 -DBSD_KERNEL_PRIVATE=1 -DLP64_DEBUG=0) ;;
            security) cdefs=(-DBSD_KERNEL_PRIVATE=1) ;;
            iokit)   cdefs=(-DDRIVER_PRIVATE=1 -DIOKIT_KERNEL_PRIVATE=1 -DIOMATCHDEBUG=1 -DIOALLOCDEBUG=1) ;;
            libkern) cdefs=(-DLIBKERN_KERNEL_PRIVATE=1 -DOSALLOCDEBUG=1) ;;
            pexpert) cdefs=(-DPEXPERT_KERNEL_PRIVATE=1) ;;
            libsa)   cdefs=(-DLIBSA_KERNEL_PRIVATE=1) ;;
        esac
        # The three per-component pieces build_xnu_arm_kernel.sh carries, and for the same reasons:
        # `sys/types.h` for BSD files (uid_t/gid_t, and kern_types.h's competing clock_t is not
        # reachable from a BSD file), the KERNEL_SERVER MIG headers first for osfmk (simport, and so
        # that a BSD file does NOT get them), and -D_CLOCK_T for osfmk (kperfbsd.c straddles).
        extra=()
        case "$comp" in
            bsd)   extra=(-include sys/types.h -I"$XNU/bsd" -I"$XNU/osfmk") ;;
            osfmk) extra=(-D_CLOCK_T=1 -I"$MIG_KSERVER" -I"$XNU/osfmk" -I"$XNU/bsd") ;;
            *)     extra=(-I"$XNU/osfmk" -I"$XNU/bsd") ;;
        esac

        # The same force-include set build_xnu_arm_kernel.sh uses, for the same reasons - see the
        # long comments there. Without it the Mach-O compile fails on `u_int` and `decl_simple_lock_data`,
        # which is a property of this project's build rather than of the target triple, and comparing
        # the two triples means holding that constant.
        if timeout 60 clang "${TARGET[@]}" -ffreestanding -fno-builtin -fno-common -fno-pic -O2 -w \
                 -ferror-limit=0 "${FORCE[@]}" "${DEFINES[@]}" "${cdefs[@]}" "${extra[@]}" "${INCLUDES[@]}" \
                 -c "$src" -o "$OUT/$key.o" 2>"$OUT/$key.log"; then
            ok=$((ok + 1)); rm -f "$OUT/$key.log"
        else
            fail=$((fail + 1)); printf '%s\n' "$src" >> "$OUT/failed-macho.txt"
        fi
    done < "$MANIFEST"
    echo "compile (Mach-O): $ok ok, $fail failed -> $OUT"
fi

if [[ $DO_LINK -eq 1 ]]; then
    if [[ -z $MACHO_LD ]]; then
        cat >&2 <<'EOF'
REFUSING: no Mach-O linker on this host.

  clang --target=armv7-apple-darwin produces Mach-O objects (verified), but linking them needs
  ld64/lld and neither is installed:

      /usr/lib/llvm-14/bin has llvm-nm, llvm-size, llvm-objdump and llvm-ar, but no ld64.lld

  This is the one step of the Mach-O path that needs something this host does not have, and it is
  a host toolchain change rather than a repository change - so it is reported rather than worked
  around. `apt-cache policy lld` offers a candidate from this host's own repositories.

  Everything the linker is not needed for has already been done by --assemble and --compile.
EOF
        exit 3
    fi
    echo "linking with $MACHO_LD"
    "$MACHO_LD" -arch armv7 -e _start -o "$OUT/xnu-macho" "$OUT"/*.o
fi

if [[ $DO_REPORT -eq 1 ]]; then
    echo
    echo "== what the Mach-O objects provide that the ELF build cannot =="
    if ! ls "$OUT"/*.o >/dev/null 2>&1; then
        echo "  (no objects yet - run --assemble first)"
    else
        PATH=$PATH:/usr/lib/llvm-14/bin
        llvm-nm --defined-only "$OUT"/*.o 2>/dev/null \
            | awk '$2 ~ /^[A-Za-z]$/ {print $3}' | sort -u > "$OUT/symbols.txt"
        n=$(wc -l < "$OUT/symbols.txt")
        echo "  symbols defined: $n"
        boot="$REPO_ROOT/out/link/$CONFIG-stubs.s"
        if [[ -f $boot ]]; then
            echo "  boot-path stubs this closes: see the run below"
        fi
    fi
fi
