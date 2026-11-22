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
cd tools && python3 -m unittest test_agent_eval test_plugin_sdk test_editor_mcp test_scene_core test_examples test_names test_hosts_agree   # стенд испытателей, SDK, MCP, ke.Scene() - без окна; test_hosts_agree поднимает одно скрытое окно
build/kinetica --agent-eval --api 47801 --scene examples/ur5e.kscene   # окно без показа; без --agent-eval - человеку; к --api ходят клиент и MCP ke-editor (.mcp.json)
build/kinetica-core --version; python3 examples/python/01_hello.py          # без окна: ke.Scene()
python3 tools/ke_editor.py scene_list                                  # одна операция из терминала: <операция> ['<JSON аргументов>']
```

Остальные ключи запуска (`--script`, `--frames`, `--screenshot`, `--language`…) разбирает `editor/main.cpp`.

Код выхода тестов = число ошибок. Тест ядра: функция в `tests/*Tests.cpp`, строка в `tests/Tests.h`,
`run(...)` в `tests/main.cpp`. Группы самотеста — `runBuilderSelfTest()` в `editor/SelfTest.cpp`.
CI проверяет абляцию тремя `git grep` (задача `ablation` в `.github/workflows/ci.yml`; локально те же, пустой
вывод = чисто): словарь нежёсткой физики только в `tests/AblationTests.cpp`, Box3D только в
`plugins/physics/box3d/`, в `src/` нет Qt. Словарь ищется подстрокой без учёта регистра - и в комментариях,
справке операций, докстрингах: «smoke test», «cloth» роняют CI.

Файлы в `CMakeLists.txt` перечислены поимённо, без glob: новый файл — в список `kecore`, `kinetica` или `ke_tests`,
шейдер — в `qt_add_resources`. Программа из `build/` берёт `examples/`, `data/`, `clients/python/`, `plugins/` из дерева
исходников (`editor/Resources.h`): правка Python и CSV пересборки не требует.

## Устройство

- **`src/` — `kecore`**, C++17 без Qt; слои снизу вверх: `math`, `core`, `spatial`, `devices`, `electro`, `scene`,
  `physics` (контракт движка), `sim`. Box3D (`extern/box3d`, подмодуль) подключает только `plugins/physics/box3d/`.
- **`editor/` — окно `kinetica`** (Qt 6), клиент API операций; `MainWindow*`, `Viewport*`, `SceneBuilder*` разрезаны
  по темам: метод ищется по теме файла. Симуляция крутится в потоке `SimController`: окно шлёт `post()` и читает
  неизменяемые `Frame`; такт плагинов (`RunHooks`) идёт мимо потока окна - через него не идёт ничего, чего ждёт шаг.
- **Скрипт и плагин.** Скрипт (`examples/scripts/*.py`, ключ `--script`, операция `script_run`) строит или меняет
  сцену и заканчивается - один шаг отмены. Плагин (`plugins/<имя>/plugin.py`) - отдельный процесс Python, клиент
  того же API, даёт редактору новое умение; хост читает ещё папки, добавленные в окне, `<app data>/plugins` и
  `KE_PLUGINS`; остальные плагины проекта - в репозитории kinetica-plugins.
- **Два хозяина операций:** окно и `kinetica-core` (без окна, `ke.Scene()`) зовут одно ядро — `src/api`, `src/scene/Document`,
  `src/sim/WorldOps`, `src/scene/LiveEdit`, `src/scene/ArgsSchema`; у окна остаются часы, поток, выделение и вид.
- **API = UI.** Любое действие — операция (ядро `SceneOps`, окно `EditorOps`) с JSON-ответом; кнопка, консоль,
  Python и агент по сети (`--api`, MCP `tools/ke_editor_mcp.py`) зовут одни и те же операции. Правка — шаг
  отмены и строка в журнале JSON lines, который только дописывается (`<app data>/ops-journal.jsonl`, на Linux
  `~/.local/share/Kinetica/Kinetica/`; другой файл — `KE_JOURNAL`). Новая возможность сначала операция, потом кнопка.
- **Операция.** Ядра — `SceneOp` в `src/scene/SceneOps*.cpp`: видит только граф; выделение, время, отмена и файлы —
  у клиента. Окна — `add`/`addSync` в `editor/EditorOps*.cpp`. Строка `args` — и справка, и схема
  (`src/scene/ArgsSchema.h`): поле, которое код читает, обязано в ней быть, вызов с лишним полем отвергается.
  Имена не переименовываются и не исчезают: реестр, словарь слов и грамматика - `docs/contracts/names.md`, только
  дополняется; новое имя или слово - сначала его строка (замок `test_names`).
- **Правка и Пуск.** В «Правке» окно рисует граф сцены как он написан (`EditView`) и ничего не считает: роль только
  помечает объект. «Пуск» один раз строит из графа `RobotWorld`, «Стоп» выбрасывает его — сцена снова как до пуска,
  правки в игре не сохраняются.
- **Python** (только стандартная библиотека): `clients/python/kinetica` — объектная модель поверх операций для консоли окна
  и скриптов (`import kinetica as ke`) и SDK плагинов (`plugin.py`); `tools/` — клиент `ke_editor.py`, сервер MCP,
  стенд испытателей `agent_eval.py`.
- **Датчик = композиция:** крепление + зонд физики (`sim/LiveSensors`, только через `PhysicsWorld`) + сигнальная цепочка
  паспорта (`devices/Signal.h`) + выход. Вид — рецепт, модель — строка таблицы `data/sensors/<вид>.csv`
  (поля и ключи — `scene/SensorSpec.h`); датчик хранит свой паспорт в сцене. Новая модель — строка CSV, не код.
- **Контракты** — `docs/contracts/*.md`: заголовок, документ и тесты меняются одним коммитом. **Конституция** —
  `docs/constitution.md`: статьи с проверкой каждой, долги и путь; изменение, нарушающее статью, исправляется или
  идёт с поправкой конституции.

## Контракт наблюдаемости

Управление робота читает только неидеальные датчики (шум, дискретность, задержка, отказы). Истинные позы и
углы Box3D, id пикселей и глубина OpenGL — только привилегированному контуру проверки. Обратная связь
суставов — через модель энкодера. ИК/FK по командам — прогноз, а не исполнение: в «Правке» моторов нет.

## Правила

- Интерфейс, документация и коммиты на русском; код и комментарии — на английском. Коммит начинается с
  области: «Ядро: …», «Физика: …», «Окно: …», «Рука: …».
- Каждый файл начинается с абзаца: что в нём и где остальное.
- Абляция постоянно: шаг удаляет в том же коммите то, что сделал лишним (прежнюю копию, временный тест, файл без
  читателя), и ничего не добавляет «на будущее». В этот файл и в документы идёт только то, чего не прочесть по коду.
- Только стандартный C++17 и Qt, без `#ifdef _WIN32`.
- Один результат на любом компиляторе: `ke::sin` и др. из `math/ElementaryFunctions.h`, не `std::`; случайность —
  `core/Random.h`, не `std::*_distribution`; шаг мира не зависит от настенных часов и порядка обхода `unordered_*`.
  Вещественное ядра — `ke::Real` (float, с `-DKE_DOUBLE=ON` double), литералы `1.5_r`.
- СИ, правая система, Y вверх, радианы; в `*.kscene` углы — градусы Эйлера.
- Числа в файлы — через `core/Format.h`: Qt на Linux включает локаль, и `printf` пишет `0,120`.
- Запуски без человека (`--self-test`, `--agent-eval`, снимки) окон не показывают; новый такой ключ — в
  `unattended()` и `unattendedArgs()` в `editor/main.cpp`; такой запуск не ждёт экрана (он может спать).
