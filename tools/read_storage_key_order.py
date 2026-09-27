#!/usr/bin/env python3
"""When was that cell read, and by which probe stage?

`xnu_live_storage_*` keys are published all over `entry_storage.c`, and the probe runs its
stages in a fixed order. A cell name says *which register*, never *which moment* - and two
landed defects are exactly that confusion:

  * **m732** (723 section 3) - `_reg_power_control` / `_reg_power_control_after` are rung 3
    and rung 4, both **before** rung 7 writes `POWER_CONTROL 0x29`, and a sentence read them
    as "POWER_CONTROL reads 0x00".
  * **m763** (753) - `_reg_pwrctl_status_after` is emitted at the end of `st_driver_reset`,
    which the probe calls **first**, and a sentence read it as a reading taken after the
    power byte.

Both were found by hand, one press apart. This tool makes the moment a **lookup**: for any
key it prints the function that emits it, the chain of calls from `entry_storage_probe` that
reaches that function, and the preprocessor guard on each hop - and `--after` refuses a
"read after X" claim the call order does not support.

What it will not do, on purpose:

  * **It does not order a cell an interrupt handler publishes.** `st_pwr_irq` is not called
    by anything; it is *registered* by `st_pwr_irq_arm` and entered when the line fires. Its
    cells are an **interval** starting at the registration, and the tool says INTERVAL rather
    than picking a point. That is the 753 subtlety: `_pwr_irq_at` is stamped at handler
    entry, so the archive knows more than the source does.
  * **It does not guess a guard it cannot evaluate.** A condition outside the simple
    rung-comparison vocabulary is reported UNKNOWN, and an UNKNOWN guard on the path makes the
    ordering UNKNOWN too (`mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one`).

Usage:

    read_storage_key_order.py                       the probe's stage table
    read_storage_key_order.py --key _pwr_before     one key's moment
    read_storage_key_order.py --rung 20             ...as it stands on a given rung
    read_storage_key_order.py --after _pwr_status_before _pwr_status_after
    read_storage_key_order.py --selftest            the two landed defects, re-derived

Exit: 0 when every claim asked for is supported, 1 when one is not, 2 on a usage error.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SRC = REPO_ROOT / "src" / "entry" / "entry_storage.c"

PROBE = "entry_storage_probe"
RUNG_MACRO = "STAGE90_XNU_STORAGE_PROBE"

# A call in this file is a bare `name();` at the start of a statement. Restricting the
# call graph to names this file defines keeps libc and the entry-image API (which are not
# storage stages) out of it, which is what makes a `--after` answer about the ladder.
CALL_RE = re.compile(r"(?<![\w.])([A-Za-z_][A-Za-z0-9_]*)\s*\(")
REGISTER_RE = re.compile(r"\(uintptr_t\)\s*([A-Za-z_][A-Za-z0-9_]*)|&\s*([A-Za-z_][A-Za-z0-9_]*)\b")
LIVE_HEAD_RE = re.compile(r"ST_LIVE\s*\(")
DEFINE_RE = re.compile(r"^#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*(.*)$", re.S)


def first_arg_extent(clean, start):
    """Where does the first argument of a call starting at `start` end?

    `clean` has string bodies blanked, so a `)` or `,` inside a key literal cannot be
    mistaken for structure. The caller reads the argument *text* from the raw source at the
    same offsets, which is why the two are indexed identically.
    """
    depth = 1
    i = start
    while i < len(clean):
        c = clean[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i
        elif c == "," and depth == 1:
            return i
        i += 1
    return i


def paren_extent(clean, start):
    """The index of the `)` closing the call whose `(` precedes `start`."""
    depth = 1
    i = start
    while i < len(clean):
        c = clean[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return i


def split_args(text):
    """Top-level comma split, so a comma inside a nested call is not a separator."""
    out, depth, cur = [], 0, []
    for c in text:
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if c == "," and depth == 0:
            out.append("".join(cur))
            cur = []
            continue
        cur.append(c)
    out.append("".join(cur))
    return [a.strip() for a in out if a.strip()]


def string_concat(text):
    """Parse a C string-concatenation expression into its parts, or None.

    A part is ("lit", contents) for a string literal and ("param", name) for anything else -
    which in this file is always a macro parameter. **This exists because the naive
    `ST_LIVE("key")` regex truncates a generated key**: `ST_LIVE("xnu_live_storage_cmd" tag
    "_ps_before")` yields `xnu_live_storage_cmd`, which is not a key and never was, and a
    tool that reported a moment for it would be inventing a cell out of half a name.
    """
    parts = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
            continue
        if c == '"':
            j = i + 1
            buf = []
            while j < n:
                if text[j] == "\\":
                    buf.append(text[j:j + 2])
                    j += 2
                    continue
                if text[j] == '"':
                    break
                buf.append(text[j])
                j += 1
            if j >= n:
                return None
            parts.append(("lit", "".join(buf)))
            i = j + 1
            continue
        m = re.match(r"[A-Za-z_][A-Za-z0-9_]*", text[i:])
        if not m:
            return None
        parts.append(("param", m.group(0)))
        i += m.end()
    return parts or None


def macro_templates(raw, clean):
    """Every `#define` whose body publishes a key by concatenation.

    Returns {name: {"params": [...], "keys": [parts, ...], "line": n}}. Continuation lines
    are joined first, because a macro body is one logical line and half of these keys are
    declared on a continued line.
    """
    logical = []
    buf, first = "", None
    for idx, line in enumerate(raw.split("\n"), start=1):
        if first is None:
            first = idx
        if line.rstrip().endswith("\\"):
            buf += line.rstrip()[:-1] + " "
            continue
        logical.append((first, buf + line))
        buf, first = "", None
    templates = {}
    for line_no, text in logical:
        m = DEFINE_RE.match(text.strip())
        if not m:
            continue
        name, params, body = m.group(1), m.group(2), m.group(3)
        # The body was joined from raw text, so re-derive the clean form for structure.
        clean_body = strip_comments_and_strings(body)
        keys = []
        for hit in LIVE_HEAD_RE.finditer(clean_body):
            end = first_arg_extent(clean_body, hit.end())
            parts = string_concat(body[hit.end():end])
            if parts and any(k == "param" for k, _ in parts):
                keys.append(parts)
        if keys:
            templates[name] = {
                "params": [p.strip() for p in params.split(",") if p.strip()],
                "keys": keys,
                "line": line_no,
            }
    return templates


def expand_templates(raw, clean, templates):
    """Concrete keys from every invocation of a key-generating macro.

    The emission line and owner are the *invocation's*, because that is where the code that
    runs the concatenation sits - the `#define` itself is expanded away before the compiler
    ever sees a storage probe.
    """
    out = []  # (key, line, template_name, unresolvable_reason)
    for name, tpl in templates.items():
        for m in re.finditer(r"(?<![\w.])" + re.escape(name) + r"\s*\(", clean):
            # A macro is invoked one way and *defined* another; the `#define` line matches the
            # same pattern and is not an invocation, so it is skipped by line.
            if raw[:m.start()].count("\n") >= 0 and \
                    raw.split("\n")[raw[:m.start()].count("\n")].lstrip().startswith("#"):
                continue
            end = paren_extent(clean, m.end())
            # Extent from `clean` (structure), text from `raw` (contents) - `clean` has every
            # string body blanked, so reading the argument values out of it yields a key with
            # the tag replaced by whitespace. It is the same off-by-one-source shape as the
            # truncation above and it produced a whole family of plausible wrong keys.
            args = split_args(raw[m.end():end])
            if len(args) != len(tpl["params"]):
                out.append((None, None, name, f"invocation with {len(args)} argument(s), "
                                              f"macro takes {len(tpl['params'])}"))
                continue
            binding = dict(zip(tpl["params"], args))
            line_no = raw[:m.start()].count("\n") + 1
            for parts in tpl["keys"]:
                key, bad = "", None
                for kind, val in parts:
                    if kind == "lit":
                        key += val
                        continue
                    arg = binding.get(val, "")
                    am = re.fullmatch(r'"((?:[^"\\]|\\.)*)"', arg)
                    if am:
                        key += am.group(1)
                    else:
                        bad = f"`{val}` is bound to `{arg}`, which is not a string literal"
                        break
                out.append((key if bad is None else None, line_no, name, bad))
    return out


def strip_comments_and_strings(text):
    """Blank out comment and string bodies, preserving every offset.

    Offsets must survive because line numbers are the whole point of the tool. String
    *literals* are kept as delimiters (so `ST_LIVE("...")` is still findable in the raw
    text) but their contents are blanked here, so a brace or a quote inside a key cannot
    move the brace counter.
    """
    out = list(text)
    i, n = 0, len(text)
    mode = None  # None | 'line' | 'block' | 'str' | 'chr'
    while i < n:
        c = text[i]
        nc = text[i + 1] if i + 1 < n else ""
        if mode is None:
            if c == "/" and nc == "/":
                mode = "line"
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == "/" and nc == "*":
                mode = "block"
                out[i] = out[i + 1] = " "
                i += 2
                continue
            if c == '"':
                mode = "str"
                i += 1
                continue
            if c == "'":
                mode = "chr"
                i += 1
                continue
        elif mode == "line":
            if c == "\n":
                mode = None
            else:
                out[i] = " "
        elif mode == "block":
            if c == "*" and nc == "/":
                out[i] = out[i + 1] = " "
                mode = None
                i += 2
                continue
            if c != "\n":
                out[i] = " "
        elif mode == "str":
            if c == "\\":
                out[i] = " "
                if i + 1 < n:
                    out[i + 1] = " "
                i += 2
                continue
            if c == '"':
                mode = None
            else:
                out[i] = " "
        elif mode == "chr":
            if c == "\\":
                out[i] = " "
                if i + 1 < n:
                    out[i + 1] = " "
                i += 2
                continue
            if c == "'":
                mode = None
            else:
                out[i] = " "
        i += 1
    return "".join(out)


def eval_guard(cond, rung):
    """True / False / None, for a preprocessor condition over the rung macro.

    The file's vocabulary is exactly `>=`, `>`, `==`, `<` and `||`/`&&` over one macro, so a
    restricted eval covers it. Anything else - a `defined()`, a second macro, a token that
    is not an operator - is UNKNOWN, and the caller must not treat UNKNOWN as false.
    """
    if cond is None:
        return True
    expr = cond.strip()
    if not expr:
        return None
    if "defined" in expr or RUNG_MACRO not in expr:
        return None
    if not re.fullmatch(r"[0-9A-Za-z_\s<>=!()|&]+", expr):
        return None
    py = expr.replace("||", " or ").replace("&&", " and ")
    py = re.sub(r"(?<![A-Za-z0-9_])" + re.escape(RUNG_MACRO) + r"(?![A-Za-z0-9_])", str(rung), py)
    try:
        return bool(eval(py, {"__builtins__": {}}, {}))  # noqa: S307 - whitelisted above
    except Exception:
        return None


def ladder_domain(source):
    """The rung domain, parsed from the source's own `#error` guard.

    m735's repair, applied here: a tool must not restate the bound, because a bound restated
    is a second definition of the ladder and nothing can contradict it. This reads the guard
    the compiler reads, and a guard it cannot parse is a refusal rather than a pass.
    """
    lines = source.split("\n")
    for idx, line in enumerate(lines):
        if not line.lstrip().startswith("#error"):
            continue
        for back in range(idx - 1, max(-1, idx - 4), -1):
            cond = lines[back].strip()
            if cond.startswith("#if"):
                cond = cond[3:]
                m_lo = re.search(re.escape(RUNG_MACRO) + r"\s*<\s*(\d+)", cond)
                m_hi = re.search(re.escape(RUNG_MACRO) + r"\s*>\s*(\d+)", cond)
                if m_lo and m_hi:
                    return int(m_lo.group(1)), int(m_hi.group(1)), cond.strip()
                return None
    return None


def live_rung(path):
    """The rung the image in `out/` was actually built at, or None."""
    try:
        for line in Path(path).read_text().split("\n"):
            m = re.match(r"\s*" + re.escape(RUNG_MACRO) + r"\s*=\s*(\d+)\s*$", line)
            if m:
                return int(m.group(1))
    except OSError:
        return None
    return None


class Func:
    __slots__ = ("name", "start", "end", "sig_line")

    def __init__(self, name, start, end, sig_line):
        self.name = name
        self.start = start      # 1-based first line of the body
        self.end = end          # 1-based last line of the body
        self.sig_line = sig_line

    def __repr__(self):
        return f"<{self.name} {self.sig_line}-{self.end}>"


def parse(source):
    clean = strip_comments_and_strings(source)
    raw_lines = source.split("\n")
    clean_lines = clean.split("\n")

    # --- preprocessor guard stack, per line ---------------------------------------------
    # Each entry is (condition_or_None, negation_flag). A line inside the `#else` branch of
    # `#if X` carries `not X`. `#elif` is treated as a new condition on the same frame and is
    # reported UNKNOWN unless it is a rung comparison, which keeps the tool honest about the
    # one construct it does not model exactly.
    guards = {}
    stack = []
    for idx, line in enumerate(clean_lines, start=1):
        s = line.lstrip()
        if s.startswith("#"):
            body = s[1:].strip()
            word = body.split(None, 1)[0] if body else ""
            rest = body[len(word):].strip()
            if word in ("if", "ifdef", "ifndef"):
                if word == "ifdef":
                    stack.append(("defined(" + rest + ")", False))
                elif word == "ifndef":
                    stack.append(("!defined(" + rest + ")", False))
                else:
                    stack.append((rest, False))
            elif word in ("else", "elif"):
                if stack:
                    cond, neg = stack[-1]
                    stack[-1] = (("!(" + cond + ")") if word == "else" else rest, True)
            elif word == "endif":
                if stack:
                    stack.pop()
            guards[idx] = [c for c, _ in stack]
        else:
            guards[idx] = [c for c, _ in stack]

    # --- functions ----------------------------------------------------------------------
    # A body opens at the first `{` seen at depth 0. The signature is whatever depth-0 text
    # accumulated since the last statement ended - which in this file is the one or two
    # lines of return type and attributes above the brace.
    funcs = []
    depth = 0
    pending = []
    for idx, line in enumerate(clean_lines, start=1):
        s = line.strip()
        if s.startswith("#"):
            continue
        if depth == 0:
            if s == "":
                pending = []
                continue
            if "{" in line:
                head = line.partition("{")[0]
                sig = " ".join(pending + [head]).strip()
                pending = []
                # Several definitions in this file carry their return type and attributes on
                # one or two lines above the name (`static __attribute__((noinline, noclone))
                # void\nst_send_command(...)`). Without stripping the attributes the anchored
                # match happily names the function `__attribute__`, which is the sort of
                # quietly-wrong identifier the whole tool exists to avoid.
                sig = re.sub(r"__attribute__\s*\(\([^()]*\)\)", " ", sig)
                m = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*$", sig)
                if m:
                    funcs.append(Func(m.group(1), idx, None, idx))
                depth = 1
                continue
            pending.append(line)
            continue
        # depth >= 1
        depth += line.count("{") - line.count("}")
        if depth == 0:
            if funcs and funcs[-1].end is None:
                funcs[-1].end = idx
            pending = []
    for f in funcs:
        if f.end is None:
            f.end = len(clean_lines)
    by_name = {}
    for f in funcs:
        by_name.setdefault(f.name, f)

    def owner(line_no):
        best = None
        for f in funcs:
            if f.start <= line_no <= f.end:
                if best is None or (f.end - f.start) < (best.end - best.start):
                    best = f
        return best

    # --- keys -----------------------------------------------------------------------------
    # Two sources, and the record has to keep them apart. A **concrete** key is one string
    # literal; a **generated** key comes from a macro that concatenates a tag, and its line
    # is the invocation's. A key that is neither - a concatenation with no resolvable
    # invocation - is carried as a template and never given a moment.
    keys = {}
    templates = macro_templates(source, clean)
    for idx, line in enumerate(clean_lines, start=1):
        for hit in LIVE_HEAD_RE.finditer(line):
            end = first_arg_extent(line, hit.end())
            parts = string_concat(raw_lines[idx - 1][hit.end():end])
            if not parts:
                continue
            if len(parts) == 1 and parts[0][0] == "lit":
                key = parts[0][1]
                keys.setdefault(key, {"line": idx, "func": owner(idx),
                                      "guard": guards.get(idx, []), "generated_by": None})
            # a concatenation here that mentions a parameter is the `#define` itself, and
            # `macro_templates` has already recorded it; it is not a key.
    unresolved = []
    for key, line_no, tpl_name, bad in expand_templates(source, clean, templates):
        if bad is not None:
            unresolved.append({"template": tpl_name, "why": bad, "line": line_no})
            continue
        keys.setdefault(key, {"line": line_no, "func": owner(line_no),
                              "guard": guards.get(line_no, []), "generated_by": tpl_name})

    # --- call edges, per function --------------------------------------------------------
    calls = {f.name: [] for f in funcs}
    for f in funcs:
        for idx in range(f.start, f.end + 1):
            line = clean_lines[idx - 1]
            for m in CALL_RE.finditer(line):
                name = m.group(1)
                if name in by_name and name != f.name:
                    calls[f.name].append((name, idx, "call"))
            for m in REGISTER_RE.finditer(line):
                name = m.group(1) or m.group(2)
                if name in by_name and name != f.name:
                    calls[f.name].append((name, idx, "registered"))
        # dedupe, keep source order; a registration of a name also called is a call
        seen = set()
        uniq = []
        for name, idx, kind in calls[f.name]:
            k = (name, idx)
            if k in seen:
                continue
            seen.add(k)
            uniq.append((name, idx, kind))
        calls[f.name] = uniq

    return {
        "templates": templates,
        "unresolved": unresolved,
        "raw_lines": raw_lines,
        "clean_lines": clean_lines,
        "funcs": funcs,
        "by_name": by_name,
        "owner": owner,
        "keys": keys,
        "calls": calls,
        "guards": guards,
    }


def reach(model, rung):
    """Every function reachable from the probe, with a shortest path and its guards.

    Path element: (func_name, line_where_it_was_reached, kind, guard_list). The first
    element is the probe itself.
    """
    by_name = model["by_name"]
    if PROBE not in by_name:
        return {}
    empty = []
    paths = {PROBE: [(PROBE, by_name[PROBE].start, "root", empty)]}
    queue = [PROBE]
    while queue:
        cur = queue.pop(0)
        for name, line, kind in model["calls"].get(cur, []):
            if name in paths:
                continue
            paths[name] = paths[cur] + [(name, line, kind, model["guards"].get(line, []))]
            queue.append(name)
    return paths


def stage_table(model, rung):
    """The probe's own ordered stage calls, with their guards."""
    probe = model["by_name"].get(PROBE)
    if probe is None:
        return []
    rows = []
    for name, line, _kind in model["calls"].get(PROBE, []):
        rows.append((line, name, model["guards"].get(line, [])))
    return sorted(rows)


