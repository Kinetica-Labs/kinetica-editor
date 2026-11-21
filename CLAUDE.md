# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Kinetica Editor — редактор робототехники: рука-манипулятор, дрон, колёсный робот, их суставы, приводы
и неидеальные датчики. Твёрдые тела считает Box3D (`extern/box3d`) за границей `src/physics/PhysicsWorld.h`;
своей физики нет. Газа, жидкостей, ткани, мягких тел, частиц, разрушения нет и не будет — даже выключенных.

## Сборка и проверка

```sh
git submodule update --init
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$HOME/Qt/6.7.3/gcc_64 -DKE_TESTS=ON
cmake --build build -j
build/ke_tests                                   # ядро; KE_TEST=<подстрока> — выборочно
build/kinetica --self-test                       # окно: жмёт кнопки, тащит гизмо; KE_EDITOR_TEST=<группа>
cd tools && python3 -m unittest test_agent_eval test_plugin_sdk test_editor_mcp   # стенд испытателей, SDK плагинов, сервер MCP - без окна
build/kinetica --agent-eval --api 47801 --scene examples/ur5e.kscene   # окно без показа; клиент tools/ke_editor.py
build/kinetica --scene examples/ur5e.kscene --api 47801                # окно человеку; с --api к нему ходят MCP ke-editor (.mcp.json) и клиент
python3 tools/ke_editor.py scene_list                                  # одна операция из терминала: <операция> ['<JSON аргументов>']
build/kinetica --scene examples/ur5e.kscene --script examples/scripts/cubes_row.py   # скрипт: строит сцену кодом и заканчивается
build/kinetica --scene examples/ur5e.kscene --frames 120 --window --screenshot /tmp/ur5e.png   # снимок без показа окна после N кадров игры; --edit — сцена как написана
```

Код выхода тестов = число ошибок. Тест ядра: функция в `tests/*Tests.cpp`, строка в `tests/Tests.h`,
`run(...)` в `tests/main.cpp`. Группы самотеста — `runBuilderSelfTest()` в `editor/SelfTest.cpp`.
CI (`.github/workflows/ci.yml`) ещё проверяет абляцию: словарь нежёсткой физики есть только в
`tests/AblationTests.cpp`, Box3D подключён только в `plugins/physics/box3d/`, в `src/` нет Qt. Это три `git grep`
задачи `ablation` — они же запускаются локально, пустой вывод = чисто. Словарь ищется подстрокой без учёта
регистра и в комментариях, справке операций, докстрингах Python: «a gas's flow», «smoke test», «cloth» роняют CI.

Файлы в `CMakeLists.txt` перечислены поимённо, без glob: новый файл — в список `kecore`, `kinetica` или `ke_tests`,
шейдер — в `qt_add_resources`. Программа из `build/` берёт `examples/`, `data/`, `python/`, `plugins/` из дерева
исходников (`editor/Resources.h`): правка Python и CSV пересборки не требует.

## Устройство

- **`src/` — `kecore`**, C++17 без Qt: `math`, `core`, `spatial`, `devices`, `electro`, `scene`
  (модель сцены, файл `*.kscene` строка-на-факт, правки с обратными `SceneEdits`, реестр операций `SceneOps`,
  IK `Kinematics`), `physics` (контракт движка), `sim` (`RobotWorld` — живой мир робота, `Simulation`, `Frame`).
- **`plugins/physics/box3d/`** — единственное место с `box3d.h`.
- **`examples/scripts/*.py`** — скрипты: файл Python строит или меняет сцену и заканчивается (операция `script_run`,
  «Файл → Выполнить скрипт…», ключ `--script`); всё, что он сделал, — один шаг отмены. Пример сцены кодом — скрипт,
  не плагин: плагин остаётся и даёт редактору новое умение.
- **`plugins/<имя>/plugin.py`** — плагины на Python, которые поставляются с редактором (`plugins/urdf` — импорт URDF):
  хост сканирует эту папку при обычном запуске, самотест копирует нужный плагин во временную папку. Плагин —
  отдельный процесс Python, клиент того же API. Остальные плагины проекта — в репозитории kinetica-plugins; хост
  читает ещё папки, добавленные в окне, `<app data>/plugins` и `KE_PLUGINS`.
