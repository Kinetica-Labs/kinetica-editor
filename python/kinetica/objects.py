"""The things of a scene as objects: a body, a joint, the sensors on them, a battery and its drivers.

An object is a handle - its id and the scene it lives in. Reading a property reads the object's block
(or, for what it measures, the live state of the running scene: None while the scene is not running or the
running world does not report it); setting one is the editor's own edit (``scene_set``), one undo step,
journaled and shown in the API console as the line that made it.
"""
import json



def _q(text):
    return json.dumps(text, ensure_ascii=False)


def _t(v):
    return "(" + ", ".join(repr(x) for x in v) + ")"


def _axis(axis):
    return _q(axis) if isinstance(axis, str) else _t(axis)


class SceneObject:
    kind = "object"

    def __init__(self, scene, id):
        self.scene, self.id = scene, int(id)

    def __repr__(self):
        return f"<{type(self).__name__} {self.id} {self.name!r}>"

    def __eq__(self, other):
        return isinstance(other, SceneObject) and other.id == self.id

    def __hash__(self):
        return self.id

    @property
    def ref(self):
        """The expression that names it in the object model: ``scene.objects["Шасси"]``."""
        return f"scene.objects[{_q(self.name)}]"

    def block(self):
        return self.scene._call("scene_get", id=self.id)["text"]

    @property
    def name(self):
        return self.scene._call("scene_get", id=self.id)["name"]

    @property
    def tags(self):
        """The words it is marked with: ``scene.objects.tagged("target")`` finds by them."""
        return self.scene._call("scene_get", id=self.id)["tags"]

    def tag(self, word, on=True):
        """A tag put on it (on=False: taken off): one word, no spaces. Any object takes one - a shape, a joint, a sensor."""
        self.scene._call("scene_tag", f"{self.ref}.tag({_q(word)}{'' if on else ', False'})", ids=[self.id], tag=word, on=bool(on))

    @property
    def inside(self):
        """What stands inside this zone now (a shape whose collider is a trigger, or a row of such - an array): the
        objects, a row of copies as its array; None when it is no zone or the scene is not running."""
        zone = self.scene._live("triggers", self.id)
        return None if zone is None else [self.scene.objects[i] for i in zone["inside"]]

    def _get(self, line, key, count=1):
        """What `_set` writes, read: the value after `key` on the block's line `line` - `count` of them as a tuple
        (None: the block has no such line or key)."""
        values = self.scene._call("scene_get", id=self.id, line=line, key=key, count=count)["values"]
        return values if values is None else values[0] if count == 1 else tuple(values)

    def _lines(self, name):
        """The block's lines that start with the word `name`, each as its words after it."""
        return [words[1:] for words in self.scene._call("scene_get", id=self.id)["lines"] if words[0] == name]

    def _set(self, line, key, values, py):
        values = values if isinstance(values, (list, tuple)) else [values]
        self.scene._call("scene_set", py, id=self.id, line=line, key=key, values=list(values))

    visible = property(lambda self: self._get("object", "visible") == 1,
                       lambda self, v: self._set("object", "visible", int(bool(v)), f"{self.ref}.visible = {bool(v)}"))
    locked = property(lambda self: self._get("object", "locked") == 1,
                      lambda self, v: self._set("object", "locked", int(bool(v)), f"{self.ref}.locked = {bool(v)}"),
                      doc="A locked object is not picked in the view nor edited by hand")

    def delete(self, keep_children=False):
        """Out of the scene as Del does it: with everything under it, the joints and sensors it holds.
        keep_children: only this object - what stood under it stays where it is, under its parent."""
        if keep_children:
            self.scene._call("scene_remove", f"{self.ref}.delete(keep_children=True)", id=self.id)
        else:
            self.scene.delete(self)

    def duplicate(self):
        """A copy a little above it (Ctrl+D): the copy."""
        return self.scene.duplicate(self)[0]

    def move(self, by):
        """Moved by a step in the world, m - its angles kept (the gizmo's arrows)."""
        self.scene._call("scene_move", f"{self.ref}.move({_t(by)})", ids=[self.id], by=list(by))

    def turn(self, axis, degrees, pivot=None):
        """Turned about an axis ("x", "y", "z" or a vector) by degrees, around its centre or `pivot`."""
        extra = {"pivot": list(pivot)} if pivot is not None else {}
        shown = f", pivot={_t(pivot)}" if pivot is not None else ""
        self.scene._call("scene_turn", f"{self.ref}.turn({_axis(axis)}, {degrees}{shown})", ids=[self.id],
                         axis=axis if isinstance(axis, str) else list(axis), degrees=degrees, **extra)

    def scale(self, by):
        """Scaled by factors along its own axes (or one number for all three); a group's spacing too."""
        self.scene._call("scene_scale", f"{self.ref}.scale({_t(by) if not isinstance(by, (int, float)) else by})", ids=[self.id],
                         by=by if isinstance(by, (int, float)) else list(by))


