# Имена API: словарь, грамматика, ступени

Имена операций, слова их аргументов, ключи ответов, слова файла сцены и имена пакета Python - обещание каждому,
кто на них пишет: скрипту, плагину, агенту, чужой программе. Этот документ - их реестр и правила. **Имя здесь не
меняется и не исчезает - реестр только пополняется.** Ошибку в имени исправляют, добавляя рядом новое; старое
работает дальше. Так живут интерфейсы, которым десятки лет: системные вызовы Linux («we do not break
userspace»), поля protobuf, расширения glTF и Vulkan. Замок - `tools/test_names.py`: операция или слово не из
реестра, исчезнувшее имя, имя не по грамматике роняют тест. Что делают операции и что отвечают -
[operations](operations.md); здесь - как они называются.

Реестр растёт слоями; нижний слой не трогают ради верхнего:

| Слой | Что | Как растёт |
|---|---|---|
| 0. Соглашения | единицы, оси, углы, цвет | не меняются никогда |
| 1. Грамматика | из чего складывается имя | не меняется |
| 2. Ступени | stable, testing, obsolete, reserved | не меняются |
| 3. Области и глаголы | первое и второе слово имени операции | только дописываются |
| 4. Словарь | слово, его тип, смысл и единица | только дописывается |
| 5. Реестр операций | каждое имя и его ступень | только дописывается; ступень растёт |
| 6. Расширения | имена плагинов | область плагина - его имя |

Сейчас под замком операции ядра и окна с их аргументами. Ключи ответов, слова файла сцены и имена пакета Python
подчиняются тем же правилам и войдут в реестр следующими шагами.

## 0. Соглашения

- Единицы СИ: метр, килограмм, секунда, ньютон, Н·м, вольт, ампер, ом. Величина в другой единице несёт её в имени
  суффиксом: `idle_timeout_ms`, `rate_hz`, `mass_kg`. Пиксели - у `x`, `y`, `width`, `height` кадра и снимка.
- Правая система координат, Y вверх.
- Поворот позы - углами Эйлера в градусах `[x, y, z]`, как в файле сцены: кватернион `Ry · Rx · Rz`
  (`objectRotation`, `src/scene/SceneHierarchy.cpp`). Кватернион - `[w, x, y, z]`.
- Координата сустава, его уставка и скорость - радианы и рад/с у поворотных, метры и м/с у линейных.
- Цвет - `[r, g, b]`, каждое от 0 до 1.
- Время - секунды; кадр - один шаг симуляции.
- Объект называют его `id` - целое больше нуля; 0 - «никакой» или «мир». `name` - для человека и не обязан быть
  единственным.

## 1. Грамматика

- Имя - строчные латинские буквы, цифры и `_`: `[a-z][a-z0-9_]*`. Ни точек, ни дефисов: имя операции - это и имя
  инструмента MCP, а он их принимает не везде.
- **Операция** - `<область>_<глагол>[_<что>]`: `scene_add`, `scene_add_camera`, `sequence_key_set`. Область - из
  реестра областей; глагол новой операции - из реестра глаголов. Операции, названные до этого правила иначе
  (`assembly_joint`, `view_through`), остаются как есть: правило не переименовывает, оно для новых.
- Род операции - её `kind` в `ops_list`: `query` ничего не меняет; `edit` - правка сцены, шаг отмены; `action` -
  действие мира или программы без шага отмены; `window` - только окно, без сцены.
- Много объектов сразу - `ids` у той же операции, одним шагом отмены; разные операции разом - `scene_batch {ops}`.
- **Аргумент** - слово словаря в своём типе и смысле. Булево - без `is_`: `fixed`, `loop`. Новое слово - сначала
  строка словаря, потом код.
- Плагин: его операции - `<плагин>_<команда>` (`formula_outlines`); имя плагина - одно слово без `_`, не
  совпадающее с областью ядра.
- Пакет Python зовёт операцию её именем: `scene.ed.scene_add(shape="box")`; объектная модель берёт те же слова.

## 2. Ступени

| Ступень | Значит | Куда дальше |
|---|---|---|
| `stable` | выпущено: работает всегда; имя, тип, смысл и единица не меняются | в `obsolete`, если найдено имя лучше |
| `testing` | ещё не выпущено - нет в чистой ветке `gold_clear`; можно поправить или убрать | в `stable` с выпуском; убранное - в `reserved` |
| `obsolete` | работает всегда, но новым не брать: рядом сказано, что вместо | никуда: не удаляется |
| `reserved` | имени нет и не будет; занять его другим смыслом нельзя | никуда |

