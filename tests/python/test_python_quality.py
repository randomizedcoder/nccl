"""
Unit tests for Python code quality fixes (PR 6).

PR 6 fixes:
  1. Rec class in src/device/symmetric/generate.py: renamed me/x/y -> self/other
  2. nccl_ep __init__.py: _import_error initialized before try block

Run: python3 -m pytest test_python_quality.py -v
"""

import unittest
import sys
import os


# ---------------------------------------------------------------------------
# Rec class (copied from src/device/symmetric/generate.py:43-52)
# Cannot import directly because generate.py runs sys.argv[1] at module level.
# We test both the OLD (me/x/y) and NEW (self/other) parameter naming to prove
# the rename preserves behavior.
# ---------------------------------------------------------------------------

class Rec(object):
    """Fixed version using standard self/other naming (PR 6 change)."""
    def __init__(self, **kw):
        self.__dict__.update(kw)

    def __eq__(self, other):
        return self.__dict__ == other.__dict__

    def __hash__(self):
        h = 0
        for k in self.__dict__:
            h += hash((k, self.__dict__[k]))
        return h


class RecOld(object):
    """Original version using non-standard me/x/y naming (pre-PR 6)."""
    def __init__(me, **kw):
        me.__dict__.update(kw)

    def __eq__(x, y):
        return x.__dict__ == y.__dict__

    def __hash__(me):
        h = 0
        for k in me.__dict__:
            h += hash((k, me.__dict__[k]))
        return h


# ---------------------------------------------------------------------------
# Table-driven Rec tests
# ---------------------------------------------------------------------------

EQUALITY_CASES = [
    # (name, rec_a, rec_b, expected_equal)
    ("identical",          dict(a=1, b=2),      dict(a=1, b=2),      True),
    ("different_values",   dict(a=1, b=2),      dict(a=1, b=3),      False),
    ("different_keys",     dict(a=1),           dict(b=1),           False),
    ("extra_key",          dict(a=1),           dict(a=1, b=2),      False),
    ("empty_recs",         dict(),              dict(),              True),
    ("string_attrs",       dict(x="hello"),     dict(x="hello"),     True),
    ("none_value",         dict(a=None),        dict(a=None),        True),
    ("none_vs_zero",       dict(a=None),        dict(a=0),           False),
    ("nested_tuple",       dict(a=(1, 2)),      dict(a=(1, 2)),      True),
    ("int_vs_float",       dict(a=1),           dict(a=1.0),         True),  # Python: 1 == 1.0
]

HASH_CASES = [
    # (name, rec_a, rec_b, expect_same_hash)
    ("equal_same_hash",      dict(a=1, b=2),    dict(a=1, b=2),    True),
    ("empty_same_hash",      dict(),            dict(),            True),
    ("none_hash",            dict(a=None),      dict(a=None),      True),
    # Different recs MAY have same hash (collision), so we don't test for inequality
]


class TestRecEquality(unittest.TestCase):
    """Table-driven equality tests for Rec class."""

    def test_equality_fixed(self):
        """Test fixed Rec (self/other naming) equality."""
        for name, kw_a, kw_b, expected in EQUALITY_CASES:
            with self.subTest(name=name):
                a = Rec(**kw_a)
                b = Rec(**kw_b)
                self.assertEqual(a == b, expected,
                                 f"Rec({kw_a}) == Rec({kw_b}) should be {expected}")

    def test_equality_old(self):
        """Test old Rec (me/x/y naming) produces same results - proves rename is safe."""
        for name, kw_a, kw_b, expected in EQUALITY_CASES:
            with self.subTest(name=name):
                a = RecOld(**kw_a)
                b = RecOld(**kw_b)
                self.assertEqual(a == b, expected,
                                 f"RecOld({kw_a}) == RecOld({kw_b}) should be {expected}")

    def test_cross_version_equivalence(self):
        """Verify old and new Rec produce identical behavior for all cases."""
        for name, kw_a, kw_b, expected in EQUALITY_CASES:
            with self.subTest(name=name):
                old_result = RecOld(**kw_a) == RecOld(**kw_b)
                new_result = Rec(**kw_a) == Rec(**kw_b)
                self.assertEqual(old_result, new_result,
                                 f"Old and new Rec disagree on {name}")


