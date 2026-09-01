.PHONY: all test test-verbose clean

all: test

test:
	$(MAKE) -C c test
	$(MAKE) -C python test

test-verbose:
	$(MAKE) -C c test-verbose
	$(MAKE) -C python test-verbose

clean:
	$(MAKE) -C c clean
