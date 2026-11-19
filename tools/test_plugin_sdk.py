"""The Python side of plugins (python/kinetica/plugin.py), without an editor: a command's fields become the schema the
editor's form and MCP read (type, default, unit, range, choices, the label's words, in the function's order); a plain
default is a field too; an error points at the author's file and line, not at the SDK's."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "python"))
from kinetica import plugin  # noqa: E402
from kinetica.plugin import command, field  # noqa: E402


@command(label="Ряд", key="Ctrl+Shift+R")
def row(scene, count: int = field(5, range=(1, 30), label="Сколько"), gap: float = field(0.1, unit="м"),
        solid: bool = True, colour: str = field("red", choices=("red", "blue"))):
    """A row."""
    return {"made": count}


class PluginSdk(unittest.TestCase):
    def test_command_registered(self):
        self.assertIn("row", plugin._commands)
        self.assertEqual(plugin._commands["row"]["label"], "Ряд")
        self.assertEqual(plugin._commands["row"]["key"], "Ctrl+Shift+R")

    def test_fields_become_schema(self):
        args = plugin._args(row)
        self.assertEqual(list(args), ["count", "gap", "solid", "colour"])
        self.assertEqual(args["count"], {"type": "integer", "default": 5, "label": "Сколько", "minimum": 1, "maximum": 30})
        self.assertEqual(args["gap"]["unit"], "м")
        self.assertEqual(args["gap"]["type"], "number")
        self.assertEqual(args["solid"], {"type": "boolean", "default": True, "label": "solid"})
        self.assertEqual(args["colour"]["enum"], ["red", "blue"])

    def test_error_points_at_author(self):
        try:
            row(None, count="x") + 1  # a dict plus an int: the author's own line
        except TypeError as error:
            where = plugin._where(error)
        self.assertTrue(where.startswith("test_plugin_sdk.py:"), where)
        self.assertIn("TypeError", where)


if __name__ == "__main__":
    unittest.main()
