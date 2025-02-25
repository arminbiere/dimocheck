all:
	for i in *.sol; do ../../../../dimocheck -q -s `basename $$i .sol`.cnf $$i || exit 1; done
test:
	@./run.sh
.PHONY: test