Выпуск - снимок в `gold_clear`: всё `testing`, что в нём есть, тем же шагом становится `stable`.

## 3. Как исправить имя

Не переименовывать. Добавить рядом правильное (`testing`), старое перевести в `obsolete` со ссылкой «вместо» - оно
работает как работало. Так Linux к `dup2` добавил `dup3`, к `renameat` - `renameat2`, а Vulkan, принимая
расширение в ядро, даёт ему новое имя и оставляет старое синонимом. Новый аргумент - только необязательный, и без
него операция ведёт себя как прежде.

## 4. Области

Первое слово имени операции. Область ядра добавляет только ядро.

| Область | Что в ней | Ступень |
|---|---|---|
| `app` | программа: выход | stable |
| `assembly` | сборка робота: суставы, приводы, датчики, колёса, цели, пути | stable |
| `camera` | камеры сцены | stable |
| `component` | компоненты плагинов на объектах | stable |
| `console` | консоль API | stable |
| `controller` | контроллеры робота | stable |
| `edit` | отмена и повтор | stable |
| `force` | силы сцены | stable |
| `gizmo` | гизмо вида | stable |
| `journal` | журнал правок | stable |
| `ops` | сами операции: список, справка | stable |
| `plugin` | плагины: запуск, регистрация, новые | stable |
| `robot` | команды роботу | stable |
| `runtime` | среда исполнения: фоновые задачи, пределы | stable |
| `scene` | объекты сцены и её файл | stable |
| `script` | скрипты | stable |
| `selection` | выделение | stable |
| `sensor` | датчики: паспорт, показания | stable |
| `sequence` | секвенции и их ключи | stable |
| `sim` | мир в работе: пуск, шаг, замеры | stable |
| `ui` | окно как его видит человек: кнопки, мышь, клавиши | stable |
| `view` | 3D-вид: камера вида, рисунки, снимки | stable |
| `world` | мир: тяжесть, физика, время проигрывания | stable |

## 5. Глаголы

Второе слово имени новой операции. Стандартные - как в API, которые живут десятилетиями: `get` - одно, `list` -
много, `add` - создать, `set` - изменить, `remove` и `delete` - убрать. Остальные - действия своих областей.

`add` `aim` `attach` `clear` `configure` `delete` `describe` `draw` `duplicate` `fire` `frame` `get` `grip` `group` `list` `look` `measure` `move` `new` `note` `open` `pause` `play` `preview` `project` `put` `quit` `reach` `read` `record` `redo` `reload` `remove` `restore` `run` `save` `scale` `scan` `seek` `set` `show` `snap` `step` `stop` `suggest` `tag` `trigger` `turn` `undo` `ungroup`

## 6. Словарь аргументов

Слово значит одно в своём типе: `target` - объект (`int`), уставка привода (`number`) или точка взгляда (`[3]`), и
у каждой пары свой смысл навсегда. Новые операции берут слово в смысле его строки; вместо `obsolete` - слово из
столбца «Вместо». Типы: `int`, `number`, `bool`, `str`; `[2]`, `[3]`, `[4]` - столько чисел; `[int]` - список id;
`[str]` - список слов; `[]` - список, что в нём - в смысле; `[[]]` - список списков; `{}` - объект; `any` - одно
из нескольких.

