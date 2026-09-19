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

**And the second defect, which experiment 335 measured.** The *kind* of an **added** name cannot be read
off the object being linked: that object only *references* it, so `nm` there says nothing about whether it
is code or storage. The first version of this file therefore called every added name a function stub —
while the build's generator classifies an undefined name by looking for it with `nm -S` **over the whole
695-object pool**, where a name nothing has linked yet may still be defined. In 335 the two answers
differed for one of five names: `_ZN10OSIterator10gMetaClassE` is referenced (and so "function") from
`iokit_Kernel_IORegistryEntry.o`, but `libkern_c++_OSIterator.o` defines it `B 0x18` — so the build added
a *storage stand-in* where this tool predicted a function stub, and both columns moved by one. That is a
one-name error with a 0x40 consequence in `.bss`, because every storage stand-in occupies a 64-byte slot.

Hence `read_pool()` below, and `--pool`: added names are classified the way the generator classifies them,
by scanning the pool for a definition and taking its `nm -S` type and size. A name found nowhere in the
pool is a function stub, which is right for a name only the C++ ABI mentions.
"""
import argparse
import subprocess
import sys

NM = 'arm-none-eabi-nm'
DEFAULT_IMAGE = 'out/stage90/xnu_arm_entry.elf'
DEFAULT_STUBNAMES = 'out/stage90/xnu_arm_entry_stubnames.txt'
DEFAULT_POOL_DIR = 'out/xnu_kernel_obj'

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


def read_pool(paths):
    """{name: (type, size)} for every name any object in the pool defines.

    The generator classifies an undefined name by looking for a definition with `nm -S` over the pool, so
    this is that lookup done once for all of them. One `nm` over every object at once is used rather than
    one per object: with more than one file nm prints a bare `path:` header line before each file's
    records and then `name type value size`, so the header lines are what delimit the files here. The
    size is printed in **hex without a prefix** (333's defect), which is why nothing in this file
    interprets it - it is reported beside the name, not arithmetic'd with.
    """
    out = nm(['-S', '-P', '--defined-only'] + list(paths))
    table = {}
    for line in out.splitlines():
        f = line.split()
        if len(f) == 1 and f[0].endswith(':'):
            continue                      # a `path:` file header, not a record
        if len(f) >= 4 and f[1] != 'U':
            table.setdefault(f[0], (f[1], f[3]))
    return table


def pool_paths(pool_dir):
    import glob
    return sorted(glob.glob(pool_dir + '/*.o'))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--against', default=DEFAULT_IMAGE, help='the image as it stands')
    ap.add_argument('--stubnames', default=DEFAULT_STUBNAMES, help="the build's stub-name list")
    ap.add_argument('--pool', default=DEFAULT_POOL_DIR,
                    help='the compiled object pool the generator classifies added names against '
                         '(empty string to skip and call every added name a function stub)')
    ap.add_argument('objects', nargs='+')
    args = ap.parse_args()

    image = read_image(args.against)
    stubs, nrec, shapes = read_stub_names(args.stubnames)
    n_func = shapes.get(3, 0)
    n_data = shapes.get(4, 0)
    print('image %s: %d symbols' % (args.against, len(image)))
    print('stubs %s: %d records (%d func, %d data)'
          % (args.stubnames, nrec, n_func, n_data))
    pool = {}
    if args.pool:
        paths = pool_paths(args.pool)
        pool = read_pool(paths)
        print('pool %s: %d objects, %d defined names' % (args.pool, len(paths), len(pool)))
    print()

    for obj in args.objects:
        defined, undef = read_object(obj)
        resolved = sorted(n for n in defined if n in stubs)
        added = sorted(n for n in undef if n not in image)
        storage = [n for n in resolved if defined[n] in STORAGE_TYPES]
        functions = [n for n in resolved if defined[n] not in STORAGE_TYPES]

        def added_kind(n):
            """How the generator will classify this reference: the pool's `nm -S` type, not the object's."""
            ntype, size = pool.get(n, (None, None))
            return 'storage' if ntype in STORAGE_TYPES else 'function', ntype, size

        new_storage = [n for n in added if added_kind(n)[0] == 'storage']
        new_funcs = [n for n in added if added_kind(n)[0] != 'storage']

        print('== %s' % obj)
        print('   %d definitions, %d references' % (len(defined), len(undef)))
        print('   resolved (%d: %d function, %d storage)'
              % (len(resolved), len(functions), len(storage)))
        for n in resolved:
            kind, ntype, size = stubs[n]
            print('      %-58s object %s, stand-in was %s %s%s'
                  % (n, defined[n], kind, ntype, '' if size is None else ' ' + size))
        print('   added (%d: %d function, %d storage)'
              % (len(added), len(new_funcs), len(new_storage)))
        for n in added:
            kind, ntype, size = added_kind(n)
            where = 'pool %s %s' % (ntype, size) if ntype else 'nowhere in the pool'
            print('      %-58s becomes a %s stub  (%s)' % (n, kind, where))
        print('   of the %d references, %d are already satisfied'
              % (len(undef), len(undef) - len(added)))
        print()
        # The base of the first pair is **the number of names the link needs and the image does not
        # provide**, which is the build's own `N symbol(s) undefined` line and therefore exactly the
        # stub list's function-plus-storage record count. It is *not* `len(image)`: the image's symbol
        # table holds every symbol it has, and the stand-ins this tool counts are definitions in it
        # (`T`/`B`) rather than undefined (`U`) names. Printing `len(image)` under the word "undefined"
        # is what this line did until experiment 355, which is a mislabel rather than a wrong delta -
        # the deltas below were always right, and the ledger's blocks were right because they take the
        # base from the build's `symbol(s) undefined` line and only the delta from here.
        print('   predicted counts: %d -> %d undefined, %d -> %d function, %d -> %d storage'
              % (n_func + n_data, n_func + n_data - len(resolved) + len(added),
                 n_func, n_func - len(functions) + len(new_funcs),
                 n_data, n_data - len(storage) + len(new_storage)))
        if new_storage:
            print('   (the %d created storage stand-in(s) cost %d x 0x40 of `.bss`, not their own sizes)'
                  % (len(new_storage), len(new_storage)))
        print()


if __name__ == '__main__':
    main()
