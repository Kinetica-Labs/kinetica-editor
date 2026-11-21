#!/usr/bin/env python3
"""The guide as a site of plain files, ready for any static hosting (GitHub Pages): docs/site/guide.html made a whole
page, the guide's Markdown it reads and the pictures beside it::

    python3 tools/build_site.py [folder]     # build/site by default

docs/site/guide.html is the page's content alone; here it gets the document
around it. .nojekyll tells GitHub Pages to serve the files as they are - without it the Markdown the page reads would
be turned into pages of its own.
"""
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")
HEAD = """<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<style>body { margin: 0; } img { max-width: 100%; } [hidden] { display: none !important; }</style>
"""


def build(out):
    page = open(os.path.join(DOCS, "site", "guide.html"), encoding="utf-8").read()
    head, mark = page.split("<header", 1)[0], "<header"
    body = mark + page.split(mark, 1)[1]
    os.makedirs(out, exist_ok=True)  # the folder may be a repository of its own: only what is written here is replaced
    shutil.rmtree(os.path.join(out, "images", "ui"), ignore_errors=True)
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write(HEAD + head + "</head>\n<body>\n" + body + "</body>\n</html>\n")
    for name in ("interface.md", "interface.en.md"):
        shutil.copy(os.path.join(DOCS, name), out)
    shutil.copytree(os.path.join(DOCS, "images", "ui"), os.path.join(out, "images", "ui"))
    open(os.path.join(out, ".nojekyll"), "w").close()
    walk = [(d, names) for d, _, names in os.walk(out) if os.sep + ".git" not in d + os.sep]
    files = sum(len(names) for _, names in walk)
    size = sum(os.path.getsize(os.path.join(d, n)) for d, names in walk for n in names)
    print(f"{out}: {files} files, {size / 1e6:.1f} MB")


if __name__ == "__main__":
    build(os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, "build", "site"))
