/* Stub for a header the OSS tarball does not ship.
 *
 * sys/_types.h:81 includes this, but bsd/sys/_pthread/ is absent from the tarball entirely —
 * and so are the __darwin_pthread_* base types it would name. So unlike _symbol_aliasing.h
 * this header cannot be empty: the names must exist, and this file has to define them itself
 * rather than re-exporting them from somewhere.
 *
 * They can be opaque, and that is checkable rather than assumed: nothing in osfmk/ takes
 * sizeof() of any of these or dereferences them. The only two occurrences in the whole tree
 * are comments (osfmk/mach/thread_policy.h:317-326, describing a userspace ABI). The kernel
 * passes these as handles; it never inspects them.
 *
 * Handles are declared as pointers to incomplete structs, which is the same shape Darwin
 * uses (`struct _opaque_pthread_t *`). Widths are correct for 32-bit; nothing consumes them.
 */
#ifndef _SYS__PTHREAD__PTHREAD_TYPES_H_
#define _SYS__PTHREAD__PTHREAD_TYPES_H_

struct _opaque_pthread_t;
struct _opaque_pthread_mutex_t;
struct _opaque_pthread_cond_t;
struct _opaque_pthread_condattr_t;
struct _opaque_pthread_attr_t;
struct _opaque_pthread_once_t;
struct _opaque_pthread_rwlock_t;
struct _opaque_pthread_rwlockattr_t;

typedef struct _opaque_pthread_t          *__darwin_pthread_t;
typedef struct _opaque_pthread_mutex_t    *__darwin_pthread_mutex_t;
typedef struct _opaque_pthread_cond_t     *__darwin_pthread_cond_t;
typedef struct _opaque_pthread_condattr_t *__darwin_pthread_condattr_t;
typedef struct _opaque_pthread_attr_t     *__darwin_pthread_attr_t;
typedef struct _opaque_pthread_once_t     *__darwin_pthread_once_t;
typedef struct _opaque_pthread_rwlock_t   *__darwin_pthread_rwlock_t;
typedef struct _opaque_pthread_rwlockattr_t *__darwin_pthread_rwlockattr_t;
typedef unsigned long                      __darwin_pthread_key_t;

typedef __darwin_pthread_t _pthread_t;
typedef __darwin_pthread_mutex_t _pthread_mutex_t;
typedef __darwin_pthread_cond_t _pthread_cond_t;
typedef __darwin_pthread_condattr_t _pthread_condattr_t;
typedef __darwin_pthread_attr_t _pthread_attr_t;
typedef __darwin_pthread_once_t _pthread_once_t;
typedef __darwin_pthread_rwlock_t _pthread_rwlock_t;
typedef __darwin_pthread_rwlockattr_t _pthread_rwlockattr_t;
typedef __darwin_pthread_key_t _pthread_key_t;

#endif
