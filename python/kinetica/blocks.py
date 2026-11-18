"""The scene file's blocks, read: an object is a block of lines (``SceneGraph.cpp``)::

    entity "Шасси"
      object id 166 visible 1 locked 0
      shape box size 0.6 0.15 0.35 position 0 0.15 0 rotation 0 0 0 color 0.75 0.3 0.25
      rigid density 300 friction 0.5 restitution 0.2 fixed 0 velocity 0 0 0 spin 0 0 0
    end

A line is named by its first word (``shape``, ``rigid``, ``bridge``...) and holds ``key value...``
pairs; ``value(block, "shape", "position", 3)`` reads three numbers after ``position``.
"""
import shlex


def lines(block):
    """The block's lines as lists of words, quotes honoured; the first is the header (kind and name)."""
    return [shlex.split(text) for text in block.splitlines() if text.strip() and text.strip() != "end"]


def header(block):
    """The block's kind and name: ``("entity", "Шасси")``."""
    words = lines(block)[0]
    return words[0], (words[1] if len(words) > 1 else "")


def line(block, name):
    """The words of the first line named `name` (None: the block has none)."""
    for words in lines(block)[1:]:
        if words and words[0] == name:
            return words
    return None


def value(block, name, key, count=1):
    """The `count` values after `key` on line `name`, numbers as floats (None: not there)."""
    words = line(block, name)
    if not words or key not in words[1:]:
        return None
    at = words.index(key, 1) + 1
    out = []
    for word in words[at:at + count]:
        try:
            out.append(float(word))
        except ValueError:
            out.append(word)
    return out[0] if count == 1 else tuple(out)
