"""The seventh rung: an arm takes a cube - the example scene of the UR5e, led by its inverse-kinematics goal.

You get a whole robot opened from a file and driven by code: the arm's goal is put over the yellow cube, lowered
onto it, the fingers close until they meet the cube, and the goal goes up - the arm follows it by inverse kinematics
and its servos carry the cube. Run it from the repository's root:

    python3 examples/python/07_arm.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "clients", "python"))
import kinetica as ke  # noqa: E402

OVER, ONTO, UP = 0.25, 0.115, 0.30  # the hand's height over the floor: above the cube, around it, lifted; m

with ke.Scene() as scene:
    scene.open(os.path.join(HERE, "..", "scenes", "ur5e.kscene"))
    goal, cube = scene.objects["Цель «Схват»"], scene.objects["Кубик жёлтый"]
    scene.run(0.5)
    at = scene.measure(objects=[cube])
    x, z = at[f"{cube.id}/centre x"], at[f"{cube.id}/centre z"]
    for height, seconds in ((OVER, 2.5), (ONTO, 2.0)):
        goal.pose(position=(x, height, z))  # the arm leads its hand there in a straight line
        scene.run(seconds)
    miss = scene.measure(goals=[goal])[f"{goal.id}/miss"]
    goal.grip()
    scene.run(1.0)
    held = goal.gripped
    goal.pose(position=(x, UP, z))
    scene.run(2.0)
    lifted = scene.measure(objects=[cube])[f"{cube.id}/height"]
    print(f"the hand came within {miss:.1f} mm of its goal, its fingers are {held}, and the cube is lifted from 0.040 to {lifted:.3f} m")