class Body(SceneObject):
    """A shape with its roles - a rigid body, a collider - what the scene's physics moves."""
    kind = "entity"

    def _shape(self, key, count=3):
        return self._get("shape", key, count)

    position = property(lambda self: self._shape("position"),
                        lambda self, v: self._set("shape", "position", v, f"{self.ref}.position = {tuple(v)}"))
    rotation = property(lambda self: self._shape("rotation"),
                        lambda self, v: self._set("shape", "rotation", v, f"{self.ref}.rotation = {tuple(v)}"))
    size = property(lambda self: self._shape("size"),
                    lambda self, v: self._set("shape", "size", v, f"{self.ref}.size = {tuple(v)}"))
    fixed = property(lambda self: self._get("rigid", "fixed") == 1,
                     lambda self, v: self._set("rigid", "fixed", int(bool(v)), f"{self.ref}.fixed = {bool(v)}"))
    trigger = property(lambda self: self._get("collider", "trigger") == 1,
                       lambda self, v: self.scene._call("scene_collider", f"{self.ref}.trigger = {bool(v)}", id=self.id, trigger=bool(v)),
                       doc="A zone: its collider touches nothing and tells what is inside it (inside)")

    @property
    def ignores(self):
        """The tags of what its collider has no contact with: ``part.ignores = ["robot-cover"]``."""
        return [word for line in self._lines("ignores") for word in line]

    @ignores.setter
    def ignores(self, tags):
        self.scene._call("scene_collider", f"{self.ref}.ignores = {list(tags)!r}", id=self.id, ignores=list(tags))

    opacity = property(lambda self: (lambda v: 1.0 if v is None else v)(self._get("shape", "opacity")),
                       lambda self, v: self._set("shape", "opacity", float(v), f"{self.ref}.opacity = {float(v)}"),
                       doc="How solid it looks: 1 - solid, less - see-through (glass). Its look only")
    mass = property(lambda self: self._get("rigid", "mass") or 0.0,
                    lambda self, v: self._set("rigid", "mass", float(v), f"{self.ref}.mass = {float(v)}"),
                    doc="Its mass given, kg (0: the shape's volume times the density)")
    center_of_mass = property(lambda self: self._get("rigid", "center_of_mass", 3),
                              lambda self, v: self._set("rigid", "center_of_mass", [float(x) for x in v], f"{self.ref}.center_of_mass = {tuple(v)}"),
                              doc="Its centre of mass given, in its own frame, m (None: the shape's own)")
    gravity_scale = property(lambda self: (lambda v: 1.0 if v is None else v)(self._get("rigid", "gravity_scale")),
                             lambda self, v: self._set("rigid", "gravity_scale", float(v), f"{self.ref}.gravity_scale = {float(v)}"),
                             doc="How much of the world's gravity takes it: 1 - as everything, 0 - it floats")
    linear_damping = property(lambda self: self._get("rigid", "linear_damping"),
                              lambda self, v: self._set("rigid", "linear_damping", float(v), f"{self.ref}.linear_damping = {float(v)}"),
                              doc="Its own damping of motion, 1/s (None: the world's)")
    angular_damping = property(lambda self: self._get("rigid", "angular_damping"),
                               lambda self, v: self._set("rigid", "angular_damping", float(v), f"{self.ref}.angular_damping = {float(v)}"),
                               doc="Its own damping of spin, 1/s (None: the world's)")
    kinematic = property(lambda self: self._get("rigid", "kinematic") == 1,
                         lambda self, v: self._set("rigid", "kinematic", int(bool(v)), f"{self.ref}.kinematic = {bool(v)}"),
                         doc="It moves as its velocity and spin say and pushes what it meets; nothing pushes it")

    @property
    def velocity(self):
        """Its rigid body's velocity now, m/s (None: the scene is not running)."""
        shape = self.scene._live("shapes", self.id)
        return tuple(shape["velocity"]) if shape else None

    def add_goal(self):
        """A goal for this effector: a see-through cube at its pose; moving or turning it drives the arm there."""
        return self.scene.objects[self.scene._call("assembly_goal", f"{self.ref}.add_goal()", effector=self.id)["id"]]

    def add_controller(self, preset="arm"):
        """A controller for this IK goal - an abstract panel of the scene: actions move (across the floor), lift, turn and
        grip, bound to W S A D, R F, Z X and Space."""
        return self.scene.objects[self.scene._call("controller_add", f"{self.ref}.add_controller()", goal=self.id, preset=preset)["id"]]

    def follow(self, points, speed=0.1, loop=False, turns=None, quaternions=None, smooth=False, start=None, start_turn=None,
               start_quaternion=None, turn_length=None):
        """This IK goal walks a path while the scene plays: from where it stands (or where its last path ended) through
        `points` (world metres) at `speed` m/s; each point may have a turn - `turns` in Euler degrees or `quaternions`
        (w, x, y, z) - reached by slerp along the stretch; smooth: a camera's flight, a spline through the points and the
        turns by SQUAD. start, start_turn (Euler degrees): the path from there instead - a way re-planned on the walk,
        from where the goal is now (sim_state's goals), or start_quaternion (w, x, y, z); turn_length: metres a radian of turn
        counts in the path's length (a hand turned on the spot takes time). The arm follows by inverse kinematics. Returns
        length and seconds."""
        pts = [[float(v) for v in p] for p in points]
        extra = {}
        if turns is not None: extra["turns"] = [[float(v) for v in t] for t in turns]
        if quaternions is not None: extra["quaternions"] = [[float(v) for v in q] for q in quaternions]
        if smooth: extra["smooth"] = True
        if start is not None: extra["start"] = [float(v) for v in start]
        if start_turn is not None: extra["start_turn"] = [float(v) for v in start_turn]
        if start_quaternion is not None: extra["start_quaternion"] = [float(v) for v in start_quaternion]
        if turn_length is not None: extra["turn_length"] = float(turn_length)
        shown = f"{self.ref}.follow({[tuple(round(v, 4) for v in p) for p in pts]}, speed={speed}" + (", loop=True" if loop else "") + \
                (f", turns={extra['turns']}" if "turns" in extra else "") + (f", quaternions={extra['quaternions']}" if "quaternions" in extra else "") + (", smooth=True" if smooth else "") + ")"
        return self.scene._call("assembly_path", shown, goal=self.id, points=pts, speed=float(speed), loop=bool(loop), **extra)

    def pose(self, position=None, turn=None, quaternion=None):
        """This IK goal put at a pose at once: `position` (world metres) and a turn - `turn` in Euler degrees or a
        `quaternion` (w, x, y, z); what is left out stays. While the scene plays the arm leads its effector there in a
        straight line and its servos hold it: a way to try the arm's IK and how it settles. Returns position and turn."""
        args = {k: [float(x) for x in v] for k, v in (("position", position), ("turn", turn), ("quaternion", quaternion)) if v is not None}
        shown = f"{self.ref}.pose(" + ", ".join(f"{k}={tuple(round(x, 4) for x in v)}" for k, v in args.items()) + ")"
        return self.scene._call("assembly_goal_pose", shown, goal=self.id, **args)

    def reach(self, position, quaternion=None, basis=None, pose=False):
        """Inverse kinematics: this body (an arm's effector) to `position` with a turn - a quaternion (w, x, y, z) or
        a basis, a rotation matrix by rows; none: the point only. The chain's servos get the coordinates as targets
        (pose=True: the links are posed there at once). The answer says whether it reached and the errors left."""
        args = {"effector": self.id, "position": [float(x) for x in position], **({"set": "pose"} if pose else {})}
        shown = [_t(position)]
        if quaternion is not None:
            args["quaternion"], shown = [float(x) for x in quaternion], shown + [f"quaternion={_t(quaternion)}"]
        if basis is not None:
            args["basis"], shown = [[float(x) for x in row] for row in basis], shown + [f"basis={[list(r) for r in basis]!r}"]
        return self.scene._call("assembly_reach", f"{self.ref}.reach({', '.join(shown + (['pose=True'] if pose else []))})", **args)

    def attach(self, preset, face="+x", **options):
        """A ready link on a face (``"motor_wheel"``, ``"hinge"``, ``"shock"``, ``"car"``...): its links."""
        args = "".join(f", {k}={v!r}" for k, v in options.items())
        answer = self.scene._call("assembly_preset", f"{self.ref}.attach({_q(preset)}, face={_q(face)}{args})",
                                  target=self.id, preset=preset, face=face, **options)
        return [Link(self.scene, l["entity"], l["joint"]) for l in answer["links"]]

    def add_wheel(self, side, model="bodies", suspension=True, encoder=500, power=True, battery=None, radius=None, rolling=None):
        """A wheel module at a side of this body - "front_left", "front_right", "rear_left", "rear_right",
        "left", "right" of its own frame (forward +x, left -z): a shock absorber, a motor wheel, an encoder
        and a driver at duty 0 - positive duty drives forward, the encoder counts forward as positive.
        model "point": the same spring, motor and masses as a point wheel - no bodies, several times faster
        (rolling: its rolling resistance coefficient, 0.021 by default - what the wheel of bodies loses)."""
        args = {"target": self.id, "side": side, "model": model, "suspension": suspension, "encoder": encoder, "power": power}
        if rolling is not None:
            args["rolling"] = rolling
        if battery is not None:
            args["battery"] = battery.id
        if radius is not None:
            args["radius"] = radius
        given = {"model": model, "suspension": suspension, "encoder": encoder, "power": power, "rolling": rolling}
        usual = {"model": "bodies", "suspension": True, "encoder": 500, "power": True, "rolling": None}
        shown = "".join(f", {k}={v!r}" for k, v in given.items() if v != usual[k])
        shown += (f", battery={battery.ref}" if battery is not None else "") + (f", radius={radius!r}" if radius is not None else "")
        return WheelModule(self.scene, self.scene._call("assembly_wheel", f"{self.ref}.add_wheel({_q(side)}{shown})", **args))

    def add_sonar(self, face="+x", model=None):
        """An ultrasonic range finder on a face - "+x", "-x", "+y", "-y", "+z", "-z" of the body's own frame (forward
        +x, left -z) - looking out of it. `model`: a row of data/sensors/sonar.csv ("HC-SR04"); none: the table's first."""
        return self._add_sensor("sonar", face, model)

    def add_imu(self, face="+y", model=None):
        """An IMU flat on a face (as add_sonar names them), its z out of it. `model`: a row of data/sensors/imu.csv
        ("MPU-6050", "ICM-42688-P"...); none: the table's first."""
        return self._add_sensor("imu", face, model)

    def add_touch(self, face="-y"):
        """A touch pad on a face (as add_sonar names them): the normal force of the contacts through it [N] -
        a fingertip's pad, a bumper, a load cell under a foot."""
        return self._add_sensor("touch", face)

    def _add_sensor(self, kind, face, model=None):
        args = {"target": self.id, "kind": kind, "face": face}
        if model is not None:
            args["model"] = model
        shown = f", model={_q(model)}" if model is not None else ""
        answer = self.scene._call("assembly_sensor", f"{self.ref}.add_{kind}(face={_q(face)}{shown})", **args)
        return self.scene.objects[answer["id"]]

    @property
    def sensors(self):
        return [s for s in self.scene.sensors if s.parent == self.id]

    @property
    def roles(self):
        """Its components: ``body.roles.add("rigid")`` (a moving body, with a collider), ``"collider" in body.roles``."""
        return Roles(self)


