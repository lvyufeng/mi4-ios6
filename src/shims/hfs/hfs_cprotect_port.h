/*
 * The HFS+ port's cprotect additions for 4570 (experiments 865/877).
 *
 * WHY THIS EXISTS.  2050's HFS directly manipulates the content-protection layer: its
 * `bsd/hfs/hfs_cprotect.c` defines `struct cprotect`, `struct cp_wrap_func`, `struct cp_root_xattr`
 * and the `CP_*` flags itself-adjacent, in `bsd/sys/cprotect.h`.  **4570 restructured that layer** -
 * `bsd/sys/cprotect.h` (191 lines vs 2050's 204) is now an OPAQUE kext-facing API (`cpx_*`), and every
 * structure HFS touches was removed.  So 2050's HFS does not compile against 4570's header, and the
 * port cannot rename to an equivalent: there is none.  This header is the port's OWN view of that
 * layer, restored for HFS only.  It is force-included for `bsd/hfs/*` and `bsd/vfs/vfs_journal.c`
 * ALONE (beside `hfs_port_force.h`), so no other file in the kernel sees a second `struct bufattr`
 * era of cprotect.
 *
 * WHAT IS HONEST ABOUT IT.  This is a **compile-level** restoration: the struct layouts and constants
 * are 2050's, byte for byte, because that is what the HFS sources were written against.  It is NOT a
 * claim that 4570's cprotect engine still drives them - `cp_is_valid_class` alone collides with
 * 4570's own global two-argument version and is renamed by the stager (`tools/stage_hfs.py`), and the
 * extern AKS entry points below are resolved at the LINK.  Everything here is a NAME the HFS sources
 * use; whether the underlying machinery runs is the cprotect work this port still owes.
 *
 * NOTHING HERE EDITS AN APPLE SOURCE.  Every row names the 2050 file and line it comes from.
 */
#ifndef _HFS_CPROTECT_PORT_H_
#define _HFS_CPROTECT_PORT_H_

/* The port header is included ONLY for the HFS files (the build's per-file `-include`), and only when
 * the port is on, so it may assume the BSD/kernel context those files compile in.  It pulls the two
 * things `bsd/sys/cprotect.h` reached in 2050 and no longer does: the on-disk protection classes
 * (4570 moved them to `content_protection.h`, whose macros are IDENTICAL - `PROTECTION_CLASS_A` is 1
 * in both, so those five the compiler flagged are already satisfied by including it) and SHA1, which
 * hfs_cprotect.c uses at :1371-1377 for the offset-IV key derivation. */
#include <sys/content_protection.h>     /* PROTECTION_CLASS_* (4570 has them here) */
#include <libkern/crypto/sha1.h>        /* SHA1_CTX / SHA1Init / SHA1Update / SHA1Final */
#include <crypto/aes.h>                 /* aes_encrypt_ctx (4570's cprotect.h pulls this too) */
#include <sys/cprotect.h>               /* CP_MAX_WRAPPEDKEYSIZE and 4570's own cp_* API */

/* ------------------------------------------------------------------------------------------------
 * 1. The constants 2050's cprotect.h defined and 4570 dropped.
 *    NONE of these is redefined if 4570 already has it - the guard is per-name, because a name that
 *    is one value in two headers is the silent defect this project pays for most often.
 * --------------------------------------------------------------------------------------------- */
#ifndef CP_IV_KEYSIZE
#define CP_IV_KEYSIZE 20                /* 2050 bsd/sys/cprotect.h:43 - SHA1 pushes 20 bytes */
#endif
#ifndef CP_MAX_KEYSIZE
#define CP_MAX_KEYSIZE 32               /* 2050 :44 */
#endif
/* CP_MAX_WRAPPEDKEYSIZE (128) is ALREADY in 4570's cprotect.h - do not redefine it. */
#ifndef CP_INITIAL_WRAPPEDKEYSIZE
#define CP_INITIAL_WRAPPEDKEYSIZE 40    /* 2050 :46 */
#endif
#ifndef CP_V2_WRAPPEDKEYSIZE
#define CP_V2_WRAPPEDKEYSIZE 40         /* 2050 :47 */
#endif

#ifndef CP_LOCKED_KEYCHAIN
#define CP_LOCKED_KEYCHAIN 0            /* 2050 :54 */
#endif
#ifndef CP_UNLOCKED_KEYCHAIN
#define CP_UNLOCKED_KEYCHAIN 1          /* 2050 :55 */
#endif

