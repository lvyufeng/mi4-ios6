#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

# The live tree: this script is in scripts/, one level below the repo root, and the
# payload it builds lives in src/ beside it. (Before the 2026-09-26 restructure this
# was stages/stageNN/, two levels down.)
SCRIPT_DIR=$PWD
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
SRC_DIR=$REPO_ROOT/src
mkdir -p $REPO_ROOT/out/stage90

# **`cd` INTO THE SOURCE TREE, because the payload's own names in this file are relative - and that is
# the shape the 2026-09-26 restructure broke here without any check seeing it.** Until then this script
# and the payload's sources were in the same directory, so `SOURCES=(start.S vectors.S ...)`,
# `-Wl,-T,linker.ld`, `--c-output macho_fixture.c` and four `-include stage90.h` all resolved against
# the stage root by virtue of where the script happened to sit. The move separated them - the script to
# `scripts/`, the sources to `src/` - and every one of those literals then pointed at a directory
# holding none of it. **Nothing in the repository could fail on it**: `tools/check_stage_paths.sh`
# looks for the old *prefix* and not one of these names carries a prefix, the entry image still built
# (it has its own directory and its own `cd`), the park is rebuilt from a record rather than from the
# tree, and `./build.sh` had not been run since the move - the 07:24 payload predates it. The first
# run after the move died in the link proof with `missing support xnu_link_support.c`, and the two
# stages before it had failed too.
#
# So this is not a prefix substitution and it is not a fifth spelling to catch: the payload's names
# are relative **to the source tree**, and the honest way to say that is to run from it. Everything
# else in this file is absolute (`$REPO_ROOT/...`, `$QCDT_DT`, `$ENTRY_BIN`, `$ENTRY_BLOB_OUT`), so
# the only things this moves are the ones meant to move. The four helper calls below are re-pointed at
# `$SCRIPT_DIR` for the same reason: they were `./xnu_*.sh` and `./` stops meaning scripts/ here.
# check_stage_paths: bare-names-resolve-against=src
cd "$SRC_DIR"

"$SCRIPT_DIR"/xnu_workspace_validate.sh $REPO_ROOT/out/stage90
"$SCRIPT_DIR"/xnu_compile_graph_scan.py $REPO_ROOT/out/stage90
"$SCRIPT_DIR"/xnu_object_subset_compile.sh $REPO_ROOT/out/stage90
"$SCRIPT_DIR"/xnu_link_proof.sh $REPO_ROOT/out/stage90


PYTHON=${PYTHON:-python3}
MACHO_FIXTURE_GEN=${MACHO_FIXTURE_GEN:-$REPO_ROOT/tools/mkmacho_fixture.py}

# boot_args must have the exact layout XNU's entry code reads. start.s loads
# physBase/virtBase/memSize/topOfKernelData by hand at fixed offsets derived from
# offsetof() in the same tree, so a field-list drift here is not a build error and not
# a visible fault - it is XNU silently using the wrong word as the physical base of
# memory. Cheap to check here, expensive to debug on the device.
if [[ -d $REPO_ROOT/external/xnu-4570.1.46 ]]; then
  "$PYTHON" $REPO_ROOT/tools/check_xnu_struct_abi.py --repo-root $REPO_ROOT
else
  echo "warning: external/xnu-4570.1.46 absent; skipping the boot_args ABI check" >&2
fi

# The device tree has to satisfy the lookups XNU's ARM platform code makes, and each
# node header's nProperties has to match what the builder emits. Both fail silently on
# the device - a missing node yields a zero SoC base or a skipped CPU, a wrong count
# makes the walker land inside a property name - so both are checked here.
if [[ -d $REPO_ROOT/external/xnu-4570.1.46 ]]; then
  "$PYTHON" $REPO_ROOT/tools/xnu_dt_requirements.py --repo-root $REPO_ROOT
else
  echo "warning: external/xnu-4570.1.46 absent; skipping the device-tree requirements check" >&2
