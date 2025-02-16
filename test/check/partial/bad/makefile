all:
	@echo "[all tests in 'test/check/partial/bad' should yield failing checks]"
	@-for i in *.sol; do ../../../../dimocheck -q `basename $$i .sol`.cnf $$i && exit 1; done; exit 0
test:
	@./run.sh
.PHONY: all test