class Roles:
    """The roles of a body, as the role bar shows them: rigid (a body the physics moves; it brings a collider) and
    collider (what it collides with - alone, a fixed obstacle)."""
    WORDS = ("rigid", "collider")

    def __init__(self, body):
        self.body = body

    def _words(self):
        return [w for w in self.WORDS if self.body._lines(w)]

    def __iter__(self):
        return iter(self._words())

    def __contains__(self, word):
        return word in self._words()

    def __repr__(self):
        return f"<Roles {self._words()}>"

    def _switch(self, word, on):
        b = self.body
        b.scene._call("scene_role", f"{b.ref}.roles.{'add' if on else 'remove'}({_q(word)})", ids=[b.id], role=word, on=on)

    def add(self, word):
        self._switch(word, True)

    def remove(self, word):
        self._switch(word, False)


class WheelModule:
    """A wheel module's parts: the wheel and its axle, the shock absorber, the encoder, the driver (a point
    module: the wheel is a PointWheel, no axle or shock absorber)."""

    def __init__(self, scene, answer):
        part = lambda key: scene.objects[answer[key]] if answer.get(key) else None
        self.wheel, self.axle, self.shock = part("wheel"), part("axle"), part("shock")
        self.encoder, self.driver, self.battery = part("encoder"), part("driver"), part("battery")

    def __repr__(self):
        return f"<WheelModule {self.wheel!r}>"

    @property
    def duty(self):
        """Its driver's PWM duty -1..1: positive drives the body forward."""
        return self.driver.duty

    @duty.setter
    def duty(self, value):
        self.driver.duty = value