fi

# And the stronger half of the same question: the scan above can only see that a property
# *name* exists somewhere. This walks the tree with XNU's own device-tree reader and
# checks the values, which is how device_type="timer" was found missing after the scan had
# passed it. Needs a host compiler; skipped with a warning if there is none.
if [[ -d $REPO_ROOT/external/xnu-upstream ]] && command -v "${CC_HOST:-cc}" >/dev/null 2>&1; then
  "$REPO_ROOT/tools/host_dt_check.sh"
else
  echo "warning: host compiler or xnu-upstream absent; skipping the device-tree walk check" >&2
fi

# And actually EXECUTE the Phase 2 module, rather than only checking its layout. Its one
# host-unknowable input is __stage90_image_end, which host_boot_args_check.sh reads from the
# image just built and passes by --defsym, so the module runs unmodified against the real
# layout. Needs a 32-bit host toolchain; skipped with a warning if there is none.
if [[ -d $REPO_ROOT/src ]] && "$REPO_ROOT/tools/host_boot_args_check.sh" >/dev/null 2>&1; then
  "$REPO_ROOT/tools/host_boot_args_check.sh"
else
  echo "warning: 32-bit host toolchain unavailable or the check failed; see tools/host_boot_args_check.sh" >&2
fi

# Regenerate the inert non-proprietary Mach-O fixture from the host tool so the
# checked-in macho_fixture.c stays reproducible. The raw fixture stays under
# the ignored out/ directory.
"$PYTHON" "$MACHO_FIXTURE_GEN" \
  --c-output macho_fixture.c \
  --bin-output $REPO_ROOT/out/stage90/stage90_fixture.macho \
  --symbol-prefix stage90 \
  --metadata-file $REPO_ROOT/out/stage90/xnu-link-macho-metadata.txt

# The entry image is linked by src/entry/build_entry.sh into out/stage90/, and the payload
# embeds its bytes. There used to be two copies - a committed hex array and the binary the build
# produced - with a cmp check to catch them drifting, which is the shape of mistake this project
# keeps finding. From experiment 175 there is one copy: the binary, and the array the payload
# compiles is generated from it here, every build. A payload cannot embed an image other than the
# one out/ holds, because this is where its bytes come from.
#
# The image is not committed. It is up to 5 MB of someone else's machine code, the repository
# ignores binaries (see .gitignore), and it is reproducible from committed sources by the command
# below - which is what this message says when it is missing.
ENTRY_BIN=$REPO_ROOT/out/stage90/xnu_arm_entry.bin
ENTRY_BLOB_OUT=$REPO_ROOT/out/stage90/xnu_arm_entry_blob.c
if [[ ! -f $ENTRY_BIN ]]; then
  echo "FAIL: no $ENTRY_BIN - the payload embeds the entry image, and it is not built yet." >&2
  echo "      Build it first:" >&2
  echo "        (cd src/entry && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)" >&2
  exit 2
fi
{
  echo '/* Generated by stage90/build.sh from out/stage90/xnu_arm_entry.bin. Not committed. */'
  echo '#include <stdint.h>'
  echo
  echo 'const uint8_t stage90_xnu_entry_blob[] = {'
  od -An -v -tu1 "$ENTRY_BIN" | awk '{for (i=1;i<=NF;i++) printf "    0x%02xu,", $i; print ""}'
  echo '};'
  echo 'const uint32_t stage90_xnu_entry_blob_size = '"$(stat -c%s "$ENTRY_BIN")"'u;'
} > "$ENTRY_BLOB_OUT"
echo "entry image: $(stat -c%s "$ENTRY_BIN") bytes from $ENTRY_BIN"

