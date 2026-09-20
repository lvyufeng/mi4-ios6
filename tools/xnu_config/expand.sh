#!/bin/bash
# Recursively expand a configuration's attribute set, the way doconf's awk stage does: a
# configuration's attributes may themselves be configuration names, and only the leaves are
# options. Prints the selected option lines.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
"$HERE/select_master.sh" "$1" | awk '-F#' '
part == 0 && $1 != "" { m[$1]=m[$1] " " $2; next }
part == 0 && $1 == "" {
	# **A configuration the tree does not declare is an error, not a small answer (488).** The
	# selection below gives an undeclared name the `+` attribute, and `+` is what every untagged
	# `options` line in MASTER matches - so `STAGE90_XNU` without its fragment used to expand to the
	# ten always-on options and nothing else, silently, for however many files the caller compiled.
	# Ten defines is a *plausible* set (the kernel links, the boot runs) and that is the whole
	# problem: the mismatch it caused was between the C objects, which had the caller-side
	# `XNU_MASTER_LOCAL` and therefore 110, and the ARM assembly, which had 10 - and the assembled
	# `locore.o` then called the two `timer_state_event_*` functions the C side does not define,
	# which the generator stubbed and which stopped the boot at the first return to user mode
	# (experiment 488). `m[]` holds every declaration from MASTER, MASTER.arm and whichever fragment
	# was found, and this line is emitted after all of them.
	req = substr($NF, 2)
	if (req != "" && m[req] == "") {
		printf "expand.sh: configuration '%s' is not declared by MASTER, MASTER.arm or a .local fragment\n", req > "/dev/stderr"
		printf "  an undeclared name would select every untagged option and no tagged one - a small, plausible answer\n" > "/dev/stderr"
		printf "  the fragment is tools/xnu_config/<dir>/%s.local; XNU_MASTER_LOCAL overrides the search\n", req > "/dev/stderr"
		exit 1
	}
	for (i=NF;i>1;i--){ s=substr($i,2); c[++na]=substr($i,1,1); a[na]=s }
	while (na > 0){
		s=a[na]; d=c[na--];
		if (m[s] == "") { f[s]=d }
		else { nx=split(m[s],x," "); for (j=nx;j>0;j--){ z=x[j]; a[++na]=z; c[na]=d } }
	}
	part=1; next
}
part != 0 {
	if ($1 != "") {
		n=split($1,x,","); ok=0;
		for (i=1;i<=n;i++){ if (f[x[i]] == "+") { ok=1 } }
		if (NF > 2 && ok == 0 || NF <= 2 && ok != 0) { print $2 }
	} else { print $2 }
}'
