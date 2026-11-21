"""The ninth rung: animation - a camera, a lamp and a pusher move by keys while the physics runs.

You get the Sequencer from code. A key is an object's place and turn at a moment; between two keys it goes in a line.
In the window the «◈ Автоключ» button makes the keys as you move things; here each is one call: stand the objects
where they begin and key them at 0 s, move them, key them again at 2 s. Played, a keyed body is led along its track
and pushes what it meets - the crate here is pushed by physics, not by keys; the camera and the lamp only move. Run it
from the repository's root:

    python3 examples/python/09_animation.py
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "python"))
import kinetica as ke  # noqa: E402

with ke.Scene() as scene:
    crate = scene.add("box", name="Crate", position=(0.5, 0.12, 0), role="rigid")
    pusher = scene.add("box", name="Pusher", position=(0, 0.12, 0), size=(0.2, 0.2, 0.4), role="rigid")
    camera = scene.add_camera(eye=(3, 1.5, 3), target=(0.8, 0.2, 0))
    lamp = scene.add_light("point", position=(-1, 1.5, 1))
    scene.key(pusher, camera, lamp, time=0)  # where they begin
    pusher.move((1.2, 0, 0))
    camera.move((-4, 1, 0))
    lamp.move((3, 0, -1))
    scene.key(pusher, camera, lamp, time=2)  # and where they are two seconds later
    scene.seek(1.0)
    halfway = camera.position[0]  # the playhead at 1 s: every keyed object stands halfway
    scene.seek(0)
    scene.run(2.0)  # the sequence plays with the world: the pusher goes along its track and takes the crate with it
    pushed = scene.measure(objects=[crate])[f"{crate.id}/centre x"]
    print(f"at 1 s the camera is halfway, at x = {halfway:.2f} m (from 3.00 to -1.00); after 2 s the pusher has moved the crate "
          f"from 0.50 to {pushed:.2f} m")
