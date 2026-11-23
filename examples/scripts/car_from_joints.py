"""A car from primitives and joints - every part by hand, nothing ready-made: a chassis, four hubs on shock absorbers,
four wheels on hinges with velocity motors. The springs share the chassis by themselves (assembly_joint type shock: a
quarter each, 4 Hz, a sag of 1.6 cm); Play drops it onto its springs and it drives, the camera following.

    build/kinetica --script examples/scripts/car_from_joints.py
"""
import kinetica as ke

SPEED = 5.0  # the wheels' turn, rad/s: 0.5 m/s on 0.1 m wheels

scene = ke.connect()
ed = scene.ed
scene.new()
ed.view_look(eye=[1.4, 0.9, 1.5], target=[0, 0.2, 0])
chassis = ed.scene_add(shape="box", name="Шасси", position=[0, 0.32, 0], size=[0.8, 0.06, 0.36], role="rigid")["id"]
ed.scene_set(id=chassis, line="rigid", key="density", values=[200])
for x in (0.3, -0.3):
    for side in (1, -1):
        hub = ed.scene_add(shape="box", name="Ступица", position=[x, 0.32, side * 0.21], size=[0.06, 0.06, 0.06], role="rigid")["id"]
        ed.assembly_joint(a=hub, b=chassis, type="shock", anchor=[x, 0.32, side * 0.18], axis=[0, 1, 0])
        wheel = ed.scene_add(shape="cylinder", name="Колесо", position=[x, 0.32, side * 0.265], size=[0.2, 0.05, 0.2], role="rigid")["id"]
        ed.scene_turn(ids=[wheel], axis="x", degrees=90)  # a cylinder's axis is its y: the axle along z
        axle = ed.assembly_joint(a=wheel, b=hub, type="hinge", anchor=[x, 0.32, side * 0.24], axis=[0, 0, 1])["id"]
        ed.assembly_drive(joint=axle, kind="velocity", velocity=-SPEED, force=3)  # about -z: forward, +x
ed.selection_set(ids=[])
ed.view_follow(body=chassis)
ed.sim_play()
print(f"машинка: шасси, 4 ступицы на амортизаторах, 4 колеса на шарнирах с моторами - едет {SPEED * 0.1:.1f} м/с")
