// The Laboratory's layers and presets (see LabLayers.h).
#include "LabLayers.h"
#include "UiLanguage.h"

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
    const QString g = uiText("Kinetica", "Контакты");
    v.push_back({DrawLayer::ContactPoints, g, uiText("Kinetica", "Точки контакта"),
                 uiText("Kinetica", "Где тела касаются друг друга: красная точка на каждой точке контакта. "
                 "Щёлкните по точке — откроется её карточка."),
                 {drawn(1.0f, 0.3f, 0.2f)}});
    v.push_back({DrawLayer::ContactNormals, g, uiText("Kinetica", "Нормали"),
                 uiText("Kinetica", "Куда контакт толкает: оранжевая стрелка 10 см от тела A к телу B."), {drawn(1.0f, 0.6f, 0.2f)}});
    v.push_back({DrawLayer::ContactImpulses, g, uiText("Kinetica", "Силы"),
                 uiText("Kinetica", "Сколько контакт давит: жёлтая стрелка, 1 м стрелки = 100 Н (импульс точки / шаг)."), {drawn(1.0f, 1.0f, 0.3f)}});
    v.push_back({DrawLayer::PenetrationDepth, g, uiText("Kinetica", "Проникновение"),
                 uiText("Kinetica", "Насколько тела вошли друг в друга: красный отрезок длиной в глубину проникновения."), {drawn(1.0f, 0.1f, 0.1f)}});
}

// 2. Bodies and joints.
void addBodies(std::vector<LabLayer>& v) {
    const QString g = uiText("Kinetica", "Тела");
    v.push_back({DrawLayer::CentreOfMass, g, uiText("Kinetica", "Центр масс"), uiText("Kinetica", "Белый крестик в центре масс каждого тела."), {drawn(1, 1, 1)}});
    v.push_back({DrawLayer::InertiaAxes, g, uiText("Kinetica", "Оси инерции"),
                 uiText("Kinetica", "Главные оси тела: красная, зелёная, синяя. Длина — радиус инерции √(I/m): чем длиннее ось, тем "
                 "труднее раскрутить тело вокруг неё."),
                 {drawn(1, 0.2f, 0.2f), drawn(0.2f, 1, 0.2f), drawn(0.2f, 0.2f, 1)}});
    v.push_back({DrawLayer::Velocities, g, uiText("Kinetica", "Скорости"),
                 uiText("Kinetica", "Голубая стрелка — скорость (куда тело сдвинется за 0,1 с), лиловая — угловая скорость (ось вращения)."),
                 {drawn(0.2f, 1, 1), drawn(1, 0.3f, 1)}});
    v.push_back({DrawLayer::Sleeping, g, uiText("Kinetica", "Сон"),
                 uiText("Kinetica", "Уснувшие тела (движок их не считает, пока их не толкнут): серая рамка, само тело темнее."), {drawn(0.35f, 0.35f, 0.35f)}});
    v.push_back({DrawLayer::JointFrames, g, uiText("Kinetica", "Суставы"),
                 uiText("Kinetica", "Точки крепления сустава (зелёная на теле A, синяя на теле B), жёлтая связь между ними, серая ось."),
                 {drawn(0.2f, 1, 0.4f), drawn(0.2f, 0.6f, 1), drawn(1, 1, 0.3f)}});
}

// 3. The broad phase and the rest.
void addBoxesAndRest(std::vector<LabLayer>& v) {
    v.push_back({DrawLayer::BodyAabbs, uiText("Kinetica", "Широкая фаза"), uiText("Kinetica", "Коробки тел (AABB)"),
                 uiText("Kinetica", "Голубая коробка вокруг каждого не спящего тела: только тела, чьи коробки пересеклись, проверяются точно."),
                 {drawn(0.3f, 0.8f, 1)}});
    v.push_back({DrawLayer::Misc, uiText("Kinetica", "Прочее"), uiText("Kinetica", "Прочие рисунки"), uiText("Kinetica", "Всё, что нарисовано без своего слоя."), {drawn(0.8f, 0.8f, 0.8f)}});
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
    static const std::vector<QString> order = {uiText("Kinetica", "Контакты"), uiText("Kinetica", "Тела"), uiText("Kinetica", "Широкая фаза"), uiText("Kinetica", "Прочее")};
    return order;
}

const std::vector<LabPreset>& labPresets() {
    static const std::vector<LabPreset> presets = {
        {uiText("Kinetica", "Контакты"), uiText("Kinetica", "Точки, нормали, силы и проникновение всех контактов"),
         bits({DrawLayer::ContactPoints, DrawLayer::ContactNormals, DrawLayer::ContactImpulses, DrawLayer::PenetrationDepth})},
        {uiText("Kinetica", "Всё о телах"), uiText("Kinetica", "Центры масс, оси инерции, скорости, сон, суставы и коробки"),
         bits({DrawLayer::CentreOfMass, DrawLayer::InertiaAxes, DrawLayer::Velocities, DrawLayer::Sleeping, DrawLayer::JointFrames,
               DrawLayer::BodyAabbs})},
        {uiText("Kinetica", "Ничего"), uiText("Kinetica", "Выключить все слои: картинка снова чистая"), 0u},
    };
    return presets;
}
