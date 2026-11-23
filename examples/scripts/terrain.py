"""Ground to drive on, added to the open scene: a static mesh of hills (its collider its own triangles, hollows kept)
and things to drive over or into - a hump, speed bumps, light crates, a pyramid of them and balls. Run again, it puts
them anew in place of its earlier ones (by their names).

The sports car (sport_car.py) is low - 5.5 cm under its chassis, 0.7 m between its axles - so nothing here is steeper
than 8 degrees or higher than 3 cm at an edge: a step or a sharp crest would beach it.

The relief is made here, the same bytes every run, into examples/models/terrain.obj (not kept in git: this script is
its source): 30 x 30 m on a 0.3 m grid, flat within 3 m of the centre - the car stands there - then rolling hills up to
about 1 m, no slope steeper than about 12 degrees, never below the world's floor (y = 0).

    build/kinetica --script examples/scripts/sport_car.py   # then, in its console or another run:
    build/kinetica --script examples/scripts/terrain.py
"""
import math
import os

import kinetica as ke

SIDE, STEP = 30.0, 0.3   # m
HILLS = [(7, 4, 0.9, 2.6), (-6, 7, 0.7, 2.2), (2, -8, 1.0, 3.0), (-9, -5, 0.6, 2.0), (10, -6, 0.5, 1.6), (-3, 11, 0.8, 2.4)]  # x, z, height, width


def height(x, z):
    """The ground's height at (x, z), m: hills and a low swell, eased in from the flat middle."""
    r = math.hypot(x, z)
    ease = 0.0 if r < 3 else 1.0 if r > 6 else (lambda t: t * t * (3 - 2 * t))((r - 3) / 3)
    hills = sum(h * math.exp(-((x - hx) ** 2 + (z - hz) ** 2) / (2 * w * w)) for hx, hz, h, w in HILLS)
    swell = 0.15 * (1 + math.sin(0.45 * x) * math.cos(0.38 * z))
    return ease * (hills + swell)


def write_obj(path):
    """The relief as an OBJ grid; its lowest and highest points."""
    n = int(round(SIDE / STEP))
    rows, low, high = [], float("inf"), float("-inf")
    for i in range(n + 1):
        for k in range(n + 1):
            x, z = -SIDE / 2 + i * STEP, -SIDE / 2 + k * STEP
            y = height(x, z)
            low, high = min(low, y), max(high, y)
            rows.append(f"v {x:.3f} {y:.4f} {z:.3f}")
    at = lambda i, k: i * (n + 1) + k + 1
    for i in range(n):
        for k in range(n):  # two triangles a cell, facing up
            rows.append(f"f {at(i, k)} {at(i, k + 1)} {at(i + 1, k + 1)}")
            rows.append(f"f {at(i, k)} {at(i + 1, k + 1)} {at(i + 1, k)}")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write("# Kinetica: examples/scripts/terrain.py\n" + "\n".join(rows) + "\n")
    return low, high


scene = ke.connect()
ed = scene.ed
if ed.sim_state().get("mode") != "edit":
    ed.sim_stop()

MINE = ("Рельеф", "Горка", "Лежачий полицейский", "Ящик", "Мяч")
old = [o["id"] for o in ed.scene_list()["objects"] if o["name"] in MINE]
if old:
    ed.scene_delete(ids=old)

here = os.path.dirname(os.path.abspath(__file__))
obj = os.path.join(here, "..", "models", "terrain.obj")
low, high = write_obj(obj)
# A mesh is fitted to its size's largest edge about its box's centre: the centre put back where the grid has it.
ed.scene_put(block=f"""entity "Рельеф"
  shape mesh file "{os.path.normpath(obj)}" size {SIDE} {high - low:.4f} {SIDE} position 0 {(low + high) / 2:.4f} 0 rotation 0 0 0 color 0.46 0.56 0.36
  collider kind mesh
end""")


def add(shape, name, at, size, role="rigid"):
    return ed.scene_add(shape=shape, name=name, position=list(at), size=list(size), role=role)["id"]


TILT, LONG, THICK = 8.0, 1.2, 0.06  # the hump's slopes: degrees, length, board thickness
cos, sin = math.cos(math.radians(TILT)), math.sin(math.radians(TILT))
for sign, foot in ((1, 2.5), (-1, 2.5 + 2 * LONG * cos + 0.8)):  # up from x 2.5, a 0.8 m top, down
    top = (foot + sign * LONG / 2 * cos, LONG / 2 * sin)  # the middle of its upper face, its low end on the ground
    slope = add("box", "Горка", (top[0] + sign * THICK / 2 * sin, top[1] - THICK / 2 * cos, 0), (LONG, THICK, 2.0), role="collider")
    ed.scene_turn(ids=[slope], axis="z", degrees=sign * TILT)
rise = LONG * sin
add("box", "Горка", (2.5 + LONG * cos + 0.4, rise - THICK / 2, 0), (0.8, THICK, 2.0), role="collider")
for x in (-2.2, -2.8, -3.4):  # speed bumps across the way back: logs sunk to 3 cm
    bump = add("cylinder", "Лежачий полицейский", (x, 0.0, 0), (0.06, 1.6, 0.06), role="collider")
    ed.scene_turn(ids=[bump], axis="x", degrees=90)
crates = [add("box", "Ящик", (x, 0.12, z), (0.2, 0.2, 0.2)) for x, z in ((1.5, 1.6), (1.8, -1.4), (-1.2, 2.0), (0.4, -2.2), (-1.8, -1.6))]
for row, count in enumerate((3, 2, 1)):  # a pyramid to drive through
    for k in range(count):
        crates.append(add("box", "Ящик", (7.5, 0.11 + row * 0.2, (k - (count - 1) / 2) * 0.22), (0.2, 0.2, 0.2)))
balls = [add("sphere", "Мяч", (x, 0.3, z), (0.3, 0.3, 0.3)) for x, z in ((2.5, 2.5), (-2.5, 2.8), (0.0, 3.5))]
for id in crates + balls:  # light, as a car can push: 0.2 m crates of 150 kg/m³ - 1.2 kg; hollow balls
    ed.scene_set(id=id, line="rigid", key="density", values=[150 if id in crates else 60])

ed.selection_set(ids=[])
ed.sim_play()
print(f"рельеф {SIDE:.0f}×{SIDE:.0f} м, высоты до {high:.2f} м; горка {rise * 100:.0f} см, 3 лежачих полицейских, {len(crates)} ящиков, {len(balls)} мяча")
