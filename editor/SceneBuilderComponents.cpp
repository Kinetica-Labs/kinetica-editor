// Plugins' components in the scene builder (SceneBuilder.h; the core: SceneGraph.h PluginComponent, component_set): the
// types the running plugins declare, a card for each on the selected shape - its fields drawn from the plugin's schema,
// an edit one component_set, one undo step and one Python line -, «Плагины» in «+ Добавить компонент», and the fields that
// have a handle, an orange point on the shape: a radius, a length, a point, dragged where they are seen.
#include "SceneBuilder.h"
#include "UiLanguage.h"

#include "ApiConsole.h"
#include "EditorOps.h"
#include "InspectorWidgets.h"
#include "core/Format.h"
#include "scene/SceneOps.h"

#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

using namespace ke;

namespace {

// A value's word of the scene file as JSON: a number, a vector "x,y,z", else the word.
QJsonValue jsonOf(const std::string& word) {
    float x = 0;
    if (parseNumber(word, x)) return double(x);
    if (word.find(',') != std::string::npos) {
        QJsonArray out;
        for (const QString& part : QString::fromStdString(word).split(',')) out.append(part.toDouble());
        return out;
    }
    return QString::fromStdString(word);
}

QJsonValue valueIn(const PluginComponent& c, const QString& key, const QJsonValue& otherwise) {
    for (const auto& [k, word] : c.values)
        if (QString::fromStdString(k) == key) return jsonOf(word);
    return otherwise;
}

Vector3 vectorOf(const QJsonValue& v) {
    const QJsonArray a = v.toArray();
    return Vector3(float(a.at(0).toDouble()), float(a.at(1).toDouble()), float(a.at(2).toDouble()));
}

// A dial - a field that is an angle about one of the shape's axes ("y" if none said): the circle its point rides, a
// little outside the shape; the direction of angle 0 and of a quarter turn (zero x quarter = the axis: a positive angle
// turns by the right hand); whether the field counts degrees (its unit says so) or radians.
struct Dial {
    Vector3 axis, zero, quarter;
    float radius = 0;
    bool degrees = false;
};

Dial dialOf(const QJsonObject& field, const Quaternion& q, const Vector3& size) {
    const int k = std::clamp(int(field.value("axis").toString("y").at(0).unicode() - 'x'), 0, 2);
    Vector3 axis(0.0f), zero(0.0f), quarter(0.0f);
    axis[k] = zero[(k + 1) % 3] = quarter[(k + 2) % 3] = 1.0f;
    const QString unit = field.value("unit").toString();
    return {q.rotate(axis), q.rotate(zero), q.rotate(quarter), std::max(0.1f, 0.6f * std::max(size[(k + 1) % 3], size[(k + 2) % 3])),
            unit == "°" || unit.startsWith("град") || unit.startsWith("deg")};
}

} // namespace

void SceneBuilder::setPluginComponents(const QString& plugin, const QJsonArray& types) {
    for (auto it = componentTypes_.begin(); it != componentTypes_.end();)
        it = it->first.startsWith(plugin + ".") ? componentTypes_.erase(it) : std::next(it);
    for (const QJsonValue& t : types) componentTypes_[plugin + "." + t.toObject().value("name").toString()] = t.toObject();
    fillInspector();
    emitShapeHandles();
}

QJsonObject SceneBuilder::componentType(const std::string& type) const {
    const auto it = componentTypes_.find(QString::fromStdString(type));
    return it == componentTypes_.end() ? QJsonObject() : it->second;
}

// One component_set: an undo step, the journal's line, the console's Python - as any core edit made from the window.
void SceneBuilder::setComponent(uint32_t id, const QString& type, const QJsonObject& values) {
    QJsonObject args{{"id", int(id)}, {"type", type}};
    if (!values.isEmpty()) args.insert("values", values);
    flushCode();
    std::string error;
    Value answer = Value::map();
    if (!transact([&](SceneGraph& g, std::string& e) { return findSceneOp("component_set")->edit(g, coreValue(args), answer, e); }, error))
        return emit statusMessage(uiText("Kinetica", "Компонент: ") + QString::fromStdString(error));
    codeAction("component_set", ApiConsole::pythonCall("component_set", args), args, false);
}

void SceneBuilder::removeComponent(uint32_t id, const QString& type) {
    const QJsonObject args{{"id", int(id)}, {"type", type}};
    flushCode();
    std::string error;
    Value answer = Value::map();
    if (!transact([&](SceneGraph& g, std::string& e) { return findSceneOp("component_remove")->edit(g, coreValue(args), answer, e); }, error))
        return emit statusMessage(uiText("Kinetica", "Компонент: ") + QString::fromStdString(error));
    codeAction("component_remove", ApiConsole::pythonCall("component_remove", args), args, false);
}

