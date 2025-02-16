all: strict
strict:
	@echo "[make 'all' in 'test/parse/strict/bad' should yield parse errors]"
	@-for i in *.sol; do ../../../../dimocheck -q -s `basename $$i .sol`.cnf $$i && exit 1; done; exit 0
relaxed:
	for i in *.sol; do ../../../../dimocheck -q `basename $$i .sol`.cnf $$i; done
relaxed:
test:
	@./run.sh
.PHONY: all strict relaxed test
