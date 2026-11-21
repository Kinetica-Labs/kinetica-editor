<p align="center">
  <img src="docs/images/kinetica-editor-logo.png" alt="Kinetica Editor" width="700">
</p>

<p align="center"><b>Редактор робототехники: собрать робота, оживить физикой, записать и повторить — руками, кодом или ИИ-агентом</b></p>

<p align="center">
  <a href="https://github.com/Kinetica-Labs/kinetica-editor/releases/latest">Скачать</a> ·
  <a href="https://kinetica-labs.github.io/kinetica-docs/">Руководство в картинках · Guide</a> ·
  <a href="docs/contracts/README.md">Контракты и плагины</a> ·
  <a href="docs/constitution.md">Конституция</a>
</p>

# Kinetica Editor

Редактор робототехники на C++17 и Qt 6: рука-манипулятор, дрон, колёсный робот, их суставы, приводы и
неидеальные датчики (энкодеры, IMU, сонар, стереокамера). Физика твёрдых тел — готовый движок
[Box3D](https://github.com/erincatto/box3d) за тонкой границей `src/physics/PhysicsWorld.h`
([docs/box3d-backend.md](docs/box3d-backend.md)).

Каждое действие в окне — операция API и строка Python в консоли: то же самое делают скрипт и ИИ-агент
по сети (`--api`, MCP). **Скрипты** — сцена кодом, как в Blender: файл Python строит или меняет сцену и
заканчивается («Файл → Выполнить скрипт…», `--script`; примеры — `examples/scripts/`, руководство —
[docs/scripts.md](docs/scripts.md)). **Плагины** остаются в редакторе и дают ему новые умения: один файл на
Python, `@command` становится кнопкой, строкой консоли и инструментом MCP для ИИ-агента; сохранили —
перезапустился ([docs/plugins.md](docs/plugins.md), контракты — [docs/contracts](docs/contracts/README.md)).
Плагины проекта — в отдельном репозитории kinetica-plugins.

Вторая программа — `kinetica-core`, та же сцена и те же операции без окна: `ke.Scene()` в Python
(`python3 examples/python/01_hello.py`) или `ke::Scene` в C++ (`examples/cpp/hello.cpp`); начать с неё —
[docs/scripts.md](docs/scripts.md), «Без окна».

Состояние: готовы ядро, модель сцены робота, физика на Box3D, мир робота с суставами, приводами и целями
ИК, всё окно редактора. Пример — UR5e с целью ИК, схватом и кубиками (`examples/ur5e.kscene`).
Дальше: панели и компоненты плагинов, датчики в живом мире, импорт URDF/SDF, стерео, дрон.

## Готовая программа

Скачайте архив своей системы со страницы последнего выпуска —
**[Releases](https://github.com/Kinetica-Labs/kinetica-editor/releases/latest)**. Ставить Qt и собирать
ничего не нужно: в архиве программа со всеми библиотеками, примеры (UR5e с целью ИК, схватом и кубиками),
пакет Python для консоли и лицензии.

**Linux** (Ubuntu 22.04 и новее) — `kinetica-linux.tar.gz`:

```sh
tar -xzf kinetica-linux.tar.gz
./kinetica-linux/kinetica --scene kinetica-linux/usr/bin/examples/ur5e.kscene
```

**Windows** (10 и 11, 64 бита) — `kinetica-windows.zip`: распакуйте и запустите `kinetica.exe`; примеры
лежат рядом, в `examples` («Открыть…» начинает с этой папки).

Консоли Python внизу окна нужен Python 3: на Ubuntu он есть, на Windows поставьте с
[python.org](https://www.python.org/downloads/) и отметьте «Add python.exe to PATH». Без драйвера
OpenGL 3 окно предложит программный OpenGL. Скрипты и ИИ-агенты: клиент API — `tools/ke_editor.py`
рядом с программой (в Linux — `kinetica-linux/usr/bin/tools/`).

Сборки каждого изменения — в **Actions**: прогон **CI** → **Artifacts** (нужен вход в GitHub).

## Сборка из исходников

Нужны компилятор C++17 (GCC 11+ или Visual Studio 2022), CMake 3.22+, Python 3 и Qt 6 — проверено с
Qt 6.7.3 (модули Core, Gui, Widgets, OpenGL, OpenGLWidgets, Network). Без Qt собираются только ядро и
его тесты. Qt проще всего поставить через [aqtinstall](https://github.com/miurahr/aqtinstall):

```sh
pip install aqtinstall
aqt install-qt linux desktop 6.7.3 linux_gcc_64 -O ~/Qt               # Linux
aqt install-qt windows desktop 6.7.3 win64_msvc2019_64 -O C:\Qt       # Windows
```

**Linux (Ubuntu):**

```sh
sudo apt install build-essential cmake ninja-build git python3 libgl1-mesa-dev libxkbcommon-x11-0 \
  libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0
git clone --recurse-submodules https://github.com/Kinetica-Labs/kinetica-editor.git
cd kinetica-editor
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$HOME/Qt/6.7.3/gcc_64
cmake --build build -j4
```

**Windows (Visual Studio 2022, «Developer PowerShell»):**

```powershell
git clone --recurse-submodules https://github.com/Kinetica-Labs/kinetica-editor.git
cd kinetica-editor
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.7.3\msvc2019_64
cmake --build build --config Release --parallel 4
C:\Qt\6.7.3\msvc2019_64\bin\windeployqt build\Release\kinetica.exe   # библиотеки Qt рядом с программой
```

## Запуск и проверка

Язык окна выбирается в **Настройки → Язык** (**Settings → Language**). Редактор сохраняет выбор, сам перезапускается
и открывает текущую сцену вместе с несохранёнными правками. Для разового запуска на английском используйте `build/kinetica --language en`.
Каталог английских подписей лежит в `translations/kinetica_en.ts`. После изменения исходных строк
обновите его командой `cmake --build build --target update_translations`.

```sh
build/kinetica --scene examples/ur5e.kscene   # окно со сценой UR5e (Windows: build\Release\kinetica.exe)
build/ke_tests                                # тесты ядра
build/kinetica --self-test                    # самотест окна: жмёт кнопки, тащит гизмо; код выхода = ошибки
build/kinetica-core --version                 # программа без окна; python3 examples/python/01_hello.py
```

Скрипты и ИИ-агенты: `build/kinetica --api 47801` слушает API на 127.0.0.1 (JSON по строке на вызов).
Клиент на Python — `tools/ke_editor.py`, сервер MCP для агентов — `tools/ke_editor_mcp.py` (подключён в `.mcp.json`).

Лицензия — MIT ([LICENSE](LICENSE)); чужой код и его лицензии — [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

<p align="center"><img src="docs/images/kinetica-labs.png" alt="Kinetica Labs" width="200"></p>