// «Плагины» in «+ Добавить компонент»: each type a running plugin declares that the shape does not have yet.
void SceneBuilder::addPluginComponentsTo(QMenu* menu, const Entity& e) {
    bool section = false;
    for (const auto& [type, schema] : componentTypes_) {
        const bool has = std::any_of(e.components.begin(), e.components.end(), [&](const PluginComponent& c) { return QString::fromStdString(c.type) == type; });
        if (has) continue;
        if (!section) menu->addSection(uiText("Kinetica", "Плагины")), section = true;
        QAction* a = menu->addAction(schema.value("label").toString(type));
        a->setToolTip(uiText("Kinetica", "Компонент плагина «") + type.section('.', 0, 0) + "»: " + schema.value("help").toString());
        connect(a, &QAction::triggered, this, [this, type, schema] {
            QJsonObject defaults; // the fields' own defaults written in: the scene says what it holds
            const QJsonObject fields = schema.value("fields").toObject();
            for (auto it = fields.begin(); it != fields.end(); ++it) defaults.insert(it.key(), it.value().toObject().value("default"));
            setComponent(selectedId_, type, defaults);
        });
    }
}

// A card for each component of the shape: the plugin's fields as the inspector draws any; a component whose plugin is not
// running keeps its words and says so.
void SceneBuilder::fillPluginCards() {
    while (QLayoutItem* it = pluginCards_->takeAt(0)) {
        if (QWidget* w = it->widget()) w->hide(), w->deleteLater();
        delete it;
    }
    const Entity* e = selection_.size() == 1 ? masterById(selectedId_) : nullptr;
    if (!e || isHandle(*e)) return;
    const uint32_t id = selectedId_;
    for (const PluginComponent& c : e->components) {
        const QString type = QString::fromStdString(c.type);
        const QJsonObject schema = componentType(c.type);
        auto* card = new ComponentCard(schema.isEmpty() ? type + uiText("Kinetica", " — нет плагина") : schema.value("label").toString(type), QIcon());
        card->setObjectName("pluginComponent:" + type);
        connect(card, &ComponentCard::removeClicked, this, [this, id, type] {
            QTimer::singleShot(0, this, [this, id, type] { removeComponent(id, type); }); // after the card's own click
        });
        pluginCards_->addWidget(card);
        if (schema.isEmpty()) {
            QString words;
            for (const auto& [k, w] : c.values) words += QString::fromStdString(k + " " + w + "  ");
            auto* note = new QLabel(uiText("Kinetica", "Плагин «") + type.section('.', 0, 0) + uiText("Kinetica", "» не запущен: значения сохраняются как есть. ") + words);
            note->setWordWrap(true);
            note->setObjectName("roleRowLabel");
            card->body()->addWidget(note);
            continue;
        }
        auto pending = std::make_shared<QJsonObject>();
        auto* form = new FieldForm([this, id, type, pending](bool) { // one field changed: one component_set, after the form's own signal
            const QJsonObject given = *pending;
            *pending = QJsonObject();
            if (!given.isEmpty()) QTimer::singleShot(0, this, [this, id, type, given] { setComponent(id, type, given); });
        });
        const QJsonObject fields = schema.value("fields").toObject();
        QStringList order;
        for (const QJsonValue& n : schema.value("order").toArray()) order << n.toString();
        for (const QString& name : order) {
            const QJsonObject f = fields.value(name).toObject();
            const QString label = f.value("label").toString(name) + (f.contains("unit") ? ", " + f.value("unit").toString() : QString());
            const QJsonValue now = valueIn(c, name, f.value("default"));
            const double lo = f.value("minimum").toDouble(-1e6), hi = f.value("maximum").toDouble(1e6);
            const QString kind = f.value("type").toString();
            if (f.contains("ref")) { // an object of the scene, chosen by its name: the component keeps its id (0: none)
                Value listed = Value::map(), of = Value::map();
                std::string why;
                of.set("kind", f.value("ref").toString().toStdString());
                findSceneOp("scene_list")->query(graph_, of, listed, why);
                QStringList items{"—"};
                QJsonArray ids{0};
                for (const Value& o : listed["objects"].items())
                    items << QString::fromStdString(o["name"].text()), ids.append(int(o["id"].integer()));
                const int kept = now.toInt();
                int at = 0;
                for (int k = 1; k < ids.size(); ++k)
                    if (ids.at(k).toInt() == kept) at = k;
                if (kept && !at) items << QString(uiText("Kinetica", "нет объекта %1")).arg(kept), ids.append(kept), at = int(ids.size()) - 1; // gone from the scene: said, kept
                form->choice(label, items, [at] { return double(at); }, [pending, name, ids](double k) { pending->insert(name, ids.at(int(k))); });
            } else if (f.contains("enum")) {
                QStringList items;
                const QJsonArray choices = f.value("enum").toArray();
                for (const QJsonValue& x : choices) items << x.toVariant().toString();
                const int at = std::max(0, int(items.indexOf(now.toVariant().toString())));
                QStringList shown; // the words of the plugin's catalog, the values as they are
                for (int k = 0; k < items.size(); ++k) shown << f.value("enum_words").toArray().at(k).toString(items.at(k));
                form->choice(label, shown, [at] { return double(at); }, [pending, name, choices](double k) { pending->insert(name, choices.at(int(k))); });
            } else if (kind == "boolean") {
                form->toggle(label, [now] { return now.toBool() || now.toDouble() != 0 ? 1.0 : 0.0; }, [pending, name](double on) { pending->insert(name, on != 0); });
            } else if (kind == "vector") {
                form->vector(label, lo, hi, 0.05, 3, [now] { return vectorOf(now); },
                             [pending, name](const Vector3& v) { pending->insert(name, QJsonArray{double(v.x), double(v.y), double(v.z)}); });
            } else if (kind == "number" || kind == "integer") {
                const bool whole = kind == "integer";
                form->number(label, lo, hi, whole ? 1.0 : std::max(0.001, (hi - lo) / 100.0), whole ? 0 : 3, [now] { return now.toDouble(); },
                             [pending, name](double v) { pending->insert(name, v); });
            } else if (kind == "string") {
                form->text(label, [now] { return now.toVariant().toString(); }, [pending, name](const QString& typed) { pending->insert(name, typed); });
            } else {
                form->note([label, now] { return label + ": " + now.toVariant().toString(); });
            }
            form->name("pluginField:" + name);
        }
        form->fill();
        card->body()->addWidget(form);
    }
}

