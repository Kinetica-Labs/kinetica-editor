"""PLUGIN_NAME - an extension of the physics (Плагины → Новый плагин… → «Физика мира» made this file): a gas, water,
wind - anything the engine does not do itself.

While the scene runs (▶), on_tick(world=True) is called every frame before the next one is stepped - the simulation
waits for it. It gets the moving bodies as the engine has them (position, velocity, spin, mass) and answers the forces
on them over the next frame. Nothing of the scene changes: Стоп brings it back as it was. Save the file: the plugin
restarts within a second; print() goes to the editor's API console.
"""
from kinetica.plugin import on_tick

WIND = (1.5, 0.0, 0.0)  # m/s: the air's velocity
DRAG = 0.8              # N per m/s of the body against the air - replace it with your model


@on_tick(world=True)
def wind(scene, dt, bodies):
    """Every frame: a drag toward the wind's velocity on every moving body."""
    forces = {}
    for body, state in bodies.items():
        v = state["velocity"]
        forces[body] = {"force": [DRAG * (WIND[k] - v[k]) for k in range(3)]}
    return forces
