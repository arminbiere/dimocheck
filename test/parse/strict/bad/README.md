These are expected invalid files for strict parsing.

If you issue `make here` all the failing tests will be run,
ignoring the failing exit code.  This allows to step through
all the errors one-by-one and check that error message line
and column information is correct.

The first 'all' goal is actually just and alias to the 'strict'
goal and there is also a 'relaxed' goal which checks the same
files and solutions but in relaxed parsing mode and thus
checking should succeed.

The actual test is `make test` which runs `run.sh`.
