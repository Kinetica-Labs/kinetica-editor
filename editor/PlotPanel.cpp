// The plots at the bottom of the window (see PlotPanel.h): the recording, the charts that come by
// themselves, what the reader may change on them, the hover, the cursor and the CSV.
#include "PlotPanel.h"
#include "UiLanguage.h"

#include "core/Format.h"

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

constexpr double kLiveSeconds = 60.0; // a longer run shows its last minute, sliding

// THE one place where the first chart is chosen: the scene's energy, its three lines in this order -
// of motion, of height, and their sum (the mechanical energy the simulation measures while the plots
// or the Laboratory are open).
const std::vector<std::string>& defaultChannels() {
    static const std::vector<std::string> channels = {"scene/kinetic energy", "scene/potential energy", "scene/mechanical energy"};
    return channels;
}

std::vector<PlotPanel::Chart> defaultCharts() {
    return {{uiText("Kinetica", "Энергия сцены — движения, высоты, полная (Дж)"), defaultChannels(), {}, false}};
}

// The selected object's two charts: how high it is and how fast it moves.
std::vector<PlotPanel::Chart> objectCharts(const QString& label) {
    const std::string l = label.toStdString();
    return {{uiText("Kinetica", "Высота, м"), {l + "/height"}, {}, true}, {uiText("Kinetica", "Скорость, м/с"), {l + "/speed"}, {}, true}};
}

QString formatValue(double v) { return numberText(v, 4); }

void eraseId(std::vector<std::string>& ids, const std::string& id) { ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end()); }

bool contains(const std::vector<std::string>& ids, const std::string& id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

} // namespace

PlotPanel::PlotPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("plotPanel");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    // Before the first frame: one quiet line, nothing to press.
    placeholder_ = new QLabel(uiText("Kinetica", "Графики появятся после ▶"));
    placeholder_->setObjectName("plotPlaceholder");
    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setStyleSheet("color: #8a93a3;");
    col->addWidget(placeholder_, 1);

    // The charts scroll: many of them never squeeze below a readable size.
    scroll_ = new QScrollArea;
    scroll_->setObjectName("plotScroll");
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    col->addWidget(scroll_, 1);

    checklist_ = new ChannelList(ChannelList::Mode::Checklist, this);
    connect(checklist_, &ChannelList::toggled, this, [this](std::string id, bool on) { setChannelShown(id, on); });
    connect(checklist_, &ChannelList::presetChosen, this, &PlotPanel::applyPreset);
    chartList_ = new ChannelList(ChannelList::Mode::Chart, this);
    connect(chartList_, &ChannelList::chosen, this, [this](std::string id) { switchChart(listChart_, id); });
    connect(chartList_, &ChannelList::toggled, this, [this](std::string id, bool on) { setOnChart(listChart_, id, on); });
    buildBar();

    // The frames come 60 times a second; the charts repaint at most 25 times a second.
    repaint_ = new QTimer(this);
    repaint_->setSingleShot(true);
    repaint_->setInterval(40);
    connect(repaint_, &QTimer::timeout, this, &PlotPanel::refreshCharts);
    pluginTick_ = new QTimer(this);
    pluginTick_->setInterval(40);
    connect(pluginTick_, &QTimer::timeout, this, &PlotPanel::refreshPluginCharts);
    for (const std::string& id : defaultChannels()) catalog_.pin(id); // blue, orange, green: always the same
    charts_ = defaultCharts();
    rebuildCharts();
}

// Under the charts, quiet: «Ещё величины…» (every quantity, with checkboxes) and the layout switch -
// «Наложить | Дорожками | Нормировать», a segmented control, always one pressed.
void PlotPanel::buildBar() {
    more_ = new QToolButton;
    more_->setObjectName("plotMore");
    more_->setText(uiText("Kinetica", "Ещё величины…"));
    more_->setToolTip(uiText("Kinetica", "Все величины, которые считает движок, с галочками — для тех, кому нужно всё"));
    connect(more_, &QToolButton::clicked, this, [this] {
        checklist_->setRows(rowsFor(-1));
        checklist_->popupAt(more_, more_->rect());
    });
    switch_ = new QWidget;
    switch_->setObjectName("plotLayoutSwitch");
    auto* row = new QHBoxLayout(switch_);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    switchGroup_ = new QButtonGroup(this);
    const std::pair<const char*, const char*> choices[] = {{"overlay", uiText("Kinetica", "Наложить")}, {"lanes", uiText("Kinetica", "Дорожками")}, {"normalized", uiText("Kinetica", "Нормировать")}};
    for (int i = 0; i < 3; ++i) {
        auto* b = new QToolButton;
        b->setObjectName(QString("plotLayout_") + choices[i].first);
        b->setText(choices[i].second);
        b->setCheckable(true);
        b->setChecked(i == int(arrangement_));
        switchGroup_->addButton(b, i);
        row->addWidget(b);
    }
    switchGroup_->button(0)->setToolTip(uiText("Kinetica", "Графики рядом; на каждом — линии одной единицы"));
    switchGroup_->button(1)->setToolTip(uiText("Kinetica", "Графики друг под другом, с общей осью времени"));
    switchGroup_->button(2)->setToolTip(uiText("Kinetica", "Все линии на одном графике, каждая от 0 до 1: форма, не величина"));
    const QString quiet = "QToolButton { color: #aab1bd; border: 1px solid #353a43; padding: 2px 8px; background: transparent; }"
                          "QToolButton:hover { background: #2f3540; }"
                          "QToolButton:checked { color: white; background: #2a3a52; border-color: #3d5a80; }";
    switch_->setStyleSheet(quiet + "QToolButton#plotLayout_overlay { border-top-left-radius: 5px; border-bottom-left-radius: 5px; }"
                                   "QToolButton#plotLayout_normalized { border-top-right-radius: 5px; border-bottom-right-radius: 5px; }");
    more_->setStyleSheet("QToolButton { color: #aab1bd; border: none; padding: 2px 6px; } QToolButton:hover { color: white; }");
    connect(switchGroup_, &QButtonGroup::idClicked, this, [this](int id) { setArrangement(Arrangement(id)); });
}

