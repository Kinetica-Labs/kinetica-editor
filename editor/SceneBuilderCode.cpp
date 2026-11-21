// The window's actions as code (see SceneBuilder.h, setCodeReporter). An action - a button, Del, Ctrl+D,
// the role bar, a gizmo drag - says the one call of the object model (kinetica) that does it, made of
// the numbers the core then applies (scene.add("box"), scene.objects["Куб 1"].move((0.25, 0, 0))); a
// click's selection says editor.select(...). Any other edit by hand - a field typed, a slider moved - is
// said a moment after it (0.4 s) as the core edits it was (ke::diffEdits: scene_set, scene_put,
// scene_remove, world_set), a property of the object model by its line. Replayed on the scene before,
// the lines make the same scene to the bit.
#include "SceneBuilder.h"

#include "core/Format.h"
#include "scene/Assembly.h"
#include "scene/SceneActions.h"
#include "scene/SceneEdits.h"

#include <QJsonArray>
#include <QStringList>
#include <QTimer>

#include <cmath>

using namespace ke;

namespace {

QString quoted(const std::string& text) {
    QString t = QString::fromStdString(text);
    return "\"" + t.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
}

std::string nameOf(const SceneGraph& g, uint32_t id) {
    if (const SceneObject* o = findObject(g, id)) return o->name;
    for (const JointObject& j : g.joints) if (j.id == id) return j.name;
    for (const BatteryObject& b : g.batteries) if (b.id == id) return b.name;
    for (const DriverObject& d : g.drivers) if (d.id == id) return d.name;
    return std::to_string(id);
}

QString ref(const SceneGraph& g, uint32_t id) { return "scene.objects[" + quoted(nameOf(g, id)) + "]"; }

template <class T>
bool has(const std::vector<T>& v, uint32_t id) {
    for (const T& o : v) if (o.id == id) return true;
    return false;
}

// A Set that is a property of the object model, as its line (empty: none is).
QString propertyLine(const SceneGraph& g, const GraphEdit& e) {
    const QString line = QString::fromStdString(e.line), key = QString::fromStdString(e.key);
    const QString v = e.values.empty() ? QString() : QString::fromStdString(e.values[0]);
    const QString r = ref(g, e.id);
    const SceneObject* o = findObject(g, e.id);
    if (has(g.entities, e.id) && line == "shape" && (key == "position" || key == "rotation" || key == "size")) {
        // the edit says the numbers up to the last that changed; the property takes all three
        const Vector3 x = key == "position" ? o->position : key == "rotation" ? o->rotationDeg : static_cast<const Entity*>(o)->size;
        return r + "." + key + " = (" + QString::fromStdString(numberText(x.x) + ", " + numberText(x.y) + ", " + numberText(x.z)) + ")";
    }
    if (e.values.size() != 1) return {};
    if (line == "object" && (key == "visible" || key == "locked") && o) return r + "." + key + " = " + (v == "1" ? "True" : "False");
    if (has(g.entities, e.id) && line == "rigid" && (key == "fixed" || key == "kinematic")) return r + "." + key + " = " + (v == "1" ? "True" : "False");
    if (has(g.entities, e.id) && line == "rigid" && key == "mass") return r + ".mass = " + v;
    if (has(g.drivers, e.id) && line == "bridge" && key == "duty") return r + ".duty = " + v;
    if (has(g.drivers, e.id) && line == "bridge" && key == "stop") return r + ".brake = " + (v == "brake" ? "True" : "False");
    if (has(g.sensors, e.id) && line == "encoder" && key == "lines") return r + ".lines = " + v;
    if (has(g.sensors, e.id) && (line == "imu" || line == "sonar") && key == "noise") return r + ".noise = " + (v == "1" ? "True" : "False");
    if (has(g.batteries, e.id) && line == "cells" && key == "series") return r + ".series = " + v;
    return {};
}

// The world's change as the object model's line when it is gravity alone (empty: else).
QString worldLine(const SceneGraph& before, const SceneGraph& after) {
    WorldSettings w = after.world;
    w.gravity = before.world.gravity;
    if (worldText(w) != worldText(before.world)) return {};
    const Vector3 g = after.world.gravity;
    return "scene.world.gravity = (" + QString::fromStdString(numberText(g.x) + ", " + numberText(g.y) + ", " + numberText(g.z)) + ")";
}

// One edit as the operation that makes it, its arguments, and the line to show (`_py`, when there is one).
QJsonObject callOf(const GraphEdit& e, const SceneGraph& before, const SceneGraph& after, QString& op) {
    QJsonObject args;
    QString py;
    switch (e.kind) {
    case GraphEdit::Kind::World: op = "world_set", args["text"] = QString::fromStdString(e.text), py = worldLine(before, after); break;
    case GraphEdit::Kind::Remove: op = "scene_remove", args["id"] = int(e.id), py = ref(before, e.id) + ".delete(keep_children=True)"; break;
    case GraphEdit::Kind::Put: op = "scene_put", args["block"] = QString::fromStdString(e.text); break;
    case GraphEdit::Kind::Set: {
        op = "scene_set", args["id"] = int(e.id), args["line"] = QString::fromStdString(e.line), args["key"] = QString::fromStdString(e.key);
        QJsonArray values;
        for (const std::string& v : e.values) values.append(QString::fromStdString(v)); // the file's own words: exact
        args["values"] = values;
        py = propertyLine(after, e);
        break;
    }
    }
    if (!py.isEmpty()) args["_py"] = py;
    return args;
}

const char* kShapeWords[] = {"box", "sphere", "cylinder", "cone", "plane", "mesh"};
const char* kLightWords[] = {"sun", "point", "spot"};

} // namespace

