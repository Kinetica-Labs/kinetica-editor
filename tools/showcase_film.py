"""The showcase film: the scene of the plugin `showcase` (kinetica-plugins) played in a hidden editor and recorded a
frame a step - twice, through the scene's own camera and as the editor's window shows it - and cut into a video and a
GIF for the README.

The film is nothing but the editor's picture of that scene: the plugin's «Собрать сцену» builds it and its story plays
on ▶ - what anyone gets who presses the same two buttons. This script only presses them, steps the simulation a frame
at a time, saves each frame and joins them: nothing is done to the picture. Every frame is one step of the simulation,
so the film is the same on any machine however fast it draws. The one move of the cut's own is the end: the camera draws
back out of the 3D view to the whole window round it - the editor's window, recorded at the same frames, the scene still
playing in it.

    python3 tools/showcase_film.py --plugins ../kinetica-plugins --out build/showcase

needs the editor built (build/kinetica), the plugins showcase, formula and textures, ffmpeg, and latex + dvisvgm for
the formulas. A hidden window does the work: nothing appears on the screen. It leaves kinetica-showcase.mp4 (the film:
the camera's pass, the editor's window in its corner, the window at the end), -camera.mp4 and -editor.mp4 (each pass
alone), kinetica-showcase-720.mp4 (the film light enough for a web page) and kinetica-showcase.gif - the README's
picture; the last two are copied by hand to docs/images/showcase.mp4 and showcase.gif when the film is liked.
"""
import argparse
import json
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
INSET = (3.4, 20.4)                    # while the editor's window stands in the film's corner, s: from the first cut to the last
MOMENTS = ((8.6, 11.6), (17.8, 20.2))  # what the GIF shows, s: the queen twirled, the tower broken
REVEAL = (24.4, 26.8)                  # the camera drawn back out of the view to the whole window, s: from its first frame to its last
FILM = (1920, 1080)
# The camera's pass runs in a window of an ordinary screen's size drawn at 1.5 its pixels: the view gives the film's frame
# sharp, and the window seen at the end is the editor as a person sees it on that screen.
SCREEN, SCALE = "1920x1080", "1.5"


def film(scene, folder, through, fps):
    """The story played a frame at a time and each frame saved: through the film's camera, with nothing of the editor
    in it - and, from the end's first frame, the whole window round that view too; or as the editor's window shows it -
    its own view from aside, the camera's mark flying, the joints' ties, the panels. The story itself is the plugin's: it
    plays on every step, as on ▶."""
    ed = scene.ed
    window = os.path.join(os.path.dirname(folder), "window")
    for made in (folder, window) if through else (folder,):
        os.makedirs(made, exist_ok=True)
    camera = ed.scene_list(tag="film-camera")["objects"][0]["id"]
    length = scene.world.duration  # the scene says how long its story is
    ed.sim_step(frames=0)  # the world built, its time 0: the story begins and the view goes through its camera
    ed.selection_set(ids=[camera])  # the editor's pass draws its mark; the window at the end shows its card
    if not through:
        ed.view_through()
    began = time.time()
    every = FPS // fps  # steps of the simulation a picture
    for frame in range(int(length * FPS) + 1):
        t, name = frame / FPS, f"{frame // every:05d}.png"
        if frame % every == 0 and through:
            ed.view_screenshot(file=os.path.join(folder, name), clean=True, marks=False)
            if t >= REVEAL[0] - 1e-9:
                shot = ed.view_screenshot(file=os.path.join(window, name), window=True, clean=True, marks=False)
                with open(os.path.join(window, "view.json"), "w") as kept:  # where the view stands in the window's picture
                    json.dump({"view": shot["view"], "width": shot["width"], "height": shot["height"]}, kept)
        elif frame % every == 0:
            turn = math.radians(-50 + 14 * t / length)  # the editor's own view drifts round the stage
            ed.view_look(eye=[0.5 + 4.2 * math.sin(turn), 2.3, 0.0 - 4.2 * math.cos(turn)], target=[0.5, 0.4, 0.0])
            ed.view_screenshot(file=os.path.join(folder, name), window=True, clean=True)  # no «an agent drives» bar
        ed.sim_step(frames=1)
        if frame % 120 == 0:
            print(f"  {'camera' if through else 'editor'}: {t:5.1f} s of {length:.0f}, {time.time() - began:5.1f} s", flush=True)
    ed.sim_stop()
    return length