def resolve_key(model, name):
    """A bare name, or a suffix, to exactly one published key. (key, None) or (None, why)."""
    if name in model["keys"]:
        return name, None
    hits = sorted(k for k in model["keys"] if k.endswith(name) or k == "xnu_live_storage" + name)
    if len(hits) == 1:
        return hits[0], None
    if not hits:
        return None, f"{name} matches no key published by this source"
    return None, f"{name} matches {len(hits)} keys: {', '.join(hits[:4])}"


def moment(model, key, rung):
    """(verdict, detail) for one key, where verdict is 'point' / 'interval' / 'dead' / 'unknown'."""
    name, why = resolve_key(model, key)
    if name is None:
        return "unknown", why
    key = name
    info = model["keys"].get(key)
    if info is None:  # unreachable while resolve_key agrees with the key table
        return "unknown", f"no ST_LIVE in this source publishes {key}"
    f = info["func"]
    if f is None:
        return "unknown", f"line {info['line']} is outside every function this parser found"
    paths = reach(model, rung)
    path = paths.get(f.name)
    if path is None:
        return ("dead",
                f"{f.name} is not reachable from {PROBE} by any call this parser found "
                f"(it is emitted at line {info['line']})")
    hops = [p for p in path if p[2] == "registered"]
    detail = {
        "key": key,
        "line": info["line"],
        "func": f.name,
        "path": path,
        "emission_guard": info["guard"],
        "registered": [h[0] for h in hops],
        "points": [p[1] for p in path] + [info["line"]],
    }
    return ("interval" if hops else "point"), detail


