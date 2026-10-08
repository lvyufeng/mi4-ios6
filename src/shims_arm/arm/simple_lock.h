#ifndef _MI4_COMPAT_ARM_SIMPLE_LOCK_H_
#define _MI4_COMPAT_ARM_SIMPLE_LOCK_H_

/*
 * WHY THIS FORWARDER EXISTS (913).  `build_xnu_arm_layer.sh` force-includes
 * `arm/simple_lock.h` for `decl_simple_lock_data`, which - per that script's own comment - is
 * included by NOTHING in the modern tree and so must be force-provided.  The two layouts that
 * answer the name differ:
 *
 *   xnu-4570            osfmk/arm/simple_lock.h   (types + the simple_lock_* macros)
 *   xnu-hd2-darwin13    osfmk/arm/lock.h          (the same usimple_lock_data_t / decl_simple_lock_data)
 *
 * `-I"$XNU/osfmk"` precedes `-I"$SHIMS_ARM"`, so a tree that ships `arm/simple_lock.h` uses its
 * own and this file is never reached; the older Darwin-13 tree has none here and falls through to
 * this forwarder.  It is a path adapter, not a re-declaration: the types come from the tree.
 */
#include <arm/lock.h>

#endif /* _MI4_COMPAT_ARM_SIMPLE_LOCK_H_ */