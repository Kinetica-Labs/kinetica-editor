# Имена API: словарь, грамматика, ступени

Имена операций, слова их аргументов, ключи ответов, слова файла сцены и имена пакета Python - обещание каждому,
кто на них пишет: скрипту, плагину, агенту, чужой программе. Этот документ - их реестр и правила. **Имя здесь не
меняется и не исчезает - реестр только пополняется.** Ошибку в имени исправляют, добавляя рядом новое; старое
работает дальше: сломать того, кто на имя опирался, - ошибка реестра, а не его. Замок - `tools/test_names.py`: операция или слово не из
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

Под замком - операции ядра и окна, слова их аргументов и ключи их ответов, какие держит запись окна
(`tests/data/window-transcript.jsonl`), слова файла сцены и имена пакета Python с именами их аргументов.

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
| `testing` | ещё не выпущено - нет в чистой ветке `main`; можно поправить или убрать | в `stable` с выпуском; убранное - в `reserved` |
| `obsolete` | работает всегда, но новым не брать: рядом сказано, что вместо | никуда: не удаляется |
| `reserved` | имени нет и не будет; занять его другим смыслом нельзя | никуда |

Выпуск - снимок `dev` в чистую ветку `main`: всё `testing`, что в нём есть, тем же шагом становится `stable`.

## 3. Как исправить имя

Не переименовывать. Добавить рядом правильное (`testing`), старое перевести в `obsolete` со ссылкой «вместо» - оно
работает как работало: так `color` встал рядом с `colour`, `angular_velocity` - рядом со `spin`. Новый аргумент -
только необязательный, и без него операция ведёт себя как прежде.

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
| `relation` | связи между объектами: кто на кого ссылается | stable |
| `schema` | опись формата: виды блоков сцены, их строки и ключи | stable |
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

`add` `aim` `attach` `clear` `configure` `delete` `describe` `draw` `duplicate` `fire` `follow` `frame` `get` `grip` `group` `list` `look` `measure` `move` `new` `note` `open` `pause` `play` `preview` `project` `put` `quit` `reach` `read` `record` `redo` `reload` `remove` `restore` `run` `save` `scale` `scan` `seek` `set` `show` `snap` `step` `stop` `suggest` `tag` `trigger` `turn` `undo` `ungroup`

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
| `color` | `[3]` | цвет [r, g, b], каждое 0-1 - слово файла сцены и всех стандартов 3D | stable |  |
| `colour` | `[3]` | цвет [r, g, b], каждое 0-1 | obsolete | `color` |
| `commands` | `{}` | команды исполнителям робота по их id: `{"7": {...}}` | stable |  |
| `constant` | `number` | постоянная мотора, Н·м/А | stable |  |
| `count` | `int` | сколько: копий в массиве, слов после ключа | stable |  |
| `cursor` | `bool` | двигать и настоящий курсор человека | stable |  |
| `curves` | `bool` | кривые одной дорожки в секвенсоре, а не дорожки | stable |  |
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
| `fit` | `bool` | показать всё целиком, как F | stable |  |
| `fixed` | `bool` | тело стоит на месте, физика его не двигает | stable |  |
| `from` | `int` | объект, от которого идёт связь | stable |  |
| `flip` | `bool` | развернуть заготовку по нормали | stable |  |
| `folder` | `str` | путь к папке | stable |  |
| `force` | `number` | предельная сила или момент привода, Н или Н·м; сила схвата, Н | stable |  |
| `fov` | `number` | угол обзора камеры по вертикали, градусы | stable |  |
| `frames` | `int` | кадров симуляции - её шагов | stable |  |
| `free` | `bool` | камера снова свободная - не следит за объектом | stable |  |
| `from` | `number` | начало показанного времени секвенсора, с | stable |  |
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
| `part` | `str` | часть окна на снимке: view, toolbar, tools, inspector, sequencer, plots, console, laboratory | stable |  |
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
| `range` | `[]` | значения кривых [низ, верх]; `[]` - по кривой в виду | stable |  |
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
| `state` | `bool` | sim_step: ответить живым состоянием (по умолчанию да); нет - только время, кадр и сколько шагнуто | testing |  |
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
| `to` | `int` | объект, к которому идёт связь; 0 - мир | stable |  |
| `to` | `[3]` | вторая точка мира - конец отрезка, м | stable |  |
| `to` | `number` | конец показанного времени секвенсора, с | stable |  |
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
| `velocity` | `[3]` | скорость ключа [x, y, z], м/с: наклон плавного ключа, его ручки Безье в кривых | stable |  |
| `voltage` | `number` | напряжение, В | stable |  |
| `width` | `int` | ширина снимка, пикселей | stable |  |
| `width` | `number` | ширина кадра камеры, пикселей | stable |  |
| `window` | `bool` | всё окно, а не только вид; клавиша - окну | stable |  |
| `x` | `number` | x пикселя, от левого края | stable |  |
| `y` | `number` | y пикселя, от верхнего края | stable |  |

## 7. Реестр операций

Каждая операция ядра и окна; операции плагинов - в их плагинах.

### stable

