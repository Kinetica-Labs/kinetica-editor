"""A cart with a range finder drives up to a wall - a robot assembled by code, read through its sensor.

A new scene: a chassis on four motor wheels, an ultrasonic range finder on its front face, a wall ahead. The wheels
get a duty, the world runs for two seconds, and the script says what the range finder read before and after - the
robot's own reading, with its datasheet's noise and steps, not the engine's truth.

    build/kinetica --script examples/scripts/cart_with_sonar.py
"""
import kinetica as ke

WALL_AT = 1.6   # the wall's near face from the cart's start, m
DUTY = 0.4      # of the drivers' full voltage
SECONDS = 2.0

scene = ke.connect()
scene.new()
car = scene.add("box", name="Шасси", size=(0.4, 0.08, 0.26), position=(0, 0.14, 0), role="rigid")
wheels = [car.add_wheel(side) for side in ("front_left", "front_right", "rear_left", "rear_right")]
sonar = car.add_sonar(face="+x")
scene.add("box", name="Стена", size=(0.1, 0.5, 1.2), position=(WALL_AT + 0.05, 0.25, 0), role="collider")

scene.step(1)
before = sonar.distance
for wheel in wheels:
    wheel.duty = DUTY
scene.run(SECONDS)
after, moved = sonar.distance, scene.measure(objects=[car])[f"{car.id}/centre x"]
scene.stop()
print(f"the range finder read {before:.2f} m, then {after:.2f} m after {SECONDS:g} s at duty {DUTY:g}; the cart went {moved:.2f} m")