// The orange points of the selected shape's components: each field with a handle - "radius" along the shape's x,
// "length" along its axis (x, y or z; y if none said), "point" a place in its frame, "dial" an angle about its axis - a
// point on a circle around the shape -, "box" a size: a point on each of the faces +x, +y, +z, half the size out.
std::vector<ShapeHandle> SceneBuilder::componentHandles() {
    std::vector<ShapeHandle> out;
    componentHandleFields_.clear();
    const Entity* e = editing() && selection_.size() == 1 ? masterById(selectedId_) : nullptr;
    const SceneObject* o = e ? findObject(graph_, selectedId_) : nullptr;
    if (!e || !o || isHandle(*e)) return out;
    Vector3 p;
    Quaternion q;
    worldPose(graph_, *o, p, q);
    for (const PluginComponent& c : e->components) {
        const QJsonObject schema = componentType(c.type);
        const QJsonObject fields = schema.value("fields").toObject();
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            const QJsonObject f = it.value().toObject();
            const QString kind = f.value("handle").toString();
            if (kind.isEmpty()) continue;
            const QJsonValue now = valueIn(c, it.key(), f.value("default"));
            ShapeHandle h;
            h.plugin = true;
            h.label = f.value("label").toString(it.key());
            h.centre = p;
            if (kind == "point") {
                h.free = true, h.axis = Vector3(1, 0, 0);
                const Vector3 at = p + q.rotate(vectorOf(now));
                h.centre = at, h.reach = 0;
            } else if (kind == "dial") {
                const Dial d = dialOf(f, q, resolveInstance(graph_, *e).size);
                const float a = float(now.toDouble() * (d.degrees ? kPi / 180 : 1.0));
                h.free = true, h.axis = Vector3(1, 0, 0), h.reach = 0, h.normal = d.axis;
                h.centre = p + (d.zero * ke::cos(a) + d.quarter * ke::sin(a)) * d.radius;
                for (int k = 0; k <= 48; ++k) { // the circle it rides
                    const float t = float(k) * 2 * float(kPi) / 48;
                    h.guide.push_back(p + (d.zero * ke::cos(t) + d.quarter * ke::sin(t)) * d.radius);
                }
            } else if (kind == "box") {
                const Vector3 size = vectorOf(now);
                for (int k = 0; k < 3; ++k) {
                    Vector3 local(0.0f);
                    local[k] = 1.0f;
                    ShapeHandle face = h;
                    face.axis = q.rotate(local), face.reach = size[k] / 2;
                    face.label = h.label + " " + QString("xyz").at(k);
                    out.push_back(face);
                    componentHandleFields_.push_back({QString::fromStdString(c.type), it.key(), kind, k});
                }
                continue;
            } else {
                const QString axis = kind == "radius" ? "x" : f.value("axis").toString("y");
                Vector3 local(0.0f);
                local[std::clamp(int(axis.at(0).unicode() - 'x'), 0, 2)] = 1.0f;
                h.axis = q.rotate(local), h.reach = float(now.toDouble());
            }
            out.push_back(h);
            componentHandleFields_.push_back({QString::fromStdString(c.type), it.key(), kind});
        }
    }
    return out;
}