`app_quit` `assembly_actuator` `assembly_attach` `assembly_camera` `assembly_can_reach` `assembly_drive` `assembly_encoder` `assembly_goal` `assembly_goal_pose` `assembly_grip` `assembly_joint` `assembly_path` `assembly_power` `assembly_preset` `assembly_presets` `assembly_preview` `assembly_reach` `assembly_sensor` `assembly_snap` `assembly_suggest` `assembly_wheel` `camera_aim` `component_list` `component_remove` `component_set` `console_note` `controller_add` `controller_input` `edit_redo` `edit_undo` `force_fire` `gizmo_get` `gizmo_set` `journal_lines` `journal_restore` `journal_sessions` `ops_list` `plugin_list` `plugin_new` `plugin_reload` `plugin_scan` `relation_list` `robot_command` `runtime_configure` `runtime_keepalive` `runtime_status` `runtime_stop` `scene_add` `scene_add_camera` `scene_add_force` `scene_add_light` `scene_array` `scene_batch` `scene_collider` `scene_delete` `scene_describe` `scene_duplicate` `scene_get` `scene_group` `scene_list` `scene_move` `scene_new` `scene_open` `scene_put` `scene_remove` `scene_role` `scene_save` `scene_scale` `scene_set` `scene_show_all` `scene_tag` `scene_turn` `scene_ungroup` `schema_get` `schema_list` `script_run` `script_stop` `selection_get` `selection_set` `sensor_models` `sensor_read` `sensor_spec` `sequence_add` `sequence_clear` `sequence_delete_key` `sequence_frame` `sequence_get` `sequence_key` `sequence_key_set` `sequence_seek` `sequence_set` `sim_impulse` `sim_measure` `sim_pause` `sim_play` `sim_record` `sim_state` `sim_step` `sim_stop` `ui_actions` `ui_key` `ui_pointer` `ui_trigger` `ui_type` `view_draw` `view_frame` `view_get` `view_hud` `view_look` `view_measure` `view_project` `view_screenshot` `view_through` `world_get` `world_set`

### testing

`view_follow`

### obsolete

нет

### reserved

нет

## 8. Ключи ответов

Ключ ответа - то же слово, что в аргументах, где смысл тот же. JSON не отличает `0` от `0.0`, поэтому ключ закреплён
смыслом и единицей, а не типом. Слова внутри `schema` - словарь стандарта JSON Schema, внутри `values` - имена полей
плагинов: они не наши и в реестр не входят. Каждый ключ ответа, который есть в записи окна, - строка здесь.

| Ключ | Смысл, единица | Ступень | Вместо |
|---|---|---|---|
| `a` | первое тело контакта | stable |  |
| `action` | действие контроллера, чья это связь | stable |  |
| `accel` | ускорение по осям датчика, м/с² | stable |  |
| `accepted` | мир принял толчок | stable |  |
| `actions` | имена действий контроллера | stable |  |
| `anchor` | точка мира, где сустав или привод держит тела, м | stable |  |
| `angle` | угол сустава, рад | stable |  |
| `angular_velocity` | угловая скорость [x, y, z], рад/с - слово всех стандартов 3D | stable |  |
| `args` | строка аргументов операции - она же её схема | stable |  |
| `asleep` | тело уснуло: не движется, мир его не считает | stable |  |
| `axle` | ось колеса - её сустав | stable |  |
| `b` | второе тело контакта | stable |  |
| `battery` | батарея | stable |  |
| `body` | тело | stable |  |
| `built` | сустав есть в построенном мире | stable |  |
| `cancelled` | шаги прерваны до конца | stable |  |
| `changed` | что изменила правка: id объектов | stable |  |
| `channels` | каналы замера `[{id, name, unit, value}]` | stable |  |
| `completed_frames` | сколько кадров сделано | stable |  |
| `components` | компоненты `[{id, type, values}]` | stable |  |
| `contacts` | касания тел `[{a, b, depth, force, points}]` | stable |  |
| `coordinates` | координаты сустава по шести степеням свободы, м и рад | stable |  |
| `count` | счёт: энкодера, сколько чего | stable |  |
| `current` | ток мотора, А | stable |  |
| `curves` | секвенсор показывает кривые, а не дорожки | stable |  |
| `damping` | демпфирование привода, Н·м·с/рад или Н·с/м | stable |  |
| `depth` | глубина проникновения, м | stable |  |
| `distance` | расстояние, м | stable |  |
| `dof` | степень свободы мотора: номер 0-5 (tx, ty, tz, rx, ry, rz) | stable |  |
| `driver` | драйвер мотора | stable |  |
| `effort` | сила или момент в суставе, Н или Н·м | stable |  |
| `encoder` | энкодер | stable |  |
| `error` | почему не вышло - текст для человека; есть, когда ok - false | stable |  |
| `file` | путь к файлу | stable |  |
| `fixed` | тело стоит на месте | stable |  |
| `force` | сила, Н | stable |  |
| `from` | объект, от которого идёт связь; у `sequence_frame` - начало показанного времени, с | stable |  |
| `form` | вид значений ключа в описи: number, vector, flag, id, one of... | stable |  |
| `frame` | номер кадра мира | stable |  |
| `frame_timing` | время частей кадра, мс | stable |  |
| `gyro` | угловая скорость по осям датчика, рад/с | stable |  |
| `height` | высота места в снимке окна, пикселей (`ui_actions`, `view` у снимка); наибольшая высота картинки `ui.svg`, пикселей | stable |  |
| `help` | справка операции | stable |  |
| `id` | объект сцены; у канала замера - его путь (строка) | stable |  |
| `ids` | объекты сцены | stable |  |
| `joint` | сустав | stable |  |
| `joints` | суставы | stable |  |
| `keys` | ключи строки описи `[{word, form, unit}]` | stable |  |
| `kind` | вид: строки сцены, датчика, операции | stable |  |
| `kinds` | виды блоков описи `[{kind, parts}]` | stable |  |
| `lead` | что стоит в строке между её первым словом и ключами: вид формы, имя степени | stable |  |
| `lines` | строки: слова строк файла сцены | stable |  |
| `mode` | режим окна: edit, playing, paused | stable |  |
| `model` | модель датчика - строка его таблицы | stable |  |
| `motors` | моторы сустава `[{current, dof, power}]` | stable |  |
| `name` | имя для человека | stable |  |
| `note` | пояснение для человека: что сделано попутно | stable |  |
| `objects` | объекты сцены `[{id, kind, name}]` | stable |  |
| `ok` | вышло ли; false - см. error | stable |  |
| `ops` | операции `[{name, kind, args, help, schema}]` | stable |  |
| `parts` | строки блока в описи: их первые слова, или `[{word, lead, keys}]` | stable |  |
| `play_stopped` | правка остановила мир и сохранена | stable |  |
| `points` | сколько точек касания | obsolete | `count` |
| `position` | где стоит, м | stable |  |
| `power` | мощность мотора, Вт | stable |  |
| `problem` | неисправность датчика: текст, пусто - нет | stable |  |
| `protocol_version` | версия протокола в ответе `ops_list`: клиент, собранный под N, работает с программой N и новее; нет поля - 0 | stable |  |
| `range` | значения кривых секвенсора [низ, верх] | stable |  |
| `ranges` | дальности лучей лидара по порядку, м (-1: ничего в пределах) | testing |  |
| `reading` | показание датчика - в его единице | stable |  |
| `redone` | повтор сделан | stable |  |
| `relative` | показание относительно нуля датчика | stable |  |
| `relations` | связи `[{kind, from, to}]`: kind - часть и ключ файла сцены, что пишут связь | stable |  |
| `reload` | что мир построит заново: world, groups, arrays, devices, bodies, joints | stable |  |
| `requested_frames` | сколько кадров просили | stable |  |
| `rotation` | поворот углами Эйлера [x, y, z], градусы | stable |  |
| `sample_ns` | время отсчёта датчика, нс; -1 - отсчёта не было | stable |  |
| `schema` | JSON Schema аргументов операции - слова внутри по стандарту JSON Schema | stable |  |
| `sensors` | датчики и их показания | stable |  |
| `shapes` | тела мира `[{id, name, position, rotation, velocity, spin, fixed, asleep}]` | stable |  |
| `shock` | амортизатор колеса | stable |  |
| `spin` | угловая скорость тела [x, y, z], рад/с | obsolete | `angular_velocity` |
| `step_ms` | время шага физики, мс | stable |  |
| `stiffness` | жёсткость привода, Н·м/рад или Н/м | stable |  |
| `strut` | стойка колеса | stable |  |
| `tags` | метки | stable |  |
| `text` | текст: файл сцены | stable |  |
| `time` | время мира, с | stable |  |
| `to` | объект, к которому идёт связь; 0 - мир; у `sequence_frame` - конец показанного времени, с | stable |  |
| `travel` | ход сустава, м или рад | stable |  |
| `truth` | истинные значения из физики рядом с показанием - для проверок, не для управления | stable |  |
| `type` | тип компонента `плагин.компонент` | stable |  |
| `undo_steps` | сколько шагов отмены есть | stable |  |
| `undone` | отмена сделана | stable |  |
| `unit` | единица - для человека: канала замера, ключа описи | stable |  |
| `until_stop` | правка действует до ■ Стоп: мир построен | stable |  |
| `value` | значение канала в его единице | stable |  |
| `values` | значения: поля компонента по именам, слова ключа | stable |  |
| `velocity` | скорость [x, y, z], м/с | stable |  |
| `wheel` | колесо | stable |  |
| `width` | ширина места в снимке окна, пикселей; у снимка - его ширина | stable |  |
| `word` | слово файла сцены: первое слово строки или ключ | stable |  |
| `world` | правка изменила мир: тяжесть, физику | stable |  |
| `x` | левый край места в снимке окна, пикселей - то, что берёт `ui_pointer` space window | stable |  |
| `y` | верхний край места в снимке окна, пикселей | stable |  |

