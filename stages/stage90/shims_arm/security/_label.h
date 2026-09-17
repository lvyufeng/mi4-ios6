#ifndef MI4IOS6_SHIM_SECURITY__LABEL_H
#define MI4IOS6_SHIM_SECURITY__LABEL_H
/*
 * security/_label.h is part of the XNU security framework, which is not in the OSS tarball (the
 * security/ tree is absent entirely). osfmk/kern/exception.h includes it for the audit token's
 * label pointers.
 *
 * An opaque forward declaration is the honest stub: nothing in the entry path dereferences a
 * label, and the audit-token layout is checked by tools/check_xnu_struct_abi.py, which reads
 * XNU's own headers rather than this one. If a path ever needs the real definition, the compiler
 * will say so.
 */
struct label;
typedef struct label *label_t;
#endif