- **`editor/` — окно `kinetica`** (Qt 6), клиент API операций. `MainWindow*`, `Viewport*`, `SceneBuilder*`
  разрезаны по темам: метод ищется по теме файла. Симуляция крутится в потоке `SimController`; окно шлёт
  команды `post()` и читает неизменяемые `Frame`. Такт плагинов (`RunHooks`) ходит между потоком симуляции и
  плагином своим потоком, мимо окна: через поток окна не идёт ничего, чего ждёт шаг. Карточка инспектора — список полей `FieldForm`
  (`InspectorWidgets.h`) над черновиком объекта; правку пишет в сцену общий `commitDraft`.
- **API = UI.** Любое действие — операция (ядро `SceneOps`, окно `EditorOps`) с JSON-ответом; кнопка, консоль,
  Python и агент по сети (`--api`, MCP `tools/ke_editor_mcp.py`) зовут одни и те же операции. Правка — шаг
  отмены и строка в журнале JSON lines, который только дописывается (`<app data>/ops-journal.jsonl`, на Linux
  `~/.local/share/Kinetica/Kinetica/`; другой файл — `KE_JOURNAL`). Новая возможность сначала операция, потом кнопка.
- **Операция.** Ядра — `SceneOp` в `src/scene/SceneOps*.cpp`: видит только граф; выделение, время, отмена и файлы —
  у клиента. Окна — `add`/`addSync` в `editor/EditorOps*.cpp`. Строка `args` — и справка, и схема
  (`src/scene/ArgsSchema.h`): поле, которое код читает, обязано в ней быть, вызов с лишним полем отвергается.
  Имена операций не переименовываются — это контракт с агентами и MCP.
- **Правка и Пуск.** В «Правке» окно рисует граф сцены как он написан (`EditView`) и ничего не считает: роль только
  помечает объект. «Пуск» один раз строит из графа `RobotWorld`, «Стоп» выбрасывает его — сцена снова как до пуска,
  правки в игре не сохраняются.
- **Python** (только стандартная библиотека): `python/kinetica` — объектная модель поверх операций для консоли окна
  и скриптов (`import kinetica as ke`) и SDK плагинов (`plugin.py`); `tools/` — клиент `ke_editor.py`, сервер MCP,
  стенд испытателей `agent_eval.py`.
- **Датчик = композиция:** крепление + зонд физики (`sim/LiveSensors`, только через `PhysicsWorld`) + сигнальная цепочка
  паспорта (`devices/Signal.h`) + выход. Вид — рецепт, модель — строка таблицы `data/sensors/<вид>.csv`
  (поля и ключи — `scene/SensorSpec.h`); датчик хранит свой паспорт в сцене. Новая модель — строка CSV, не код.
- **Контракты** — `docs/contracts/*.md` (physics, drives, sensors, operations, editor-plugin): заголовок,
  документ и тесты меняются одним коммитом.

## Контракт наблюдаемости

Управление робота читает только неидеальные датчики (шум, дискретность, задержка, отказы). Истинные позы и
углы Box3D, id пикселей и глубина OpenGL — только привилегированному контуру проверки. Обратная связь
суставов — через модель энкодера. ИК/FK по командам — прогноз, а не исполнение: в «Правке» моторов нет.

## Правила

- Интерфейс, документация и коммиты на русском; код и комментарии — на английском. Коммит начинается с
  области: «Ядро: …», «Физика: …», «Окно: …», «Рука: …».
- Каждый файл начинается с абзаца: что в нём и где остальное.
- Только стандартный C++17 и Qt, без `#ifdef _WIN32`.
- Один результат на любом компиляторе: `ke::sin` и др. из `math/ElementaryFunctions.h`, не `std::`; случайность —
  `core/Random.h`, не `std::*_distribution`; шаг мира не зависит от настенных часов и порядка обхода `unordered_*`.
  Вещественное ядра — `ke::Real` (float, с `-DKE_DOUBLE=ON` double), литералы `1.5_r`.
- СИ, правая система, Y вверх, радианы; в `*.kscene` углы — градусы Эйлера.
- Числа в файлы — через `core/Format.h`: Qt на Linux включает локаль, и `printf` пишет `0,120`.
- Запуски без человека (`--self-test`, `--agent-eval`, снимки) окон не показывают; новый такой ключ — в
  `unattended()` и `unattendedArgs()` в `editor/main.cpp`; такой запуск не ждёт экрана (он может спать).

## Конституция и путь

`docs/constitution.md` — статьи разработки с проверкой каждой (мир, действие, автор, ремесло), долги на
сегодня и путь. Изменение, нарушающее статью, исправляется или идёт с поправкой конституции. Обоснование и
источники — `docs/design/plugin-sdk-research.md`.
