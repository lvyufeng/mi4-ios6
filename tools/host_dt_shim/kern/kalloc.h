#ifndef _KERN_KALLOC_H_
#define _KERN_KALLOC_H_
#include <stdint.h>
void *kalloc(uint32_t size);
void kfree(void *addr, uint32_t size);
#endif
