"""The UR5e and a pyramid of cubes beside it - the example scene opened and built on by code.

Opens examples/ur5e.kscene, stacks a pyramid of rigid cubes in front of the arm, lets the world run for a second and
says how the pyramid stood. Stop brings the scene back as it was built: the cubes stay, their fall is not kept.

    build/kinetica --script examples/scripts/ur5e_cubes.py
"""
import os

import kinetica as ke

BASE = 4       # cubes in the lowest row
SIDE = 0.05    # a cube's edge, m
GAP = 0.004    # between neighbours, m
FLOOR = 0.02   # the top of this scene's floor plate, m
AT = (0.55, 0.0)  # the pyramid's middle on the floor: x, z

scene = ke.connect()
scene.open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ur5e.kscene"))

cubes = []
for level in range(BASE):
    row = BASE - level
    for k in range(row):
        z = AT[1] + (k - (row - 1) / 2) * (SIDE + GAP)
        cubes.append(scene.add("box", size=(SIDE, SIDE, SIDE), position=(AT[0], FLOOR + SIDE / 2 + level * SIDE, z), role="rigid"))
top = cubes[-1]

scene.run(1.0)
height = scene.measure(objects=[top])[f"{top.id}/height"]
scene.stop()
print(f"{len(cubes)} cubes in {BASE} rows; after 1 s the top one stands {height:.3f} m high (built at {FLOOR + SIDE / 2 + (BASE - 1) * SIDE:.3f} m)")
