#pragma once
// The Sequencer - a tab of the bottom panel, a simple Unreal Sequencer over the scene's sequences (scene/Animation.h).
// The bar: which sequence is open, whether ▶ plays it (its bodies led as an animation), ⇤ to its start, ▶ a preview,
// the loop, ◆ a key of the selected objects where they stand (I), ⏺ a take of the physics (sim_record), 〰 the curves,
// and a filter of the tracks. Below it one of two views (SequencerViews.h): the tracks, folding by the scene's groups, or
// one track's curves. Dragging the playhead or ▶ in edit mode shows the poses (SceneBuilder's preview) and then makes
// one edit, sequence_seek; in play the playhead follows the world, and dragged back it shows the run's frames kept in
// memory (FrameHistory, the scene pauses) - the window's one timeline; «● Вживую» comes back. Every change is an operation of the API (sequence_*),
// said in the console as a Python line.
#include <QElapsedTimer>
#include <QWidget>

#include <cstdint>

class EditorOps;
class SceneBuilder;
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
    void showPast(bool past, double time); // a kept frame of the run on screen (FrameHistory), or live again
    double atLeast() const;         // the views' span: in a run, past the world's time
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

signals:
    void pastWanted(double time);   // the playhead taken back while a run is on screen: show its frame then
    void liveWanted();              // «● Вживую»: the newest frame, the run goes on

private:
    void togglePreview(bool on);
    void showCurves(bool on);

    EditorOps* ops_;
    SceneBuilder* builder_;
    QComboBox* sequences_ = nullptr;
    QToolButton *add_ = nullptr, *start_ = nullptr, *play_ = nullptr, *loop_ = nullptr, *key_ = nullptr, *record_ = nullptr, *plays_ = nullptr,
                *curves_ = nullptr, *live_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QLabel *time_ = nullptr, *note_ = nullptr;
    QStackedLayout* views_ = nullptr;
    SequencerTimeline* timeline_ = nullptr;
    SequencerCurves* curvesView_ = nullptr;
    QTimer* previewTimer_ = nullptr;
    QElapsedTimer previewClock_;
    QElapsedTimer recordClock_;     // a second press within 0.5 s of the take's start: a double click, not a stop
    double previewFrom_ = 0, scrubTime_ = 0, playTime_ = 0, liveTime_ = 0;
    bool scrubbing_ = false, past_ = false;
};
