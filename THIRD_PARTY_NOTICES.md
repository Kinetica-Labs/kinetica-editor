# Чужой код в Kinetica Editor и его лицензии

Собственный код редактора распространяется по лицензии MIT (файл [LICENSE](LICENSE)).

Сторонние библиотеки остаются под **своими** лицензиями:

| Что | Где | Лицензия | Что требует |
|---|---|---|---|
| Box3D (Erin Catto) | сабмодуль `extern/box3d` | MIT | Приложить текст лицензии `extern/box3d/LICENSE` к распространяемой программе. |
| Qt 6 | подключается при сборке окна | LGPL-3.0 | Qt подключается динамически. Раздавая программу, приложите текст LGPL-3.0 и ссылку на исходники Qt (https://download.qt.io/) и не мешайте заменить библиотеки Qt своими. |
| Среда выполнения Microsoft Visual C++ (`vcruntime140*.dll`, `msvcp140*.dll`) | рядом с `kinetica.exe` в архиве для Windows | лицензия Visual Studio на распространяемые файлы | Только файлы из списка распространяемых (папка `VC\Redist` Visual Studio), без изменений, вместе с программой. |
| Меши UR5e (ROS-Industrial Consortium, 2018) | `examples/models/ur5e/` | BSD-3-Clause | Сохранять текст лицензии `examples/models/ur5e/LICENSE` рядом с мешами; имя консорциума не использовать для продвижения. |
| URDF и сетки столкновений UR5e (репозиторий UniversalRobots/Universal_Robots_ROS2_Description) | `examples/ur5e-urdf/` | BSD-3-Clause | Сохранять текст лицензии `examples/ur5e-urdf/LICENSE` рядом с файлами; имена авторов не использовать для продвижения. |
