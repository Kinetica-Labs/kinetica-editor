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

## Сборка

Нужны компилятор C++17, CMake 3.22 или новее и, для окна, Qt 6 (Core, Gui, Widgets, OpenGL,
OpenGLWidgets, Network). Без Qt собираются только ядро и его тесты.

```sh
git clone --recurse-submodules git@github.com:Kinetica-Labs/kinetica-editor.git
cd kinetica-editor
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt>/6.x/gcc_64
cmake --build build -j
build/ke_tests                                # тесты ядра
build/kinetica --scene examples/ur5e.kscene   # окно
build/kinetica --self-test                    # самотест окна
```

Лицензия — MIT ([LICENSE](LICENSE)); чужой код и его лицензии — [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