class Link:
    """A link a preset put: the new body and the joint that holds it."""

    def __init__(self, scene, body, joint):
        self.body, self.joint = scene.objects[body], scene.objects[joint]

    def __repr__(self):
        return f"<Link {self.body!r} by {self.joint!r}>"


class Joint(SceneObject):
    kind = "joint"

    @property
    def servo(self):
        """Whether the joint's main freedom has a servo (a drive with stiffness): what a controller's "target" turns."""
        stiffness = self._get("drive", "stiffness")
        return bool(stiffness and stiffness > 0)

    @property
    def coordinates(self):
        """Travel x y z [m] and turns about x y z [rad] of the joint now (None: not running)."""
        joint = self.scene._live("joints", self.id)
        return joint.get("coordinates") if joint else None

    def drive(self, kind="servo", dof=None, **fields):
        """A drive on a freedom (None: the main one) - its output a force on the joint's two links: kind servo (stiffness,
        damping, target, velocity, force), velocity (damping, velocity, force) or dcmotor (voltage, resistance, constant).
        Fields not given keep their values: drive(target=0.5) moves the servo's target only."""
        args = {"joint": self.id, "kind": kind, **({"dof": dof} if dof else {}), **{k: float(v) for k, v in fields.items()}}
        shown = ", ".join([repr(kind)] + ([f"dof={dof!r}"] if dof else []) + [f"{k}={v!r}" for k, v in fields.items()])
        return self.scene._call("assembly_drive", f"{self.ref}.drive({shown})", **args)

    def add_encoder(self, lines=500, reference=None):
        """A quadrature encoder on the main freedom: 4*lines counts/turn (or metre).
        `reference` is the known coordinate at count zero in rad (or m); omitted means
        unreferenced displacement only, unavailable as absolute arm feedback."""
        args = {"joint": self.id, "lines": lines}
        if reference is not None:
            args["reference"] = float(reference)
        shown = f"lines={lines}" + (f", reference={reference!r}" if reference is not None else "")
        answer = self.scene._call("assembly_encoder", f"{self.ref}.add_encoder({shown})", **args)
        return self.scene.objects[answer["id"]]

    def add_wrench(self):
        """A six-axis force-torque sensor on the joint: the force [N] and torque [N m] it passes to its link a,
        in the joint's frame on a about its anchor - as at a robot's wrist."""
        return self._add_joint_sensor("wrench", f"{self.ref}.add_wrench()")

    def add_effort(self, dof=None):
        """The drive's force or torque along a freedom (None: the main one) - a servo's current sense."""
        return self._add_joint_sensor("effort", f"{self.ref}.add_effort({_q(dof) if dof else ''})", **({"dof": dof} if dof else {}))

    def _add_joint_sensor(self, kind, py, **extra):
        answer = self.scene._call("assembly_sensor", py, joint=self.id, kind=kind, **extra)
        return self.scene.objects[answer["id"]]

    def power(self, battery=None):
        """The joint's DC motor fed from a battery (None: the scene's first, or a new 3S pack): its driver."""
        args = {"battery": battery.id} if battery else {}
        call = f"{self.ref}.power({battery.ref if battery else ''})"
        return self.scene.objects[self.scene._call("assembly_power", call, joint=self.id, **args)["id"]]


