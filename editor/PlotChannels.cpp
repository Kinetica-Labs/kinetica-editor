// The plots' catalogue of channels: name cards and colours (see PlotChannels.h).
#include "PlotChannels.h"
#include "UiLanguage.h"

#include "TrackChart.h"

#include <QCoreApplication>

#include <algorithm>

namespace {

struct FixedCard {
    const char* id;
    const char* name;
    const char* unit;
};

// The scene's energy as the first chart names it: plain words for everyone. The simulation measures
// the total; its two parts the panel adds up from its channels (PlotPanel::append).
const FixedCard kEnergy[] = {
    {"scene/kinetic energy", QT_TRANSLATE_NOOP("Kinetica", "Кинетическая энергия"), QT_TRANSLATE_NOOP("Kinetica", "Дж")},
    {"scene/potential energy", QT_TRANSLATE_NOOP("Kinetica", "Потенциальная энергия"), QT_TRANSLATE_NOOP("Kinetica", "Дж")},
    {"scene/mechanical energy", QT_TRANSLATE_NOOP("Kinetica", "Полная энергия"), QT_TRANSLATE_NOOP("Kinetica", "Дж")},
};

// The engine's counters people ask for most, in words (the rest keep the engine's own words).
const FixedCard kEngine[] = {
    {"physics/step ms", QT_TRANSLATE_NOOP("Kinetica", "Кадр: шаг физики"), QT_TRANSLATE_NOOP("Kinetica", "мс")},
    {"physics/contacts", QT_TRANSLATE_NOOP("Kinetica", "Физика: точек контакта"), ""},
    {"physics/time s", QT_TRANSLATE_NOOP("Kinetica", "Время сцены"), QT_TRANSLATE_NOOP("Kinetica", "с")},
};

// The parts of the engine an internal's id starts with ("physics/drives ms" -> «Физика: drives»).
const FixedCard kParts[] = {{"rigid", QT_TRANSLATE_NOOP("Kinetica", "Твёрдые тела"), ""}, {"physics", QT_TRANSLATE_NOOP("Kinetica", "Физика"), ""}, {"scene", QT_TRANSLATE_NOOP("Kinetica", "Сцена"), ""}};

// The unit an engine id ends with ("rigid/solve ms"), and the id without it.
struct UnitSuffix {
    const char* suffix;
    const char* unit;
};
const UnitSuffix kSuffixes[] = {{" ms", QT_TRANSLATE_NOOP("Kinetica", "мс")}, {" s", QT_TRANSLATE_NOOP("Kinetica", "с")}, {" J", QT_TRANSLATE_NOOP("Kinetica", "Дж")}, {" N", QT_TRANSLATE_NOOP("Kinetica", "Н")}, {" %", "%"}, {" m", QT_TRANSLATE_NOOP("Kinetica", "м")}};

PlotGroup groupOf(ke::ChannelGroup g) {
    switch (g) {
    case ke::ChannelGroup::Scene: return PlotGroup::Scene;
    case ke::ChannelGroup::Object: return PlotGroup::Object;
    case ke::ChannelGroup::Group: return PlotGroup::Group;
    }
    return PlotGroup::Engine;
}

// The simulation's card. An object's, a group's or a joint's name says what, not whose («Масса»): its
// label - the id before the last '/' - goes in front, the name after it in small letters.
PlotChannel fromSimulation(const ke::ChannelInfo& info) {
    PlotChannel c{info.id, QCoreApplication::translate("Kinetica", info.name.c_str()),
                  QCoreApplication::translate("Kinetica", info.unit.c_str()), groupOf(info.group)};
    // The simulation sends some labels as a battery name or a joint axis followed by a label.
    // Look up the label separately so the name and the channel's stable ID stay intact.
    if (UiLanguage::active() == "en" && c.name == QString::fromStdString(info.name)) {
        const size_t colon = info.name.rfind(": ");
        if (colon != std::string::npos) {
            c.name = QString::fromStdString(info.name.substr(0, colon))
                + QCoreApplication::translate("Kinetica", info.name.c_str() + colon);
        }
    }
    const size_t slash = info.id.rfind('/');
    if (info.group != ke::ChannelGroup::Scene && slash != std::string::npos && !c.name.isEmpty()) {
        QString what = c.name;
        if (what.size() > 1 && what[1].isLower()) what[0] = what[0].toLower(); // «Масса» -> «масса», «|v|» stays
        c.name = QString::fromStdString(info.id.substr(0, slash)) + " — " + what;
    }
    return c;
}

// An engine internal: its unit read off the end of the id, its part in words, the rest in the
// engine's own words ("rigid/solve ms" -> «Твёрдые тела: solve», мс).
PlotChannel engineCard(const std::string& id) {
    for (const FixedCard& e : kEngine)
        if (id == e.id) return {id, QString::fromUtf8(uiText("Kinetica", e.name)), QString::fromUtf8(uiText("Kinetica", e.unit)), PlotGroup::Engine};
    std::string rest = id;
    QString unit;
    for (const UnitSuffix& s : kSuffixes) {
        const std::string suffix = s.suffix;
        if (rest.size() > suffix.size() && rest.compare(rest.size() - suffix.size(), suffix.size(), suffix) == 0) {
            unit = QString::fromUtf8(uiText("Kinetica", s.unit));
            rest.resize(rest.size() - suffix.size());
            break;
        }
    }
    const size_t slash = rest.find('/');
    for (const FixedCard& part : kParts)
        if (slash != std::string::npos && rest.compare(0, slash, part.id) == 0)
            return {id, QString::fromUtf8(uiText("Kinetica", part.name)) + ": " + QString::fromStdString(rest.substr(slash + 1)), unit, PlotGroup::Engine};
    return {id, QString::fromStdString(rest), unit, PlotGroup::Engine};
}

} // namespace