QWidget* PlotPanel::moreButton() const { return more_; }

void PlotPanel::setArrangement(Arrangement a) {
    arrangement_ = a;
    if (QAbstractButton* b = switchGroup_->button(int(a))) b->setChecked(true);
    rebuildCharts();
}

void PlotPanel::setWholeRun(bool on) {
    wholeRun_ = on;
    refreshCharts();
}

// --- The recording --------------------------------------------------------------------------------

void PlotPanel::setScene(const std::string& name) {
    std::vector<Chart> own;
    for (const Chart& c : charts_)
        if (!c.object) own.push_back(c);
    if (!scene_.empty() && touched_) chartsByScene_[scene_] = own;
    scene_ = name;
    const auto it = chartsByScene_.find(name);
    touched_ = it != chartsByScene_.end();
    charts_ = touched_ ? it->second : defaultCharts();
    object_.clear(); // the object's section goes with the old scene
    clear();
}

// One frame:
//   1. the name cards of this frame's measurements are learnt (an object's channels come so);
//   2. every value is recorded - the Probe's channels, the measurements - and the scene's energy in
//      its two parts (addEnergyParts); a channel the simulation measures is taken from the
//      measurements only (the Probe may still hold its value from an earlier scene);
//   3. a channel that was not in this frame gets a gap, so its line breaks instead of lying;
//   4. a new channel: the history's bounds, the charts waiting for it, the lists follow;
//   5. the charts repaint soon (at most 25 times a second).
void PlotPanel::append(double t, const std::vector<ke::Probe::Channel>& probe, const std::vector<ke::Measurement>& measurements) {
    catalog_.learn(measurements);
    if (frames_++ == 0) start_ = t, showHint();
    now_ = t;
    reported_.clear();
    for (const ke::Probe::Channel& c : probe)
        if (!catalog_.measuredBySimulation(c.name)) record(t, c.name, c.value);
    for (const ke::Measurement& m : measurements) record(t, m.info.id, m.value);
    addEnergyParts(t, measurements);
    for (auto& [id, s] : series_)
        if (!reported_.count(id)) s.add(t, std::nan(""));
    if (fresh_) {
        fresh_ = false;
        updateKeep();
        rebuildCharts(); // a chart waiting for this channel gets its line
        refreshLists();
    }
    if (!repaint_->isActive()) repaint_->start();
}

void PlotPanel::record(double t, const std::string& id, double v) {
    auto [it, added] = series_.try_emplace(id);
    if (added) fresh_ = true, it->second.setKeep(kept(id));
    it->second.add(t, v);
    reported_.insert(id);
}

// The scene's energy in the two parts the first chart shows (the simulation measures their sum): of
// motion - the bodies' translation and rotation - and of height. Added exactly as the simulation adds
// the total (sim/Channels.cpp), so the three agree.
void PlotPanel::addEnergyParts(double t, const std::vector<ke::Measurement>& measurements) {
    static const std::set<std::string> motion = {"rigid/kinetic energy", "rigid/rotational energy"};
    static const std::set<std::string> height = {"rigid/potential energy"};
    double kinetic = 0, potential = 0;
    bool total = false;
    for (const ke::Measurement& m : measurements) {
        if (motion.count(m.info.id)) kinetic += m.value;
        if (height.count(m.info.id)) potential += m.value;
        total = total || m.info.id == "scene/mechanical energy";
    }
    if (!total) return;
    record(t, "scene/kinetic energy", kinetic);
    record(t, "scene/potential energy", potential);
}

// Which channels keep the whole run: the lines on the charts (the selected object's among them), the
// scene's energy.
bool PlotPanel::kept(const std::string& id) const {
    if (contains(defaultChannels(), id)) return true;
    return std::any_of(charts_.begin(), charts_.end(), [&](const Chart& c) { return contains(c.ids, id); });
}

void PlotPanel::updateKeep() {
    for (auto& [id, s] : series_) s.setKeep(kept(id));
}

size_t PlotPanel::bytes() const {
    size_t sum = 0;
    for (const auto& [id, s] : series_) sum += s.bytes() + id.capacity();
    return sum;
}

void PlotPanel::clear() {
    series_.clear();
    frames_ = 0;
    rebuildCharts(); // the views pointed into the history that is gone
    refreshLists();
}

const PlotSeries* PlotPanel::series(const std::string& id) const {
    const auto it = series_.find(id);
    return it != series_.end() ? &it->second : nullptr;
}

// --- The selected object --------------------------------------------------------------------------

void PlotPanel::showObject(const QString& label) {
    showSection(label, label.isEmpty() ? QString() : uiText("Kinetica", "Объект: ") + label, label.isEmpty() ? std::vector<Chart>() : objectCharts(label),
                {label.toStdString() + "/"});
}

void PlotPanel::showSection(const QString& key, const QString& title, const std::vector<Chart>& charts, const std::vector<std::string>& prefixes) {
    if (key == object_) return;
    object_ = key, sectionTitle_ = title, sectionCharts_ = charts, sectionPrefixes_ = prefixes;
    for (Chart& c : sectionCharts_) c.object = true;
    charts_.erase(std::remove_if(charts_.begin(), charts_.end(), [](const Chart& c) { return c.object; }), charts_.end());
    if (!key.isEmpty()) charts_.insert(charts_.end(), sectionCharts_.begin(), sectionCharts_.end());
    updateKeep();
    forgetColours();
    rebuildCharts();
    QTimer::singleShot(0, this, &PlotPanel::refreshLists);
}

