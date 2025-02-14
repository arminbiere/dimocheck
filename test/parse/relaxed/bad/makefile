all:
	@echo "[all tests should yield parse errors]"
	@-for i in *.sol; do ../../../../dimocheck -q `basename $$i .sol`.cnf $$i; done; exit 0
test:
	@./run.sh
.PHONY: all test
