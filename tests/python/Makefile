.PHONY: test test-verbose

test:
	@echo "======================================="
	@echo "Running Python quality tests (PR 6)"
	@echo "======================================="
	python3 -m unittest test_python_quality -v
	@echo ""
	@echo "======================================="
	@echo "Running SQL safety tests (PR 5)"
	@echo "======================================="
	python3 -m unittest test_sql_safety -v

test-verbose:
	python3 -m unittest test_python_quality -v
	python3 -m unittest test_sql_safety -v