bool PlotPanel::ownChannel(const std::string& id) const {
    return !object_.isEmpty() && std::any_of(sectionPrefixes_.begin(), sectionPrefixes_.end(), [&](const std::string& p) { return id.rfind(p, 0) == 0; });
}

QString PlotPanel::objectTitle() const { return object_.isEmpty() ? QString() : sectionTitle_; }

// --- What the reader may change -------------------------------------------------------------------

// After any change the reader made: the history's bounds, the views, and the lists a moment later
// (the click may have come from one of them).
void PlotPanel::chartsChanged() {
    touched_ = true;
    updateKeep();
    forgetColours();
    rebuildCharts();
    QTimer::singleShot(0, this, &PlotPanel::refreshLists);
}

// The lines no longer on any chart give their colours back, so the lines on screen keep the first,
// best-told-apart colours of the palette (the scene's energy keeps its three for good).
void PlotPanel::forgetColours() {
    std::vector<std::string> onScreen;
    for (const Chart& c : charts_) onScreen.insert(onScreen.end(), c.ids.begin(), c.ids.end());
    catalog_.releaseAllBut(onScreen);
}

bool PlotPanel::showsChannel(const std::string& id) const {
    return std::any_of(charts_.begin(), charts_.end(), [&](const Chart& c) { return contains(c.ids, id); });
}

// A line on the first chart of its unit (never on the object's), else on a chart of its own.
void PlotPanel::showChannel(const std::string& id) {
    if (showsChannel(id)) return;
    const QString unit = catalog_.card(id).unit;
    const bool own = ownChannel(id);
    for (Chart& c : charts_)
        if (c.object == own && !c.ids.empty() && unitOf(c) == unit) {
            c.ids.push_back(id);
            c.title.clear();
            return chartsChanged();
        }
    charts_.insert(std::find_if(charts_.begin(), charts_.end(), [](const Chart& c) { return c.object; }), Chart{{}, {id}, {}, own});
    chartsChanged();
}

// The checklist: on - onto a chart (showChannel); off - off every chart, an emptied chart goes too.
void PlotPanel::setChannelShown(const std::string& id, bool on) {
    if (on) return showChannel(id);
    for (Chart& c : charts_) {
        if (contains(c.ids, id)) c.title.clear();
        eraseId(c.ids, id);
        c.hidden.erase(id);
    }
    charts_.erase(std::remove_if(charts_.begin(), charts_.end(), [](const Chart& c) { return c.ids.empty(); }), charts_.end());
    chartsChanged();
}

// The checklist's presets:
//   «Энергия»  - the scene's energy chart, back in front if it was taken away;
//   «Движок»   - a chart of the step's time and its five longest stages right now;
//   «Сбросить» - the charts as they come by themselves: the energy, and the object's if one is selected.
void PlotPanel::applyPreset(ChannelList::Preset preset) {
    if (preset == ChannelList::Preset::Energy) {
        if (!std::any_of(charts_.begin(), charts_.end(), [](const Chart& c) { return c.ids == defaultChannels(); }))
            charts_.insert(charts_.begin(), defaultCharts().front());
    } else if (preset == ChannelList::Preset::Engine) {
        std::vector<std::pair<double, std::string>> stages;
        for (const auto& [id, s] : series_) {
            const PlotChannel card = catalog_.card(id);
            if (id != "physics/step ms" && card.group == PlotGroup::Engine && card.unit == uiText("Kinetica", "мс") && std::isfinite(s.latest()))
                stages.push_back({s.latest(), id});
        }
        std::sort(stages.rbegin(), stages.rend()); // the longest first
        Chart engine{uiText("Kinetica", "Время шага физики по стадиям, мс"), {"physics/step ms"}, {}, false};
        for (size_t i = 0; i < stages.size() && i < 5; ++i) engine.ids.push_back(stages[i].second);
        charts_.insert(std::find_if(charts_.begin(), charts_.end(), [](const Chart& c) { return c.object; }), engine);
    } else {
        charts_ = defaultCharts();
        if (!object_.isEmpty()) charts_.insert(charts_.end(), sectionCharts_.begin(), sectionCharts_.end());
    }
    chartsChanged();
    touched_ = preset != ChannelList::Preset::Reset;
}

// The «+»: an empty chart after the scene's charts, its list open at once.
void PlotPanel::addChart() {
    const auto at = std::find_if(charts_.begin(), charts_.end(), [](const Chart& c) { return c.object; });
    const int chart = int(at - charts_.begin());
    charts_.insert(at, Chart{});
    chartsChanged();
    openChartList(chart);
}

void PlotPanel::openChartList(int chart) {
    if (chart < 0 || chart >= int(charts_.size())) return;
    listChart_ = chart;
    chartList_->setRows(rowsFor(chart));
    TrackChart* view = viewOf(chart);
    if (view) chartList_->popupAt(view, view->titleRect().toRect());
    else chartList_->popupAt(this, QRect(0, 0, width(), 0));
}

// The list's click on a row: this quantity alone on the chart.
void PlotPanel::switchChart(int chart, const std::string& id) {
    if (chart < 0 || chart >= int(charts_.size())) return;
    charts_[size_t(chart)].ids = {id};
    charts_[size_t(chart)].hidden.clear();
    charts_[size_t(chart)].title.clear();
    chartsChanged();
}

// The list's ＋ / ✓: one more line on this chart - or, of another unit, on a new chart right after it
// (never two y-axes) - or one line fewer.
void PlotPanel::setOnChart(int chart, const std::string& id, bool on) {
    if (chart < 0 || chart >= int(charts_.size())) return;
    Chart& c = charts_[size_t(chart)];
    if (!on) {
        eraseId(c.ids, id);
        c.hidden.erase(id);
        c.title.clear();
    } else if (!c.ids.empty() && unitOf(c) != catalog_.card(id).unit) {
        charts_.insert(charts_.begin() + chart + 1, Chart{{}, {id}, {}, c.object});
    } else if (!contains(c.ids, id)) {
        c.ids.push_back(id);
        c.title.clear();
    }
    chartsChanged();
    QTimer::singleShot(0, this, [this] {
        if (chartList_->isVisible()) chartList_->setRows(rowsFor(listChart_)); // the ＋ becomes ✓
    });
}

