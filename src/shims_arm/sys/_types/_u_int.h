/*
 * Copyright (c) 2017 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
#ifndef _U_INT
#define _U_INT
typedef	unsigned int 	u_int;
#endif /* _U_INT */

/*
 * WHY THIS COPY EXISTS (913).  `build_xnu_arm_layer.sh` force-includes
 * `sys/_types/_u_int.h` because osfmk/arm reaches `u_int` routinely.  Two XNU
 * layouts answer that name: the modern tree (xnu-4570, bsd/sys/_types/_u_int.h)
 * and this shim.  The harness lists `-I"$XNU/bsd"` BEFORE `-I"$SHIMS_ARM"`, so a
 * tree that ships the fragment uses its own and this file is never reached; the
 * older Darwin-13 tree (xnu-hd2-darwin13) has no `bsd/sys/_types/_u_int.h` and
 * only defines `u_int` in `bsd/sys/types.h`, which the entry path cannot include
 * (it collides with kern_types.h over clock_t).  So this is not a stand-in for a
 * missing header - it is the same declaration, provided where the old tree puts it.
 */