CC=${CC:-arm-none-eabi-gcc}
OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}
OBJDUMP=${OBJDUMP:-arm-none-eabi-objdump}
NM=${NM:-arm-none-eabi-nm}
SIZE=${SIZE:-arm-none-eabi-size}
MKBOOTIMG=${MKBOOTIMG:-mkbootimg}
QCDT_PACKER=${QCDT_PACKER:-$REPO_ROOT/tools/mkbootimg_v0_qcdt.py}
QCDT_DT=${QCDT_DT:-$REPO_ROOT/xiaomi4-cancro-backup-20260604-112053/boot-unpacked/dt.img}

CFLAGS=(
  -mcpu=cortex-a15
  -marm
  -ffreestanding
  -fno-builtin
  -fno-stack-protector
  -fno-unwind-tables
  -fno-asynchronous-unwind-tables
  -fno-pic
  -O2
  -Wall
  -Wextra
  -Werror
  -std=c11
  -I$REPO_ROOT/out/stage90
  # Only for xnu_real_dt.c's <pexpert/device_tree.h>, which needs <sys/appleapiopts.h>. Kept to
  # the two paths that header needs rather than the whole XNU tree, so the payload cannot
  # accidentally start resolving its own includes against XNU's.
  -I$SRC_DIR/shims
  -I$REPO_ROOT/external/xnu-upstream/pexpert
)

# Build a switch variant without editing stage90.h, e.g.
#   STAGE90_EXTRA_CFLAGS='-DSTAGE90_EXCLUSIVE_PROBE=1' ./build.sh
# These land in CFLAGS, so the -dM config dump below records them and
# preflight_boot_check.sh gates on what the image was actually built with.
# shellcheck disable=SC2206
if [[ -n ${STAGE90_EXTRA_CFLAGS:-} ]]; then
  CFLAGS+=($STAGE90_EXTRA_CFLAGS)
fi

LDFLAGS=(
  -nostdlib
  -Wl,-T,linker.ld
  -Wl,--build-id=none
  -Wl,-Map,$REPO_ROOT/out/stage90/stage90.map
)

SOURCES=(
  start.S
  vectors.S
  runtime.c
  ram_console.c
  cache_ops.c
  apple_dt.c
  boot_args.c
  probes.c
  timebase.c
  xnu_log.c
  xnu_timebase.c
  pexpert.c
  pe_state.c
  gic.c
  exclusive_probe.c
  hw_watchdog.c
  xnu_boot_args_conformant.c
  xnu_msm8974_shim.c
  xnu_msm8974_fiq_probe.c
  macho_fixture.c
  macho_probe.c
  mmu.c
  xnu_workspace.c
  xnu_compile_graph.c
  xnu_object_subset.c
  xnu_link.c
  xnu_bootstrap_contract.c
  xnu_pmap_bootstrap_contract.c
  xnu_pmap_table_dryrun_contract.c
  xnu_pmap_page_dryrun_contract.c
  xnu_pmap_attr_dryrun_contract.c
  xnu_pmap_multiwindow_dryrun_contract.c
  xnu_pmap_transition_dryrun_contract.c
  xnu_pexpert_hook_readiness_contract.c
  xnu_iokit_platform_scaffold_contract.c
  xnu_iokit_match_dryrun_contract.c
  xnu_iokit_registry_service_dryrun_contract.c
  xnu_iokit_provider_plane_dryrun_contract.c
  xnu_iokit_catalog_property_dryrun_contract.c
  xnu_iokit_property_inheritance_dryrun_contract.c
  xnu_iokit_registry_topology_dryrun_contract.c
  xnu_iokit_attach_start_readiness_dryrun_contract.c
  xnu_iokit_lifecycle_register_service_readiness_dryrun_contract.c
  xnu_iokit_provider_notification_delivery_readiness_dryrun_contract.c
  xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract.c
  xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract.c
  xnu_entry_start.S
  xnu_entry_stub.c
  xnu_early_pmap_platform_init.c
  xnu_pe_init_platform_false.c
  xnu_arm_init_post_pe_bootstrap.c
  xnu_arm_vm_init_full_pmap.c
  xnu_arm_vm_init_high_va_code_exec.c
  xnu_arm_vm_init_high_va_irq_handler.c
  xnu_arm_vm_init_high_va_data_abort_handler.c
  xnu_arm_vm_init_high_va_undef_handler.c
  xnu_macho_loader.c
  xnu_handoff.c
  xnu_real_dt.c
  xnu_entry_jump.c
  xnu_kernel.c
  stage90_main.c
)

