#!/usr/bin/env python3
"""Short films of the window for the README (docs/images/gif/<name>.gif): what a person does, shown rather than told.
An editor that shows no window is driven as a hand drives it - ui_pointer moves and clicks where ui_actions says a
button stands, the gizmo's arrow where view_project puts it - and the window's picture as a person sees it (the pointer
drawn in) is taken frame by frame; ffmpeg makes each GIF. Retaken by one command when the window changes::

    python3 tools/ui_guide_gifs.py            # every film
    python3 tools/ui_guide_gifs.py play       # the films whose names start so

Needs build/kinetica (another: KE_EDITOR) and ffmpeg; the client is tools/ke_editor.py, only the standard library.
"""
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time

from ke_editor import EDITOR_DIR, Editor, reachable

BINARY = os.environ.get("KE_EDITOR", os.path.join(EDITOR_DIR, "build", "kinetica"))
PORT = 47834
OUT = os.path.join(EDITOR_DIR, "docs", "images", "gif")
FPS = 10
FILMS = []


def film(function):
    FILMS.append(function)
    return function


class Camera:
    """The hidden editor (Russian, 1600 x 950, the person's settings untouched), a hand on it, and the frames."""

    def __init__(self):
        self.journal = tempfile.NamedTemporaryFile(suffix=".jsonl", delete=False).name
        self.process = subprocess.Popen([BINARY, "--agent-eval", "--api", str(PORT), "--language", "ru"], cwd=EDITOR_DIR,
                                        env=dict(os.environ, KE_JOURNAL=self.journal), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(150):
            if reachable(PORT):
                break
            time.sleep(0.2)
        self.ed = Editor(port=PORT)
        self.ed.scene_list()
        self.at = (800, 450)

    def fresh(self):
        """Before a film: no tool begun, the move tool, no ruler line."""
        self.ed.ui_key(key="Escape")
        self.ed.ui_trigger(text="toolMove")
        self.ed.view_measure()
        self.at = (800, 450)

    def close(self):
        try:
            self.ed.app_quit()
            self.process.wait(timeout=15)
        except Exception:
            self.process.kill()
        os.remove(self.journal)

    # --- the film ----------------------------------------------------------------------------------------------------
    def begin(self, box):
        """A new film of the window's part box (left, top, right, bottom, in its pixels)."""
        self.box, self.dir, self.n = box, tempfile.mkdtemp(prefix="kinetica-film-"), 0
        self.view = self.ed.view_screenshot(window=True, file=os.path.join(self.dir, "probe.png"))["view"]  # where the 3D view is
        os.remove(os.path.join(self.dir, "probe.png"))

    def frame(self, n=1):
        """The window as a person sees it now, held for n frames."""
        first = os.path.join(self.dir, f"f{self.n:04d}.png")
        self.ed.view_screenshot(screen=True, file=first)
        self.n += 1
        for _ in range(n - 1):
            shutil.copyfile(first, os.path.join(self.dir, f"f{self.n:04d}.png"))
            self.n += 1

    def hold(self, seconds):
        """The window as it goes on - a click's result arriving, the scene playing: real frames, a tenth of a second apart."""
        end = time.time() + seconds
        while time.time() < end:
            started = time.time()
            self.frame()
            time.sleep(max(0.0, 1 / FPS - (time.time() - started)))

    def end(self, name, width):
        """The frames as docs/images/gif/<name>.gif, the part cut out and scaled to `width` pixels."""
        os.makedirs(OUT, exist_ok=True)
        left, top, right, bottom = self.box
        target = os.path.join(OUT, name + ".gif")
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(FPS), "-i", os.path.join(self.dir, "f%04d.png"), "-vf",
                        f"crop={right - left}:{bottom - top}:{left}:{top},scale={width}:-1:flags=lanczos,"
                        "split[a][b];[a]palettegen=max_colors=160:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle",
                        target], check=True)
        shutil.rmtree(self.dir)
        print(f"  {name}.gif  {self.n} frames, {os.path.getsize(target) // 1024} KB")

    # --- the hand ----------------------------------------------------------------------------------------------------
    def button(self, name=None, text=None):
        """The middle of a button, a tab or an open menu's item, in the window's pixels."""
        for a in self.ed.ui_actions()["actions"]:
            if "x" in a and ((name and a.get("name") == name) or (text and a["text"].startswith(text))):
                return a["x"] + a["width"] / 2, a["y"] + a["height"] / 2
        raise LookupError(f"no button {name or text!r} in sight")

    def view_point(self, world):
        """Where a point of the world is in the window's pixels."""
        seen = self.ed.view_project(point=list(world))
        x, y = seen["x"], seen["y"]
        return self.view["x"] + x, self.view["y"] + y

    def move(self, to, seconds=0.5):
        """The hand glides there, a frame a step."""
        steps = max(1, round(seconds * FPS))
        (x0, y0), (x1, y1) = self.at, to
        for k in range(1, steps + 1):
            f = 0.5 - 0.5 * math.cos(math.pi * k / steps)
            self.ed.ui_pointer(action="move", x=x0 + (x1 - x0) * f, y=y0 + (y1 - y0) * f, space="window")
            self.frame()
        self.at = to

    def click(self, to, seconds=0.5):
        self.move(to, seconds)
        self.ed.ui_pointer(action="click", x=to[0], y=to[1], space="window")
        self.hold(0.3)

    def drag(self, start, to, seconds=1.0, button="left"):
        self.move(start)
        self.ed.ui_pointer(action="press", x=start[0], y=start[1], space="window", button=button)
        steps = max(1, round(seconds * FPS))
        for k in range(1, steps + 1):
            f = 0.5 - 0.5 * math.cos(math.pi * k / steps)
            self.ed.ui_pointer(action="drag", x=start[0] + (to[0] - start[0]) * f, y=start[1] + (to[1] - start[1]) * f, space="window", button=button)
            self.frame()
        self.ed.ui_pointer(action="release", x=to[0], y=to[1], space="window", button=button)
        self.at = to
        self.hold(0.2)

    def wheel(self, at, notches, modifiers=()):
        self.move(at)
        for _ in range(abs(notches)):
            self.ed.ui_pointer(action="wheel", x=at[0], y=at[1], notches=1 if notches > 0 else -1, space="window", modifiers=list(modifiers))
            self.frame()



