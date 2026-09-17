# `shims_stdbool/` — one header, applied to one file

`stdbool.h`, defining `bool` but **not** `false`/`true`.

`libkern/gen/OSAtomicOperations.c:33-36` is

```c
enum {
	false	= 0,
	true	= 1
};
```

and it fails because `EXTERNAL_HEADERS/stdbool.h:36-37` has already defined those two as macros. The
chain that does it is `mach/vm_param.h:79` (`#ifdef KERNEL`) → `libkern/os/overflow.h:45` →
`stdbool.h`, reached through the build's force-include set — Apple's build force-includes nothing, so
it does not reach `stdbool.h` in this file and compiles it.

Measured, both ways:

| | files compiling |
| --- | --- |
| the shim applied to **every** file | 530 of 615 — 1 fixed, **78 broken** |
| the shim applied to **this file only** | see experiment-151 |

78 of the kernel's files write `true` or `false` and need the macros; one file defines an enum with
those names and needs them absent. That is not a global setting — it is a per-file one, and this is
the same mechanism `COMP_FIRST` and the component roots already use.
