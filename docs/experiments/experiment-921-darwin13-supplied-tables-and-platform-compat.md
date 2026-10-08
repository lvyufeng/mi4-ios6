# 921 — Darwin-13's supplied tables and the platform block: three files that are 4570's shape (2026-10-08)

After 920 the Darwin-13 whole-kernel compile stood at **372 of ~409 objects** and the only remaining
failures were the three BSD-rooted files in `tools/build_xnu_arm_kernel.sh`'s platform block:
`src/supply/stage90_pthread_functions.c`, `src/supply/stage90_crypto_functions.c`, and
`src/platform/stage90_root_media.c`. All three were written for 4570 and read 4570's headers. This
rung makes each one **follow the selected tree**, and with it the Darwin-13 platform block compiles:
**370 C + 2 C++ = 372 objects, 0 failures.**

## The one premise, three instances (again)

The pattern is 917/918/920's, one level down: **the platform block is a set of translation units
compiled against whichever tree is selected, and three of them hard-coded a fact that is true of
4570 and false of Darwin-13.** The fix in each case is not a switch but a **token the tree itself
supplies**, so a future tree change cannot leave it stale:

| file | 4570 says | Darwin-13 says | token |
|---|---|---|---|
| `stage90_pthread_functions.c` | `<sys/pthread_shims.h>` exists; `pthread_init` panics when the table is NULL | no such header; no `pthread_shims.c` at all | `__has_include(<sys/pthread_shims.h>)` |
| `stage90_crypto_functions.c` | wide `crypto_functions`; chacha/rsa/cts3/gcm-init-iv; NEON AES schedule | narrow struct; no `ccrsa.h`/`ccchacha20poly1305.h`; scalar AES schedule; older `ccmode_*` layouts | `__has_include(<corecrypto/ccrsa.h>)` |
| `stage90_root_media.c` | mockfs reads `DKIOCGETMEMDEVINFO` (`bsd/sys/disk.h:299`) | no mockfs; the ioctl is undeclared | `#ifdef DKIOCGETMEMDEVINFO` |