@film
def shapes(c):
    """«Фигура ▾» -> «Куб», drawn with the mouse: the footprint dragged on the floor, the height lifted, a click; then the
    gizmo's arrow carries it along."""
    c.ed.scene_new()
    c.ed.view_look(eye=[4.2, 3.0, 4.6], target=[0, 0.4, 0])
    c.begin((0, 0, 1220, 690))
    c.hold(0.8)
    c.click(c.button(name="shapesButton"))
    c.hold(0.6)
    c.click(c.button(text="Куб"))
    c.hold(0.6)
    corner, across = c.view_point([-0.35, 0, -0.35]), c.view_point([0.35, 0, 0.35])
    c.drag(corner, across, seconds=1.0)
    top = c.view_point([0.35, 0.7, 0.35])
    c.move(top, 0.8)
    c.ed.ui_pointer(action="click", x=top[0], y=top[1], space="window")
    c.hold(1.2)  # one cube made, the tool ends by itself: the cube selected, its gizmo shows
    centre, along = c.view_point([0, 0.35, 0]), c.view_point([1, 0.35, 0])
    d = math.hypot(along[0] - centre[0], along[1] - centre[1]) or 1
    unit = ((along[0] - centre[0]) / d, (along[1] - centre[1]) / d)
    grip = (centre[0] + unit[0] * 85, centre[1] + unit[1] * 85)
    c.drag(grip, (grip[0] + unit[0] * 170, grip[1] + unit[1] * 170), seconds=1.2)
    c.hold(1.2)
    c.end("shapes", 720)


