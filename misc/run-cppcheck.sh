#!/bin/bash
set -Eeuo pipefail

SCRIPTNAME=${0#*/} ; die() { [ -z "$*" ] || echo "$SCRIPTNAME: $*" >&2; exit 127 ; }
test -e bse/bsemain.cc || die "script needs to run in beast/ root"

# prepare input file list
FILELIST=.ls.cppcheck
echo >$FILELIST
trap "rm -f $FILELIST" ERR EXIT

# configure default checks
CHECKLIST=warning,style,performance,portability
PARALLEL=-j`nproc`

# parse options and file list
ALLFILES=false
ANY=false
while test $# -ne 0 ; do
  case "$1" in
    --all)		ALLFILES=true ;;
    -u)			PARALLEL= CHECKLIST=$CHECKLIST,unusedFunction ;;
    -h)                 echo "Usage: $0 [--all] [-u] [sourcefiles...]"
			echo "Run cppcheck on the beast sources."
			echo "  -h    help message"
			echo "  -u    check for unused function"
			echo "  --all check all sources (default)"
			exit 0 ;;
    *)			printf '%s\n' "$1" >> $FILELIST ; ANY=true ;;
  esac
  shift
done

# gather input file list
$ANY || ALLFILES=true	# assume --all if no files are given
$ALLFILES && {
  git ls-tree -r --name-only HEAD |
    egrep '^(aidacc|sfi|bse|plugins|drivers|beast-gtk)/.*\.(cc|hh)$' >$FILELIST
}

# check cppchec is present since the stderr redirection below may swallow error messages
${CPPCHECK:-cppcheck} --version >/dev/null || die "failed to execute: ${CPPCHECK:-cppcheck}"

# run cppcheck, post-process stderr
${CPPCHECK:-cppcheck} $PARALLEL --enable=$CHECKLIST \
    -D__SIZEOF_LONG__=8 \
    -D__SIZEOF_WCHAR_T__=4 \
    -U_SC_NPROCESSORS_ONLN \
    -U_WIN32 \
    -U__clang__ \
    `cat $FILELIST` \
    2> >(
  sed -r 's/^\[([^: ]+:[0-9]+)\]:? /\1: /' | # fix extraneous []-brakets so editors can match locations
    tee cppcheck.err
)

# display error log
test -e cppcheck.err && {
  MSG="Logfile: "`wc ./cppcheck.err`
  echo "$MSG" | sed 's/./=/g'
  echo "$MSG"
}
