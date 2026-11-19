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
cd tools && python3 -m unittest test_agent_eval
build/kinetica --agent-eval --api 47801 --scene examples/ur5e.kscene   # окно без показа; клиент tools/ke_editor.py
```

Код выхода тестов = число ошибок. Тест ядра: функция в `tests/*Tests.cpp`, строка в `tests/Tests.h`,
`run(...)` в `tests/main.cpp`. Группы самотеста — `runBuilderSelfTest()` в `editor/SelfTest.cpp`.
CI (`.github/workflows/ci.yml`) ещё проверяет абляцию: словарь нежёсткой физики есть только в
`tests/AblationTests.cpp`, Box3D подключён только в `plugins/physics/box3d/`, в `src/` нет Qt.

## Устройство

- **`src/` — `kecore`**, C++17 без Qt: `math`, `core`, `spatial`, `devices`, `electro`, `robot`, `scene`
  (модель сцены, файл `*.kscene` строка-на-факт, правки с обратными `SceneEdits`, реестр операций `SceneOps`,
  IK `Kinematics`), `physics` (контракт движка), `sim` (`RobotWorld` — живой мир робота, `Simulation`, `Frame`).
- **`plugins/physics/box3d/`** — единственное место с `box3d.h`.
- **`editor/` — окно `kinetica`** (Qt 6), клиент API операций. `MainWindow*`, `Viewport*`, `SceneBuilder*`
  разрезаны по темам: метод ищется по теме файла. Симуляция крутится в потоке `SimController`; окно шлёт
  команды `post()` и читает неизменяемые `Frame`. Карточка инспектора — список полей `FieldForm`
  (`InspectorWidgets.h`) над черновиком объекта; правку пишет в сцену общий `commitDraft`.
- **API = UI.** Любое действие — операция (ядро `SceneOps`, окно `EditorOps`) с JSON-ответом; кнопка, консоль,
  Python и агент по сети (`--api`, MCP `tools/ke_editor_mcp.py`) зовут одни и те же операции. Правка — шаг
  отмены и строка в журнале JSON lines, который только дописывается. Новая возможность сначала операция,
  потом кнопка.
- **Датчик = композиция:** крепление + зонд физики (`src/sim`, только через `PhysicsWorld`) + сигнальная цепочка
  паспорта (`devices/Signal.h`) + выход. Вид — рецепт, модель — строка таблицы `catalog/sensors/<вид>.csv`
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
- Только стандартный C++17 и Qt, без `#ifdef _WIN32`. `ke::sin` и др. из `math/ElementaryFunctions.h`, не `std::`.
- Числа в файлы — через `core/Format.h`: Qt на Linux включает локаль, и `printf` пишет `0,120`.
- Запуски без человека (`--self-test`, `--agent-eval`, снимки) окон не показывают; новый такой ключ — в
  `unattended()` и `withoutDesktopGrab()` в `editor/main.cpp`.
- Перенос из `phys_realflow_editor@ca3684e` / `phys_realflow@fd0214c`: `rf`→`ke`, `.rfscene`→`.kscene`,
  имена операций API не меняются. Решатели SDK не переносятся.

## Дальше

Зонды датчиков в мире робота (`src/sim`): инерциальный, луч, сустав, контакт — к цепочкам паспорта.
SDK плагина (`docs/contracts/editor-plugin.md`): схема, команда, описание панели; кнопки из реестра команд;
плагины-процессы. Затем `controller_input`, импорт URDF/SDF, стерео, дрон.