class Sensor(SceneObject):
    kind = "sensor"

    @property
    def parent(self):
        return int(self._get("object", "parent") or 0)

    def _reading(self, key):
        sensor = self.scene._live("sensors", self.id)
        return sensor.get(key) if sensor else None

    @property
    def problem(self):
        """Why it reads nothing ("" when it reads; None: the scene is not running)."""
        return self._reading("problem")

    @property
    def model(self):
        """The model of the catalog its datasheet was copied from ("" : its own numbers)."""
        return next((words[0] for words in self._lines("model") if words), "")

    @property
    def spec(self):
        """Its datasheet: {key: number}, the keys of its kind's fields (sensor_models), units in their names."""
        words = next(iter(self._lines("spec")), [])
        return {key: float(value) for key, value in zip(words[0::2], words[1::2])}

    def set_spec(self, model=None, **values):
        """A model's numbers from its table, then `values` over them: ``imu.set_spec("BMI088", rate_hz=400)``."""
        args = {"sensor": self.id}
        shown = [_q(model)] if model is not None else []
        if model is not None:
            args["model"] = model
        if values:
            args["spec"] = values
            shown += [f"{k}={v!r}" for k, v in values.items()]
        return self.scene._call("sensor_spec", f"{self.ref}.set_spec({', '.join(shown)})", **args)["datasheet"]