def evaluate(model, key, rung):
    """Filter a moment's guards against a rung, returning (verdict, detail, why)."""
    verdict, detail = moment(model, key, rung)
    if verdict in ("unknown", "dead"):
        return verdict, detail, detail if isinstance(detail, str) else ""
    for cond in detail["emission_guard"]:
        v = eval_guard(cond, rung)
        if v is False:
            return "dead", detail, f"its own guard `{cond}` is false at rung {rung}"
        if v is None:
            return "unknown", detail, f"its own guard `{cond}` is outside the rung vocabulary"
    for _name, _line, _kind, gl in detail["path"]:
        for cond in gl:
            v = eval_guard(cond, rung)
            if v is None:
                return "unknown", detail, f"a hop carries `{cond}`, outside the rung vocabulary"
    return verdict, detail, ""


def compare(model, a_key, b_key, rung):
    """Where does a_key sit relative to b_key?  Returns (relation, explanation)."""
    va, da, wa = evaluate(model, a_key, rung)
    vb, db, wb = evaluate(model, b_key, rung)
    if va != "point" or vb != "point":
        parts = []
        for label, v, w in ((a_key, va, wa), (b_key, vb, wb)):
            if v != "point":
                parts.append(f"{label}: {v.upper()}" + (f" - {w}" if w else ""))
        if va == "interval" or vb == "interval":
            iv = da if va == "interval" else db
            parts.append(
                f"a cell published from an interrupt client ({', '.join(iv['registered'])}) is an "
                f"INTERVAL starting at its registration ({[p[1] for p in iv['path']][-1]}); the "
                f"source cannot say when the line fires, so no point order is established")
        return "UNORDERED", "; ".join(parts)

    pa, pb = da["points"], db["points"]
    for i in range(min(len(pa), len(pb))):
        if pa[i] != pb[i]:
            first = "after" if pa[i] > pb[i] else "before"
            return first.upper(), (
                f"the paths diverge at hop {i}: {a_key} at line {pa[i]} "
                f"({da['path'][min(i, len(da['path']) - 1)][0]}), "
                f"{b_key} at line {pb[i]} ({db['path'][min(i, len(db['path']) - 1)][0]}), "
                f"and calls in one function run in source order")
    if len(pa) == len(pb):
        return "SAME", f"both are published at line {pa[-1]} - one read, two keys"
    shorter, longer = (a_key, b_key) if len(pa) < len(pb) else (b_key, a_key)
    return ("AFTER" if shorter == a_key else "BEFORE"), (
        f"{shorter}'s emission line is on {longer}'s call path, and the shorter path is a "
        f"prefix of the longer - so {shorter} is published before the call that leads further")