## 9. Слова файла сцены

Формат сцены - часть API ([operations](operations.md), «Гарантии»): строка на факт, первое слово - вид строки или
её часть, дальше пары «ключ значения». Здесь каждое слово, которое знает чтение сцены (`src/scene/SceneGraph.cpp`), и
слова привязок контроллера: вид строки, часть, ключ или слово-значение. Ключи паспорта датчика (`spec`) - столбцы
таблиц `data/sensors/<вид>.csv` и живут по контракту [sensors](sensors.md); метки, имена действий и поля компонентов
плагинов - слова пользователя и плагинов, не наши. Слово, которое в аргументах значит то же, пишется так же.

| Слово | Где | Смысл, единица | Ступень | Вместо |
|---|---|---|---|---|
| `a` | link; bind | первое тело сустава; у привязки - кнопка A геймпада | stable |  |
| `action` | часть | действие контроллера | stable |  |
| `active` | lens, clip | у камеры - вид через неё; у секвенции - открыта в Секвенсоре | stable |  |
| `against` | encoder | id тела, относительно которого считает | stable |  |
| `anchor_a` | link | точка сустава в осях тела a, м | stable |  |
| `anchor_b` | link | точка сустава в осях тела b, м | stable |  |
| `angular_damping` | physics, rigid | затухание вращения, 1/с | stable |  |
| `angular_velocity` | rigid | угловая скорость [x, y, z], рад/с - слово всех стандартов 3D | stable |  |
| `array` | строка | массив копий шаблона | stable |  |
| `auto` | collider | форма коллайдера по форме тела | stable |  |
| `axis` | action | вид действия: одно число -1..1 | stable |  |
| `b` | link | второе тело сустава - опора; 0 - мир | stable |  |
| `baseline` | stereo | база стереокамеры, м | stable |  |
| `battery` | строка; bridge | батарея; у драйвера - его батарея | stable |  |
| `bind` | часть | привязка действия к устройству ввода | stable |  |
| `box` | shape, collider | коробка | stable |  |
| `brake` | bridge | торможение: обмотка замкнута | stable |  |
| `bridge` | часть | мост драйвера: сустав, батарея, скважность | stable |  |
| `bump` | texture | высота рельефа, м | stable |  |
| `button` | action | вид действия: кнопка | stable |  |
| `c1` | cells | ёмкость RC-звена, Ф | stable |  |
| `camera` | строка; target | камера; у target - чья это цель | stable |  |
| `capacity` | cells | ёмкость, А·ч | stable |  |
| `capsule` | collider | капсула | stable |  |
| `cells` | часть | банки батареи и их модель | stable |  |
| `center_of_mass` | rigid | центр масс в осях тела, м | stable |  |
| `checker` | texture | шахматка | stable |  |
| `circle` | pattern | копии по кругу | stable |  |
| `clip` | часть | секвенция целиком: время, круг, проигрывание | stable |  |
| `coast` | bridge | выбег: мотор отпущен | stable |  |
| `collide` | часть | тела сустава сталкиваются между собой | stable |  |
| `collider` | часть | коллайдер: форма контакта | stable |  |
| `color` | pose, shape | цвет [r, g, b], 0-1 | stable |  |
| `component` | часть | компонент плагина `плагин.имя` и его поля - слова плагина | stable |  |
| `cone` | shape; kind | конус; у прожектора - угол конуса, градусы | stable |  |
| `constant` | effect; dcmotor | постоянная сила; у мотора - постоянная, Н·м/А | stable |  |
| `contact_damping` | physics | демпфирование контакта, доля критического | stable |  |
| `contact_hertz` | physics | жёсткость контакта, Гц | stable |  |
| `continuous` | physics | непрерывный поиск столкновений | stable |  |
| `controller` | строка | контроллер: действия и привязки к клавишам, мыши, геймпаду | stable |  |
| `count` | pattern | копий по x, y, z | stable |  |
| `cutoff` | cells | напряжение отсечки банки, В | stable |  |
| `cylinder` | shape, collider | цилиндр | stable |  |
| `damping` | drive, spring | демпфирование, Н·м·с/рад или Н·с/м | stable |  |
| `dcmotor` | часть | мотор постоянного тока | stable |  |
| `density` | rigid | плотность, кг/м³ | stable |  |
| `directional` | effect | поле одного направления | stable |  |
| `dof` | sensor, bridge, action | степень свободы: tx, ty, tz, rx, ry, rz | stable |  |
| `drive` | часть; action | привод степени сустава; у действия - как оно ведёт привод | stable |  |
| `driver` | строка | драйвер мотора (H-мост) | stable |  |
| `duration` | world | время проигрывания, с; 0 - без конца | stable |  |
| `duty` | bridge; action | скважность моста -1..1; у действия - оно ведёт скважность драйвера | stable |  |
| `effect` | часть | действие силы: форма поля, режим, сила | stable |  |
| `effector` | goal | id эффектора, чья это цель | stable |  |
| `effort` | датчик | датчик усилия в суставе | stable |  |
| `encoder` | датчик | энкодер | stable |  |
| `end` | строка | конец блока | stable |  |
| `entity` | строка | форма - тело, коллайдер, цель, путь | stable |  |
| `falloff` | effect | спад силы с расстоянием | stable |  |
| `far` | lens | дальше не видит, м | stable |  |
| `file` | shape, texture | файл: сетка OBJ/STL, картинка текстуры | stable |  |
| `fit` | collider | коллайдер по форме сетки | stable |  |
| `fixed` | rigid | тело стоит на месте | stable |  |
| `flip` | dcmotor | мотор колеса крутит в обратную сторону | stable |  |
| `force` | строка; drive | сила сцены; у привода - предельная сила или момент, Н или Н·м | stable |  |
| `fov` | lens, stereo | угол обзора по вертикали, градусы | stable |  |
| `frame_a` | link | поворот осей сустава в теле a, градусы | stable |  |
| `frame_b` | link | поворот осей сустава в теле b, градусы | stable |  |
| `free` | часть | свободная степень сустава | stable |  |
| `friction` | rigid, tyre | коэффициент трения | stable |  |
| `gamepad` | bind | геймпад | stable |  |
| `glued` | часть | группа склеена: двигается целиком | stable |  |
| `goal` | часть | цель обратной кинематики: чей эффектор | stable |  |
| `gravity` | world | ускорение тяжести [x, y, z], м/с² | stable |  |
| `gravity_scale` | rigid | доля тяжести, что действует на тело | stable |  |
| `grid` | pattern | копии сеткой | stable |  |
| `grip` | action | открывает и закрывает схват | stable |  |
| `group` | строка | группа объектов | stable |  |
| `height` | stereo | высота кадра, пикселей | stable |  |
| `height_file` | texture | файл карты высот | stable |  |
| `hull` | collider | выпуклая оболочка сетки | stable |  |
| `id` | object | id объекта | stable |  |
| `ignores` | часть | метки того, с чем у коллайдера нет контакта | stable |  |
| `image` | texture | картинка из файла | stable |  |
| `impulse` | effect | толчок - один раз | stable |  |
| `imu` | датчик | инерциальный: акселерометр и гироскоп | stable |  |
| `inertia` | rigid | тензор инерции: диагональ и произведения, кг·м² | stable |  |
| `instance` | object | копия чьего шаблона | stable |  |
| `intensity` | kind | яркость света | stable |  |
| `interp` | key | интерполяция: step, linear, smooth | stable |  |
| `inverse_square` | effect | спад по обратному квадрату | stable |  |
| `invert` | bind | обратное направление | stable |  |
| `jitter` | pattern | случайный разброс положения, м | stable |  |
| `jog` | action | привод сустава, пока держат | stable |  |
| `joint` | строка; bridge | сустав; у драйвера - его сустав | stable |  |
| `joint_damping` | physics | демпфирование суставов, доля критического | stable |  |
| `joint_hertz` | physics | жёсткость суставов, Гц | stable |  |
| `key` | часть | ключ дорожки: время, положение, поворот | stable |  |
| `keys` | bind | клавиатура: имена клавиш Qt | stable |  |
| `kind` | часть | вид: коллайдера, текстуры, света | stable |  |
| `kinematic` | rigid | тело ведут ключи и код | stable |  |
| `left` | bind | левая кнопка мыши | stable |  |
| `left_stick` | bind | левый стик геймпада | stable |  |
| `lens` | часть | объектив камеры | stable |  |
| `lidar` | датчик | лидар: лучи по паспорту (`rays_h`, `fov_h_deg`, `rays_v`, `fov_v_deg`) - луч, круг, сфера, фонарик | testing |  |
| `lift` | action | ведёт цель вверх и вниз | stable |  |
| `light` | строка | свет | stable |  |
| `limit` | часть; bridge | ограниченная степень сустава; у драйвера - предел тока, А | stable |  |
| `line` | pattern | копии в ряд | stable |  |
| `linear` | effect | спад линейный | stable |  |
| `linear_damping` | physics, rigid | затухание скорости, 1/с | stable |  |
| `lines` | encoder | линий на оборот | stable |  |
| `link` | часть | что сустав связывает: тела a, b и их точки | stable |  |
| `locked` | object | заперт от правки мышью | stable |  |
| `loop` | path, clip | по кругу | stable |  |
| `lower` | limit | нижний предел, рад или м | stable |  |
| `mass` | rigid, tyre | масса, кг | stable |  |
| `mesh` | shape; collider | сетка из файла; коллайдер из её треугольников, со впадинами (неподвижное тело) | stable |  |
| `mode` | effect | режим силы: impulse или constant | stable |  |
| `model` | часть | модель датчика - строка его таблицы `data/sensors/<вид>.csv` | stable |  |
| `mouse` | bind | мышь | stable |  |
| `near` | lens | ближе не видит, м | stable |  |
| `noise` | sensor | показания с шумом паспорта | stable |  |
| `none` | texture; effect | без текстуры; у силы - без спада | stable |  |
| `normal_file` | texture | файл карты нормалей, зелёный вверх | stable |  |
| `object` | часть; target, track | id и флаги объекта; у target и track - id объекта | stable |  |
| `offset` | collider | смещение от центра тела, м | stable |  |
| `opacity` | shape | непрозрачность 0-1 | stable |  |
| `parallel` | cells | банок параллельно | stable |  |
| `parent` | object | id родителя | stable |  |
| `path` | часть | путь цели: скорость, круг, сглаживание | stable |  |
| `pattern` | часть | раскладка массива | stable |  |
| `physics` | строка | настройки решателя физики | stable |  |
| `plane` | shape; action | плоскость; у действия - ведёт цель по плоскости | stable |  |
| `play` | clip | ▶ проигрывает секвенцию | stable |  |
| `point` | kind | лампа - во все стороны | stable |  |
| `pose` | часть | положение, поворот и цвет объекта | stable |  |
| `position` | pose, shape, key | где, м | stable |  |
| `projector` | stereo | у камеры проектор узора | stable |  |
| `r0` | cells | внутреннее сопротивление, Ом | stable |  |
| `r1` | cells | сопротивление RC-звена, Ом | stable |  |
| `radial` | effect | поле от центра или к центру | stable |  |
| `radius` | pattern, effect, tyre | радиус, м | stable |  |
| `range` | kind | дальность света, м | stable |  |
| `record_from` | world | начало дубля, с времени мира; раньше `duration` | stable |  |
| `reference` | encoder | координата сустава при нулевом счёте, рад или м | stable |  |
| `repeats` | texture | повторов на метр | stable |  |
| `resistance` | dcmotor | сопротивление обмотки, Ом | stable |  |
| `rest` | spring | длина пружины без нагрузки, м | stable |  |
| `restitution` | rigid | упругость удара 0-1 | stable |  |
| `right_trigger` | bind | правый курок | stable |  |
| `right_x` | bind | правый стик по x | stable |  |
| `right_y` | bind | правый стик по y | stable |  |
| `rigid` | часть | твёрдое тело: масса, трение, скорость | stable |  |
| `rolling` | tyre | коэффициент сопротивления качению | stable |  |
| `rotation` | pose, shape, collider, key | поворот углами Эйлера, градусы | stable |  |
| `rotation_step` | pattern | поворот от копии к копии, градусы | stable |  |
| `rx` | степень | поворот вокруг x | stable |  |
| `ry` | степень | поворот вокруг y | stable |  |
| `rz` | степень | поворот вокруг z | stable |  |
| `scene` | заголовок | первая строка файла: `# Kinetica scene` | stable |  |
| `seed` | pattern, sensor | зерно случайности | stable |  |
| `sensor` | строка | датчик | stable |  |
| `sequence` | строка | секвенция - анимация по ключам | stable |  |
| `series` | cells | банок последовательно | stable |  |
| `shadows` | kind | свет отбрасывает тени | stable |  |
| `shape` | часть; effect | форма и её размер; у силы - направленная или радиальная | stable |  |
| `size` | world, shape, collider | полные размеры [x, y, z], м | stable |  |
| `sleeping` | physics | тела засыпают | stable |  |
| `smooth` | path; interp | сгладить путь; плавная интерполяция ключа | stable |  |
| `soc` | cells | заряд 0-1 | stable |  |
| `softness` | kind | мягкость края конуса, градусы | stable |  |
| `sonar` | датчик | ультразвуковой дальномер | stable |  |
| `spec` | часть | паспорт датчика - ключи столбцов его таблицы (контракт sensors) | stable |  |
| `speckle` | texture | крапинка | stable |  |
| `speed` | path; action | скорость: пути, м/с; действия, м/с или рад/с | stable |  |
| `sphere` | shape, collider | шар | stable |  |
| `spin` | rigid | угловая скорость [x, y, z], рад/с - первое имя: читается, пишется `angular_velocity` | obsolete | `angular_velocity` |
| `spot` | kind | прожектор - конусом | stable |  |
| `spring` | часть | пружина подвески | stable |  |
| `start` | effect | когда толчок, с | stable |  |
| `step` | pattern; interp | шаг между копиями, м; ступенчатая интерполяция ключа | stable |  |
| `stereo` | датчик | стереокамера | stable |  |
| `stiffness` | drive, spring | жёсткость, Н·м/рад или Н/м | stable |  |
| `stone` | texture | камень | stable |  |
| `stop` | bridge | как мост стоит при нуле: coast или brake | stable |  |
| `strength` | effect | сила поля: м/с у толчка, м/с² у постоянной | stable |  |
| `substeps` | physics | подшагов решателя на кадр | stable |  |
| `sun` | kind | солнце - свет издалека | stable |  |
| `switches` | bridge | сопротивление ключей моста, Ом | stable |  |
| `tag` | effect | на тела с этой меткой; пусто - на все | stable |  |
| `tags` | часть | метки объекта | stable |  |
| `target` | часть; drive; action | у камеры - на что смотрит; у привода - уставка, рад или м; у действия - объект | stable |  |
| `template` | pattern | id шаблона массива | stable |  |
| `texture` | часть | текстура формы | stable |  |
| `time` | clip, key | время, с | stable |  |
| `toggle` | bind | нажатие переключает | stable |  |
| `torque` | effect | момент: крутит тело, на котором стоит, вокруг своей оси +Y | testing |  |
| `touch` | датчик | датчик касания | stable |  |
| `track` | часть | дорожка секвенции - объект | stable |  |
| `trigger` | collider | только замечает касание, не толкает | stable |  |
| `turn` | path, waypoint; action | у пути - метров на радиан поворота; у точки - поворот, градусы; у действия - поворачивает цель | stable |  |
| `tx` | степень | сдвиг по x | stable |  |
| `ty` | степень | сдвиг по y | stable |  |
| `tyre` | часть | шина колеса | stable |  |
| `tz` | степень | сдвиг по z | stable |  |
| `unsprung` | tyre | неподрессоренная масса, кг | stable |  |
| `upper` | limit | верхний предел, рад или м | stable |  |
| `vector2` | action | вид действия: два числа x, y | stable |  |
| `velocity` | rigid, key; drive | скорость [x, y, z], м/с; у привода - уставка скорости, рад/с или м/с | stable |  |
| `visible` | object | виден | stable |  |
| `voltage` | dcmotor | напряжение, В | stable |  |
| `waypoint` | часть | точка пути [x, y, z], м, и поворот | stable |  |
| `wheel` | строка | колесо | stable |  |
| `width` | stereo; tyre | ширина кадра, пикселей; у шины - ширина, м | stable |  |
| `world` | строка | мир: тяжесть, размер, время проигрывания | stable |  |
| `wrench` | датчик | датчик силы и момента | stable |  |