None of the three is a *value* that could be set by a define; each is a *structural* fact about the
tree (a header's presence, a member's presence, an ioctl's presence). Reading it from the tree is the
whole difference between `mi4-off-option-two-spellings` being avoided and committed again.

## 1. `stage90_pthread_functions.c` — 4570-only, and it says so

`struct pthread_functions_s` and `pthread_kext_register` are defined by `bsd/kern/pthread_shims.c`
(4570) and by nothing in Darwin-13 — which has no such file and no `<sys/pthread_shims.h>`. The
whole translation unit exists to fill that table so `pthread_init` does not panic. On Darwin-13 it
has no table to fill, so the guard is the header it already includes:

```c
#if defined(__has_include)
#  if !__has_include(<sys/pthread_shims.h>)
#    define STAGE90_PTHREAD_FUNCTIONS_OMITTED 1
#  endif
#endif
#ifndef STAGE90_PTHREAD_FUNCTIONS_OMITTED
... the whole file ...
#endif
```

The object is still produced (an empty translation unit), so the build's object count does not
depend on the tree. What 4570 compiles is **byte-identical** (§Verification).

## 2. `stage90_crypto_functions.c` — one table, two generations

This one has to **do its job on both trees**, not be omitted: Darwin-13 dereferences `g_crypto_funcs`
from linked code just as 4570 does — `osfmk/vm/vm_pageout.c`'s `swap_crypt_ctx_initialize` calls
`aes_encrypt_key` (in the 409-file manifest, and `__HIB`-gated rather than build-gated), and the only
writer of `g_crypto_funcs` is `register_crypto_functions()`, whose **only caller anywhere is the
`com.apple.kec.corecrypto` kext** in both trees. So the file exists in both trees for the same reason
and must define the table in each tree's own shape.

Darwin-13's corecrypto is an **older generation**. The differences, read from the two trees' headers:

- **`aes.h`** — Darwin-13's `AES_CBC_CTX_MAX_SIZE` has no bit-sliced (NEON) term, so it is the scalar
  **276** even though `__ARM_NEON__` is defined on this cortex-a8 build; 4570 adds `(14-1)*128 + 32`
  for a bit-sliced **1972**. The `_Static_assert` set is selected by the tree token, not by
  `__ARM_NEON__` (which is true on D13 and would have picked the wrong branch).
- **`ccmode_impl.h`** — `struct ccmode_ctr` has no `ecb_block_size`/`setctr`, `ccmode_xts` no
  `key_sched`, `ccmode_gcm` no `encdec`. The stub **functions** are signature-compatible across the
  generations (Darwin-13's fn-pointer typedefs are `void`/`unsigned long`, 4570's `int`/`size_t`, and
  the stub bodies are `static … (int, …)` — the one shape that satisfies both), so only the **macro
  designators** need the tree split.
- **`register_crypto.h`** — Darwin-13's `struct crypto_functions` has **no** `ccrng_fn`,
  `ccrsa_make_pub_fn`, `ccrsa_verify_pkcs1v15_fn`, `ccchacha20poly1305_fns`, `ccpad_cts3_*`,
  `ccaes_ctr_crypt`, or the GCM rows. Those stub bodies and table entries are gated out.
- **no `<corecrypto/ccrsa.h>`, no `<corecrypto/ccchacha20poly1305.h>`** — the RSA/chacha stub bodies,
  descriptor, and table rows are gated out.

The token is `__has_include(<corecrypto/ccrsa.h>)`: the newest header 4570 has and Darwin-13 does
not. Every gate is one `#if STAGE90_CRYPTO_TREE_HAS_RSA … #endif`; on 4570 the expansion is the
file as it was.

## 3. `stage90_root_media.c` — mockfs's ioctl is 4570's

`st_media_memdev_info` and the `DKIOCGETMEMDEVINFO` case answer `mockfs_mountroot`'s question about
memory-backing. Darwin-13 has **no `bsd/miscfs/mockfs`** and `bsd/sys/disk.h` does not declare the
ioctl or `dk_memdev_info_t`, so nothing can ask and the body cannot compile. The guard is the macro
itself:

```c
#ifdef DKIOCGETMEMDEVINFO
... st_media_memdev_info ...
#endif
...
#ifdef DKIOCGETMEMDEVINFO
    case DKIOCGETMEMDEVINFO:
        return st_media_memdev_info(dev, (dk_memdev_info_t *)data);
#endif
```

`DKIOCGETMEMDEVINFO` is a `#define` (not an enum), so `#ifdef` sees it; on 4570 nothing here changes.

## Verification

- **Darwin-13** whole-kernel: **370 C (all) + 2 C++ (all) = 372 objects, `fail: 0`, `EXIT=0`** — the
  platform block, including the four MSM8974 C++ objects, now compiles end to end. Manifest 409.
- **4570 neutrality — measured, not asserted.** The three files are shared with 4570's platform
  block, so their objects were built **twice**: once from the working-tree (guarded) sources and once
  from `git checkout HEAD --` (unguarded) sources, each with the same `--platform-only` build.
  `cmp` on the three objects:
  - `stage90_pthread_functions.o` — **BYTE-IDENTICAL**
  - `stage90_crypto_functions.o` — **BYTE-IDENTICAL**
  - `stage90_root_media.o` — **BYTE-IDENTICAL**
  So every guard is inert on 4570 down to the object, which is stronger than "the source is
  unchanged" — it is "the tree-derived tokens expand the same way 4570's headers did".
- `make check` → **0**.
- `check_device_conditions.py` (D13) → the same informational notes as 920 (`pty_init`'s three-count
  disagreement; `gif.h`/`vndevice.h` in two header dirs; `stf.h`/`vc.h` unread) and **no new
  failures**.

## What this does not do

The Darwin-13 platform block now compiles, but the D13 image is not yet **linked** — the manifest is
409 files and the platform objects land in `out/xnu_platform_obj` "not in the manifest", exactly as on
4570. Linking D13 is a later rung, and `bsdthread_*_args` (28 sites, generated `sysproto` detail) is
still owed. The HD2-fork playbook (machine config `MSM8974_*`, `PlatformConfigs.h`, the SOC timer
dispatch, `KernelConfigTables.cpp` `IONameMatch`, `NO_KEXTD`, `vm_param.h` TTBR0 split, a
loader→kernel handoff header) is the next body of platform work this build is now able to carry.