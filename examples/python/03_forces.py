"""The third rung: forces - a push applied to one body, and a blast that throws everything around it.

You get the two kinds of force. A push is a body's own: its strength is an impulse, N s, and a metre of its arrow is
3 m/s of that body's velocity, light or heavy. A blast is a field: it gives every body inside its sphere a velocity,
weaker toward the edge. Run it from the repository's root:

    python3 examples/python/03_forces.py
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "clients", "python"))
import kinetica as ke  # noqa: E402

with ke.Scene() as scene:
    scene.world.gravity = (0, 0, 0)  # no weight: the numbers below are the forces' own
    crate = scene.add("box", position=(0, 0.52, 0), size=(1, 1, 1), role="rigid")
    push = scene.add_force("directional", "impulse", body=crate)  # an arrow from its centre of mass, straight up
    scene.step(1)
    after = scene.measure(objects=[crate])
    print(f"a push of {push.strength:.0f} N s on a {after[f'{crate.id}/mass']:.0f} kg crate: {after[f'{crate.id}/speed']:.2f} m/s")

with ke.Scene() as scene:
    scene.world.gravity = (0, 0, 0)
    near = scene.add("box", position=(0.5, 0.12, 0), role="rigid")
    far = scene.add("box", position=(2.0, 0.12, 0), role="rigid")
    outside = scene.add("box", position=(4.0, 0.12, 0), role="rigid")
    blast = scene.add_force("radial", "impulse", at=(0, 0.12, 0))  # 6 m/s at its centre, nothing beyond its 3 m
    scene.step(1)
    speeds = scene.measure(objects=[near, far, outside])
    print(f"a blast of {blast.strength:.0f} m/s, radius {blast.radius:.0f} m: " +
          ", ".join(f"{speeds[f'{box.id}/speed']:.2f} m/s at {distance} m" for box, distance in ((near, 0.5), (far, 2.0), (outside, 4.0))))
