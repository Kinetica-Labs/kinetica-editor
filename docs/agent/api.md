# Карта операций

Операция отвечает `{"ok": true, ...}` или `{"ok": false, "error": "причина"}`; лишнее поле отвергается, причина
называет нужные. Правка — один шаг отмены; правка в идущей сцене живёт до `sim_stop`. Сведения о семействе —
`ops_list {families: [...]}`, без `families` — сто тысяч знаков.

- **scene** — граф: `scene_add {shape, position, size, role, fixed, density, name}`, `scene_batch {ops: [{as, op,
  args}]}` (в пакете только правки), `scene_put {block}` (id 0 — новый), `scene_get {id}` (без id — вся сцена),
  `scene_set {id, line, key, values}`, `scene_list {kind | tag}`, `scene_describe` (сцена словами), `scene_delete {ids}`,
  `scene_move`, `scene_turn` (одно тело — вокруг себя), `scene_duplicate`, `scene_array`, `scene_group`,
  `scene_add_light`, `scene_add_camera`, `scene_add_force`, `scene_new`, `scene_open`, `scene_save {file}`;
  `scene_collider {id, kind: "mesh", hulls, concavity}` — сложная модель, что движется, сталкивается выпуклыми частями.
- **assembly** — сборка: `assembly_list`, `assembly_describe {name}` (образец словами), `assembly_add {name, position,
  overrides | text}` (`text` — блоки из `scene_get`: копия части сцены), `assembly_save {ids, name}`, `assembly_joint {a, b, type, anchor, axis}` (`hinge`, `slider`,
  `ball`, `weld`, `shock`, `cylinder`, `universal`, `planar`), `assembly_actuator` (шарнир или ползун с сервоприводом),
  `assembly_drive {joint, target}`, `assembly_power` (мотор, драйвер, батарея), `assembly_wheel {target, side,
  suspension, power}`, `assembly_sensor {target, kind, face}`, `assembly_goal {effector}`, `assembly_path {goal,
  points, turns, loop, smooth}`, `assembly_reach`, `assembly_can_reach`, `assembly_grip`.
- **sim** — прогон: `sim_play`, `sim_step {frames, state: false}`, `sim_measure {objects | scene | joints}`
  (каналы с единицами, `names`), `sim_state`, `sim_stop`, `sim_impulse`.
- **controller** — пульт: `controller_add {goal, preset: "arm"}`, `controller_input {values: {действие: число | [x,
  y]}}` (держится, пока не задано снова).
- **sequence** — анимация: `sequence_key {ids, time}` — ключ позы; с `line` и `key` — любое числовое поле, как его
  называет `scene_set` (цвет, свет, цель сервопривода `line: "drive rx", key: "target"`); играется в правке и в «Пуске».
- **world** — `world_get`, `world_set {line, key, values}` (тяжесть, длительность).
- **view** — вид: `view_screenshot` (картинка в ответе), `view_look {eye, target, glide, orbit}`, `view_draw`,
  `view_follow {body, chase}`, `view_show {cards, labels}`.
- **Плагины** — `<плагин>_<команда>`, каждая — инструмент: `narration_say {title, text, target}`, `narration_stage`,
  `formula_callout {latex, target}` (формула в стороне, выноска к детали; цель — фигура или сустав; простые слова —
  `view_draw {key, text, object, leader: {point}}`), `terrain_make`, `textures_apply`, `chess_board`,
  `chess_move {move}`, `depz_imu_connect`, `depz_arm`; какие работают — `plugin_list`.
