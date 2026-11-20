"""PLUGIN_NAME - a robot's controller (Плагины → Новый плагин… → «Контроллер робота» made this file).

While the scene runs (▶), on_tick is called every frame before the next one is stepped - the simulation waits for it. It
gets what the sensors read, as the robot reads them (never the truth), and answers commands: a joint's target, a
driver's duty. Commands change nothing of the scene: Стоп brings it back as it was. Save the file: the plugin restarts
within a second; print() goes to the editor's API console.
"""
import math

from kinetica.plugin import on_play, on_tick

servos = []  # the joints this controller drives
t = 0.0


@on_play
def start(scene):
    """▶: every joint with a servo is ours."""
    global servos, t
    servos, t = [j.id for j in scene.joints if j.servo], 0.0
    print("ведём суставов:", len(servos))


@on_tick
def control(scene, dt, sensors):
    """Every frame: a slow wave on every servo, ±0.3 rad - replace it with your control law."""
    global t
    t += dt
    return {joint: {"target": 0.3 * math.sin(t + k)} for k, joint in enumerate(servos)}
