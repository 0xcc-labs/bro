
DIRS=btest external

all: make-verbose coverage

brief: make-brief coverage

distclean:
	@rm -f coverage.log
	$(MAKE) -C btest $@

make-verbose:
	@for repo in $(DIRS); do (cd $$repo && make -s ); done

make-brief:
	@for repo in $(DIRS); do (cd $$repo && make -s brief ); done

coverage:
	@for repo in $(DIRS); do (cd $$repo && echo "Coverage for '$$repo' dir:" && make -s coverage); done
	@test -f btest/coverage.log && cp btest/coverage.log `mktemp brocov.tmp.XXXXXX` || true
	@for f in external/*/coverage.log; do test -f $$f && cp $$f `mktemp brocov.tmp.XXXXXX` || true; done
	@echo "Complete test suite code coverage:"
	@./scripts/coverage-calc "brocov.tmp.*" coverage.log `pwd`/../scripts
	@rm -f brocov.tmp.*

