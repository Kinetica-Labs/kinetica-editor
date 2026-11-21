"""The eighth rung: a drone - a whole robot written in code: an airframe, four thrusts, two sensors and a flight
controller that takes off, hovers, and levels itself after a knock.

You get every rung before this one at work together. The airframe is one body; each rotor is a thrust applied to it
at the end of an arm (a force of the body's own: it rides and turns with the frame). The controller reads only its
sensors - an IMU's gyroscope for the tilt, a sonar looking down for the height - and commands the four thrusts sixty
times a second. Three things a real flight controller does are here because the sensors are not ideal: the gyroscope's
zero is measured on the ground before the take-off, the height's last good reading is kept when the tilted sonar
loses its echo, and the thrust is limited. Run it from the repository's root:

    python3 examples/python/08_drone.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "python"))
import kinetica as ke  # noqa: E402

ARM, SIZE = 0.15, (0.3, 0.06, 0.3)  # a rotor's distance from the centre; the frame, m
HOVER_AT, KNOCK_AT, FLIGHT = 1.0, 3.0, 6.0  # m; the knock's moment and the flight's length, s
DT = 1 / 60


def build(scene):
    """The airframe on the floor: four thrusts at the ends of a plus, an IMU on its top, a sonar looking down, and a
    knock - an impulse under one arm - that will hit it in flight."""
    frame = scene.add("box", name="Frame", position=(0, 0.05, 0), size=SIZE, role="rigid")
    rotors = {name: scene.add_force("directional", "constant", body=frame, at=(x, 0.05, z), strength=0)
              for name, (x, z) in {"+x": (ARM, 0), "-x": (-ARM, 0), "+z": (0, ARM), "-z": (0, -ARM)}.items()}
    imu = frame.add_imu(face="+y", model="ICM-42688-P")  # its z is the frame's up, its x the frame's z, its y the frame's x
    sonar = frame.add_sonar(face="-y")
    knock = scene.add_force("directional", "impulse", body=frame, at=(ARM, 0.05, 0), strength=0.3)
    knock.start = KNOCK_AT
    return frame, rotors, imu, sonar


def gyro_zero(scene, imu, frames=30):
    """The gyroscope's own zero, measured while the drone stands still: what it reads then is all error."""
    zero = [0.0, 0.0, 0.0]
    for _ in range(frames):
        scene.step(1)
        zero = [z + rate / frames for z, rate in zip(zero, imu.gyro)]
    return zero


def fly(scene, frame, rotors, imu, sonar):
    """Take off and hold HOVER_AT: the height by the sonar, the tilt by the gyroscope's rates added up. Returns the
    tilt as the controller counted it, and - for the report only, the controller never reads them - the height and the
    place the scene itself says."""
    mass = SIZE[0] * SIZE[1] * SIZE[2] * 500  # the frame's own data: its volume at 500 kg/m^3
    inertia = mass * (SIZE[0] ** 2 + SIZE[1] ** 2) / 12  # about either arm
    zero = gyro_zero(scene, imu)
    tilt_x = tilt_z = height = climb = 0.0
    flight = {"worst_tilt": 0.0}
    for step in range(round((FLIGHT - 0.5) / DT)):
        rate_z, rate_x, _ = (rate - z for rate, z in zip(imu.gyro, zero))  # the chip's x is the frame's z, its y the frame's x
        tilt_x, tilt_z = tilt_x + rate_x * DT, tilt_z + rate_z * DT
        echo = sonar.distance
        if echo is not None and echo > 0:  # tilted, the sonar loses its echo (-1): the last good height stays
            seen = echo + SIZE[1] / 2
            climb += 0.3 * ((seen - height) / DT - climb)
            height = seen
        lift = mass * min(2 * 9.81, max(0.0, 9.81 + 4.0 * (HOVER_AT - height) - 3.0 * climb))
        turn_x = -inertia * (64 * tilt_x + 11 * rate_x)  # a torque that brings the tilt back to zero
        turn_z = -inertia * (64 * tilt_z + 11 * rate_z)
        thrust = {"+x": lift / 4 + turn_z / (2 * ARM), "-x": lift / 4 - turn_z / (2 * ARM),
                  "-z": lift / 4 + turn_x / (2 * ARM), "+z": lift / 4 - turn_x / (2 * ARM)}
        scene.command({rotors[name]: {"strength": max(0.0, newtons)} for name, newtons in thrust.items()})
        scene.step(1)
        if step * DT + 0.5 > KNOCK_AT:
            flight["worst_tilt"] = max(flight["worst_tilt"], math.degrees(math.hypot(tilt_x, tilt_z)))
    flight["tilt"] = math.degrees(math.hypot(tilt_x, tilt_z))
    truth = scene.measure(objects=[frame])
    flight["height"] = truth[f"{frame.id}/height"]
    flight["drift"] = math.hypot(truth[f"{frame.id}/centre x"], truth[f"{frame.id}/centre z"])
    return flight


with ke.Scene() as scene:
    frame, rotors, imu, sonar = build(scene)
    scene.step(1)
    flight = fly(scene, frame, rotors, imu, sonar)
    print(f"the drone took off and hovers at {flight['height']:.2f} m (asked: {HOVER_AT:.2f}); a knock at {KNOCK_AT:.0f} s tilted it "
          f"{flight['worst_tilt']:.1f} degrees, and at {FLIGHT:.0f} s it is level within {flight['tilt']:.1f}; "
          f"it has drifted {flight['drift']:.1f} m aside - nothing aboard measures its place over the floor")
