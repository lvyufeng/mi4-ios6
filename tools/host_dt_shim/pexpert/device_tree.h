/*
 * XNU's own device-tree header, pulled in unchanged.
 *
 * device_tree.c does `#include <pexpert/device_tree.h>`, so with -I tools/host_dt_shim it
 * resolves to this file; the quoted include below is resolved relative to *this* file's
 * directory, which is why the path climbs to the repository root and into external/.
 *
 * A real file rather than a symlink into external/: external/ is gitignored, so a
 * committed symlink would dangle in any checkout that has not fetched it. This way the
 * missing-external case is a normal compile error the harness already guards against.
 */
#include "../../../external/xnu-upstream/pexpert/pexpert/device_tree.h"
