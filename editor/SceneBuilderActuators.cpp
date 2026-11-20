// "+ Добавить компонент" → «Сочленение с приводом»: a rotary or a linear actuator about or along a world axis, between the
// active shape and the world - or the other selected shape when two are selected - in one click, where a person looks for
// what a body can get (an agent at the screen did not find joints in the magnet tool, and could not join a body to the
// world there). The window is only a client: the click runs the core's assembly_actuator, one undo step, one line of the
// journal, the same operation an agent calls - the joint, its range and its servo tuned to what the joint carries.
// The menu offers it only while the core's registry has that operation (scene/SceneOps.h): tuning a servo to its load
// needs the running world, and a window offers nothing the API cannot do.
#include "SceneBuilder.h"

#include "scene/SceneOps.h"

#include <QAction>
#include <QMenu>

using namespace ke;

void SceneBuilder::fillJointMenu(const Entity& e, QMenu* menu) {
    if (!findSceneOp("assembly_actuator")) return;
    const uint32_t other = selection_.size() == 2 ? (selection_[0] == e.id ? selection_[1] : selection_[0]) : 0;
    const SceneObject* to = other ? findObject(graph_, other) : nullptr;
    // Which one moves is the selection's order, said where it is chosen (a newcomer found it nowhere): the active one - the
    // last selected - hangs on the other.
    const QString moving = QString::fromStdString(e.name);
    menu->addSection(to ? "Сочленение с приводом: «" + moving + "» на «" + QString::fromStdString(to->name) + "»"
                        : "Сочленение с приводом: «" + moving + "» на мире");
    const QString order = to ? "\nДвижется «" + moving + "», «" + QString::fromStdString(to->name) +
                                   "» — опора. Наоборот: выберите их в другом порядке, опору последней"
                             : QString("\nВторое тело с Ctrl — сочленение между ними");
    static const char* const turns[] = {"Поворот вокруг X", "Поворот вокруг вертикали (Y)", "Поворот вокруг Z"};
    static const char* const slides[] = {"Ход вдоль X", "Ход вдоль вертикали (Y)", "Ход вдоль Z"};
    for (int kind = 0; kind < 2; ++kind)
        for (int axis = 0; axis < 3; ++axis) {
            QAction* a = menu->addAction(kind == 0 ? turns[axis] : slides[axis]);
            a->setToolTip(QString(kind == 0 ? "Шарнир с сервоприводом, настроенным под то, что он несёт: держит позу, цель — угол в карточке сочленения"
                                            : "Ползун с сервоприводом, настроенным под то, что он несёт: держит место, цель — ход в карточке сочленения") +
                          order);
            connect(a, &QAction::triggered, this, [this, link = e.id, other, kind, axis] { addActuator(link, other, kind == 0, axis); });
        }
}

void SceneBuilder::addActuator(uint32_t link, uint32_t other, bool rotary, int axis) {
    Value args = Value::map(), answer = Value::map(), direction = Value::list();
    for (int k = 0; k < 3; ++k) direction.push(Real(k == axis ? 1 : 0));
    args.set("kind", rotary ? "rotary" : "linear"), args.set("a", link), args.set("b", other), args.set("axis", direction);
    std::string error;
    flushCode(); // what was done before is said first: the journal then holds this as one assembly_actuator line, not a scene_put too
    if (!transact([&](SceneGraph& g, std::string& e) { return findSceneOp("assembly_actuator")->edit(g, args, answer, e); }, error))
        return emit statusMessage("Привод: " + QString::fromStdString(error));
    const QString dir = QString("[%1, %2, %3]").arg(axis == 0).arg(axis == 1).arg(axis == 2);
    codeAction("assembly_actuator", QString("ed.assembly_actuator(kind=\"%1\", a=%2, b=%3, axis=%4)").arg(rotary ? "rotary" : "linear").arg(link).arg(other).arg(dir),
               QJsonObject{{"kind", rotary ? "rotary" : "linear"}, {"a", int(link)}, {"b", int(other)}, {"axis", QJsonArray{axis == 0, axis == 1, axis == 2}}},
               false);
    const uint32_t joint = uint32_t(answer["id"].number());
    setSelected(joint);
    emit statusMessage(QString("%1 «%2»: привод держит позу; цель, скорость и предел — в карточке сочленения")
                           .arg(rotary ? "Поворотный привод" : "Линейный привод", QString::fromStdString(answer["name"].text(""))));
}
