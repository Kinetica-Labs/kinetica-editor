"""The project's order as machine checks (tools/project_map.py): no include reaches up a layer of src/, no file over its
budget, no file without a reader - kept at zero; the rules themselves see what they are for, on made-up files; the page
holds the map."""
import contextlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import project_map  # noqa: E402


class ProjectMap(unittest.TestCase):
    def test_the_project_keeps_its_order(self):
        rules = project_map.build()["rules"]
        self.assertEqual(rules["layers"], [], "an include reaches up a layer of src/ (CLAUDE.md, «Устройство»)")
        self.assertEqual(rules["budgets"], [], "a file over its budget: split it, or trim what is read every session")
        self.assertEqual(rules["readers"], [], "a file no one reads: delete it, or name it where it is used")

    def test_the_rules_see_what_they_are_for(self):
        texts = {"src/scene/A.h": '#include "math/Real.h"\n#include "sim/World.h"\n', "src/sim/B.cpp": '#include "scene/A.h"\n',
                 "tools/use.py": "import helper\n", "tools/helper.py": "x = 1\n", "docs/lost.md": "nobody\n",
                 "src/core/Big.cpp": "\n" * (project_map.BUDGET_LINES + 1)}
        self.assertEqual(project_map.layer_breaks(texts), [("src/scene/A.h", "sim/World.h")])
        self.assertEqual([b[0] for b in project_map.budget_breaks(texts)], ["src/core/Big.cpp"])
        found = project_map.readers(sorted(texts), texts)
        self.assertEqual(found.get("tools/helper.py"), "tools/use.py")  # a module, by its import
        self.assertNotIn("docs/lost.md", found)

    def test_the_page_holds_the_map(self):
        with tempfile.TemporaryDirectory() as folder, contextlib.redirect_stdout(io.StringIO()):
            page = os.path.join(folder, "map.html")
            self.assertEqual(project_map.main(["--html", page]), 0)
            with open(page, encoding="utf-8") as f:
                text = f.read()
        self.assertNotIn("__DATA__", text)
        self.assertIn('"growth":[{"d":', text)
        self.assertNotIn("</script>", text.split('id="data">', 1)[1].split("</script>", 1)[0])  # the data cannot end its script


if __name__ == "__main__":
    unittest.main()