## 10. Пакет Python

Пакет `kinetica` (`clients/python/kinetica`) - объектная модель над операциями и SDK плагинов. Его публичное имя - класс,
функция, метод или свойство без `_` в начале; у `draw` - его методы. Имя здесь значит то же навсегда; рядом - что оно
делает и какую операцию зовёт.

| Имя | Что, операция | Ступень |
|---|---|---|
| `Battery` | батарея | stable |
| `Body` | форма с ролями: твёрдое тело, коллайдер | stable |
| `Body.add_controller` | контроллер цели - `controller_add` | stable |
| `Body.add_goal` | цель ИК для эффектора - `assembly_goal` | stable |
| `Body.add_imu` | IMU на грань - `assembly_sensor` | stable |
| `Body.add_lidar` | лидар на грань - `assembly_sensor` | testing |
| `Body.add_sonar` | дальномер на грань - `assembly_sensor` | stable |
| `Body.add_touch` | датчик касания на грань - `assembly_sensor` | stable |
| `Body.add_wheel` | колесо - `assembly_wheel` | stable |
| `Body.attach` | готовое звено на грань - `assembly_preset` | stable |
| `Body.follow` | цель идёт по пути - `assembly_path` | stable |
| `Body.grip` | закрыть или открыть схват - `assembly_grip` | stable |
| `Body.gripped` | что говорит схват: open, closing, holding | stable |
| `Body.ignores` | метки того, с чем нет контакта - `scene_collider` | stable |
| `Body.pose` | цель сразу в позу - `assembly_goal_pose` | stable |
| `Body.reach` | обратная кинематика - `assembly_reach` | stable |
| `Body.roles` | роли тела | stable |
| `Body.sensors` | датчики на теле | stable |
| `Body.velocity` | скорость тела сейчас, м/с | stable |
| `Camera` | камера сцены | stable |
| `Camera.aim` | камера с целью - `camera_aim` | stable |
| `Camera.free` | камера снова свободная - `camera_aim` | stable |
| `Camera.target` | на что смотрит камера с целью | stable |
| `check` | ответ операции или OperationError | stable |
| `Client` | связь с окном по API | stable |
| `Client.call` | ответ одной операции | stable |
| `Client.close` | закрыть связь | stable |
| `command` | функция файла - команда редактора: кнопка, операция, инструмент MCP | stable |
| `component` | класс - компонент: его поля в карточке объекта | stable |
| `connect` | сцена окна редактора по его API: `ke.connect(port)` | stable |
| `Controller` | контроллер: действия и привязки | stable |
| `Controller.actions` | имена действий | stable |
| `Core` | связь с `kinetica-core` по каналам | stable |
| `Core.call` | ответ одной операции | stable |
| `Core.close` | закрыть `kinetica-core` | stable |
| `draw.box` | рёбра коробки в виде | stable |
| `draw.circle` | окружность в виде | stable |
| `draw.clear` | убрать свои рисунки | stable |
| `draw.fill` | залитый рисунок - `view_draw {fill}` | stable |
| `draw.label` | надпись у точки мира | stable |
| `draw.line` | ломаная в виде | stable |
| `draw.points` | точки в виде | stable |
| `draw.segments` | отрезки в виде | stable |
| `Driver` | драйвер мотора | stable |
| `Driver.battery` | его батарея | stable |
| `Driver.joint` | его сустав | stable |
| `Editor` | окно как объекты: выделение - `selection_get`, `selection_set` | stable |
| `Editor.select` | выделить объекты - `selection_set` | stable |
| `Editor.selection` | выделенные объекты, активный последним | stable |
| `Effort` | датчик усилия | stable |
| `Encoder` | энкодер | stable |
| `field` | аргумент команды или поле компонента: значение по умолчанию, единица, предел | stable |
| `field.schema` | схема поля | stable |
| `find_core` | путь к `kinetica-core` | stable |
| `Force` | сила сцены | stable |
| `Force.acts_on` | метка тел, на которые действует | stable |
| `Force.fire` | толчок сейчас - `force_fire` | stable |
| `Gizmo` | гизмо: инструмент и оси - `gizmo_get`, `gizmo_set` | stable |
| `Group` | группа | stable |
| `Group.members` | её объекты | stable |
| `Group.ungroup` | распустить - `scene_ungroup` | stable |
| `Imu` | IMU | stable |
| `Joint` | сустав | stable |
| `Joint.add_effort` | датчик усилия - `assembly_sensor` | stable |
| `Joint.add_encoder` | энкодер - `assembly_encoder` | stable |
| `Joint.add_wrench` | датчик силы и момента - `assembly_sensor` | stable |
| `Joint.coordinates` | координаты сустава сейчас, м и рад | stable |
| `Joint.drive` | привод степени - `assembly_drive` | stable |
| `Joint.power` | мотор от батареи - `assembly_power` | stable |
| `Joint.servo` | есть ли у главной степени сервопривод | stable |
| `Light` | свет | stable |
| `Link` | звено, поставленное заготовкой: тело и его сустав | stable |
| `main` | запуск файла как плагина: `python -m kinetica.plugin plugin.py` | stable |
| `Objects` | объекты сцены: перебор, по id или по имени - `scene_list`, `scene_get` | stable |
| `Objects.tagged` | объекты с меткой - `scene_list {tag}` | stable |
| `on_change` | вызов, когда компонент плагина поставили, изменили или сняли | stable |
| `on_play` | вызов при ▶ | stable |
| `on_stop` | вызов при ■ Стоп | stable |
| `on_tick` | вызов каждого кадра, пока сцена играет | stable |
| `OperationError` | ошибка операции: `{ok: false, error}` | stable |
| `Operations` | любая операция по имени: `ed.<операция>(**аргументы)` | stable |
| `PointWheel` | колесо точкой своего тела | stable |
| `process_pool` | пул рабочих процессов плагина (`ProcessPoolExecutor`), гаснущий вместе с плагином | stable |
| `PROTOCOL_VERSION` | версия протокола, под которую собран пакет: при неизвестной операции он сверяет её с программой | stable |
| `Roles` | роли: rigid, collider - `scene_role` | stable |
| `Roles.add` | добавить роль | stable |
| `Roles.remove` | снять роль | stable |
| `Scene` | сцена как объекты; `Scene()` - без окна, через `kinetica-core` | stable |
| `Scene.add` | форма, как добавляет кнопка - `scene_add` | stable |
| `Scene.add_camera` | камера - `scene_add_camera` | stable |
| `Scene.add_force` | сила - `scene_add_force` | stable |
| `Scene.add_joint` | сустав между телами - `assembly_joint` | stable |
| `Scene.add_light` | свет - `scene_add_light` | stable |
| `Scene.close` | закрыть сцену без окна | stable |
| `Scene.command` | команды роботу в работе - `robot_command` | stable |
| `Scene.delete` | убрать объекты, как Del - `scene_delete` | stable |
| `Scene.describe` | сцена словами - `scene_describe` | stable |
| `Scene.duplicate` | копии объектов, как Ctrl+D - `scene_duplicate` | stable |
| `Scene.group` | сгруппировать - `scene_group` | stable |
| `Scene.key` | ключ секвенции - `sequence_key` | stable |
| `Scene.make_array` | массив копий - `scene_array` | stable |
| `Scene.measure` | каналы замера - `sim_measure` | stable |
| `Scene.move` | сдвинуть объекты, м - `scene_move` | stable |
| `Scene.new` | пустая сцена - `scene_new` | stable |
| `Scene.open` | открыть файл сцены - `scene_open` | stable |
| `Scene.pause` | пауза - `sim_pause` | stable |
| `Scene.play` | ▶ - `sim_play` | stable |
| `Scene.push` | толчок телу, Н·с - `sim_impulse` | stable |
| `Scene.put` | объект из куска файла сцены - `scene_put` | stable |
| `Scene.redo` | повторить - `edit_redo` | stable |
| `Scene.restore` | сцена сессии журнала после строки - `journal_restore` | stable |
| `Scene.run` | секунды симуляции, потом пауза - `sim_step` | stable |
| `Scene.save` | сохранить сцену - `scene_save` | stable |
| `Scene.scale` | масштабировать объекты - `scene_scale` | stable |
| `Scene.screenshot` | снимок вида - `view_screenshot` | stable |
| `Scene.seek` | бегунок секвенции - `sequence_seek` | stable |
| `Scene.sessions` | сессии журнала - `journal_sessions` | stable |
| `Scene.show_all` | показать скрытое - `scene_show_all` | stable |
| `Scene.step` | кадры симуляции, потом пауза - `sim_step` | stable |
| `Scene.stop` | ■ Стоп - `sim_stop` | stable |
| `Scene.time` | время мира, с - `sim_state` | stable |
| `Scene.turn` | повернуть объекты, градусы - `scene_turn` | stable |
| `Scene.undo` | отменить - `edit_undo` | stable |
| `Scene.ungroup` | распустить группы - `scene_ungroup` | stable |
| `SceneObject` | объект сцены по id | stable |
| `SceneObject.block` | его кусок файла сцены - `scene_get` | stable |
| `SceneObject.delete` | убрать - `scene_delete` | stable |
| `SceneObject.duplicate` | копия - `scene_duplicate` | stable |
| `SceneObject.inside` | что сейчас внутри этой зоны | stable |
| `SceneObject.move` | сдвинуть, м - `scene_move` | stable |
| `SceneObject.name` | имя | stable |
| `SceneObject.ref` | выражение, которым он назван: `scene.objects["Шасси"]` | stable |
| `SceneObject.scale` | масштабировать - `scene_scale` | stable |
| `SceneObject.tag` | поставить или снять метку - `scene_tag` | stable |
| `SceneObject.tags` | метки | stable |
| `SceneObject.turn` | повернуть, градусы - `scene_turn` | stable |
| `Sensor` | датчик | stable |
| `Sensor.model` | модель - строка таблицы | stable |
| `Sensor.parent` | на чём стоит | stable |
| `Sensor.problem` | почему не читает | stable |
| `Sensor.set_spec` | паспорт модели и поверх свои числа - `sensor_spec` | stable |
| `Sensor.spec` | паспорт `{ключ: число}` | stable |
| `show` | значения и слова панели из плагина - `plugin_panel_set` | stable |
| `Sonar` | ультразвуковой дальномер | stable |
| `Lidar` | лидар: лучи по паспорту, дальность каждого | testing |
| `Touch` | датчик касания | stable |
| `ui` | панель плагина как данные | stable |
| `ui.button` | кнопка - команда | stable |
| `ui.chart` | график: линии - значения панели; по времени прогона, а с `x` - по шагу (поколению, итерации); и в нижней панели «Графики» | stable |
| `ui.choice` | выбор из слов | stable |
| `ui.label` | надпись | stable |
| `ui.number` | поле числа | stable |
| `ui.panel` | панель плагина: вкладка справа или снизу | stable |
| `ui.row` | строка панели | stable |
| `ui.section` | раздел панели | stable |
| `ui.slider` | ползунок | stable |
| `ui.svg` | картинка в панели: SVG, который плагин присылает значением панели (`show(имя='<svg…>')`), по ширине панели, не выше `height` | stable |
| `ui.text` | текстовое поле | stable |
| `ui.toggle` | переключатель | stable |
| `ui.value` | показываемое значение | stable |
| `ui.vector` | поле [x, y, z] | stable |
| `View` | камера вида - `view_get`, `view_look`, `view_frame` | stable |
| `View.frame` | объект или вся сцена в кадре - `view_frame` | stable |
| `View.look` | глаз в `eye`, взгляд на `target` - `view_look` | stable |
| `WheelModule` | части колёсного модуля | stable |
| `WheelModule.duty` | скважность драйвера -1..1 | stable |
| `World` | мир: тяжесть, размер, физика - `world_get`, `world_set` | stable |
| `World.duration` | время проигрывания, с; 0 - без конца | stable |
| `World.gravity` | тяжесть [x, y, z], м/с² | stable |
| `World.record_from` | начало дубля, с; раньше `duration` | stable |
| `World.size` | размер мира [x, y, z], м | stable |
| `World.substeps` | подшагов физики на кадр | stable |
| `Wrench` | датчик силы и момента | stable |

