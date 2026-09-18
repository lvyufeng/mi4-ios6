#!/usr/bin/env bash
#
# Resolve an address the device reported into `function+0xNN` for the entry image.
#
# Experiment 244 makes every generated stub report the `lr` it was entered with
# (`xnu_entry_stub_caller`), because the symbol name alone stopped being enough the moment the
# frontier left `arm_init`: the stop is a stub several frames down, and `stub_hit=<symbol>` names a
# symbol and never a caller (experiment 206's lesson). The address in that key is the return address
# of a `bl` - the instruction *after* the call - so the call itself is at `addr - 4`, and this script
# prints both readings rather than making the reader remember which is which.
#
# It resolves against the ELF the device ran (`out/stage90/xnu_arm_entry.elf`), by default the
# current one. A run's own image is the only thing that can resolve its addresses: the same number
# means a different function after any object is linked.
#
# What it does and does not know:
#
#   - it uses the ELF's own symbol table, so it resolves *code* symbols (T/t/W) and nothing else. An
#     address that lands between two functions is reported with the gap ("+0xNN, past the end"), not
#     rounded into the next one, because that answer means the caller's address is wrong rather than
#     the symbol being unknown;
#   - it is a *symbol* resolver, not a line resolver: the entry objects are compiled without
#     `-g`, so there is no line table to read. `function+0xNN` is the same unit the experiments
#     quote from `objdump -d`, and that is the point - the two can be compared by eye.
#
# Usage: ./host_resolve_entry_addr.sh 0x80040374 [more addresses...]
#        ./host_resolve_entry_addr.sh --elf out/stage90/xnu_arm_entry.elf 0x80040374

set -euo pipefail

cd "$(dirname "$0")"
TOOLS_DIR=$PWD
REPO_ROOT=$(cd "$TOOLS_DIR/.." && pwd)
ELF=${ELF:-$REPO_ROOT/out/stage90/xnu_arm_entry.elf}
NM=${NM:-arm-none-eabi-nm}
PYTHON=${PYTHON:-python3}

if [[ ${1:-} == --elf ]]; then
    ELF=${2:?--elf needs a path}
    shift 2
fi
if [[ $# -eq 0 ]]; then
    sed -n '2,32p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
fi
if [[ ! -f "$ELF" ]]; then
    echo "host_resolve_entry_addr: no $ELF - build the entry image first" >&2
    exit 2
fi

"$NM" -S -P --defined-only "$ELF" > "$ELF.nmres"
trap 'rm -f "$ELF.nmres"' EXIT

"$PYTHON" - "$ELF" "$ELF.nmres" "$@" <<'PY'
import sys

elf, nmpath = sys.argv[1], sys.argv[2]
want = sys.argv[3:]

# `nm -S -P` is `name type value size`, all in hex without a prefix, and the size is empty for a
# symbol the object did not give one (a `-S` on a local label, mostly). That empty column is why the
# fields are indexed rather than unpacked: the fourth field may not exist.
code = []
for line in open(nmpath):
    parts = line.split()
    if len(parts) < 3:
        continue
    name, typ, value = parts[0], parts[1], parts[2]
    if typ not in ("T", "t", "W", "w"):
        continue
    size = int(parts[3], 16) if len(parts) > 3 else 0
    code.append((int(value, 16), name, size))
code.sort()

def resolve(addr):
    best = None
    for base, name, size in code:
        if base > addr:
            break
        best = (base, name, size)
    if best is None:
        return "below the first code symbol in %s" % elf
    base, name, size = best
    off = addr - base
    if size and off >= size:
        return "%s+0x%x (0x%x past the end of %s, which is 0x%x bytes)" % (
            name, off, off - size, name, size)
    return "%s+0x%x" % (name, off)

for a in want:
    addr = int(a, 16) if a.lower().startswith("0x") else int(a, 0)
    print("0x%08x  %s" % (addr, resolve(addr)))
    # The `lr` of a `bl` is the instruction after it, so the call site is four bytes below. Both
    # readings are printed because both are asked for: the first is the key's own value, the second
    # is the call to look up in a disassembly.
    if addr >= 4:
        print("          caller-4 = 0x%08x  %s   <- the `bl`, if the call was one"
              % (addr - 4, resolve(addr - 4)))
PY
