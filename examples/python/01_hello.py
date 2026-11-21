"""The first rung: a box falls on the floor and its height is printed.

You get a scene built and stepped by code with no window; it takes a fraction of a second. Run it from the
repository's root, after building kinetica-core (cmake --build build --target kinetica-core):

    python3 examples/python/01_hello.py
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "python"))
import kinetica as ke  # noqa: E402

with ke.Scene() as scene:
    box = scene.add("box", position=(0, 1, 0), role="rigid")
    scene.run(1.0)
    print(f"after 1 s the box is {scene.measure(objects=[box])[f'{box.id}/height']:.3f} m high")