// A click on a legend entry: that line hidden or shown again (-1: on every chart that has it).
void PlotPanel::toggleLine(int chart, const std::string& id) {
    for (int i = 0; i < int(charts_.size()); ++i) {
        if ((chart >= 0 && i != chart) || !contains(charts_[size_t(i)].ids, id)) continue;
        std::set<std::string>& hidden = charts_[size_t(i)].hidden;
        if (!hidden.erase(id)) hidden.insert(id);
    }
    chartsChanged();
}

// A double click: that line alone; when it is alone already, every line again.
void PlotPanel::isolateLine(int chart, const std::string& id) {
    for (int i = 0; i < int(charts_.size()); ++i) {
        Chart& c = charts_[size_t(i)];
        if ((chart >= 0 && i != chart) || !contains(c.ids, id)) continue;
        const bool alone = std::all_of(c.ids.begin(), c.ids.end(), [&](const std::string& o) { return o == id || c.hidden.count(o); });
        c.hidden.clear();
        if (!alone)
            for (const std::string& o : c.ids)
                if (o != id) c.hidden.insert(o);
    }
    chartsChanged();
}

void PlotPanel::removeChart(int chart) {
    if (chart < 0 || chart >= int(charts_.size())) return;
    charts_.erase(charts_.begin() + chart);
    focus_.clear(); // the charts after it move up: «scene:3» would be another one
    chartsChanged();
}

// The right click on a chart: what may be done to it, to its lines, to the layout, to the picture.
QMenu* PlotPanel::contextMenu(TrackChart* view, const QPoint& at) {
    const int chart = chartOf(view);
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto item = [&](const char* name, const QString& text, std::function<void()> act) {
        QAction* a = menu->addAction(text);
        a->setObjectName(name);
        connect(a, &QAction::triggered, this, act);
        return a;
    };
    item("plotMenuAdd", uiText("Kinetica", "Добавить величину…"), [this, chart] {
        if (chart >= 0) return openChartList(chart);
        checklist_->setRows(rowsFor(-1));
        checklist_->popupAt(more_, more_->rect());
    });
    const int k = view->nearestTrack(at);
    const std::string line = k >= 0 ? view->tracks()[size_t(k)].id : std::string();
    item("plotMenuRemoveLine", k >= 0 ? uiText("Kinetica", "Убрать линию «") + view->tracks()[size_t(k)].name + "»" : QString(uiText("Kinetica", "Убрать линию")),
         [this, chart, line] { chart >= 0 ? setOnChart(chart, line, false) : setChannelShown(line, false); })
        ->setEnabled(k >= 0);
    item("plotMenuRemoveChart", uiText("Kinetica", "Убрать этот график"), [this, chart] { removeChart(chart); })->setEnabled(chart >= 0);
    if (chart >= 0) focusItem(menu, "scene:" + QString::number(chart));
    menu->addSeparator();
    auto* layouts = new QActionGroup(menu);
    const std::pair<const char*, const char*> choices[] = {{"plotMenuOverlay", uiText("Kinetica", "Наложить")}, {"plotMenuLanes", uiText("Kinetica", "Дорожками")},
                                                           {"plotMenuNormalized", uiText("Kinetica", "Нормировать")}};
    for (int i = 0; i < 3; ++i) {
        QAction* a = item(choices[i].first, choices[i].second, [this, i] { setArrangement(Arrangement(i)); });
        a->setCheckable(true);
        a->setChecked(int(arrangement_) == i);
        layouts->addAction(a);
    }
    QAction* whole = item("plotMenuWhole", uiText("Kinetica", "Весь прогон, а не последняя минута"), [this] { setWholeRun(!wholeRun_); });
    whole->setCheckable(true);
    whole->setChecked(wholeRun_);
    menu->addSeparator();
    item("plotMenuCsv", uiText("Kinetica", "Сохранить CSV…"), [this] { emit saveCsvRequested(); });
    item("plotMenuCopy", uiText("Kinetica", "Скопировать картинку"), [view] { QApplication::clipboard()->setImage(view->grab().toImage()); });
    return menu;
}

// --- The lists ------------------------------------------------------------------------------------

// The rows of a list: every recorded quantity and every one on a chart (an object's may not be
// measured yet), each with its card, whether it is on this chart (chart ≥ 0) or anywhere (-1), its
// colour when drawn and its latest value.
std::vector<ChannelList::Row> PlotPanel::rowsFor(int chart) {
    std::set<std::string> ids;
    for (const auto& [id, s] : series_) ids.insert(id);
    for (const Chart& c : charts_) ids.insert(c.ids.begin(), c.ids.end());
    std::vector<ChannelList::Row> rows;
    for (const std::string& id : ids) {
        const bool own = chart >= 0 && chart < int(charts_.size()) && charts_[size_t(chart)].object;
        if (own && !ownChannel(id)) continue;
        ChannelList::Row r;
        r.channel = catalog_.card(id);
        r.on = chart >= 0 && chart < int(charts_.size()) ? contains(charts_[size_t(chart)].ids, id) : showsChannel(id);
        if (showsChannel(id)) r.color = catalog_.color(id), r.style = catalog_.lineStyle(id);
        const PlotSeries* s = series(id);
        r.value = s ? formatValue(s->latest()) : "—";
        rows.push_back(r);
    }
    return rows;
}

void PlotPanel::refreshLists() {
    if (checklist_->isVisible()) checklist_->setRows(rowsFor(-1));
    if (chartList_->isVisible()) chartList_->setRows(rowsFor(listChart_));
}

// --- The charts on screen -------------------------------------------------------------------------

