#pragma once
// The Sequencer's two views (SequencerPanel.h), on one time axis. The tracks (SequencerTimeline.cpp): a tree of the open
// sequence's bound objects - «Все дорожки», then each root of the scene's hierarchy (a robot, a group, an array) and its
// objects -, every row folding; a folded row shows its tracks' keys summed as ticks. Only the rows in sight are drawn and
// the tree is built again only when the sequence changes, so a hundred thousand tracks scroll as ten do. The curves
// (SequencerCurves.cpp): one track's place or turn against time, sampled as the world plays it; a key dragged up and down
// changes its value, along its time.
#include "scene/SceneGraph.h"

#include <QWidget>

#include <array>
#include <cstdint>
#include <unordered_set>
#include <vector>

class SequencerPanel;
class QScrollBar;

namespace sequencer {

constexpr int kNames = 180, kRuler = 22, kRow = 22, kPad = 12;

// 0 s to `span` across a view's width right of the names' column (span: at least 5 s, a second past the last key).
struct Axis {
    int width = 0;
    double span = 5;
    double x(double t) const { return kNames + kPad + t * double(width - kNames - 2 * kPad) / span; }
    double time(double x) const;
};
Axis axisOf(const ke::SequenceObject* q, int width);
void drawRuler(QPainter& p, const Axis& a, const QPalette& pal, int height); // seconds, and faint lines down the view
void drawPlayhead(QPainter& p, const Axis& a, double time, int height);
void drawKey(QPainter& p, const QPointF& at, ke::AnimInterp interp, const QColor& fill, bool picked, const QColor& outline);

} // namespace sequencer

class SequencerTimeline : public QWidget {
public:
    SequencerTimeline(SequencerPanel* panel, QScrollBar* scroll);
    void refresh();                    // the tree again if the sequence or the filter changed; repainted
    void setFilter(const QString& words);
    void toggle(int row);              // a row's fold
    int rows() const { return int(rows_.size()); }
    QPointF pointOf(uint32_t object, double time) const; // where a key is drawn; (-1, -1): not in sight
    QPointF foldOf(int row) const;     // where a row's fold arrow is
    uint32_t pickedObject() const { return keyObject_; }
    double pickedTime() const { return keyTime_; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    struct Row {
        enum Kind { All, Group, Track } kind = Track;
        int depth = 0;
        uint32_t id = 0;            // the group's or the track's object (All: 0)
        size_t track = 0;           // Track: its index in the sequence
        int count = 0;              // All, Group: their tracks
        bool lost = false;          // Track: its object is gone
        QString name;
        std::vector<float> times;   // All, Group: their keys' times, one a 1/240 s
    };
    void rebuild();
    int rowAt(double y) const;      // -1: the ruler or below the rows
    bool keyAt(const QPointF& at, uint32_t& object, double& time) const;
    void fitScroll();

    SequencerPanel* panel_;
    QScrollBar* scroll_;
    std::vector<Row> rows_;         // the tree as it stands unfolded, in order
    std::unordered_set<uint32_t> open_; // unfolded groups
    bool allOpen_ = true;
    uint32_t decidedFor_ = 0;       // the sequence whose first fold was chosen (open when it has few tracks)
    QString filter_;
    uint64_t signature_ = 0;        // of the sequence the tree was built from
    uint32_t keyObject_ = 0;        // the key picked: Delete removes it
    double keyTime_ = -1;
};

class SequencerCurves : public QWidget {
public:
    explicit SequencerCurves(SequencerPanel* panel);
    uint32_t object() const;        // the track shown: the selected object's, else the picked key's, else the first
    QPointF pointOf(size_t key, int channel) const; // where a key's channel is drawn (the self-test's hands)

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    const ke::SequenceTrack* track() const;
    double valueOf(const ke::AnimKey& k, int channel) const;    // place m or turn degrees
    double valueAt(const std::vector<ke::AnimKey>& keys, double t, int channel) const;
    double yOf(double v) const;
    double vAt(double y) const;
    void fitRange(const std::vector<ke::AnimKey>& keys);
    bool dotAt(const QPointF& at, size_t& key, int& channel) const;

    SequencerPanel* panel_;
    bool turn_ = false;             // the turn's curves (degrees), not the place's (m)
    std::array<bool, 3> shown_{true, true, true};
    double lo_ = 0, hi_ = 1;        // the values from the bottom to the top
    // A drag of a key: which and along what, the keys as they will be.
    std::vector<ke::AnimKey> draft_;
    long dragKey_ = -1;
    int dragChannel_ = 0;
    QPointF pressAt_;
    enum class Drag { None, Value, Time } drag_ = Drag::None;
};
