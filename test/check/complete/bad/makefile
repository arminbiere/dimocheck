all: complete
complete:
	@echo "[make 'all' in 'test/check/complete/bad' should yield parse errors]"
	@-for i in *.sol; do ../../../../dimocheck -q -c `basename $$i .sol`.cnf $$i && exit 1; done; exit 0
partial:
	for i in *.sol; do ../../../../dimocheck -q `basename $$i .sol`.cnf $$i; done
test:
	@./run.sh
.PHONY: all strict relaxed test
