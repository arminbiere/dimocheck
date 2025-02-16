These are expected invalid files for strict parsing.

If you issue `make here` all the failing tests will be run,
ignoring the failing exit code.  This allows to step through
all the errors one-by-one and check that error message line
and column information is correct.

The actual test is `make test` which runs `run.sh`.
