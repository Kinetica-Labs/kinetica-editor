"""The showcase film: one scene that uses what the editor has, filmed twice - through its own animated camera and as
the editor sees it - and cut into a video and a GIF for the README.

The scene: the UR5e takes a cube and puts it on a plate (inverse kinematics, the gripper); a ball on a pendulum, set off
by a force of the scene, breaks a tower of blocks (joints, rigid bodies); formulas stand over the bodies and titles in
the frame (the plugin `formula`); the floor, the blocks and the ball wear generated textures with normal maps (the
plugin `textures`); two spotlights cast shadows and one of them travels; a camera of the scene flies along the keys of
a sequence. Every frame is one step of the simulation and one picture, so the film is the same on any machine however
fast it draws.

    python3 tools/showcase_film.py --plugins ../kinetica-plugins --out build/showcase

needs the editor built (build/kinetica), the two plugins, ffmpeg, and latex + dvisvgm for the formulas. A hidden window
does the work: nothing appears on the screen. It leaves kinetica-showcase.mp4 (the film: the camera's pass, the editor's
window in its corner), -camera.mp4 and -editor.mp4 (each pass alone) and kinetica-showcase.gif - the README's picture,
copied by hand to docs/images/showcase.gif when the film is liked.
"""
import argparse
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "python"))
import kinetica as ke  # noqa: E402

FPS = 60
LENGTH = 18.0  # seconds of the scene's time

# The story, in seconds of the scene's time.
REACH, LOWER, GRIP, LIFT, CARRY, PLACE, OPEN, AWAY = 0.5, 3.0, 5.0, 6.0, 8.0, 10.0, 11.4, 12.0
STRIKE = 13.0  # the pendulum is set off

PIVOT = (1.05, 1.45, 0.75)  # where the pendulum hangs
ROD = 1.2                   # from the pivot to the ball's centre, m
TOWER = (1.53, 0.75)        # x, z of the tower: the ball's first step from rest
PLATE = (-0.184, -0.272)   # x, z of the plate the cube is put on (the scene's «Площадка»)


def put(scene, thing, line, key, *values):
    scene.ed.scene_set(id=getattr(thing, "id", thing), line=line, key=key, values=list(values))


def look(eye, target):
    """A free camera's rotation (Euler degrees: pitch, yaw, roll) that looks from `eye` at `target` along its -z."""
    f = [t - e for e, t in zip(eye, target)]
    n = math.sqrt(sum(v * v for v in f))
    return [math.degrees(math.asin(f[1] / n)), math.degrees(math.atan2(-f[0], -f[2])), 0.0]


def ease(t):
    return t * t * (3 - 2 * t)


def along(keys, t):
    """A smooth way through `keys` - (time, point) pairs in order - at time t: eased between each two."""
    if t <= keys[0][0]:
        return list(keys[0][1])
    for (t0, a), (t1, b) in zip(keys, keys[1:]):
        if t <= t1:
            s = ease((t - t0) / (t1 - t0))
            return [p + (q - p) * s for p, q in zip(a, b)]
    return list(keys[-1][1])


# Where the film's camera stands and what it looks at: the stage whole; the arm taking its cube, from near; the carry;
# a turn to the tower as the ball is let go; the fall; and back out over everything.
EYE = [(0.0, (2.95, 1.5, -3.05)), (2.6, (1.25, 0.6, -1.6)), (5.2, (0.62, 0.24, -1.12)), (8.0, (-0.75, 0.42, -1.25)), (11.8, (-0.85, 0.36, -0.95)),
       (13.0, (2.35, 0.75, -0.75)), (14.3, (3.2, 0.9, -0.55)), (15.8, (3.5, 1.35, -1.3)), (18.0, (3.0, 1.9, -2.7))]
AIM = [(0.0, (0.55, 0.6, 0.1)), (2.6, (0.15, 0.25, -0.5)), (5.2, (0.115, 0.09, -0.6)), (8.0, (0.0, 0.22, -0.48)), (11.8, (-0.18, 0.08, -0.28)),
       (13.0, (1.45, 0.4, 0.72)), (14.3, (2.1, 0.25, 0.75)), (15.8, (1.6, 0.3, 0.6)), (18.0, (0.6, 0.35, 0.25))]