QString PlotPanel::unitOf(const Chart& c) { return c.ids.empty() ? QString() : catalog_.card(c.ids.front()).unit; }

// A chart's title in words: its fixed words, else its line's name and unit, else the first line's
// name, how many more, and their unit.
QString PlotPanel::titleOf(const Chart& c) {
    if (!c.title.isEmpty()) return c.title;
    if (c.ids.empty()) return uiText("Kinetica", "Выберите величину");
    const PlotChannel first = catalog_.card(c.ids.front());
    const QString unit = first.unit.isEmpty() ? QString() : ", " + first.unit;
    if (c.ids.size() == 1) return first.name + unit;
    return QString(uiText("Kinetica", "%1 и ещё %2%3")).arg(first.name).arg(c.ids.size() - 1).arg(unit);
}

PlotTrack PlotPanel::trackOf(const std::string& id, bool hidden) {
    const PlotChannel card = catalog_.card(id);
    return {id, card.name, card.unit, catalog_.color(id), catalog_.lineStyle(id), series(id), hidden};
}

int PlotPanel::chartOf(const TrackChart* view) const {
    for (size_t i = 0; i < views_.size(); ++i)
        if (views_[i] == view) return viewChart_[i];
    return -1;
}

TrackChart* PlotPanel::viewOf(int chart) const {
    for (size_t i = 0; i < views_.size(); ++i)
        if (viewChart_[i] == chart) return views_[i];
    return nullptr;
}

// A chart's view and what it answers: the hover, the title's ▾, the
// legend, the right click. chart -1: the normalised chart of every line.
TrackChart* PlotPanel::makeView(int chart) {
    auto* v = new TrackChart;
    v->setObjectName("trackChart");
    v->setNormalized(chart < 0);
    connect(v, &TrackChart::hovered, this, [this, v](double t, QPoint at, int track) { onHover(v, t, at, track); });
    connect(v, &TrackChart::hoverLeft, this, &PlotPanel::onHoverLeft);
    connect(v, &TrackChart::titleClicked, this, [this, chart] { openChartList(chart); });
    connect(v, &TrackChart::legendClicked, this, [this, v, chart](int k) { toggleLine(chart, v->tracks()[size_t(k)].id); });
    connect(v, &TrackChart::legendDoubleClicked, this, [this, v, chart](int k) { isolateLine(chart, v->tracks()[size_t(k)].id); });
    connect(v, &TrackChart::menuRequested, this, [this, v](QPoint at) { contextMenu(v, at)->popup(v->mapToGlobal(at)); });
    if (chart >= 0) connect(v, &TrackChart::doubleClicked, this, [this, chart] { focusChart(focus_.isEmpty() ? "scene:" + QString::number(chart) : QString()); });
    views_.push_back(v);
    viewChart_.push_back(chart);
    return v;
}

// The charts again, for the charts and the layout:
//   Наложить    - the scene's charts side by side (three to a row), the object's beside them;
//   Дорожками   - every chart under the one before, one set of margins so the time axes line up, the
//                 time labelled under the lowest lane only;
//   Нормировать - one chart of every line, each 0 … 1.
// With an object selected, each part has a quiet header: «Сцена», «Объект: Куб 1».
void PlotPanel::rebuildCharts() {
    if (QWidget* old = scroll_->takeWidget()) old->deleteLater();
    views_.clear();
    viewChart_.clear();
    for (PluginSection& s : plugins_) s.views.clear();
    auto* host = new QWidget;
    host->setObjectName("plotHost");
    const bool lanes = arrangement_ == Arrangement::Lanes;
    QBoxLayout* box = lanes ? static_cast<QBoxLayout*>(new QVBoxLayout(host)) : new QHBoxLayout(host);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(lanes ? 6 : 12);
    if (!focus_.isEmpty()) { // one chart in the whole width, a way back over it
        auto* back = new QToolButton;
        back->setObjectName("plotFocusBack");
        back->setText(uiText("Kinetica", "◂ Все графики"));
        back->setToolTip(uiText("Kinetica", "Вернуть все графики (или двойной щелчок по графику)"));
        back->setAutoRaise(true);
        connect(back, &QToolButton::clicked, this, [this] { focusChart({}); });
        auto* col = new QVBoxLayout;
        col->setSpacing(3);
        col->addWidget(back, 0, Qt::AlignLeft);
        QWidget* one = nullptr;
        if (focus_.startsWith("scene:")) {
            const int chart = focus_.mid(6).toInt();
            if (chart >= 0 && chart < int(charts_.size())) one = section(QString(), {chart}, false);
        } else
            for (size_t i = 0; i < plugins_.size() && !one; ++i)
                for (size_t k = 0; k < plugins_[i].charts.size() && !one; ++k)
                    if (focus_ == "plugin:" + plugins_[i].title + '\n' + plugins_[i].charts[k].title) one = pluginSection(i, int(k));
        if (one) {
            col->addWidget(one, 1);
            box->addLayout(col, 1);
            scroll_->setWidget(host);
            refreshCharts();
            refreshPluginCharts();
            return;
        }
        delete col, delete back;
        focus_.clear(); // the chart is gone (its plugin stopped, the chart removed): every chart again
    }
    if (arrangement_ == Arrangement::Normalized) {
        box->addWidget(normalizedView(), 1);
    } else {
        std::vector<int> scene, object;
        for (int i = 0; i < int(charts_.size()); ++i) (charts_[size_t(i)].object ? object : scene).push_back(i);
        const QString sceneTitle = uiText("Kinetica", "Сцена");
        const int own = shownOf(sceneTitle, chartTitles(scene)); // the room by its charts ticked - none: its header and «+» only
        box->addWidget(section(sceneTitle, scene, true), own, own ? Qt::Alignment() : Qt::AlignTop);
        if (!object.empty()) box->addWidget(section(objectTitle(), object, false), shownOf(objectTitle(), chartTitles(object)));
    }
    for (size_t i = 0; i < plugins_.size(); ++i) { // in every layout; the room by its charts ticked - none: its header only
        std::vector<QString> titles;
        for (const SharedChart& c : plugins_[i].charts) titles.push_back(c.title);
        const int shown = shownOf(plugins_[i].title, titles);
        box->addWidget(pluginSection(i), shown, shown ? Qt::Alignment() : Qt::AlignTop);
    }
    scroll_->setWidget(host);
    refreshCharts();
    showHint();
    if (lanes) emit roomWanted(host->minimumSizeHint().height() + 64); // the lanes and the bar under them
}

