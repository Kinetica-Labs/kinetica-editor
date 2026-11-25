"""The fifth rung: sensors - what a robot reads is not the truth: it has noise, steps and a limit.

You get an IMU and a sonar on a box that stands in front of a wall. The IMU's accelerometer reads the floor's push
up, 9.81 m/s^2, with its chip's noise, offset and scale error; the sonar reads the distance to the wall. A hundred
readings of each give a mean and a spread; the true numbers are the scene's own, which a robot's control never sees. Run it from the
repository's root:

    python3 examples/python/05_sensors.py
"""
import os
import statistics
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "clients", "python"))
import kinetica as ke  # noqa: E402

with ke.Scene() as scene:
    box = scene.add("box", position=(0, 0.1, 0), role="rigid")  # a cube of 0.2 m standing on the floor (its top at y = 0)
    scene.add("box", position=(1.6, 0.5, 0), size=(0.1, 1, 1), role="collider")  # a wall: its near face at x = 1.55
    imu = box.add_imu(face="+y")  # flat on the top: its z points up
    sonar = box.add_sonar(face="+x")  # on the front face, at x = 0.1, looking at the wall
    scene.run(1.0)  # the box settles
    up, far = [], []
    for _ in range(100):
        scene.step(1)
        up.append(imu.accel[2])
        far.append(sonar.distance)
    offset = imu.spec["accel_offset_mg"] * 9.81e-3  # the chip's own zero error, from its datasheet
    print(f"the IMU ({imu.model}) reads {statistics.mean(up):.2f} m/s^2 up, spread {statistics.stdev(up):.3f}; "
          f"the truth is 9.81 - its datasheet allows an offset of {offset:.2f} and a scale error of {imu.spec['scale_error_pct']:.0f}%")
    print(f"the sonar ({sonar.model}) reads {statistics.mean(far):.3f} m to the wall, spread {statistics.stdev(far):.4f}; the truth is 1.450")