@film
def play(c):
    """«Твёрдое» on a cube in the air, «Пуск»: it falls, the window green while the scene plays; «Стоп»: back as it was."""
    c.ed.scene_new()
    c.ed.scene_add(shape="box", position=[0, 1.2, 0])
    c.ed.selection_set(ids=[c.ed.scene_list()["objects"][-1]["id"]])
    c.ed.view_look(eye=[3.2, 2.4, 3.6], target=[0, 0.5, 0])
    c.ed.ui_trigger(text="Объект")
    c.begin((0, 0, 1600, 760))
    c.hold(0.6)
    c.click(c.button(text="Твёрдое"))
    c.hold(0.5)
    c.click(c.button(name="actionPlay"))
    c.hold(2.2)
    c.click(c.button(name="actionStop"))
    c.hold(1.2)
    c.end("play", 800)


@film
def sequencer(c):
    """A take of the physics up to «До», ■ Стоп brings it into the Sequencer, its curves looked at closer."""
    c.ed.scene_new()
    c.ed.scene_add(shape="box", position=[0, 1.2, 0], role="rigid")
    c.ed.world_set(line="world", key="duration", values=[1.5])
    c.ed.ui_trigger(text="Секвенсор")
    c.begin((62, 688, 1220, 950))  # the mode's line and the Sequencer: a wide strip
    c.hold(0.6)
    c.click(c.button(name="actionRecord"))
    c.hold(2.0)
    c.click(c.button(name="actionStop"))
    c.hold(0.6)
    c.click(c.button(name="sequencerCurvesButton"))
    c.hold(0.6)
    c.wheel((380, 860), 5)
    c.hold(1.2)
    c.end("sequencer", 1000)


@film
def robot(c):
    """The UR5e in play: the keys drive its goal, the arm follows it; the space bar closes the gripper."""
    c.ed.scene_open(file=os.path.join(EDITOR_DIR, "examples", "scenes", "ur5e.kscene"))
    c.ed.view_look(eye=[1.25, 0.95, 1.25], target=[0.15, 0.4, 0])
    c.begin((62, 108, 1218, 685))
    c.hold(0.5)
    c.click(c.button(name="actionPlay"))
    c.hold(0.6)
    for key, seconds in (("D", 1.4), ("W", 1.0), ("F", 0.8), ("A", 1.4)):
        c.ed.ui_key(key=key, action="press")
        c.hold(seconds)
        c.ed.ui_key(key=key, action="release")
    c.ed.ui_key(key="Space")
    c.hold(1.0)
    c.end("robot", 720)


# --- the places the map of buttons does not name, in the window's pixels (1600 x 950) ------------------------------------
INSPECTOR_TABS = {"Сцена": (1259, 144), "Объект": (1336, 144), "Мир": (1408, 144)}
COLLIDER_SPHERE = (1349, 731)        # the collider card's shape buttons: auto, box, sphere, ...
CONSOLE_INPUT = (500, 887)           # the console's «>>>» line
RULER_Y, RULER_X0, RULER_PER_S = 797, 258, 84.5  # the Sequencer's ruler at the axis of a new scene (0..11 s)


def newest(c):
    """The newest object of the scene: its id, place and size (its shape line)."""
    for o in sorted(c.ed.scene_list()["objects"], key=lambda o: -o["id"]):  # the magnet makes a joint too: no shape
        line = next((l for l in c.ed.scene_get(id=o["id"]).get("lines", []) if l[0] == "shape"), None)
        if line:
            break
    at = lambda key: [float(v) for v in line[line.index(key) + 1: line.index(key) + 4]]
    return o["id"], at("position"), at("size")


def gizmo_arrow(c, centre, axis=(1, 0, 0), along=85):
    """Where the gizmo's arrow along an axis is grabbed, and the unit of its direction on the screen."""
    a, b = c.view_point(centre), c.view_point([centre[i] + axis[i] for i in range(3)])
    d = math.hypot(b[0] - a[0], b[1] - a[1]) or 1
    unit = ((b[0] - a[0]) / d, (b[1] - a[1]) / d)
    return (a[0] + unit[0] * along, a[1] + unit[1] * along), unit