def build(scene):
    """The scene and what the film needs of it by name: a dict of its objects."""
    ed = scene.ed
    scene.open(os.path.join(ROOT, "examples", "ur5e.kscene"))
    ed.view_draw(clear=True)
    floor = scene.objects["Пол"]
    ed.world_set(line="world", key="size", values=[60, 20, 60])  # the stage's walls far out of the light
    put(scene, floor, "shape", "size", 40, 0.02, 40)
    for name in ("Кубик жёлтый", "Кубик зелёный", "Кубик пурпурный"):
        put(scene, scene.objects[name], "texture", "kind", "none")  # plain bright colours
    for light in [o for o in scene.objects if o.kind == "light"]:
        light.delete()
    select = scene.editor.select

    def texture(shape, kind, repeats, relief=2.0, variant=0, colours=None):
        select(shape)
        own = {"own": True, "base": list(colours[0]), "mark": list(colours[1])} if colours else {}
        ed.textures_apply(kind=kind, repeats=repeats, relief=relief, variant=variant, maps="нормали", **own)

    texture(floor, "плитка", 0.5, relief=1.5, colours=((0.21, 0.22, 0.27), (0.08, 0.085, 0.11)))  # dark slate, half a metre a tile
    texture(scene.objects["Площадка"], "мрамор", 6.0, relief=0.5)

    # The pendulum: a frame of two posts and a beam, a rod on a hinge under the beam, a ball welded to its end.
    px, py, pz = PIVOT
    for dz in (-0.42, 0.42):
        post = scene.add("box", name="Стойка", position=(px, (py + 0.1) / 2, pz + dz), size=(0.07, py + 0.1, 0.07), role="rigid", fixed=True)
        texture(post, "металл", 2.0, relief=0.6)
    beam = scene.add("box", name="Балка", position=(px, py + 0.055, pz), size=(0.07, 0.07, 0.91), role="rigid", fixed=True)
    texture(beam, "металл", 2.0, relief=0.6)
    rod = scene.add("box", name="Штанга", position=(px, py - ROD / 2, pz), size=(0.03, ROD, 0.03), role="rigid")
    put(scene, rod, "shape", "color", 0.75, 0.77, 0.8)
    ball = scene.add("sphere", name="Шар", position=(px, py - ROD, pz), size=(0.26, 0.26, 0.26), role="rigid")
    put(scene, ball, "rigid", "density", 2600)
    texture(ball, "мрамор", 3.0, relief=0.5, variant=4, colours=((0.14, 0.15, 0.18), (0.75, 0.62, 0.32)))  # black marble, gold veins
    scene.add_joint(rod, kind="hinge", anchor=(px, py, pz), axis=(0, 0, 1))
    scene.add_joint(ball, rod, kind="weld", anchor=(px, py - ROD, pz))
    strike = scene.add_force("directional", "impulse", body=ball, strength=150.0)
    strike.turn("z", -90)  # its +y, the way it pushes, turned along the world's +x: at the tower
    put(scene, strike, "effect", "start", STRIKE)

    # The tower: three blocks a course, each course across the one under it.
    blocks, (tx, tz) = [], TOWER
    for course in range(7):
        for k in (-1, 0, 1):
            across = course % 2 == 1
            block = scene.add("box", name="Брусок", position=(tx + (0.0 if across else 0.082 * k), 0.02 + 0.04 + 0.08 * course, tz + (0.082 * k if across else 0.0)),
                              size=(0.08, 0.08, 0.245) if not across else (0.245, 0.08, 0.08), role="rigid")
            blocks.append(block)
    select(*blocks)
    ed.textures_apply(kind="дерево", repeats=4.0, relief=0.8, variant=3, maps="нормали")

    # Formulas: what each thing is about. The plugin typesets them and the editor keeps them on their bodies.
    cube, goal = scene.objects["Кубик жёлтый"], scene.objects["Цель «Схват»"]
    for body, latex, size, offset in ((cube, r"m\vec{g}", 3.2, (0, 0.11, 0)), (ball, r"\vec{p} = m\vec{v}", 3.6, (0, 0.3, 0)),
                                      (scene.objects["UR5e предплечье"], r"\vec{\tau} = J^{T}\vec{F}", 3.4, (0, 0.22, 0))):
        ed.component_set(id=body.id, type="formula.label", values={"latex": latex, "size": size, "offset": list(offset), "colour": [1.0, 0.86, 0.35]})
    ed.component_set(id=beam.id, type="formula.world", values={"latex": r"T = 2\pi\sqrt{\frac{L}{g}}", "size": 0.11, "offset": [0, 0.17, 0], "turn": [0, 180, 0],
                                                                "colour": [0.72, 0.66, 1.0]})

    # Light: a warm key from the front left, a cool rim from behind that travels, a faint sun to lift the shadows.
    key = scene.add_light("spot", position=(-2.0, 3.2, -2.6), target=(0.6, 0.2, 0.2))
    rim = scene.add_light("spot", position=(2.8, 2.6, 3.2), target=(0.6, 0.35, 0.2))
    fill = scene.add_light("sun", position=(1.0, 4.0, -2.0), target=(0.0, 0.0, 0.0))
    glow = scene.add_light("point", position=(2.1, 0.55, 1.5))  # an ember behind the tower: its far side is not black
    for light, colour, intensity, shadows, cone in ((key, (1.0, 0.88, 0.72), 2.5, True, 95), (rim, (0.45, 0.66, 1.0), 2.3, True, 95),
                                                   (fill, (0.8, 0.86, 1.0), 0.36, False, 0), (glow, (1.0, 0.45, 0.2), 2.0, False, 0)):
        put(scene, light, "pose", "color", *colour)
        light.intensity, light.shadows = intensity, shadows
        put(scene, light, "kind", "range", 4 if light is glow else 24)
        if cone:
            put(scene, light, "kind", "cone", cone), put(scene, light, "kind", "softness", 25)

    # The camera of the film, and the keys of everything the sequence leads: the camera along its way, the rim light round the stage.
    camera = scene.add_camera(eye=EYE[0][1], target=AIM[0][1])
    put(scene, camera, "lens", "fov", 42)
    step = 0.25
    for k in range(int(LENGTH / step) + 1):
        t = k * step
        eye = along(EYE, t)
        put(scene, camera, "pose", "position", *eye)
        put(scene, camera, "pose", "rotation", *look(eye, along(AIM, t)))
        ed.sequence_key(ids=[camera.id], time=t)
    for t, place in ((0.0, (2.8, 2.6, 3.2)), (9.0, (0.4, 2.9, 3.8)), (LENGTH, (-2.2, 2.6, 3.0))):
        put(scene, rim, "pose", "position", *place)
        ed.sequence_key(ids=[rim.id], time=t, interp="smooth")
    ed.sequence_set(play=True)
    ed.sequence_seek(time=0)
    select()
    return {"camera": camera, "cube": cube, "goal": goal, "ball": ball, "rim": rim}


