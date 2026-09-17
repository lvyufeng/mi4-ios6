/* Host build of MIG.
 *
 * MIG is BSD-era code using u_int, u_char and friends, which XNU's bsd/sys/types.h defines and
 * glibc's does not. Darwin's types.h would drag the whole bsd/sys tree (including the absent
 * _pthread directory) into a host build that needs none of it, so only those names are added.
 * Anything glibc already has is left alone - redefining daddr_t, dev_t, key_t or nlink_t collides
 * and, because the collision stops this file, takes u_int down with it. */
#include_next <sys/types.h>
typedef unsigned char  u_char;
typedef unsigned short u_short;
typedef unsigned int   u_int;
typedef unsigned long  u_long;
typedef unsigned long  u_quad_t;
typedef long           quad_t;