@film
def light_force(c):
    """«Свет ▾» -> «Прожектор»: a spot over the scene; a cube clicked, «Сила ▾» -> «Толчок»: an arrow on it; «Пуск» - pushed."""
    c.ed.scene_new()
    for x in (-0.7, 0.0, 0.7):
        c.ed.scene_add(shape="box", position=[x, 0.15, 0], size=[0.3, 0.3, 0.3], role="rigid")
    c.ed.selection_set(ids=[])
    c.ed.view_look(eye=[2.6, 2.0, 3.0], target=[0, 0.3, 0])
    c.begin((0, 0, 1220, 690))
    c.hold(0.5)
    c.click(c.button(name="lightsButton"))
    c.hold(0.5)
    c.click(c.button(text="Прожектор"))
    c.hold(0.4)
    c.click(c.view_point([1.0, 0, 0.9]))           # where it stands
    c.click(c.view_point([1.0, 1.3, 0.9]))         # how high
    c.click(c.view_point([0.0, 0.15, 0]))          # what it lights
    c.hold(0.6)
    c.click(c.button(name="toolMove"))
    c.click(c.view_point([0.0, 0.15, 0]))          # the middle cube chosen
    c.hold(0.4)
    c.click(c.button(text="Сила"))
    c.hold(0.5)
    c.click(c.button(text="Толчок"))
    c.hold(0.8)
    c.click(c.button(name="actionPlay"))
    c.hold(2.0)
    c.click(c.button(name="actionStop"))
    c.hold(0.6)
    c.end("light-force", 720)


@film
def magnet(c):
    """«Магнит»: over a face its ghost, a click - a link on a hinge; once more on the new link; «Датчик ▾» -> a sensor on it."""
    c.ed.scene_new()
    base = c.ed.scene_add(shape="box", position=[0, 0.25, 0], size=[0.5, 0.5, 0.5], role="rigid", fixed=True)["id"]
    c.ed.selection_set(ids=[base])
    c.ed.view_look(eye=[2.6, 2.1, 3.2], target=[0, 0.9, 0])  # room for two links over the base
    c.begin((0, 0, 1220, 690))
    c.hold(0.5)
    c.click(c.button(name="magnetTool"))
    c.move(c.view_point([0, 0.5, 0]), 0.6)
    c.hold(0.6)
    top = c.view_point([0, 0.5, 0])
    c.ed.ui_pointer(action="click", x=top[0], y=top[1], space="window")
    c.hold(0.8)
    _, at, size = newest(c)
    up = c.view_point([at[0], at[1] + size[1] / 2, at[2]])
    c.move(up, 0.6)
    c.hold(0.5)
    c.ed.ui_pointer(action="click", x=up[0], y=up[1], space="window")
    c.hold(0.8)
    c.ed.ui_key(key="Escape")
    c.hold(0.3)
    c.click(c.button(name="sensorsButton"))
    c.hold(0.6)
    c.click(c.button(text="IMU"))
    c.hold(1.0)
    c.end("magnet", 720)


@film
def undo(c):
    """A cube carried by its arrow; «Отменить» - back; «Повторить» - there again."""
    c.ed.scene_new()
    cube = c.ed.scene_add(shape="box", position=[0, 0.3, 0], size=[0.6, 0.6, 0.6])["id"]
    c.ed.selection_set(ids=[cube])
    c.ed.view_look(eye=[3.4, 2.4, 3.8], target=[0.4, 0.3, 0])
    c.begin((0, 0, 1220, 690))
    c.hold(0.5)
    grip, unit = gizmo_arrow(c, [0, 0.3, 0])
    c.drag(grip, (grip[0] + unit[0] * 180, grip[1] + unit[1] * 180), seconds=1.0)
    c.hold(0.5)
    c.click(c.button(text="Отменить"))
    c.hold(0.8)
    c.click(c.button(text="Повторить"))
    c.hold(0.8)
    c.end("undo", 720)