def render_path(detail):
    hops = [f"{p[0]}[{p[1]}]" for p in detail["path"]]
    return " -> ".join(hops) + f" -> emit@{detail['line']}"


def cmd_table(model, rung):
    print(f"the probe's stage calls, in source order "
          f"(switch value {rung} = ordinal rung {rung + 1}):")
    for line, name, gl in stage_table(model, rung):
        cond = " && ".join(gl) if gl else "(unguarded)"
        v = eval_guard(gl[-1], rung) if gl else True
        state = "runs" if v else ("skipped" if v is False else "UNKNOWN guard")
        print(f"  :{line:<5d} {name:<24s} {state:<13s} {cond}")
    keys = model["keys"]
    print(f"\n{len(model['funcs'])} function(s) parsed, {len(keys)} distinct key(s) published.")
    if model["templates"]:
        gen = sum(1 for v in keys.values() if v["generated_by"])
        print(f"  of which {gen} come from a key-generating macro:")
        for name in sorted(model["templates"]):
            tpl = model["templates"][name]
            lines = sorted({v["line"] for v in keys.values() if v["generated_by"] == name})
            print(f"    {name}() defined at :{tpl['line']} - {len(tpl['keys'])} key(s) per "
                  f"invocation, invoked at line(s) {', '.join(str(n) for n in lines)}")
    for u in model["unresolved"]:
        print(f"  UNRESOLVED {u['template']} at :{u['line']} - {u['why']}")


