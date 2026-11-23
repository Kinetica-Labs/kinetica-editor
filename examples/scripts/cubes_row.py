"""A row of cubes in front of the robot - a scene made by code.

A script builds or changes the scene and ends; everything it did is one step of Ctrl+Z. Run it from the editor
(«Файл → Выполнить скрипт…»), with the editor's start:

    build/kinetica --scene examples/scenes/ur5e.kscene --script examples/scripts/cubes_row.py

or from a terminal beside an editor started with --api (python3 examples/scripts/cubes_row.py, PYTHONPATH=python).
The same words work line by line in the editor's API console.
"""
import kinetica as ke

COUNT = 5     # how many cubes
GAP = 0.12    # between their centres, m
SIDE = 0.05   # a cube's edge, m
FLOOR = 0.02  # the top of the UR5e scene's floor plate, m

scene = ke.connect()
for k in range(COUNT):
    scene.add("box", size=(SIDE, SIDE, SIDE), position=(0.45, FLOOR + SIDE / 2, (k - (COUNT - 1) / 2) * GAP), role="rigid")
print("cubes placed:", COUNT)