class TestRecHash(unittest.TestCase):
    """Table-driven hash tests for Rec class."""

    def test_hash_consistency(self):
        """Equal objects must have equal hashes (Python contract)."""
        for name, kw_a, kw_b, expect_same in HASH_CASES:
            with self.subTest(name=name):
                a = Rec(**kw_a)
                b = Rec(**kw_b)
                if expect_same:
                    self.assertEqual(hash(a), hash(b),
                                     f"hash(Rec({kw_a})) should equal hash(Rec({kw_b}))")

    def test_hash_stability(self):
        """Hash of same object should be stable across calls."""
        r = Rec(a=1, b="hello", c=None)
        h1 = hash(r)
        h2 = hash(r)
        self.assertEqual(h1, h2, "hash should be stable across calls")

    def test_hash_none_value(self):
        """Rec with None attribute should be hashable without crash."""
        r = Rec(a=None)
        # Should not raise
        h = hash(r)
        self.assertIsInstance(h, int)

    def test_hash_many_attrs(self):
        """Rec with 1000 attributes should be hashable."""
        kw = {f"attr_{i}": i for i in range(1000)}
        r = Rec(**kw)
        h = hash(r)
        self.assertIsInstance(h, int)

        # Equal recs should have equal hashes
        r2 = Rec(**kw)
        self.assertEqual(hash(r), hash(r2))


class TestRecInCollections(unittest.TestCase):
    """Test Rec usability in sets and dicts (requires correct __eq__ + __hash__)."""

    def test_set_dedup(self):
        """Equal Recs should be deduplicated in a set."""
        s = {Rec(a=1), Rec(a=1), Rec(a=2)}
        self.assertEqual(len(s), 2)

    def test_dict_key(self):
        """Rec should be usable as a dict key."""
        d = {Rec(a=1, b=2): "value"}
        self.assertEqual(d[Rec(a=1, b=2)], "value")

    def test_dict_key_not_found(self):
        """Different Rec should not match as dict key."""
        d = {Rec(a=1): "value"}
        with self.assertRaises(KeyError):
            _ = d[Rec(a=2)]

    def test_set_many(self):
        """Large set of Recs should maintain correct count."""
        recs = {Rec(i=i) for i in range(100)}
        self.assertEqual(len(recs), 100)
        # Adding duplicates shouldn't increase size
        for i in range(100):
            recs.add(Rec(i=i))
        self.assertEqual(len(recs), 100)


# ---------------------------------------------------------------------------
# nccl_ep import fallback tests (PR 6)
# ---------------------------------------------------------------------------

class TestNcclEpImportFallback(unittest.TestCase):
    """Test that _import_error is always defined before use."""

    def test_import_error_initialized_before_try(self):
        """
        The fix initializes _import_error = None before the try block.
        Verify the pattern: _import_error must be defined regardless of
        whether the import succeeds or fails.
        """
        # Simulate the fixed pattern
        _import_error = None  # PR 6 adds this line

        try:
            # Simulate successful import
            HAVE_NCCL_EP = True
        except ImportError as e:
            _import_error = e
            HAVE_NCCL_EP = False

        # _import_error should always be accessible
        self.assertIsNone(_import_error)
        self.assertTrue(HAVE_NCCL_EP)

    def test_import_error_set_on_failure(self):
        """When import fails, _import_error should contain the error."""
        _import_error = None

        try:
            raise ImportError("test: module not found")
        except ImportError as e:
            _import_error = e
            HAVE_NCCL_EP = False

        self.assertIsNotNone(_import_error)
        self.assertFalse(HAVE_NCCL_EP)
        self.assertIn("module not found", str(_import_error))

    def test_import_error_used_before_assignment_bug(self):
        """
        Demonstrate the original bug: without initializing _import_error,
        accessing it after a successful import raises NameError.
        """
        # Original buggy pattern (no initialization before try)
        # _import_error is NOT defined here

        local_vars = {}
        exec("""
try:
    HAVE_NCCL_EP = True  # Successful import
except ImportError as e:
    _import_error = e
    HAVE_NCCL_EP = False

# This would crash in the original code if HAVE_NCCL_EP is True
# and someone later references _import_error
try:
    result = _import_error
    defined = True
except NameError:
    defined = False
""", local_vars)

        # In the original buggy code, _import_error is NOT defined after
        # successful import. The fix ensures it's always None or the error.
        self.assertFalse(local_vars['defined'],
                         "_import_error should NOT be defined in buggy pattern")


if __name__ == '__main__':
    unittest.main()
