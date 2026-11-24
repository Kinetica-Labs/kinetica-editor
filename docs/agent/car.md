# Машинка

Готовая — `assembly_add {name: "Спорткар"}`: шасси, амортизаторы, поворотные кулаки с сервоприводами, моторы постоянного
тока от батареи, IMU и пульт. Её ведёт плагин «Мозг»: он читает пульт и гироскоп и держит курс.

## Своя машинка

- Образец — `assembly_describe {name: "Спорткар"}`: тела, суставы, моторы и пульт готовой, словами.
- Колёса — `assembly_wheel {target: шасси, side, suspension: true, power: true}`: колесо одним вызовом — с амортизатором,
  мотором на батарее, драйвером и энкодером, положительная скважность везёт вперёд; `side` — `front_left`, `front_right`,
  `rear_left`, `rear_right` (вперёд — +x шасси). Четыре вызова — четыре колеса; руками колёса не собирайте.
- Колесо вручную — цилиндр, повёрнутый вокруг x на 90°: `scene_turn {ids: [колесо], axis: "x", degrees: 90}` — **по одному**:
  несколько ids крутятся вокруг центра последнего и съезжают. Сначала все тела на местах, потом суставы: якорь сустава
  записан в осях тел, и тело, сдвинутое после, сустав тянет к старому месту.
- Ось колеса — `assembly_joint {a: колесо, b: шасси, type: "hinge", anchor, axis: [0, 0, 1]}`; мотор на ней —
  тема `power`. Колёса крутит мотор, машинку везёт трение: сил к шасси никто не прикладывает.
- Руль — кулак между колесом и шасси на шарнире с осью y и сервоприводом (`assembly_actuator {a: кулак, b: шасси,
  axis: [0, 1, 0]}`).
- IMU — `assembly_sensor {target: шасси, kind: "imu"}`: без `face` ложится на верх тела.

## Пульт

Действия машинки — `drive value` с `target` = id **шасси** (не руля и не мотора): их читает «Мозг» и сам ведёт и руль,
и моторы. `CHASSIS` ниже — id шасси; блок ставит `scene_put {block}`:

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

Клавиши — по именам Qt (`Up Down Left Right W Space`), геймпад — `left_y right_x left_trigger …`. Проверка:
`sim_play`, `controller_input {values: {"throttle": 0.5}}`, через 2 с `sim_measure` — `speed` шасси.

## Рельеф

`terrain_make {size, height, hills, flat, seed}` — неподвижные холмы, ровная середина радиусом `flat`. `track: true`
добавляет горку, лежачих полицейских и свой набор лёгких ящиков и мячей; если задание просит другое число тел, не
ставьте `track`, а добавьте тела сами: `scene_add {shape, size, position, density: 150}`.
