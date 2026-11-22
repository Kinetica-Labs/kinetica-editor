#pragma once
// The Sequencer - a tab of the bottom panel, a simple sequencer over the scene's sequences (scene/Animation.h).
// The bar: which sequence is open, whether ▶ plays it (its bodies led as an animation), ⇤ to its start, ▶ a preview,
// the loop, ◆ a key of the selected objects where they stand (I), ◈ Auto Key (a hand's move or turn of an object keys it at
// the playhead by itself, in the same undo step), ⏺ a take of the physics (sim_record), 〰 the curves,
// and a filter of the tracks. Below it one of two views (SequencerViews.h): the tracks, folding by the scene's groups, or
// one track's curves. Dragging the playhead or ▶ in edit mode shows the poses (SceneBuilder's preview) and then makes
// one edit, sequence_seek; in play the playhead follows the world only while the timeline has something of the run - a
// take being recorded (⏺), the open sequence playing in it (▶ в Пуске): a plain «Пуск» keeps nothing, and its playhead
// stays. Every change is an operation of the API (sequence_*), said in the console as a Python line.
#include <QElapsedTimer>
#include <QWidget>

#include <cstdint>

class EditorOps;
class SceneBuilder;
namespace sequencer { struct Axis; }
class SequencerCurves;
class SequencerTimeline;
class QComboBox;
class QJsonObject;
class QLabel;
class QLineEdit;
class QStackedLayout;
class QString;
class QTimer;
class QToolButton;

class SequencerPanel : public QWidget {
    Q_OBJECT
public:
    SequencerPanel(EditorOps* ops, SceneBuilder* builder, QWidget* parent = nullptr);
    void sync();                    // the sequences as the scene has them now
    void setPlayTime(double time);  // in play: where the world is (its newest frame)
    double atLeast() const;         // the views' span: in a run, past the world's time; the play range's end in sight
    // The views' time axis, one for the tracks and the curves: the whole sequence, or the part zoomed into - the wheel
    // (over the tracks' ruler or with Ctrl), the middle button moves it, F over the Sequencer shows the whole again.
    sequencer::Axis axis(int width) const;
    void zoomTime(double factor, double at); // the span × factor; the time `at` stays where it is
    void panTime(double seconds);
    void fitTime();                 // the whole sequence, and the curves' values fitted (F)
    void setEnd(double seconds);    // the scene's play range end (world_set: world duration); 0: none
    void setFrom(double seconds);   // where a take starts (world_set: world record_from), before the end
    void keySelected();             // ◆: the selected objects keyed at the playhead
    double playhead() const;

    // What the views do through the panel.
    SceneBuilder* builder() const { return builder_; }
    bool scrubbing() const { return scrubbing_; }
    void run(const QString& name, const QJsonObject& args);
    void seek(double time);         // the edit: sequence_seek
    void scrub(double time);        // the preview, no edit yet
    void endScrub();                // the preview ends: one sequence_seek
    void keyMenu(uint32_t object, double time, const QPoint& at); // a key's menu: how it goes on, the playhead to it, delete
    uint32_t pickedObject() const;  // the key picked on the tracks (0: none)
    void addBarAction(QAction* a, const QString& text); // a window's action as a button of the bar (⇔ the whole width)

protected:
    // Space over the Sequencer plays its sequence and stops it where it is, as a video editor's timeline does; over the 3D
    // view it is «Пуск» still - the keys go to what the mouse is over. Nothing to play: «Пуск» too.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool spacePlays() const; // the sequence has keys to play in edit mode, or it is playing
    void togglePreview(bool on);
    void showCurves(bool on);

    EditorOps* ops_;
    SceneBuilder* builder_;
    QComboBox* sequences_ = nullptr;
    QToolButton *add_ = nullptr, *start_ = nullptr, *play_ = nullptr, *loop_ = nullptr, *key_ = nullptr, *autoKey_ = nullptr, *record_ = nullptr, *plays_ = nullptr,
                *curves_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QLabel *time_ = nullptr, *note_ = nullptr;
    class QDoubleSpinBox* end_ = nullptr; // the scene's play range end (world duration), s
    class QDoubleSpinBox* from_ = nullptr; // where a take starts (world record_from), s
    QStackedLayout* views_ = nullptr;
    SequencerTimeline* timeline_ = nullptr;
    SequencerCurves* curvesView_ = nullptr;
    QTimer* previewTimer_ = nullptr;
    QElapsedTimer previewClock_;
    QElapsedTimer recordClock_;     // a second press within 0.5 s of the take's start: a double click, not a stop
    double previewFrom_ = 0, scrubTime_ = 0, playTime_ = 0, liveTime_ = 0;
    bool scrubbing_ = false;
    double zoomFrom_ = 0, zoomSpan_ = 0; // the part of the time zoomed into, s (span 0: the whole)
};
