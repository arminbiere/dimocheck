all:
	@echo "[make 'all' in 'test/parse/strict/bad' should yield parse errors]"
	@-for i in *.sol; do ../../../../dimocheck -q -s `basename $$i .sol`.cnf $$i; done; exit 0
test:
	@./run.sh
.PHONY: all test
