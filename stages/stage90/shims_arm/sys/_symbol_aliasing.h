/* Build-generated header the OSS tarball does not ship.
 *
 * The real one comes from bsd/sys/make_symbol_aliasing.sh, which requires
 * ${SDKROOT}/usr/local/libexec/availability.pl — an Apple SDK tool. Its purpose is to alias
 * symbols to availability-annotated variants for deployment-target handling, which does not
 * apply when compiling a source standalone. Empty is correct for that use.
 *
 * Verified by iteration, not assumed: adding it moved the ARM sweep's dominant blocker from
 * 17 files to 16, i.e. it resolved exactly what it should and nothing more. */
#ifndef _SYS__SYMBOL_ALIASING_H_
#define _SYS__SYMBOL_ALIASING_H_
#endif
