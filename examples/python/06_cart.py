"""The sixth rung: a cart drives up to a wall and stops by its range finder - a robot's control loop.

You get a robot assembled by code - a chassis on four motor wheels, an ultrasonic range finder on its front - and a
loop that closes through a sensor: ten times a second it reads the distance and sets the motors' duty in proportion
to what is left to the stopping place, 0.40 m before the wall. It never asks the scene where the cart is. Run it
from the repository's root:

    python3 examples/python/06_cart.py
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "clients", "python"))
import kinetica as ke  # noqa: E402

STOP_AT, GAIN = 0.40, 4.0  # m before the wall; duty per metre left (the duty is held within -1..1)

with ke.Scene() as scene:
    car = scene.add("box", name="Chassis", size=(0.4, 0.08, 0.26), position=(0, 0.14, 0), role="rigid")
    wheels = [car.add_wheel(side) for side in ("front_left", "front_right", "rear_left", "rear_right")]
    sonar = car.add_sonar(face="+x")
    scene.add("box", name="Wall", size=(0.1, 0.5, 1.2), position=(1.45, 0.25, 0), role="collider")  # its near face 1.2 m from the sonar
    scene.step(1)
    first = sonar.distance
    for _ in range(120):  # 12 s, a decision every 0.1 s: the geared wheels give 0.16 m/s at full duty
        left = sonar.distance - STOP_AT
        duty = max(-1.0, min(1.0, GAIN * left))
        scene.command({wheel.driver: {"duty": duty} for wheel in wheels})
        scene.step(6)
    truth = 1.45 - 0.05 - (scene.measure(objects=[car])[f"{car.id}/centre x"] + 0.2)  # the wall's face to the cart's front
    print(f"the range finder read {first:.2f} m at the start; the cart stopped {sonar.distance:.2f} m from the wall by its reading, "
          f"{truth:.2f} m in truth (asked: {STOP_AT:.2f} - near the goal a proportional loop's duty is too small to turn the wheels)")
