# Experiment 287 — `osfmk_kern_coalition.o`, `zinit`'s only stub, and the 16 KB boundary

**Step:** link the object that defines `coalitions_init` — `osfmk/kern/coalition.c`,
`osfmk_kern_coalition.o` — the name the 286 run stopped at.
**Prediction:** that the frontier moves **seven calls** into the function, to `init_task_ledgers` —
which required reading `zinit` (0x97c bytes, three times the size its name suggests) far enough to
show that the only stub inside it is never reached. Also predicted, from 284's arithmetic: that this
step is the one that finally moves the 16 KB boundary.
**Result:** both, exactly. **`stub_hit=init_task_ledgers` at `xnu_entry_stub_caller=0x800bde8c`**
(= `coalitions_init+0xe8`), all three stub counts exact, and `__bss_start` predicted to the byte.

## `zinit` is the step's real content

`coalitions_init` is twelve calls long and eleven of them are real. Reading its body in the object
gives the stop immediately — `init_task_ledgers` is the only name in it that this image does not
have — but that reading is only worth something if the calls *before* it return, and one of them is
not obviously safe:

```
zinit(&coalition_zone, 0x2c, 0, "coalition")     real, and 0x97c bytes long
zone_change                                      real, 0 calls of its own
PE_parse_boot_argn x2                            real
lck_grp_attr_setdefault / lck_grp_init / lck_attr_setdefault / lck_mtx_init    all real
init_task_ledgers                                ***STUB***  <- the stop
coalition_create_internal x4                     real (defined in this object)
panic                                            real
```

Every `bl` in `zinit`'s whole extent, with its status in this image:

```
real  lck_spin_lock x2   strcmp   lck_attr_setdefault   lck_mtx_init_ext
      lck_spin_unlock x3  panic x2  strlen x2  _consume_printf_args x3
      kmem_alloc_kobject  __bzero   strlcpy   snprintf   PE_parse_boot_argn x3
STUB  btlog_create x1   (at 0x8006e550)
```

`btlog_create` is a stub in this image, and `zinit` is not a leaf — so the step turns on whether
that one call is executed. It is guarded by `zalloc.c:2259-2360`: entered on every `zinit`, but the
btlog loop needs `log_records_init == FALSE && zone_logging_enabled == TRUE`, and
`zone_logging_enabled` is set by `track_this_zone(z->zone_name, zone_name_to_log)` where
`zone_name_to_log` comes from `PE_parse_boot_argn("zlog1")` … `PE_parse_boot_argn("zlog10")` and then
`PE_parse_boot_argn("zlog")`. This payload's boot-args line contains no `zlog` of any spelling, so
the loop is skipped and `zinit` returns.

**This is the third step in a row whose answer came from the boot args** — 286's from an *absent*
DT property, 285's from a storage stand-in's zero value, and this one from an absent boot-arg — and
it is worth naming as a pattern rather than three coincidences. The stub walk's stopping point is
decided by three different kinds of thing, and only the first is visible in the undefined-symbol
list:

1. a symbol nothing defines (the ordinary case);
2. a **value** this build invents — a zeroed stand-in — that control flow branches on (285, and
   `task_ledger_template` in this step);
3. a **string** this build supplies or omits — boot args, device-tree properties — that a real
   function parses (286, 287).

## The 16 KB boundary moves

Experiment 284 found that the entry image's `.bin` ends at the end of `__DATA,__data`, that `.data`
is 16 KB-aligned after the read-only region, and that within the slack a step's text growth is
invisible in the image size. The slack was 0x1be4 bytes after 285; this object carries 11312 bytes of
text, so this step crosses. `.data` moved `0x800fc000 → 0x80100000`, and `__bss_start` with it:

```
                      predicted        measured
undefined             888              888
function stubs        791              791
storage               97               97
text                  1042400          1042736
image                 1148544          1148528
__bss_start           0x80117b68       0x80117b68   <- exact to the byte
```

`__bss_start` is the number that proves the mechanism: it is the end of `.data`, so predicting it to
the byte is predicting the whole 16 KB move and the 8 bytes this object adds, together.

## The object, measured

11312 bytes of text, 8 of data, 332 of bss, 72 of rodata, 881 of `rodata.str1.1`, 47 definitions, 44
references. Resolves **13** (all functions: the twelve `coalition_*` predicates and accessors plus
`coalitions_init` itself) and adds **5** — four functions and one storage:

```
coalition_notification   mach_coalition_notification_user.o   T 0x64
init_task_ledgers        osfmk_kern_task.o                    T 0x5bc
task_cpu_ptime           osfmk_kern_task.o                    T 0xc
task_energy              osfmk_kern_task.o                    T 0x8c
task_ledger_template     osfmk_kern_task.o                    B 4
```

Four of the five come from `osfmk_kern_task.o`, and the storage one is a *pointer* — zero here for
the same reason `clock_count` was in 285, and the next step's compile will meet the same object
again.

## The run

```
stub_hit=init_task_ledgers        xnu_entry_stub_caller=0x800bde8c
```

`0x800bde8c` is `coalitions_init+0xe8`, the instruction after the `bl` at `+0xe4`. So one run measured
that `zinit` executed for the first time in this boot and returned with `btlog_create` never called;
that `zone_change`, both `PE_parse_boot_argn` calls, `lck_grp_attr_setdefault`, `lck_grp_init`,
`lck_attr_setdefault` and `lck_mtx_init` all returned after it; and that the frontier is now the
task-ledger template.

bss end 0x8014a008 → 0x8014e188, headroom 1794040 → 1777272. Preflight clean
(`loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`), log 301118 bytes, no
`exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` x25, `failure_mask=0x00000000` x87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own.

**Next:** experiment 288 — `osfmk/kern/task.o` (`osfmk_kern_task.o`) for `init_task_ledgers`. This is
the object the walk has been circling: 285's census named `task_init` a stub, 287 brought four of its
symbols in, and the pending list has carried `task.o` / `task_max` for several steps. It is large and
it defines `task_init`, so it is the first step in this sequence that may retire *several* adjacent
stubs at once — and the prediction will have to be made the same way, from the body of
`init_task_ledgers` rather than from its name.
