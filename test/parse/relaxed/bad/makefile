all:
	@echo "[all tests in 'test/parse/relaxed/bad' should yield parse errors]"
	@-for i in *.sol; do ../../../../dimocheck -q `basename $$i .sol`.cnf $$i && exit 1; done; exit 0
test:
	@./run.sh
.PHONY: all test