@film
def tools(c):
    """The tool shelf: «Вращение» - a ring turns the cube, «Масштаб» - a handle stretches it, «Линейка» - a drag measures."""
    c.ed.scene_new()
    cube = c.ed.scene_add(shape="box", position=[0, 0.35, 0], size=[0.7, 0.7, 0.7])["id"]
    c.ed.selection_set(ids=[cube])
    c.ed.view_look(eye=[3.0, 2.2, 3.4], target=[0, 0.35, 0])
    c.begin((0, 0, 1220, 690))
    c.hold(0.4)
    c.click(c.button(name="toolRotate"))
    centre = [0, 0.35, 0]
    a, b = c.view_point(centre), c.view_point([0, 0.35, 1])
    per_m = math.hypot(b[0] - a[0], b[1] - a[1]) or 1
    ring = c.view_point([0, 0.35, 110 / per_m])  # the ring about y passes 110 px from the centre
    c.drag(ring, (ring[0] + 150, ring[1] - 20), seconds=1.2)
    c.hold(0.4)
    c.click(c.button(name="toolScale"))
    grip, unit = gizmo_arrow(c, centre, along=100)
    c.drag(grip, (grip[0] + unit[0] * 90, grip[1] + unit[1] * 90), seconds=1.0)
    c.hold(0.4)
    c.click(c.button(name="toolRuler"))
    c.drag(c.view_point([-1.2, 0, 1.0]), c.view_point([1.2, 0, 1.0]), seconds=1.2)
    c.hold(1.2)
    c.end("tools", 720)


@film
def camera(c):
    """The 3D view by the mouse: the right button turns the view, the middle one moves it, the wheel comes closer, F frames."""
    c.ed.scene_new()
    for k, (x, z) in enumerate(((-0.6, -0.4), (0.5, -0.5), (0.0, 0.5))):
        c.ed.scene_add(shape=("box", "sphere", "cylinder")[k], position=[x, 0.25, z], size=[0.5, 0.5, 0.5])
    c.ed.view_look(eye=[2.6, 2.0, 3.0], target=[0, 0.3, 0])
    c.ed.selection_set(ids=[])
    c.begin((62, 108, 1218, 685))
    c.hold(0.4)
    c.drag((640, 420), (840, 400), seconds=1.4, button="right")
    c.hold(0.3)
    c.drag((640, 420), (560, 480), seconds=1.0, button="middle")
    c.hold(0.3)
    c.wheel((640, 400), 6)
    c.hold(0.4)
    c.click(c.view_point([0.0, 0.25, 0.5]))  # the cylinder chosen; F frames it
    c.ed.ui_key(key="F")
    c.hold(1.2)
    c.end("camera", 720)


@film
def inspector(c):
    """The inspector: «Сцена» lists the objects; «Объект» - its roles, its numbers, its collider's shape; «Мир» - the world."""
    c.ed.scene_new()
    for x in (-0.6, 0.0, 0.6):
        c.ed.scene_add(shape="box", position=[x, 0.3, 0], role="rigid")
    c.ed.view_look(eye=[2.4, 1.8, 2.8], target=[0, 0.3, 0])
    c.begin((62, 100, 1600, 950))
    c.hold(0.4)
    c.click(INSPECTOR_TABS["Сцена"])
    c.hold(0.4)
    c.click((1330, 243))  # the third row: «Куб 2»
    c.hold(0.5)
    c.click(INSPECTOR_TABS["Объект"])
    c.hold(0.6)
    c.click(COLLIDER_SPHERE)
    c.hold(0.8)
    c.click(INSPECTOR_TABS["Мир"])
    c.hold(1.0)
    c.end("inspector", 800)