def act(scene, parts, t, state):
    """What the arm is told at the scene's time t: once, when its moment comes."""
    goal, cube = parts["goal"], parts["cube"]
    if "at" not in state and t >= REACH:
        seen = scene.measure(objects=[cube])
        state["at"] = (seen[f"{cube.id}/centre x"], seen[f"{cube.id}/centre z"])
    x, z = state.get("at", (0, 0))
    for moment, name, do in ((REACH, "reach", lambda: goal.pose(position=(x, 0.25, z))), (LOWER, "lower", lambda: goal.pose(position=(x, 0.115, z))),
                             (GRIP, "grip", lambda: goal.grip()), (LIFT, "lift", lambda: goal.pose(position=(x, 0.32, z))),
                             (CARRY, "carry", lambda: goal.pose(position=(PLATE[0], 0.32, PLATE[1]))),
                             (PLACE, "place", lambda: goal.pose(position=(PLATE[0], 0.122, PLATE[1]))),
                             (OPEN, "open", lambda: goal.grip(close=False)), (AWAY, "away", lambda: goal.pose(position=(PLATE[0], 0.36, PLATE[1])))):
        if t >= moment and name not in state:
            state[name] = True
            do()


def typesetter(plugins):
    """The formula plugin's own typesetting, for the titles the film draws in the frame by itself."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("kinetica_formula_plugin", os.path.join(plugins, "formula", "plugin.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.typeset


# The words in the frame: (from, to, LaTeX, place in the frame, size, colour).
TITLES = [(0.4, 2.9, r"\text{Kinetica}", (0.5, 0.74), 0.115, (1.0, 1.0, 1.0)),
          (0.9, 2.9, r"\text{robotics} \cdot \text{physics} \cdot \text{plugins}", (0.5, 0.87), 0.036, (0.72, 0.8, 1.0)),
          (3.4, 7.6, r"\text{inverse kinematics}", (0.5, 0.92), 0.036, (0.92, 0.94, 1.0)),
          (13.1, 15.6, r"\text{rigid-body physics}", (0.5, 0.92), 0.036, (0.92, 0.94, 1.0)),
          (16.2, 99.0, r"\text{Kinetica Editor}", (0.5, 0.76), 0.085, (1.0, 1.0, 1.0)),
          (16.6, 99.0, r"\text{github.com/Kinetica-Labs}", (0.5, 0.87), 0.032, (0.72, 0.8, 1.0))]


def titles(ed, typeset, t, shown):
    for k, (since, until, latex, place, size, colour) in enumerate(TITLES):
        on = since <= t < until
        if on and k not in shown:
            ed.view_draw(key=f"film/{k}", fill=typeset(latex), space="screen", place=list(place), scale=size, colour=list(colour))
            shown.add(k)
        elif not on and k in shown:
            ed.view_draw(key=f"film/{k}", clear=True)
            shown.discard(k)


def film(scene, parts, typeset, folder, through, fps):
    """The scene played a frame at a time and each frame saved: through the film's camera, with nothing of the editor
    in it; or as the editor's window shows it - its own view from aside, the camera's mark flying, the joints' ties, the
    panels."""
    ed = scene.ed
    os.makedirs(folder, exist_ok=True)
    scene.editor.select() if through else scene.editor.select(parts["camera"])
    ed.view_through(camera=parts["camera"].id if through else 0)
    ed.sim_step(frames=0)  # the world built, its time 0
    state, shown = {}, set()
    began = time.time()
    every = FPS // fps  # steps of the simulation a picture
    for frame in range(int(LENGTH * FPS) + 1):
        t = frame / FPS
        act(scene, parts, t, state)
        if frame % every == 0 and through:
            titles(ed, typeset, t, shown)
            ed.view_screenshot(file=os.path.join(folder, f"{frame // every:05d}.png"), clean=True, marks=False)
        elif frame % every == 0:
            turn = math.radians(-50 + 14 * t / LENGTH)  # the editor's own view drifts round the stage
            ed.view_look(eye=[0.6 + 4.6 * math.sin(turn), 2.5, 0.2 - 4.6 * math.cos(turn)], target=[0.6, 0.45, 0.2])
            ed.view_screenshot(file=os.path.join(folder, f"{frame // every:05d}.png"), window=True, clean=True)  # no «an agent drives» bar
        ed.sim_step(frames=1)
        if frame % 120 == 0:
            print(f"  {'camera' if through else 'editor'}: {t:5.1f} s of {LENGTH:.0f}, {time.time() - began:5.1f} s", flush=True)
    for k in list(shown):
        ed.view_draw(key=f"film/{k}", clear=True)
    ed.sim_stop()
    ed.view_through()


def start(port, plugins, size, work):
    """A hidden editor of its own, the plugins running: the scene to drive it by."""
    env = dict(os.environ, KE_JOURNAL=os.path.join(work, "journal.jsonl"), XDG_CONFIG_HOME=work, XDG_DATA_HOME=work, KE_PLUGINS=os.path.abspath(plugins))
    editor = subprocess.Popen([os.path.join(ROOT, "build", "kinetica"), "--agent-eval", "--api", str(port), "--size", size], cwd=ROOT, env=env,
                              stdout=open(os.path.join(work, "editor.log"), "w"), stderr=subprocess.STDOUT)
    for _ in range(150):
        try:
            scene = ke.connect(port)
            running = {p["name"]: p["state"] for p in scene.ed.plugin_list()["plugins"]}
            if running.get("formula") == "работает" and running.get("textures") == "работает":
                return editor, scene
        except Exception:
            pass
        time.sleep(0.2)
    editor.kill()
    sys.exit("the editor did not start with the plugins formula and textures (see " + os.path.join(work, "editor.log") + ")")


def main():
    said = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    said.add_argument("--plugins", default=os.path.join(ROOT, "..", "kinetica-plugins"), help="the folder of the plugins formula and textures")
    said.add_argument("--out", default=os.path.join(ROOT, "build", "showcase"))
    said.add_argument("--port", type=int, default=47877)
    said.add_argument("--stills", default="", help="times, s, with commas: the scene at these moments as pictures, no film")
    said.add_argument("--passes", default="camera,editor", help="which of the two passes to film")
    said.add_argument("--fps", type=int, default=FPS, choices=(10, 15, 20, 30, 60), help="pictures a second of the scene's time")
    args = said.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for name in ["stills"] if args.stills else args.passes.split(","):
        shoot(args, name)
    if not args.stills:
        cut(args)


def shoot(args, name):
    """One pass in an editor of its own: the camera's in a window whose view is larger than the film's frame, the
    editor's in a window of the film's own shape."""
    work = tempfile.mkdtemp(prefix="kinetica-film-")
    editor, scene = start(args.port, args.plugins, "1600x900" if name == "editor" else "2560x1500", work)
    try:
        parts = build(scene)
        time.sleep(1.5)  # the formulas are typeset and drawn
        if args.stills:
            scene.ed.view_through(camera=parts["camera"].id)
            for t in (float(word) for word in args.stills.split(",")):
                scene.ed.sequence_seek(time=t)
                time.sleep(0.3)
                shot = scene.ed.view_screenshot(file=os.path.join(args.out, f"still-{t:05.2f}.png"), clean=True, marks=False)
                print("still", t, shot["width"], "x", shot["height"])
            scene.ed.view_through()
            scene.ed.view_look(eye=[4.6, 3.4, -4.4], target=[0.5, 0.4, 0.3])
            scene.ed.view_screenshot(file=os.path.join(args.out, "still-editor.png"), window=True)
            return
        shutil.rmtree(os.path.join(args.out, name), ignore_errors=True)
        film(scene, parts, typesetter(args.plugins), os.path.join(args.out, name), name == "camera", args.fps)
    finally:
        try:
            scene.ed.app_quit()
            editor.wait(timeout=10)
        except Exception:
            editor.kill()
        shutil.rmtree(work, ignore_errors=True)


def cut(args):
    """The frames into the film: the camera's pass full frame, the editor's window in its corner while the story runs; and
    the two moments of the story - the cube taken, the tower broken - as a GIF for the README."""
    camera, editor = (os.path.join(args.out, name, "%05d.png") for name in ("camera", "editor"))
    run = lambda *words: subprocess.run(["ffmpeg", "-y", "-loglevel", "error", *words], check=True)  # noqa: E731
    frame = "scale=-2:1080:flags=lanczos,crop=1920:1080"
    film_ = os.path.join(args.out, "kinetica-showcase.mp4")
    if os.path.isdir(os.path.dirname(editor)) and os.listdir(os.path.dirname(editor)):
        inset = "scale=608:-2:flags=lanczos,pad=iw+6:ih+6:3:3:color=0xE8ECF4"
        run("-framerate", str(args.fps), "-i", camera, "-framerate", str(args.fps), "-i", editor, "-filter_complex",
            f"[0:v]{frame}[main];[1:v]{inset}[small];[main][small]overlay=W-w-40:40:enable='between(t,3.1,15.9)'",
            "-c:v", "libx264", "-preset", "slow", "-crf", "16", "-pix_fmt", "yuv420p", "-r", str(args.fps), film_)
        run("-framerate", str(args.fps), "-i", editor, "-c:v", "libx264", "-preset", "slow", "-crf", "16", "-pix_fmt", "yuv420p", "-vf", "scale=1600:-2",
            os.path.join(args.out, "kinetica-showcase-editor.mp4"))
    else:
        run("-framerate", str(args.fps), "-i", camera, "-vf", frame, "-c:v", "libx264", "-preset", "slow", "-crf", "16", "-pix_fmt", "yuv420p", film_)
    clean = os.path.join(args.out, "kinetica-showcase-camera.mp4")
    run("-framerate", str(args.fps), "-i", camera, "-vf", frame, "-c:v", "libx264", "-preset", "slow", "-crf", "16", "-pix_fmt", "yuv420p", clean)
    moments = "select='between(t,3.9,6.5)+between(t,12.9,15.7)',setpts=N/FRAME_RATE/TB"
    small = f"{moments},fps=15,scale=600:-2:flags=lanczos"  # under 5 MB: a README's picture is loaded by everyone who opens it
    run("-i", clean, "-filter_complex", f"[0:v]{small},split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4",
        os.path.join(args.out, "kinetica-showcase.gif"))
    for name in ("kinetica-showcase.mp4", "kinetica-showcase-camera.mp4", "kinetica-showcase-editor.mp4", "kinetica-showcase.gif"):
        file = os.path.join(args.out, name)
        if os.path.exists(file):
            print(f"  {file}: {os.path.getsize(file) / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
