"""The second rung: a pendulum - a weight on a hinge - and its period against the formula.

You get a joint made by code and a quantity measured over time: the weight hangs a metre below the hinge, is let go
10 degrees aside, and the time between two passes through the bottom is its period. Run it from the repository's root:

    python3 examples/python/02_pendulum.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "clients", "python"))
import kinetica as ke  # noqa: E402

LENGTH, ASIDE, HINGE = 1.0, math.radians(10), (0, 2, 0)

with ke.Scene() as scene:
    weight = scene.add("sphere", position=(LENGTH * math.sin(ASIDE), HINGE[1] - LENGTH * math.cos(ASIDE), 0), size=(0.1, 0.1, 0.1), role="rigid")
    scene.add_joint(weight, kind="hinge", anchor=HINGE, axis=(0, 0, 1))  # to the world, turning about z

    def x():
        return scene.measure(objects=[weight])[f"{weight.id}/centre x"]

    scene.step(1)
    passes, before = [], x()
    for frame in range(1, 400):  # the moments it passes the bottom going left, between two frames by a straight line
        scene.step(1)
        now = x()
        if before > 0 >= now:
            passes.append((frame + before / (before - now)) / 60)
        before = now
    period = passes[1] - passes[0]
    formula = 2 * math.pi * math.sqrt(LENGTH / 9.81) * (1 + ASIDE ** 2 / 16)
    print(f"the pendulum's period is {period:.3f} s; the formula says {formula:.3f} s")