| Слово | Тип | Смысл, единица | Ступень | Вместо |
|---|---|---|---|---|
| `a` | `int` | первое тело сустава или привода - то, что движется | stable |  |
| `action` | `str` | действие: имя действия управления у контроллера; у `ui_key`, `ui_pointer` - что делают клавиша или мышь (значения - у операции) | stable |  |
| `active` | `bool` | секвенция открыта в Секвенсоре | stable |  |
| `anchor` | `[3]` | точка мира, где сустав или привод держит тела, м | stable |  |
| `at` | `int` | объект, на который смотрит камера | obsolete | `target` |
| `at` | `[3]` | точка мира, у которой ставят свет, силу или рисунок, м | obsolete | `position` |
| `axis` | `int` | ось коробки: 0 - x, 1 - y, 2 - z | stable |  |
| `axis` | `[3]` | направление оси в мире, не обязательно единичное | stable |  |
| `axis` | `any` | ось поворота: слово x, y, z или направление [x, y, z] | stable |  |
| `b` | `int` | второе тело сустава или привода - опора; 0 - мир | stable |  |
| `baseline` | `number` | база стереокамеры - расстояние между объективами, м | stable |  |
| `basis` | `[[]]` | поворот строками матрицы 3x3 | stable |  |
| `battery` | `int` | батарея | stable |  |
| `block` | `str` | кусок файла сцены - строки одного объекта | stable |  |
| `body` | `int` | тело | stable |  |
| `box` | `{}` | коробка рисунка `{centre, size}`, м | stable |  |
| `button` | `str` | кнопка мыши: left, right, middle | stable |  |
| `by` | `[3]` | на сколько сдвинуть по x, y, z, м | stable |  |
| `by` | `any` | во сколько раз: число или по осям [x, y, z] | stable |  |
| `camera` | `int` | камера сцены | stable |  |
| `circle` | `{}` | окружность рисунка `{centre, radius, axis}`, м | stable |  |
| `clean` | `bool` | снимок без надписей и куба поверх вида | stable |  |
| `clear` | `bool` | убрать рисунки, чьё имя начинается так | stable |  |
| `close` | `bool` | закрыть схват; false - открыть | stable |  |
| `color` | `[3]` | цвет [r, g, b], каждое 0-1 - слово файла сцены и всех стандартов 3D | testing |  |
| `colour` | `[3]` | цвет [r, g, b], каждое 0-1 | obsolete | `color` |
| `commands` | `{}` | команды исполнителям робота по их id: `{"7": {...}}` | stable |  |
| `constant` | `number` | постоянная мотора, Н·м/А | stable |  |
| `count` | `int` | сколько: копий в массиве, слов после ключа | stable |  |
| `cursor` | `bool` | двигать и настоящий курсор человека | stable |  |
| `damping` | `number` | демпфирование привода, Н·м·с/рад или Н·с/м | stable |  |
| `degrees` | `number` | угол, градусы | stable |  |
| `diameter` | `number` | диаметр, м | stable |  |
| `dof` | `str` | степень свободы: tx, ty, tz - сдвиг, rx, ry, rz - поворот | stable |  |
| `effector` | `int` | звено-эффектор цепи (кисть) | stable |  |
| `encoder` | `number` | энкодер колеса, линий на оборот; 0 - без энкодера | stable |  |
| `eye` | `[3]` | где стоит глаз камеры, м | stable |  |
| `face` | `str` | грань коробки: +x, -x, +y, -y, +z, -z | stable |  |
| `families` | `[]` | начала имён операций для отбора: `["scene", "sim"]` | stable |  |
| `file` | `str` | путь к файлу; относительный - от места запуска | stable |  |
| `fill` | `[]` | контуры заливки: строки `M x y L x y C x1 y1 x2 y2 x y Z` | stable |  |
| `fixed` | `bool` | тело стоит на месте, физика его не двигает | stable |  |
| `flip` | `bool` | развернуть заготовку по нормали | stable |  |
| `folder` | `str` | путь к папке | stable |  |
| `force` | `number` | предельная сила или момент привода, Н или Н·м; сила схвата, Н | stable |  |
| `fov` | `number` | угол обзора камеры по вертикали, градусы | stable |  |
| `frames` | `int` | кадров симуляции - её шагов | stable |  |
| `free` | `bool` | камера снова свободная - не следит за объектом | stable |  |
| `goal` | `int` | цель обратной кинематики | stable |  |
| `goals` | `[int]` | цели обратной кинематики | stable |  |
| `group` | `[int]` | объекты, замеряемые вместе | stable |  |
| `height` | `number` | высота: м у формы, пикселей у кадра камеры | stable |  |
| `id` | `int` | объект сцены, над которым действие; 0 - никакой | stable |  |
| `idle_timeout_ms` | `number` | через сколько без запросов закрыться, мс | stable |  |
| `ids` | `[int]` | объекты сцены, над которыми действие; все - одним шагом отмены | stable |  |
| `ignores` | `[]` | метки того, с чем у формы нет контакта | stable |  |
| `impulse` | `[3]` | импульс, Н·с | stable |  |
| `interp` | `str` | интерполяция между ключами | stable |  |
| `joint` | `int` | сустав | stable |  |
| `joint` | `str` | вид сустава: hinge, weld, slider, ball, cylinder, universal, planar | obsolete | `kind` |
| `joints` | `[int]` | суставы | stable |  |
| `key` | `str` | имя: ключ строки сцены, имя рисунка или клавиша - по операции | stable |  |
| `kind` | `str` | вид того, что делают или о чём спрашивают; значения - у операции | stable |  |
| `kinematic` | `bool` | тело ведут ключи и код, а не силы | stable |  |
| `last` | `int` | сколько последних строк | stable |  |
| `layout` | `str` | раскладка клавиатуры: us, ru | stable |  |
| `line` | `int` | номер строки журнала | stable |  |
| `line` | `str` | первые слова строки файла сцены - её вид | stable |  |
| `line` | `[[]]` | ломаная через точки мира `[[x, y, z], ...]`, м | obsolete | `points` |
| `lines` | `int` | линий энкодера на оборот | stable |  |
| `local` | `bool` | оси гизмо - собственные оси объекта | stable |  |
| `look_at` | `[3]` | точка, на которую смотрит камера, м | obsolete | `target` |
| `loop` | `bool` | по кругу: после конца - сначала | stable |  |
| `marks` | `bool` | снимок с метками редактора; false - как снимает камера | stable |  |
| `max_background_tasks` | `int` | предел одновременных фоновых задач | stable |  |
| `max_fps` | `number` | предел кадров вида в секунду | stable |  |
| `mirror` | `bool` | зеркально | stable |  |
| `mode` | `str` | режим; значения - у операции | stable |  |
| `model` | `str` | модель: строка таблицы датчика, файл корпуса или вид колеса - по операции | stable |  |
| `modifiers` | `[str]` | клавиши-модификаторы: shift, ctrl, alt | stable |  |
| `name` | `str` | имя, которое видит человек; не обязано быть единственным | stable |  |
| `new_time` | `number` | новое время ключа, с | stable |  |
| `normal` | `[3]` | нормаль поверхности в мире | stable |  |
| `notches` | `int` | щелчки колеса мыши; плюс - от себя | stable |  |
| `object` | `int` | объект-владелец рисунка: рисунок едет с ним | stable |  |
| `objects` | `[int]` | объекты | stable |  |
| `offset` | `[3]` | смещение от центра объекта в его осях, м | stable |  |
| `on` | `bool` | включить; false - выключить | stable |  |
| `open_force` | `number` | сила, с которой схват открывается, Н | stable |  |
| `ops` | `[]` | операции пакета `[{op, args}]` - одним шагом отмены | stable |  |
| `pivot` | `[3]` | точка, вокруг которой поворачивают, м | stable |  |
| `place` | `[2]` | место в кадре: доли ширины и высоты вида от левого верхнего угла | stable |  |
| `play` | `bool` | ▶ проигрывает секвенцию | stable |  |
| `point` | `[3]` | точка мира, которую передают действию: щелчок, привязка, начало отрезка, м | stable |  |
| `points` | `[[]]` | точки мира `[[x, y, z], ...]`, м | stable |  |
| `position` | `[3]` | где стоит объект или цель, м | stable |  |
| `power` | `bool` | у колеса есть мотор | stable |  |
| `preset` | `str` | имя заготовки | stable |  |
| `quaternion` | `[4]` | поворот кватернионом [w, x, y, z] | stable |  |
| `quaternions` | `[[]]` | повороты кватернионами `[[w, x, y, z], ...]` | stable |  |
| `radius` | `number` | радиус, м | stable |  |
| `reference` | `number` | координата сустава при нулевом счёте энкодера, рад или м | stable |  |
| `resistance` | `number` | сопротивление обмотки, Ом | stable |  |
| `role` | `str` | роль тела: rigid - твёрдое, collider - коллайдер | stable |  |
| `rolling` | `number` | коэффициент сопротивления качению | stable |  |
| `rotation` | `[3]` | поворот углами Эйлера [x, y, z], градусы (соглашения, п. 0) | stable |  |
| `scale` | `number` | единица рисунка: метров в мире, доля высоты вида на экране | stable |  |
| `scene` | `bool` | замерить и сцену целиком: энергию, импульс, глубину контакта | stable |  |
| `screen` | `bool` | снимок окна, как его видит человек, с открытыми меню | stable |  |
| `segments` | `[[]]` | отрезки - пары точек мира, м | stable |  |
| `sensor` | `int` | датчик | stable |  |
| `sequence` | `int` | секвенция | stable |  |
| `session` | `str` | сессия журнала | stable |  |
| `set` | `str` | что задать: targets - уставки приводов, pose - позу сразу | stable |  |
| `shape` | `str` | форма; значения - у операции | stable |  |
| `side` | `str` | сторона колеса: front_left, front_right, rear_left, rear_right, left, right | stable |  |
| `size` | `number` | размер точки или текста рисунка, пикселей | stable |  |
| `size` | `[3]` | полные размеры по x, y, z, м | stable |  |
| `smooth` | `bool` | сгладить путь | stable |  |
| `space` | `str` | в каких координатах; значения - у операции | stable |  |
| `spec` | `{}` | поля паспорта датчика `{ключ: число}` | stable |  |
| `speed` | `number` | скорость, м/с или рад/с | stable |  |
| `start` | `[3]` | начальная точка, м | stable |  |
| `start_quaternion` | `[4]` | начальный поворот [w, x, y, z] | stable |  |
| `start_turn` | `[3]` | начальный поворот углами Эйлера, градусы | stable |  |
| `step` | `[3]` | шаг между копиями по x, y, z, м | stable |  |
| `stiffness` | `number` | жёсткость привода, Н·м/рад или Н/м | stable |  |
| `strength` | `number` | сила поля: м/с у толчка, м/с² у постоянной силы | stable |  |
| `stroke` | `[3]` | цвет обводки [r, g, b], каждое 0-1 | stable |  |
| `stroke_width` | `number` | ширина обводки, в единицах рисунка | stable |  |
| `suspension` | `bool` | колесо на подвеске | stable |  |
| `tag` | `str` | метка | stable |  |
| `target` | `int` | объект, к которому или на который действие: тело для привязки, объект, за которым следят | stable |  |
| `target` | `number` | уставка привода, рад или м | stable |  |
| `target` | `[3]` | точка, на которую смотрят, м | stable |  |
| `text` | `str` | текст | stable |  |
| `time` | `number` | время, с | stable |  |
| `to` | `[3]` | вторая точка мира - конец отрезка, м | testing |  |
| `tool` | `str` | инструмент гизмо: select, move, rotate, scale | stable |  |
| `travel` | `number` | ход схвата от открытого, м | stable |  |
| `trigger` | `bool` | форма-датчик: замечает касание, но не толкает | stable |  |
| `turn` | `[3]` | поворот углами Эйлера [x, y, z], градусы | obsolete | `rotation` |
| `turn_length` | `number` | сколько метров пути стоит радиан поворота | stable |  |
| `turns` | `[[]]` | повороты углами Эйлера `[[x, y, z], ...]`, градусы | stable |  |
| `type` | `str` | тип компонента `плагин.компонент`; у сустава - его вид | stable |  |
| `values` | `[]` | значения ключа строки сцены | stable |  |
| `values` | `{}` | значения по именам | stable |  |
| `velocity` | `number` | скорость привода, рад/с или м/с | stable |  |
| `voltage` | `number` | напряжение, В | stable |  |
| `width` | `int` | ширина снимка, пикселей | stable |  |
| `width` | `number` | ширина кадра камеры, пикселей | stable |  |
| `window` | `bool` | всё окно, а не только вид; клавиша - окну | stable |  |
| `x` | `number` | x пикселя, от левого края | stable |  |
| `y` | `number` | y пикселя, от верхнего края | stable |  |