def cmd_key(model, names, rung):
    bad = 0
    for name in names:
        verdict, detail, why = evaluate(model, name, rung)
        if verdict in ("unknown", "dead"):
            print(f"  {name}: {verdict.upper()} - {why}")
            bad += 1
            continue
        kind = "INTERVAL" if verdict == "interval" else "POINT"
        print(f"  {name}: {kind}   {render_path(detail)}")
        if detail["emission_guard"]:
            print(f"      guard: {' && '.join(detail['emission_guard'])}")
        if detail["registered"]:
            print(f"      reached by registration of {', '.join(detail['registered'])} "
                  f"- the moment is an interval, not a point")
    return 1 if bad else 0


def cmd_after(model, event, names, rung):
    bad = 0
    print(f"claim: each key below is READ AFTER `{event}` "
          f"(switch value {rung} = ordinal rung {rung + 1}).")
    for name in names:
        rel, why = compare(model, name, event, rung)
        ok = rel == "AFTER"
        print(f"  {'ok  ' if ok else 'FAIL'}  {name:<34s} {rel:<10s} {why}")
        if not ok:
            bad += 1
    if bad:
        print(f"\n{bad} claim(s) not supported by the call order. A cell is a register at a "
              f"TIME; ask which key, emitted by which function, under which guard.")
    return 1 if bad else 0


