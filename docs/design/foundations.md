# Основания устройства редактора

Каждое решение устройства — с работой, на которую оно опирается, и предупреждением из неё, которое мы
учли. Статьи и книги названы по авторам и году; документация промышленных систем помечена отдельно.

## Схема — интерфейс и формат из описания данных

| Решение | Опора | Учтённое предупреждение |
|---|---|---|
| Инспектор, файл сцены, проверка API и справка строятся из одной схемы полей | Pawson, *Naked Objects*, PhD thesis, Trinity College Dublin, 2004; Pawson, Matthews, *Naked Objects*, 2002 | сгенерированный интерфейс неудобен для сложных видов — свои виды остаются встроенным частям |
| Типы и поля как данные, а не классы | Yoder, Johnson, «The Adaptive Object-Model Architectural Style», WICSA 2002; Johnson, Woolf, «Type Object», 1997 | модель на метаданных трудно отлаживать — схема плоская, без наследования схем |
| Роли объекта как применяемые компоненты | Pixar USD: схемы и UsdPhysics `RigidBodyAPI`, `CollisionAPI`, `MassAPI` (промышленный стандарт, документация) | — |

## Команда — каждое действие именовано, отменяемо и видно кодом

| Решение | Опора |
|---|---|
| Каждая клавиша и кнопка — именованная, самодокументированная команда; редактор расширяют пользователи | Stallman, «EMACS: The Extensible, Customizable Display Editor», ACM SIGPLAN/SIGOA, 1981 |
| Команда как объект; отмена, запись, повтор | Gamma и др., *Design Patterns*, 1994 (Command); Berlage, «A Selective Undo Mechanism for Graphical User Interfaces Based on Command Objects», ACM TOCHI, 1994; Myers, Kosbie, «Reusable Hierarchical Command Objects», CHI 1996 |
| Весь интерфейс открыт скриптам как объектная модель (консоль Python) | Cook, «AppleScript», HOPL III, 2007 |
| Журнал команд только дописывается; восстановление из снимков и повтор | Event Sourcing (Fowler, 2005, статья практика) |

## Плагин — ядро и расширения

| Решение | Опора | Учтённое предупреждение |
|---|---|---|
| Ядро плюс плагины по контрактам | Buschmann и др., *Pattern-Oriented Software Architecture*, т. 1, 1996 (Microkernel) | — |
| Встроенное — тоже плагины; вклад в интерфейс декларативный | Birsan, «On Plug-ins and Extensible Architectures», ACM Queue, 2005 (опыт Eclipse) | запутанные зависимости между плагинами — версии контрактов в манифесте, без зависимостей плагин → плагин |
| Панель плагина описывается, тулкит рисует | Blender: панели дополнений через `layout.prop` / `layout.operator` (документация) | — |
| Плагин — отдельный процесс | Quigley и др., «ROS: an open-source Robot Operating System», 2009; расширения VS Code в отдельном процессе (документация) | задержка сети — горячие циклы остаются встраиваемыми плагинами |
| Плагины симулятора роботов: мир, модель, датчик | Koenig, Howard, «Design and Use Paradigms for Gazebo», IROS 2004 | — |
| Объект — набор компонентов | Bilas, «A Data-Driven Game Object System», GDC 2002 (доклад индустрии) | — |

## ИИ-агенты как пользователи и авторы плагинов

| Решение | Опора |
|---|---|
| Действие агента — исполняемый код (строка Python), а не JSON | Wang и др., «Executable Code Actions Elicit Better LLM Agents» (CodeAct), ICML 2024 |
| Агент пополняет библиотеку навыков-кода и строит на ней дальше (цикл улучшения через плагины) | Wang и др., «Voyager: An Open-Ended Embodied Agent with Large Language Models», 2023 |
| API и справка команд проектируются для агента | Yang и др., «SWE-agent: Agent-Computer Interfaces Enable Automated Software Engineering», 2024 |
| Ошибки агентов размечаются по таксономии | Cemri и др., «Why Do Multi-Agent LLM Systems Fail?» (MAST), 2025 |
