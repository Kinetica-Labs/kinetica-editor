"""The ladder of examples (examples/python/NN_*.py): each is run as a newcomer runs it - from the repository's root,
by its file - and the numbers it prints are held to what its lesson says (a period to the formula, a velocity to the
force, a sensor's reading to the truth within its datasheet). No window; needs build/kinetica-core."""
import os
import re
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "python"))
from kinetica.core import find_core  # noqa: E402


def built():
    try:
        return bool(find_core())
    except FileNotFoundError:
        return False


def said(name):
    """What the example prints, line by line, and the numbers of each line."""
    out = subprocess.run([sys.executable, os.path.join("examples", "python", name)], cwd=ROOT, capture_output=True, text=True, timeout=120)
    if out.returncode != 0:
        raise AssertionError(f"{name} ended with {out.returncode}:\n{out.stderr}")
    lines = out.stdout.strip().splitlines()
    return lines, [[float(x) for x in re.findall(r"-?\d+(?:\.\d+)?", line)] for line in lines]


@unittest.skipUnless(built(), "build/kinetica-core is not built; run: cmake --build build --target kinetica-core")
class Examples(unittest.TestCase):
    def test_01_hello(self):
        lines, numbers = said("01_hello.py")
        self.assertIn("m high", lines[0])
        self.assertAlmostEqual(numbers[0][-1], 0.12, delta=0.005)  # a 0.2 m box resting on the floor

    def test_02_pendulum(self):
        _, numbers = said("02_pendulum.py")
        period, formula = numbers[0]
        self.assertAlmostEqual(period, formula, delta=0.01 * formula)

    def test_03_forces(self):
        _, numbers = said("03_forces.py")
        strength, mass, speed = numbers[0]
        self.assertAlmostEqual(speed, 3.0, delta=0.02)  # a metre of arrow: 3 m/s, whatever the crate weighs
        self.assertAlmostEqual(strength, 3 * mass, delta=1)
        near, far, outside = numbers[1][2], numbers[1][4], numbers[1][6]
        self.assertAlmostEqual(near, 5.0, delta=0.05)   # 6 m/s fading linearly to the 3 m edge
        self.assertAlmostEqual(far, 2.0, delta=0.05)
        self.assertEqual(outside, 0.0)

    def test_04_motor(self):
        _, numbers = said("04_motor.py")
        for told, encoder, joint in numbers:
            self.assertAlmostEqual(joint, told, delta=1.0)      # the servo holds within a degree, sagging under the arm's weight
            self.assertAlmostEqual(encoder, joint, delta=0.2)   # 2000 counts a turn: 0.18 degrees a count

    def test_05_sensors(self):
        lines, numbers = said("05_sensors.py")
        up, spread = numbers[0][1], numbers[0][3]  # after the chip's name MPU-6050 (one number of its own)
        self.assertIn("IMU", lines[0])
        self.assertAlmostEqual(up, 9.81, delta=0.8)   # offset 50 mg and scale error 3%: within the datasheet
        self.assertGreater(spread, 0.0)
        self.assertLess(spread, 0.3)
        self.assertAlmostEqual(numbers[1][-3], 1.45, delta=0.01)  # the sonar's mean; its accuracy is 3 mm


if __name__ == "__main__":
    unittest.main()
