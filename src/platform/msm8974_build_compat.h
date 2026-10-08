#ifndef MSM8974_BUILD_COMPAT_H
#define MSM8974_BUILD_COMPAT_H

/*
 * The two IOKit/build spellings our platform code uses that exist in the modern tree (4570,
 * Darwin 17) and not in Darwin-13 (iOS 7). Force-included ahead of every `src/platform/*.cpp`, and
 * inert on 4570 because each is guarded on the token the modern tree defines.
 *
 * `APPLE_KEXT_OVERRIDE`. `libkern/c++/OSMetaClass.h` defines it in 4570 (`:106` `override`, `:113`
 * empty) and Darwin-13 does not define it at all - it has `APPLE_KEXT_DEPRECATED` and
 * `APPLE_KEXT_COMPATIBILITY_VIRTUAL`, but not this one. The macro's whole content is the C++11
 * `override` specifier; spelling it here as `override` when the compiler takes C++11 and empty
 * otherwise reproduces 4570's two branches.
 *
 * `AbsoluteTime`. Not here as a macro - as a build define (`-DABSOLUTETIME_SCALAR_TYPE=1`, in the
 * platform block's flags). Darwin-13 declares `options ABSOLUTETIME_SCALAR_TYPE` in
 * `bsd/conf/MASTER:85`, so its BSD translation units get it and see `AbsoluteTime` as the scalar
 * `UInt64` (`libkern/libkern/OSTypes.h:97`), with `absolutetime_to_nanoseconds` calling the real
 * `(uint64_t, uint64_t *)` function directly. An iokit translation unit - the platform block is
 * `iokit` - gets only `iokit/conf/MASTER`'s defines (the per-component slice, experiment 918), so
 * without the define it sees `AbsoluteTime` as `UnsignedWide` and `absolutetime_to_nanoseconds` as
 * the wrapping macro `absolutetime_to_nanoseconds(__OSAbsoluteTime(a), b)` (`kern/clock.h:305`),
 * whose `__OSAbsoluteTime` takes a `UnsignedWide` - so a `uint64_t` argument does not convert and
 * the call fails. Forcing the scalar spelling makes the platform block agree with the BSD half and
 * with 4570, where `AbsoluteTime` is `UInt64` unconditionally.
 */

#ifndef APPLE_KEXT_OVERRIDE
#  if defined(__cplusplus) && (__cplusplus >= 201103L)
#    define APPLE_KEXT_OVERRIDE override
#  else
#    define APPLE_KEXT_OVERRIDE
#  endif
#endif

#endif /* MSM8974_BUILD_COMPAT_H */