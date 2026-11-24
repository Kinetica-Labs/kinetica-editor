"""The laws of the editor's operations (tools/op_algebra.py) in kinetica-core: every edit runs on the fixture with the
arguments its schema and recipe give (a new operation without them is caught here), a sample of pairs keeps the laws -
inverse, iso, words - and the check itself catches an undo that gives nothing back."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import op_algebra  # noqa: E402


class OpAlgebra(unittest.TestCase):
    def test_every_edit_runs_and_the_laws_hold(self):
        r = op_algebra.run(sample=40)
        refused = {n: s["error"] for n, s in r["singles"].items() if not s["ok"]}
        self.assertEqual(refused, {}, "an edit refused on the fixture: give it a RECIPE in tools/op_algebra.py")
        self.assertEqual(r["laws"], [])

    def test_a_broken_undo_is_caught(self):
        real = op_algebra.Bench.back
        calls = []

        def once_broken(bench):  # the first put-back gives nothing back: the inverse law must say so
            calls.append(1)
            return bench.text() == bench.base if len(calls) == 1 else real(bench)
        op_algebra.Bench.back = once_broken
        try:
            r = op_algebra.run(sample=1)
        finally:
            op_algebra.Bench.back = real
        self.assertEqual([l["law"] for l in r["laws"]][:1], ["inverse"])


if __name__ == "__main__":
    unittest.main()