// Every line of every chart, each once, on one chart - each scaled to 0 … 1.
TrackChart* PlotPanel::normalizedView() {
    std::vector<PlotTrack> tracks;
    std::set<std::string> seen;
    for (const Chart& c : charts_)
        for (const std::string& id : c.ids)
            if (seen.insert(id).second) tracks.push_back(trackOf(id, c.hidden.count(id) > 0));
    TrackChart* v = makeView(-1);
    v->setTitle(uiText("Kinetica", "Все линии от 0 до 1 — форма, не величина"), false);
    v->setEmptyText(uiText("Kinetica", "Нет ни одной линии: щёлкните правой кнопкой — «Добавить величину…»"));
    v->setTracks(tracks);
    return v;
}

// A section's header, «Сцена ▾» or a plugin's: its ▾ lists the section's charts with a tick each; an unticked one is not
// drawn and holds no room (kept by the section's and the chart's titles: a plugin started again keeps the choice).
QToolButton* PlotPanel::sectionHeader(const QString& title, const std::vector<QString>& charts) {
    auto* button = new QToolButton;
    button->setObjectName("plotSection");
    button->setText(title + " ▾");
    button->setToolTip(uiText("Kinetica", "Какие графики раздела видны"));
    button->setAutoRaise(true);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setStyleSheet("QToolButton { color: #8fc1ff; font-weight: 600; padding-left: 4px; border: none; } QToolButton::menu-indicator { image: none; width: 0; }");
    auto* menu = new QMenu(button);
    menu->setObjectName("plotSectionMenu");
    for (const QString& chart : charts) {
        QAction* a = menu->addAction(chart);
        a->setCheckable(true);
        a->setChecked(!hiddenCharts_.count(title + '\n' + chart));
        connect(a, &QAction::toggled, this, [this, k = title + '\n' + chart](bool on) {
            if (on) hiddenCharts_.erase(k);
            else hiddenCharts_.insert(k);
            QTimer::singleShot(0, this, &PlotPanel::rebuildCharts); // not under the menu's own signal: the section goes
        });
    }
    button->setMenu(menu);
    return button;
}

int PlotPanel::shownOf(const QString& title, const std::vector<QString>& charts) const {
    int n = 0;
    for (const QString& chart : charts) n += !hiddenCharts_.count(title + '\n' + chart);
    return n;
}

std::vector<QString> PlotPanel::chartTitles(const std::vector<int>& charts) {
    std::vector<QString> out;
    for (int i : charts) out.push_back(titleOf(charts_[size_t(i)]));
    return out;
}

// A part of the charts: its header «Сцена ▾» / «Объект: Куб 1 ▾» (none: the chart alone in the whole width), its ticked
// charts - side by side, three to a row, or as lanes one under another - and, in the scene's part, the quiet «+».
QWidget* PlotPanel::section(const QString& header, const std::vector<int>& all, bool plus) {
    auto* w = new QWidget;
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(3);
    std::vector<int> charts;
    for (int i : all)
        if (header.isEmpty() || !hiddenCharts_.count(header + '\n' + titleOf(charts_[size_t(i)]))) charts.push_back(i);
    if (!header.isEmpty()) col->addWidget(sectionHeader(header, chartTitles(all)), 0, Qt::AlignLeft);
    auto* grid = new QGridLayout;
    grid->setSpacing(6);
    const bool lanes = arrangement_ == Arrangement::Lanes;
    const int columns = lanes ? 1 : std::clamp(int(charts.size()), 1, 3);
    for (size_t i = 0; i < charts.size(); ++i) {
        const Chart& c = charts_[size_t(charts[i])];
        std::vector<PlotTrack> tracks;
        for (const std::string& id : c.ids) tracks.push_back(trackOf(id, c.hidden.count(id) > 0));
        TrackChart* v = makeView(charts[i]);
        v->setTitle(titleOf(c));
        v->setEmptyText(c.ids.empty() ? uiText("Kinetica", "Щёлкните ▾ у заголовка и выберите, что рисовать")
                                      : uiText("Kinetica", "Ждём первых чисел — они приходят, пока сцена идёт"));
        v->setTracks(tracks);
        if (lanes) v->setMinimumHeight(96);
        grid->addWidget(v, int(i) / columns, int(i) % columns);
    }
    if (plus) {
        auto* add = new QToolButton;
        add->setObjectName("plotAddChart");
        add->setText("+");
        add->setToolTip(uiText("Kinetica", "Ещё один график: выбрать, что на нём"));
        add->setStyleSheet("QToolButton { color: #8b93a2; border: 1px dashed #3a3f48; border-radius: 6px; background: transparent; font-size: 17px; }"
                           "QToolButton:hover { color: white; border-color: #4096ff; }");
        connect(add, &QToolButton::clicked, this, &PlotPanel::addChart);
        const int rows = std::max(1, (int(charts.size()) + columns - 1) / columns);
        add->setSizePolicy(lanes ? QSizePolicy::Expanding : QSizePolicy::Fixed, lanes ? QSizePolicy::Fixed : QSizePolicy::Expanding);
        if (lanes) add->setFixedHeight(20), grid->addWidget(add, rows, 0);
        else add->setFixedWidth(22), grid->addWidget(add, 0, columns, rows, 1);
    }
    col->addLayout(grid, 1);
    return w;
}