def start(port, plugins, size, scale, work):
    """A hidden editor of its own, the plugins running: the scene to drive it by."""
    env = dict(os.environ, KE_JOURNAL=os.path.join(work, "journal.jsonl"), XDG_CONFIG_HOME=work, XDG_DATA_HOME=work,
               KE_PLUGINS=os.path.abspath(plugins), QT_SCALE_FACTOR=scale)
    editor = subprocess.Popen([os.path.join(ROOT, "build", "kinetica"), "--agent-eval", "--api", str(port), "--size", size], cwd=ROOT, env=env,
                              stdout=open(os.path.join(work, "editor.log"), "w"), stderr=subprocess.STDOUT)
    for _ in range(150):
        try:
            scene = ke.connect(port)
            running = {p["name"]: p["state"] for p in scene.ed.plugin_list()["plugins"]}
            if all(running.get(name) == "работает" for name in ("showcase", "formula", "textures")):
                return editor, scene
        except Exception:
            pass
        time.sleep(0.2)
    editor.kill()
    sys.exit("the editor did not start with the plugins showcase, formula and textures (see " + os.path.join(work, "editor.log") + ")")


def main():
    said = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    said.add_argument("--plugins", default=os.path.join(ROOT, "..", "kinetica-plugins"), help="the folder of the plugins showcase, formula and textures")
    said.add_argument("--out", default=os.path.join(ROOT, "build", "showcase"))
    said.add_argument("--port", type=int, default=47877)
    said.add_argument("--passes", default="camera,editor", help='which of the two passes to film; "": only the cut, from the frames there are')
    said.add_argument("--fps", type=int, default=FPS, choices=(10, 15, 20, 30, 60), help="pictures a second of the scene's time")
    args = said.parse_args()
    args.out, args.plugins = os.path.abspath(args.out), os.path.abspath(args.plugins)
    os.makedirs(args.out, exist_ok=True)
    for name in [name for name in args.passes.split(",") if name]:
        shoot(args, name)
    cut(args)


def shoot(args, name):
    """One pass in an editor of its own: the camera's in a window of a screen's size at 1.5 its pixels (SCREEN), the
    editor's in a window of the film's own shape."""
    work = tempfile.mkdtemp(prefix="kinetica-film-")
    editor, scene = start(args.port, args.plugins, *(("1600x900", "1") if name == "editor" else (SCREEN, SCALE)), work)
    try:
        built = scene.ed.showcase_build()
        print(f"  {name}: {built.get('note', 'the scene is built')}", flush=True)
        time.sleep(2.0)  # the formulas are typeset and drawn
        for gone in (name, "window", "reveal") if name == "camera" else (name,):
            shutil.rmtree(os.path.join(args.out, gone), ignore_errors=True)
        film(scene, os.path.join(args.out, name), name == "camera", args.fps)
    finally:
        try:
            scene.ed.app_quit()
            editor.wait(timeout=10)
        except Exception:
            editor.kill()
        shutil.rmtree(work, ignore_errors=True)


def draw_back(args, run):
    """The end: the window's pictures, each cut round the film's frame and widened towards the whole window - from the
    camera's frame (the middle of the view at its full height, what `frame` in cut keeps) to the window. The frame widens
    by the same ratio every step, about the one point that stays where it is, slow at both ends. Answers the first frame
    it covers; None when the camera's pass left no window."""
    window, reveal = os.path.join(args.out, "window"), os.path.join(args.out, "reveal")
    if not os.path.exists(os.path.join(window, "view.json")):
        return None
    with open(os.path.join(window, "view.json")) as kept:
        shot = json.load(kept)
    view, width, height = shot["view"], shot["width"], shot["height"]
    h0 = view["height"]
    w0 = h0 * FILM[0] / FILM[1]
    x0, y0 = view["x"] + (view["width"] - w0) / 2, view["y"]
    ratio = width / w0  # the window is of the film's shape: the frame grows by it to fill the window
    still = (x0 * ratio / (ratio - 1), y0 * ratio / (ratio - 1))
    os.makedirs(reveal, exist_ok=True)
    frames = sorted(int(f[:-4]) for f in os.listdir(window) if f.endswith(".png"))
    for n in frames:
        s = min(1.0, max(0.0, (n / args.fps - REVEAL[0]) / (REVEAL[1] - REVEAL[0])))
        grow = ratio ** (s * s * s * (s * (6 * s - 15) + 10))
        k = FILM[0] / (w0 * grow)  # the window scaled so the frame is the film's size
        x, y = still[0] + (x0 - still[0]) * grow, still[1] + (y0 - still[1]) * grow
        run("-i", os.path.join(window, f"{n:05d}.png"), "-vf",
            f"scale={round(width * k)}:{round(height * k)}:flags=lanczos,crop={FILM[0]}:{FILM[1]}:{round(x * k)}:{round(y * k)}",
            os.path.join(reveal, f"{n:05d}.png"))
    return frames[0]