## 7. Реестр операций

Каждая операция ядра и окна; операции плагинов - в их плагинах.

### stable

`app_quit` `assembly_actuator` `assembly_attach` `assembly_camera` `assembly_can_reach` `assembly_drive` `assembly_encoder` `assembly_goal` `assembly_goal_pose` `assembly_grip` `assembly_joint` `assembly_path` `assembly_power` `assembly_preset` `assembly_presets` `assembly_preview` `assembly_reach` `assembly_sensor` `assembly_snap` `assembly_suggest` `assembly_wheel` `camera_aim` `component_list` `component_remove` `component_set` `console_note` `controller_add` `controller_input` `edit_redo` `edit_undo` `force_fire` `gizmo_get` `gizmo_set` `journal_lines` `journal_restore` `journal_sessions` `ops_list` `plugin_list` `plugin_new` `plugin_reload` `plugin_scan` `robot_command` `runtime_configure` `runtime_keepalive` `runtime_status` `runtime_stop` `scene_add` `scene_add_camera` `scene_add_force` `scene_add_light` `scene_array` `scene_batch` `scene_collider` `scene_delete` `scene_describe` `scene_duplicate` `scene_get` `scene_group` `scene_list` `scene_move` `scene_new` `scene_open` `scene_put` `scene_remove` `scene_role` `scene_save` `scene_scale` `scene_set` `scene_show_all` `scene_tag` `scene_turn` `scene_ungroup` `script_run` `script_stop` `selection_get` `selection_set` `sensor_models` `sensor_read` `sensor_spec` `sequence_add` `sequence_clear` `sequence_delete_key` `sequence_get` `sequence_key` `sequence_key_set` `sequence_seek` `sequence_set` `sim_impulse` `sim_measure` `sim_pause` `sim_play` `sim_record` `sim_state` `sim_step` `sim_stop` `ui_actions` `ui_key` `ui_pointer` `ui_trigger` `ui_type` `view_draw` `view_frame` `view_get` `view_hud` `view_look` `view_project` `view_screenshot` `view_through` `world_get` `world_set`

### testing

`view_measure`

### obsolete

нет

### reserved

нет
