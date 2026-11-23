#pragma once
// The plots at the bottom of the window: what the scene does over time, with nothing to set up.
//
// Zero clicks. The first ▶ opens the plots by itself (MainWindow) and the scene's energy is drawn
// at once: «Энергия сцены — движения, высоты, полная (Дж)» - its kinetic and potential parts and
// their sum, the mechanical energy the simulation measures (sim/Channels.h). Selecting a body - what a person does anyway to
// look at it - adds its own charts, «Высота, м» and «Скорость, м/с», under «Объект: Куб 1»;
// selecting nothing takes them away. An IK goal selected brings its arm's charts instead: a chart for
// each joint (its angle and its servo's target) and the goal's miss (MainWindowPlots.cpp).
//
// Everything else is optional and quiet, on the charts themselves (TrackChart):
//   - the ▾ after a chart's title opens the list of quantities for that chart: a click on a row shows
//     that one instead, its ＋ adds it as another line;
//   - a click on a legend entry hides or shows its line, a double click shows it alone;
//   - a right click: add or remove a line, remove the chart, the layout, the CSV, a copy of the
//     picture, that moment in the Laboratory;
//   - the «+» after the last chart adds an empty chart with its list open;
//   - «Ещё величины…» under the charts: every quantity at once, with checkboxes;
//   - «Что показывать» at the left: every section - «Сцена», the object's, a plugin's - and its charts with a tick
//     each, a line to find one; unticked charts are not drawn and hold no room, nor a section with none ticked;
//   - a double click on a chart (not its legend): it alone in the whole width, «◂ Все графики» over it; again - all.
// Three layouts: «Наложить» (charts side by side, each with its lines of one unit together),
// «Дорожками» (the charts stacked over one shared time axis), «Нормировать» (every line on one chart,
// each scaled to 0 … 1: the shape of a quantity, not its size). Never two y-axes: a quantity of
// another unit gets a chart of its own.
//
// Bounded cost, however long the run: each channel's history is a PlotSeries (the whole run, thinned
// when old, for the lines on the charts; the last minute for the rest); the charts show the last
// minute once the run is longer (the whole run on request) and draw a few thousand points at most;
// they repaint at most 25 times a second.
#include "ChannelList.h"
#include "PlotChannels.h"
#include "PlotSeries.h"
#include "TrackChart.h"

#include "core/Probe.h"
#include "sim/Channels.h"

#include <QWidget>

#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

class QButtonGroup;
class QLabel;
class QMenu;
class QScrollArea;
class QTimer;
class QToolButton;

class PlotPanel : public QWidget {
    Q_OBJECT
public:
    enum class Arrangement { Overlay, Lanes, Normalized };

    // One chart as the reader sees it.
    struct Chart {
        QString title;                 // fixed words («Энергия сцены — …»); empty: named after its lines
        std::vector<std::string> ids;  // its lines
        std::set<std::string> hidden;  // the lines switched off in the legend
        bool object = false;           // one of the selected object's charts: they go with the selection
    };

    explicit PlotPanel(QWidget* parent = nullptr);

    // --- Recording ---
    // A new scene: the history goes; the scene's charts as the reader left them come back (or the
    // default ones: the scene's energy).
    void setScene(const std::string& name);
    // One frame: every channel recorded, the charts repainted soon.
    void append(double t, const std::vector<ke::Probe::Channel>& probe, const std::vector<ke::Measurement>& measurements);
    void clear();
    bool empty() const { return frames_ == 0; }
    void refreshCharts(); // the charts take their new samples now (their timer does it 25 times a second)
    size_t bytes() const;            // what the history holds

    // --- Files ---
    bool writeCsv(const QString& path) const;        // every channel, one column each
    bool writeVisibleCsv(const QString& path) const; // the lines on the charts: «t, с», then «name, unit»

    // The selected object («Куб 1»; empty: none): its height and speed charts, in a section of their own.
    void showObject(const QString& label);
    // Any section that goes with the selection, the same way: its key (empty: none), its header, its charts and
    // the prefixes of the channels that are its own - an arm's goal: a chart for each motor and its miss.
    void showSection(const QString& key, const QString& title, const std::vector<Chart>& charts, const std::vector<std::string>& prefixes);

    // A plugin panel's charts (a training's best and mean by generation, a controller's error): a section of their own
    // after the scene's, headed with the panel's title, the same lines over the same series, live. `key` names the panel;
    // no charts - the panel is gone: its section goes (the series are shared, so nothing it points to is gone before).
    void showPluginCharts(const QString& key, const QString& title, std::vector<SharedChart> charts);

    // --- What the reader may choose (never needed) ---
    QWidget* moreButton() const;       // «Ещё величины…»
    QWidget* arrangementSwitch() const { return switch_; }
    void setArrangement(Arrangement a);
    void setWholeRun(bool on);         // the whole run on the charts instead of the last minute
    void showChannel(const std::string& id);              // a line on the chart of its unit, or a new chart
    void setChannelShown(const std::string& id, bool on); // the checklist's checkbox
    bool showsChannel(const std::string& id) const;
    void applyPreset(ChannelList::Preset preset);
    void addChart();                                      // the «+»: an empty chart, its list open
    void openChartList(int chart);                        // the ▾ after a chart's title
    void switchChart(int chart, const std::string& id);   // the list: this quantity instead
    void setOnChart(int chart, const std::string& id, bool on); // the list's ＋ / ✓
    void toggleLine(int chart, const std::string& id);    // a click on a legend entry (-1: every chart)
    void isolateLine(int chart, const std::string& id);   // a double click on it
    void removeChart(int chart);
    QMenu* contextMenu(TrackChart* view, const QPoint& at); // the right click's menu, built, not shown