# The entry image's byte array, generated above from the binary rather than committed as source.
# Listed after the static sources because it lives in out/, not in this directory - hence the
# `basename` in the object naming below, which is a no-op for every other entry here.
SOURCES+=( "$ENTRY_BLOB_OUT" )

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="$REPO_ROOT/out/stage90/$(basename "${src%.*}").o"
  OBJECTS+=("$obj")
  $CC "${CFLAGS[@]}" -c "$src" -o "$obj"
done

# Public-XNU objects, compiled by xnu_object_subset_compile.sh above with its own flags and
# linked in only when a switch asks for them. Until 2026-09-17 nothing here was linked into the
# payload at all, and the project's notes said so; STAGE90_XNU_REAL_DT is the switch that changes
# that, and it is off by default.
XNU_REAL_DT_VALUE=$($CC "${CFLAGS[@]}" -E -dM -include stage90.h - </dev/null \
  | awk '/^#define STAGE90_XNU_REAL_DT /{print $3}')
XNU_OBJECTS=()
if [[ ${XNU_REAL_DT_VALUE:-0} != 0 ]]; then
  XNU_OBJECTS=(
    $REPO_ROOT/out/stage90/xnu-objects/xnu_object_shims.o
    $REPO_ROOT/out/stage90/xnu-objects/device_tree.o
    $REPO_ROOT/out/stage90/xnu-objects/bootargs.o
    $REPO_ROOT/out/stage90/xnu-objects/pe_gen.o
    $REPO_ROOT/out/stage90/xnu-objects/arm_pe_bootargs.o
    $REPO_ROOT/out/stage90/xnu-objects/arm_pe_consistent_debug.o
  )
  echo "linking public-XNU objects: ${XNU_OBJECTS[*]##*/}"
fi

$CC "${CFLAGS[@]}" "${LDFLAGS[@]}" "${OBJECTS[@]}" "${XNU_OBJECTS[@]}" -o $REPO_ROOT/out/stage90/stage90.elf
$OBJCOPY -O binary $REPO_ROOT/out/stage90/stage90.elf $REPO_ROOT/out/stage90/stage90.bin
$OBJDUMP -d $REPO_ROOT/out/stage90/stage90.elf > $REPO_ROOT/out/stage90/stage90.disasm
$NM -n $REPO_ROOT/out/stage90/stage90.elf > $REPO_ROOT/out/stage90/stage90.symbols
$SIZE $REPO_ROOT/out/stage90/stage90.elf > $REPO_ROOT/out/stage90/stage90.size

# The high alias of the payload's own image has to cover the whole image, and both tables that
# build it loop until they reach either the image's end or `STAGE90_IMAGE_ALIAS_LIMIT`. When the
# image wins that race, nothing faults: the alias simply stops early, and whatever reads through it
# resolves into the next alias instead and returns that device's contents. Experiment 425 spent a
# hardware run on exactly this - the probe moved past a window that was 3 MB while the other table's
# was 4 MB - and the only symptom was a verification reporting a mismatch.
#
# Checked here, after the link and before the images are packed: this is the first point at which
# the image's size exists, and a build that cannot run is worth stopping. The limit is read out of
# the header rather than repeated, so there is still one definition of the window - the compiler
# expands it into an array bound, which goes to the shell as arithmetic and has no second copy to
# drift. (`-dM` would print the macro's replacement text symbolically, not its value.)
ALIAS_LIMIT_EXPR=$($CC "${CFLAGS[@]}" -E -P -include stage90.h -x c - \
    <<< 'char stage90_alias_limit_probe[STAGE90_IMAGE_ALIAS_LIMIT];' \
  | sed -n 's/^char stage90_alias_limit_probe\[\(.*\)\];$/\1/p' | tr -d 'u')