@film
def keys(c):
    """The Sequencer by hand: «◆ Ключ» at 0 s, the playhead dragged to 2 s, the cube carried, «◆ Ключ»; the playhead dragged
    back and forth - the cube goes along its track; «▶ Просмотр» plays it."""
    c.ed.scene_new()
    cube = c.ed.scene_add(shape="box", position=[-0.8, 0.3, 0], size=[0.5, 0.5, 0.5])["id"]
    c.ed.selection_set(ids=[cube])
    c.ed.view_look(eye=[2.6, 2.2, 3.6], target=[0, 0.3, 0])
    c.ed.ui_trigger(text="Секвенсор")
    c.begin((62, 100, 1220, 950))
    c.hold(0.4)
    c.click(c.button(name="sequencerKey"))
    c.hold(0.4)
    x = lambda t: RULER_X0 + RULER_PER_S * t
    c.drag((x(0), RULER_Y), (x(2), RULER_Y), seconds=0.8)
    grip, unit = gizmo_arrow(c, [-0.8, 0.3, 0])
    c.drag(grip, (grip[0] + unit[0] * 220, grip[1] + unit[1] * 220), seconds=1.0)
    c.click(c.button(name="sequencerKey"))
    c.hold(0.3)
    c.drag((x(2), RULER_Y), (x(0), RULER_Y), seconds=1.2)
    c.drag((x(0), RULER_Y), (x(1.3), RULER_Y), seconds=1.0)
    c.click(c.button(name="sequencerPlay"))
    c.hold(2.4)
    c.end("keys", 760)


@film
def plots(c):
    """«Графики»: three cubes fall, the energies of the scene drawn as it plays."""
    c.ed.scene_new()
    for k, x in enumerate((-0.5, 0.0, 0.5)):
        c.ed.scene_add(shape="box", position=[x, 0.8 + 0.4 * k, 0], size=[0.3, 0.3, 0.3], role="rigid")
    c.ed.view_look(eye=[2.4, 1.8, 2.8], target=[0, 0.5, 0])
    c.begin((62, 100, 1220, 950))
    c.hold(0.4)
    c.click(c.button(name="actionGraphs"))  # the panel comes up as the bottom's tab
    c.hold(0.5)
    c.click(c.button(name="actionPlay"))
    c.hold(3.0)
    c.click(c.button(name="actionStop"))
    c.hold(0.5)
    c.end("plots", 760)


@film
def console(c):
    """The API console: a line of Python typed, Enter - the sphere is in the scene and falls at «Пуск»."""
    c.ed.scene_new()
    c.ed.view_look(eye=[2.4, 1.8, 2.8], target=[0, 0.4, 0])
    c.ed.ui_trigger(text="Консоль API")
    c.begin((62, 100, 1220, 950))
    c.hold(0.4)
    c.click(CONSOLE_INPUT)
    for word in ('scene.add("sphere", ', 'position=(0, 1.2, 0), ', 'role="rigid")'):
        c.ed.ui_type(text=word)
        c.hold(0.3)
    c.ed.ui_key(key="Enter", window=True)
    c.hold(0.8)
    c.click(c.button(name="actionPlay"))
    c.hold(1.6)
    c.click(c.button(name="actionStop"))
    c.hold(0.4)
    c.end("console", 760)


@film
def examples(c):
    """«Примеры»: the gallery of scenes; a picture clicked - the scene is open."""
    c.ed.scene_new()
    c.begin((0, 0, 1600, 950))
    c.hold(0.4)
    c.click(c.button(text="Примеры"))  # a click, not ui_trigger: the gallery is a dialog, the call would wait for it
    c.hold(2.0)
    c.move((480, 208), 0.8)  # «Рука-манипулятор»: a click on a picture opens its scene
    c.hold(0.4)
    c.click((480, 208))
    c.hold(2.0)
    c.end("examples", 800)


def main():
    wanted = sys.argv[1:]
    camera = Camera()
    try:
        for f in FILMS:
            if not wanted or any(f.__name__.startswith(w) for w in wanted):
                try:
                    camera.fresh()
                    f(camera)
                except Exception as e:  # one film's trouble said, the others still made
                    print(f"  {f.__name__}: {type(e).__name__}: {e}")
                    camera.ed.ui_key(key="Escape")
    finally:
        camera.close()


if __name__ == "__main__":
    main()
