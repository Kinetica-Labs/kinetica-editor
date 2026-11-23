# Kinetica для агента: как строить быстро

Это выжимка для модели, которая работает с редактором через MCP `ke-editor`; сервер отдаёт её модели при подключении
(`tools/ke_editor_mcp.py`). Каждое правило здесь стоило агенту минут в настоящих прогонах.

Kinetica — редактор роботов на физике Box3D. Человек видит каждый ваш вызов. **Каждый вызов — ваш ход, около 2 с:
меньше вызовов — быстрее.**

## Мир

- Оси: x вперёд, y вверх (пол на y = 0), z вправо. Единицы СИ. Углы в радианах; градусы Эйлера — только в строках
  `rotation` текста сцены.
- У фигуры `position` — её центр, `size` — полный размер (у шара — диаметр). Ось цилиндра — его собственная y.

## Сверху вниз: берите самый крупный шаг, который подходит

1. **Готовое — один вызов.** `assembly_list`, затем `assembly_add {name, position}`. «Спорткар» — шасси, амортизаторы,
   поворотные кулаки с сервоприводами, моторы постоянного тока от батареи, IMU и пульт. Его части меняются тем же вызовом:

   ```json
   {"name": "Спорткар", "position": [0, 0, 0],
    "overrides": [{"name": "Батарея 1", "line": "cells", "key": "parallel", "values": [2]}]}
   ```

2. **Земля — один вызов.** `terrain_make {size, height, hills, flat, seed, track}`: неподвижные холмы, ровная середина
   радиусом `flat`; `track: true` — ещё горка, лежачие полицейские, лёгкие ящики и мячи.
3. **Новое — один `scene_batch`.** `"$n"` — id, который выдал шаг n (счёт с 0), `"$n.key"` — другой ключ его ответа.
   Робот из деталей, суставов и моторов — один пакет:

   ```json
   {"ops": [
     {"op": "scene_add", "args": {"shape": "box", "name": "Шасси", "position": [0, 0.3, 0], "size": [1, 0.05, 0.4], "role": "rigid"}},
     {"op": "scene_add", "args": {"shape": "cylinder", "name": "Колесо", "position": [-0.35, 0.1, 0.3], "size": [0.2, 0.06, 0.2], "role": "rigid"}},
     {"op": "scene_turn", "args": {"ids": ["$1"], "axis": "x", "degrees": 90}},
     {"op": "assembly_joint", "args": {"a": "$1", "b": "$0", "type": "hinge", "anchor": [-0.35, 0.1, 0.27], "axis": [0, 0, 1]}},
     {"op": "assembly_drive", "args": {"joint": "$3", "kind": "dcmotor", "voltage": -11.1, "resistance": 0.13, "constant": 0.29}},
     {"op": "assembly_power", "args": {"joint": "$3"}}]}
   ```

4. **Один блок — `scene_put {block}`** (id 0 — новый объект). Пульт машинки, `CHASSIS` — id шасси:

   ```text
   controller "Пульт"
     action steer axis drive value target CHASSIS speed 1.5
     action throttle axis drive value target CHASSIS speed 2
     bind steer keys Right Left
     bind steer gamepad right_x
     bind throttle keys Up Down
     bind throttle gamepad left_y
   end
   ```

5. **Копия части сцены** — `scene_get` даёт её блоки, `assembly_add {text}` вставляет их со своими номерами: номера
   перенумеровываются сами, ссылки внутри сохраняются.

Остальные операции — `editor_call {op, args}`; `ops_list {families: ["scene"]}` описывает семейство.

## Проверка — четыре вызова

`sim_play` → `sim_step {frames: 120, state: false}` → `sim_measure {objects: [id]}` (`centre y`, `speed`, `contacts`) →
`sim_stop`. Посмотреть: `view_screenshot`. Пока сцена играет, правка живёт до `sim_stop`.

## Ловушки

- Сначала мотор, потом питание: `assembly_drive kind dcmotor`, затем `assembly_power`. Драйвер стоит на скважности 0,
  пока её не зададут.
- Типы суставов: `hinge` (вращение вокруг axis), `slider`, `ball`, `weld`, `shock` (пружина с демпфером вдоль axis),
  `cylinder`, `universal`, `planar`. `anchor` и `axis` — в мире.
- Колесо на оси вдоль z: цилиндр повернуть вокруг x на 90°.
- Действия пульта машинки — `drive value`: их читает плагин, стабилизатор «Мозга» ведёт машинку. Клавиши — по именам Qt
  (`Up Down Left Right W Space`), геймпад — `left_y right_x left_trigger …`.
- Каждый блок текста кончается строкой `end`.
- Лёгкое тело, которое машинка толкнёт: `scene_set {id, line: "rigid", key: "density", values: [150]}`.
- Не ставьте целую сборку, чтобы списать с неё одну часть: нужный блок есть выше или в `scene_get`.
- Не читайте всю схему формата: примеров выше хватает; `schema_get {kind}` — только для незнакомого вида блока.