ALIAS_LIMIT=$(( ALIAS_LIMIT_EXPR ))
IMAGE_END=$($NM -n $REPO_ROOT/out/stage90/stage90.elf \
  | awk '$3 == "__stage90_image_end" { print $1 }')
if [[ -z $IMAGE_END ]]; then
  echo "error: __stage90_image_end is not in stage90.elf; the image-alias check cannot run" >&2
  exit 1
fi
IMAGE_END=$(( 16#$IMAGE_END ))
if (( IMAGE_END > ALIAS_LIMIT )); then
  printf 'error: the image ends at 0x%x, past the image-alias window limit 0x%x\n' \
    "$IMAGE_END" "$ALIAS_LIMIT" >&2
  echo "       the high alias (STAGE90_HIGH_ALIAS_BASE) would stop before the end of the image," >&2
  echo "       and a reader of that alias would get the next device alias's contents instead" >&2
  echo "       - widen STAGE90_IMAGE_ALIAS_WINDOW in stage90.h, or shrink the image" >&2
  exit 1
fi
printf 'image alias window: image ends at 0x%x, limit 0x%x, %d MB of headroom\n' \
  "$IMAGE_END" "$ALIAS_LIMIT" "$(( (ALIAS_LIMIT - IMAGE_END) / (1024 * 1024) ))"

: > $REPO_ROOT/out/stage90/empty-ramdisk

# The kernel cmdline is a claim about what this payload does, so the cache token has to follow
# the build: a run that enables the I-cache must not still advertise "no-cache-change". Read the
# switch from the preprocessor, the way the config dump at the end of this script does, so the
# two cannot drift apart.
CACHE_MODE_VALUE=$($CC "${CFLAGS[@]}" -E -dM -include stage90.h - </dev/null \
  | awk '/^#define STAGE90_CACHE_MODE /{print $3}')
# Note the value is the macro's *replacement text*, which is symbolic unless the switch was
# passed numerically, so both forms have to be recognised - the same reason the gate accepts
# both for every other switch.
case "$CACHE_MODE_VALUE" in
  STAGE90_CACHE_MODE_NONE|0|0u)               CACHE_TOKEN='no-cache-change' ;;
  STAGE90_CACHE_MODE_ICACHE|1|1u)             CACHE_TOKEN='icache-enabled' ;;
  STAGE90_CACHE_MODE_ICACHE_DCACHE|2|2u)      CACHE_TOKEN='icache-dcache-enabled' ;;
  *)                                          CACHE_TOKEN='cache-mode-unrecognised' ;;
esac

CMDLINE_BASE='stage90 mi4ios6=stage90 c-runtime apple-dt macho-fixture load-plan materialize highva-dryrun safe-table stage-owned-tables ttbr0-roundtrip recovery-table cache-bits-preserved xnu-entry-stub xnu-early-init early-pmap-platform xnu-pe-init-false xnu-postpe cpu-topo bootcpu rtclock xnu-armvm live-pmap ttbr-live tlb-live pmap-restore prevm-pexpert dtinit-facts peid-machine xnu-bs-contract xnu-pmap-bs-contract xnu-pmap-table-contract xnu-pmap-page-contract xnu-pmap-attr-contract xnu-pmap-mw-contract xnu-pmap-trans-contract xnu-pexpert-hook xnu-iokit-platform xnu-iokit-match-contract xnu-iokit-regsvc-contract xnu-iokit-provider xnu-iokit-catalog xnu-iokit-propinh xnu-iokit-regtop pexpert-hook-ready iokit-platform-scaffold iokit-match-local iokit-regsvc-local iokit-provider-local iokit-catalog-local iokit-propinh-local iokit-regtop-local irq-timer-hook pmap-bootstrap-ref public-xnu-workspace public-xnu-compile-graph public-xnu-platform-graph public-xnu-object-subset xnu-controlled-link xnu-bounded-pe-gen xnu-arm-pe-bootargs xnu-arm-consistent-debug stage90-xnu-link-proof inert-macho-fixture cancro-target arm-init-stub no-full-xnu-build no-pub-start no-pub-arm-init no-pub-thread no-pub-cpuboot no-pub-rtclock no-pub-pmap no-pub-pexpert no-pub-peinit no-pub-dtinit no-pub-peid no-pub-armvm no-iokit-runtime-exec no-macho-exec'
CMDLINE="$CMDLINE_BASE $CACHE_TOKEN no-persist-write no-external-mutation"

