"""The fourth rung: a motor - a servo on a hinge is told an angle, and an encoder says where the arm is.

You get the robot's own loop in its smallest form: a command goes to a drive, a sensor's reading comes back. The
encoder counts steps (500 lines: 2000 a turn), so its angle is the truth rounded to 0.18 degrees. Run it from the
repository's root:

    python3 examples/python/04_motor.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "python"))
import kinetica as ke  # noqa: E402

with ke.Scene() as scene:
    arm = scene.add("box", position=(0.25, 1, 0), size=(0.5, 0.05, 0.05), role="rigid")
    shoulder = scene.add_joint(arm, kind="hinge", anchor=(0, 1, 0), axis=(0, 0, 1))  # the arm's end on the world
    shoulder.drive("servo", stiffness=200, damping=20, target=0)
    encoder = shoulder.add_encoder(lines=500, reference=0)
    scene.run(0.5)  # the world is built and runs: a command goes to a running world, it is not an edit of the scene
    for degrees in (30, -45):
        scene.command({shoulder: {"target": math.radians(degrees)}})
        scene.run(1.5)
        truth = scene.measure(joints=[shoulder])[f"{shoulder.id}/rx coordinate"]  # a hinge turns about its own x
        print(f"told {degrees} degrees: the encoder reads {math.degrees(encoder.angle):.2f}, the joint is at {math.degrees(truth):.2f}")