// A plugin panel's charts as a part of the plots: its header, its charts the way the scene's are laid out, no «+». The
// header's ▾ lists the charts with a tick each: the ones unticked are not drawn (kept by the plugin's and the chart's
// titles, so a plugin started again keeps the choice).
QWidget* PlotPanel::pluginSection(size_t i, int only) {
    PluginSection& s = plugins_[i];
    auto* w = new QWidget;
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(3);
    const auto key = [&s](const SharedChart& c) { return s.title + '\n' + c.title; };
    std::vector<QString> titles;
    for (const SharedChart& c : s.charts) titles.push_back(c.title);
    col->addWidget(sectionHeader(s.title, titles), 0, Qt::AlignLeft);
    auto* grid = new QGridLayout;
    grid->setSpacing(6);
    const bool lanes = arrangement_ == Arrangement::Lanes;
    const auto drawn = [&](size_t k) { return only >= 0 ? int(k) == only : !hiddenCharts_.count(key(s.charts[k])); };
    int shown = 0;
    for (size_t k = 0; k < s.charts.size(); ++k) shown += drawn(k);
    const int columns = lanes ? 1 : std::clamp(shown, 1, 3);
    for (size_t k = 0, at = 0; k < s.charts.size(); ++k) {
        const SharedChart& c = s.charts[k];
        auto* v = new TrackChart(w);
        v->setObjectName("pluginPlotChart");
        v->setTitle(c.title, false);
        v->setAxisWords(c.xWords);
        v->setEmptyText(c.xWords.isEmpty() ? uiText("Kinetica", "линии появятся после ▶") : uiText("Kinetica", "точки появятся по ходу обучения"));
        v->setTracks(c.tracks);
        connect(v, &TrackChart::hovered, this, [this, v, i](double x, QPoint at, int track) {
            for (TrackChart* o : plugins_[i].views) o->setHoverTime(o->axisWords() == v->axisWords() ? x : std::nan(""));
            const QString text = track >= 0 && track < int(v->tracks().size()) ? plotHoverText(v->tracks()[size_t(track)], x, v->axisWords()) : QString();
            if (text.isEmpty()) QToolTip::hideText();
            else QToolTip::showText(at + QPoint(14, 12), text, v);
        });
        connect(v, &TrackChart::hoverLeft, this, [this, i] {
            for (TrackChart* o : plugins_[i].views) o->setHoverTime(std::nan(""));
            QToolTip::hideText();
        });
        connect(v, &TrackChart::doubleClicked, this, [this, k = key(c)] { focusChart(focus_.isEmpty() ? "plugin:" + k : QString()); });
        connect(v, &TrackChart::menuRequested, this, [this, v, k = key(c)](QPoint at) {
            auto* menu = new QMenu(this);
            menu->setAttribute(Qt::WA_DeleteOnClose);
            focusItem(menu, "plugin:" + k);
            connect(menu->addAction(uiText("Kinetica", "Скопировать картинку")), &QAction::triggered, this, [v] { QApplication::clipboard()->setImage(v->grab().toImage()); });
            menu->popup(v->mapToGlobal(at));
        });
        if (lanes) v->setMinimumHeight(96);
        s.views.push_back(v); // one view a chart, even unticked: the views follow the charts one by one
        if (!drawn(k)) v->hide();
        else grid->addWidget(v, int(at) / columns, int(at) % columns), ++at;
    }
    col->addLayout(grid, 1);
    return w;
}

// One chart in the whole width of the plots, the others out of the way - as a dashboard's «view panel»; double-clicked
// again, «◂ Все графики» or the menu's item, every chart back as it was.
void PlotPanel::focusChart(const QString& chart) {
    if (chart == focus_) return;
    focus_ = chart;
    QTimer::singleShot(0, this, &PlotPanel::rebuildCharts); // not under the chart's own event: it goes
}

QAction* PlotPanel::focusItem(QMenu* menu, const QString& chart) {
    const bool on = focus_ == chart;
    QAction* a = menu->addAction(on ? uiText("Kinetica", "Все графики") : uiText("Kinetica", "Во всю ширину"));
    a->setObjectName("plotMenuFocus");
    connect(a, &QAction::triggered, this, [this, chart, on] { focusChart(on ? QString() : chart); });
    return a;
}

void PlotPanel::refreshPluginCharts() {
    for (PluginSection& s : plugins_)
        for (size_t k = 0; k < s.views.size() && k < s.charts.size(); ++k) {
            const auto [t0, t1] = s.charts[k].window();
            s.views[k]->setWindow(t0, t1);
            s.views[k]->refresh();
        }
}

void PlotPanel::showPluginCharts(const QString& key, const QString& title, std::vector<SharedChart> charts) {
    const size_t had = plugins_.size();
    plugins_.erase(std::remove_if(plugins_.begin(), plugins_.end(), [&](const PluginSection& s) { return s.key == key; }), plugins_.end());
    if (charts.empty() && plugins_.size() == had) return; // a panel without charts: nothing to show, nothing to take away
    if (!charts.empty()) plugins_.push_back({key, title, std::move(charts), {}});
    if (plugins_.empty()) pluginTick_->stop();
    else pluginTick_->start();
    rebuildCharts();
}

std::vector<std::pair<QString, std::vector<TrackChart*>>> PlotPanel::pluginSections() const {
    std::vector<std::pair<QString, std::vector<TrackChart*>>> out;
    for (const PluginSection& s : plugins_) out.push_back({s.title, s.views});
    return out;
}

// The span of time on the charts: the whole run while it is short (or when asked for), else the
// last minute, sliding with the run.
std::pair<double, double> PlotPanel::timeWindow() const {
    if (frames_ == 0) return {0.0, 1.0};
    const double from = wholeRun_ || now_ - start_ <= kLiveSeconds ? start_ : now_ - kLiveSeconds;
    return {from, std::max(now_, from + 1e-3)};
}

