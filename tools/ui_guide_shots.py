#!/usr/bin/env python3
"""The pictures of the window's guide (docs/interface.md, docs/interface.en.md): every function of the window done by
its operation in an editor that shows no window, and photographed - in Russian and in English - so the guide is
retaken by one command when the window changes::

    python3 tools/ui_guide_shots.py              # every picture, both languages, into docs/images/ui/<ru|en>/
    python3 tools/ui_guide_shots.py ru magnet    # one language, only the shots whose names start so

The editor is build/kinetica (another: KE_EDITOR). The client is tools/ke_editor.py; only the standard library - a
part of the window is cut out of its picture by `crop` below.
"""
import os
import struct
import subprocess
import sys
import tempfile
import time
import zlib

from ke_editor import EDITOR_DIR, Editor, reachable

BINARY = os.environ.get("KE_EDITOR", os.path.join(EDITOR_DIR, "build", "kinetica"))
PORT = 47833
WINDOW, VIEW, THIRD, HALF = 1200, 900, 560, 660  # widths, px: the whole window, the 3D view, one of three, one of two
SHOTS = []

# The window's parts in its own pixels (an unattended window is 1600 x 950): left, top, right, bottom.
TOOLBAR = (0, 0, 1600, 108)
TOOLS = (0, 108, 62, 340)
VIEWPORT = (62, 108, 1218, 685)
INSPECTOR = (1222, 108, 1600, 930)
BOTTOM = (62, 726, 1222, 930)

WORDS = {
    "ru": {"object": "Объект", "world": "Мир", "plugins_x": 374, "other": "en",
           "brick": "Кирпич", "ball": "Шар", "cart": "Тележка", "wall": "Стена", "base": "База", "cube": "Куб", "crate": "Ящик",
           "box": "куб", "sphere": "сфера", "cylinder": "цилиндр", "cone": "конус", "plane": "плоскость",
           "sun": "солнце", "lamp": "лампа", "spot": "прожектор", "camera": "камера с целью",
           "ghost": "призрак звена", "imu": "IMU", "sonar": "сонар", "beam": "луч сонара",
           "zone": "зона", "part": "деталь: внутри", "miss": "шар: мимо", "bin": "Корзина"},
    "en": {"object": "Object", "world": "World", "plugins_x": 320, "other": "ru",
           "brick": "Brick", "ball": "Ball", "cart": "Cart", "wall": "Wall", "base": "Base", "cube": "Cube", "crate": "Crate",
           "box": "box", "sphere": "sphere", "cylinder": "cylinder", "cone": "cone", "plane": "plane",
           "sun": "sun", "lamp": "lamp", "spot": "spot", "camera": "target camera",
           "ghost": "ghost of the link", "imu": "IMU", "sonar": "sonar", "beam": "sonar's beam",
           "zone": "zone", "part": "the part: inside", "miss": "the ball: missed", "bin": "Bin"},
}


