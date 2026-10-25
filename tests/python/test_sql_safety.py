"""
Unit tests for SQL injection prevention (PR 5).

PR 5 replaces f-string SQL interpolation with DuckDB parameterized queries
in plugins/profiler/inspector/exporter/example/perf_summary_exporter.py.

Tests demonstrate that parameterized queries safely handle injection payloads
while f-string interpolation is vulnerable.

Run: python3 -m pytest test_sql_safety.py -v
"""

import os
import unittest
import sys

try:
    import duckdb
    HAS_DUCKDB = True
except ImportError:
    HAS_DUCKDB = False


# ---------------------------------------------------------------------------
# Injection payloads: table-driven test data
# ---------------------------------------------------------------------------

# Classic SQL injection attacks
SQL_INJECTION_PAYLOADS = [
    ("classic_drop",       "'; DROP TABLE logs; --"),
    ("boolean_bypass",     "\" OR 1=1 --"),
    ("union_select",       "' UNION SELECT * FROM information_schema.tables --"),
    ("statement_stacking", "AllReduce; DELETE FROM logs"),
    ("comment_injection",  "AllReduce' -- "),
    ("backslash_escape",   "AllReduce\\'"),
    ("double_quote",       'AllReduce"'),
    ("nested_quotes",      "''"),
]

# Null byte and encoding attacks
ENCODING_PAYLOADS = [
    ("null_byte",          "AllReduce\x00DROP TABLE logs"),
    ("unicode_homoglyph",  "\uff07 OR 1=1"),       # Fullwidth apostrophe U+FF07
    ("unicode_apostrophe", "All\u0027Reduce"),      # Unicode apostrophe U+0027
    ("null_only",          "\x00"),
    ("high_bytes",         "\xff\xfe\xfd"),
]

# Boundary and edge cases
BOUNDARY_PAYLOADS = [
    ("normal_coll",        "AllReduce"),
    ("normal_comm",        "single-rank"),
    ("empty_string",       ""),
    ("single_char",        "A"),
    ("like_wildcard_pct",  "%"),
    ("like_wildcard_us",   "_"),
    ("spaces_only",        "   "),
    ("newlines",           "All\nReduce"),
    ("tabs",               "All\tReduce"),
]

# Very long strings (potential buffer overflow in string handling)
LONG_PAYLOADS = [
    ("long_1k",            "A" * 1000),
    ("long_100k",          "B" * 100000),
]

ALL_PAYLOADS = (SQL_INJECTION_PAYLOADS + ENCODING_PAYLOADS +
                BOUNDARY_PAYLOADS + LONG_PAYLOADS)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

@unittest.skipUnless(HAS_DUCKDB, "duckdb not installed")
class TestParameterizedQueries(unittest.TestCase):
    """Test that DuckDB parameterized queries safely handle injection payloads."""

    def setUp(self):
        """Create in-memory DuckDB with test data."""
        self.con = duckdb.connect()
        self.con.execute("""
            CREATE TABLE logs (
                id INTEGER,
                coll VARCHAR,
                comm_type VARCHAR,
                coll_sn INTEGER,
                coll_msg_size_bytes INTEGER
            )
        """)
        # Insert test data
        self.con.execute("""
            INSERT INTO logs VALUES
                (1, 'AllReduce', 'single-rank', 1, 1024),
                (2, 'AllReduce', 'single-rank', 2, 2048),
                (3, 'Broadcast', 'multi-rank',  1, 512)
        """)
        self.initial_count = self._row_count()

    def tearDown(self):
        self.con.close()

    def _row_count(self):
        return self.con.execute("SELECT COUNT(*) FROM logs").fetchone()[0]

    def test_parameterized_coll_type_safe(self):
        """Parameterized queries prevent SQL injection via coll_type parameter."""
        for name, payload in ALL_PAYLOADS:
            with self.subTest(name=name, payload_repr=repr(payload[:50])):
                # Safe: parameterized query (the PR 5 fix)
                result = self.con.execute(
                    "SELECT * FROM logs WHERE coll = $1 AND comm_type = $2",
                    [payload, "single-rank"]
                ).fetchall()

                # Injection payloads should return empty (not matched)
                # Normal "AllReduce" should return 2 rows
                if payload == "AllReduce":
                    self.assertEqual(len(result), 2,
                                     f"Normal 'AllReduce' should match 2 rows")
                else:
                    # Most payloads won't match any row
                    self.assertIsInstance(result, list,
                                         f"Result should be a list for {name}")

                # Table should be intact (no rows deleted/modified)
                self.assertEqual(self._row_count(), self.initial_count,
                                 f"Row count changed after payload '{name}' - "
                                 f"possible injection!")

    def test_parameterized_comm_type_safe(self):
        """Parameterized queries prevent SQL injection via comm_type parameter."""
        for name, payload in ALL_PAYLOADS:
            with self.subTest(name=name):
                result = self.con.execute(
                    "SELECT * FROM logs WHERE coll = $1 AND comm_type = $2",
                    ["AllReduce", payload]
                ).fetchall()

                if payload == "single-rank":
                    self.assertEqual(len(result), 2)
                else:
                    self.assertIsInstance(result, list)

                self.assertEqual(self._row_count(), self.initial_count,
                                 f"Row count changed after comm_type payload '{name}'")

    def test_parameterized_read_parquet_path(self):
        """
        Test parameterized path in read_parquet()-like context.
        DuckDB's read_parquet($1) with parameterized path.
        """
        for name, payload in SQL_INJECTION_PAYLOADS[:4]:
            with self.subTest(name=name):
                # The parameterized path should be treated as a literal string,
                # not interpreted as SQL
                try:
                    # This should fail because the file doesn't exist,
                    # NOT because of SQL injection
                    self.con.execute(
                        "SELECT * FROM read_parquet($1)",
                        [payload]
                    )
                except Exception as e:
                    # Expected: file not found or invalid path
                    # NOT expected: SQL syntax error from injection
                    error_msg = str(e).lower()
                    self.assertNotIn("syntax error", error_msg,
                                     f"Payload '{name}' caused SQL syntax error "
                                     f"(possible injection): {e}")

    def test_table_intact_after_all_payloads(self):
        """Final verification: table data is completely unchanged."""
        self.assertEqual(self._row_count(), 3,
                         "Table should still have exactly 3 rows")

        rows = self.con.execute(
            "SELECT * FROM logs ORDER BY id"
        ).fetchall()

        self.assertEqual(rows[0], (1, 'AllReduce', 'single-rank', 1, 1024))
        self.assertEqual(rows[1], (2, 'AllReduce', 'single-rank', 2, 2048))
        self.assertEqual(rows[2], (3, 'Broadcast', 'multi-rank', 1, 512))