    // --- For the self-test ---
    const std::vector<Chart>& charts() const { return charts_; }
    const std::vector<TrackChart*>& views() const { return views_; }
    int chartOf(const TrackChart* view) const; // -1: the normalised chart of everything
    QString objectTitle() const;               // the object section's header ("" none)
    const QString& objectKey() const { return object_; } // the section's key ("" none)
    ChannelList* chartList() const { return chartList_; }
    QLabel* placeholder() const { return placeholder_; }
    const PlotSeries* series(const std::string& id) const;
    // The plugins' charts on screen: their header and views (the self-test reads them).
    std::vector<std::pair<QString, std::vector<TrackChart*>>> pluginSections() const;
    PlotCatalog& catalog() { return catalog_; }
    QString tooltipText(const TrackChart* view, double t, int track) const;

signals:
    void saveCsvRequested();     // the right click's «Сохранить CSV…»
    void roomWanted(int height); // the lanes need this much height to be read

private:
    void buildBar();
    void record(double t, const std::string& id, double v);
    void addEnergyParts(double t, const std::vector<ke::Measurement>& measurements);
    bool kept(const std::string& id) const;
    void updateKeep();
    void chartsChanged(); // the reader changed the charts: the history kept, the views and lists anew
    void forgetColours();
    std::vector<ChannelList::Row> rowsFor(int chart);
    void refreshLists();
    QString titleOf(const Chart& c);
    QString unitOf(const Chart& c);
    PlotTrack trackOf(const std::string& id, bool hidden);
    void rebuildCharts();
    QWidget* section(const QString& header, const std::vector<int>& charts, bool plus);
    TrackChart* normalizedView(const std::vector<int>& charts); // the charts ticked, one chart of their lines
    TrackChart* makeView(int chart);
    TrackChart* viewOf(int chart) const;
    std::pair<double, double> timeWindow() const;
    void alignLanes();
    void onHover(TrackChart* view, double t, QPoint globalPos, int track);
    void onHoverLeft();
    QLabel* sectionHeader(const QString& title);
    void fillTree(const std::vector<std::pair<QString, std::vector<QString>>>& sections); // «Что показывать»: section -> charts
    void filterTree(const QString& words);
    int shownOf(const QString& title, const std::vector<QString>& charts) const;          // its charts not unticked
    std::vector<QString> chartTitles(const std::vector<int>& charts);
    QWidget* pluginSection(size_t i, int only = -1); // only: that one chart of it (the whole width), else the ticked ones
    void focusChart(const QString& chart);           // one chart in the whole width («scene:3», «plugin:<section>\n<chart>»), empty: all
    QAction* focusItem(QMenu* menu, const QString& chart);
    void refreshPluginCharts();
    void showHint(); // before the first frame: one quiet line instead of the charts

    // The bar and the lists.
    QToolButton* more_ = nullptr;
    QWidget* switch_ = nullptr;
    QButtonGroup* switchGroup_ = nullptr;
    ChannelList* checklist_ = nullptr;
    ChannelList* chartList_ = nullptr;
    int listChart_ = -1; // the chart whose list is open
    // The charts on screen.
    QScrollArea* scroll_ = nullptr;
    QLabel* placeholder_ = nullptr;
    QTimer* repaint_ = nullptr;
    struct PluginSection {
        QString key, title;
        std::vector<SharedChart> charts;
        std::vector<TrackChart*> views;
    };
    std::vector<PluginSection> plugins_; // a plugin panel's charts, after the scene's: they come and go with the panel
    QString focus_;                        // the chart in the whole width (focusChart), empty: every chart
    std::set<QString> hiddenCharts_; // the charts unticked in «Что показывать»: «section\nchart»
    std::set<QString> folded_;       // its sections folded, by title
    class QSplitter* body_ = nullptr;     // «Что показывать» | the charts
    class QTreeWidget* tree_ = nullptr;
    class QLineEdit* find_ = nullptr;
    QTimer* rebuildSoon_ = nullptr;
    QTimer* pluginTick_ = nullptr;       // their series are filled from outside the frames: repaint at 25 a second while there are any
    std::vector<TrackChart*> views_;
    std::vector<int> viewChart_; // the chart each view shows (-1: the normalised one)
    // What is drawn.
    std::vector<Chart> charts_;
    bool touched_ = false; // the reader changed the charts of this scene
    std::map<std::string, std::vector<Chart>> chartsByScene_;
    std::string scene_;
    QString object_;                        // the section's key (the object's label)
    QString sectionTitle_;                  // its header
    std::vector<Chart> sectionCharts_;      // its charts as first shown («Сбросить» brings them back)
    std::vector<std::string> sectionPrefixes_; // the ids that are its own start so
    bool ownChannel(const std::string& id) const;
    Arrangement arrangement_ = Arrangement::Overlay;
    bool wholeRun_ = false;
    // What is recorded.
    PlotCatalog catalog_;
    std::map<std::string, PlotSeries> series_;
    std::set<std::string> reported_;      // the channels of the frame being recorded
    bool fresh_ = false;                  // a channel appeared in this frame
    double start_ = 0, now_ = 0;
    long frames_ = 0;
};