// The lanes share their margins, so that one moment is one x on every lane; the lowest one labels the time.
void PlotPanel::alignLanes() {
    int left = 0, right = 0;
    for (const TrackChart* v : views_) left = std::max(left, v->neededLeftMargin()), right = std::max(right, v->neededRightMargin());
    for (size_t i = 0; i < views_.size(); ++i) {
        views_[i]->setSharedMargins(left, right);
        views_[i]->setTimeAxisVisible(i + 1 == views_.size());
    }
}

// At most every 40 ms, while frames come: the window slides, every chart takes its new samples, the
// lanes line up again, an open list shows fresh values.
void PlotPanel::refreshCharts() {
    const auto [t0, t1] = timeWindow();
    for (TrackChart* v : views_) {
        v->setWindow(t0, t1);
        v->refresh();
    }
    if (arrangement_ == Arrangement::Lanes) alignLanes();
    if (!checklist_->isVisible() && !chartList_->isVisible()) return;
    std::map<std::string, QString> values;
    for (const auto& [id, s] : series_) values[id] = formatValue(s.latest());
    checklist_->updateValues(values);
    chartList_->updateValues(values);
}

// Before the first frame: one quiet line instead of the charts - nothing to press, nothing to choose.
void PlotPanel::showHint() {
    const bool shown = frames_ > 0 || !plugins_.empty(); // a plugin's chart needs no run
    scroll_->setVisible(shown);
    placeholder_->setVisible(!shown);
}

// --- The cursor and the hover ---------------------------------------------------------------------


// The mouse over a chart: the dashed line and the dots on every chart, and the line under the mouse
// in words.
void PlotPanel::onHover(TrackChart* view, double t, QPoint globalPos, int track) {
    for (TrackChart* v : views_) v->setHoverTime(t);
    const QString text = tooltipText(view, t, track);
    if (text.isEmpty()) QToolTip::hideText();
    else QToolTip::showText(globalPos + QPoint(14, 12), text, view);
}

void PlotPanel::onHoverLeft() {
    for (TrackChart* v : views_) v->setHoverTime(std::nan(""));
    QToolTip::hideText();
}

// «Кинетическая энергия: 1,23 Дж в 2,40 с» - the recorded value, also on the normalised chart.
QString PlotPanel::tooltipText(const TrackChart* view, double t, int track) const {
    if (!view || track < 0 || track >= int(view->tracks().size())) return QString();
    const PlotTrack& line = view->tracks()[size_t(track)];
    const double v = line.series ? line.series->valueAt(t) : std::nan("");
    if (!std::isfinite(v)) return QString();
    const QString value = formatValue(v) + (line.unit.isEmpty() ? QString() : " " + line.unit);
    return QString(uiText("Kinetica", "%1: %2 в %3 с")).arg(line.name, value, QString::number(t, 'f', 2).replace('.', ','));
}

// --- The CSV --------------------------------------------------------------------------------------

namespace {

// A number of the table in the classic locale ('.' always), as short as gives back the float it was
// measured in: the simulation's numbers are floats, more digits would only be noise.
QString cell(double v) { return QString::fromStdString(ke::numberText(float(v))); }

// Channels as a table: the time first, then a column each; a row per moment any of them was
// recorded, an empty cell where one was not. For people («t, с», names with units, a byte-order mark
// so that a spreadsheet reads the Russian) or for programs ("t", the channels' ids).
bool writeTable(const QString& path, const std::vector<std::pair<QString, const PlotSeries*>>& columns, bool people) {
    QFile f(path);
    if (columns.empty() || !f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    std::vector<double> times;
    for (const auto& [head, s] : columns)
        for (size_t i = 0; i < s->size(); ++i) times.push_back(s->time(i));
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    QTextStream out(&f);
    out.setGenerateByteOrderMark(people);
    auto quoted = [](QString s) { return "\"" + s.replace("\"", "\"\"") + "\""; };
    out << (people ? quoted(uiText("Kinetica", "t, с")) : QString("t"));
    for (const auto& [head, s] : columns) out << "," << quoted(head);
    out << "\n";
    std::vector<size_t> at(columns.size(), 0);
    for (double t : times) {
        out << cell(t);
        for (size_t c = 0; c < columns.size(); ++c) {
            const PlotSeries& s = *columns[c].second;
            while (at[c] < s.size() && s.time(at[c]) < t) ++at[c];
            out << ",";
            if (at[c] < s.size() && s.time(at[c]) == t && std::isfinite(s.value(at[c]))) out << cell(s.value(at[c]));
        }
        out << "\n";
    }
    return true;
}

} // namespace

// Every channel («Экспорт всех каналов CSV…»): first the energy chart's three lines, then the rest by
// id - a row for every simulated frame.
bool PlotPanel::writeCsv(const QString& path) const {
    const std::vector<std::string>& first = defaultChannels();
    std::vector<std::pair<QString, const PlotSeries*>> columns;
    for (const std::string& id : first)
        if (const PlotSeries* s = series(id)) columns.push_back({QString::fromStdString(id), s});
    for (const auto& [id, s] : series_)
        if (!contains(first, id)) columns.push_back({QString::fromStdString(id), &s});
    return writeTable(path, columns, false);
}

// The lines on the charts (the shown ones, each once), headed with their names and units.
bool PlotPanel::writeVisibleCsv(const QString& path) const {
    std::vector<std::pair<QString, const PlotSeries*>> columns;
    std::set<std::string> seen;
    for (const Chart& c : charts_)
        for (const std::string& id : c.ids) {
            const PlotSeries* s = series(id);
            if (!s || c.hidden.count(id) || !seen.insert(id).second) continue;
            const PlotChannel card = catalog_.card(id);
            columns.push_back({card.unit.isEmpty() ? card.name : card.name + ", " + card.unit, s});
        }
    return writeTable(path, columns, true);
}
