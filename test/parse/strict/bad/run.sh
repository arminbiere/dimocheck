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
  args="$cnf $sol -s -q"
  #echo "dimocheck $args"
  if $binary $args 1>/dev/null
  then
    die "'dimocheck $args' unexpectedly succeeded"
    exit 1
  fi
done
