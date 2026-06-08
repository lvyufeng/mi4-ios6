#ifndef MI4IOS6_STAGE57_SHIM_KERN_DEBUG_H
#define MI4IOS6_STAGE57_SHIM_KERN_DEBUG_H

#define DB_NMI 0x00000004u

void Debugger(const char *reason);
void cnputc(char c);
void vcattach(void);

#endif