def cut(args):
    """The frames into the film: the camera's pass full frame, the editor's window in its corner while the story runs,
    the camera drawn back out of the window at the end; and the two moments of the story - the queen twirled, the tower
    broken - as a GIF for the README."""
    camera, editor = (os.path.join(args.out, name, "%05d.png") for name in ("camera", "editor"))
    run = lambda *words: subprocess.run(["ffmpeg", "-y", "-loglevel", "error", *words], check=True)  # noqa: E731
    # The frame as the editor drew it, fitted to the film's size: nothing is done to the picture - what is seen is what
    # the editor shows whoever opens the scene.
    frame = f"scale=-2:{FILM[1]}:flags=lanczos,crop={FILM[0]}:{FILM[1]}"
    encode = ("-c:v", "libx264", "-preset", "slow", "-crf", "16", "-pix_fmt", "yuv420p")
    whole = os.path.join(args.out, "kinetica-showcase.mp4")
    clean = os.path.join(args.out, "kinetica-showcase-camera.mp4")
    run("-framerate", str(args.fps), "-i", camera, "-vf", frame, *encode, clean)
    back = draw_back(args, run)
    inputs, graph = ["-framerate", str(args.fps), "-i", camera], f"[0:v]{frame}[main]"
    if back is not None:  # the camera's frames to the end's first, then the end's own
        inputs += ["-framerate", str(args.fps), "-start_number", str(back), "-i", os.path.join(args.out, "reveal", "%05d.png")]
        graph = f"[0:v]trim=end_frame={back},{frame},setsar=1[story];[1:v]setsar=1[end];[story][end]concat=n=2:v=1:a=0[main]"
    if os.path.isdir(os.path.dirname(editor)) and os.listdir(os.path.dirname(editor)):
        inset = "scale=608:-2:flags=lanczos,pad=iw+6:ih+6:3:3:color=0xE8ECF4"
        corner = inputs.count("-i")  # the editor's pass is the input after the camera's
        run(*inputs, "-framerate", str(args.fps), "-i", editor, "-filter_complex",
            f"{graph};[{corner}:v]{inset}[small];[main][small]overlay=W-w-40:40:enable='between(t,{INSET[0]},{INSET[1]})'", *encode, "-r", str(args.fps), whole)
        run("-framerate", str(args.fps), "-i", editor, "-vf", "scale=1600:-2", *encode, os.path.join(args.out, "kinetica-showcase-editor.mp4"))
    else:
        run(*inputs, "-filter_complex", graph, "-map", "[main]", *encode, whole)
    # The page's copy: 720 lines, light enough to start at once on the documentation's first page.
    run("-i", whole, "-vf", "scale=1280:720:flags=lanczos", "-c:v", "libx264", "-preset", "slow", "-crf", "28", "-pix_fmt", "yuv420p",
        "-movflags", "+faststart", "-an", os.path.join(args.out, "kinetica-showcase-720.mp4"))
    moments = "+".join(f"between(t,{a},{b})" for a, b in MOMENTS)
    small = f"select='{moments}',setpts=N/FRAME_RATE/TB,fps=15,scale=600:-2:flags=lanczos"  # under 5 MB: a README's picture is loaded by everyone who opens it
    run("-i", clean, "-filter_complex", f"[0:v]{small},split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4",
        os.path.join(args.out, "kinetica-showcase.gif"))
    for name in ("kinetica-showcase.mp4", "kinetica-showcase-camera.mp4", "kinetica-showcase-editor.mp4", "kinetica-showcase-720.mp4", "kinetica-showcase.gif"):
        file = os.path.join(args.out, name)
        if os.path.exists(file):
            print(f"  {file}: {os.path.getsize(file) / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
