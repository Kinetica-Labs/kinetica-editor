"""The names' contract (docs/contracts/names.md) held against the operations the editor has: tests/data/ops-list.json,
the answer of ops_list recorded from the window by tools/record_window_transcript.py (whose --check keeps it current).

The registry only grows: every operation the editor has is in it, every name in it (but reserved) is still there,
names follow the grammar, and every argument word is a row of the dictionary in its type. A new operation or a new
word fails here until its row is written - the name is decided once, on purpose, and never changed after.

    cd tools && python3 -m unittest test_names
"""
import json
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC = os.path.join(ROOT, "docs", "contracts", "names.md")
OPS = os.path.join(ROOT, "tests", "data", "ops-list.json")
NAME = re.compile(r"[a-z][a-z0-9_]*\Z")
TIERS = ("stable", "testing", "obsolete", "reserved")


def section(text, number):
    """The text of «## <number>. ...» up to the next «## »."""
    found = re.search(r"^## " + re.escape(number) + r"\. .*?$(.*?)(?=^## |\Z)", text, re.M | re.S)
    if not found:
        raise AssertionError(f"{DOC} has no section «## {number}.»")
    return found.group(1)


def rows(text):
    """The cells of a table's rows that start with a word in backticks."""
    return [[cell.strip() for cell in line.strip().strip("|").split("|")] for line in text.splitlines() if line.startswith("| `")]


def kind(schema):
    """An argument's type as the dictionary writes it, from its JSON Schema."""
    simple = {"integer": "int", "number": "number", "boolean": "bool", "string": "str", "object": "{}"}
    t = schema.get("type")
    if t in simple:
        return simple[t]
    if t != "array":
        return "any"
    items, size = schema.get("items", {}), schema.get("minItems")
    if items.get("type") == "number" and size and size == schema.get("maxItems"):
        return f"[{size}]"
    return {"integer": "[int]", "string": "[str]", "array": "[[]]"}.get(items.get("type"), "[]")


class Names(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(DOC, encoding="utf-8") as f:
            text = f.read()
        cls.areas = {r[0].strip("`") for r in rows(section(text, "4"))}
        cls.verbs = set(re.findall(r"`([a-z]+)`", [p for p in section(text, "5").split("\n\n") if p.strip()][-1]))  # its last paragraph
        cls.words = {}
        for r in rows(section(text, "6")):
            cls.words.setdefault((r[0].strip("`"), r[1].strip("`")), []).append(r)
        cls.registry = {}
        for tier, names in re.findall(r"^### (\w+)\n(.*?)(?=^### |\Z)", section(text, "7"), re.M | re.S):
            for name in re.findall(r"`([a-z0-9_]+)`", names):
                cls.registry.setdefault(name, []).append(tier)
        with open(OPS, encoding="utf-8") as f:
            cls.ops = {o["name"]: o for o in json.load(f)["ops"]}

    def test_every_operation_is_in_the_registry(self):
        missing = sorted(set(self.ops) - {n for n, t in self.registry.items() if "reserved" not in t})
        self.assertEqual(missing, [], f"operations not in {DOC} section 7: add each under «### testing»")

    def test_no_registered_operation_is_gone(self):
        gone = sorted(n for n, t in self.registry.items() if "reserved" not in t and n not in self.ops)
        self.assertEqual(gone, [], "registered operations the editor no longer has: a name is never removed or renamed - "
                                   "an unreleased one moves to «### reserved»")
        back = sorted(n for n, t in self.registry.items() if "reserved" in t and n in self.ops)
        self.assertEqual(back, [], "reserved names are never taken again")

    def test_each_name_is_registered_once_in_a_known_tier(self):
        for name, tiers in self.registry.items():
            self.assertEqual(len(tiers), 1, f"{name} is in more than one tier: {tiers}")
            self.assertIn(tiers[0], TIERS, f"{name}: no tier «{tiers[0]}»")

    def test_names_follow_the_grammar(self):
        for name in self.ops:
            self.assertRegex(name, NAME)
            area = name.split("_")[0]
            self.assertIn(area, self.areas, f"{name}: area «{area}» is not in section 4")
            if self.registry.get(name) == ["testing"]:  # the grammar binds new names; released ones stay as they were
                verb = name.split("_")[1] if "_" in name else ""
                self.assertIn(verb, self.verbs, f"{name}: a new operation is <area>_<verb>[_<what>], «{verb}» is not in section 5")

    def test_every_argument_is_a_word_of_the_dictionary(self):
        unknown = sorted({f"{w} ({kind(s)}) in {n}" for n, o in self.ops.items() for w, s in o["schema"].get("properties", {}).items()
                          if (w, kind(s)) not in self.words})
        self.assertEqual(unknown, [], f"argument words not in {DOC} section 6: write the row - its meaning, unit, tier - first")

    def test_dictionary_rows_are_well_formed(self):
        words = {w for w, _ in self.words}
        for (word, typ), found in self.words.items():
            self.assertEqual(len(found), 1, f"«{word}» ({typ}) has {len(found)} rows: one meaning a type")
            row = found[0]
            self.assertRegex(word, NAME)
            self.assertEqual(len(row), 5, f"«{word}»: a row is word | type | meaning | tier | instead")
            self.assertIn(row[3], TIERS, f"«{word}» ({typ}): no tier «{row[3]}»")
            instead = row[4].strip("`")
            if row[3] == "obsolete":
                self.assertTrue(instead, f"«{word}» ({typ}) is obsolete: say what instead")
            if instead:
                self.assertIn(instead, words, f"«{word}»: «{instead}» instead - a word of the dictionary")


if __name__ == "__main__":
    unittest.main()