QString plotGroupTitle(PlotGroup group) {
    switch (group) {
    case PlotGroup::Scene: return uiText("Kinetica", "Сцена");
    case PlotGroup::Object: return uiText("Kinetica", "Объект");
    case PlotGroup::Group: return uiText("Kinetica", "Группа");
    case PlotGroup::Engine: return uiText("Kinetica", "Движок (для разработчиков)");
    }
    return QString();
}

void PlotCatalog::learn(const std::vector<ke::Measurement>& measurements) {
    for (const ke::Measurement& m : measurements)
        if (!learned_.count(m.info.id)) learned_[m.info.id] = fromSimulation(m.info);
}

// 1. the scene's energy in plain words; 2. what the simulation sent with a measurement; 3. the
// catalogue of scene channels; 4. a channel with no part (not an ASCII "part/quantity" id); 5. an
// engine internal.
PlotChannel PlotCatalog::card(const std::string& id) const {
    for (const FixedCard& e : kEnergy)
        if (id == e.id) return {id, QString::fromUtf8(uiText("Kinetica", e.name)), QString::fromUtf8(uiText("Kinetica", e.unit)), PlotGroup::Scene};
    if (auto it = learned_.find(id); it != learned_.end()) return it->second;
    if (const ke::ChannelInfo* info = ke::describeChannel(id)) return fromSimulation(*info);
    const QString text = QString::fromStdString(id);
    if (id.find('/') == std::string::npos) { // the scene's own plot: «Высота мяча, м»
        const int comma = text.lastIndexOf(", ");
        if (comma > 0) return {id, text.left(comma), text.mid(comma + 2), PlotGroup::Scene};
        return {id, text, QString(), PlotGroup::Scene};
    }
    return engineCard(id);
}

bool PlotCatalog::measuredBySimulation(const std::string& id) const { return learned_.count(id) || ke::describeChannel(id); }

// A channel on screen keeps its slot. A new one takes the slot it had last time if that one is
// free, else the first free slot.
int PlotCatalog::slotOf(const std::string& id) {
    if (auto it = slot_.find(id); it != slot_.end()) return it->second;
    auto taken = [this](int slot) {
        for (const auto& [other, s] : slot_)
            if (s == slot) return true;
        return false;
    };
    int slot = 0;
    if (auto last = lastSlot_.find(id); last != lastSlot_.end() && !taken(last->second)) slot = last->second;
    else
        while (taken(slot)) ++slot;
    slot_[id] = lastSlot_[id] = slot;
    return slot;
}

void PlotCatalog::pin(const std::string& id) {
    slotOf(id);
    pinned_.push_back(id);
}

// The channels no longer on any chart give their slots back (a pinned one keeps its own).
void PlotCatalog::releaseAllBut(const std::vector<std::string>& onScreen) {
    for (auto it = slot_.begin(); it != slot_.end();) {
        const bool keep = std::find(onScreen.begin(), onScreen.end(), it->first) != onScreen.end() ||
                          std::find(pinned_.begin(), pinned_.end(), it->first) != pinned_.end();
        it = keep ? std::next(it) : slot_.erase(it);
    }
}

QColor PlotCatalog::color(const std::string& id) { return plotSlotColor(slotOf(id)); }

Qt::PenStyle PlotCatalog::lineStyle(const std::string& id) { return plotSlotStyle(slotOf(id)); }
