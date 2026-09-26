/*
 * One declaration, force-included for `libkern/OSKextLib.cpp` and nowhere else.
 *
 * The problem it answers is not a missing declaration; it is a declaration that disagrees with
 * itself. `kext_request` is a `friend` of `OSKext` — `libkern/libkern/c++/OSKext.h:189`, inside
 * `#ifdef XNU_KERNEL_PRIVATE` — and `OSKextLib.cpp:190` defines it inside the `extern "C" {` block
 * that opens at `:39`. Those are the same function in Apple's source, but with no earlier
 * declaration clang reads the friend as C++ linkage and the definition as C linkage, and reports
 *
 *     OSKextLib.cpp:190:15: error: declaration of 'kext_request' has a different language linkage
 *     OSKextLib.cpp:280:30: error: 'loadFromMkext' is a private member of 'OSKext'
 *     OSKextLib.cpp:291:30: error: 'handleRequest' is a private member of 'OSKext'
 *
 * Three errors, one cause, and the second two are the interesting ones: a `friend` declaration
 * grants access to *that function*, so a `kext_request` that clang does not believe is the friend
 * has no access to `OSKext`'s private statics either.
 *
 * Declaring it `extern "C"` before anything else is read gives the friend declaration and the
 * definition the same linkage — a function's linkage is fixed by its first declaration
 * ([dcl.link]) — and all three errors go. Measured: three errors before, an object after, and no
 * other file changes. The declaration is spelled out from Apple's own two, so if either moves it is
 * a "conflicting types" error and not a silent disagreement; that is the point of the full
 * parameter list below rather than a `typedef`.
 */
#ifndef MI4IOS6_KEXT_REQUEST_C_H
#define MI4IOS6_KEXT_REQUEST_C_H

#include <mach/mach_types.h>
#include <mach/vm_types.h>

extern "C" {

kern_return_t kext_request(
    host_priv_t                             hostPriv,
    uint32_t                                clientLogSpec,
    vm_offset_t                             requestIn,
    mach_msg_type_number_t                  requestLengthIn,
    vm_offset_t                           * responseOut,
    mach_msg_type_number_t                * responseLengthOut,
    vm_offset_t                           * logDataOut,
    mach_msg_type_number_t                * logDataLengthOut,
    kern_return_t                         * op_result);

}

#endif /* MI4IOS6_KEXT_REQUEST_C_H */
