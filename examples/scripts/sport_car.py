"""A sports car from primitives and joints - every part by hand, nothing ready-made - driving a circle.

A low wide chassis with a cabin and a rear wing welded on. Each corner: a hub on a shock absorber (assembly_joint type
shock: the springs share the chassis, a quarter each, 4 Hz). At the front a steering knuckle turns on the hub about a
vertical kingpin - a servo holds its angle - and the wheel turns on the knuckle; at the rear the wheel turns on the hub,
driven by a velocity motor. The front angles are Ackermann's: each wheel square to its line to the circle's centre, the
inner one turned more, so both roll on their own circles. The rear wheels are driven by DC motors made by the
motors plugin (K and R from the magnet's field, the winding and the wire), fed by one battery through two H-bridges: the
energy is the pack's - its voltage sags under load, its charge runs down, the motors heat. Play: it settles on its springs and drives round, the camera
following - or, with the brain plugin, stands on the panel «Пульт машинки»: the arrows or a game controller's sticks
drive it (teleop plugin), its stabiliser holding the turn and the speed.

    build/kinetica --script examples/scripts/sport_car.py
"""
import math

import kinetica as ke

RADIUS = 1.5     # the circle the rear axle's middle drives on, m (to the left)
DUTY = 0.25      # the rear motors' duty with no hand on them: about 1 m/s on 0.1 m wheels
BASE = 0.7       # front axle to rear axle, m
TRACK = 0.59     # left wheel to right wheel, m
Y = 0.3          # where the axles are made; they settle at the wheels' radius

scene = ke.connect()
ed = scene.ed
scene.new()
ed.view_look(eye=[1.6, 1.1, 1.8], target=[0, 0.2, 0])


def box(name, at, size, **more):
    return ed.scene_add(shape="box", name=name, position=list(at), size=list(size), role="rigid", **more)["id"]


chassis = box("Шасси", (0, Y, 0), (1.0, 0.05, 0.4))
ed.scene_set(id=chassis, line="rigid", key="density", values=[250])
ed.assembly_sensor(target=chassis, kind="imu", face="+y")  # flat on the roof, z up: its gyroscope reads the turn rate
for name, at, size in (("Кабина", (-0.08, Y + 0.075, 0), (0.4, 0.1, 0.3)), ("Антикрыло", (-0.47, Y + 0.13, 0), (0.06, 0.02, 0.42)),
                       ("Стойка крыла", (-0.47, Y + 0.075, 0), (0.03, 0.09, 0.03))):
    part = box(name, at, size)
    ed.assembly_joint(a=part, b=chassis, type="weld", anchor=list(at))

# Ackermann: the front wheels' turns about +y (positive: towards -z, the left) for a circle about a centre left of the rear axle.
rear = []
inner = math.atan(BASE / (RADIUS - TRACK / 2))
outer = math.atan(BASE / (RADIUS + TRACK / 2))
for x in (BASE / 2, -BASE / 2):
    front = x > 0
    for side in (1, -1):  # +z right, -z left
        z = side * 0.23
        where = ("П" if front else "З") + ("П" if side > 0 else "Л")  # front/rear, right/left: «Колесо ПЛ» - front left
        hub = box(f"Ступица {where}", (x, Y, z), (0.06, 0.06, 0.06))
        ed.assembly_joint(a=hub, b=chassis, type="shock", anchor=[x, Y, side * 0.2], axis=[0, 1, 0])
        carrier = hub
        if front:  # the knuckle on its kingpin, held at its angle by a servo
            knuckle = box(f"Кулак {where}", (x, Y, side * 0.275), (0.04, 0.04, 0.03))
            king = ed.assembly_joint(a=knuckle, b=hub, type="hinge", anchor=[x, Y, side * 0.26], axis=[0, 1, 0])["id"]
            angle = inner if side < 0 else outer
            ed.assembly_drive(joint=king, kind="servo", stiffness=40, damping=2, target=angle)
            carrier = knuckle
        wheel = ed.scene_add(shape="cylinder", name=f"Колесо {where}", position=[x, Y, side * 0.32], size=[0.2, 0.06, 0.2], role="rigid")["id"]
        ed.scene_turn(ids=[wheel], axis="x", degrees=90)  # a cylinder's axis is its y: the axle along z
        axle = ed.assembly_joint(a=wheel, b=carrier, type="hinge", anchor=[x, Y, side * 0.29], axis=[0, 0, 1])["id"]
        if not front:  # a DC motor through a 36:1 gearbox, its K and R from its make (the motors plugin: Lorentz, Faraday, Ohm)
            try:  # negative: about -z, forward +x
                ed.motors_make(joint=axle, voltage=-11.1)
            except ke.OperationError:  # no motors plugin: the numbers it gives for its default make (60 turns, 0.6 mm, 36:1)
                ed.assembly_drive(joint=axle, kind="dcmotor", constant=0.285768, resistance=0.132674, voltage=-11.1)
            rear.append(axle)

# One pack for both: 3S3P of 18650 cells (11.1 V, 7.5 A h), each motor on its own H-bridge with a 15 A limit, an ESC's.
pack = None
for axle in rear:
    driver = ed.assembly_power(joint=axle, **({"battery": pack} if pack else {}))["id"]
    pack = pack or next(o["id"] for o in ed.scene_list(kind="battery")["objects"])
    ed.scene_set(id=driver, line="bridge", key="limit", values=[15])
    ed.scene_set(id=driver, line="bridge", key="duty", values=[-DUTY])  # the circle, with no hand on it
ed.scene_set(id=pack, line="cells", key="parallel", values=[3])

# The driver's panel, as a game engine's input actions: steer and throttle are values only (drive value) - the brain
# plugin's stabiliser reads them in its tick and holds the turn rate (1.5 rad/s at full) and the speed (2 m/s); the
# keys are the editor's, a game controller the teleop plugin's (left stick up/down: speed, right stick left/right: turn;
# ZR/RT throttle, ZL/LT brake and reverse).
ed.scene_put(block=f"""controller "Пульт машинки"
  action steer axis drive value target {chassis} speed 1.5
  action throttle axis drive value target {chassis} speed 2
  bind steer keys Right Left
  bind steer gamepad right_x
  bind throttle keys Up Down
  bind throttle gamepad left_y
  bind throttle gamepad right_trigger
  bind throttle gamepad left_trigger invert
end""")

ed.selection_set(ids=[])
ed.view_follow(body=chassis)
ed.sim_play()
print(f"спортивная машинка: поворот передних {math.degrees(inner):.0f}° и {math.degrees(outer):.0f}° (Аккерман), круг {RADIUS} м, "
      f"задний привод - моторы постоянного тока от батареи 3S3P, скважность {DUTY}")