# -------------------------------------------------------------------------- 515: the boot argument
#
# **This check is here and not in the entry build, because the string it reads is the payload's and
# this is the script that builds it.** The entry build runs first; a check there for a token in
# `boot_args.o` would be reading the *previous* build's object - 460's defect class, a generated file
# read from before the edit that produced it. Both halves of the comparison are in hand here: the
# entry image (built above by `src/entry/build_entry.sh`) and the objects just compiled from
# `boot_args.c` and `stage90_main.c`.
#
# 515 supplies the one boot argument Apple's own idle-cache path is switched by:
# `up_style_idle_exit=1`, which `arm_init` parses (`osfmk/arm/arm_init.c:287-289`) into the global
# `caches.c:414` tests. It has to be a `name=value` token and not a bare word
# (`PE_parse_boot_argn_internal` only matches a bare word that begins with `-`), and it has to be in
# *both* command lines, because the port's own contracts read the tree's copy while XNU's parser reads
# this one. The name is taken out of the entry image rather than written here: it is the literal the
# kernel's own call site passes to that parser, so a payload token spelled differently is refused
# instead of being compared against this script's memory of it.
# **No `awk ... exit` and no `head` on these three lines, and that is a defect this step found by
# running them**: `strings` on a 5.5 MB image is still writing when its reader stops, so the reader
# closing the pipe gives it SIGPIPE, and under `set -o pipefail` the *pipeline* then fails - the
# first version of this check died with status 141 and no message, which is the same silent death
# the entry build's own clause warns about. A reader that runs to the end of the stream, and a
# `|| true` for the case where there is no match at all, are both part of the check working.
argname=$(arm-none-eabi-strings "$ENTRY_BIN" | awk '$0 == "up_style_idle_exit" { n++; if (n == 1) print }')
if [[ -z $argname ]]; then
  echo "FAIL: $ENTRY_BIN carries no literal 'up_style_idle_exit'; arm_init's own PE_parse_boot_argn" >&2
  echo "      call is what puts it there, so 515's boot argument has no name to be written against." >&2
  exit 1
fi
cmdstr=$(arm-none-eabi-strings "$REPO_ROOT/out/stage90/boot_args.o" | grep -F " $argname=1" || true)
[[ -n $cmdstr ]] || true   # the test below is the one that fails, with its own message
if [[ -z $cmdstr ]]; then
  echo "FAIL: the payload's command line does not carry ' $argname=1'." >&2
  echo "      That token is 515's repair: it is what makes caches.c:414's test true, and without it" >&2
  echo "      the boot stops on the same store to 0x130 that 514's two hardware runs stopped on." >&2
  exit 1
