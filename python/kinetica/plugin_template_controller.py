"""PLUGIN_NAME - a robot's controller (Плагины → Новый плагин… → «Контроллер робота» made this file).

While the scene runs (▶), on_tick is called every frame before the next one is stepped - the simulation waits for it. It
gets what the sensors read, as the robot reads them (never the truth), and answers commands: a joint's target, a
driver's duty. Commands change nothing of the scene: Стоп brings it back as it was. Save the file: the plugin restarts
within a second; print() goes to the editor's API console.

This one closes a loop. Every servo joint with an encoder follows a slow wave; a servo is a spring, so the arm's weight
leaves it short of its target - and what the encoder reads short is gathered into the target until the reading is the
wave. A joint with no encoder of a known zero is left alone (Датчик ▾ → Энкодер, or joint.add_encoder(reference=0)).
"""
import math
import os

from kinetica import Encoder
from kinetica.plugin import on_play, on_tick

ENGLISH = os.environ.get("KE_UI_LANGUAGE") == "en"
SWING = 0.3   # rad: how far a joint goes each way
GAIN = 4.0    # 1/s: how fast what the encoder reads short moves the target
encoders = {}  # a servo joint -> the encoder on it
trim = {}      # a joint -> what its target is moved by so far, rad
t = 0.0
said = 0.0


@on_play
def start(scene):
    """▶: every servo joint that has an encoder is ours."""
    global t, said
    t = said = 0.0
    encoders.clear(), trim.clear()
    servos = {joint.id for joint in scene.joints if joint.servo}
    for sensor in scene.sensors:
        if isinstance(sensor, Encoder) and sensor.parent in servos:
            encoders[sensor.parent] = sensor.id
    if ENGLISH:
        print(f"Controlling {len(encoders)} of {len(servos)} servo joints with encoders")
    else:
        print("ведём суставов с энкодером:", len(encoders), "из", len(servos), "с сервоприводом")


@on_tick
def control(scene, dt, sensors):
    """Every frame: the wave, corrected by what each encoder reads - replace it with your control law."""
    global t, said
    t += dt
    commands, worst = {}, 0.0
    for k, (joint, encoder) in enumerate(encoders.items()):
        goal = SWING * math.sin(t + k)
        angle = sensors[encoder]["reading"]  # rad, as its counts say; None: its zero is not known (reference)
        if angle is None:
            continue
        trim[joint] = trim.get(joint, 0.0) + GAIN * (goal - angle) * dt
        worst = max(worst, abs(goal - angle))
        commands[joint] = {"target": goal + trim[joint]}
    if commands and t - said >= 1.0:  # once a second: how far the worst joint is from where it should be
        said = t
        print("tracking error, rad:" if ENGLISH else "отклонение, рад:", round(worst, 4))
    return commands
