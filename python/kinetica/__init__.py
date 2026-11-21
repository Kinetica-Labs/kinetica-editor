"""Kinetica from Python as objects - a scene, its objects and their parts read as attributes::

    import kinetica as ke

    scene = ke.connect()                          # the editor started with --api (port 47801, KE_EDITOR_PORT)
    scene = ke.Scene()                            # or no window at all: the program kinetica-core (KE_CORE names it)
    car = scene.objects["Шасси"]                  # a body, by name or id
    sonar = car.add_sonar(face="+x")              # an ultrasonic range finder looking out of the front face
    wheel = car.attach("motor_wheel", face="+z")[0]
    encoder = wheel.joint.add_encoder(lines=500)
    driver = wheel.joint.power()                  # its DC motor fed from a battery
    driver.duty = 0.5
    scene.world.gravity = (0, -9.81, 0)
    box = scene.add("box")                        # the toolbar's button: "Куб 1" over a free spot
    box.roles.add("rigid"); box.move((0, 0.2, 0)) # the role bar, the gizmo's arrow
    scene.editor.select(box)                      # the window's own state
    scene.run(1.0)
    print(scene.measure(objects=[box]))           # the plots' channels: the engine's truth, for checks
    scene.stop()

Underneath, every call is one of the server's operations (EditorOps), journaled, undoable and shown in
the editor's API console as the line above that made it. Only the Python standard library is used.
"""
from .client import Client, OperationError
from .objects import (Battery, Body, Controller, Driver, Effort, Encoder, Group, Imu, Joint, Link, PointWheel, Roles, SceneObject, Sensor,
                      Sonar, Touch, WheelModule, Wrench)
from .scene import Editor, Gizmo, Scene, View, World, connect

__all__ = ["connect", "Scene", "World", "Client", "OperationError", "SceneObject", "Body", "Link", "Joint", "Sensor", "Encoder", "Imu",
           "Sonar", "Touch", "Wrench", "Effort", "Battery", "Driver", "Controller", "Group", "Roles", "Editor", "Gizmo", "View",
           "WheelModule", "PointWheel"]
