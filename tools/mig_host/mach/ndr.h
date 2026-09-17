#ifndef _MACH_NDR_H_
#define _MACH_NDR_H_
/* Shape and order copied from osfmk/mach/ndr.h:38-49, so MIG's emitted NDR_record initialiser
 * matches the one XNU declares. */
typedef struct {
    unsigned char mig_vers;
    unsigned char if_vers;
    unsigned char reserved1;
    unsigned char mig_encoding;
    unsigned char int_rep;
    unsigned char char_rep;
    unsigned char float_rep;
    unsigned char reserved2;
} NDR_record_t;
extern NDR_record_t NDR_record;
#endif
