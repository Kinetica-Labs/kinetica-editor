# Рука

Готовая — `assembly_add {name: "Рука UR5e"}`: шесть суставов с сервоприводами, схват с двумя пальцами, датчики касания
и силы-момента, цель IK «Цель «Схват»» и пульт схвата.

## Своя рука — один пакет

Основание неподвижно (`fixed: true`); каждый сустав — `assembly_actuator` (шарнир с сервоприводом, настроенным на то, что
он держит); цель IK на последнем звене; пульт для цели:

```json
{"ops": [
  {"as": "база", "op": "scene_add", "args": {"shape": "cylinder", "name": "База", "position": [0, 0.05, 0], "size": [0.2, 0.1, 0.2], "fixed": true}},
  {"as": "поворот", "op": "scene_add", "args": {"shape": "cylinder", "name": "Поворот", "position": [0, 0.15, 0], "size": [0.12, 0.1, 0.12], "role": "rigid"}},
  {"as": "плечо", "op": "scene_add", "args": {"shape": "box", "name": "Плечо", "position": [0, 0.35, 0], "size": [0.06, 0.3, 0.06], "role": "rigid"}},
  {"as": "предплечье", "op": "scene_add", "args": {"shape": "box", "name": "Предплечье", "position": [0.125, 0.5, 0], "size": [0.25, 0.05, 0.05], "role": "rigid"}},
  {"op": "assembly_actuator", "args": {"a": "$поворот", "b": "$база", "axis": [0, 1, 0], "anchor": [0, 0.1, 0]}},
  {"op": "assembly_actuator", "args": {"a": "$плечо", "b": "$поворот", "axis": [0, 0, 1], "anchor": [0, 0.2, 0]}},
  {"op": "assembly_actuator", "args": {"a": "$предплечье", "b": "$плечо", "axis": [0, 0, 1], "anchor": [0, 0.5, 0]}},
  {"as": "цель", "op": "assembly_goal", "args": {"effector": "$предплечье"}},
  {"op": "controller_add", "args": {"goal": "$цель", "preset": "arm"}}]}
```

## Управление

- Цель IK: двигаете её — цепочка сервоприводов ведёт к ней звено (`effector`). Пульт `preset: "arm"` двигает цель:
  `move` (по полу, `[x, z]`), `lift`, `turn` и `grip`, если у звена есть пальцы.
- Проверка: `sim_play`, `controller_input {values: {"move": [1, 0]}}`, через 1,5 с `sim_measure {objects: [звено]}` —
  его `centre` сдвинулся. Цель дальше, чем дотянется рука, звено не догонит: держите её в пределах длины звеньев.
- Один сустав без цели: `assembly_drive {joint, target}` (рад) или `controller_add {joint}` — пульт одной оси.
- Кубики рядом: `scene_add {shape: "box", size: [0.05, 0.05, 0.05], position, role: "rigid"}`.