SELFTEST = [
    # (event, key, expected relation, why this one)
    ("_pwr_before", "_pwr_status_after", "AFTER",
     "m763: the power-status pair is read either side of the byte; the 'after' really is after"),
    ("_pwr_status_after", "_reg_pwrctl_status_after", "BEFORE",
     "m763: the key 748 quoted for 'after the power byte' is published by the RESET stage, first"),
    ("_pwr_before", "_reg_power_control_after", "BEFORE",
     "m732: the rung-4 cell is read before rung 7 writes the byte"),
    ("_pwr_before", "_reg_power_control", "BEFORE",
     "m732: as above for the rung-3 twin"),
    # A GENERATED key, so the macro-expansion path is covered as well as the literal one.
    # `_cmd1_resp` does not exist as a string literal anywhere - it is `ST_CMD_PUBLISH("1", c1)`
    # at :3702 - and the same run's CMD0 census is published at :3637, before it.
    ("_cmd0_resp", "_cmd1_resp", "AFTER",
     "generated keys: the CMD0 census is published before the CMD1 one, both from st_cmd_path"),
    ("_cmd1_resp", "_cmd1_inhibit_seen", "AFTER",
     "generated keys: the 12-key macro runs after the 20-key one, at :3704 against :3702"),
]


def cmd_selftest(model, rung):
    bad = 0
    print(f"the two landed moments defects, re-derived by the call order "
          f"(switch value {rung} = ordinal rung {rung + 1}):")
    for event, key, want, why in SELFTEST:
        rel, why2 = compare(model, key, event, rung)
        ok = rel == want
        print(f"  {'ok  ' if ok else 'FAIL'}  {key:<32s} {rel:<10s} (want {want})  {why}")
        if not ok:
            print(f"        got: {why2}")
            bad += 1
    # the interval case must refuse to be a point
    rel, why = compare(model, "_pwr_irq_at", "_pwr_before", rung)
    ok = rel == "UNORDERED"
    print(f"  {'ok  ' if ok else 'FAIL'}  {'_pwr_irq_at':<32s} {rel:<10s} (want UNORDERED)  "
          f"a handler cell is an interval, so no point order exists")
    if not ok:
        print(f"        got: {why}")
        bad += 1
    if bad:
        print(f"\n{bad} self-test(s) failed - the tool disagrees with a landed finding, so it is "
              f"the tool that is wrong.")
    return 1 if bad else 0


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="when was that storage cell read, and by which probe stage?",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default=str(DEFAULT_SRC),
                    help=f"the entry source to read (default {DEFAULT_SRC})")
    ap.add_argument("--rung", type=int, default=None,
                    help=f"evaluate the {RUNG_MACRO} guards at this value (default: the value "
                         f"the image in out/ was built at, read from its own config - and a "
                         f"value outside the ladder's declared domain is a refusal)")
    ap.add_argument("--key", action="append", default=[], metavar="NAME",
                    help="a key to describe; repeatable, and a bare name is matched as a suffix")
    ap.add_argument("--after", metavar="EVENT_KEY",
                    help="assert that each --key is read AFTER this key")
    ap.add_argument("--selftest", action="store_true",
                    help="re-derive m732 and m763 from the call order")
    args = ap.parse_args(argv)

    try:
        source = Path(args.src).read_text()
    except OSError as exc:
        print(f"REFUSED: cannot read {args.src}: {exc}", file=sys.stderr)
        return 2

    # The rung: asked for, else the arm in `out/`, else the top of the ladder. Then checked
    # against the ladder's own `#error` guard, because a rung the source refuses to compile
    # is not a rung, and quietly evaluating the guards at one would answer a question about
    # an image that cannot exist.
    domain = ladder_domain(source)
    if args.rung is not None:
        rung, how = args.rung, "given on the command line"
    else:
        live = live_rung(REPO_ROOT / "out" / "stage90" / "xnu_arm_entry-config.txt")
        if live is not None:
            rung, how = live, "the image in out/ was built at this value"
        elif domain:
            rung, how = domain[1], "the top of the ladder's own declared domain"
        else:
            print("REFUSED: no --rung given, no arm in out/ to read one from, and the source's "
                  "own domain guard could not be parsed. A rung this tool cannot establish is "
                  "not a rung it will assume.", file=sys.stderr)
            return 2
    if domain is None:
        print("REFUSED: the source's own `#error` guard does not state the ladder's domain, so "
              "no rung can be checked against it. Restating the bound here would be a second "
              "definition of the ladder - the repair is to read the guard, and a guard this "
              "tool cannot read is a refusal, not a pass.", file=sys.stderr)
        return 2
    lo, hi, cond = domain
    if not (lo < rung <= hi):
        extra = ""
        if rung == hi + 1:
            # The record's two spellings, and they are one apart at exactly this end. An
            # experiment subject counts ORDINAL rungs (the 21st arm is "rung 21"), while the
            # switch counts the VALUE. A refusal here is the right answer, but a refusal that
            # does not say why sends the next reader looking for a missing rung.
            extra = (" NOTE: the record counts ORDINAL rungs - `%d` is the %dth arm and the "
                     "ladder's \"rung %d\" IS value %d. The value is the spelling the guards "
                     "and every capture cell use." % (hi, hi, hi + 1, hi))
        print(f"REFUSED: --rung {rung} is outside the ladder's declared domain ({cond}), so no "
              f"image exists at that value and every guard below would be evaluated for one "
              f"that does not compile.{extra}", file=sys.stderr)
        return 2
    model = parse(source)
    if not model["keys"]:
        print(f"REFUSED: no ST_LIVE key found in {args.src} - the wrong file, or the macro "
              f"changed shape, and either way this tool has no reading to give.", file=sys.stderr)
        return 2

    # **Both spellings, always, and that is not decoration.** The guards in the source, the value
    # in `out/`'s config and this tool all count the SWITCH; the capture filenames and the commit
    # subjects count ORDINAL arms (`rung20-nrsp-...` is value 19). The two are one apart, and the
    # 754 section 4 sentence that got this wrong did so *while quoting this tool's header* - which
    # carried only the switch's spelling. So every header carries both, and the ordinal is derived
    # here rather than left to a reader who has already read the header and is now looking at a
    # capture's name.
    print(f"# rung {rung} (switch VALUE; the record's ordinal rung {rung + 1}) "
          f"[{how}; domain {cond}]")

    def resolve(names):
        out, missing = [], []
        for n in names:
            if n in model["keys"]:
                out.append(n)
                continue
            hits = [k for k in model["keys"] if k.endswith(n) or k == "xnu_live_storage" + n]
            if len(hits) == 1:
                out.append(hits[0])
            elif len(hits) > 1:
                missing.append(f"{n} matches {len(hits)} keys: {', '.join(sorted(hits)[:4])}")
            else:
                missing.append(f"{n} matches no key")
        return out, missing

    if args.selftest:
        return cmd_selftest(model, rung)

    if args.after:
        ev, miss = resolve([args.after])
        keys, miss2 = resolve(args.key)
        if miss or miss2:
            print("REFUSED: " + "; ".join(miss + miss2), file=sys.stderr)
            return 2
        if not keys:
            print("REFUSED: --after needs at least one --key to test against it.", file=sys.stderr)
            return 2
        return cmd_after(model, ev[0], keys, rung)

    if args.key:
        keys, miss = resolve(args.key)
        if miss:
            print("REFUSED: " + "; ".join(miss), file=sys.stderr)
            return 2
        return cmd_key(model, keys, rung)

    cmd_table(model, rung)
    return 0


if __name__ == "__main__":
    sys.exit(main())