// An orange point dragged: the field's value from where the mouse takes it, live on the shape; let go, one component_set.
void SceneBuilder::onComponentHandle(int index, Vector3 point, int phase) {
    if (index < 0 || index >= int(componentHandleFields_.size())) return;
    const ComponentHandleField f = componentHandleFields_[size_t(index)];
    Entity* e = masterById(selectedId_);
    const SceneObject* o = findObject(graph_, selectedId_);
    if (!e || !o) return;
    if (phase == 0) {
        componentGraph_ = graph_;
        componentDragging_ = true;
        return;
    }
    if (!componentDragging_) return;
    if (phase == 3) { // Esc: back as it was
        graph_ = componentGraph_;
        componentDragging_ = false;
        refreshColliderGuides();
        return;
    }
    Vector3 p;
    Quaternion q;
    worldPose(componentGraph_, *findObject(componentGraph_, selectedId_), p, q);
    QJsonValue value;
    const QJsonObject schema = componentType(f.type.toStdString()).value("fields").toObject().value(f.field).toObject();
    if (f.kind == "point") {
        const Vector3 local = q.conjugate().rotate(point - p);
        value = QJsonArray{double(local.x), double(local.y), double(local.z)};
    } else if (f.kind == "dial") { // where the point is on its circle: the angle from the circle's zero, as the field counts it
        const Dial d = dialOf(schema, q, resolveInstance(componentGraph_, *masterById(selectedId_)).size);
        double a = double(ke::atan2(dot(point - p, d.quarter), dot(point - p, d.zero)));
        if (a < 0 && schema.value("minimum").toDouble(-1) >= 0) a += 2 * kPi; // a field from 0 up counts all the way round
        if (d.degrees) a *= 180 / kPi;
        value = std::clamp(a, schema.value("minimum").toDouble(-1e6), schema.value("maximum").toDouble(1e6));
    } else if (f.kind == "box") { // a face's point: that size, twice its distance from the centre; the other two as they were
        QJsonArray size;
        for (const PluginComponent& c : findEntity(componentGraph_, e->id)->components)
            if (QString::fromStdString(c.type) == f.type) size = valueIn(c, f.field, schema.value("default")).toArray();
        Vector3 local(0.0f);
        local[f.part] = 1.0f;
        const double lo = std::max(1e-3, schema.value("minimum").toDouble(1e-3));
        size[f.part] = std::clamp(2.0 * double(dot(point - p, q.rotate(local))), lo, schema.value("maximum").toDouble(1e6));
        value = size;
    } else {
        const QString axis = f.kind == "radius" ? "x" : schema.value("axis").toString("y");
        Vector3 local(0.0f);
        local[std::clamp(int(axis.at(0).unicode() - 'x'), 0, 2)] = 1.0f;
        double v = double(dot(point - p, q.rotate(local)));
        v = std::clamp(v, schema.value("minimum").toDouble(f.kind == "radius" ? 0.0 : -1e6), schema.value("maximum").toDouble(1e6));
        value = v;
    }
    if (phase == 1) { // live: the word in the scene, the points after it; the card and the journal at the end
        for (PluginComponent& c : e->components)
            if (QString::fromStdString(c.type) == f.type) {
                std::string word;
                if (value.isArray()) {
                    const QJsonArray a = value.toArray();
                    word = numberText(float(a.at(0).toDouble())) + "," + numberText(float(a.at(1).toDouble())) + "," + numberText(float(a.at(2).toDouble()));
                } else {
                    word = numberText(float(value.toDouble()));
                }
                auto at = std::find_if(c.values.begin(), c.values.end(), [&](const auto& kv) { return QString::fromStdString(kv.first) == f.field; });
                if (at == c.values.end()) c.values.push_back({f.field.toStdString(), word});
                else at->second = word;
            }
        emitShapeHandles();
        return;
    }
    componentDragging_ = false; // released: the scene back as it was, then the one edit with its undo step and line
    graph_ = componentGraph_;
    setComponent(selectedId_, f.type, QJsonObject{{f.field, value}});
}