const char* shapeWord(ShapeKind k) { return kShapeWords[int(k)]; }
const char* lightWord(LightKind k) { return kLightWords[int(k)]; }
const char* roleWord(RoleIcon r) { return ke::roleWord(Role(int(r))); } // RoleIcon is in the order of ke::Role

QString tuple(const Vector3& v) { return QString::fromStdString("(" + numberText(v.x) + ", " + numberText(v.y) + ", " + numberText(v.z) + ")"); }
QJsonArray json(const Vector3& v) { return QJsonArray{double(v.x), double(v.y), double(v.z)}; }

QJsonArray jsonIds(const std::vector<uint32_t>& ids) {
    QJsonArray out;
    for (uint32_t id : ids) out.append(int(id));
    return out;
}

void SceneBuilder::apiCallStarted() {
    flushCode(); // what the hand did before the call is said before it
    ++apiDepth_;
}

void SceneBuilder::apiCallFinished() {
    apiDepth_ = std::max(0, apiDepth_ - 1);
    if (apiDepth_ == 0) codeSynced(), codedSelection_ = orderedSelection(); // the call's edits are its own line already
}

std::vector<uint32_t> SceneBuilder::orderedSelection() const {
    std::vector<uint32_t> ids;
    for (uint32_t id : selection_) if (id != selectedId_) ids.push_back(id);
    if (selectedId_) ids.push_back(selectedId_); // editor.select(a, b): the last one is the active one
    return ids;
}

// The drag just done as the core's operation on what it moved (the active object last): a step in the
// world, a turn about an axis (a world axis by its name) by degrees, factors along the objects' own axes.
QString SceneBuilder::gizmoLine(QString& op, QJsonObject& args) const {
    const std::vector<uint32_t> ids = gizmoIds();
    const GizmoStep& s = gizmoStep_;
    if (ids.empty() || s.mode == GizmoMode::Select) return {};
    args["ids"] = jsonIds(ids);
    const bool one = ids.size() == 1;
    const QString head = one ? ref(graph_, ids[0]) + "." : "scene.";
    const QString objects = one ? QString() : codeRefs(ids) + ", ";
    if (s.mode == GizmoMode::Translate) {
        op = "scene_move", args["by"] = json(s.by);
        return head + "move(" + objects + (one ? "" : "by=") + tuple(s.by) + ")";
    }
    if (s.mode == GizmoMode::Scale) {
        op = "scene_scale", args["by"] = json(s.scale);
        return head + "scale(" + objects + (one ? "" : "by=") + tuple(s.scale) + ")";
    }
    int named = -1;
    for (int k = 0; k < 3; ++k) named = s.axis[k] == 1.0f && s.axis[(k + 1) % 3] == 0.0f && s.axis[(k + 2) % 3] == 0.0f ? k : named;
    const QString axis = named >= 0 ? QString("\"%1\"").arg(QChar('x' + named)) : tuple(s.axis);
    op = "scene_turn", args["degrees"] = double(s.degrees);
    args["axis"] = named >= 0 ? QJsonValue(QString(QChar('x' + named))) : QJsonValue(json(s.axis));
    QString pivot;
    if (!gizmoStarts_.count(gizmoEntity_)) // the turn's centre is not the last object's own: say it
        args["pivot"] = json(gizmoFrom_.position), pivot = ", pivot=" + tuple(gizmoFrom_.position);
    const QString degrees = QString::fromStdString(numberText(s.degrees));
    return head + "turn(" + objects + (one ? axis + ", " + degrees : "axis=" + axis + ", degrees=" + degrees) + pivot + ")";
}

// A drag or a typed number is many small edits: their line comes once they rest (0.4 s), as one change.
void SceneBuilder::scheduleCode() {
    if (!reportCode_) return;
    codePending_ = true;
    if (!codeTimer_) {
        codeTimer_ = new QTimer(this);
        codeTimer_->setSingleShot(true);
        codeTimer_->setInterval(400);
        connect(codeTimer_, &QTimer::timeout, this, &SceneBuilder::flushCode);
    }
    codeTimer_->start();
}

void SceneBuilder::flushCode() {
    if (codeTimer_) codeTimer_->stop();
    if (gizmoEntity_ || goalDragging_) return; // a drag in progress is said whole when it ends, not in pieces
    const std::vector<uint32_t> selected = orderedSelection();
    if (reportCode_ && apiDepth_ == 0 && selected != codedSelection_)
        reportCode_("selection_set", {{"ids", jsonIds(selected)}, {"_py", "editor.select(" + codeRefs(selected) + ")"}});
    codedSelection_ = selected;
    // Compared only after a hand's edit: every API call starts here, and writing both scenes out as text to find
    // nothing took a quarter of its time (16 of 70 samples of scene_move).
    if (reportCode_ && apiDepth_ == 0 && codePending_)
        for (const GraphEdit& e : diffEdits(coded_, graph_)) {
            QString op;
            const QJsonObject args = callOf(e, coded_, graph_, op);
            reportCode_(op, args);
        }
    codeSynced();
}

QString SceneBuilder::codeRefs(const std::vector<uint32_t>& ids) const {
    QStringList refs;
    for (uint32_t id : ids) refs << ref(graph_, id);
    return refs.join(", ");
}

QString SceneBuilder::codeCall(const QString& method, const std::vector<uint32_t>& ids) const {
    return ids.size() == 1 ? ref(graph_, ids[0]) + "." + method + "()" : "scene." + method + "(" + codeRefs(ids) + ")";
}

void SceneBuilder::codeAction(const QString& op, const QString& py, QJsonObject args, bool flushFirst) {
    if (flushFirst) flushCode();
    if (reportCode_ && apiDepth_ == 0) {
        args["_py"] = py;
        reportCode_(op, args);
    }
    codeSynced();
    codedSelection_ = orderedSelection(); // what an action selects is part of what it does
}