/* `struct cprotect` cp_flags.  Four bits in the low nibble, then one more. */
#ifndef CP_NEEDS_KEYS
#define CP_NEEDS_KEYS 0x1               /* 2050 :58 - file needs persistent keys */
#endif
#ifndef CP_KEY_FLUSHED
#define CP_KEY_FLUSHED 0x2              /* 2050 :59 - unwrapped key purged from memory */
#endif
#ifndef CP_NO_XATTR
#define CP_NO_XATTR 0x4                 /* 2050 :60 - key info not saved as an EA */
#endif
#ifndef CP_OFF_IV_ENABLED
#define CP_OFF_IV_ENABLED 0x8           /* 2050 :61 - use the offset-IV route */
#endif
#ifndef CP_RELOCATION_INFLIGHT
#define CP_RELOCATION_INFLIGHT 0x10     /* 2050 :63 - offset-IV file being relocated */
#endif

/* Content Protection VNOP operation flags. */
#ifndef CP_READ_ACCESS
#define CP_READ_ACCESS 0x1              /* 2050 :66 */
#endif
#ifndef CP_WRITE_ACCESS
#define CP_WRITE_ACCESS 0x2             /* 2050 :67 */
#endif

/* The on-disk xattr name and the version the readers accept. */
#ifndef CONTENT_PROTECTION_XATTR_NAME
#define CONTENT_PROTECTION_XATTR_NAME "com.apple.system.cprotect"   /* 2050 :72 */
#endif
#ifndef CP_NEW_MAJOR_VERS
#define CP_NEW_MAJOR_VERS 4             /* 2050 :73 */
#endif
#ifndef CP_PREV_MAJOR_VERS
#define CP_PREV_MAJOR_VERS 2            /* 2050 :74 */
#endif
#ifndef CP_MINOR_VERS
#define CP_MINOR_VERS 0                 /* 2050 :75 */
#endif

/* ------------------------------------------------------------------------------------------------
 * 2. The types 2050's cprotect.h declared.  `cprotect_t` and `cp_wrap_func_t` exist in 4570 (the
 *    latter from `hfs_port_force.h`); the rest are the port's.
 * --------------------------------------------------------------------------------------------- */
typedef struct cp_global_state *cp_global_state_t;   /* 2050 :78 */
typedef struct cp_xattr *cp_xattr_t;                 /* 2050 :79 */
typedef struct cnode *cnode_ptr_t;                   /* 2050 :81 */

struct hfsmount;   /* forward declared, as 2050 did (:83): the structs below name it by pointer */

/* The wrappers, invoked by the AKS kext.  2050's signatures (:86-87) - but **4570's `cprotect.h`
 * already defines `unwrapper_t`/`rewrapper_t`/`new_key_t`/... with a `cp_cred_t` first argument**
 * (its opaque kext API), so only the two 2050 names 4570 does NOT carry (and nothing in the staged
 * HFS names the 4570 ones - measured) are declared here.  `unwrapper_t` is deliberately NOT redefined:
 * a second definition of it is a redefinition error, and 4570's own is visible to every file anyway. */
typedef int wrapper_t(uint32_t properties, uint64_t file_id, void *key_bytes, size_t key_length,
                      void *wrapped_data, size_t *wrapped_length);
/* 2050's `unwrapper_t` shape under a PORT-LOCAL name, because the name `unwrapper_t` is 4570's.  The
 * `struct cp_wrap_func` field below reads it, so it must keep 2050's argument list. */
typedef int hfs_unwrapper_t(uint32_t properties, void *wrapped_data, size_t wrapped_data_length,
                            void *key_bytes, size_t *key_length);

/* ------------------------------------------------------------------------------------------------
 * 3. The structures the HFS sources read and write.  Layouts are 2050's, verbatim: the HFS code
 *    indexes into them and passes them to IOKit through `bufattr`, so a change here would be read at
 *    the wrong offset rather than fail to compile.
 * --------------------------------------------------------------------------------------------- */
struct cprotect {                       /* 2050 bsd/sys/cprotect.h:104 */
	uint32_t        cp_flags;
	uint32_t        cp_pclass;
	aes_encrypt_ctx cp_cache_iv_ctx;
	uint32_t        cp_cache_key_len;
	uint8_t         cp_cache_key[CP_MAX_KEYSIZE];
	uint32_t        cp_persistent_key_len;
	uint8_t         cp_persistent_key[];
};

