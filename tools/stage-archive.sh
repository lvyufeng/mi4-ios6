#!/usr/bin/env bash
#
# Access the archived stage snapshots (stage0 .. stage84) that are no longer in
# the working tree. They still exist in the git history at the archive tag.
#
# Restoring puts a snapshot back at the repository ROOT, not under stages/, so
# that its build scripts keep working unchanged - they were written when every
# stage lived at `<root>/stageN/`, so they resolve `../out`, `../external` and
# `../tools` relative to that location. Delete the directory again afterwards.
#
# The snapshot-per-stage model was retired on 2026-09-26; the five snapshots that
# were still in the working tree then (stage85 .. stage89) live under
# `archive/stages/` and are NOT under the tag, so `list` reports them separately
# and `show`/`diff` (which read the tag) do not apply to them.
set -euo pipefail

ARCHIVE_TAG=${STAGE_ARCHIVE_TAG:-stage-archive-base}
REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)

usage() {
  cat <<'EOF'
usage: tools/stage-archive.sh <command> [args]

  list             list every archived stage and where it is present now
  restore <N>      restore stage<N> to the repository root
  show <N> <path>  print one file from an archived stage
  diff <N> [path]  diff an archived stage against the working tree

The archive tag defaults to `stage-archive-base` (override with
STAGE_ARCHIVE_TAG). It points at the last commit where all 91 stage directories
were present in the tree.

Snapshots still kept in the working tree - archived from the retired
snapshot-per-stage model - live under `archive/stages/` and are listed by
`list` as well. They are not under the tag, so `show` and `diff` do not reach
them; read them in place.
EOF
}

die() { echo "stage-archive: $*" >&2; exit 1; }

if ! git -C "$REPO_ROOT" rev-parse --verify --quiet "$ARCHIVE_TAG" >/dev/null; then
  die "archive tag '$ARCHIVE_TAG' not found; set STAGE_ARCHIVE_TAG"
fi

# Every stage directory recorded at the archive tag, one number per line.
archived_stages() {
  git -C "$REPO_ROOT" ls-tree --name-only "$ARCHIVE_TAG" |
    sed -nE 's/^stage([0-9]+)$/\1/p' | sort -n
}

require_stage() {
  archived_stages | grep -qx "$1" || die "stage$1 is not in the archive"
}

# Where a given stage lives in the working tree today, if anywhere.
# (An if/elif with no matching branch still returns 0, hence the explicit
# `return 1` - callers rely on the status to tell "archived" from "present".)
worktree_stage() {
  if [[ -d $REPO_ROOT/stage$1 ]]; then
    echo "$REPO_ROOT/stage$1"
    return 0
  elif [[ -d $REPO_ROOT/stages/stage$1 ]]; then
    echo "$REPO_ROOT/stages/stage$1"
    return 0
  elif [[ -d $REPO_ROOT/archive/stages/stage$1 ]]; then
    echo "$REPO_ROOT/archive/stages/stage$1"
    return 0
  fi
  return 1
}

# The snapshots kept in the working tree under archive/stages/, newest last.
archived_in_tree() {
  ls -d "$REPO_ROOT"/archive/stages/stage* 2>/dev/null | sort -V
}

command=${1:-}
case "$command" in
  list)
    while read -r n; do
      if present=$(worktree_stage "$n"); then
        state="present at ./${present#"$REPO_ROOT"/}"
      else
        state="archived (restore with: tools/stage-archive.sh restore $n)"
      fi
      printf 'stage%-3s %s\n' "$n" "$state"
    done < <(archived_stages)
    while read -r d; do
      [[ -n $d ]] || continue
      printf '%-12s %s files, kept in the working tree at ./%s\n' \
        "$(basename "$d")" "$(ls -1 "$d" | wc -l)" "${d#"$REPO_ROOT"/}"
    done < <(archived_in_tree)
    echo
    echo "The snapshot-per-stage model was retired on 2026-09-26; the tree above is one"
    echo "evolving tree now. Read archive/stages/README.md for what the snapshots are."
    ;;

  path)
    n=${2:-}
    [[ -n $n ]] || die "usage: stage-archive.sh path <N>"
    require_stage "$n"
    echo "$REPO_ROOT/stage$n"
    ;;

  restore)
    n=${2:-}
    [[ -n $n ]] || die "usage: stage-archive.sh restore <N>"
    require_stage "$n"
    dest=$REPO_ROOT/stage$n
    if [[ -e $dest ]]; then
      die "$dest already exists"
    fi
    git -C "$REPO_ROOT" archive "$ARCHIVE_TAG" "stage$n" | tar -x -C "$REPO_ROOT"
    cat <<EOF
restored $dest

note: this snapshot predates the stages/ move, so its build scripts still
      resolve ../out, ../external and ../tools - which is why it was put back
      at the repository root rather than under stages/.
        build:  cd stage$n && ./build.sh
        remove: rm -rf stage$n
EOF
    ;;

  show)
    n=${2:-}
    path=${3:-}
    [[ -n $n && -n $path ]] || die "usage: stage-archive.sh show <N> <path>"
    require_stage "$n"
    git -C "$REPO_ROOT" show "$ARCHIVE_TAG:stage$n/$path"
    ;;

  diff)
    n=${2:-}
    path=${3:-}
    [[ -n $n ]] || die "usage: stage-archive.sh diff <N> [path]"
    require_stage "$n"
    right=$(worktree_stage "$n")
    [[ -n $right ]] || die "stage$n is not present in the working tree; use 'show' instead"
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    git -C "$REPO_ROOT" archive "$ARCHIVE_TAG" "stage$n" | tar -x -C "$tmp"
    rc=0
    if [[ -n $path ]]; then
      diff -u "$tmp/stage$n/$path" "$right/$path" || rc=$?
    else
      diff -ru --new-file "$tmp/stage$n" "$right" || rc=$?
    fi
    case $rc in
      0) echo "stage-archive: stage$n is identical to the archived copy" ;;
      1) ;; # differences were printed above
      *) die "diff failed (exit $rc)" ;;
    esac
    ;;

  ""|-h|--help|help)
    usage
    ;;

  *)
    die "unknown command '$command' (try --help)"
    ;;
esac