fi
if (( ${#cmdstr} > 255 )); then
  echo "FAIL: the command line is ${#cmdstr} characters and CommandLine is 256 bytes with its NUL." >&2
  echo "      build_boot_args copies the smaller of the two sizes and drops the tail silently, and" >&2
  echo "      this step's token is the last one in the string." >&2
  exit 1
fi
dtstr=$(arm-none-eabi-strings "$REPO_ROOT/out/stage90/stage90_main.o" | grep -F " $argname=1" || true)
[[ -n $dtstr ]] || true   # the test below is the one that fails, with its own message
if [[ -z $dtstr ]]; then
  echo "FAIL: stage90_main.o's /chosen boot-args string does not carry ' $argname=1'." >&2
  echo "      The port's own contracts read that copy (xnu_pe_init_platform_false.c:410-413," >&2
  echo "      xnu_pexpert_hook_readiness_contract.c:163-164), so a token in one copy only is a check" >&2
  echo "      and an effect about two different strings." >&2
  exit 1
fi
echo "xnu_entry_515: the payload's command line (${#cmdstr} characters) and the tree's /chosen copy"
echo "  both carry '$argname=1', the name taken from the entry image's own parse site"


$MKBOOTIMG \
  --kernel $REPO_ROOT/out/stage90/stage90.bin \
  --ramdisk $REPO_ROOT/out/stage90/empty-ramdisk \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x02000000 \
  --second_offset 0x00f00000 \
  --tags_offset 0x01e00000 \
  --pagesize 2048 \
  --cmdline "$CMDLINE" \
  --header_version 0 \
  --output $REPO_ROOT/out/stage90/stage90.img

if [[ -f "$QCDT_DT" ]]; then
  "$QCDT_PACKER" \
    --kernel $REPO_ROOT/out/stage90/stage90.bin \
    --ramdisk $REPO_ROOT/out/stage90/empty-ramdisk \
    --dt "$QCDT_DT" \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x02000000 \
    --second_offset 0x00f00000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    --cmdline "$CMDLINE" \
    --output $REPO_ROOT/out/stage90/stage90-qcdt.img
else
  echo "warning: QCDT_DT not found: $QCDT_DT; skipping stage90-qcdt.img" >&2
fi

sha256sum $REPO_ROOT/out/stage90/stage90_fixture.macho $REPO_ROOT/out/stage90/stage90.elf $REPO_ROOT/out/stage90/stage90.bin $REPO_ROOT/out/stage90/stage90.img \
  $(if [[ -f $REPO_ROOT/out/stage90/stage90-qcdt.img ]]; then printf '%s' $REPO_ROOT/out/stage90/stage90-qcdt.img; fi) \
  > $REPO_ROOT/out/stage90/SHA256SUMS.txt

# Record the switches this image was actually built with, so preflight_boot_check.sh
# can gate a hardware run on them instead of on what the source is assumed to say.
$CC "${CFLAGS[@]}" -E -dM -include stage90.h - </dev/null \
  | grep -E '^#define STAGE90_(HANDOFF_MODE|ENTRY_LADDER_LEVEL|DEADMAN_ENABLE|DEADMAN_SELFTEST|BYPASS_ENTRY_STUB|EXCLUSIVE_PROBE|PMAP_ATTR_MODE|HW_WATCHDOG|HW_WATCHDOG_SELFTEST|XNU_BOOT_ARGS|HANDOFF_FAULT_INJECT_VA|XNU_MSM8974_SHIM|CACHE_MODE|XNU_REAL_DT|XNU_ENTRY|XNU_MSM8974_FIQ_PROBE) ' \
  > $REPO_ROOT/out/stage90/stage90-build-config.txt
cat $REPO_ROOT/out/stage90/stage90-build-config.txt

ls -l $REPO_ROOT/out/stage90/stage90.elf $REPO_ROOT/out/stage90/stage90.bin $REPO_ROOT/out/stage90/stage90.img \
  $(if [[ -f $REPO_ROOT/out/stage90/stage90-qcdt.img ]]; then printf '%s' $REPO_ROOT/out/stage90/stage90-qcdt.img; fi)
cat $REPO_ROOT/out/stage90/stage90.size
cat $REPO_ROOT/out/stage90/SHA256SUMS.txt