def shot_of(function):
    SHOTS.append(function)
    return function


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def read_png(source, rows=None):
    """A PNG as the editor writes it - 8 bits a channel, RGB or RGBA, not interlaced - as (width, bytes a pixel, its
    rows): the first `rows` of them, or all."""
    data = open(source, "rb").read()
    at, header, packed = 8, None, b""
    while at < len(data):
        size, kind = struct.unpack(">I", data[at:at + 4])[0], data[at + 4:at + 8]
        if kind == b"IHDR":
            header = data[at + 8:at + 8 + size]
        elif kind == b"IDAT":
            packed += data[at + 8:at + 8 + size]
        at += 12 + size
    width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", header)
    if depth != 8 or colour not in (2, 6) or interlace:
        raise ValueError("not a PNG this can read: " + source)
    step = 3 if colour == 2 else 4
    stride = width * step
    raw, out, before = zlib.decompress(packed), [], bytearray(stride)
    for y in range(min(rows or height, height)):  # a row is written as its difference from the one above
        kind, row = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if kind == 1:
            for i in range(step, stride):
                row[i] = (row[i] + row[i - step]) & 255
        elif kind == 2:
            for i in range(stride):
                row[i] = (row[i] + before[i]) & 255
        elif kind == 3:
            for i in range(stride):
                row[i] = (row[i] + ((row[i - step] if i >= step else 0) + before[i]) // 2) & 255
        elif kind == 4:
            for i in range(stride):
                a, b, c = (row[i - step] if i >= step else 0), before[i], (before[i - step] if i >= step else 0)
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                row[i] = (row[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        before = row
        out.append(row)
    return width, step, out


def packed_rows(rows):
    return zlib.compress(b"".join(b"\0" + bytes(row) for row in rows), 9)


def write_png(target, width, step, rows):
    head = struct.pack(">IIBBBBB", width, len(rows), 8, 2 if step == 3 else 6, 0, 0, 0)
    with open(target, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", head) + chunk(b"IDAT", packed_rows(rows)) + chunk(b"IEND", b""))


def crop(source, target, box):
    """A rectangle (left, top, right, bottom) of a picture."""
    width, step, rows = read_png(source, box[3])
    left, right = max(0, box[0]), min(box[2], width)
    write_png(target, right - left, step, [row[left * step:right * step] for row in rows[max(0, box[1]):]])


# The digits of a mark, 5 x 7 dots each.
DIGITS = {"1": "00100 01100 00100 00100 00100 00100 01110", "2": "01110 10001 00001 00010 00100 01000 11111",
          "3": "11110 00001 00001 01110 00001 00001 11110", "4": "00010 00110 01010 10010 11111 00010 00010",
          "5": "11111 10000 11110 00001 00001 10001 01110", "6": "00110 01000 10000 11110 10001 10001 01110",
          "7": "11111 00001 00010 00100 01000 01000 01000", "8": "01110 10001 10001 01110 10001 10001 01110",
          "9": "01110 10001 10001 01111 00001 00010 01100"}
MARK = (242, 163, 58)  # the orange the editor outlines a selection with


def mark(path, marks):
    """Frames on a picture, each with its number in a corner: where to look and in what order - a reader's eye finds
    a control faster by a frame on it than by a sentence under the picture. marks: (left, top, right, bottom, number)."""
    width, step, rows = read_png(path)
    def fill(left, top, right, bottom, colour):
        for y in range(max(0, top), min(bottom, len(rows))):
            for x in range(max(0, left), min(right, width)):
                rows[y][x * step:x * step + 3] = bytes(colour)
    for left, top, right, bottom, number in marks:
        for l, t, r, b in ((left, top, right, top + 3), (left, bottom - 3, right, bottom), (left, top, left + 3, bottom), (right - 3, top, right, bottom)):
            fill(l, t, r, b, MARK)
        if not number:
            continue
        x0, y0 = right - 11, top - 11  # the badge sits on the frame's corner, clear of the words the frame begins with
        fill(x0, y0, x0 + 22, y0 + 22, MARK)
        for j, line in enumerate(DIGITS[str(number)].split()):
            for i, dot in enumerate(line):
                if dot == "1":
                    fill(x0 + 6 + i * 2, y0 + 4 + j * 2, x0 + 8 + i * 2, y0 + 6 + j * 2, (26, 18, 5))
    write_png(path, width, step, rows)


def animate(frames, target, seconds):
    """Pictures of one size as one that moves (an animated PNG: browsers and GitHub play it in an <img>), each shown
    for `seconds`, the last one four times as long."""
    pictures = [read_png(f) for f in frames]
    width, step, first = pictures[0]
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, len(first), 8, 2 if step == 3 else 6, 0, 0, 0))
    out += chunk(b"acTL", struct.pack(">II", len(pictures), 0))
    sequence = 0
    for k, (_, _, rows) in enumerate(pictures):
        ms = int(seconds * 1000 * (4 if k == len(pictures) - 1 else 1))
        out += chunk(b"fcTL", struct.pack(">IIIIIHHBB", sequence, width, len(rows), 0, 0, ms, 1000, 0, 0))
        sequence += 1
        if k == 0:
            out += chunk(b"IDAT", packed_rows(rows))
        else:
            out += chunk(b"fdAT", struct.pack(">I", sequence) + packed_rows(rows))
            sequence += 1
    with open(target, "wb") as f:
        f.write(out + chunk(b"IEND", b""))


class Studio:
    """A hidden editor and its camera. After a call that changes something the window says for 4 s that an agent
    drives it and hides the gizmo from the hand: a picture waits that out."""

    def __init__(self, language):
        self.language, self.words = language, WORDS[language]
        self.out = os.path.join(EDITOR_DIR, "docs", "images", "ui", language)
        os.makedirs(self.out, exist_ok=True)
        self.whole = tempfile.NamedTemporaryFile(suffix=".png", delete=False).name
        self.journal = tempfile.NamedTemporaryFile(suffix=".jsonl", delete=False).name  # not the person's own journal
        self.process = subprocess.Popen([BINARY, "--agent-eval", "--api", str(PORT), "--language", language], cwd=EDITOR_DIR,
                                        env=dict(os.environ, KE_JOURNAL=self.journal), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.time() + 60
        while not reachable(PORT):
            if self.process.poll() is not None or time.time() > deadline:
                raise RuntimeError("the editor did not start: " + BINARY)
            time.sleep(0.2)
        time.sleep(1.5)
        self.ed = Editor(port=PORT)

    def close(self):
        try:
            self.ed.app_quit()
        except (OSError, ConnectionError):
            pass
        self.process.wait(timeout=20)
        os.unlink(self.journal)
        os.unlink(self.whole)

    def ids(self):
        return {o["name"]: o["id"] for o in self.ed.scene_list()["objects"]}

    def new(self, eye, target):
        """An empty scene, nothing drawn over it, the bottom area on the Sequencer."""
        self.ed.scene_new()
        self.ed.view_draw(clear=True)
        self.ed.gizmo_set(tool="select")
        self.ed.ui_trigger(text="actionSequencer")
        self.ed.view_look(eye=eye, target=target)

    def open(self, name, eye, target):
        self.ed.scene_open(file=os.path.join(EDITOR_DIR, "examples", name))
        self.ed.view_draw(clear=True)
        self.ed.selection_set(ids=[])
        self.ed.ui_trigger(text="actionSequencer")
        self.ed.view_look(eye=eye, target=target)

    def add(self, shape, word, **more):
        return self.ed.scene_add(shape=shape, name=self.words[word], **more)["id"]

    def tab(self, word):
        """A tab of the inspector: «Объект», «Мир» («Сцена» is a menu's name too - the tab a fresh editor shows)."""
        self.ed.ui_trigger(text=self.words[word])

    def tree(self):
        """The inspector's first tab - the scene's tree - by a click on it: its text is a menu's name too."""
        self.ed.ui_pointer(action="click", x=INSPECTOR[0] + 37, y=INSPECTOR[1] + 36, space="window")

    def label(self, word, at, size=15):
        self.ed.view_draw(key="guide/" + word, text=self.words[word], at=at, colour=[1, 0.85, 0.3], size=size)

    def check(self, action, on):
        """A switch of the window - a menu's tick - put on or off, by its action's name."""
        if next(a["checked"] for a in self.ed.ui_actions()["actions"] if a.get("name") == action) != on:
            self.ed.ui_trigger(text=action)

    def take(self, name, width, **what):
        time.sleep(4.4)
        answer = self.ed.view_screenshot(file=os.path.join(self.out, name + ".png"), width=width, **what)
        print(f"  {self.language}/{name}.png  {answer['width']}x{answer['height']}")

    def window(self, name, marks=()):
        """The whole window; marks - frames with numbers on it, in the window's own pixels."""
        self.take(name, WINDOW, window=True)
        if marks:
            k = WINDOW / 1600
            mark(os.path.join(self.out, name + ".png"), [(int(l * k), int(t * k), int(r * k), int(b * k), n) for l, t, r, b, n in marks])

    def view(self, name, width=VIEW):
        self.take(name, width)

    def part(self, name, box, screen=False, marks=()):
        """A part of the window in its own pixels: sharp letters, a small file. screen: with an open menu over it.
        marks - frames with numbers, in the window's pixels as well."""
        time.sleep(4.4)
        self.ed.view_screenshot(file=self.whole, **({"screen": True} if screen else {"window": True}))
        crop(self.whole, os.path.join(self.out, name + ".png"), box)
        if marks:
            mark(os.path.join(self.out, name + ".png"), [(l - box[0], t - box[1], r - box[0], b - box[1], n) for l, t, r, b, n in marks])
        print(f"  {self.language}/{name}.png  {box[2] - box[0]}x{box[3] - box[1]}")

    def wall(self):
        """The first minutes' scene: a brick made a wall of twenty, and a heavy ball thrown at it."""
        brick = self.add("box", "brick", position=[-0.5, 0.075, 0], size=[0.24, 0.15, 0.2], role="rigid")
        row = self.ed.scene_array(ids=[brick], count=5, step=[0.25, 0, 0])["id"]
        self.ed.scene_set(id=row, line="pattern", values=["grid"])
        self.ed.scene_set(id=row, line="pattern", key="count", values=[5, 4, 1])
        self.ed.scene_set(id=row, line="pattern", key="step", values=[0.25, 0.155, 0.3])
        ball = self.add("sphere", "ball", position=[0, 0.4, 1.7], size=[0.3, 0.3, 0.3], role="rigid")
        self.ed.scene_set(id=ball, line="rigid", key="density", values=[4000])
        self.ed.scene_set(id=ball, line="rigid", key="velocity", values=[0, 1.5, -7])
        return brick, row, ball


EYE, TARGET = [2.3, 1.4, 2.7], [0, 0.35, 0.4]
# A body's card in the inspector, in the window's pixels: the rows the first lesson sends a reader to.
RIGID, PLACE, SIZE = (1230, 234, 1412, 304), (1226, 376, 1596, 404), (1226, 463, 1596, 491)


@shot_of
def first(s):
    """The first five minutes: a brick, a wall of them, a ball, «Пуск» and «Стоп»."""
    s.new(EYE, TARGET)
    s.tree()
    s.window("first-1-empty")
    s.ed.ui_pointer(action="click", x=46, y=62, space="window")
    s.part("first-2-menu", (0, 18, 520, 262), screen=True, marks=[(8, 26, 88, 104, 1), (78, 109, 200, 133, 2)])
    s.ed.ui_key(key="Escape")
    brick = s.add("box", "brick", position=[-0.5, 0.075, 0], size=[0.24, 0.15, 0.2], role="rigid")
    s.ed.selection_set(ids=[brick])
    s.ed.gizmo_set(tool="move")
    s.tab("object")
    s.window("first-3-brick", [SIZE + (1,), PLACE + (2,), RIGID + (3,)])
    s.new(EYE, TARGET)
    brick, row, ball = s.wall()
    s.ed.selection_set(ids=[row])
    s.tab("object")
    s.window("first-4-wall", [(1306, 416, 1362, 479, 1), (1226, 482, 1596, 508, 2), (1226, 510, 1596, 536, 3)])
    s.ed.selection_set(ids=[ball])
    s.ed.gizmo_set(tool="move")
    s.window("first-5-ball", [RIGID + (1,), PLACE + (2,), (1226, 885, 1596, 911, 3)])
    s.part("first-5-play", TOOLBAR, marks=[(488, 26, 562, 104, 0)])
    s.ed.gizmo_set(tool="select")
    s.ed.selection_set(ids=[])
    s.ed.sim_play()
    s.ed.sim_pause()
    s.ed.sim_step(frames=16)
    s.view("first-6-hit", HALF)
    s.ed.sim_step(frames=40)
    s.view("first-6-after", HALF)
    s.window("first-6-pause")
    s.part("first-6-stop", TOOLBAR, marks=[(768, 26, 838, 104, 0)])
    s.ed.sim_stop()
    s.view("first-7-stop", HALF)
    # What the lesson ends with, moving: the same for both languages - a clean picture has no words on it.
    s.new(EYE, TARGET)
    s.wall()
    s.ed.selection_set(ids=[])
    s.ed.sim_play()
    s.ed.sim_pause()
    frames = []
    for k in range(24):
        frames.append(os.path.join(tempfile.gettempdir(), f"kinetica-guide-{k}.png"))
        s.ed.view_screenshot(file=frames[-1], width=480, clean=True)
        s.ed.sim_step(frames=3)
    s.ed.sim_stop()
    animate(frames, os.path.join(EDITOR_DIR, "docs", "images", "ui", "first-result.png"), 0.09)
    for f in frames:
        os.unlink(f)
    print("  first-result.png  (it moves)")


# The window's own icons, cut out of it - what stands beside a button's name in the guide's text, so a reader knows the
# button by sight. No words on them: one set for both languages. Centres in the window's pixels.
BAR_ICONS = {"shape": 46, "light": 140, "camera": 233, "magnet": 326, "sensor": 419, "play": 524, "record": 617, "undo": 908, "redo": 1003}
RUN_ICONS = {"pause": 710, "stop": 803}       # these two are alive only while the scene plays
TOOL_ICONS = {"select": 132, "move": 177, "rotate": 222, "scale": 267}
ROLE_ICONS = {"rigid": 1321, "collider": 1491}


@shot_of
def icons(s):
    """The icons of the toolbar, the tools and the roles: docs/images/ui/icons/."""
    out = os.path.join(EDITOR_DIR, "docs", "images", "ui", "icons")
    os.makedirs(out, exist_ok=True)
    def cut(names, box):
        time.sleep(4.4)
        s.ed.view_screenshot(file=s.whole, window=True)
        for name, at in names.items():
            crop(s.whole, os.path.join(out, name + ".png"), box(at))
    bar = lambda x: (x - 28, 28, x + 28, 84)
    tool = lambda y: (10, y - 20, 50, y + 20)
    s.new(EYE, TARGET)
    body = s.add("box", "brick", position=[0, 0.25, 0], size=[0.5, 0.5, 0.5], role="rigid")
    s.ed.selection_set(ids=[body])
    s.tab("object")
    s.ed.gizmo_set(tool="rotate")  # the pressed tool has a blue ground: each is cut while another is pressed
    cut({k: v for k, v in TOOL_ICONS.items() if k != "rotate"}, tool)
    s.ed.gizmo_set(tool="select")
    cut({"rotate": TOOL_ICONS["rotate"], **BAR_ICONS}, lambda at: tool(at) if at == TOOL_ICONS["rotate"] else bar(at))
    cut(ROLE_ICONS, lambda x: (x - 20, 242, x + 20, 278))
    s.ed.sim_play()
    cut(RUN_ICONS, bar)
    s.ed.sim_stop()
    print("  icons/ ", len(os.listdir(out)), "files")


@shot_of
def map(s):
    """The window's parts: the whole of it, then the toolbar, the tools, the three tabs of the inspector, the bottom."""
    s.open("ur5e.kscene", [1.5, 1.15, 1.5], [0.15, 0.4, -0.1])
    s.tree()
    s.window("window")
    s.part("map-toolbar", TOOLBAR)
    s.part("map-scene", INSPECTOR)
    s.part("map-bottom", BOTTOM)
    s.ed.selection_set(ids=[s.ids()["Кубик жёлтый"]])
    s.ed.gizmo_set(tool="move")
    s.tab("object")
    s.part("map-tools", TOOLS)
    s.part("map-object", INSPECTOR)
    s.tab("world")
    s.part("map-world", INSPECTOR)
    s.ed.gizmo_set(tool="select")


@shot_of
def shapes(s):
    """«Фигура»: the five shapes."""
    s.new([0.4, 1.5, 2.9], [0, 0.2, -0.1])
    for k, shape in enumerate(("box", "sphere", "cylinder", "cone")):
        s.ed.scene_add(shape=shape, position=[-1.05 + 0.7 * k, 0.25, 0], size=[0.5, 0.5, 0.5])
        s.label(shape, [-1.05 + 0.7 * k, 0.72, 0])
    s.ed.scene_add(shape="plane", position=[0, 0.02, -0.9], size=[2.6, 0.02, 0.6])
    s.ed.selection_set(ids=[])
    s.view("shapes")


@shot_of
def gizmo(s):
    """The tool shelf: move, rotate, scale."""
    s.new([1.3, 1.0, 1.5], [0, 0.3, 0])
    box = s.ed.scene_add(shape="box", position=[0, 0.25, 0], size=[0.5, 0.5, 0.5])["id"]
    s.ed.selection_set(ids=[box])
    for tool in ("move", "rotate", "scale"):
        s.ed.gizmo_set(tool=tool)
        s.view("gizmo-" + tool, THIRD)


@shot_of
def lights(s):
    """«Свет» and «Камера»: the sun, a lamp, a spot and a camera with its target."""
    s.new([2.4, 1.9, 2.6], [0, 0.5, 0.2])
    ball = s.ed.scene_add(shape="sphere", position=[0, 0.3, 0], size=[0.6, 0.6, 0.6])["id"]
    s.ed.scene_add_light(kind="sun", at=[0, 0, 0])
    s.ed.scene_add_light(kind="point", position=[-0.9, 0.9, 0.3])
    s.ed.scene_add_light(kind="spot", position=[0.9, 1.2, 0.3], target=[0, 0.3, 0])
    s.ed.scene_add_camera(eye=[0.2, 0.7, 1.5], kind="target", at=ball)
    s.label("lamp", [-0.85, 1.1, 0.3])
    s.label("spot", [0.95, 1.4, 0.3])
    s.label("camera", [0.3, 0.9, 1.5])
    s.ed.selection_set(ids=[])
    s.view("lights")


@shot_of
def zone(s):
    """A zone - a collider that is a trigger, made see-through: the part that fell into it, the ball that missed."""
    s.new([1.5, 1.2, 2.3], [0.35, 0.3, 0])
    bin = s.add("box", "bin", position=[0, 0.3, 0], size=[0.6, 0.6, 0.6], role="collider")
    s.ed.scene_collider(id=bin, trigger=True)
    s.ed.scene_set(id=bin, line="shape", key="opacity", values=[0.3])
    s.ed.scene_add(shape="sphere", position=[0, 1.0, 0], size=[0.2, 0.2, 0.2], role="rigid")
    s.ed.scene_add(shape="sphere", position=[0.9, 1.0, 0], size=[0.2, 0.2, 0.2], role="rigid")
    s.ed.selection_set(ids=[])
    s.ed.sim_play()
    s.ed.sim_pause()
    s.ed.sim_step(frames=90)
    s.label("zone", [-0.28, 0.75, 0.3])
    s.label("part", [0.02, 0.38, 0.1])
    s.label("miss", [0.85, 0.42, 0.1])
    s.view("zone")
    s.ed.sim_stop()


@shot_of
def magnet(s):
    """«Магнит»: the ghost of a link over a face, the menu of ready links, and the link a click put there."""
    s.new([1.5, 1.3, 1.9], [0, 0.5, 0])
    base = s.add("box", "base", position=[0, 0.25, 0], size=[0.5, 0.5, 0.5], role="rigid", fixed=True)
    s.ed.selection_set(ids=[base])
    s.ed.ui_trigger(text="magnetTool")
    at = s.ed.view_project(point=[0, 0.5, 0])
    s.ed.ui_pointer(action="move", x=at["x"], y=at["y"])
    s.label("ghost", [0.3, 0.85, 0], 22)
    s.view("magnet-ghost", THIRD)
    s.ed.view_draw(clear=True)
    s.ed.ui_pointer(action="press", x=at["x"], y=at["y"])
    s.ed.ui_pointer(action="move", x=at["x"] + 30, y=at["y"] - 10)
    centre = (VIEWPORT[0] + int(at["x"]), VIEWPORT[1] + int(at["y"]))
    s.part("magnet-menu", (centre[0] - 300, centre[1] - 190, centre[0] + 300, centre[1] + 190), screen=True)
    s.ed.ui_pointer(action="release", x=at["x"] + 30, y=at["y"] - 10)
    s.ed.ui_key(key="Escape")
    s.new([1.5, 1.3, 1.9], [0, 0.5, 0])
    base = s.add("box", "base", position=[0, 0.25, 0], size=[0.5, 0.5, 0.5], role="rigid", fixed=True)
    s.ed.selection_set(ids=[base])
    s.ed.ui_trigger(text="magnetTool")
    s.ed.ui_pointer(action="move", x=at["x"], y=at["y"])
    s.ed.ui_pointer(action="click", x=at["x"], y=at["y"])
    s.ed.ui_pointer(action="move", x=40, y=40)
    s.view("magnet-link", THIRD)
    s.ed.ui_key(key="Escape")


@shot_of
def joint(s):
    """A joint's card: the UR5e's shoulder hinge - its axis, limit and drive."""
    s.open("ur5e.kscene", [1.1, 0.9, 1.1], [0.15, 0.45, -0.1])
    s.ed.selection_set(ids=[s.ids()["Шарнир 5"]])
    s.tab("object")
    s.window("joint")


@shot_of
def sensors(s):
    """«Датчик»: its menu; an IMU and a sonar on a cart, the sonar's datasheet in the inspector."""
    s.new([-0.9, 1.1, 1.9], [0.5, 0.25, 0])
    cart = s.add("box", "cart", position=[0, 0.2, 0], size=[0.6, 0.2, 0.4], role="rigid")
    s.add("box", "wall", position=[1.5, 0.4, 0], size=[0.1, 0.8, 1.2], role="collider")
    s.ed.selection_set(ids=[cart])
    s.ed.ui_pointer(action="click", x=418, y=62, space="window")
    s.part("sensor-menu", (300, 18, 820, 262), screen=True)
    s.ed.ui_key(key="Escape")
    s.ed.assembly_sensor(target=cart, kind="imu", face="+y")
    sonar = s.ed.assembly_sensor(target=cart, kind="sonar", face="+x")["id"]
    s.label("imu", [0, 0.48, 0])
    s.label("beam", [0.9, 0.62, 0])
    s.ed.selection_set(ids=[sonar])
    s.tab("object")
    s.window("sensors")


@shot_of
def play(s):
    """In play: the graphs, the laboratory and the performance plate."""
    s.new(EYE, TARGET)
    brick, row, ball = s.wall()
    s.ed.sim_play()
    s.ed.sim_pause()
    s.ed.sim_step(frames=30)
    s.ed.selection_set(ids=[ball])
    s.ed.ui_trigger(text="actionGraphs")
    s.ed.sim_step(frames=40)
    s.window("graphs")
    s.ed.ui_trigger(text="actionGraphs")
    s.ed.ui_trigger(text="actionSequencer")
    s.ed.selection_set(ids=[])
    s.ed.ui_trigger(text="actionLaboratory")
    s.ed.sim_step(frames=2)
    s.window("laboratory")
    s.ed.ui_trigger(text="actionLaboratory")
    s.check("actionColliders", True)
    s.ed.view_hud(on=True)
    s.ed.sim_play()  # the plate counts frames of a scene that runs
    s.view("hud")
    s.ed.view_hud(on=False)
    s.ed.sim_stop()


@shot_of
def sequencer(s):
    """«Секвенсор»: three keys of a cube and the playhead between them."""
    s.new([0.6, 1.4, 2.6], [0, 0.4, 0])
    box = s.add("box", "cube", position=[-0.8, 0.25, 0], size=[0.4, 0.4, 0.4])
    s.ed.sequence_key(ids=[box], time=0)
    s.ed.scene_move(ids=[box], by=[0.8, 0.6, 0])
    s.ed.sequence_key(ids=[box], time=1, interp="smooth")
    s.ed.scene_move(ids=[box], by=[0.8, -0.6, 0])
    s.ed.sequence_key(ids=[box], time=2)
    s.ed.sequence_seek(time=0.6)
    s.ed.selection_set(ids=[box])
    s.window("sequencer")
    s.part("sequencer-panel", BOTTOM)


@shot_of
def console(s):
    """«Консоль API»: every action of the window written as a line of Python."""
    s.new([1.6, 1.2, 1.9], [0.3, 0.3, 0])
    s.ed.ui_trigger(text="actionApiConsole")
    box = s.ed.scene_add(shape="box", position=[0, 0.25, 0], size=[0.5, 0.5, 0.5], role="rigid")["id"]
    s.ed.scene_move(ids=[box], by=[0.4, 0, 0])
    s.ed.scene_turn(ids=[box], axis="y", degrees=30)
    s.ed.scene_duplicate(ids=[box])
    s.part("console", BOTTOM)


@shot_of
def search(s):
    """«Найти команду…»: every action of the window by its name."""
    s.open("ur5e.kscene", [1.5, 1.15, 1.5], [0.15, 0.4, -0.1])
    s.ed.ui_trigger(text="commandSearch")
    s.take("search", WINDOW, screen=True)
    s.ed.ui_key(key="Escape")


@shot_of
def script(s):
    """«Выполнить скрипт…» - a pyramid of cubes beside the arm - and the menu «Плагины»."""
    s.open("ur5e.kscene", [1.25, 0.75, 0.75], [0.3, 0.2, -0.25])
    s.ed.script_run(file=os.path.join(EDITOR_DIR, "examples", "scripts", "ur5e_cubes.py"))
    s.ed.selection_set(ids=[])
    s.ed.view_look(eye=[1.25, 0.75, 0.75], target=[0.3, 0.2, -0.25])  # the script opened the scene itself: the view went back
    s.view("script")
    x = s.words["plugins_x"]
    s.ed.ui_pointer(action="click", x=x, y=8, space="window")
    s.part("plugins-menu", (x - 60, 0, x + 460, 200), screen=True)
    s.ed.ui_key(key="Escape")


@shot_of
def language(s):
    """The window as it is in this language - the picture of the other language's guide."""
    s.open("ur5e.kscene", [1.5, 1.15, 1.5], [0.15, 0.4, -0.1])
    s.ed.selection_set(ids=[s.ids()["Кубик жёлтый"]])
    s.tab("object")
    time.sleep(4.4)
    other = os.path.join(EDITOR_DIR, "docs", "images", "ui", s.words["other"])
    os.makedirs(other, exist_ok=True)
    s.ed.view_screenshot(file=os.path.join(other, "language.png"), width=WINDOW, window=True)
    print(f"  {s.words['other']}/language.png")


def gallery(language):
    """«Примеры…»: the gallery draws itself (--gallery-shot)."""
    target = os.path.join(EDITOR_DIR, "docs", "images", "ui", language, "gallery.png")
    subprocess.run([BINARY, "--gallery-shot", target, "--language", language], cwd=EDITOR_DIR, check=True, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
    print(f"  {language}/gallery.png")


def main(names):
    languages = [n for n in names if n in WORDS] or list(WORDS)
    names = [n for n in names if n not in WORDS]
    wanted = lambda f: not names or any(f.__name__.startswith(n) for n in names)
    for language in languages:
        inside = [f for f in SHOTS if wanted(f)]
        if inside:
            s = Studio(language)
            try:
                for f in inside:
                    print(f.__name__ + ": " + f.__doc__.splitlines()[0])
                    f(s)
            finally:
                s.close()
        if wanted(gallery):
            gallery(language)


if __name__ == "__main__":
    main(sys.argv[1:])
