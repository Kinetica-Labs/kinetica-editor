#!/usr/bin/env python3
"""The window's icons beside the names of its buttons in the guide's text (docs/interface.md, docs/interface.en.md): a
reader knows a button by sight before reading its name. Run after the text was edited - it takes the icons it put
before off first, so it may be run any number of times::

    python3 tools/ui_guide_icons.py          # says how many icons the text has
    python3 tools/ui_guide_icons.py --write  # puts them

Not in headings, in a picture's caption or in a key's name. The icons themselves are cut out of the window by
tools/ui_guide_shots.py (docs/images/ui/icons/).
"""
import os
import re
import sys

DOCS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "docs")
RU = {"Фигура": "shape", "Свет": "light", "Камера": "camera", "Магнит": "magnet", "Датчик": "sensor", "Сила": "force", "Пуск": "play", "Запись": "record",
      "Пауза": "pause", "Стоп": "stop", "Отменить": "undo", "Повторить": "redo", "Твёрдое": "rigid", "Коллайдер": "collider"}
EN = {"Shape": "shape", "Light": "light", "Camera": "camera", "Magnet": "magnet", "Sensor": "sensor", "Force": "force", "Play": "play", "Record": "record",
      "Pause": "pause", "Stop": "stop", "Undo": "undo", "Redo": "redo", "Rigid Body": "rigid", "Collider": "collider"}
TOOLS = {"ru": {"Перемещение": "move", "Вращение": "rotate", "Масштаб": "scale"}, "en": {"Move": "move", "Rotate": "rotate", "Scale": "scale"}}
tag = lambda name: f'<img src="images/ui/icons/{name}.png" height="22" alt="">'
PUT = re.compile(r'<img src="images/ui/icons/[a-z]+\.png" height="22" alt=""> ')
def decorate(text, lang):
    out, n = [], 0
    for line in PUT.sub("", text).split("\n"):
        skip = line.startswith(("#", "<", "```", "    ")) or "режим —" in line or "the mode -" in line or "написано" in line
        if not skip:
            parts = re.split(r'(!\[[^\]]*\]\([^)]*\)|`[^`]*`|<img[^>]*>)', line)  # pictures, keys and icons already there stay as they are
            for i in range(0, len(parts), 2):
                if lang == "ru":
                    for word, icon in RU.items():
                        parts[i], k = re.subn(r'(?<!> )«%s(?=[»\s→])' % word, tag(icon) + " «" + word, parts[i]); n += k
                        parts[i], k = re.subn(r'(?<=\| )%s(?= \|)' % word, tag(icon) + " " + word, parts[i]); n += k            # a table's cell that is the name
                        parts[i], k = re.subn(r'\*\*%s\*\*' % word, "**" + tag(icon) + " " + word + "**", parts[i]); n += k   # the name in bold
                else:
                    for word, icon in EN.items():
                        parts[i], k = re.subn(r'(?<!> )(?<![\w-])%s(?! Contacts| Script|/)(?![\w-])' % word, tag(icon) + " " + word, parts[i]); n += k
                if re.match(r'^\|[^|]*\|[^|]*\|[^|]*\|$', line) and all(w in line for w in TOOLS[lang]):  # the gizmo's table head
                    for word, icon in TOOLS[lang].items():
                        parts[i], k = re.subn(r'(?<!> )\b%s\b' % word, tag(icon) + " " + word, parts[i]); n += k
            line = "".join(parts)
        out.append(line)
    return "\n".join(out), n
for name, lang in (("interface.md", "ru"), ("interface.en.md", "en")):
    path = os.path.join(DOCS, name)
    new, n = decorate(open(path, encoding="utf-8").read(), lang)
    if "--write" in sys.argv:
        open(path, "w", encoding="utf-8").write(new)
    print(name, n, "icons")
