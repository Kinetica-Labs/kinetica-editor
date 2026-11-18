#pragma once
// The plots' research instruments: the scene or one object (by its entity id, stable while the scene
// lives), one of its quantities, a raw time window, and that quantity's histogram with its sample
// statistics (PlotStatistics.h). Histograms count frames, not elapsed time: every recorded frame weighs
// the same. The window turns the object chosen into channel requests (MainWindowPlots.cpp); the panel
// gives the raw recorded samples (PlotPanel.h).
#include "HistogramChart.h"
#include "PlotChannels.h"

#include <QWidget>

class QComboBox;
class QLabel;

class PlotResearch : public QWidget {
    Q_OBJECT
public:
    using Objects = std::vector<std::pair<int, QString>>;
    explicit PlotResearch(QWidget* parent = nullptr);
    void setObjects(const Objects& objects, int selected);
    void setChannels(const std::vector<PlotChannel>& channels);
    void resetObjects();
    int body() const; // an object's entity id; -1: the scene (-2 in the selector follows the mouse / inspector selection)
    QString bodyLabel() const;
    std::string channel() const;
    double windowSeconds() const;
    int binCount() const;
    bool histogram() const;
    void setResult(const PlotStatistics& result);
    bool writeCsv(const QString& path) const;
    const PlotStatistics& result() const { return chart_->result(); }

signals:
    void bodyWanted(int body);
    void channelWanted(std::string id);
    void analysisChanged();

private:
    void buildControls();
    void buildDistribution();
    void filterChannels();
    void chooseBody();
    void exportCsv();
    QComboBox *object_ = nullptr, *quantity_ = nullptr, *view_ = nullptr, *window_ = nullptr, *bins_ = nullptr;
    QWidget* distribution_ = nullptr;
    HistogramChart* chart_ = nullptr;
    QLabel* statistics_ = nullptr;
    Objects objects_;
    int selected_ = -1;
    std::vector<PlotChannel> channels_;
};
