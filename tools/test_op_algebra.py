"""The laws of the editor's operations (tools/op_algebra.py) in kinetica-core: every edit runs on the fixture with the
arguments its schema and recipe give (a new operation without them is caught here), a sample of pairs keeps the laws -
inverse, iso, words - the check itself catches an undo that gives nothing back; the walks of the automaton cover every ordered triple of
actions, and two of them keep the laws."""
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

    def test_the_walks_cover_every_ordered_triple(self):
        walks = op_algebra.sequence_cover(9, seed=3)
        seen = {(a, b, c) for w in walks for i, a in enumerate(w) for j, b in enumerate(w[i + 1:], i + 1) for c in w[j + 1:]}
        self.assertEqual(len(seen), 9 * 8 * 7)
        self.assertGreaterEqual(len(walks), 6)  # a walk holds each set of three in one of its 6 orders
        self.assertLess(len(walks), 30)          # yet a handful of walks, not the 504 triples one by one

    def test_a_short_walk_keeps_the_laws(self):
        r = op_algebra.walk(limit=2)
        self.assertEqual(r["laws"], [])
        self.assertGreater(len(r["states"]), 3)  # the walk leaves the fixture's class


if __name__ == "__main__":
    unittest.main()
