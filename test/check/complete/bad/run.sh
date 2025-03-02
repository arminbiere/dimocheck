#!/bin/sh
path=test/check/complete/bad
name=$path/run.sh
die () {
  echo "$name: error: $*" 1>&2
  exit 1
}
cd `dirname $0` || exit 1
cd ../../../.. || exit 1
binary=./dimocheck
[ -f $binary ] || die "could not find 'dimocheck'"
echo "[running '$name']"
for cnf in $path/*.cnf
do
  sol=$path/`basename $cnf .cnf`.sol
  [ -f $sol ] || die "could not find '$sol'"
  args="$cnf $sol -q"
  $binary $args -c 1>/dev/null 2>/dev/null && \
    die "'dimocheck $args -c' unexpectedly succeeded in complete mode"
  $binary $args 1>/dev/null 2>/dev/null || \
    die "'dimocheck $args' unexpectedly failed in partial mode"
done