class Encoder(Sensor):
    lines = property(lambda self: int(self._get("encoder", "lines")),
                     lambda self, v: self._set("encoder", "lines", int(v), f"{self.ref}.lines = {int(v)}"))
    count = property(lambda self: None if self._reading("count") is None else int(self._reading("count")),
                     doc="Quadrature steps counted since it began")
    angle = property(lambda self: self._reading("reading"), doc="Decoded count-based coordinate [rad] or [m]; None without a valid reference")


class Imu(Sensor):
    accel = property(lambda self: self._reading("accel"), doc="Specific force in the chip's axes, m/s^2, as its counts say")
    gyro = property(lambda self: self._reading("gyro"), doc="Angular rate in the chip's axes, rad/s, as its counts say")
    noise = property(lambda self: self._get("imu", "noise") == 1,
                     lambda self, v: self._set("imu", "noise", int(bool(v)), f"{self.ref}.noise = {bool(v)}"))


class Sonar(Sensor):
    distance = property(lambda self: self._reading("distance"), doc="The echo's distance, m (-1: no echo)")
    noise = property(lambda self: self._get("sonar", "noise") == 1,
                     lambda self, v: self._set("sonar", "noise", int(bool(v)), f"{self.ref}.noise = {bool(v)}"))


class Touch(Sensor):
    force = property(lambda self: self._reading("force"), doc="The normal force of the contacts through its face, N")
    points = property(lambda self: self._reading("points"), doc="How many contact points press it")


class Wrench(Sensor):
    force = property(lambda self: self._reading("force"), doc="The force its joint passes to the link, N, in the joint's frame on the link")
    torque = property(lambda self: self._reading("torque"), doc="The torque about the joint's anchor, N m, in the same frame")


class Effort(Sensor):
    effort = property(lambda self: self._reading("effort"), doc="The drive's force (N) or torque (N m) along its freedom")


class Battery(SceneObject):
    kind = "battery"
    voltage = property(lambda self: (self.scene._live("batteries", self.id) or {}).get("voltage"))
    current = property(lambda self: (self.scene._live("batteries", self.id) or {}).get("current"))
    charge = property(lambda self: (self.scene._live("batteries", self.id) or {}).get("charge"), doc="State of charge, 0..1")
    series = property(lambda self: int(self._get("cells", "series")),
                      lambda self, v: self._set("cells", "series", int(v), f"{self.ref}.series = {int(v)}"))