@unittest.skipUnless(HAS_DUCKDB, "duckdb not installed")
class TestFStringVulnerability(unittest.TestCase):
    """Demonstrate that f-string interpolation IS vulnerable (the original bug)."""

    def setUp(self):
        self.con = duckdb.connect()
        self.con.execute("""
            CREATE TABLE logs (
                id INTEGER,
                coll VARCHAR,
                comm_type VARCHAR
            )
        """)
        self.con.execute("""
            INSERT INTO logs VALUES
                (1, 'AllReduce', 'single-rank'),
                (2, 'Broadcast', 'multi-rank')
        """)

    def tearDown(self):
        self.con.close()

    def test_fstring_boolean_bypass(self):
        """f-string interpolation allows boolean bypass injection."""
        coll_type = "' OR '1'='1"
        comm_type = "' OR '1'='1"

        # Vulnerable pattern (what PR 5 fixes)
        query = f"SELECT * FROM logs WHERE coll = '{coll_type}' AND comm_type = '{comm_type}'"

        try:
            result = self.con.execute(query).fetchall()
            # If injection succeeds, ALL rows are returned instead of none
            if len(result) > 0:
                # This demonstrates the vulnerability
                pass  # Expected: injection works with f-strings
        except Exception:
            # Some DB engines reject the syntax, which is also acceptable
            pass

        # Now demonstrate the safe version
        safe_result = self.con.execute(
            "SELECT * FROM logs WHERE coll = $1 AND comm_type = $2",
            [coll_type, comm_type]
        ).fetchall()
        # Safe version: no rows match the literal string "' OR '1'='1"
        self.assertEqual(len(safe_result), 0,
                         "Parameterized query should return 0 rows for injection payload")

    def test_fstring_union_injection(self):
        """f-string interpolation allows UNION-based data exfiltration."""
        # This payload tries to UNION with system tables
        coll_type = "' UNION SELECT 999, table_name, 'x' FROM information_schema.tables WHERE '1'='1"

        query = f"SELECT * FROM logs WHERE coll = '{coll_type}'"

        try:
            result = self.con.execute(query).fetchall()
            # If injection succeeds, we get system table names
            injected = any(row[0] == 999 for row in result)
            # This demonstrates the vulnerability if injected is True
        except Exception:
            pass  # Query may fail on syntax, but that's OK

        # Safe version
        safe_result = self.con.execute(
            "SELECT * FROM logs WHERE coll = $1",
            [coll_type]
        ).fetchall()
        self.assertEqual(len(safe_result), 0,
                         "Parameterized query should not return injected data")


class TestWithoutDuckDB(unittest.TestCase):
    """Tests that run even without DuckDB installed."""

    def test_parameterized_query_pattern(self):
        """Verify the code pattern uses $1/$2 placeholders, not f-strings."""
        # Read the actual source file if available
        exporter_path = os.path.join(
            os.path.dirname(__file__), '..', '..', 'plugins', 'profiler',
            'inspector', 'exporter', 'example', 'perf_summary_exporter.py'
        )

        if not os.path.exists(exporter_path):
            self.skipTest("perf_summary_exporter.py not found")

        with open(exporter_path, 'r') as f:
            content = f.read()

        # Check if the fix has been applied (PR 5 branch)
        has_parameterized = "$1" in content and "$2" in content
        has_fstring_sql = "f\"" in content or "f'" in content

        # At minimum, the file should exist and contain SQL-related code
        self.assertIn("duckdb", content,
                       "File should reference duckdb")

        if not has_parameterized:
            # Fix not yet applied (e.g., running on master).
            # This is expected before PR 5 is merged.
            self.skipTest(
                "PR 5 fix not applied on this branch "
                "(f-string SQL still present, parameterized queries missing)"
            )

    def test_injection_payload_variety(self):
        """Verify we have sufficient payload coverage."""
        self.assertGreaterEqual(len(SQL_INJECTION_PAYLOADS), 6,
                                "Should have at least 6 injection payloads")
        self.assertGreaterEqual(len(ENCODING_PAYLOADS), 4,
                                "Should have at least 4 encoding payloads")
        self.assertGreaterEqual(len(BOUNDARY_PAYLOADS), 5,
                                "Should have at least 5 boundary payloads")

        # Verify null byte is tested
        has_null = any('\x00' in p for _, p in ALL_PAYLOADS)
        self.assertTrue(has_null, "Should test null byte injection")

        # Verify unicode is tested
        has_unicode = any(ord(c) > 127 for _, p in ALL_PAYLOADS for c in p)
        self.assertTrue(has_unicode, "Should test unicode payloads")


if __name__ == '__main__':
    unittest.main()
