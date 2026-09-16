/* Minimal stand-in for XNU's sys/appleapiopts.h.
 *
 * pexpert/device_tree.h puts its entire interface behind __APPLE_API_PRIVATE, which in
 * the real kernel this header defines. Without it the walker's declarations are invisible
 * and device_tree.c does not compile - which is why this file exists at all. */
#ifndef _SYS_APPLEAPIOPTS_H_
#define _SYS_APPLEAPIOPTS_H_
#define __APPLE_API_PRIVATE 1
#endif
