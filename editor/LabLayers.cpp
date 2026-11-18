// The Laboratory's layers and presets (see LabLayers.h).
#include "LabLayers.h"

#include <algorithm>
#include <initializer_list>

using ke::DrawLayer;

namespace {

// A colour of the drawing (components 0..1, as ke::Probe draws with) as a QColor.
QColor drawn(float r, float g, float b) {
    auto channel = [](float v) { return int(std::min(1.0f, std::max(0.0f, v)) * 255.0f + 0.5f); };
    return QColor(channel(r), channel(g), channel(b));
}

// 1. Contacts: the frame's contact points (the view draws them as dots, ViewportLab.cpp), their normals,
// forces and depths.
void addContacts(std::vector<LabLayer>& v) {
    const QString g = "Контакты";
    v.push_back({DrawLayer::ContactPoints, g, "Точки контакта",
                 "Где тела касаются друг друга: красная точка на каждой точке контакта. "
                 "Щёлкните по точке — откроется её карточка.",
                 {drawn(1.0f, 0.3f, 0.2f)}});
    v.push_back({DrawLayer::ContactNormals, g, "Нормали",
                 "Куда контакт толкает: оранжевая стрелка 10 см от тела A к телу B.", {drawn(1.0f, 0.6f, 0.2f)}});
    v.push_back({DrawLayer::ContactImpulses, g, "Силы",
                 "Сколько контакт давит: жёлтая стрелка, 1 м стрелки = 100 Н (импульс точки / шаг).", {drawn(1.0f, 1.0f, 0.3f)}});
    v.push_back({DrawLayer::PenetrationDepth, g, "Проникновение",
                 "Насколько тела вошли друг в друга: красный отрезок длиной в глубину проникновения.", {drawn(1.0f, 0.1f, 0.1f)}});
}

// 2. Bodies and joints.
void addBodies(std::vector<LabLayer>& v) {
    const QString g = "Тела";
    v.push_back({DrawLayer::CentreOfMass, g, "Центр масс", "Белый крестик в центре масс каждого тела.", {drawn(1, 1, 1)}});
    v.push_back({DrawLayer::InertiaAxes, g, "Оси инерции",
                 "Главные оси тела: красная, зелёная, синяя. Длина — радиус инерции √(I/m): чем длиннее ось, тем "
                 "труднее раскрутить тело вокруг неё.",
                 {drawn(1, 0.2f, 0.2f), drawn(0.2f, 1, 0.2f), drawn(0.2f, 0.2f, 1)}});
    v.push_back({DrawLayer::Velocities, g, "Скорости",
                 "Голубая стрелка — скорость (куда тело сдвинется за 0,1 с), лиловая — угловая скорость (ось вращения).",
                 {drawn(0.2f, 1, 1), drawn(1, 0.3f, 1)}});
    v.push_back({DrawLayer::Sleeping, g, "Сон",
                 "Уснувшие тела (движок их не считает, пока их не толкнут): серая рамка, само тело темнее.", {drawn(0.35f, 0.35f, 0.35f)}});
    v.push_back({DrawLayer::JointFrames, g, "Суставы",
                 "Точки крепления сустава (зелёная на теле A, синяя на теле B), жёлтая связь между ними, серая ось.",
                 {drawn(0.2f, 1, 0.4f), drawn(0.2f, 0.6f, 1), drawn(1, 1, 0.3f)}});
}

// 3. The broad phase and the rest.
void addBoxesAndRest(std::vector<LabLayer>& v) {
    v.push_back({DrawLayer::BodyAabbs, "Широкая фаза", "Коробки тел (AABB)",
                 "Голубая коробка вокруг каждого не спящего тела: только тела, чьи коробки пересеклись, проверяются точно.",
                 {drawn(0.3f, 0.8f, 1)}});
    v.push_back({DrawLayer::Misc, "Прочее", "Прочие рисунки", "Всё, что нарисовано без своего слоя.", {drawn(0.8f, 0.8f, 0.8f)}});
}

uint32_t bits(std::initializer_list<DrawLayer> layers) {
    uint32_t mask = 0;
    for (DrawLayer l : layers) mask |= ke::Probe::bit(l);
    return mask;
}

} // namespace

const std::vector<LabLayer>& labLayers() {
    static const std::vector<LabLayer> all = [] {
        std::vector<LabLayer> v;
        addContacts(v);
        addBodies(v);
        addBoxesAndRest(v);
        return v;
    }();
    return all;
}

const std::vector<QString>& labGroups() {
    static const std::vector<QString> order = {"Контакты", "Тела", "Широкая фаза", "Прочее"};
    return order;
}

const std::vector<LabPreset>& labPresets() {
    static const std::vector<LabPreset> presets = {
        {"Контакты", "Точки, нормали, силы и проникновение всех контактов",
         bits({DrawLayer::ContactPoints, DrawLayer::ContactNormals, DrawLayer::ContactImpulses, DrawLayer::PenetrationDepth})},
        {"Всё о телах", "Центры масс, оси инерции, скорости, сон, суставы и коробки",
         bits({DrawLayer::CentreOfMass, DrawLayer::InertiaAxes, DrawLayer::Velocities, DrawLayer::Sleeping, DrawLayer::JointFrames,
               DrawLayer::BodyAabbs})},
        {"Ничего", "Выключить все слои: картинка снова чистая", 0u},
    };
    return presets;
}
