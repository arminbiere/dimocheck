#!/bin/sh
path=test/parse/strict/bad
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
  if $binary $args -s 1>/dev/null 2>/dev/null
  then
    die "'dimocheck $args -s' unexpectedly succeeded in strict mode"
    exit 1
  fi
  $binary $args 1>/dev/null || \
    die "'dimocheck $args' failed in relaxed mode"
done
