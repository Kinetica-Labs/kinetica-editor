# Kinetica Editor

Редактор робототехники на C++17 и Qt 6: рука-манипулятор, дрон, колёсный робот, их суставы, приводы и
неидеальные датчики (энкодеры, IMU, сонар, стереокамера). Физика твёрдых тел — готовый движок
[Box3D](https://github.com/erincatto/box3d) за тонкой границей `src/physics/PhysicsWorld.h`
([docs/box3d-backend.md](docs/box3d-backend.md)).

Каждое действие в окне — операция API и строка Python в консоли: то же самое делают скрипт и ИИ-агент
по сети (`--api`, MCP). Плагины пишутся по контрактам [docs/contracts](docs/contracts/README.md).

Состояние: готовы ядро, модель сцены робота, физика на Box3D, мир робота с суставами, приводами и целями
ИК, всё окно редактора. Пример — UR5e с целью ИК, схватом и кубиками (`examples/ur5e.kscene`).
Дальше: SDK плагинов, датчики в живом мире, импорт URDF/SDF, стерео, дрон.

## Готовая программа для Linux (Ubuntu 22.04 и новее)

Одна папка со всеми библиотеками — ставить Qt и собирать ничего не нужно.

1. На вкладке **Actions** откройте последний зелёный прогон **CI** на ветке `main` (нужен вход в GitHub) и
   скачайте в **Artifacts** `kinetica-linux`.
2. Распакуйте и запустите:

```sh
unzip kinetica-linux.zip && tar -xzf kinetica-linux.tar.gz
./kinetica-linux/kinetica --scene kinetica-linux/usr/bin/examples/ur5e.kscene
```

Консоль Python берёт системный `python3` (на Ubuntu он есть). Скрипты и агенты: клиент API —
`kinetica-linux/usr/bin/tools/ke_editor.py`.

## Готовая программа для Windows

1. На вкладке **Actions** откройте последний зелёный прогон **CI** на ветке `main` (нужен вход в GitHub).
2. Внизу страницы, в **Artifacts**, скачайте `kinetica-windows` и распакуйте архив.
3. Запустите `kinetica.exe`. Примеры лежат рядом, в `examples` («Открыть…» начинает с этой папки);
   UR5e с целью ИК — `examples/ur5e.kscene`.

Консоли Python внизу окна нужен Python 3 ([python.org](https://www.python.org/downloads/), при установке
отметьте «Add python.exe to PATH»). Без драйвера OpenGL 3 окно предложит программный OpenGL:
`opengl32sw.dll` уже лежит рядом. Архивы прогонов GitHub хранит 90 дней.

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

```sh
build/kinetica --scene examples/ur5e.kscene   # окно со сценой UR5e (Windows: build\Release\kinetica.exe)
build/ke_tests                                # тесты ядра
build/kinetica --self-test                    # самотест окна: жмёт кнопки, тащит гизмо; код выхода = ошибки
```

Скрипты и ИИ-агенты: `build/kinetica --api 47801` слушает API на 127.0.0.1 (JSON по строке на вызов).
Клиент на Python — `tools/ke_editor.py`, сервер MCP для агентов — `tools/ke_editor_mcp.py` (подключён в `.mcp.json`).

Лицензия — MIT ([LICENSE](LICENSE)); чужой код и его лицензии — [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
