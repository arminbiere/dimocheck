all:
	for i in *.sol; do ../../../../dimocheck -q -c `basename $$i .sol`.cnf $$i || exit 1; done
test:
	@./run.sh
.PHONY: test