struct cp_wrap_func {                   /* 2050 :114 */
	wrapper_t     *wrapper;
	hfs_unwrapper_t *unwrapper;
};

struct cp_global_state {                /* 2050 :119 */
	uint8_t   wrap_functions_set;
	uint8_t   lock_state;
	u_int16_t reserved;
};

/* On-disk per-file EA payloads (2050 :124/:139).  All multi-byte fields are stored little-endian and
 * are swapped by the HFS readers, not here. */
struct cp_xattr_v2 {                    /* 2050 :124 */
	u_int16_t xattr_major_version;
	u_int16_t xattr_minor_version;
	u_int32_t flags;
	u_int32_t persistent_class;
	u_int32_t key_size;
	uint8_t   persistent_key[CP_V2_WRAPPEDKEYSIZE];
};

struct cp_xattr_v4 {                    /* 2050 :139 */
	u_int16_t xattr_major_version;
	u_int16_t xattr_minor_version;
	u_int32_t flags;
	u_int32_t persistent_class;
	u_int32_t key_size;
	u_int32_t reserved1;
	u_int32_t reserved2;
	u_int32_t reserved3;
	u_int32_t reserved4;
	u_int32_t reserved5;
	uint8_t   persistent_key[CP_MAX_WRAPPEDKEYSIZE];
};

struct cp_root_xattr {                  /* 2050 :155 */
	u_int16_t major_version;
	u_int16_t minor_version;
	u_int64_t flags;
	u_int32_t reserved1;
	u_int32_t reserved2;
	u_int32_t reserved3;
	u_int32_t reserved4;
};

/* ------------------------------------------------------------------------------------------------
 * 4. The prototypes 2050's cprotect.h carried (:167-200) that HFS calls.  They are needed not only as
 *    declarations but as a FIX: `hfs_cprotect.c` calls `cp_entry_create_keys` at :168, before its own
 *    definition at :213, so without a prototype the call is implicit-`int` and the definition then
 *    "conflicts" with it.  Declaring the two here is what makes them one function.
 *
 *    Only the two the compiler named are declared; the rest of 2050's cprotect.h prototypes name
 *    functions that live in HFS files (cp_entry_init, cp_getxattr, ...) and would be a SECOND
 *    declaration of a function each file already declares - a header is not where those belong.
 * --------------------------------------------------------------------------------------------- */
int  cp_entry_create_keys(struct cprotect **entry_ptr, struct cnode *dcp, struct hfsmount *hfsmp,
                          uint32_t input_class, uint32_t fileid, mode_t cmode);   /* 2050 :178 */
void cp_entry_destroy(struct cprotect **entry_ptr);                               /* 2050 :181 */

/* ------------------------------------------------------------------------------------------------
 * 5. The ONE name this header cannot simply add, because 4570 already has it: `cp_is_valid_class`.
 *
 * 4570 carries a GLOBAL two-argument `cp_is_valid_class(int isdir, int32_t protectionclass)`
 * (`vfs_cprotect.c:335`, declared in the `sys/cprotect.h` included above).  2050's HFS carries a
 * STATIC one-argument `cp_is_valid_class(int class)` (`hfs_cprotect.c:1180`, called at :224/:242/
 * :482/:1407).  They cannot share a name, and this layer's rule is not to edit 2050's source.  So the
 * port renames its OWN copy, here and only here: the `#define` sits AFTER `sys/cprotect.h` above, so
 * 4570's declaration was read under its own name and is untouched; within every HFS translation unit
 * the identifier `cp_is_valid_class` expands to `hfs_cp_is_valid_class`, and `hfs_cprotect.c`'s
 * static definition and its callers agree.  The two functions never meet.  This is the same shape as
 * the `vnode_name`->`vnode_getname` rename in `stage90_hfs_shims.c`, except the two here are both
 * *defined* (one in HFS, one in 4570), so a link-time shim cannot answer it - a preprocessor rename
 * in the one file that holds the HFS copy is the whole fix.
 * --------------------------------------------------------------------------------------------- */
#define cp_is_valid_class hfs_cp_is_valid_class

#endif /* _HFS_CPROTECT_PORT_H_ */