### Слова аргументов Python

Именованный аргумент функции пакета - слово словаря аргументов (раздел 6) или строка здесь: слова, которые есть только у
Python. Короткие имена параметров, которые достались от первых версий (`g`, `s`, `n`), - obsolete: рядом слово файла
сцены, которым их назвать в новой функции.

| Слово | Смысл, единица | Ступень | Вместо |
|---|---|---|---|
| `annotation` | тип из подписи функции, по которому поле строит свою схему | stable |  |
| `answer` | ответ операции | stable |  |
| `center` | центр [x, y, z], м - как `center_of_mass` в файле сцены и во всех стандартах 3D | stable |  |
| `centre` | центр окружности или коробки рисунка [x, y, z], м | obsolete | `center` |
| `choices` | слова, из которых выбирают | stable |  |
| `command` | команда, которую вызывает кнопка панели | stable |  |
| `default` | значение по умолчанию | stable |  |
| `executable` | путь к программе `kinetica-core` | stable |  |
| `fn` | функция, которую делают крючком | stable |  |
| `g` | тяжесть [x, y, z], м/с² | obsolete | `gravity` |
| `gravity` | тяжесть [x, y, z], м/с² - слово файла сцены | stable |  |
| `help` | справка для человека | stable |  |
| `host` | адрес машины окна | stable |  |
| `initargs` | аргументы `initializer` (как у `ProcessPoolExecutor`) | stable |  |
| `initializer` | функция, которую рабочий процесс пула зовёт при старте (как у `ProcessPoolExecutor`) | stable |  |
| `keep_children` | убрать объект, оставив вложенные | stable |  |
| `label` | подпись на кнопке или поле | stable |  |
| `max_workers` | сколько рабочих процессов у пула; нет - по числу ядер (как у `ProcessPoolExecutor`) | stable |  |
| `n` | подшагов физики на кадр | obsolete | `substeps` |
| `obj` | объект сцены | stable |  |
| `op` | имя операции | stable |  |
| `opens` | расширения файлов, которые команда открывает | stable |  |
| `outlines` | контуры заливки: строки `M x y L x y C x1 y1 x2 y2 x y Z` | stable |  |
| `port` | порт API окна | stable |  |
| `pose` | поза: `targets` - уставки приводов, `pose` - сразу | stable |  |
| `py` | строка Python, которой операцию покажет консоль | stable |  |
| `range` | предел значения [от, до] | stable |  |
| `s` | размер мира [x, y, z], м | obsolete | `size` |
| `seconds` | секунды | stable |  |
| `substeps` | подшагов физики на кадр - слово файла сцены | stable |  |
| `tags` | метки | stable |  |
| `title` | заголовок панели, раздела или графика | stable |  |
| `unit` | единица - для человека | stable |  |
| `value` | значение | stable |  |
| `word` | слово: метка или роль | stable |  |
| `world` | такт мира: функция получает тела и отвечает силами | stable |  |
| `x` | значение панели - горизонтальная ось графика `ui.chart` (шаг: поколение, итерация), имя или `{имя: слова}`; без него ось - время прогона | stable |  |
