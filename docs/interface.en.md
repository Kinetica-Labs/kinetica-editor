# Kinetica: a guide to the window

[Русский](interface.md) · **English**

An editor of robots: you assemble a mechanism from bodies, joints, motors and sensors, press <img src="images/ui/icons/play.png" height="22" alt=""> Play - and physics shows how it behaves.

![The Kinetica window with a UR5e arm](images/ui/en/window.png)

> **In five minutes** you will have a wall that a ball knocks down. **In half an hour** - a mechanism with a motor and a sensor.

| For whom | What you need | Checked on |
|---|---|---|
| for those who open the editor for the first time; no 3D experience needed | a running Kinetica editor | Kinetica 0.1.0, November 2026 |

## How to read this guide

1. **Do [the first five minutes](#first) with your own hands first.** Six steps, a picture for each.
2. **After that, open only the chapter you need.** Each one starts with a picture and fits on one screen.
3. **You cannot break anything.** `Ctrl+Z` undoes any edit, and <img src="images/ui/icons/stop.png" height="22" alt=""> Stop brings the scene back as it was before <img src="images/ui/icons/play.png" height="22" alt=""> Play.

## What is inside

<table>
<tr>
<td width="33%" valign="top"><a href="#first"><img src="images/ui/en/first-6-hit.png" width="260"><br><b>1. The first five minutes</b></a><br>a wall and a ball, step by step</td>
<td width="33%" valign="top"><a href="#map"><img src="images/ui/en/window.png" width="260"><br><b>2. The window: what is where</b></a><br>panels, tabs, the mouse</td>
<td width="33%" valign="top"><a href="#build"><img src="images/ui/en/shapes.png" width="260"><br><b>3. Building a scene</b></a><br>shapes, the gizmo, roles, lights</td>
</tr>
<tr>
<td width="33%" valign="top"><a href="#robot"><img src="images/ui/en/magnet-menu.png" width="260"><br><b>4. Assembling a robot</b></a><br>the magnet, joints, sensors</td>
<td width="33%" valign="top"><a href="#run"><img src="images/ui/en/graphs.png" width="260"><br><b>5. Running and watching</b></a><br>Play, plots, the Laboratory</td>
<td width="33%" valign="top"><a href="#anim"><img src="images/ui/en/sequencer.png" width="260"><br><b>6. Animation</b></a><br>the Sequencer and its keys</td>
</tr>
<tr>
<td width="33%" valign="top"><a href="#world"><img src="images/ui/en/map-world.png" width="260"><br><b>7. The world</b></a><br>gravity and physics</td>
<td width="33%" valign="top"><a href="#more"><img src="images/ui/en/search.png" width="260"><br><b>8. Faster and deeper</b></a><br>command search, scripts, plugins</td>
<td width="33%" valign="top"><a href="#keys"><img src="images/ui/en/map-toolbar.png" width="260"><br><b>Cheat sheet and help</b></a><br>keys; when something goes wrong</td>
</tr>
</table>
<a id="first"></a>

## 1. The first five minutes

**This is what you will get:**

![A ball knocks down a wall of bricks](images/ui/first-result.png)

Six steps, five minutes. Do them in order: at the end you will know the main loop of the work -
**add → set up → play → bring back**. Type the numbers without fear: whatever you do, `Ctrl+Z` undoes it.

### Step 1. An empty scene

Scene → New Empty Scene (`Ctrl+N`). Only the floor is left.

![An empty scene](images/ui/en/first-1-empty.png)

### Step 2. A brick

Press <img src="images/ui/icons/shape.png" height="22" alt=""> Shape **(1)** and choose Cube **(2)**.

![The Shape menu](images/ui/en/first-2-menu.png)

The cube appears selected, and its card opens on the right. In it, in order:

| # | Field | Value |
|---|---|---|
| 1 | Size, m | `0.24` · `0.15` · `0.2` |
| 2 | Position, m | `-0.5` · `0.075` · `0` |
| 3 | The <img src="images/ui/icons/rigid.png" height="22" alt=""> Rigid Body button | pressed - now it is a body, not just a picture |

![The brick and its card](images/ui/en/first-3-brick.png)

> **Did it not work?** No card - click the cube: the card shows the selected object.

### Step 3. A wall of twenty bricks

With the brick selected: Edit → Create Array…. In the array's card:

| # | Field | Value |
|---|---|---|
| 1 | Kind | Grid |
| 2 | Count | `5` · `4` · `1` |
| 3 | Distance, m | `0.25` · `0.155` · `0.3` |

![The wall and the array's card](images/ui/en/first-4-wall.png)

> **Did it not work?** The bricks stand in one line or inside each other - check Distance: the second number is the step up.

### Step 4. A ball

<img src="images/ui/icons/shape.png" height="22" alt=""> Shape → Sphere, then the <img src="images/ui/icons/rigid.png" height="22" alt=""> Rigid Body button. In the card:

| # | Field | Value |
|---|---|---|
| 1 | The <img src="images/ui/icons/rigid.png" height="22" alt=""> Rigid Body button | pressed |
| 2 | Position, m | `0` · `0.4` · `1.7` |
| | Size, m | `0.3` · `0.3` · `0.3` |
| 3 | Density, kg/m3 | `4000` - heavy |
| | Speed, m/s - lower in the same card | `0` · `1.5` · `-7` - it flies at the wall |

You can also move the ball by hand: the arrows on it are the move gizmo.

![The ball with the move gizmo](images/ui/en/first-5-ball.png)

> **Did it not work?** The ball is not in view - press `F`: the view frames the selection.

### Step 5. Play

Press <img src="images/ui/icons/play.png" height="22" alt=""> Play (`Space`).

![The Play button](images/ui/en/first-5-play.png)

| The hit | A second later |
|---|---|
| ![The ball hits the wall](images/ui/en/first-6-hit.png) | ![The bricks fly apart](images/ui/en/first-6-after.png) |

> **Did it not work?** The ball fell short - the third number of Speed must be `-7`: the minus points at the wall.
> Nothing moves - <img src="images/ui/icons/rigid.png" height="22" alt=""> Rigid Body is not pressed on the ball or on the brick.

### Step 6. Stop

Press <img src="images/ui/icons/stop.png" height="22" alt=""> Stop (`Shift+Space`). The wall is whole again: <img src="images/ui/icons/play.png" height="22" alt=""> Play spoiled nothing.

![The Stop button](images/ui/en/first-6-stop.png)

![The scene after Stop](images/ui/en/first-7-stop.png)

> **You can already:** add a shape, set its numbers in the card, make it a body, multiply it with an array, start and
> stop physics. Everything else in the editor is a variation of this loop.

### Try it yourself

Change one number and press <img src="images/ui/icons/play.png" height="22" alt=""> Play again.

**1. Make the ball light:** density `500` instead of `4000`. Will it break the wall?

<details><summary>Answer</summary>

No: a light ball bounces off and stays in front of the wall. The speed is the same, and the mass is eight times less.

</details>

**2. Switch on the Moon:** the World tab → the Moon button. What changes?

<details><summary>Answer</summary>

The ball and the bricks fly farther and fall slower: gravity on the Moon is six times weaker. The Earth button brings
the usual one back.

</details>

<a id="map"></a>

## 2. The window: what is where

> The lesson is over. What follows is a reference by task: no need to read it in order, open what you need.

The toolbar is everything that creates and runs:

![The toolbar](images/ui/en/map-toolbar.png)

| Buttons | What for |
|---|---|
| <img src="images/ui/icons/shape.png" height="22" alt=""> Shape, <img src="images/ui/icons/light.png" height="22" alt=""> Light, <img src="images/ui/icons/camera.png" height="22" alt=""> Camera, <img src="images/ui/icons/sensor.png" height="22" alt=""> Sensor | add to the scene |
| <img src="images/ui/icons/magnet.png" height="22" alt=""> Magnet | assemble a mechanism face to face |
| <img src="images/ui/icons/play.png" height="22" alt=""> Play, <img src="images/ui/icons/record.png" height="22" alt=""> Record, <img src="images/ui/icons/pause.png" height="22" alt=""> Pause, <img src="images/ui/icons/stop.png" height="22" alt=""> Stop | the scene's time |
| <img src="images/ui/icons/undo.png" height="22" alt=""> Undo, <img src="images/ui/icons/redo.png" height="22" alt=""> Redo | a step back and forward |
| Examples, Laboratory, Plots | ready scenes and instruments |

On the right is the inspector with three tabs:

| Scene: the tree of objects | Object: the card of the selection | World: gravity and physics |
|---|---|---|
| ![The Scene tab](images/ui/en/map-scene.png) | ![The Object tab](images/ui/en/map-object.png) | ![The World tab](images/ui/en/map-world.png) |

On the left are the tools, at the bottom the Sequencer and the console:

| Tools | The bottom panel |
|---|---|
| ![The tools](images/ui/en/map-tools.png) | ![The bottom panel](images/ui/en/map-bottom.png) |

Under the 3D view stands the mode - Edit, Play or Pause. The lowest line of the window says what the mouse does right
now. Four buttons in the top right corner hide and show the panels; `F11` leaves the 3D view alone.

### The mouse in the 3D view

| Action | What it does |
|---|---|
| Left button | select an object; drag from empty space - a box |
| Right button | orbit; a click - the menu; with the button held `W` `A` `S` `D` - fly |
| Middle button | pan |
| Wheel | zoom |
| `F` | frame the selection; `Home` - the whole scene |

The habits of other programs are in Settings → Controls: Simple, Blender Style, Maya / Unity Style.

<a id="build"></a>

## 3. Building a scene

### Shapes

<img src="images/ui/icons/shape.png" height="22" alt=""> Shape adds a cube, a sphere, a cylinder, a cone or a plane - in the air over a free spot. Model takes the shape from
a file.

![The five shapes](images/ui/en/shapes.png)

### Move, rotate, scale

Select an object and a tool on the left. An arrow, an arc or a cube leads along one axis; the small square between the
arrows - across a plane.

| <img src="images/ui/icons/move.png" height="22" alt=""> Move | <img src="images/ui/icons/rotate.png" height="22" alt=""> Rotate | <img src="images/ui/icons/scale.png" height="22" alt=""> Scale |
|---|---|---|
| ![The move gizmo](images/ui/en/gizmo-move.png) | ![The rotate gizmo](images/ui/en/gizmo-rotate.png) | ![The scale gizmo](images/ui/en/gizmo-scale.png) |

- Global switches the gizmo's axes between the world's and the object's own.
- A turn released near a quarter of a revolution completes itself to it.

### Roles: what physics knows

The two large buttons of the card are the shape's roles.

| Role | What it means |
|---|---|
| **<img src="images/ui/icons/rigid.png" height="22" alt=""> Rigid Body** | a body: density, friction, restitution. The Body field chooses how it moves - see below |
| **<img src="images/ui/icons/collider.png" height="22" alt=""> Collider** | what the body collides with; drawn as a green frame. Auto takes the shape's own form |
| neither | geometry only: it is seen, but physics does not know it |

### Three kinds of body

The Body field of the <img src="images/ui/icons/rigid.png" height="22" alt=""> Rigid Body card:

| Kind | How it behaves | Example |
|---|---|---|
| **Dynamic** | physics moves it: it falls, collides, bounces | a crate, a ball, a robot's link |
| **Kinematic** | it moves as it is set - by its velocity or by a sequence; it pushes others, and nothing pushes it | a conveyor, a door, a lift |
| **Static** | it stands still | a wall, a table |

### Tags

The card of any object has a Tags field - words apart by spaces: `target`, `keep`, `robot-1`. By themselves they do
nothing: scripts and plugins find the object by them.

```python
for target in scene.objects.tagged("target"):
    print(target.name)
```

A tag goes on anything - a shape, a joint, a sensor, a light.

### Lights and cameras

<img src="images/ui/icons/light.png" height="22" alt=""> Light puts a sun, a point light or a spotlight. <img src="images/ui/icons/camera.png" height="22" alt=""> Camera - a free camera or a target camera, which always looks at its
object.

![A sun, a lamp, a spot and a target camera](images/ui/en/lights.png)

### Copies, groups, arrays

| Action | Key |
|---|---|
| Duplicate | `Ctrl+D` |
| Group / Ungroup | `Ctrl+G` / `Ctrl+Shift+G` |
| Hide Selection / Show Hidden Objects | `H` / `Alt+H` |
| Delete | `Delete` |

Create Array… turns a shape into the template of a row, a grid or a circle of copies - that is how the wall of the
first lesson was made. Convert to Separate Objects in the array's card makes the copies shapes of their own.

<a id="robot"></a>

## 4. Assembling a robot

### The magnet

<img src="images/ui/icons/magnet.png" height="22" alt=""> Magnet assembles a mechanism face to face - three moves:

| 1. Point at a face | 2. Hold and drag - the menu | 3. A click or a choice - the link stands |
|---|---|---|
| ![The ghost of a link over a face](images/ui/en/magnet-ghost.png) | ![The magnet's menu of ready links](images/ui/en/magnet-menu.png) | ![The link on a hinge](images/ui/en/magnet-link.png) |

A plain click puts the ready link with the star - the one the editor finds fitting for this place.

| Key | What it does |
|---|---|
| `1`–`5` | choose a ready link: Rotation, Travel, Wheel, Fixed Joint, Ball Joint |
| `0` | the hint for the place |
| `A` / `Tab` | rotate the axis / flip it |
| `M` | mirror |
| `Ctrl` + two clicks | join two bodies that already stand |
| `Esc` | leave the magnet |

### Joints and actuators

A joint is an object of the scene with a card of its own: the axis and the anchor, the limit, the actuator. A position
servo holds an angle by a spring with stiffness and damping.

![The card of the UR5e's shoulder hinge](images/ui/en/joint.png)

The card shows the angle, the target and the torque after <img src="images/ui/icons/play.png" height="22" alt=""> Play: in Edit there are no motors.

### Sensors

Select a body or a joint and press <img src="images/ui/icons/sensor.png" height="22" alt=""> Sensor. On a body go an IMU, a sonar and a contact sensor; on a joint - an encoder,
a force/torque sensor and an actuator effort sensor.

![The Sensor menu](images/ui/en/sensor-menu.png)

A sensor has a datasheet: a model from the table and its numbers - range, beam angle, accuracy, noise.

![A sonar on a cart and its datasheet](images/ui/en/sensors.png)

> A robot's control reads only such sensors - with noise, quantisation and delay, as a real device has.

<a id="run"></a>

## 5. Running and watching

### Play, Pause, Stop

| Button | Key | What it does |
|---|---|---|
| <img src="images/ui/icons/play.png" height="22" alt=""> Play | `Space` | builds a live world from the scene and starts physics |
| <img src="images/ui/icons/pause.png" height="22" alt=""> Pause | | stops time; Step in the Simulation menu moves the world one frame |
| <img src="images/ui/icons/stop.png" height="22" alt=""> Stop | `Shift+Space` | brings the scene back as it was before <img src="images/ui/icons/play.png" height="22" alt=""> Play |

After <img src="images/ui/icons/play.png" height="22" alt=""> Play the view has a frame around it. Keep Simulated Poses (`K`) carries the bodies' poses from the simulation
into the scene.

![The window on pause](images/ui/en/first-6-pause.png)

### Plots

Plots (`F9`): on the left - the scene's energy, on the right - the height and the speed of the selected object. The
curves can be overlaid, stacked, normalised and saved as CSV.

![Plots of the scene's energy and of the ball](images/ui/en/graphs.png)

### The Laboratory

Laboratory (`F8`) shows what the engine computes right in the view: contact points, normals, forces, penetration, the
colliders of all bodies. A click on a contact point or a body opens a card with its numbers.

![The Laboratory panel](images/ui/en/laboratory.png)

### Performance

The plate (`F3`) writes frames per second, the time of a frame and of a physics step, and the scene's speed against
real time.

![The performance plate](images/ui/en/hud.png)

<a id="anim"></a>

## 6. Animation: the Sequencer

Three actions:

1. Select an object and press Key (`I`) - the sequence remembers where it stands at the playhead's moment.
2. Move the playhead and put the object elsewhere.
3. Press Key again - between the keys the object travels by itself.

![A cube travels between its keys](images/ui/en/sequencer.png)

The object's track and its keys - the diamonds on the time scale:

![The Sequencer panel](images/ui/en/sequencer-panel.png)

<img src="images/ui/icons/record.png" height="22" alt=""> Record (`Ctrl+R`) plays the scene and records the bodies' motion as a new sequence. Loop repeats the sequence.

<a id="world"></a>

## 7. The world

The World tab holds the settings of the whole scene.

| Section | What is inside |
|---|---|
| Gravity | gravitational acceleration; the buttons Earth, Moon, Zero Gravity |
| Rigid Body Contacts | substeps per frame, contact stiffness and damping, continuous collision detection |
| Joints | stiffness and damping |
| Sleeping and Damping, World Size | folded, a click opens them |

Reset Physics brings the defaults back. The picture of the tab is [in the chapter about the window](#map).

<a id="more"></a>

## 8. Faster and deeper

### Find Command

`Ctrl+K` searches all the commands of the window. Type a part of a name - "group", "collider", "save" - and press
`Enter`. When you do not remember where a button is, start here.

![Command search](images/ui/en/search.png)

### The API console

Every action of the window is an operation, and the console writes it as a line of Python. The line can be copied
into a script, and your own can be typed into the field below.

![The API console](images/ui/en/console.png)

### Scripts

File → Run Script… runs a Python file that builds or changes the scene; everything it did is one undo step. Here
`examples/scripts/ur5e_cubes.py` stacked a pyramid of cubes beside the arm. In detail - [scripts.md](scripts.md) (in
Russian).

![A pyramid built by a script](images/ui/en/script.png)

### Plugins

A plugin gives the editor a new skill. The Plugins menu makes a plugin from a template, adds a folder of plugins and
restarts them; the plugins' commands stand here too. In detail - [plugins.md](plugins.md) (in Russian).

![The Plugins menu](images/ui/en/plugins-menu.png)

### Examples

Examples opens the gallery of scenes: a click on a picture opens the scene, and it can be changed and saved.

![The gallery of examples](images/ui/en/gallery.png)

### Language and settings

Settings → Language switches the window between Russian and English. The mouse scheme and Software Rendering
(requires a restart) are there too.

![The window in Russian](images/ui/en/language.png)

<a id="keys"></a>

## Cheat sheet of keys

| What | Key | | What | Key |
|---|---|---|---|---|
| <img src="images/ui/icons/play.png" height="22" alt=""> Play | `Space` | | <img src="images/ui/icons/undo.png" height="22" alt=""> Undo | `Ctrl+Z` |
| <img src="images/ui/icons/stop.png" height="22" alt=""> Stop | `Shift+Space` | | <img src="images/ui/icons/redo.png" height="22" alt=""> Redo | `Ctrl+Shift+Z` |
| <img src="images/ui/icons/record.png" height="22" alt=""> Record | `Ctrl+R` | | Duplicate | `Ctrl+D` |
| Keep Simulated Poses | `K` | | Group | `Ctrl+G` |
| Find Command | `Ctrl+K` | | Delete | `Delete` |
| Frame Selection | `F` | | Hide / Show Hidden | `H` / `Alt+H` |
| Entire Scene | `Home` | | Select All | `Ctrl+A` |
| A Sequencer key | `I` | | New scene | `Ctrl+N` |
| Plots | `F9` | | Open / Save | `Ctrl+O` / `Ctrl+S` |
| Laboratory | `F8` | | 3D View Only | `F11` |
| Performance | `F3` | | <img src="images/ui/icons/stop.png" height="22" alt=""> Stop / Deselect | `Esc` |

## When something goes wrong

| What you see | Why, and what to do |
|---|---|
| An object does not fall | it has no <img src="images/ui/icons/rigid.png" height="22" alt=""> Rigid Body role - or its Body field is not Dynamic |
| A body falls through another | one of them has no <img src="images/ui/icons/collider.png" height="22" alt=""> Collider role |
| After <img src="images/ui/icons/stop.png" height="22" alt=""> Stop everything came back | it is meant to. To keep it, press Keep Simulated Poses (`K`) on pause |
| You lost an object from view | `F` - to the selection, `Home` - the whole scene |
| You did the wrong thing | `Ctrl+Z`. Any edit is undone |
| You do not remember where a button is | `Ctrl+K` and the first letters of its name |

## Retaking the pictures

```sh
python3 tools/ui_guide_shots.py              # every picture, both languages, into docs/images/ui/<ru|en>/
python3 tools/ui_guide_shots.py en magnet    # one language, only the named groups
```

The script starts the editor without showing a window, does every function by its operation and photographs the
window with the `view_screenshot` operation. When the window changes, the pictures are retaken by one command.
