#!/usr/bin/env python3
"""
What one more object resolves, adds, and leaves alone — against the image as it stands.

    ./tools/entry_object_effect.py <object.o> [<object.o> ...]
    ./tools/entry_object_effect.py --against <image.elf> <object.o>

The walk links one XNU object per step, so the question asked before every device run is the same
three-way split of an object's names:

  * **resolved** — names the image currently carries as stand-ins and this object defines. These stop
    being stubs, so each retires a function body *and* its name slot, or a `.bss` slot.
  * **added** — names this object references that nothing in the image defines. These become new stubs.
  * **already** — references the image already satisfies.

**The record-shape defect this tool exists to not have.** The stub list the build generates,
`out/stage90/xnu_arm_entry_stubnames.txt`, has **two** record shapes:

    func NAME TYPE            (a function stub: three fields)
    data NAME TYPE SIZE       (a storage stand-in: four fields)

The first version of this measurement lived in a scratch script that admitted records by field count
(`if len(f) == 3`) and silently dropped every `data` line. The consequence was not an error but a
*smaller world*: a model that held 675 stubs where the image had 782, so an object that retires two
storage stand-ins was predicted to resolve 15 names instead of the measured 17 (experiment 331). A
parser that drops a whole record class cannot report that it did — the arithmetic between the model and
the build's own counts is what caught it.

Hence `read_stub_names()` below: it accepts both shapes, keeps the kind as a column, and **asserts the
total against the file's line count**, so a third shape added later fails here rather than in a
prediction. Three separate steps in this walk have now been mispredicted by a hand-written parser of a
generated list (320's linker-map parser dropped every two-line mergeable record), and the cheap check is
the one this file performs: count what you read, and compare it with what is there.

`nm` is read with `-S -P` for the object so a storage name's type is known, and the image's symbol table
is read once. Nothing here links anything: the answer is a prediction, and the ledger records it before
the build so the build can refute it.
"""
import argparse
import subprocess
import sys

NM = 'arm-none-eabi-nm'
DEFAULT_IMAGE = 'out/stage90/xnu_arm_entry.elf'
DEFAULT_STUBNAMES = 'out/stage90/xnu_arm_entry_stubnames.txt'

# `nm -S` types that mean "this name occupies storage". The build's own generator uses the same set to
# decide between a sized stand-in and a reporting stub body.
STORAGE_TYPES = set('DBRSGC')


def nm(args):
    return subprocess.run([NM] + args, capture_output=True, text=True).stdout


def read_stub_names(path):
    """Return {name: (kind, type, size)} and the record count, refusing an unknown shape."""
    out = {}
    lines = 0
    shapes = {}
    with open(path) as fh:
        for lineno, line in enumerate(fh, 1):
            fields = line.split()
            if not fields:
                continue
            lines += 1
            shapes[len(fields)] = shapes.get(len(fields), 0) + 1
            if len(fields) == 3:
                kind, name, ntype = fields
                size = None
            elif len(fields) == 4:
                kind, name, ntype, size = fields
            else:
                sys.exit('%s:%d: %d fields, not 3 (func) or 4 (data): %r'
                         % (path, lineno, len(fields), line.rstrip()))
            out[name] = (kind, ntype, size)
    if lines != len(out):
        sys.exit('%s: %d records but %d distinct names - a generated list should have one line each'
                 % (path, lines, len(out)))
    return out, lines, shapes


def read_image(path):
    image = {}
    for line in nm([path]).splitlines():
        f = line.split()
        if len(f) == 3:
            image[f[2]] = f[1]
    return image


def read_object(path):
    """{name: type} for every defined symbol, and the undefined names."""
    defined = {}
    for line in nm(['-S', '-P', '--defined-only', path]).splitlines():
        f = line.split()
        if len(f) >= 3:
            # `-P` prints `name type value size`
            defined[f[0]] = f[1]
    undef = set()
    for line in nm(['-u', '-P', path]).splitlines():
        f = line.split()
        if f:
            undef.add(f[0])
    return defined, undef


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--against', default=DEFAULT_IMAGE, help='the image as it stands')
    ap.add_argument('--stubnames', default=DEFAULT_STUBNAMES, help="the build's stub-name list")
    ap.add_argument('objects', nargs='+')
    args = ap.parse_args()

    image = read_image(args.against)
    stubs, nrec, shapes = read_stub_names(args.stubnames)
    n_func = shapes.get(3, 0)
    n_data = shapes.get(4, 0)
    print('image %s: %d symbols' % (args.against, len(image)))
    print('stubs %s: %d records (%d func, %d data)'
          % (args.stubnames, nrec, n_func, n_data))
    print()

    for obj in args.objects:
        defined, undef = read_object(obj)
        resolved = sorted(n for n in defined if n in stubs)
        added = sorted(n for n in undef if n not in image)
        storage = [n for n in resolved if defined[n] in STORAGE_TYPES]
        functions = [n for n in resolved if defined[n] not in STORAGE_TYPES]
        print('== %s' % obj)
        print('   %d definitions, %d references' % (len(defined), len(undef)))
        print('   resolved (%d: %d function, %d storage)'
              % (len(resolved), len(functions), len(storage)))
        for n in resolved:
            kind, ntype, size = stubs[n]
            print('      %-58s object %s, stand-in was %s %s%s'
                  % (n, defined[n], kind, ntype, '' if size is None else ' ' + size))
        print('   added (%d):' % len(added))
        for n in added:
            print('      %-58s becomes a %s stub'
                  % (n, 'storage' if defined.get(n) in STORAGE_TYPES else 'function'))
        print('   of the %d references, %d are already satisfied'
              % (len(undef), len(undef) - len(added)))
        print()
        print('   predicted counts: %d -> %d undefined, %d -> %d function, %d -> %d storage'
              % (len(image), len(image) - len(resolved) + len(added),
                 n_func, n_func - len(functions) + len([n for n in added
                                                        if defined.get(n) not in STORAGE_TYPES]),
                 n_data, n_data - len(storage) + len([n for n in added
                                                      if defined.get(n) in STORAGE_TYPES])))
        print()


if __name__ == '__main__':
    main()