class Driver(SceneObject):
    kind = "driver"
    duty = property(lambda self: self._get("bridge", "duty"),
                    lambda self, v: self._set("bridge", "duty", float(v), f"{self.ref}.duty = {float(v)}"),
                    doc="PWM duty -1..1, the sign the direction")
    brake = property(lambda self: self._get("bridge", "stop") == "brake",
                     lambda self, v: self._set("bridge", "stop", "brake" if v else "coast", f"{self.ref}.brake = {bool(v)}"),
                     doc="At duty 0: the winding shorted (brake), not open (coast)")
    current = property(lambda self: (self.scene._live("drivers", self.id) or {}).get("current"), doc="Motor current now, A")
    voltage = property(lambda self: (self.scene._live("drivers", self.id) or {}).get("voltage"), doc="On the motor now, V")

    @property
    def battery(self):
        id = int(self._get("bridge", "battery") or 0)
        return self.scene.objects[id] if id else None

    @property
    def joint(self):
        return self.scene.objects[int(self._get("bridge", "joint"))]


class Controller(SceneObject):
    """An abstract panel of the scene (controller_add): named actions, each driving a goal, the fingers, a joint or a
    motor; the keys bound to them are the editor's."""
    kind = "controller"

    @property
    def actions(self):
        return [words[0] for words in self._lines("action")]


class Camera(SceneObject):
    """A camera: free (it looks where it is turned) or a target camera that always looks at its target - an object,
    which it follows in play, or a point."""
    kind = "camera"

    @property
    def target(self):
        """What a target camera looks at: an object - its own target cube (move it to aim) or another; None: free."""
        at = self._get("target", "object")
        return None if at is None else self.scene.objects[int(at)]

    def aim(self, at=None, point=None):
        """A target camera: always looking at object `at` (wherever it goes), else at its own target cube - put at
        `point`, or where it looks now."""
        args, shown = {"camera": self.id}, []
        if at is not None:
            args["at"] = at.id
            shown.append(f"at={at.ref}")
        if point is not None:
            args["point"] = list(point)
            shown.append(f"point={_t(point)}")
        self.scene._call("camera_aim", f"{self.ref}.aim({', '.join(shown)})", **args)
        return self

    def free(self):
        """A free camera again, looking where it looked."""
        self.scene._call("camera_aim", f"{self.ref}.free()", camera=self.id, free=True)
        return self


class Group(SceneObject):
    """Objects held together: a click picks the whole group."""
    kind = "group"

    @property
    def members(self):
        return [o for o in self.scene.objects if int(o._get("object", "parent") or 0) == self.id]

    def ungroup(self):
        """The group dissolved, its members kept where they stand: the members."""
        return self.scene._objects(self.scene._call("scene_ungroup", f"{self.ref}.ungroup()", ids=[self.id])["ids"])


class PointWheel(SceneObject):
    """A wheel as a point of its body (the fast model of a wheel module): a ray to the ground, a spring, a
    tyre, a DC motor - no body of its own."""
    kind = "wheel"
    radius = property(lambda self: self._get("tyre", "radius"))
    rolling = property(lambda self: self._get("tyre", "rolling"),
                       lambda self, v: self._set("tyre", "rolling", float(v), f"{self.ref}.rolling = {float(v)}"),
                       doc="The rolling resistance coefficient C_rr: a torque C_rr N r against the spin")
    friction = property(lambda self: self._get("tyre", "friction"),
                        lambda self, v: self._set("tyre", "friction", float(v), f"{self.ref}.friction = {float(v)}"))

    def _reading(self, key):
        wheel = self.scene._live("wheels", self.id)
        return wheel.get(key) if wheel else None

    spin = property(lambda self: self._reading("spin"), doc="About its axle now, rad/s (positive rolls forward)")
    contact = property(lambda self: self._reading("contact"), doc="On the ground now")
    load = property(lambda self: self._reading("load"), doc="The ground's push on it now, N")

